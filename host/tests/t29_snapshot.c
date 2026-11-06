/*
 * T29: the Snapshot module, host tier.
 *
 * The image is built here, byte by byte, by a builder that shares no code with the
 * module: the test owns the layout, the CRC and the fingerprint so a change in the
 * module cannot make the test agree with it. The published FNV-1a and CRC-32 vectors
 * anchor both implementations to something outside this repository.
 *
 * What is exercised: validation (every rule the format lists), row lookup including
 * the size check, string reading, the filter predicate, the key and sort parsers, and
 * the bounded hit collector.
 */
#include "test_util.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static uint64_t fnv1a_ref(const char *s)
{
    uint64_t h = 0xCBF29CE484222325ull;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        h ^= *p;
        h *= 0x00000100000001B3ull;
    }
    return h;
}

/* --------------------------------------------------------------- builder */

typedef struct {
    uint8_t *p;
    size_t   n;
    size_t   cap;
} buf_t;

static void need(buf_t *b, size_t extra)
{
    if (b->n + extra <= b->cap) return;
    size_t cap = b->cap ? b->cap * 2 : 256;
    while (cap < b->n + extra) cap *= 2;
    uint8_t *q = realloc(b->p, cap);
    if (!q) abort();
    b->p = q;
    b->cap = cap;
}

static void put(buf_t *b, const void *src, size_t n)
{
    need(b, n);
    memcpy(b->p + b->n, src, n);
    b->n += n;
}
static void put16(buf_t *b, uint16_t v)
{
    uint8_t t[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    put(b, t, 2);
}
static void put24(buf_t *b, uint32_t v)
{
    uint8_t t[3] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16)};
    put(b, t, 3);
}
static void put32(buf_t *b, uint32_t v)
{
    uint8_t t[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    put(b, t, 4);
}
static void put64(buf_t *b, uint64_t v)
{
    uint8_t t[8];
    for (int i = 0; i < 8; i++) t[i] = (uint8_t)(v >> (8 * i));
    put(b, t, 8);
}
static uint64_t get64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}
static void patch24(uint8_t *p, size_t at, uint32_t v)
{
    for (int i = 0; i < 3; i++) p[at + i] = (uint8_t)(v >> (8 * i));
}
static void patch32(uint8_t *p, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[at + i] = (uint8_t)(v >> (8 * i));
}
static void patch64(uint8_t *p, size_t at, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[at + i] = (uint8_t)(v >> (8 * i));
}
/* Replace one field of the packed measurements word. The tests use this to write an
 * out-of-range value the builder would never produce. */
static void patch_field(uint8_t *img, size_t row_at, int shift, int width, uint64_t v)
{
    uint64_t mask = ((1ull << width) - 1ull) << shift;
    uint64_t m = get64(img + row_at + 24);
    patch64(img, row_at + 24, (m & ~mask) | ((v << shift) & mask));
}

static const char MAGIC[8] = {'K', '7', 'S', 'N', 'A', 'P', 0, 0};
/* The wire values this test owns: the reader must not define the oracle. */
#define IMG_VERSION 3u
#define IMG_HEADER_SIZE 80u
#define IMG_ROW_SIZE 32u
#define IMG_LIST_SIZE 32u
#define IMG_LIST_ROW_SIZE 4u
#define IMG_MAX_LIST_ROWS 8192u
#define FILE_UNKNOWN_PCT 127u

typedef struct {
    uint64_t fp;
    uint32_t size;
    uint32_t title_off, artist_off, album_off, genre_off;
    uint16_t bpm_x10; /* 0 unknown, otherwise 700..1800 */
    uint8_t  key, energy, dance, valence, arousal, mood, conf, src; /* file values */
} row_t;

static uint64_t pack_measurements(const row_t *r)
{
    uint64_t m = r->bpm_x10 ? (uint64_t)(r->bpm_x10 - 699u) : 0;
    m |= ((uint64_t)r->key & 0x1F) << 11;
    m |= ((uint64_t)r->energy & 0x7F) << 16;
    m |= ((uint64_t)r->dance & 0x7F) << 23;
    m |= ((uint64_t)r->valence & 0x7F) << 30;
    m |= ((uint64_t)r->arousal & 0x7F) << 37;
    m |= ((uint64_t)r->conf & 0x7F) << 44;
    m |= ((uint64_t)r->mood & 0x1F) << 51;
    m |= ((uint64_t)r->src & 0x3F) << 56;
    return m;
}

static void put_row(buf_t *b, const row_t *r)
{
    put64(b, r->fp);
    put32(b, r->size);
    put24(b, r->title_off);
    put24(b, r->artist_off);
    put24(b, r->album_off);
    put24(b, r->genre_off);
    put64(b, pack_measurements(r));
}

/* One list, as the test builds it. The row tiling is computed by the builder, not
 * given, so a test that wants a broken tiling patches the bytes afterwards. */
typedef struct {
    uint64_t        id;
    const char     *name;
    uint64_t        created;
    const char *const *paths;
    uint32_t        n;
} list_t;

/* A whole version 3 image: header, rows, pool, list records, list rows. The row
 * strings are the caller's pool (offsets are into it); the list strings follow them,
 * each list's name and then its paths, in list order. */
