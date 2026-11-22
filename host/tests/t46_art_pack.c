/*
 * T46: the K7Art pack and the two-sheet pool, host tier.
 *
 * The pack's bytes are built here, by a writer that shares no code with the reader: the
 * test owns the header, the CRC and the sheet's layout, so a change in the reader cannot
 * make the test agree with it. The reader is then asked every refusal in art-packs.md §1
 * and §5 that a host can ask - the magic, the version, the bounds, the CRC, the sheet at
 * its offset, the design count against the sheet, the font count against the firmware -
 * and the pool's factory-first mapping and factory fallback.
 *
 * What this cannot prove: the `art` partition's flash write and erase, the mmap, and the
 * board's boot. Those are the board's (README, "Two-tier acceptance"); the host's store is
 * a file and its routes are covered by host/tests/art_tests.py.
 */
#include "test_util.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "label.h"
#include "label_pack.h"
#include "label_pool.h"

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
    size_t cap = b->cap ? b->cap : 256;
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

static void patch32(uint8_t *p, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[at + i] = (uint8_t)(v >> (8 * i));
}

/* ------------------------------------------------------------ sheet builder */

/* A valid sheet with `templates` designs, one paper rect each, no Slots. It is enough for
 * the pack's checks - which never look inside a prim - and the pool only needs the sheet to
 * open and count. */
static uint8_t *build_sheet(const char *name, uint16_t templates, size_t *n_out)
{
    tbuf_t b = {0};
    const size_t pool = strlen(name) + 1;
    put(&b, "K7LBL\0\0\0", 8);
    put16(&b, LABEL_VERSION);
    put16(&b, 32);
    put32(&b, 0); /* total, patched */
    put32(&b, 0); /* crc, patched */
    put16(&b, 748);
    put16(&b, 359);
    put16(&b, templates);
    put16(&b, templates); /* one prim each */
    put16(&b, 0);         /* no slots */
    put16(&b, (uint16_t)pool);

    for (uint16_t t = 0; t < templates; t++) {
        put32(&b, 0);      /* name_offset: the pool's first string */
        put16(&b, t);      /* prim_start */
        put16(&b, 1);      /* prim_count */
        put16(&b, 0);      /* slot_start */
        put16(&b, 0);      /* slot_count */
        put8(&b, 0);       /* title font */
        put8(&b, 0);       /* numeral font */
        put8(&b, 0);       /* caption font */
        put8(&b, 0);       /* no canvas */
    }
    for (uint16_t t = 0; t < templates; t++)
        for (int i = 0; i < 8; i++) put32(&b, 0); /* the canvas rows */

    for (uint16_t t = 0; t < templates; t++) {
        put8(&b, LABEL_RECT);
        put8(&b, 0);          /* reserved */
        put8(&b, 0);          /* font */
        put8(&b, 0);          /* align */
        put16(&b, 0);
        put16(&b, 0);
        put16(&b, 748);
        put16(&b, 359);
        put16(&b, 19);
        put16(&b, 0);
        put32(&b, 0xFFFFFFFFu);
        put32(&b, 0xF2EDE3);  /* the paper's own colour */
    }
    put(&b, name, pool);

    patch32(b.p, 12, (uint32_t)b.n);
    patch32(b.p, 16, crc32_ref(b.p + 32, b.n - 32));
    *n_out = b.n;
    return b.p;
}

/* ------------------------------------------------------------ pack builder */

static const char k_jcards[] = "{\"v\":1,\"designs\":[]}";

static uint8_t *build_pack(const uint8_t *sheet, size_t sheet_len, const char *pack_id,
                           const char *name, const char *author, uint16_t design_count,
                           uint16_t font_count, size_t *n_out)
{
    tbuf_t b = {0};
    put(&b, "K7ART\0\0\0", 8);
    put16(&b, LABEL_PACK_VERSION);
    put16(&b, LABEL_PACK_HEADER_SIZE);
    put32(&b, 0); /* total, patched */
    put32(&b, 0); /* crc, patched */
    put16(&b, design_count);
    put16(&b, LABEL_VERSION);
    put16(&b, font_count);
    put16(&b, 0); /* reserved */
    put32(&b, LABEL_PACK_HEADER_SIZE);
    put32(&b, (uint32_t)sheet_len);
    put32(&b, (uint32_t)(LABEL_PACK_HEADER_SIZE + sheet_len));
    put32(&b, (uint32_t)strlen(k_jcards));
    char field[32];
    memset(field, 0, sizeof field);
    snprintf(field, sizeof field, "%s", pack_id);
    put(&b, field, 16);
    memset(field, 0, sizeof field);
    snprintf(field, sizeof field, "%s", name);
    put(&b, field, 32);
    memset(field, 0, sizeof field);
    snprintf(field, sizeof field, "%s", author);
    put(&b, field, 32);
    put32(&b, 0); /* reserved */
    put(&b, sheet, sheet_len);
    put(&b, k_jcards, strlen(k_jcards));

    patch32(b.p, 12, (uint32_t)b.n);
    patch32(b.p, 16,
            crc32_ref(b.p + LABEL_PACK_HEADER_SIZE, b.n - LABEL_PACK_HEADER_SIZE));
    *n_out = b.n;
    return b.p;
}

