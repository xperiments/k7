/*
 * snapshot - see include/snapshot.h and docs/specs/snapshot-format.md.
 *
 * The image is hostile input: it arrives on a card that anyone can write, so every
 * offset is checked before it is used and a failed check is a refusal, not a clamp.
 * The one thing this module trusts is that it validated the image first; after
 * `snapshot_open` the accessors read fields that were already range-checked.
 */
#include "snapshot.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ bytes */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

static uint32_t crc_table[256];
static bool     crc_ready;

/* Built once, at boot, before anything can be concurrent: the only callers are
 * snapshot_open (boot, or a host test's main) and the update path. */
static const uint32_t *crc_init(void)
{
    if (crc_ready) return crc_table;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        crc_table[i] = c;
    }
    crc_ready = true;
    return crc_table;
}

uint32_t snapshot_crc_begin(void) { return 0xFFFFFFFFu; }

uint32_t snapshot_crc_feed(uint32_t state, const uint8_t *p, size_t n)
{
    const uint32_t *t = crc_init();
    uint32_t crc = state;
    for (size_t i = 0; i < n; i++) crc = t[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return crc;
}

uint32_t snapshot_crc_end(uint32_t state) { return ~state; }

uint32_t snapshot_crc32(const uint8_t *p, size_t n)
{
    return snapshot_crc_end(snapshot_crc_feed(snapshot_crc_begin(), p, n));
}

uint64_t snapshot_path_hash(const char *relative_path)
{
    uint64_t h = 0xCBF29CE484222325ull;
    if (!relative_path) return h;
    for (const unsigned char *p = (const unsigned char *)relative_path; *p; p++) {
        h ^= *p;
        h *= 0x00000100000001B3ull;
    }
    return h;
}

/* ------------------------------------------------------------------ struct */

struct snapshot {
    const uint8_t *image;
    size_t         size;
    const uint8_t *rows;
    uint32_t       row_count;
    const uint8_t *pool;
    uint32_t       pool_size;
    uint32_t       generation;
    uint32_t       analyser;
    uint32_t       app_schema;
    uint64_t       model_set;
};

/* ------------------------------------------------------------------ open */

static snapshot_result_t fail(char *why, size_t why_size, const char *reason)
{
    if (why && why_size) {
        size_t n = strlen(reason);
        if (n >= why_size) n = why_size - 1;
        memcpy(why, reason, n);
        why[n] = 0;
    }
    return SNAPSHOT_INVALID;
}

/* The four strings of one row, walked from the pool; true when all four terminate
 * inside it. `lens`, when non-NULL, receives their lengths. */
static bool strings_ok(const uint8_t *pool, uint32_t pool_size, uint32_t off,
                       uint16_t lens[4])
{
    if (off >= pool_size) return false;
    uint32_t i = off;
    for (int s = 0; s < 4; s++) {
        uint32_t start = i;
        while (i < pool_size && pool[i]) i++;
        if (i >= pool_size) return false;
        if (lens) lens[s] = (uint16_t)(i - start);
        i++; /* the NUL */
    }
    return true;
}

static bool pct_ok(uint8_t v) { return v <= 100u || v == SNAPSHOT_UNKNOWN_PCT; }

static bool row_ok(const uint8_t *row, const uint8_t *pool, uint32_t pool_size)
{
    uint16_t bpm = rd16(row + 16);
    uint8_t  key = row[18];
    uint8_t  source = row[25];
    if (bpm != 0 && (bpm < 700u || bpm > 1800u)) return false;
    if (key > 24) return false;
    if (!pct_ok(row[19]) || !pct_ok(row[20]) || !pct_ok(row[21]) || !pct_ok(row[22]) ||
        !pct_ok(row[24]))
        return false;
    if ((row[23] & 0xE0u) != 0) return false;
    if ((source & 0xC0u) != 0) return false;
    for (int i = 26; i < 32; i++)
        if (row[i]) return false;
    return strings_ok(pool, pool_size, rd32(row + 12), NULL);
}

static bool bad(char *why, size_t why_size, const char *reason)
{
    fail(why, why_size, reason);
    return false;
}

/* Every structural rule that can be decided from the header alone. Both the reader
 * and the update path run it: an update must not erase a partition for a file the
 * reader would refuse. */
static bool header_check(const uint8_t *hdr, uint32_t size, uint32_t *rows_out,
                         uint32_t *pool_out, uint32_t *total_out, char *why, size_t why_size)
{
    if (size < SNAPSHOT_HEADER_SIZE) return bad(why, why_size, "image shorter than its header");
    if (memcmp(hdr, SNAPSHOT_MAGIC, 8) != 0) return bad(why, why_size, "not a snapshot");
    if (rd16(hdr + 8) != SNAPSHOT_VERSION) return bad(why, why_size, "unknown format version");
    if (rd16(hdr + 10) != SNAPSHOT_HEADER_SIZE) return bad(why, why_size, "bad header size");
    if (rd32(hdr + 20) != SNAPSHOT_ROW_SIZE) return bad(why, why_size, "bad row size");
    for (int i = 56; i < (int)SNAPSHOT_HEADER_SIZE; i++)
        if (hdr[i]) return bad(why, why_size, "reserved header bytes are not zero");

    uint32_t total = rd32(hdr + 12);
    uint32_t rows = rd32(hdr + 16);
    uint32_t pool = rd32(hdr + 32);
    uint64_t want = (uint64_t)SNAPSHOT_HEADER_SIZE + (uint64_t)rows * SNAPSHOT_ROW_SIZE + pool;
    if (want != total) return bad(why, why_size, "sizes disagree");
    if (total != size) return bad(why, why_size, "image is not its stated length");
    if (total > SNAPSHOT_MAX_BYTES) return bad(why, why_size, "image is larger than any partition");

    *rows_out = rows;
    *pool_out = pool;
    *total_out = total;
    return true;
}

/* Feed one chunk's payload (everything after the header) into a running CRC. */
static uint32_t feed_payload(uint32_t crc, uint32_t off, const uint8_t *buf, uint32_t n)
{
    uint32_t skip = off < SNAPSHOT_HEADER_SIZE ? SNAPSHOT_HEADER_SIZE - off : 0;
    return skip < n ? snapshot_crc_feed(crc, buf + skip, n - skip) : crc;
}

snapshot_result_t snapshot_open_why(const uint8_t *image, size_t size, snapshot_t **out,
                                    char *why, size_t why_size)
{
    if (why && why_size) why[0] = 0;
    if (!image || !out) return SNAPSHOT_INVALID;
    *out = NULL;
    if (size > UINT32_MAX) return fail(why, why_size, "image is larger than any partition");

    uint32_t rows = 0, pool_size = 0, total = 0;
    if (!header_check(image, (uint32_t)size, &rows, &pool_size, &total, why, why_size))
        return SNAPSHOT_INVALID;

    if (snapshot_crc32(image + SNAPSHOT_HEADER_SIZE, size - SNAPSHOT_HEADER_SIZE) !=
        rd32(image + 36))
        return fail(why, why_size, "checksum mismatch");

    const uint8_t *rowp = image + SNAPSHOT_HEADER_SIZE;
    const uint8_t *pool = rowp + (size_t)rows * SNAPSHOT_ROW_SIZE;

    uint64_t prev = 0;
    for (uint32_t i = 0; i < rows; i++) {
        const uint8_t *r = rowp + (size_t)i * SNAPSHOT_ROW_SIZE;
        uint64_t fp = rd64(r);
        if (i && fp <= prev) return fail(why, why_size, "rows are not sorted by fingerprint");
        prev = fp;
        if (!row_ok(r, pool, pool_size)) return fail(why, why_size, "a row is out of range");
    }

    snapshot_t *s = (snapshot_t *)calloc(1, sizeof *s);
    if (!s) return SNAPSHOT_NO_MEM;
    s->image = image;
    s->size = size;
    s->rows = rowp;
    s->row_count = rows;
    s->pool = pool;
    s->pool_size = pool_size;
    s->generation = (uint32_t)rd64(image + 24);
    s->analyser = rd32(image + 40);
    s->app_schema = rd32(image + 44);
    s->model_set = rd64(image + 48);
    *out = s;
    return SNAPSHOT_OK;
}

snapshot_result_t snapshot_open(const uint8_t *image, size_t size, snapshot_t **out)
{
    return snapshot_open_why(image, size, out, NULL, 0);
}

void snapshot_close(snapshot_t *s) { free(s); }

uint32_t snapshot_rows(const snapshot_t *s) { return s ? s->row_count : 0; }
uint32_t snapshot_generation(const snapshot_t *s) { return s ? s->generation : 0; }
uint32_t snapshot_analyser(const snapshot_t *s) { return s ? s->analyser : 0; }
uint32_t snapshot_app_schema(const snapshot_t *s) { return s ? s->app_schema : 0; }
uint64_t snapshot_model_set(const snapshot_t *s) { return s ? s->model_set : 0; }

static const uint8_t *row_at(const snapshot_t *s, uint32_t i)
{
    return s->rows + (size_t)i * SNAPSHOT_ROW_SIZE;
}

static void row_read(const uint8_t *r, snapshot_row_t *out)
{
    out->fingerprint = rd64(r);
    out->size = rd32(r + 8);
    out->string_offset = rd32(r + 12);
    out->bpm_x10 = rd16(r + 16);
    out->key = r[18];
    out->energy = r[19];
    out->danceability = r[20];
    out->valence = r[21];
    out->arousal = r[22];
    out->mood_bits = r[23];
    out->confidence = r[24];
    out->source_flags = r[25];
}

bool snapshot_find(const snapshot_t *s, uint64_t fingerprint, uint32_t size,
                   snapshot_row_t *row)
{
    if (!s || !s->row_count) return false;
    uint32_t lo = 0, hi = s->row_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        uint64_t fp = rd64(row_at(s, mid));
        if (fp < fingerprint)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo >= s->row_count) return false;
    const uint8_t *r = row_at(s, lo);
    if (rd64(r) != fingerprint) return false;
    /* The size is part of the binding: same path, different file is not this row. */
    if (rd32(r + 8) != size) return false;
    if (row) row_read(r, row);
    return true;
}