static uint8_t *build_image_lists(size_t *out_n, const row_t *rows, size_t n,
                                  const char *row_pool, size_t row_pool_n,
                                  const list_t *lists, uint32_t nlists)
{
    uint32_t list_rows = 0;
    for (uint32_t i = 0; i < nlists; i++) list_rows += lists[i].n;

    size_t list_pool_n = 0;
    for (uint32_t i = 0; i < nlists; i++) {
        list_pool_n += strlen(lists[i].name) + 1;
        for (uint32_t j = 0; j < lists[i].n; j++)
            list_pool_n += strlen(lists[i].paths[j]) + 1;
    }
    size_t pool_n = row_pool_n + list_pool_n;

    uint8_t *pool = malloc(pool_n ? pool_n : 1);
    uint32_t *name_off = malloc(sizeof(uint32_t) * (nlists ? nlists : 1));
    uint32_t *path_off = malloc(sizeof(uint32_t) * (list_rows ? list_rows : 1));
    if (!pool || !name_off || !path_off) abort();

    memcpy(pool, row_pool, row_pool_n);
    size_t pn = row_pool_n;
    uint32_t at = 0;
    for (uint32_t i = 0; i < nlists; i++) {
        name_off[i] = (uint32_t)pn;
        size_t ln = strlen(lists[i].name) + 1;
        memcpy(pool + pn, lists[i].name, ln);
        pn += ln;
        for (uint32_t j = 0; j < lists[i].n; j++) {
            path_off[at] = (uint32_t)pn;
            size_t pl = strlen(lists[i].paths[j]) + 1;
            memcpy(pool + pn, lists[i].paths[j], pl);
            pn += pl;
            at++;
        }
    }

    size_t total = IMG_HEADER_SIZE + n * IMG_ROW_SIZE + pool_n +
                   (size_t)nlists * IMG_LIST_SIZE +
                   (size_t)list_rows * IMG_LIST_ROW_SIZE;

    buf_t b = {0};
    put(&b, MAGIC, 8);
    put16(&b, IMG_VERSION);
    put16(&b, IMG_HEADER_SIZE);
    put32(&b, (uint32_t)total);
    put32(&b, (uint32_t)n);
    put32(&b, IMG_ROW_SIZE);
    put64(&b, 1767225600ull); /* 2026-01-01T00:00:00Z */
    put32(&b, (uint32_t)pool_n);
    put32(&b, 0); /* crc, patched below */
    put32(&b, 300);
    put32(&b, 1);
    put64(&b, 0x0123456789ABCDEFull);
    put64(&b, 0); /* reserved 56..63 */
    put16(&b, (uint16_t)nlists);
    put16(&b, IMG_LIST_SIZE);
    put16(&b, IMG_LIST_ROW_SIZE);
    put16(&b, 0); /* reserved 70..71 */
    put32(&b, list_rows);
    put32(&b, (uint32_t)row_pool_n);

    for (size_t i = 0; i < n; i++) put_row(&b, &rows[i]);
    put(&b, pool, pool_n);

    at = 0;
    for (uint32_t i = 0; i < nlists; i++) {
        put64(&b, lists[i].id);
        put32(&b, name_off[i]);
        put32(&b, at);
        put32(&b, lists[i].n);
        put64(&b, lists[i].created);
        put32(&b, 0);
        at += lists[i].n;
    }
    for (uint32_t i = 0; i < list_rows; i++) put32(&b, path_off[i]);

    patch32(b.p, 36, crc32_ref(b.p + IMG_HEADER_SIZE, b.n - IMG_HEADER_SIZE));
    *out_n = b.n;
    free(pool);
    free(name_off);
    free(path_off);
    return b.p;
}

static uint8_t *build_image(size_t *out_n, const row_t *rows, size_t n, const char *pool,
                            size_t pool_n)
{
    return build_image_lists(out_n, rows, n, pool, pool_n, NULL, 0);
}

static void fix_crc(uint8_t *image, size_t n)
{
    patch32(image, 36, crc32_ref(image + IMG_HEADER_SIZE, n - IMG_HEADER_SIZE));
}

/* The pool of the common fixture: row 0 at offset 0, row 1 after it. Each row names
 * its four strings by their own offset. */
static const char POOL[] =
    "Blue Monday\0New Order\0Power, Corruption & Lies\0electronic\0"
    "So What\0Miles Davis\0Kind of Blue\0jazz\0";
static const uint32_t POOL_ROW1_AT = 58u; /* the first four strings together */
/* sizeof POOL counts the literal's own terminator, which is not part of the image. */
#define POOL_N (sizeof POOL - 1u)

#define POOL0_TITLE 0u
#define POOL0_ARTIST 12u
#define POOL0_ALBUM 22u
#define POOL0_GENRE 47u
#define POOL1_TITLE POOL_ROW1_AT
#define POOL1_ARTIST (POOL_ROW1_AT + 8u)
#define POOL1_ALBUM (POOL_ROW1_AT + 20u)
#define POOL1_GENRE (POOL_ROW1_AT + 33u)

static const row_t ROWS[] = {
    {0x1111111111111111ull, 1000, POOL0_TITLE, POOL0_ARTIST, POOL0_ALBUM, POOL0_GENRE,
     1245, 8, 78, 61, 40, 78, SNAPSHOT_MOOD_HAPPY | SNAPSHOT_MOOD_PARTY, 72,
     SNAPSHOT_SRC_VA_PRESENT},
    {0x2222222222222222ull, 2000, POOL1_TITLE, POOL1_ARTIST, POOL1_ALBUM, POOL1_GENRE,
     920, 5, FILE_UNKNOWN_PCT, 40, FILE_UNKNOWN_PCT, 30, SNAPSHOT_MOOD_RELAXED,
     FILE_UNKNOWN_PCT, SNAPSHOT_SRC_BPM_TAGGED | SNAPSHOT_SRC_KEY_TAGGED},
};

/* --------------------------------------------------------------- fixtures */

static snapshot_t *open_ok(const uint8_t *image, size_t n)
{
    char why[96] = {0};
    snapshot_t *s = NULL;
    snapshot_result_t r = snapshot_open_why(image, n, &s, why, sizeof why);
    CHECK(r == SNAPSHOT_OK, "open: %d (%s)", (int)r, why);
    return s;
}

/* ------------------------------------------------------------- lists */

/* Two lists over the two-row fixture: ids ascending, one row each. */
static const char *const LIST1_PATHS[] = {"New Order/Power/x.mp3"};
static const char *const LIST2_PATHS[] = {"Miles/Blue/y.flac"};

static const list_t LISTS[] = {
    {0xAAAA000000000001ull, "Late night", 1767225600ull, LIST1_PATHS, 1},
    {0xBBBB000000000002ull, "Morning", 1767225600ull, LIST2_PATHS, 1},
};

static void test_lists(void)
{
    size_t n = 0;
    uint8_t *img = build_image_lists(&n, ROWS, 2, POOL, POOL_N, LISTS, 2);

    snapshot_t *s = open_ok(img, n);
    CHECK(s != NULL, "a version 3 image with lists opens");
    if (!s) return;
    CHECK_EQ(snapshot_version(s), 3, "version 3");
    CHECK_EQ(snapshot_lists(s), 2, "two lists");
    CHECK_EQ(snapshot_list_rows_total(s), 2, "two list rows");
    CHECK_EQ(snapshot_rows(s), 2, "rows are still readable");

    snapshot_list_t l;
    CHECK(snapshot_list_find(s, LISTS[0].id, &l), "find the first list by id");
    CHECK(strcmp(snapshot_list_name(s, &l), "Late night") == 0, "its name");
    CHECK_EQ(l.row_count, 1, "one row");
    CHECK(l.created == LISTS[0].created, "created");
    /* Both list strings live after the row strings. */
    CHECK(l.name_offset >= POOL_N, "a list name is in the list region");

    const char *path = NULL;
    CHECK(snapshot_list_row(s, &l, 0, &path), "read its row");
    CHECK(strcmp(path, LIST1_PATHS[0]) == 0, "the path is the row's");

    CHECK(snapshot_list_at(s, 1, &l), "the second list by index");
    CHECK(strcmp(snapshot_list_name(s, &l), "Morning") == 0, "its name");
    CHECK(!snapshot_list_find(s, 0, &l), "id zero is never a list");
    CHECK(!snapshot_list_find(s, 0xCCCC000000000003ull, &l), "an unknown id is not a list");
    CHECK(!snapshot_list_row(s, &l, 7, &path), "a row past the list is refused");

    /* The header records where the row strings end; the list strings follow it. */
    CHECK_EQ(img[76] | (img[77] << 8), (int)POOL_N, "row_pool_size in the header");
    snapshot_close(s);
    free(img);
}

