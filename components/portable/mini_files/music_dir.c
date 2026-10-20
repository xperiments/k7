/*
 * music_dir - name safety, source-format acceptance, joining, containment and the
 * directory walk. mini_files' two routes are adapters over this.
 */
#include "music_dir.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "audio_format.h"

static bool name_is_safe(const char *name)
{
    if (!name || !name[0]) return false;
    if (name[0] == '.') return false;              /* also excludes macOS "._foo.mp3" */
    if (strstr(name, "..")) return false;
    if (strchr(name, '/') || strchr(name, '\\')) return false;
    return true;
}

static bool name_is_audio(const char *name) { return mi_dec_format_of(name) != MI_FMT_NONE; }

bool music_name_is_listable(const char *name)
{
    return name_is_safe(name) && strlen(name) <= MUSIC_NAME_MAX && name_is_audio(name);
}

static music_result_t refuse(music_result_t r, char *why, size_t why_size, const char *text)
{
    if (why && why_size) snprintf(why, why_size, "%s", text);
    return r;
}

music_result_t music_resolve(const char *dir, const char *name, char *path, size_t path_size,
                             char *why, size_t why_size)
{
    if (!dir || !dir[0])
        return refuse(MUSIC_NO_CARD, why, why_size, "no card is mounted");
    if (!name_is_safe(name))
        return refuse(MUSIC_UNSAFE, why, why_size, "that name could leave the music directory");
    if (strlen(name) > MUSIC_NAME_MAX)
        return refuse(MUSIC_TOO_LONG, why, why_size, "that name is longer than the card allows");
    if (!name_is_audio(name))
        return refuse(MUSIC_UNSUPPORTED, why, why_size, "only .mp3, .flac and .wav are served");

    char joined[MUSIC_PATH_MAX];
    int n = snprintf(joined, sizeof joined, "%s/%s", dir, name);
    if (n <= 0 || (size_t)n >= sizeof joined)
        return refuse(MUSIC_TOO_LONG, why, why_size, "that name does not fit the music directory path");

    /* The name checks above already forbid the separators, so this cannot fail for
     * a name that reached here. It stays because it is the rule the route relies
     * on: whatever a future name rule lets through, the resolved path is still
     * inside the directory. */
    size_t dlen = strlen(dir);
    if (strncmp(joined, dir, dlen) != 0 || joined[dlen] != '/')
        return refuse(MUSIC_UNSAFE, why, why_size, "that name could leave the music directory");

    if (path && path_size) snprintf(path, path_size, "%s", joined);
    if (why && why_size) why[0] = 0;
    return MUSIC_OK;
}

int music_dir_list(const char *dir, music_entry_fn fn, void *ctx)
{
    if (!dir || !dir[0]) {
        errno = ENOENT;
        return -1;
    }
    DIR *d = opendir(dir);
    if (!d) return -1;

    int count = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        const char *name = e->d_name;
        if (!music_name_is_listable(name)) continue;

        char path[MUSIC_PATH_MAX];
        int n = snprintf(path, sizeof path, "%s/%s", dir, name);
        if (n <= 0 || (size_t)n >= sizeof path) continue;
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;

        count++;
        if (fn && !fn(ctx, name, (unsigned long long)st.st_size)) break;
    }
    closedir(d);
    return count;
}
