/*
 * card_path - see include/card_path.h.
 */
#include "card_path.h"

#include <stdio.h>
#include <string.h>

static const char *s_music_dir = "";

void card_path_init(const char *music_dir)
{
    s_music_dir = music_dir ? music_dir : "";
}

void card_path_root(char *out, size_t cap)
{
    if (!out || !cap) return;
    const char *music = s_music_dir;
    size_t len = strlen(music);
    while (len > 1 && music[len - 1] == '/') len--;
    const char *slash = NULL;
    for (size_t i = len; i > 0; i--) {
        if (music[i - 1] == '/') {
            slash = music + i - 1;
            break;
        }
    }
    if (!slash) {
        out[0] = 0;
        return;
    }
    size_t parent = (size_t)(slash - music);
    if (parent == 0) parent = 1; /* "/music" -> "/" */
    snprintf(out, cap, "%.*s", (int)parent, music);
}

void card_path_file(char *out, size_t cap, const char *name)
{
    if (!out || !cap) return;
    char root[224];
    card_path_root(root, sizeof root);
    if (root[0] == 0) snprintf(out, cap, "%s", name);
    else if (strcmp(root, "/") == 0) snprintf(out, cap, "/%s", name);
    else snprintf(out, cap, "%s/%s", root, name);
}
