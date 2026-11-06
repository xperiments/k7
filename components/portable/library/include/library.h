#pragma once
/*
 * library - the immutable Library tree, built once from the Music directory.
 *
 * The scan walks the Music directory recursively, accepts only Tracks whose name
 * passes the shared source-format rule, sorts each directory by name, and builds
 * one arena: the tree the HTTP surface looks up, the broadcast order and the
 * binary view. Lookups walk the arena; they never touch the filesystem.
 *
 * This module knows nothing about HTTP, the broadcast or the display. It is told
 * whether a card is present (a no-card generation is built without touching the
 * filesystem) and produces one generation; the application decides when to build
 * one and who may read it.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h> /* FILENAME_MAX: the VFS path ceiling */

#include "audio_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LIB_NODE_NONE UINT32_MAX
#define LIB_NAME_MAX  128            /* CONFIG_FATFS_MAX_LFN */
#define LIB_PATH_MAX  (FILENAME_MAX + 1)
#define LIB_DEFAULT_MAX_BYTES  (4u * 1024u * 1024u)
#define LIB_DEFAULT_MAX_TRACKS 20000u

typedef struct library library_t;
typedef uint32_t lib_node_t;

typedef enum {
    LIB_KIND_NONE = -1,
    LIB_DIR = 0,
    LIB_TRACK = 1,
} lib_kind_t;

typedef enum {
    LIB_OK = 0,
    LIB_INVALID_ARG, /* no Music directory, or one that does not fit the path ceiling */
    LIB_NO_MEM,
} lib_result_t;

typedef enum {
    LIB_STATE_OK = 0,
    LIB_STATE_NO_CARD,
    LIB_STATE_NO_DIR,
    LIB_STATE_EMPTY,
    LIB_STATE_ERROR,
} lib_state_t;

typedef struct {
    const char *music_dir; /* the Music directory; library_open copies it */
    uint32_t    max_bytes; /* 0 = LIB_DEFAULT_MAX_BYTES */
    uint32_t    max_tracks;/* 0 = LIB_DEFAULT_MAX_TRACKS */
    uint32_t    max_path;  /* 0 = FILENAME_MAX; absolute path ceiling for the scan */
    uint32_t    generation;/* 0 = 1; the number embedded in the blob */
} library_cfg_t;

/* Scan the Music directory and build one immutable generation. A missing or
 * unreadable Music directory is still a generation: it has state
 * LIB_STATE_NO_DIR and an error sentence, so the HTTP surface can stay
 * diagnosable. An empty directory has state LIB_STATE_EMPTY. */
lib_result_t library_open(const library_cfg_t *cfg, library_t **out);

/* Build the no-card generation without touching the filesystem. `reason` is the
 * mount error, or NULL; it becomes the generation's error sentence. */
lib_result_t library_open_no_card(const library_cfg_t *cfg, const char *reason,
                                  library_t **out);

/* The generation number for the next build. The application calls this before
 * library_open(); a rescan must use a number larger than the published one. */
uint32_t     library_next_generation(void);

/* Publish a built generation. The store takes the caller's reference; the old
 * generation is freed when its last reader releases it. Returns false when
 * `next` is NULL. */
bool         library_publish(library_t *next);

/* A lease on the current generation, or NULL when none has been published. Every
 * acquire must be paired with release. */
const library_t *library_acquire(void);
void         library_release(const library_t *lib);
uint32_t     library_current_generation(void); /* 0 when none is published */
uint32_t     library_refs(const library_t *lib); /* test/diagnostic only */

/* Close an unpublished generation, or a leased one that has no readers. */
void         library_close(library_t *lib);

lib_state_t  library_state(const library_t *lib);
const char  *library_state_name(lib_state_t state);
const char  *library_error(const library_t *lib); /* "" when there is nothing to report */
bool         library_truncated(const library_t *lib);

uint32_t     library_generation(const library_t *lib);
uint32_t     library_node_count(const library_t *lib);
uint32_t     library_track_count(const library_t *lib);
uint32_t     library_dir_count(const library_t *lib); /* excludes the root */
uint64_t     library_total_bytes(const library_t *lib);
uint32_t     library_crc32(const library_t *lib); /* the blob header's CRC */

lib_node_t   library_root(const library_t *lib);
lib_node_t   library_parent(const library_t *lib, lib_node_t n);
uint32_t     library_child_count(const library_t *lib, lib_node_t n);
lib_node_t   library_child(const library_t *lib, lib_node_t n, uint32_t i);
const char  *library_name(const library_t *lib, lib_node_t n, uint16_t *len);
lib_kind_t   library_kind(const library_t *lib, lib_node_t n);
uint64_t     library_size(const library_t *lib, lib_node_t n);
mi_fmt_t     library_format(const library_t *lib, lib_node_t n);

/* Build "Artist/Album/track.mp3" into buf. Returns the length, (size_t)-1 when
 * it does not fit, and 0 for the root (whose relative path is empty). */
size_t       library_relpath(const library_t *lib, lib_node_t n, char *buf, size_t buf_size);

/* Build the absolute path (Music directory + relative path) for a node. This is
 * the only place the route and the broadcast turn a tree node into a filename. */
size_t       library_abspath(const library_t *lib, lib_node_t n, char *buf, size_t buf_size);

/* Split relpath on '/', walk the children: exact match first, then an ASCII
 * case-insensitive fallback. An empty path is the root. A path that is empty in
 * a segment, contains "." or "..", or matches more than one child in the
 * fallback returns LIB_NODE_NONE. No filesystem call. */
lib_node_t   library_find(const library_t *lib, const char *relpath);

/* The broadcast order is the depth-first track index. */
lib_node_t   library_track_at(const library_t *lib, uint32_t track_index);
uint32_t     library_track_index(const library_t *lib, lib_node_t n); /* LIB_NODE_NONE if not a track */

/* The prebuilt blob, valid until library_close(). */
const void  *library_blob(const library_t *lib, size_t *len);

/* Well-formed UTF-8. The scan uses this and skips a name that is not; exposed so
 * the rule is testable without a filesystem that can store such a name. */
bool         library_utf8_valid(const char *s, size_t n);

#ifdef __cplusplus
}
#endif