/* A copy of `pack` with one field changed and the CRC recomputed, so a check fails for the
 * reason it names. */
static uint8_t *mutate_pack(const uint8_t *pack, size_t n, size_t at, uint32_t value,
                            size_t width)
{
    uint8_t *p = malloc(n);
    if (!p) abort();
    memcpy(p, pack, n);
    for (size_t i = 0; i < width; i++) p[at + i] = (uint8_t)(value >> (8 * i));
    patch32(p, 16, crc32_ref(p + LABEL_PACK_HEADER_SIZE, n - LABEL_PACK_HEADER_SIZE));
    return p;
}

/* The sheet inside the pack changed: recompute the sheet's own CRC first, then the pack's. */
static uint8_t *mutate_sheet(uint8_t *pack, size_t n, size_t sheet_at, uint32_t value,
                             size_t width, size_t sheet_len)
{
    for (size_t i = 0; i < width; i++)
        pack[LABEL_PACK_HEADER_SIZE + sheet_at + i] = (uint8_t)(value >> (8 * i));
    patch32(pack, LABEL_PACK_HEADER_SIZE + 16,
            crc32_ref(pack + LABEL_PACK_HEADER_SIZE + 32, sheet_len - 32));
    patch32(pack, 16,
            crc32_ref(pack + LABEL_PACK_HEADER_SIZE, n - LABEL_PACK_HEADER_SIZE));
    return pack;
}

static bool opens(const uint8_t *bytes, size_t n, uint8_t fonts)
{
    label_pack_t pack;
    label_sheet_t *sheet = NULL;
    char why[160];
    if (label_pack_open_why(bytes, n, fonts, &pack, &sheet, why, sizeof why) != LABEL_OK)
        return false;
    if (sheet) label_close(sheet);
    return true;
}

static bool refuses(const uint8_t *bytes, size_t n, uint8_t fonts, const char *needle)
{
    label_pack_t pack;
    label_sheet_t *sheet = NULL;
    char why[160];
    if (label_pack_open_why(bytes, n, fonts, &pack, &sheet, why, sizeof why) == LABEL_OK) {
        if (sheet) label_close(sheet);
        return false;
    }
    if (sheet) label_close(sheet);
    return needle && strstr(why, needle) != NULL;
}

/* ------------------------------------------------------------ the pack */

