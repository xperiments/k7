/*
 * T15: the music directory's rules, without an HTTP request.
 *
 * The rule that matters is one sentence - a name never escapes the music
 * directory - and it used to be written at two strengths, in two routes, with the
 * weaker copy on the index. These cases are the ones the routes used to answer
 * differently, plus the walk, which now decides what the index can list.
 */
#include "test_util.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "music_dir.h"

static void make_file(const char *dir, const char *name, const char *bytes)
{
    char path[MUSIC_PATH_MAX];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    int f = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (f < 0) {
        printf("FATAL: cannot create %s\n", path);
        exit(1);
    }
    if (write(f, bytes, strlen(bytes)) < 0) { /* the size is asserted below */ }
    close(f);
}

typedef struct {
    char names[8][MUSIC_NAME_MAX + 1];
    int n;
} list_ctx_t;

static bool collect(void *ctx, const char *name, unsigned long long size)
{
    list_ctx_t *c = ctx;
    if (c->n < 8) snprintf(c->names[c->n], MUSIC_NAME_MAX + 1, "%s", name);
    c->n++;
    CHECK(size > 0, "list reports a size for %s", name);
    return true;
}

static bool stop_after_one(void *ctx, const char *name, unsigned long long size)
{
    (void)name; (void)size;
    list_ctx_t *c = ctx;
    c->n++;
    return false;
}