static void test_list_rejects(void)
{
    size_t n = 0;
    uint8_t *img = build_image_lists(&n, ROWS, 2, POOL, POOL_N, LISTS, 2);
    uint8_t *bad = malloc(n);
    snapshot_t *s = NULL;

    /* The list area is the tail of the image: two records, then two rows. */
    uint32_t lrows = (uint32_t)n - 2 * IMG_LIST_ROW_SIZE;
    uint32_t list1 = lrows - IMG_LIST_SIZE;
    uint32_t list0 = list1 - IMG_LIST_SIZE;

    /* ids descending */
    memcpy(bad, img, n);
    patch64(bad, list0, 0xFFFF000000000000ull);
    fix_crc(bad, n);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "descending list ids refused");

    /* a second list that does not start where the first ended */
    memcpy(bad, img, n);
    patch32(bad, list1 + 12, 2);
    fix_crc(bad, n);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "a tiling gap is refused");

    /* a list record whose reserved bytes are not zero */
    memcpy(bad, img, n);
    bad[list0 + 28] = 1;
    fix_crc(bad, n);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "a list record's reserved byte is refused");

    /* the wrong list size */
    memcpy(bad, img, n);
    bad[66] = 31;
    fix_crc(bad, n);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "a bad list size is refused");

    /* the wrong list row size */
    memcpy(bad, img, n);
    bad[68] = 3;
    fix_crc(bad, n);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "a bad list row size is refused");

    /* a reserved byte in the second half of the header */
    memcpy(bad, img, n);
    bad[70] = 1;
    fix_crc(bad, n);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "reserved header bytes 70..71 refused");

    /* a name offset past the pool */
    memcpy(bad, img, n);
    patch32(bad, list0 + 8, (uint32_t)POOL_N + 40u + 0x1000u);
    fix_crc(bad, n);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "a name outside the pool is refused");

    /* a name offset that points into the row-string region */
    memcpy(bad, img, n);
    patch32(bad, list0 + 8, 0);
    fix_crc(bad, n);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID,
          "a list name in the row region is refused");

    /* a path offset that points into the row-string region */
    memcpy(bad, img, n);
    patch32(bad, lrows, 0);
    fix_crc(bad, n);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID,
          "a list path in the row region is refused");

    /* a path offset past the pool */
    memcpy(bad, img, n);
    patch32(bad, lrows, (uint32_t)n);
    fix_crc(bad, n);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "a path outside the pool is refused");

    /* a list name that does not terminate inside the list region */
    memcpy(bad, img, n);
    bad[n - 1] = 'x';
    fix_crc(bad, n);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID,
          "an unterminated list string is refused");

    /* a list count over the cap */
    memcpy(bad, img, n);
    bad[64] = 65;
    fix_crc(bad, n);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "more than 64 lists is refused");

    /* more list rows than the image may hold */
    memcpy(bad, img, n);
    patch32(bad, 72, IMG_MAX_LIST_ROWS + 1u);
    fix_crc(bad, n);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "more than 8192 list rows is refused");

    free(bad);
    free(img);
}

/* A row offset that names a string in the list region is invalid even when that
 * string terminates there. */
static void test_row_offset_into_list_region(void)
{
    size_t n = 0;
    uint8_t *img = build_image_lists(&n, ROWS, 2, POOL, POOL_N, LISTS, 2);
    uint8_t *bad = malloc(n);
    snapshot_t *s = NULL;

    memcpy(bad, img, n);
    patch24(bad, IMG_HEADER_SIZE + 12, POOL_N); /* title -> first list name */
    fix_crc(bad, n);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID,
          "a row string offset in the list region is refused");

    /* the same offset one byte before the list region is fine */
    memcpy(bad, img, n);
    patch24(bad, IMG_HEADER_SIZE + 12, POOL_N - 6u); /* inside "jazz" */
    fix_crc(bad, n);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_OK, "a row string inside the row region opens");
    snapshot_close(s);

    free(bad);
    free(img);
}

static void test_vectors(void)
{
    /* Published FNV-1a 64 vectors, and the CRC-32 check value. */
    CHECK_EQ(snapshot_path_hash(""), 0xCBF29CE484222325ull, "fnv empty");
    CHECK_EQ(snapshot_path_hash("a"), 0xAF63DC4C8601EC8Cull, "fnv a");
    CHECK_EQ(snapshot_path_hash("foobar"), 0x85944171F73967E8ull, "fnv foobar");
    CHECK_EQ(fnv1a_ref("Artist/Album/x.mp3"), snapshot_path_hash("Artist/Album/x.mp3"),
             "fnv agrees on a path");
    CHECK_EQ(crc32_ref((const uint8_t *)"123456789", 9), 0xCBF43926u, "crc check value");
}