static void test_pack(const uint8_t *sheet, size_t sheet_len)
{
    size_t n = 0;
    uint8_t *pack = build_pack(sheet, sheet_len, "neon-2026", "Neon Pack", "Ada", 1, 2, &n);

    label_pack_t out;
    label_sheet_t *opened = NULL;
    char why[160] = {0};
    CHECK(label_pack_open_why(pack, n, 2, &out, &opened, why, sizeof why) == LABEL_OK,
          "the pack opens: %s", why);
    if (opened) {
        CHECK_EQ(out.total_size, n, "total size");
        CHECK_EQ(out.design_count, 1, "design count");
        CHECK_EQ(out.sheet_version, LABEL_VERSION, "sheet version");
        CHECK_EQ(out.font_count, 2, "font count");
        CHECK_EQ(out.sheet_offset, LABEL_PACK_HEADER_SIZE, "sheet offset");
        CHECK_EQ(out.sheet_size, sheet_len, "sheet size");
        CHECK_EQ(out.jcards_offset, LABEL_PACK_HEADER_SIZE + sheet_len, "J-cards offset");
        CHECK_EQ(out.jcards_size, strlen(k_jcards), "J-cards size");
        CHECK_EQ(out.sheet_crc, crc32_ref(sheet + 32, sheet_len - 32), "the sheet's CRC");
        CHECK(strcmp(out.pack_id, "neon-2026") == 0, "pack id");
        CHECK(strcmp(out.name, "Neon Pack") == 0, "name");
        CHECK(strcmp(out.author, "Ada") == 0, "author");
        CHECK_EQ(label_templates(opened), 1, "the opened sheet's designs");
        CHECK(out.sheet == pack + LABEL_PACK_HEADER_SIZE, "the sheet is read in place");
        label_close(opened);
        opened = NULL;
    }

    CHECK(opens(pack, n, 0), "a firmware that has no font table yet accepts it");
    CHECK(opens(pack, n, 2), "the matching font count accepts it");
    CHECK(refuses(pack, n, 3, "built against 2 fonts"), "another font table is refused by name");

    /* The pack in a partition: total_size may be smaller than the bytes the store holds. */
    uint8_t *roomy = malloc(n + 64);
    CHECK(roomy != NULL, "room for a partition tail");
    if (roomy) {
        memcpy(roomy, pack, n);
        memset(roomy + n, 0xFF, 64);
        CHECK(opens(roomy, n + 64, 2), "the bytes past total_size are not part of the pack");
        free(roomy);
    }

    CHECK(refuses(pack, n - 1, 2, "shorter than its total size"), "a truncated pack is refused");
    if (n > 0) {
        uint8_t *p = mutate_pack(pack, n, 0, 'X', 1);
        CHECK(refuses(p, n, 2, "magic is not K7ART"), "a wrong magic is refused");
        free(p);
        p = mutate_pack(pack, n, 8, 2, 2);
        CHECK(refuses(p, n, 2, "not pack version 1"), "a wrong pack version is refused");
        free(p);
        p = mutate_pack(pack, n, 10, 64, 2);
        CHECK(refuses(p, n, 2, "header size is not 128"), "a wrong header size is refused");
        free(p);
        p = mutate_pack(pack, n, 20, 0, 2);
        CHECK(refuses(p, n, 2, "no designs"), "no designs is refused");
        free(p);
        p = mutate_pack(pack, n, 20, 2, 2);
        CHECK(refuses(p, n, 2, "design_count disagrees"), "a count the sheet does not have is refused");
        free(p);
        p = mutate_pack(pack, n, 22, 3, 2);
        CHECK(refuses(p, n, 2, "not sheet version 5"), "a sheet from another version is refused");
        free(p);
        p = mutate_pack(pack, n, 24, 0, 2);
        CHECK(refuses(p, n, 2, "no font table"), "a pack with no font table is refused");
        free(p);
        p = mutate_pack(pack, n, 26, 1, 2);
        CHECK(refuses(p, n, 2, "reserved"), "a reserved header field is refused");
        free(p);
        p = mutate_pack(pack, n, 28, 0, 4);
        CHECK(refuses(p, n, 2, "sheet is not at its offset"), "a sheet elsewhere is refused");
        free(p);
        p = mutate_pack(pack, n, 36, 0, 4);
        CHECK(refuses(p, n, 2, "J-cards are not at their offset"), "J-cards elsewhere are refused");
        free(p);
        p = mutate_pack(pack, n, 40, 0, 4);
        CHECK(refuses(p, n, 2, "no J-cards"), "no J-cards is refused");
        free(p);
        p = mutate_pack(pack, n, 124, 1, 4);
        CHECK(refuses(p, n, 2, "reserved"), "the trailing reserved word is refused");
        free(p);
        /* A flipped payload byte with the header's CRC left alone: the test's CRC must not
         * cover the flip, or the pack would be consistently wrong and the reader right. */
        p = malloc(n);
        CHECK(p != NULL, "room for the corrupted copy");
        if (p) {
            memcpy(p, pack, n);
            p[n - 1] ^= 0xFF;
            CHECK(refuses(p, n, 2, "checksum disagrees"), "a flipped payload byte is refused");
            free(p);
        }
    }

    /* The sheet's own checks are the reader's, not a second copy here. */
    uint8_t *bad_sheet = malloc(n);
    if (bad_sheet) {
        memcpy(bad_sheet, pack, n);
        (void)mutate_sheet(bad_sheet, n, 0, 'X', 1, sheet_len);
        CHECK(refuses(bad_sheet, n, 2, "sheet does not open (magic is not K7LBL)"),
              "a sheet with a wrong magic is refused");
        free(bad_sheet);
    }
    bad_sheet = malloc(n);
    if (bad_sheet) {
        memcpy(bad_sheet, pack, n);
        /* Template 0's first font index, at the sheet's offset 32 + 12. */
        (void)mutate_sheet(bad_sheet, n, 32 + 12, 5, 1, sheet_len);
        CHECK(refuses(bad_sheet, n, 2, "sheet does not open (a template is out of range)"),
              "a font index past the pack's table is refused");
        free(bad_sheet);
    }

    /* A sheet shorter than its own 32-byte header: the pack's fields still point inside
     * the buffer, and the reader must refuse it without reading past the sheet's length. */
    {
        const size_t short_sheet = 4;
        const size_t short_n = LABEL_PACK_HEADER_SIZE + short_sheet + strlen(k_jcards);
        uint8_t *p = malloc(short_n);
        CHECK(p != NULL, "room for a short sheet");
        if (p) {
            memcpy(p, pack, LABEL_PACK_HEADER_SIZE);
            memcpy(p + LABEL_PACK_HEADER_SIZE, "K7LB", short_sheet);
            memcpy(p + LABEL_PACK_HEADER_SIZE + short_sheet, k_jcards, strlen(k_jcards));
            patch32(p, 12, (uint32_t)short_n);
            patch32(p, 32, (uint32_t)short_sheet);
            patch32(p, 36, (uint32_t)(LABEL_PACK_HEADER_SIZE + short_sheet));
            patch32(p, 16, crc32_ref(p + LABEL_PACK_HEADER_SIZE,
                                     short_n - LABEL_PACK_HEADER_SIZE));
            CHECK(refuses(p, short_n, 2, "sheet does not open (shorter than its header)"),
                  "a sheet shorter than its header is refused");
            free(p);
        }
    }

    /* The partition's bound is the pack's, not the sheet's. */
    uint8_t *huge = calloc(1, LABEL_PACK_MAX + 1);
    if (huge) {
        memcpy(huge, "K7ART", 5);
        patch32(huge, 12, LABEL_PACK_MAX + 1u);
        CHECK(refuses(huge, LABEL_PACK_MAX + 1, 2, "larger than the art partition"),
              "a pack past 1 MB is refused");
        free(huge);
    }

    free(pack);
}