const char *snapshot_row_string(const snapshot_t *s, const snapshot_row_t *row,
                                snapshot_string_t which)
{
    if (!s || !row || row->string_offset >= s->pool_size) return "";
    uint32_t i = row->string_offset;
    for (int k = 0; k < (int)which; k++) {
        while (i < s->pool_size && s->pool[i]) i++;
        if (i >= s->pool_size) return "";
        i++;
    }
    if (i >= s->pool_size) return "";
    return (const char *)(s->pool + i);
}

/* ------------------------------------------------------------------ keys */

/* Pitch class to Camelot number, 1..12. The wheel has one slot per pitch class in
 * each mode, so this is a pair of twelve-entry tables rather than a chain of ifs. */
static const uint8_t MINOR_WHEEL[12] = {5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10};
static const uint8_t MAJOR_WHEEL[12] = {8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1};

static int note_pc(char letter)
{
    switch (letter) {
    case 'C': return 0;
    case 'D': return 2;
    case 'E': return 4;
    case 'F': return 5;
    case 'G': return 7;
    case 'A': return 9;
    case 'B': return 11;
    default: return -1;
    }
}

uint8_t snapshot_key_parse(const char *text)
{
    if (!text) return 0;
    while (*text == ' ' || *text == '\t') text++;

    /* Camelot first: one or two digits and a letter, nothing else. */
    const char *p = text;
    int num = 0, digits = 0;
    while (*p >= '0' && *p <= '9' && digits < 3) {
        num = num * 10 + (*p - '0');
        p++;
        digits++;
    }
    if (digits >= 1 && digits <= 2) {
        char letter = (char)toupper((unsigned char)*p);
        if ((letter == 'A' || letter == 'B') && p[1] == 0 && num >= 1 && num <= 12)
            return (uint8_t)(num + (letter == 'B' ? 12 : 0));
        return 0;
    }

    /* Then a note name: [A-G], an optional accidental, then an optional mode. */
    int pc = note_pc((char)toupper((unsigned char)text[0]));
    if (pc < 0) return 0;
    p = text + 1;
    if (*p == '#') {
        pc = (pc + 1) % 12;
        p++;
    } else if (*p == 'b') {
        pc = (pc + 11) % 12;
        p++;
    }
    while (*p == ' ') p++;

    /* The mode suffix: nothing, "m", "min", "minor", "maj" or "major". A bare "m"
     * is minor; a bare letter is major. Anything else after the note is not a key. */
    bool major = true;
    if (*p == 'm' || *p == 'M') {
        p++;
        if (*p == 'i' || *p == 'I') {
            p++;
            if (*p == 'n' || *p == 'N') p++;
            if (*p == 'o' || *p == 'O') p++;
            if (*p == 'r' || *p == 'R') p++;
            major = false;
        } else if (*p == 'a' || *p == 'A') {
            p++;
            if (*p == 'j' || *p == 'J') p++;
            if (*p == 'o' || *p == 'O') p++;
            if (*p == 'r' || *p == 'R') p++;
        } else {
            major = false;
        }
    }
    while (*p == ' ') p++;
    if (*p) return 0;

    return (uint8_t)((major ? MAJOR_WHEEL[pc] : MINOR_WHEEL[pc]) + (major ? 12 : 0));
}

