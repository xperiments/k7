/*
 * T32: the Label sheet reader and the slot resolver, host tier.
 *
 * The sheet is built here, byte by byte, by a builder that shares no code with the module:
 * the test owns the layout and the CRC, so a change in the module cannot make the test agree
 * with it. The published CRC-32 vector anchors both implementations to something outside this
 * repository.
 *
 * What is exercised: every validation rule in docs/specs/label-templates.md §3.5, the pick
 * rule, every accessor, the UTF-8 character cut, and every formatting rule of §5 against a
 * Snapshot image the test also builds.
 */
#include "test_util.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "label.h"
#include "label_values.h"
#include "snapshot.h"

/* ------------------------------------------------------------ independent bits */

static uint32_t crc32_ref(const uint8_t *p, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

typedef struct {
    uint8_t *p;
    size_t   n;
    size_t   cap;
} tbuf_t;

static void need(tbuf_t *b, size_t extra)
{
    if (b->n + extra <= b->cap) return;
    size_t cap = b->cap ? b->cap * 2 : 256;
    while (cap < b->n + extra) cap *= 2;
    uint8_t *q = realloc(b->p, cap);
    if (!q) abort();
    b->p = q;
    b->cap = cap;
}

static void put(tbuf_t *b, const void *src, size_t n)
{
    need(b, n);
    memcpy(b->p + b->n, src, n);
    b->n += n;
}

static void put8(tbuf_t *b, uint8_t v) { put(b, &v, 1); }

static void put16(tbuf_t *b, uint16_t v)
{
    uint8_t t[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    put(b, t, 2);
}

static void put32(tbuf_t *b, uint32_t v)
{
    uint8_t t[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    put(b, t, 4);
}

static void put64(tbuf_t *b, uint64_t v)
{
    uint8_t t[8];
    for (int i = 0; i < 8; i++) t[i] = (uint8_t)(v >> (8 * i));
    put(b, t, 8);
}

static void put24(tbuf_t *b, uint32_t v)
{
    uint8_t t[3] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16)};
    put(b, t, 3);
}

static void patch32(uint8_t *p, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[at + i] = (uint8_t)(v >> (8 * i));
}

/* ------------------------------------------------------------ sheet builder */

#define T_AT(t)        (32u + 48u * (size_t)(t))
#define P_AT(i)        (32u + 48u * 2u + 24u * (size_t)(i))
#define S_AT(i)        (32u + 48u * 2u + 24u * 5u + 8u * (size_t)(i))
#define POOL_AT         (32u + 48u * 2u + 24u * 5u + 8u * 2u)

enum {
    POOL_NIGHT = 0,
    POOL_SECOND = 12,   /* "Night Drive\0" */
    POOL_SIDE = 19,     /* "Second\0" */
    POOL_SONG = 26,     /* "SIDE A\0" */
    POOL_BPM = 37,      /* "song_title\0" */
    POOL_N = 41,        /* "bpm\0" */
};

static const char k_pool[POOL_N] =
    "Night Drive\0Second\0SIDE A\0song_title\0bpm\0";

/* One prim record, as the test writes it. */
static void put_prim(tbuf_t *b, uint8_t kind, uint8_t fill, uint8_t font, uint8_t align,
                     int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t r, int16_t rot10,
                     uint32_t text, uint32_t literal)
{
    put8(b, kind);
    put8(b, fill);
    put8(b, font);
    put8(b, align);
    put16(b, (uint16_t)x);
    put16(b, (uint16_t)y);
    put16(b, w);
    put16(b, h);
    put16(b, r);
    put16(b, (uint16_t)rot10);
    put32(b, text);
    put32(b, literal);
}

static void put_template(tbuf_t *b, uint32_t name, uint16_t prim_start, uint16_t prim_count,
                         uint16_t slot_start, uint16_t slot_count, uint8_t f0, uint8_t f1,
                         uint8_t f2)
{
    put32(b, name);
    put16(b, prim_start);
    put16(b, prim_count);
    put16(b, slot_start);
    put16(b, slot_count);
    put8(b, f0);
    put8(b, f1);
    put8(b, f2);
    put8(b, 0);              /* reserved */
    put32(b, 0xF2EDE3);      /* paper */
    put32(b, 0x24211D);      /* ink */
    put32(b, 0xB9AC8E);      /* rule */
    put32(b, 0xC68B2E);      /* band 0 */
    put32(b, 0xA24E2C);      /* band 1 */
    put32(b, 0x8A5D34);      /* band 2 */
    put32(b, 0x6B7A2F);      /* band 3 */
    put32(b, 0);             /* reserved */
}

/* The canonical sheet: two designs, five prims, two slots, one shared pool. Its header's
 * CRC is the reference implementation, not the module's. */
static uint8_t *build_sheet(size_t *n_out)
{
    tbuf_t b = {0};
    put(&b, "K7LBL\0\0\0", 8);
    put16(&b, 1);              /* version */
    put16(&b, 32);             /* header size */
    put32(&b, 0);              /* total, patched */
    put32(&b, 0);              /* crc, patched */
    put16(&b, 748);
    put16(&b, 359);
    put16(&b, 2);              /* templates */
    put16(&b, 5);              /* prims */
    put16(&b, 2);              /* slots */
    put16(&b, POOL_N);         /* pool size */

    put_template(&b, POOL_NIGHT, 0, 4, 0, 2, 0, 1, 2);
    put_template(&b, POOL_SECOND, 4, 1, 2, 0, 1, 1, 1);

    put_prim(&b, LABEL_RECT, LABEL_FILL_PAPER, 0, 0, 0, 0, 748, 359, 19, 0, 0xFFFFFFFFu, 0);
    put_prim(&b, LABEL_RECT, LABEL_FILL_LITERAL, 0, 0, 700, 292, 20, 20, 3, 0, 0xFFFFFFFFu,
             0xC63A2E);
    put_prim(&b, LABEL_TEXT_STATIC, LABEL_FILL_INK, LABEL_FONT_CAPTION, LABEL_ALIGN_RIGHT,
             32, 20, 200, 0, 0, 0, POOL_SIDE, 0);
    put_prim(&b, LABEL_TEXT_SLOT, LABEL_FILL_INK, LABEL_FONT_TITLE, LABEL_ALIGN_LEFT,
             32, 282, 400, 0, 0, 900, POOL_SONG, 0);
    put_prim(&b, LABEL_RECT, LABEL_FILL_INK, 0, 0, 0, 0, 748, 359, 19, 0, 0xFFFFFFFFu, 0);

    put32(&b, POOL_SONG);      /* slot 0: song_title */
    put8(&b, 24);
    put8(&b, 0); put8(&b, 0); put8(&b, 0);
    put32(&b, POOL_BPM);       /* slot 1: bpm */
    put8(&b, 3);
    put8(&b, 0); put8(&b, 0); put8(&b, 0);

    put(&b, k_pool, POOL_N);

    patch32(b.p, 12, (uint32_t)b.n);
    patch32(b.p, 16, crc32_ref(b.p + 32, b.n - 32));
    *n_out = b.n;
    return b.p;
}

/* A copy of the canonical sheet with one field changed and its CRC recomputed, so the test
 * fails for the reason it names and not for the checksum. */
static uint8_t *mutate(size_t at, uint32_t value, size_t width, size_t *n_out)
{
    uint8_t *s = build_sheet(n_out);
    for (size_t i = 0; i < width; i++) s[at + i] = (uint8_t)(value >> (8 * i));
    patch32(s, 16, crc32_ref(s + 32, *n_out - 32));
    return s;
}

static bool opens(const uint8_t *blob, size_t n)
{
    label_sheet_t *s = NULL;
    char why[96];
    if (label_open_why(blob, n, 3, &s, why, sizeof why) != LABEL_OK) return false;
    label_close(s);
    return true;
}

static bool refuses(const uint8_t *blob, size_t n, const char *needle)
{
    label_sheet_t *s = NULL;
    char why[96];
    if (label_open_why(blob, n, 3, &s, why, sizeof why) == LABEL_OK) {
        label_close(s);
        return false;
    }
    return needle && strstr(why, needle) != NULL;
}

/* ------------------------------------------------------------ reader */

static void test_reader(void)
{
    size_t n = 0;
    uint8_t *blob = build_sheet(&n);
    label_sheet_t *s = NULL;
    char why[96];

    CHECK(label_open_why(blob, n, 3, &s, why, sizeof why) == LABEL_OK, "the sheet opens: %s", why);
    if (!s) return;

    CHECK_EQ(label_templates(s), 2, "templates");
    CHECK_EQ(label_pick(s, 0), 0, "pick 0");
    CHECK_EQ(label_pick(s, 1), 1, "pick 1");
    CHECK_EQ(label_pick(s, 2), 0, "pick wraps");
    CHECK_EQ(label_pick(s, 12345), 12345u % 2u, "pick by seed");

    CHECK(strcmp(label_template_name(s, 0), "Night Drive") == 0, "template name 0");
    CHECK(strcmp(label_template_name(s, 1), "Second") == 0, "template name 1");
    CHECK(strcmp(label_template_name(s, 7), "") == 0, "name out of range");

    CHECK_EQ(label_prim_count(s, 0), 4, "prims of template 0");
    CHECK_EQ(label_prim_count(s, 1), 1, "prims of template 1");
    CHECK_EQ(label_prim_count(s, 9), 0, "prims out of range");

    label_prim_t p;
    CHECK(label_prim_at(s, 0, 0, &p), "prim 0");
    CHECK_EQ(p.kind, LABEL_RECT, "prim 0 is a rect");
    CHECK_EQ(p.fill, LABEL_FILL_PAPER, "prim 0 fills with paper");
    CHECK_EQ(p.w, 748, "prim 0 width");
    CHECK_EQ(p.h, 359, "prim 0 height");
    CHECK_EQ(p.r, 19, "prim 0 radius");

    CHECK(label_prim_at(s, 0, 1, &p), "prim 1");
    CHECK_EQ(p.fill, LABEL_FILL_LITERAL, "prim 1 is the literal colour");
    CHECK_EQ(p.literal, 0xC63A2E, "prim 1's colour");

    CHECK(label_prim_at(s, 0, 2, &p), "prim 2");
    CHECK_EQ(p.kind, LABEL_TEXT_STATIC, "prim 2 is a static text");
    CHECK_EQ(p.font, LABEL_FONT_CAPTION, "prim 2's face");
    CHECK_EQ(p.align, LABEL_ALIGN_RIGHT, "prim 2's alignment");
    CHECK(strcmp(label_prim_text(s, 0, 2), "SIDE A") == 0, "prim 2's text");

    CHECK(label_prim_at(s, 0, 3, &p), "prim 3");
    CHECK_EQ(p.kind, LABEL_TEXT_SLOT, "prim 3 is a slot text");
    CHECK_EQ(p.font, LABEL_FONT_TITLE, "prim 3's face");
    CHECK_EQ(p.rot10, 900, "prim 3's rotation");
    CHECK(strcmp(label_prim_text(s, 0, 3), "song_title") == 0, "prim 3 names its slot");
    CHECK(label_prim_text(s, 0, 0) == NULL, "a rect has no text");
    CHECK(label_prim_text(s, 0, 99) == NULL, "text out of range");

    CHECK(!label_prim_at(s, 0, 4, &p), "prim past the template");
    CHECK(!label_prim_at(s, 3, 0, &p), "template out of range");

    label_slot_t slot;
    CHECK_EQ(label_slot_count(s, 0), 2, "slots of template 0");
    CHECK(label_slot_at(s, 0, 0, &slot), "slot 0");
    CHECK(strcmp(slot.name, "song_title") == 0, "slot 0's name");
    CHECK_EQ(slot.max, 24, "slot 0's max");
    CHECK(label_slot_at(s, 0, 1, &slot), "slot 1");
    CHECK(strcmp(slot.name, "bpm") == 0, "slot 1's name");
    CHECK_EQ(slot.max, 3, "slot 1's max");
    CHECK(!label_slot_at(s, 0, 2, &slot), "slot past the template");

    label_inks_t inks = label_inks(s, 0);
    CHECK_EQ(inks.paper, 0xF2EDE3, "paper");
    CHECK_EQ(inks.ink, 0x24211D, "ink");
    CHECK_EQ(inks.rule, 0xB9AC8E, "rule");
    CHECK_EQ(inks.band[0], 0xC68B2E, "band 0");
    CHECK_EQ(inks.band[3], 0x6B7A2F, "band 3");

    CHECK_EQ(label_font(s, 0, LABEL_FONT_TITLE), 0, "title face");
    CHECK_EQ(label_font(s, 0, LABEL_FONT_NUMERAL), 1, "numeral face");
    CHECK_EQ(label_font(s, 0, LABEL_FONT_CAPTION), 2, "caption face");

    label_close(s);
    free(blob);

    /* The published CRC-32 check value, so the module's implementation is anchored outside
     * this repository. */
    CHECK_EQ(label_crc32((const uint8_t *)"123456789", 9), 0xCBF43926u, "CRC-32 check value");
}

static void test_refusals(void)
{
    size_t n = 0;

    uint8_t *b = mutate(0, 0, 1, &n);           /* "X7LBL..." */
    CHECK(refuses(b, n, "magic"), "bad magic");
    free(b);

    b = mutate(8, 2, 2, &n);
    CHECK(refuses(b, n, "version 1"), "version 2");
    free(b);

    b = mutate(10, 40, 2, &n);
    CHECK(refuses(b, n, "header size"), "header size 40");
    free(b);

    b = mutate(20, 800, 2, &n);
    CHECK(refuses(b, n, "748x359"), "space 800x359");
    free(b);

    b = mutate(24, 0, 2, &n);
    CHECK(refuses(b, n, "no designs"), "no templates");
    free(b);

    b = mutate(12, 0, 4, &n);                   /* total_size 0 */
    CHECK(refuses(b, n, "total_size"), "total size disagrees");
    free(b);

    b = build_sheet(&n);
    b[16] ^= 0xFF;                               /* corrupt the CRC only */
    CHECK(refuses(b, n, "checksum"), "checksum mismatch");
    free(b);

    b = mutate(T_AT(0) + 4, 9, 2, &n);           /* prim_start 9 > prims */
    CHECK(refuses(b, n, "template"), "template prims out of range");
    free(b);

    b = mutate(T_AT(0) + 12, 3, 1, &n);          /* font index == font_count */
    CHECK(refuses(b, n, "template"), "font index past the table");
    free(b);

    b = mutate(P_AT(0) + 0, 3, 1, &n);           /* kind 3 */
    CHECK(refuses(b, n, "prim"), "prim kind out of range");
    free(b);

    b = mutate(P_AT(0) + 1, 8, 1, &n);           /* fill 8 */
    CHECK(refuses(b, n, "prim"), "prim fill out of range");
    free(b);

    b = mutate(P_AT(0) + 16, 0, 4, &n);          /* a rect with a text offset */
    CHECK(refuses(b, n, "prim"), "rect with a text offset");
    free(b);

    b = mutate(P_AT(2) + 1, LABEL_FILL_PAPER, 1, &n);  /* a text that is not ink */
    CHECK(refuses(b, n, "prim"), "text colour not ink");
    free(b);

    b = mutate(P_AT(2) + 16, POOL_N + 4, 4, &n); /* text offset past the pool */
    CHECK(refuses(b, n, "prim"), "text offset out of the pool");
    free(b);

    b = mutate(P_AT(0) + 4, 700, 2, &n);         /* a rect: x 700 + w 748 leaves the print */
    CHECK(refuses(b, n, "prim"), "a rect outside the print is refused");
    free(b);

    b = mutate(P_AT(2) + 6, (uint16_t)-10, 2, &n);  /* a text's line box above the print */
    CHECK(opens(b, n), "a text above the print's edge opens: its ink is what clips");
    free(b);

    b = mutate(P_AT(2) + 4, 700, 2, &n);         /* a text whose box runs past the right edge */
    CHECK(opens(b, n), "a text's box past the edge opens too");
    free(b);

    b = mutate(S_AT(0) + 4, 0, 1, &n);           /* slot max 0 */
    CHECK(refuses(b, n, "slot"), "slot max 0");
    free(b);

    b = mutate(S_AT(0) + 4, 97, 1, &n);          /* slot max past LABEL_VALUE_MAX */
    CHECK(refuses(b, n, "slot"), "slot max past the value");
    free(b);

    b = mutate(S_AT(1), POOL_N + 1, 4, &n);      /* slot name past the pool */
    CHECK(refuses(b, n, "slot"), "slot name out of the pool");
    free(b);
}

/* ------------------------------------------------------------ the cut */

static void test_cut(void)
{
    char out[32];

    label_cut("Night Drive", 24, out, sizeof out);
    CHECK(strcmp(out, "Night Drive") == 0, "a short value is whole");

    label_cut("Night Drive", 4, out, sizeof out);
    CHECK(strcmp(out, "Nigh") == 0, "cut at four characters");

    label_cut("Night Drive", 0, out, sizeof out);
    CHECK(strcmp(out, "") == 0, "zero characters");

    label_cut(NULL, 4, out, sizeof out);
    CHECK(strcmp(out, "") == 0, "NULL is empty");

    /* Á and É are two bytes each: a cut at one character must not split either. */
    label_cut("\xC3\x81\xC3\x89", 1, out, sizeof out);
    CHECK(strcmp(out, "\xC3\x81") == 0, "cut on a two-byte character");

    /* A four-byte code point is one character. */
    label_cut("\xF0\x9F\x8E\xB5 tail", 1, out, sizeof out);
    CHECK(strcmp(out, "\xF0\x9F\x8E\xB5") == 0, "cut on a four-byte character");

    label_cut("\xC3\x81\xC3\x89", 2, out, 3);     /* room for one character and its NUL */
    CHECK(strcmp(out, "\xC3\x81") == 0, "a small buffer stops on a boundary");

    label_cut("abc", 3, out, 1);
    CHECK(strcmp(out, "") == 0, "a one-byte buffer holds nothing but the NUL");
}

/* ------------------------------------------------------------ values */

#define IMG_HEADER 80u
#define IMG_ROW 32u

typedef struct {
    const char *title, *artist, *album, *genre;
    uint16_t    bpm_x10;
    uint8_t     key, energy, dance, valence, arousal, mood;
} srow_t;

/* A one-row Snapshot image, laid out by the test: header, row, pool. */
static uint8_t *build_snapshot(size_t *n_out, const srow_t *r, uint32_t size,
                               const char *row_path)
{
    tbuf_t pool = {0};
    uint32_t off[4];
    const char *str[4] = {r->title, r->artist, r->album, r->genre};
    for (int i = 0; i < 4; i++) {
        off[i] = (uint32_t)pool.n;
        put(&pool, str[i], strlen(str[i]) + 1);
    }

    tbuf_t b = {0};
    put(&b, "K7SNAP\0\0", 8);
    put16(&b, 3);                 /* format version */
    put16(&b, IMG_HEADER);
    put32(&b, (uint32_t)(IMG_HEADER + IMG_ROW + pool.n));
    put32(&b, 1);                 /* rows */
    put32(&b, IMG_ROW);
    put64(&b, 1);                 /* generation */
    put32(&b, (uint32_t)pool.n);  /* pool size */
    put32(&b, 0);                 /* crc, patched */
    put32(&b, 1);                 /* analyser */
    put32(&b, 1);                 /* app schema */
    put64(&b, 0);                 /* model set */
    put64(&b, 0);                 /* reserved */
    put16(&b, 0);                 /* list count */
    put16(&b, 32);                /* list size */
    put16(&b, 4);                 /* list row size */
    put16(&b, 0);                 /* reserved */
    put32(&b, 0);                 /* list rows */
    put32(&b, (uint32_t)pool.n);  /* row pool size */

    put64(&b, snapshot_path_hash(row_path));
    put32(&b, size);
    put24(&b, off[0]);
    put24(&b, off[1]);
    put24(&b, off[2]);
    put24(&b, off[3]);

    uint64_t m = r->bpm_x10 ? (uint64_t)(r->bpm_x10 - 699u) : 0;
    m |= ((uint64_t)r->key & 0x1F) << 11;
    m |= ((uint64_t)r->energy & 0x7F) << 16;
    m |= ((uint64_t)r->dance & 0x7F) << 23;
    m |= ((uint64_t)r->valence & 0x7F) << 30;
    m |= ((uint64_t)r->arousal & 0x7F) << 37;
    m |= ((uint64_t)100 & 0x7F) << 44;          /* confidence */
    m |= ((uint64_t)r->mood & 0x1F) << 51;
    put64(&b, m);

    put(&b, pool.p, pool.n);
    free(pool.p);

    patch32(b.p, 36, crc32_ref(b.p + IMG_HEADER, b.n - IMG_HEADER));
    *n_out = b.n;
    return b.p;
}

static void values_of(srow_t *row, label_values_t *v, uint32_t size)
{
    size_t n = 0;
    uint8_t *img = build_snapshot(&n, row, size, "music/x.mp3");
    snapshot_t *sn = NULL;

    if (snapshot_open(img, n, &sn) != SNAPSHOT_OK) {
        CHECK(false, "the test's Snapshot image did not open");
        free(img);
        return;
    }
    snapshot_row_t r;
    CHECK(snapshot_find(sn, snapshot_path_hash("music/x.mp3"), size, &r), "the row is found");
    label_values_apply(v, sn, &r);
    snapshot_close(sn);
    free(img);
}

/* macOS hands a card's names out decomposed (NFD) and FATFS reads the same card composed (NFC),
 * so an image built on a Mac is keyed on bytes the device never sees. The join tries the path as
 * given and then once more with the accents decomposed, and the size must still agree. */
static void test_row_for_normalisation(void)
{
    srow_t row = {
        .title = "Cafe", .artist = "Manu", .album = "Tres", .genre = "Jazz",
        .bpm_x10 = 1200, .key = 1, .energy = 70, .dance = 60, .valence = 50, .arousal = 40,
        .mood = 1,
    };
    size_t n = 0;
    uint8_t *img = build_snapshot(&n, &row, 4242,
                                 "music/Cafe\xCC\x81/01 - Sole\xCC\x81" "a.mp3");  /* NFD */
    CHECK(img != NULL, "the NFD fixture built");
    snapshot_t *sn = NULL;
    CHECK(snapshot_open(img, n, &sn) == SNAPSHOT_OK, "the NFD image opens");

    snapshot_row_t got;
    CHECK(label_row_for(sn, "music/Caf\xC3\xA9/01 - Sol\xC3\xA9" "a.mp3", 4242, &got),
          "a composed path finds a decomposed row");
    CHECK(!label_row_for(sn, "music/Caf\xC3\xA9/01 - Sol\xC3\xA9" "a.mp3", 4243, &got),
          "the size still has to agree");
    CHECK(!label_row_for(sn, "music/Other/x.mp3", 4242, &got), "a different path does not");
    CHECK(!label_row_for(NULL, "music/x.mp3", 4242, &got), "no snapshot, no row");

    snapshot_close(sn);
    free(img);
}

static void test_values(void)
{
    label_values_t v;

    srow_t row = {
        .title = "Neon Skyline", .artist = "The Analog Sons", .album = "Night Drive",
        .genre = "Synthwave", .bpm_x10 = 1244, .key = 8, .energy = 70, .dance = 62,
        .valence = SNAPSHOT_UNKNOWN_PCT, .arousal = 48,
        .mood = SNAPSHOT_MOOD_HAPPY | SNAPSHOT_MOOD_RELAXED,
    };
    values_of(&row, &v, 1234);

    CHECK(strcmp(label_values_get(&v, "song_title"), "Neon Skyline") == 0, "the title");
    CHECK(strcmp(label_values_get(&v, "song_artist"), "The Analog Sons") == 0, "the artist");
    CHECK(strcmp(label_values_get(&v, "song_album"), "Night Drive") == 0, "the album");
    CHECK(strcmp(label_values_get(&v, "genre"), "Synthwave") == 0, "the genre");
    CHECK(strcmp(label_values_get(&v, "bpm"), "124") == 0, "BPM is whole and rounded");
    CHECK(strcmp(label_values_get(&v, "musical_key"), "8A") == 0, "the key is Camelot");
    CHECK(strcmp(label_values_get(&v, "energy"), "70") == 0, "energy");
    CHECK(strcmp(label_values_get(&v, "danceability"), "62") == 0, "danceability");
    CHECK(strcmp(label_values_get(&v, "valence"), "") == 0, "an unmeasured percentage is empty");
    CHECK(strcmp(label_values_get(&v, "arousal"), "48") == 0, "arousal");
    CHECK(strcmp(label_values_get(&v, "mood"), "happy") == 0, "the lowest set mood bit wins");

    CHECK(strcmp(label_values_get(&v, "year"), "") == 0, "a name outside the vocabulary is empty");
    CHECK(strcmp(label_values_get(&v, "song_album_artist"), "") == 0, "so is the retired one");
    CHECK(strcmp(label_values_get(NULL, "bpm"), "") == 0, "no values, no text");

    srow_t second = {
        .title = "Second", .artist = "", .album = "", .genre = "",
        .bpm_x10 = 0, .key = 20, .energy = SNAPSHOT_UNKNOWN_PCT, .dance = 0,
        .valence = 0, .arousal = 0, .mood = 0,
    };
    values_of(&second, &v, 99);
    CHECK(strcmp(label_values_get(&v, "bpm"), "") == 0, "no BPM is empty");
    CHECK(strcmp(label_values_get(&v, "musical_key"), "8B") == 0, "the second side of the key");
    CHECK(strcmp(label_values_get(&v, "energy"), "") == 0, "unmeasured energy is empty");
    CHECK(strcmp(label_values_get(&v, "danceability"), "0") == 0, "a measured zero prints 0");
    CHECK(strcmp(label_values_get(&v, "mood"), "") == 0, "no mood bit is empty");
    CHECK(strcmp(label_values_get(&v, "song_artist"), "") == 0, "an absent string is empty");

    /* BPM rounds up at the half. */
    srow_t third = {.title = "", .artist = "", .album = "", .genre = "", .bpm_x10 = 1245,
                    .key = 0, .energy = 0, .dance = 0, .valence = 0, .arousal = 0, .mood = 0};
    values_of(&third, &v, 7);
    CHECK(strcmp(label_values_get(&v, "bpm"), "125") == 0, "BPM rounds up");
    CHECK(strcmp(label_values_get(&v, "musical_key"), "") == 0, "no key is empty");

    /* Nothing to resolve: fill clears. */
    memset(&v, 'x', sizeof v);
    label_values_fill(&v, NULL, NULL, NULL);
    CHECK(strcmp(label_values_get(&v, "song_title"), "") == 0, "no library, no title");

    CHECK(strcmp(label_values_name(LABEL_SLOT_BPM), "bpm") == 0, "the slot's id");
    CHECK(strcmp(label_values_name(LABEL_SLOT_COUNT), "") == 0, "an id out of range");
}

int main(void)
{
    test_reader();
    test_refusals();
    test_cut();
    test_row_for_normalisation();
    test_values();
    TEST_END();
}