/* ------------------------------------------------------------ the pool */

/* A sheet's readers keep the blob, so the bytes have to outlive the handle. */
static label_sheet_t *open_kept(const char *name, uint16_t templates, uint8_t **bytes_out)
{
    size_t n = 0;
    uint8_t *bytes = build_sheet(name, templates, &n);
    label_sheet_t *sheet = NULL;
    CHECK(label_open_why(bytes, n, 2, &sheet, NULL, 0) == LABEL_OK, "%s opens", name);
    if (bytes_out) *bytes_out = bytes;
    else free(bytes);
    return sheet;
}

static void test_pool(void)
{
    label_pool_t pool;
    label_pool_init(&pool);
    CHECK_EQ(label_pool_count(&pool), 0, "an empty pool");
    const label_sheet_t *sheet = NULL;
    uint16_t t = 99;
    CHECK(!label_pool_pick(&pool, 0, &sheet, &t), "an empty pool picks nothing");

    uint8_t *factory_bytes = NULL, *user_bytes = NULL;
    label_sheet_t *factory = open_kept("Factory", 2, &factory_bytes);
    label_sheet_t *user = open_kept("User", 1, &user_bytes);
    label_pool_set_factory(&pool, factory);
    label_pool_set_user(&pool, user);

    CHECK_EQ(label_pool_count(&pool), 3, "two factory designs and one user");
    CHECK_EQ(label_pool_factory_count(&pool), 2, "factory count");
    CHECK_EQ(label_pool_user_count(&pool), 1, "user count");

    CHECK(label_pool_pick(&pool, 0, &sheet, &t), "seed 0 picks");
    CHECK(sheet == factory && t == 0, "seed 0 is the factory's first design");
    CHECK(label_pool_pick(&pool, 1, &sheet, &t), "seed 1 picks");
    CHECK(sheet == factory && t == 1, "seed 1 is the factory's second design");
    CHECK(label_pool_pick(&pool, 2, &sheet, &t), "seed 2 picks");
    CHECK(sheet == user && t == 0, "seed 2 is the user's first design");
    CHECK(label_pool_pick(&pool, 3, &sheet, &t), "seed 3 wraps");
    CHECK(sheet == factory && t == 0, "seed 3 is the factory's first design again");
    CHECK(label_pool_pick(&pool, 12345, &sheet, &t), "a larger seed picks");
    CHECK(sheet == factory && t == 12345 % 3, "the pick is the seed modulo the pool");

    CHECK(label_pool_at(&pool, 2, &sheet, &t) && sheet == user && t == 0, "index 2 is the user");
    CHECK(!label_pool_at(&pool, 3, &sheet, &t), "index 3 is past the pool");

    /* An absent or invalid user pack is the factory: the caller simply never sets one. */
    label_pool_clear_user(&pool);
    CHECK_EQ(label_pool_count(&pool), 2, "without the user pack the pool is the factory");
    CHECK(label_pool_pick(&pool, 2, &sheet, &t), "seed 2 still picks");
    CHECK(sheet == factory && t == 0, "and it is the factory's");
    CHECK_EQ(label_pool_user_count(&pool), 0, "no user designs");

    label_pool_close(&pool);
    CHECK_EQ(label_pool_count(&pool), 0, "closing empties the pool");
    free(factory_bytes);
    free(user_bytes);
}

int main(void)
{
    size_t sheet_len = 0;
    uint8_t *sheet = build_sheet("Pack Design", 1, &sheet_len);
    test_pack(sheet, sheet_len);
    free(sheet);
    test_pool();
    TEST_END();
}
