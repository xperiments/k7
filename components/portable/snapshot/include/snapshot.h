#pragma once
/*
 * snapshot - the measured music a host produced for this Library.
 *
 * One immutable image, validated once and then read in place: a 64-byte header (80 in
 * version 2, which adds the Playlists), one 32-byte row per track sorted by
 * fingerprint, and one UTF-8 string pool. Nothing is
 * copied, nothing is cached, and the module never touches a filesystem -- it is handed
 * a pointer and a length. On the device the pointer is the mapped `snapshot` partition
 * (ADR 0047); in the host tests it is a buffer.
 *
 * The byte layout is docs/specs/snapshot-format.md. This header is the only interface
 * the rest of the firmware sees: the MCP layer asks whether a track matches a filter,
 * reads the row's fields, and collects the best hits.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The header's first eight bytes. The product was renamed (ADR 0062) and the app writes
 * `K7SNAP` now; `XTAPESNP` is what a card or a previous app build may still send. The
 * field is eight bytes, so the shorter name is NUL-padded and a compare covers all eight -
 * never the C string, or "K7SNAPxx" would read as a Snapshot. */
#define SNAPSHOT_MAGIC        "K7SNAP\0\0"
#define SNAPSHOT_MAGIC_LEGACY "XTAPESNP"
/* Version 1 has no list area and a 64-byte header; version 2 appends the lists and
 * grows the header to 80 bytes. The reader accepts both (format spec §12). */
#define SNAPSHOT_VERSION_1    1u
#define SNAPSHOT_VERSION_2    2u
#define SNAPSHOT_VERSION      SNAPSHOT_VERSION_2
#define SNAPSHOT_HEADER_SIZE  64u  /* version 1; version 2 is this plus 16 */
#define SNAPSHOT_HEADER_SIZE_2 80u
#define SNAPSHOT_ROW_SIZE     32u
#define SNAPSHOT_LIST_SIZE    32u
#define SNAPSHOT_LIST_ROW_SIZE 4u
#define SNAPSHOT_MAX_LISTS    64u
#define SNAPSHOT_MAX_LIST_ROWS 8192u
#define SNAPSHOT_MAX_BYTES    (2u * 1024u * 1024u)

/* Unknown percentages are 0xFF and unknown BPM/key are 0; the accessors keep them
 * as they are rather than inventing a zero, because "0 BPM" and "not measured" are
 * different answers and only the caller knows which one to show. */
#define SNAPSHOT_UNKNOWN_PCT  0xFFu

typedef enum {
    SNAPSHOT_OK = 0,
    SNAPSHOT_NO_PARTITION, /* no snapshot partition in this build */
    SNAPSHOT_NO_UPDATE,    /* the partition exists but holds no image */
    SNAPSHOT_INVALID,      /* an image that failed validation */
    SNAPSHOT_NO_MEM,
} snapshot_result_t;

/* A validated image. Opaque: the fields are read through the accessors below. */
typedef struct snapshot snapshot_t;

/* One Playlist, as read: its opaque id, where its name is in the pool, and its slice
 * of the list-row array. `snapshot_list_row` reads one row of that slice. */
typedef struct {
    uint64_t id;
    uint32_t name_offset;
    uint32_t row_start;
    uint32_t row_count;
    uint64_t created;
} snapshot_list_t;

/* One row, as read. `snapshot_find` fills it; every field is exactly what the image
 * holds, unknown markers included. */
typedef struct {
    uint64_t fingerprint;
    uint32_t size;
    uint32_t string_offset;
    uint16_t bpm_x10;
    uint8_t  key;
    uint8_t  energy;
    uint8_t  danceability;
    uint8_t  valence;
    uint8_t  arousal;
    uint8_t  mood_bits;
    uint8_t  confidence;
    uint8_t  source_flags;
} snapshot_row_t;

/* source_flags bits */
#define SNAPSHOT_SRC_BPM_TAGGED    0x01u
#define SNAPSHOT_SRC_KEY_TAGGED    0x02u
#define SNAPSHOT_SRC_MOOD_TAGGED   0x04u
#define SNAPSHOT_SRC_ENERGY_TAGGED 0x08u
#define SNAPSHOT_SRC_VA_PRESENT    0x10u
#define SNAPSHOT_SRC_BPM_ESCALATED 0x20u

/* mood_bits bits */
#define SNAPSHOT_MOOD_HAPPY        0x01u
#define SNAPSHOT_MOOD_SAD          0x02u
#define SNAPSHOT_MOOD_AGGRESSIVE   0x04u
#define SNAPSHOT_MOOD_RELAXED      0x08u
#define SNAPSHOT_MOOD_PARTY        0x10u