static void test_validation(void)
{
    size_t n = 0;
    uint8_t *img = build_image(&n, ROWS, 2, POOL, POOL_N);

    snapshot_t *s = open_ok(img, n);
    CHECK(s != NULL, "two-row image opens");
    CHECK_EQ(snapshot_version(s), 3, "version 3");
    CHECK_EQ(snapshot_rows(s), 2, "rows");
    CHECK_EQ(snapshot_generation(s), 1767225600u, "generation");
    CHECK_EQ(snapshot_analyser(s), 300, "analyser");
    CHECK_EQ(snapshot_app_schema(s), 1, "app schema");
    CHECK(snapshot_model_set(s) == 0x0123456789ABCDEFull, "model set");
    snapshot_close(s);

    /* magic */
    uint8_t *bad = malloc(n);
    memcpy(bad, img, n);
    bad[0] = 'X' + 1;
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "bad magic refused");
    memcpy(bad, img, n);

    /* version 2 and 1 are refused: there is no compatibility path. */
    bad[8] = 2;
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "a version 2 image refused");
    bad[8] = 1;
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "a version 1 image refused");
    memcpy(bad, img, n);

    /* header size */
    bad[10] = 64;
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "bad header size refused");
    memcpy(bad, img, n);

    /* row size */
    bad[20] = 64;
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "bad row size refused");
    memcpy(bad, img, n);

    /* total size disagrees with rows and pool */
    patch32(bad, 12, (uint32_t)n + 32);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "total size refused");
    memcpy(bad, img, n);

    /* row_count that would overflow the size arithmetic */
    patch32(bad, 16, 0x08000000u);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "huge row count refused");
    memcpy(bad, img, n);

    /* row_pool_size larger than the whole pool */
    patch32(bad, 76, (uint32_t)POOL_N + 1u);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "a row pool past the pool refused");
    memcpy(bad, img, n);

    /* reserved header bytes, which the checksum does not cover */
    bad[56] = 1;
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "reserved header refused");
    memcpy(bad, img, n);
    bad[63] = 1;
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "the last reserved header byte refused");
    memcpy(bad, img, n);
    bad[70] = 1;
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "reserved list header refused");
    memcpy(bad, img, n);
    bad[71] = 1;
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "the last reserved list byte refused");
    memcpy(bad, img, n);

    /* CRC */
    bad[n - 1] ^= 0xFF;
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "bad crc refused");
    memcpy(bad, img, n);

    /* image shorter than its header */
    CHECK(snapshot_open(bad, 32, &s) == SNAPSHOT_INVALID, "short image refused");
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_OK, "restored image opens");
    snapshot_close(s);

    /* rows out of order */
    memcpy(bad, img, n);
    patch64(bad, IMG_HEADER_SIZE, 0x9999999999999999ull);
    fix_crc(bad, n);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "descending rows refused");

    /* a string offset outside the row pool */
    memcpy(bad, img, n);
    patch24(bad, IMG_HEADER_SIZE + 12, (uint32_t)POOL_N);
    fix_crc(bad, n);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "string offset refused");

    /* a string with no terminator inside the pool */
    memcpy(bad, img, n);
    bad[n - 1] = 'x';
    fix_crc(bad, n);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "unterminated string refused");

    /* a bpm code past 1101 */
    memcpy(bad, img, n);
    patch_field(bad, IMG_HEADER_SIZE, 0, 11, 1102);
    fix_crc(bad, n);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "a bpm past 1800 refused");

    /* a key past 24 */
    memcpy(bad, img, n);
    patch_field(bad, IMG_HEADER_SIZE, 11, 5, 25);
    fix_crc(bad, n);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "key 25 refused");

    /* percentages: 101 and 126 are out of range, 127 is the unknown marker */
    memcpy(bad, img, n);
    patch_field(bad, IMG_HEADER_SIZE, 16, 7, 101);
    fix_crc(bad, n);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "energy 101 refused");
    memcpy(bad, img, n);
    patch_field(bad, IMG_HEADER_SIZE, 44, 7, 126);
    fix_crc(bad, n);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "confidence 126 refused");
    memcpy(bad, img, n);
    patch_field(bad, IMG_HEADER_SIZE, 16, 7, 127);
    fix_crc(bad, n);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_OK, "127 is a valid unknown percentage");
    snapshot_close(s);

    /* a source flag bit that is not defined (the top bits of the word) */
    memcpy(bad, img, n);
    patch_field(bad, IMG_HEADER_SIZE, 62, 2, 1);
    fix_crc(bad, n);
    CHECK(snapshot_open(bad, n, &s) == SNAPSHOT_INVALID, "reserved measurement bits refused");

    /* an empty image: header only, CRC over nothing */
    free(bad);
    free(img);

    size_t empty_n = 0;
    uint8_t *empty = build_image(&empty_n, NULL, 0, "", 0);
    CHECK_EQ(empty_n, IMG_HEADER_SIZE, "an empty image is a header");
    snapshot_t *e = NULL;
    CHECK(snapshot_open(empty, empty_n, &e) == SNAPSHOT_OK, "empty image opens");
    CHECK(e && snapshot_rows(e) == 0, "no rows");
    CHECK(e && snapshot_lists(e) == 0, "no lists");
    snapshot_row_t none;
    CHECK(e && !snapshot_find(e, 1, 1, &none), "an empty image finds nothing");
    snapshot_close(e);
    free(empty);
}

/* The legacy magic and any other name are refused; only K7SNAP opens. */
static void test_magic(void)
{
    size_t n = 0;
    uint8_t *img = build_image(&n, ROWS, 2, POOL, POOL_N);
    snapshot_t *s = NULL;

    /* The field is eight bytes, not the C string: K7SNAP and two NULs. */
    CHECK(memcmp(img, "K7SNAP\0\0", 8) == 0, "the builder writes K7SNAP");
    CHECK(snapshot_open(img, n, &s) == SNAPSHOT_OK, "and it opens");
    snapshot_close(s);

    memcpy(img, "XTAPESNP", 8);
    CHECK(snapshot_open(img, n, &s) == SNAPSHOT_INVALID, "the legacy magic is refused");

    memcpy(img, "K7SNAPq\0", 8);
    CHECK(snapshot_open(img, n, &s) == SNAPSHOT_INVALID, "a wrong magic is still refused");

    free(img);
}