bool snapshot_sort_parse(const char *name, snapshot_sort_t *out)
{
    if (!name) return false;
    if (strcmp(name, "path") == 0) {
        *out = SNAPSHOT_SORT_PATH;
    } else if (strcmp(name, "title") == 0) {
        *out = SNAPSHOT_SORT_TITLE;
    } else if (strcmp(name, "bpm") == 0) {
        *out = SNAPSHOT_SORT_BPM;
    } else if (strcmp(name, "energy") == 0) {
        *out = SNAPSHOT_SORT_ENERGY;
    } else if (strcmp(name, "danceability") == 0) {
        *out = SNAPSHOT_SORT_DANCEABILITY;
    } else {
        return false;
    }
    return true;
}

void snapshot_filter_defaults(snapshot_filter_t *f)
{
    if (!f) return;
    memset(f, 0, sizeof *f);
    f->energy_min = f->energy_max = SNAPSHOT_UNKNOWN_PCT;
    f->danceability_min = f->danceability_max = SNAPSHOT_UNKNOWN_PCT;
    f->valence_min = f->valence_max = SNAPSHOT_UNKNOWN_PCT;
    f->arousal_min = f->arousal_max = SNAPSHOT_UNKNOWN_PCT;
}

static const char *const MOOD_NAMES[5] = {"happy", "sad", "aggressive", "relaxed", "party"};