typedef enum {
    SNAPSHOT_STRING_TITLE = 0,
    SNAPSHOT_STRING_ARTIST,
    SNAPSHOT_STRING_ALBUM,
    SNAPSHOT_STRING_GENRE,
} snapshot_string_t;

/* ---------------------------------------------------------------- the image */

/* Whether these first eight bytes are a magic this firmware reads: the current
 * `SNAPSHOT_MAGIC`, or `SNAPSHOT_MAGIC_LEGACY`. A caller that pre-checks a header (the
 * board's mapped partition) uses this instead of comparing the constants itself, so the
 * read-both rule lives in one place. */
bool snapshot_magic_ok(const uint8_t *magic);

/* Validate an image already in memory and take a reference to it. The image must
 * outlive the snapshot. `why`, when non-NULL, receives one sentence naming the first
 * validation failure (or "" on success). */
snapshot_result_t snapshot_open(const uint8_t *image, size_t size, snapshot_t **out);
snapshot_result_t snapshot_open_why(const uint8_t *image, size_t size, snapshot_t **out,
                                    char *why, size_t why_size);
void              snapshot_close(snapshot_t *s);

/* 1 or 2; the image's own format version, which the accessors below depend on. */
uint16_t       snapshot_version(const snapshot_t *s);
uint32_t       snapshot_rows(const snapshot_t *s);
uint32_t       snapshot_generation(const snapshot_t *s);
uint32_t       snapshot_analyser(const snapshot_t *s);
uint32_t       snapshot_app_schema(const snapshot_t *s);
uint64_t       snapshot_model_set(const snapshot_t *s);

/* The row for a track, when both the fingerprint and the size agree. A size mismatch
 * is "not this track": a file replaced under the same name must not wear the old
 * file's measurements. */
bool snapshot_find(const snapshot_t *s, uint64_t fingerprint, uint32_t size,
                   snapshot_row_t *row);

/* A row's string, NUL-terminated inside the image; "" when absent. */
const char *snapshot_row_string(const snapshot_t *s, const snapshot_row_t *row,
                                snapshot_string_t which);

/* ---------------------------------------------------------------- the lists */

/* Playlists are version 2 only: a version 1 image reports zero and every accessor
 * below returns false or "". Lists are stored in ascending id order, so `find` is a
 * binary search. None of this resolves a row to a track -- that needs the Library,
 * which this module never touches. */
uint32_t snapshot_lists(const snapshot_t *s);
bool     snapshot_list_at(const snapshot_t *s, uint32_t index, snapshot_list_t *out);
bool     snapshot_list_find(const snapshot_t *s, uint64_t id, snapshot_list_t *out);
const char *snapshot_list_name(const snapshot_t *s, const snapshot_list_t *list);
uint32_t snapshot_list_rows_total(const snapshot_t *s);

/* One row of a list: the relative path it names, NUL-terminated inside the image. The
 * path is the whole binding -- a list row has no measurement to protect, and the
 * Library is what resolves it. */
bool snapshot_list_row(const snapshot_t *s, const snapshot_list_t *list, uint32_t index,
                       const char **path);

/* FNV-1a 64 of a relative path's bytes -- the hash half of a Track fingerprint. The
 * other half is the file's size, which the lookup takes separately because a renamed
 * file is a different track and a replaced file is not the same measurements. */
uint64_t snapshot_path_hash(const char *relative_path);

/* CRC-32/IEEE, the same one the image's header carries. The update path uses it to
 * verify the partition it just wrote; the reader uses it to refuse a torn image.
 * The incremental form exists so an image can be checksummed as it streams through a
 * 1 KB buffer instead of being held whole. */
uint32_t snapshot_crc32(const uint8_t *data, size_t size);
uint32_t snapshot_crc_begin(void);
uint32_t snapshot_crc_feed(uint32_t state, const uint8_t *data, size_t size);
uint32_t snapshot_crc_end(uint32_t state);

/* ---------------------------------------------------------------- applying */

/* Where an update comes from and where it goes. Both are injected: the board wires
 * a FILE and the `snapshot` partition, and the host tests wire two buffers, so the
 * order that matters -- validate, erase, write, read back, and only then let the
 * caller delete the file -- is testable without a board. */
typedef struct {
    void *ctx;
    uint32_t (*size)(void *ctx);
    bool (*read)(void *ctx, uint32_t offset, uint8_t *data, uint32_t size);
} snapshot_source_t;