static void test_lookup(void)
{
    size_t n = 0;
    uint8_t *img = build_image(&n, ROWS, 2, POOL, POOL_N);
    snapshot_t *s = open_ok(img, n);
    if (!s) {
        free(img);
        return;
    }

    snapshot_row_t row;
    CHECK(snapshot_find(s, 0x1111111111111111ull, 1000, &row), "first row found");
    CHECK_EQ(row.bpm_x10, 1245, "bpm");
    CHECK_EQ(row.key, 8, "key");
    CHECK_EQ(row.energy, 78, "energy");
    CHECK_EQ(row.mood_bits, SNAPSHOT_MOOD_HAPPY | SNAPSHOT_MOOD_PARTY, "mood bits");
    CHECK(!snapshot_find(s, 0x1111111111111111ull, 1001, &row), "size mismatch is no match");
    CHECK(!snapshot_find(s, 0x3333333333333333ull, 1000, &row), "unknown fingerprint");

    CHECK(snapshot_find(s, 0x2222222222222222ull, 2000, &row), "second row found");
    CHECK_EQ(row.energy, SNAPSHOT_UNKNOWN_PCT, "the file's 127 is exposed as 0xFF");
    CHECK_EQ(row.confidence, SNAPSHOT_UNKNOWN_PCT, "confidence unknown");
    CHECK(strcmp(snapshot_row_string(s, &row, SNAPSHOT_STRING_TITLE), "So What") == 0,
          "title");
    CHECK(strcmp(snapshot_row_string(s, &row, SNAPSHOT_STRING_ARTIST), "Miles Davis") == 0,
          "artist");
    CHECK(strcmp(snapshot_row_string(s, &row, SNAPSHOT_STRING_ALBUM), "Kind of Blue") == 0,
          "album");
    CHECK(strcmp(snapshot_row_string(s, &row, SNAPSHOT_STRING_GENRE), "jazz") == 0, "genre");

    /* The first row's four own offsets find its four strings too. */
    CHECK(snapshot_find(s, 0x1111111111111111ull, 1000, &row), "first row again");
    CHECK(strcmp(snapshot_row_string(s, &row, SNAPSHOT_STRING_TITLE), "Blue Monday") == 0,
          "row 0 title");
    CHECK(strcmp(snapshot_row_string(s, &row, SNAPSHOT_STRING_ARTIST), "New Order") == 0,
          "row 0 artist");
    CHECK(strcmp(snapshot_row_string(s, &row, SNAPSHOT_STRING_ALBUM),
                 "Power, Corruption & Lies") == 0,
          "row 0 album");
    CHECK(strcmp(snapshot_row_string(s, &row, SNAPSHOT_STRING_GENRE), "electronic") == 0,
          "row 0 genre");

    snapshot_close(s);
    free(img);
}

/* Two rows may share one string offset, a 127 percentage round-trips as 0xFF, and the
 * BPM code sits at both ends of its range. */
static void test_shared_strings_predicates_and_bounds(void)
{
    static const char SHARED_POOL[] =
        "Shared Title\0Shared Artist\0Shared Album\0rock\0jazz\0";
    enum {
        SH_TITLE = 0,
        SH_ARTIST = 13,
        SH_ALBUM = 27,
        SH_ROCK = 40,
        SH_JAZZ = 45,
    };
    static const row_t SHARED_ROWS[] = {
        {0xAAAA000000000001ull, 100, SH_TITLE, SH_ARTIST, SH_ALBUM, SH_ROCK, 700, 24,
         FILE_UNKNOWN_PCT, 0, 100, FILE_UNKNOWN_PCT, 0x1F, FILE_UNKNOWN_PCT, 0x3F},
        {0xBBBB000000000002ull, 200, SH_TITLE, SH_ARTIST, SH_ALBUM, SH_JAZZ, 1800, 0,
         0, 100, FILE_UNKNOWN_PCT, 0, 0x01, FILE_UNKNOWN_PCT, 0},
    };
    size_t n = 0;
    uint8_t *img = build_image(&n, SHARED_ROWS, 2, SHARED_POOL, sizeof SHARED_POOL - 1u);

    /* The builder wrote the file's own spelling of unknown, not the C marker. */
    CHECK_EQ((int)((get64(img + IMG_HEADER_SIZE + 24) >> 16) & 0x7Fu), 127,
             "the file stores 127 for unknown");
    /* And both ends of the packed BPM range, as codes 1 and 1101. */
    CHECK_EQ((int)(get64(img + IMG_HEADER_SIZE + 24) & 0x7FFu), 1, "bpm code 1");
    CHECK_EQ((int)(get64(img + IMG_HEADER_SIZE + IMG_ROW_SIZE + 24) & 0x7FFu), 1101,
             "bpm code 1101");

    snapshot_t *s = open_ok(img, n);
    if (!s) {
        free(img);
        return;
    }
    snapshot_row_t row;
    CHECK(snapshot_find(s, SHARED_ROWS[0].fp, 100, &row), "the 700 BPM row");
    CHECK_EQ(row.bpm_x10, 700, "the bottom of the BPM range");
    CHECK_EQ(row.key, 24, "12B");
    CHECK_EQ(row.energy, SNAPSHOT_UNKNOWN_PCT, "unknown energy");
    CHECK_EQ(row.danceability, 0, "a measured zero");
    CHECK_EQ(row.valence, 100, "the top percentage");
    CHECK_EQ(row.arousal, SNAPSHOT_UNKNOWN_PCT, "unknown arousal");
    CHECK_EQ(row.confidence, SNAPSHOT_UNKNOWN_PCT, "unknown confidence");
    CHECK_EQ(row.mood_bits, 0x1F, "all five mood bits");
    CHECK_EQ(row.source_flags, 0x3F, "all six source flags");
    CHECK(strcmp(snapshot_row_string(s, &row, SNAPSHOT_STRING_TITLE), "Shared Title") == 0,
          "the shared title");

    CHECK(snapshot_find(s, SHARED_ROWS[1].fp, 200, &row), "the 1800 BPM row");
    CHECK_EQ(row.bpm_x10, 1800, "the top of the BPM range");
    CHECK_EQ(row.key, 0, "unknown key");
    CHECK_EQ(row.energy, 0, "a measured zero is not unknown");
    CHECK_EQ(row.danceability, 100, "the top danceability");
    CHECK_EQ(row.valence, SNAPSHOT_UNKNOWN_PCT, "unknown valence");
    CHECK_EQ(row.confidence, SNAPSHOT_UNKNOWN_PCT, "unknown confidence again");
    CHECK(strcmp(snapshot_row_string(s, &row, SNAPSHOT_STRING_TITLE), "Shared Title") == 0,
          "the same offset resolves for both rows");
    CHECK(strcmp(snapshot_row_string(s, &row, SNAPSHOT_STRING_GENRE), "jazz") == 0,
          "the second genre");
    snapshot_close(s);
    free(img);
}