int main(void)
{
    char tmpl[] = "/tmp/xtmusicXXXXXX";
    char *dir = mkdtemp(tmpl);
    if (!dir) {
        printf("FATAL: mkdtemp failed\n");
        return 1;
    }

    make_file(dir, "tone.mp3", "abc");
    make_file(dir, "weird name.flac", "abc");
    make_file(dir, "Café.wav", "abc");
    make_file(dir, "notes.txt", "abc");
    make_file(dir, ".hidden.mp3", "abc");
    make_file(dir, "..escape.mp3", "abc");
    char long_name[140];
    memset(long_name, 'a', 126);
    memcpy(long_name + 126, ".mp3", 5); /* 130 characters: over the card's LFN limit */
    make_file(dir, long_name, "abc");
    char sub[MUSIC_PATH_MAX];
    snprintf(sub, sizeof sub, "%s/sub", dir);
    if (mkdir(sub, 0755) != 0) {
        printf("FATAL: mkdir %s\n", sub);
        return 1;
    }
    make_file(sub, "nested.mp3", "abc");

    char path[MUSIC_PATH_MAX], why[MUSIC_WHY_MAX];

    CHECK_EQ(music_resolve(dir, "tone.mp3", path, sizeof path, why, sizeof why), MUSIC_OK,
             "a track resolves");
    char want[MUSIC_PATH_MAX];
    snprintf(want, sizeof want, "%s/tone.mp3", dir);
    CHECK(strcmp(path, want) == 0, "to the path the download route will open: %s", path);

    CHECK_EQ(music_resolve(dir, "weird name.flac", path, sizeof path, why, sizeof why), MUSIC_OK,
             "a name with a space resolves");
    CHECK_EQ(music_resolve(dir, "Café.wav", path, sizeof path, why, sizeof why), MUSIC_OK,
             "a UTF-8 name resolves");
    CHECK_EQ(music_resolve(dir, "TONE.MP3", path, sizeof path, why, sizeof why), MUSIC_OK,
             "the extension is matched case-insensitively");

    CHECK_EQ(music_resolve(dir, "notes.txt", path, sizeof path, why, sizeof why), MUSIC_UNSUPPORTED,
             "a non-audio name is unsupported");
    CHECK(strstr(why, "mp3") != NULL, "the refusal names the accepted formats: '%s'", why);

    CHECK_EQ(music_resolve(dir, "../tone.mp3", path, sizeof path, why, sizeof why), MUSIC_UNSAFE,
             "a parent reference is refused");
    CHECK_EQ(music_resolve(dir, "sub/nested.mp3", path, sizeof path, why, sizeof why), MUSIC_UNSAFE,
             "a forward slash is refused");
    CHECK_EQ(music_resolve(dir, "sub\\nested.mp3", path, sizeof path, why, sizeof why), MUSIC_UNSAFE,
             "a backslash is refused");
    CHECK_EQ(music_resolve(dir, ".hidden.mp3", path, sizeof path, why, sizeof why), MUSIC_UNSAFE,
             "a leading dot is refused, which also excludes macOS sidecars");
    CHECK_EQ(music_resolve(dir, "..escape.mp3", path, sizeof path, why, sizeof why), MUSIC_UNSAFE,
             "a name that only looks like a dot-file is refused");
    CHECK_EQ(music_resolve(dir, "", path, sizeof path, why, sizeof why), MUSIC_UNSAFE,
             "an empty name is refused");
    CHECK_EQ(music_resolve(NULL, "tone.mp3", path, sizeof path, why, sizeof why), MUSIC_NO_CARD,
             "no directory means no card");
    CHECK_EQ(music_resolve("", "tone.mp3", path, sizeof path, why, sizeof why), MUSIC_NO_CARD,
             "an empty directory means no card");
    CHECK_EQ(music_resolve(dir, long_name, path, sizeof path, why, sizeof why), MUSIC_TOO_LONG,
             "a name over the LFN limit is refused");

    /* A directory with a trailing slash still resolves, and still contains the path. */
    char dslash[MUSIC_PATH_MAX];
    snprintf(dslash, sizeof dslash, "%s/", dir);
    CHECK_EQ(music_resolve(dslash, "tone.mp3", path, sizeof path, why, sizeof why), MUSIC_OK,
             "a trailing slash on the directory is accepted");
    CHECK(strncmp(path, dslash, strlen(dslash)) == 0, "and the path is still inside it");

    /* The index's predicate agrees with the resolver's accept/reject decision. */
    CHECK(music_name_is_listable("tone.mp3"), "a track is listable");
    CHECK(music_name_is_listable("Café.wav"), "a UTF-8 track is listable");
    CHECK(!music_name_is_listable("notes.txt"), "a text file is not listable");
    CHECK(!music_name_is_listable(".hidden.mp3"), "a dot-file is not listable");
    CHECK(!music_name_is_listable("sub/nested.mp3"), "a nested path is not listable");
    CHECK(!music_name_is_listable(long_name), "an over-long name is not listable");

    /* The walk: only the accepted regular files, in the directory itself. */
    list_ctx_t ctx = {0};
    int n = music_dir_list(dir, collect, &ctx);
    CHECK_EQ(n, 3, "three tracks are listed");
    CHECK_EQ(ctx.n, 3, "the callback saw three tracks");
    int found_tone = 0, found_space = 0, found_utf8 = 0;
    for (int i = 0; i < ctx.n && i < 8; i++) {
        if (strcmp(ctx.names[i], "tone.mp3") == 0) found_tone = 1;
        if (strcmp(ctx.names[i], "weird name.flac") == 0) found_space = 1;
        if (strcmp(ctx.names[i], "Café.wav") == 0) found_utf8 = 1;
    }
    CHECK(found_tone && found_space && found_utf8, "and they are the expected names");

    list_ctx_t one = {0};
    CHECK_EQ(music_dir_list(dir, stop_after_one, &one), 1, "a callback can stop the walk after one");
    CHECK_EQ(one.n, 1, "and it stopped there");

    CHECK_EQ(music_dir_list("/tmp/xtmusic-does-not-exist-7f3a", collect, &ctx), -1,
             "an unreadable directory is -1");
    CHECK_EQ(music_dir_list(NULL, collect, &ctx), -1, "no directory is -1");

    /* Clean up. */
    char p[MUSIC_PATH_MAX];
    const char *names[] = {"tone.mp3", "weird name.flac", "Café.wav", "notes.txt", ".hidden.mp3",
                           "..escape.mp3", long_name};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        snprintf(p, sizeof p, "%s/%s", dir, names[i]);
        unlink(p);
    }
    snprintf(p, sizeof p, "%s/nested.mp3", sub);
    unlink(p);
    rmdir(sub);
    rmdir(dir);

    TEST_END();
}
