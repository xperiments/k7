#pragma once
/*
 * card_path - where a card's own files are.
 *
 * The device is started with the Music directory (`/sdcard/music`); the card root is its
 * parent, and the files the App writes - `k7.db`, `k7.card`, `k7.covers`, and the
 * `k7.playlists/` directory - sit there. The device writes only the selection record and,
 * when the app asks it to, one playlist file at a time. The
 * derivation was written twice, once for the covers archive and once for the card mark.
 */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Once, with the Music directory the services were started with. */
void card_path_init(const char *music_dir);

/* The card's root: the parent of the Music directory, or "" when there is not one. */
void card_path_root(char *out, size_t cap);

/* A file at the card's root. Falls back to a bare name when there is no root. */
void card_path_file(char *out, size_t cap, const char *name);

#ifdef __cplusplus
}
#endif