static void test_keys(void)
{
    CHECK_EQ(snapshot_key_parse("8A"), 8, "8A");
    CHECK_EQ(snapshot_key_parse("12B"), 24, "12B");
    CHECK_EQ(snapshot_key_parse("1A"), 1, "1A");
    CHECK_EQ(snapshot_key_parse("8a"), 8, "lower case");
    CHECK_EQ(snapshot_key_parse("Am"), 8, "A minor");
    CHECK_EQ(snapshot_key_parse("A minor"), 8, "A minor, spelled out");
    CHECK_EQ(snapshot_key_parse("F#m"), 11, "F# minor");
    CHECK_EQ(snapshot_key_parse("Bb"), 18, "Bb major is 6B");
    CHECK_EQ(snapshot_key_parse("A major"), 23, "A major is 11B");
    CHECK_EQ(snapshot_key_parse("Cb"), 13, "Cb is B major, 1B");
    CHECK_EQ(snapshot_key_parse("G#m"), 1, "G#m is Abm");
    CHECK_EQ(snapshot_key_parse(""), 0, "empty");
    CHECK_EQ(snapshot_key_parse("H"), 0, "not a note");
    CHECK_EQ(snapshot_key_parse("13A"), 0, "13A is not Camelot");
    CHECK_EQ(snapshot_key_parse("Am7"), 0, "a chord is not a key");

    snapshot_sort_t sort;
    CHECK(snapshot_sort_parse("bpm", &sort) && sort == SNAPSHOT_SORT_BPM, "sort bpm");
    CHECK(snapshot_sort_parse("path", &sort) && sort == SNAPSHOT_SORT_PATH, "sort path");
    CHECK(snapshot_sort_parse("title", &sort) && sort == SNAPSHOT_SORT_TITLE, "sort title");
    CHECK(!snapshot_sort_parse("duration", &sort), "duration is not a sort");
}

static void test_filters(void)
{
    size_t n = 0;
    uint8_t *img = build_image(&n, ROWS, 2, POOL, POOL_N);
    snapshot_t *s = open_ok(img, n);
    if (!s) {
        free(img);
        return;
    }

    const uint64_t fp0 = ROWS[0].fp, fp1 = ROWS[1].fp;
    (void)fp1;
    const char *path0 = "electronic/New Order/Power, Corruption & Lies/01 Blue Monday.mp3";
    const char *path1 = "jazz/Miles Davis/Kind of Blue/01 So What.mp3";
    int64_t key = 0;
    const char *str = NULL;

    /* The fingerprints in the fixture are synthetic, so re-key the rows through the
     * real fingerprint of their paths: that is what snapshot_matches computes. */
    row_t rows[2];
    memcpy(rows, ROWS, sizeof rows);
    rows[0].fp = snapshot_path_hash(path0);
    rows[1].fp = snapshot_path_hash(path1);
    uint8_t *img2 = build_image(&n, rows, 2, POOL, POOL_N);
    snapshot_t *s2 = open_ok(img2, n);
    snapshot_close(s);
    if (!s2) {
        free(img);
        free(img2);
        return;
    }
    (void)fp0;

    snapshot_filter_t f;
    snapshot_filter_defaults(&f);

    CHECK(snapshot_matches(s2, &f, path0, 1000, &key, &str), "no filter matches");
    CHECK(snapshot_matches(s2, &f, "x/y.mp3", 1, &key, &str),
          "a track with no row matches an empty filter");

    /* name: in the path */
    f.query = "miles";
    CHECK(snapshot_matches(s2, &f, path1, 2000, &key, &str), "query matches the path");
    CHECK(!snapshot_matches(s2, &f, path0, 1000, &key, &str), "query excludes the other");

    /* name: only in the tagged title, which the path does not contain */
    f.query = "blue monday";
    CHECK(snapshot_matches(s2, &f, path0, 1000, &key, &str), "query matches the title");
    f.query = "not there";
    CHECK(!snapshot_matches(s2, &f, path0, 1000, &key, &str), "query misses");
    f.query = NULL;

    /* bpm range */
    f.bpm_min_x10 = 1200;
    f.bpm_max_x10 = 1300;
    f.sort = SNAPSHOT_SORT_BPM;
    CHECK(snapshot_matches(s2, &f, path0, 1000, &key, &str), "bpm in range");
    CHECK_EQ(key, 1245, "sort key is the bpm");
    CHECK(!snapshot_matches(s2, &f, path1, 2000, &key, &str), "bpm out of range");
    f.sort = SNAPSHOT_SORT_PATH;

    /* a track with no row cannot match a bpm filter */
    CHECK(!snapshot_matches(s2, &f, "unknown/track.mp3", 10, &key, &str),
          "no row, no bpm match");
    f.bpm_min_x10 = f.bpm_max_x10 = 0;

    /* energy, danceability and their unknown markers */
    f.energy_min = 70;
    CHECK(snapshot_matches(s2, &f, path0, 1000, &key, &str), "energy above 70");
    CHECK(!snapshot_matches(s2, &f, path1, 2000, &key, &str), "unknown energy excluded");
    f.energy_min = SNAPSHOT_UNKNOWN_PCT;
    /* 0 is a legal bound, not "unset": a maximum of 0 must exclude every track. */
    f.energy_max = 0;
    CHECK(!snapshot_matches(s2, &f, path0, 1000, &key, &str), "energy_max 0 excludes");
    f.energy_max = SNAPSHOT_UNKNOWN_PCT;
    f.danceability_min = 50;
    CHECK(snapshot_matches(s2, &f, path0, 1000, &key, &str), "danceability above 50");
    CHECK(!snapshot_matches(s2, &f, path1, 2000, &key, &str), "unknown danceability excluded");
    f.danceability_min = SNAPSHOT_UNKNOWN_PCT;

    /* key mask */
    f.key_mask = (uint16_t)(1u << (8 - 1));
    CHECK(snapshot_matches(s2, &f, path0, 1000, &key, &str), "8A in the mask");
    CHECK(!snapshot_matches(s2, &f, path1, 2000, &key, &str), "5A not in the mask");
    f.key_mask = 0;

    /* mood */
    f.mood_mask = SNAPSHOT_MOOD_PARTY;
    CHECK(snapshot_matches(s2, &f, path0, 1000, &key, &str), "mood bit set");
    CHECK(!snapshot_matches(s2, &f, path1, 2000, &key, &str), "mood bit clear");
    f.mood_mask = 0;

    /* genre */
    f.genre = "electro";
    CHECK(snapshot_matches(s2, &f, path0, 1000, &key, &str), "genre substring");
    CHECK(!snapshot_matches(s2, &f, path1, 2000, &key, &str), "genre miss");
    f.genre = NULL;

    /* sort keys */
    f.sort = SNAPSHOT_SORT_BPM;
    CHECK(snapshot_matches(s2, &f, path0, 1000, &key, &str), "bpm sort matches");
    CHECK_EQ(key, 1245, "bpm key");
    f.sort = SNAPSHOT_SORT_DANCEABILITY;
    CHECK(snapshot_matches(s2, &f, path0, 1000, &key, &str), "danceability sort matches");
    CHECK_EQ(key, 61, "danceability key");
    CHECK(snapshot_matches(s2, &f, path1, 2000, &key, &str),
          "row 1 matches a danceability sort");
    CHECK_EQ(key, 40, "row 1's danceability");
    CHECK(snapshot_matches(s2, &f, "unknown/track.mp3", 10, &key, &str),
          "a track with no row still matches a sort");
    CHECK(key == INT64_MIN, "and has no value");
    f.sort = SNAPSHOT_SORT_TITLE;
    CHECK(snapshot_matches(s2, &f, path0, 1000, &key, &str), "title sort matches");
    CHECK(str && strcmp(str, "Blue Monday") == 0, "title sort string");

    /* an untitled row has no title to sort by */
    size_t n3 = 0;
    row_t untitled = {0};
    untitled.fp = snapshot_path_hash("No Tags/x.mp3");
    untitled.size = 10;
    uint8_t *img3 = build_image(&n3, &untitled, 1, "\0\0\0\0", 4);
    snapshot_t *s3 = open_ok(img3, n3);
    if (s3) {
        snapshot_filter_t g;
        snapshot_filter_defaults(&g);
        g.sort = SNAPSHOT_SORT_TITLE;
        CHECK(snapshot_matches(s3, &g, "No Tags/x.mp3", 10, &key, &str), "untitled matches");
        CHECK(str == NULL, "and has no title value, so it sorts last");
        snapshot_close(s3);
    }
    free(img3);

    snapshot_close(s2);
    free(img);
    free(img2);
}

