#pragma once
/*
 * music_dir - the music directory's rules, in one place.
 *
 * Two HTTP routes serve the directory and they used to check its safety
 * differently: the download route re-verified that the joined path sat under the
 * directory, and the index route did not. This module owns that rule, the name
 * rules that go with it, and the directory walk, so the index can only list what
 * the download surface would serve.
 *
 * It takes the directory as an argument rather than reading a global: the card's
 * state belongs to the routes, and a test can point the rules at any directory.
 */
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MUSIC_NAME_MAX 128 /* CONFIG_FATFS_MAX_LFN */
#define MUSIC_PATH_MAX 256 /* directory + '/' + a 128-byte name */
#define MUSIC_WHY_MAX  160

typedef enum {
    MUSIC_OK = 0,
    MUSIC_UNSAFE,      /* a name that could escape the directory */
    MUSIC_UNSUPPORTED, /* not one of the three source formats */
    MUSIC_TOO_LONG,    /* longer than the card's LFN limit, or the joined path */
    MUSIC_NO_CARD,     /* no directory is mounted */
} music_result_t;

/* Resolve a request name to a path inside `dir`.
 *
 * On MUSIC_OK, `path` holds the joined path, which is guaranteed to sit directly
 * under `dir`. Otherwise `why` explains the refusal in a sentence the route can
 * log or show. `path` and `why` may be NULL when only the verdict is wanted. */
music_result_t music_resolve(const char *dir, const char *name, char *path, size_t path_size,
                             char *why, size_t why_size);

/* The index's predicate: the same rules as music_resolve, without the join. A
 * name is listable when it is safe, within the LFN limit and a source format. */
bool music_name_is_listable(const char *name);

/* Called for each accepted track. Return false to stop the walk. */
typedef bool (*music_entry_fn)(void *ctx, const char *name, unsigned long long size);

/* Walk `dir` and call `fn` for every regular file that music_name_is_listable()
 * accepts. Returns the number reported (which is where an early stop lands), or
 * -1 when the directory cannot be read - errno then says why. */
int music_dir_list(const char *dir, music_entry_fn fn, void *ctx);

#ifdef __cplusplus
}
#endif
