#pragma once
/*
 * playlist_file - a Playlist as one file on the card.
 *
 * `k7.playlists/<id>.k7list` at the card root, one file per list, the text format in
 * docs/specs/playlist-format.md. The app writes these files when the card is in a computer;
 * the device writes them when the card is in the player (ADR 0094), which is what lets a list
 * change without an image and a reboot. This module is the only code that knows the layout:
 * the HTTP layer asks for the index, one list's rows, a write and a remove.
 *
 * It is stdio and dirent only -- no ESP-IDF, no VFS type -- so the host tests exercise exactly
 * the bytes the firmware writes, against a temporary directory. A card root of "" means "no
 * card": every call answers NO_CARD rather than touching the filesystem.
 *
 * The fingerprint is FNV-1a 64 over the list's *content*, not its bytes: the name, the
 * creation time and every row, each followed by a newline, in that order. A file re-saved with
 * CRLF line endings by a text editor therefore has the same fingerprint as the writer's, and
 * the page compares what it wrote with what the card holds.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLAYLIST_DIR_NAME  "k7.playlists"
#define PLAYLIST_EXT       ".k7list"
#define PLAYLIST_MAX_LISTS 64u
#define PLAYLIST_MAX_ROWS  8192u
#define PLAYLIST_NAME_MAX  63u
#define PLAYLIST_PATH_MAX  1024u

typedef enum {
    PLAYLIST_FILE_OK = 0,
    PLAYLIST_FILE_NO_CARD,   /* there is no directory to read, or the card refused it */
    PLAYLIST_FILE_NOT_FOUND, /* no such list */
    PLAYLIST_FILE_BAD,       /* a file that does not hold a valid list */
    PLAYLIST_FILE_TOO_MANY,  /* the directory already holds the maximum */
    PLAYLIST_FILE_FULL,      /* the card would not take the bytes */
} playlist_file_result_t;

/* One list as the index states it: the file's identity and size, never its rows. */
typedef struct {
    uint64_t id;
    char name[PLAYLIST_NAME_MAX + 1];
    uint64_t created;
    uint32_t rows;
    uint64_t fingerprint;
} playlist_file_t;

/* One row, in play order. Returning false stops the read; the count then reports what was
 * handed over, and the caller decides what that means. */
typedef bool (*playlist_row_fn)(void *ctx, const char *path);

/* `<root>/k7.playlists`. A root of "" leaves the output empty. */
void playlist_dir(const char *root, char *out, size_t cap);

/* `<root>/k7.playlists/<id>.k7list`; false when the output would not fit or id is 0. */
bool playlist_path(const char *root, uint64_t id, char *out, size_t cap);

/*
 * The lists the card holds, sorted by id, at most `max` of them. A file that fails the format
 * is skipped rather than failing the directory: one bad file must not hide the rest, and the
 * index is where a person would see it missing.
 *
 * Returns the number written, or -1 when there is no card (or the directory cannot be read).
 */
int playlist_list(const char *root, playlist_file_t *out, int max);

/* One list's index entry, without its rows. */
playlist_file_result_t playlist_read_meta(const char *root, uint64_t id, playlist_file_t *out);

/*
 * One list's rows, in order, through `row`. `meta` is filled when it is not NULL, and the
 * whole file is checked -- a bad line, a count that disagrees with the lines, or an id that
 * disagrees with the file name answers BAD. Rows are handed over as they are read, so a
 * caller must discard what it collected unless the answer is OK.
 */
playlist_file_result_t playlist_read(const char *root, uint64_t id, playlist_file_t *meta,
                                    playlist_row_fn row, void *ctx);

/*
 * Write one list, atomically: a temporary file in the same directory, renamed onto the final
 * name. A card that fills mid-write keeps the list it had. The directory is created when it is
 * absent -- the one directory the device may create (ADR 0094).
 */
playlist_file_result_t playlist_write(const char *root, uint64_t id, const char *name,
                                     uint64_t created, const char *const *rows, uint32_t count);

/*
 * Store a list from the bytes of its file -- the body of a PUT. The bytes are written to the
 * temporary name, validated by reading that file back with the same parser, and only then
 * renamed: a body that is not a valid list leaves the card exactly as it was. `meta` is
 * filled with what the stored file says, for the answer.
 */
playlist_file_result_t playlist_store_body(const char *root, uint64_t id, const char *body,
                                          size_t len, playlist_file_t *meta);

/* Remove one list's file, and any temporary one left beside it. */
playlist_file_result_t playlist_remove(const char *root, uint64_t id);

/* The content fingerprint the index reports: FNV-1a 64 over name, created and the rows. */
uint64_t playlist_fingerprint(const char *name, uint64_t created, const char *const *rows,
                              uint32_t count);

#ifdef __cplusplus
}
#endif