static void test_hits(void)
{
    snapshot_hits_t *h = snapshot_hits_new(3);
    CHECK(h != NULL, "hits allocated");
    CHECK(snapshot_hits_add(h, 0, 50, NULL), "add 1");
    CHECK(snapshot_hits_add(h, 1, 30, NULL), "add 2");
    CHECK(snapshot_hits_add(h, 2, 40, NULL), "add 3");
    CHECK_EQ(snapshot_hits_count(h), 3, "filled to the cap");
    /* best first: 30, 40, 50 */
    CHECK_EQ(snapshot_hits_index(h, 0), 1, "best first");
    CHECK_EQ(snapshot_hits_index(h, 1), 2, "then 40");
    CHECK_EQ(snapshot_hits_index(h, 2), 0, "then 50");

    CHECK(snapshot_hits_add(h, 3, 10, NULL), "a better candidate is taken");
    CHECK_EQ(snapshot_hits_count(h), 3, "still capped");
    CHECK_EQ(snapshot_hits_index(h, 0), 3, "the new best");
    CHECK_EQ(snapshot_hits_index(h, 1), 1, "30");
    CHECK_EQ(snapshot_hits_index(h, 2), 2, "40");

    CHECK(!snapshot_hits_add(h, 4, 90, NULL), "a worse candidate is refused");
    CHECK_EQ(snapshot_hits_count(h), 3, "unchanged");

    /* equal keys break by track index */
    snapshot_hits_clear(h);
    CHECK(snapshot_hits_add(h, 7, 10, NULL), "tie 1");
    CHECK(snapshot_hits_add(h, 5, 10, NULL), "tie 2");
    CHECK_EQ(snapshot_hits_index(h, 0), 5, "lower index first");

    /* INT64_MIN (no value) sorts last */
    snapshot_hits_clear(h);
    CHECK(snapshot_hits_add(h, 0, INT64_MIN, NULL), "none 1");
    CHECK(snapshot_hits_add(h, 1, 5, NULL), "value");
    CHECK_EQ(snapshot_hits_index(h, 0), 1, "value first");
    CHECK_EQ(snapshot_hits_index(h, 1), 0, "none last");

    /* strings order the title sort */
    snapshot_hits_clear(h);
    CHECK(snapshot_hits_add(h, 0, 0, "b"), "str b");
    CHECK(snapshot_hits_add(h, 1, 0, "A"), "str A");
    CHECK_EQ(snapshot_hits_index(h, 0), 1, "case-insensitive order");

    snapshot_hits_free(h);
}

/* --------------------------------------------------------------- applying */

typedef struct {
    const uint8_t *p;
    uint32_t       n;
} mem_src_t;

static uint32_t src_size(void *ctx) { return ((mem_src_t *)ctx)->n; }

static bool src_read(void *ctx, uint32_t off, uint8_t *data, uint32_t n)
{
    mem_src_t *s = (mem_src_t *)ctx;
    if ((uint64_t)off + n > s->n) return false;
    memcpy(data, s->p + off, n);
    return true;
}

typedef struct {
    uint8_t *p;
    uint32_t cap;
    bool     fail_erase;
    bool     fail_write;
    bool     flip_on_write; /* the flash accepts the write and keeps something else */
} mem_tgt_t;

static bool tgt_erase(void *ctx, uint32_t off, uint32_t n)
{
    mem_tgt_t *t = (mem_tgt_t *)ctx;
    if (t->fail_erase || (uint64_t)off + n > t->cap) return false;
    memset(t->p + off, 0xFF, n);
    return true;
}
static bool tgt_write(void *ctx, uint32_t off, const uint8_t *data, uint32_t n)
{
    mem_tgt_t *t = (mem_tgt_t *)ctx;
    if (t->fail_write || (uint64_t)off + n > t->cap) return false;
    memcpy(t->p + off, data, n);
    if (t->flip_on_write && n) t->p[off + n - 1] ^= 0x01;
    return true;
}
static bool tgt_read(void *ctx, uint32_t off, uint8_t *data, uint32_t n)
{
    mem_tgt_t *t = (mem_tgt_t *)ctx;
    if ((uint64_t)off + n > t->cap) return false;
    memcpy(data, t->p + off, n);
    return true;
}