const char *snapshot_mood_name(unsigned bit)
{
    return bit < 5 ? MOOD_NAMES[bit] : NULL;
}

int snapshot_mood_from_name(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < 5; i++) {
        const char *a = name, *b = MOOD_NAMES[i];
        while (*a && *b) {
            char x = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
            char y = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
            if (x != y) break;
            a++;
            b++;
        }
        if (!*a && !*b) return i;
    }
    return -1;
}

/* ------------------------------------------------------------------ match */

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static bool fold_contains(const char *hay, const char *needle)
{
    if (!hay || !needle || !needle[0]) return false;
    size_t hn = strlen(hay), nn = strlen(needle);
    if (nn > hn) return false;
    for (size_t i = 0; i + nn <= hn; i++) {
        size_t k = 0;
        while (k < nn && lower(hay[i + k]) == lower(needle[k])) k++;
        if (k == nn) return true;
    }
    return false;
}

static bool name_matches(const snapshot_t *s, const snapshot_row_t *row, const char *relpath,
                         const char *query)
{
    if (fold_contains(relpath, query)) return true;
    if (!row) return false;
    return fold_contains(snapshot_row_string(s, row, SNAPSHOT_STRING_TITLE), query) ||
           fold_contains(snapshot_row_string(s, row, SNAPSHOT_STRING_ARTIST), query) ||
           fold_contains(snapshot_row_string(s, row, SNAPSHOT_STRING_ALBUM), query) ||
           fold_contains(snapshot_row_string(s, row, SNAPSHOT_STRING_GENRE), query);
}