typedef struct {
    void *ctx;
    uint32_t capacity;
    bool (*erase)(void *ctx, uint32_t offset, uint32_t size);
    bool (*write)(void *ctx, uint32_t offset, const uint8_t *data, uint32_t size);
    bool (*read)(void *ctx, uint32_t offset, uint8_t *data, uint32_t size);
} snapshot_target_t;

/* Copy the source into the target, checksumming as it goes and verifying the read
 * back. SNAPSHOT_OK means the target now holds a valid image and the caller may
 * delete the source. Anything else leaves the target as it was, as far as the
 * target's own erase/write semantics allow, and fills `why`. */
snapshot_result_t snapshot_apply(const snapshot_source_t *src, const snapshot_target_t *tgt,
                                 char *why, size_t why_size);

/* ---------------------------------------------------------------- matching */

typedef enum {
    SNAPSHOT_SORT_PATH = 0, /* broadcast order */
    SNAPSHOT_SORT_TITLE,
    SNAPSHOT_SORT_BPM,
    SNAPSHOT_SORT_ENERGY,
    SNAPSHOT_SORT_DANCEABILITY,
} snapshot_sort_t;

/* Every filter is optional. Percentages use 0xFF for "unset" -- the same value the
 * image uses for "unknown", which is why a caller must start from
 * snapshot_filter_defaults() rather than zeroing the struct: 0 is a legal bound and
 * `energy_max: 0` has to mean "at most 0", not "no filter". A BPM bound is unset at
 * 0 (no track has 0 BPM) and a string filter at NULL. A set filter on a field the
 * track does not have excludes the track. */
typedef struct {
    const char *query;   /* case-insensitive substring of path, title, artist, album or genre */
    const char *genre;   /* case-insensitive substring of the genre string */
    uint16_t    bpm_min_x10;
    uint16_t    bpm_max_x10;
    uint8_t     energy_min, energy_max;
    uint8_t     danceability_min, danceability_max;
    uint8_t     valence_min, valence_max;
    uint8_t     arousal_min, arousal_max;
    uint16_t    key_mask;  /* 1 << (camelot code - 1), 0 for no key filter */
    uint8_t     mood_mask; /* any listed bit matches, 0 for no mood filter */
    snapshot_sort_t sort;
} snapshot_filter_t;

/* The filter with every optional field unset. */
void snapshot_filter_defaults(snapshot_filter_t *f);

/* The five mood names, in bit order: happy, sad, aggressive, relaxed, party.
 * `snapshot_mood_name` returns NULL for a bit outside 0..4; `snapshot_mood_from_name`
 * returns the bit, or -1 when the name is not one of them. */
const char *snapshot_mood_name(unsigned bit);
int         snapshot_mood_from_name(const char *name);

/* Parse a key as Camelot (`8A`) or as a note name (`Am`, `A minor`, `F#m`, `Bb`,
 * `A major`). Returns 1..24, or 0 when the string is not a key this module knows. */
uint8_t snapshot_key_parse(const char *text);

/* Parse a sort name (`path`, `title`, `bpm`, `energy`, `danceability`). Returns false
 * for anything else, leaving `out` untouched. */
bool snapshot_sort_parse(const char *name, snapshot_sort_t *out);

/* Does this track match? `relpath` is the Library's relative path and `size` its file
 * size; `sort_key`/`sort_str` receive the value the hit collector orders by (unused
 * by the sort in question, and INT64_MIN when the track has no value -- which sorts
 * it last). */
bool snapshot_matches(const snapshot_t *s, const snapshot_filter_t *f, const char *relpath,
                      uint32_t size, int64_t *sort_key, const char **sort_str);

/* ---------------------------------------------------------------- hits */

/* A bounded, ordered set of hits: the best `cap` in sort order, then the caller skips
 * and truncates. Deterministic: equal keys break by track index, so broadcast order is
 * the tie-break everywhere. */
typedef struct snapshot_hits snapshot_hits_t;

snapshot_hits_t *snapshot_hits_new(uint32_t cap);
void             snapshot_hits_free(snapshot_hits_t *h);
void             snapshot_hits_clear(snapshot_hits_t *h);

/* Offer a candidate. `index` is the caller's track index, `key`/`str` the sort value.
 * Returns true when the candidate was taken (it is in the set, or it pushed another
 * one out). */
bool snapshot_hits_add(snapshot_hits_t *h, uint32_t index, int64_t key, const char *str);

uint32_t snapshot_hits_count(const snapshot_hits_t *h);
uint32_t snapshot_hits_index(const snapshot_hits_t *h, uint32_t i);

#ifdef __cplusplus
}
#endif