static void test_apply(void)
{
    size_t n = 0;
    uint8_t *img = build_image(&n, ROWS, 2, POOL, POOL_N);

    mem_src_t src = {img, (uint32_t)n};
    snapshot_source_t S = {&src, src_size, src_read};

    uint8_t *flash = malloc(n + 4096);
    mem_tgt_t tgt = {flash, (uint32_t)(n + 4096), false, false, false};
    snapshot_target_t T = {&tgt, tgt.cap, tgt_erase, tgt_write, tgt_read};

    char why[96] = {0};
    CHECK(snapshot_apply(&S, &T, why, sizeof why) == SNAPSHOT_OK, "apply succeeds: %s", why);
    CHECK(memcmp(flash, img, n) == 0, "the partition holds the image");
    /* and the device's own reader accepts what was written */
    snapshot_t *s = NULL;
    CHECK(snapshot_open(flash, n, &s) == SNAPSHOT_OK, "the written image opens");
    snapshot_close(s);

    /* a source shorter than its header claims */
    mem_src_t shortsrc = {img, (uint32_t)n - 1};
    snapshot_source_t S2 = {&shortsrc, src_size, src_read};
    CHECK(snapshot_apply(&S2, &T, why, sizeof why) == SNAPSHOT_INVALID,
          "a truncated file is refused");

    /* a header that lies about its checksum */
    uint8_t *bad = malloc(n);
    memcpy(bad, img, n);
    bad[36] ^= 0xFF;
    mem_src_t badsrc = {bad, (uint32_t)n};
    snapshot_source_t S3 = {&badsrc, src_size, src_read};
    CHECK(snapshot_apply(&S3, &T, why, sizeof why) == SNAPSHOT_INVALID,
          "a bad checksum is refused");

    /* a file the reader would refuse must not cost the image already in flash: the
     * row count is patched and the checksum left intact, so only the arithmetic
     * catches it. */
    uint8_t *flash_before = malloc(n + 4096);
    memcpy(flash_before, flash, n + 4096);
    uint8_t *bad_arith = malloc(n);
    memcpy(bad_arith, img, n);
    bad_arith[16] = (uint8_t)(bad_arith[16] + 1); /* row_count, little-endian */
    mem_src_t arith_src = {bad_arith, (uint32_t)n};
    snapshot_source_t S4 = {&arith_src, src_size, src_read};
    mem_tgt_t tgt_before = {flash, (uint32_t)(n + 4096), false, false, false};
    snapshot_target_t T6 = {&tgt_before, tgt_before.cap, tgt_erase, tgt_write, tgt_read};
    CHECK(snapshot_apply(&S4, &T6, why, sizeof why) == SNAPSHOT_INVALID,
          "a bad row count is refused: %s", why);
    CHECK(memcmp(flash, flash_before, n + 4096) == 0,
          "and the partition was not erased for it");
    free(bad_arith);
    free(flash_before);

    /* a target that cannot write */
    mem_tgt_t nowrite = {flash, (uint32_t)(n + 4096), false, true, false};
    snapshot_target_t T2 = {&nowrite, nowrite.cap, tgt_erase, tgt_write, tgt_read};
    CHECK(snapshot_apply(&S, &T2, why, sizeof why) == SNAPSHOT_INVALID, "a failed write is refused");

    /* a target whose erase fails */
    mem_tgt_t noerase = {flash, (uint32_t)(n + 4096), true, false, false};
    snapshot_target_t T3 = {&noerase, noerase.cap, tgt_erase, tgt_write, tgt_read};
    CHECK(snapshot_apply(&S, &T3, why, sizeof why) == SNAPSHOT_INVALID, "a failed erase is refused");

    /* a target too small */
    mem_tgt_t small = {flash, (uint32_t)n - 1, false, false, false};
    snapshot_target_t T4 = {&small, small.cap, tgt_erase, tgt_write, tgt_read};
    CHECK(snapshot_apply(&S, &T4, why, sizeof why) == SNAPSHOT_INVALID, "a small partition is refused");

    /* a write the flash does not keep: the read-back is what catches it */
    mem_tgt_t liar = {flash, (uint32_t)(n + 4096), false, false, true};
    snapshot_target_t T5 = {&liar, liar.cap, tgt_erase, tgt_write, tgt_read};
    CHECK(snapshot_apply(&S, &T5, why, sizeof why) == SNAPSHOT_INVALID,
          "a write the flash did not keep is refused: %s", why);

    free(bad);
    free(flash);
    free(img);
}

static int cmp_row_fp(const void *a, const void *b)
{
    const row_t *x = (const row_t *)a, *y = (const row_t *)b;
    return x->fp < y->fp ? -1 : x->fp > y->fp ? 1 : 0;
}

/* The stated target library: 5,000 rows, which is where the binary search and the
 * size arithmetic have to hold together. */
static void test_many_rows(void)
{
    enum { N = 5000 };
    row_t *rows = calloc(N, sizeof *rows);
    CHECK(rows != NULL, "rows allocated");
    if (!rows) return;

    for (int i = 0; i < N; i++) {
        char path[32];
        snprintf(path, sizeof path, "Artist/Album/%05d.mp3", i);
        rows[i].fp = snapshot_path_hash(path);
        rows[i].size = 1000u + (uint32_t)i;
        rows[i].title_off = 0;
        rows[i].artist_off = 1;
        rows[i].album_off = 2;
        rows[i].genre_off = 3;
        rows[i].bpm_x10 = (uint16_t)(700 + i % 1100);
        rows[i].key = (uint8_t)(1 + i % 24);
        rows[i].energy = (uint8_t)(i % 101);
        rows[i].dance = (uint8_t)(i % 101);
        rows[i].valence = (uint8_t)(i % 101);
        rows[i].arousal = (uint8_t)(i % 101);
        rows[i].mood = (uint8_t)(1u << (i % 5));
        rows[i].conf = 50;
    }
    qsort(rows, N, sizeof *rows, cmp_row_fp);

    size_t n = 0;
    uint8_t *img = build_image(&n, rows, N, "\0\0\0\0", 4);
    CHECK_EQ(n, IMG_HEADER_SIZE + N * IMG_ROW_SIZE + 4, "image size at 5,000 rows");

    snapshot_t *s = open_ok(img, n);
    if (s) {
        CHECK_EQ(snapshot_rows(s), N, "5,000 rows");
        int found = 0;
        for (int i = 0; i < N; i += 97) {
            snapshot_row_t row;
            if (snapshot_find(s, rows[i].fp, rows[i].size, &row) &&
                row.bpm_x10 == rows[i].bpm_x10)
                found++;
        }
        CHECK_EQ(found, (N + 96) / 97, "every sampled row is found");
        snapshot_row_t row;
        CHECK(!snapshot_find(s, rows[0].fp, rows[0].size + 1, &row), "a size mismatch still misses");
        snapshot_close(s);
    }
    free(img);
    free(rows);
}

int main(void)
{
    test_vectors();
    test_validation();
    test_magic();
    test_lookup();
    test_shared_strings_predicates_and_bounds();
    test_keys();
    test_filters();
    test_hits();
    test_apply();
    test_lists();
    test_list_rejects();
    test_row_offset_into_list_region();
    test_many_rows();
    TEST_END();
}