bool snapshot_matches(const snapshot_t *s, const snapshot_filter_t *f, const char *relpath,
                      uint32_t size, int64_t *sort_key, const char **sort_str)
{
    if (sort_key) *sort_key = 0;
    if (sort_str) *sort_str = NULL;
    if (!s || !f || !relpath) return false;

    snapshot_row_t row;
    bool have = snapshot_find(s, snapshot_path_hash(relpath), size, &row);
    const snapshot_row_t *r = have ? &row : NULL;

    if (f->query && f->query[0] && !name_matches(s, r, relpath, f->query)) return false;

    /* Every filter below needs a row: a field that was never measured cannot be in a
     * range. The caller's contract is that a filter on a missing field excludes. */
    if (f->bpm_min_x10 || f->bpm_max_x10) {
        if (!r || r->bpm_x10 == 0) return false;
        if (f->bpm_min_x10 && r->bpm_x10 < f->bpm_min_x10) return false;
        if (f->bpm_max_x10 && r->bpm_x10 > f->bpm_max_x10) return false;
    }
    if (f->key_mask) {
        if (!r || r->key == 0 || r->key > 24) return false;
        if (!((f->key_mask >> (r->key - 1)) & 1u)) return false;
    }
    if (f->mood_mask) {
        if (!r || !(r->mood_bits & f->mood_mask)) return false;
    }
    if (f->genre && f->genre[0]) {
        if (!r || !fold_contains(snapshot_row_string(s, r, SNAPSHOT_STRING_GENRE), f->genre))
            return false;
    }
    struct {
        uint8_t min, max;
        uint8_t value;
        bool    present;
    } ranges[4] = {
        {f->energy_min, f->energy_max, r ? r->energy : 0, r != NULL},
        {f->danceability_min, f->danceability_max, r ? r->danceability : 0, r != NULL},
        {f->valence_min, f->valence_max, r ? r->valence : 0, r != NULL},
        {f->arousal_min, f->arousal_max, r ? r->arousal : 0, r != NULL},
    };
    for (int i = 0; i < 4; i++) {
        bool lo = ranges[i].min != SNAPSHOT_UNKNOWN_PCT;
        bool hi = ranges[i].max != SNAPSHOT_UNKNOWN_PCT;
        if (!lo && !hi) continue;
        if (!ranges[i].present || ranges[i].value == SNAPSHOT_UNKNOWN_PCT) return false;
        if (lo && ranges[i].value < ranges[i].min) return false;
        if (hi && ranges[i].value > ranges[i].max) return false;
    }

    if (sort_key || sort_str) {
        switch (f->sort) {
        case SNAPSHOT_SORT_PATH:
            break;
        case SNAPSHOT_SORT_TITLE:
            if (sort_str && r) {
                const char *title = snapshot_row_string(s, r, SNAPSHOT_STRING_TITLE);
                *sort_str = title[0] ? title : NULL; /* no title: no value, sorts last */
            }
            break;
        case SNAPSHOT_SORT_BPM:
            if (sort_key) *sort_key = (r && r->bpm_x10) ? (int64_t)r->bpm_x10 : INT64_MIN;
            break;
        case SNAPSHOT_SORT_ENERGY:
            if (sort_key)
                *sort_key = (r && r->energy != SNAPSHOT_UNKNOWN_PCT) ? (int64_t)r->energy
                                                                    : INT64_MIN;
            break;
        case SNAPSHOT_SORT_DANCEABILITY:
            if (sort_key)
                *sort_key = (r && r->danceability != SNAPSHOT_UNKNOWN_PCT)
                                ? (int64_t)r->danceability
                                : INT64_MIN;
            break;
        default:
            break;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ apply */

/* One chunk at a time, so the image never has to be held whole. Static because the
 * update runs once, at boot, with nothing else running. */
#define APPLY_CHUNK 1024u
static uint8_t apply_chunk[APPLY_CHUNK];

snapshot_result_t snapshot_apply(const snapshot_source_t *src, const snapshot_target_t *tgt,
                                 char *why, size_t why_size)
{
    if (why && why_size) why[0] = 0;
    if (!src || !tgt || !src->size || !src->read || !tgt->erase || !tgt->write || !tgt->read)
        return fail(why, why_size, "the update has no source or target");

    uint32_t size = src->size(src->ctx);
    if (size < SNAPSHOT_HEADER_SIZE) return fail(why, why_size, "the update file is too short");

    uint8_t hdr[SNAPSHOT_HEADER_SIZE];
    if (!src->read(src->ctx, 0, hdr, sizeof hdr))
        return fail(why, why_size, "the update file cannot be read");

    uint32_t rows = 0, pool = 0, total = 0;
    if (!header_check(hdr, size, &rows, &pool, &total, why, why_size)) return SNAPSHOT_INVALID;
    if (total > tgt->capacity) return fail(why, why_size, "the update file exceeds the partition");
    uint32_t want = rd32(hdr + 36);

    /* The file is checksummed before the partition is touched: an update that would
     * be refused must not erase the image the device is still serving from. */
    uint32_t crc = snapshot_crc_begin();
    for (uint32_t off = 0; off < total;) {
        uint32_t n = total - off < APPLY_CHUNK ? total - off : APPLY_CHUNK;
        if (!src->read(src->ctx, off, apply_chunk, n))
            return fail(why, why_size, "the update file stopped early");
        crc = feed_payload(crc, off, apply_chunk, n);
        off += n;
    }
    if (snapshot_crc_end(crc) != want)
        return fail(why, why_size, "the update file's checksum does not match");

    if (!tgt->erase(tgt->ctx, 0, (total + 4095u) & ~4095u))
        return fail(why, why_size, "the partition erase failed");

    for (uint32_t off = 0; off < total;) {
        uint32_t n = total - off < APPLY_CHUNK ? total - off : APPLY_CHUNK;
        if (!src->read(src->ctx, off, apply_chunk, n))
            return fail(why, why_size, "the update file stopped early");
        if (!tgt->write(tgt->ctx, off, apply_chunk, n))
            return fail(why, why_size, "the partition write failed");
        off += n;
    }

    /* The write is only trusted once the flash agrees with it. */
    uint32_t back = snapshot_crc_begin();
    for (uint32_t off = 0; off < total;) {
        uint32_t n = total - off < APPLY_CHUNK ? total - off : APPLY_CHUNK;
        if (!tgt->read(tgt->ctx, off, apply_chunk, n))
            return fail(why, why_size, "the partition read-back failed");
        back = feed_payload(back, off, apply_chunk, n);
        off += n;
    }
    if (snapshot_crc_end(back) != want)
        return fail(why, why_size, "the partition did not keep the write");

    return SNAPSHOT_OK;
}

/* ------------------------------------------------------------------ hits */

typedef struct {
    uint32_t    index;
    int64_t     key;
    const char *str;
} hit_t;

struct snapshot_hits {
    hit_t   *v;
    uint32_t cap;
    uint32_t n;
};

snapshot_hits_t *snapshot_hits_new(uint32_t cap)
{
    if (!cap) cap = 1;
    snapshot_hits_t *h = (snapshot_hits_t *)calloc(1, sizeof *h);
    if (!h) return NULL;
    h->v = (hit_t *)calloc(cap, sizeof *h->v);
    if (!h->v) {
        free(h);
        return NULL;
    }
    h->cap = cap;
    return h;
}

void snapshot_hits_free(snapshot_hits_t *h)
{
    if (!h) return;
    free(h->v);
    free(h);
}

void snapshot_hits_clear(snapshot_hits_t *h)
{
    if (h) h->n = 0;
}

/* Total order, best first: key, then string (NUL last), then track index. */
static int cmp_hit(const hit_t *a, const hit_t *b)
{
    /* INT64_MIN means "no value": everything with a value sorts before it. */
    if (a->key == INT64_MIN || b->key == INT64_MIN) {
        if (a->key == b->key) {
            /* fall through to the index tie-break below */
        } else {
            return a->key == INT64_MIN ? 1 : -1;
        }
    } else if (a->key != b->key) {
        return a->key < b->key ? -1 : 1;
    }
    if (a->str && b->str) {
        const char *x = a->str, *y = b->str;
        while (*x && *y && lower(*x) == lower(*y)) {
            x++;
            y++;
        }
        int c = (int)(unsigned char)lower(*x) - (int)(unsigned char)lower(*y);
        if (c) return c < 0 ? -1 : 1;
    } else if (a->str) {
        return -1;
    } else if (b->str) {
        return 1;
    }
    if (a->index != b->index) return a->index < b->index ? -1 : 1;
    return 0;
}

bool snapshot_hits_add(snapshot_hits_t *h, uint32_t index, int64_t key, const char *str)
{
    if (!h) return false;
    hit_t add = {index, key, str};

    if (h->n == h->cap && cmp_hit(&add, &h->v[h->cap - 1]) >= 0) return false;

    uint32_t pos = h->n;
    while (pos > 0 && cmp_hit(&add, &h->v[pos - 1]) < 0) pos--;
    if (h->n < h->cap) h->n++;
    uint32_t last = h->n - 1;
    if (pos < last) memmove(&h->v[pos + 1], &h->v[pos], (last - pos) * sizeof *h->v);
    h->v[pos] = add;
    return true;
}

uint32_t snapshot_hits_count(const snapshot_hits_t *h) { return h ? h->n : 0; }

uint32_t snapshot_hits_index(const snapshot_hits_t *h, uint32_t i)
{
    if (!h || i >= h->n) return 0;
    return h->v[i].index;
}
