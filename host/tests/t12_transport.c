/*
 * T12: the transport's two directions.
 *
 * The display now sends both, and a `prev` that silently restarts the current track
 * would look the same as a working one on a progress bar. This drives the real
 * broadcast through next, previous and forward-again, and reads the file it reports.
 *
 * The two tracks are eight seconds of silence written here rather than taken from the
 * fixture corpus: the corpus' tracks are under a second, so a request would race the
 * natural end of the track it is aimed at.
 */
#include "test_util.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mini_icecast.h"

#define HZ 44100
#define KBPS 128
#define TRACK_SECONDS 8

/* A canonical 44-byte PCM WAV header followed by silence. */
static bool write_silence_wav(const char *path, int seconds)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;

    const uint32_t data = (uint32_t)seconds * HZ * 2; /* mono, 16-bit */
    const uint32_t riff = 36 + data;
    const uint32_t fmt_size = 16;
    const uint32_t sample_rate = HZ;
    const uint32_t byte_rate = HZ * 2;
    const uint16_t pcm = 1, mono = 1, bits = 16, block = 2;

    bool ok = fwrite("RIFF", 1, 4, f) == 4 && fwrite(&riff, 4, 1, f) == 1 &&
              fwrite("WAVEfmt ", 1, 8, f) == 8 && fwrite(&fmt_size, 4, 1, f) == 1 &&
              fwrite(&pcm, 2, 1, f) == 1 && fwrite(&mono, 2, 1, f) == 1 &&
              fwrite(&sample_rate, 4, 1, f) == 1 && fwrite(&byte_rate, 4, 1, f) == 1 &&
              fwrite(&block, 2, 1, f) == 1 && fwrite(&bits, 2, 1, f) == 1 &&
              fwrite("data", 1, 4, f) == 4 && fwrite(&data, 4, 1, f) == 1;

    static const uint8_t zeros[4096];
    uint32_t left = data;
    while (ok && left) {
        size_t n = left < sizeof zeros ? left : sizeof zeros;
        ok = fwrite(zeros, 1, n, f) == n;
        left -= (uint32_t)n;
    }
    fclose(f);
    return ok;
}

/* The order the broadcast will consume. */
static bool read_order(const library_t *lib, char names[][MINI_ICECAST_STATUS_FILE_MAX], int want)
{
    if (library_track_count(lib) != (uint32_t)want) return false;
    for (int i = 0; i < want; i++) {
        lib_node_t n = library_track_at(lib, (uint32_t)i);
        uint16_t len = 0;
        const char *name = library_name(lib, n, &len);
        snprintf(names[i], MINI_ICECAST_STATUS_FILE_MAX, "%.*s", (int)len, name);
    }
    return true;
}

static bool wait_for_file(const char *want, char *last, size_t last_size)
{
    mini_icecast_status_t st = {0};
    for (int i = 0; i < 60; i++) { /* up to six seconds */
        usleep(100 * 1000);
        mini_icecast_status(&st);
        if (strcmp(st.file, want) == 0) return true;
    }
    snprintf(last, last_size, "%s", st.file);
    return false;
}

int main(void)
{
    char dir[128];
    snprintf(dir, sizeof dir, "/tmp/xtape-transport-%ld", (long)getpid());
    if (mkdir(dir, 0700) != 0) {
        printf("FATAL: cannot create %s\n", dir);
        return 1;
    }
    char album[192], deep[192], one[192], two[192], three[192];
    snprintf(album, sizeof album, "%s/Album", dir);
    snprintf(deep, sizeof deep, "%s/Album/Deep", dir);
    snprintf(one, sizeof one, "%s/Album/one.wav", dir);
    snprintf(two, sizeof two, "%s/two.wav", dir);
    snprintf(three, sizeof three, "%s/Album/Deep/three.wav", dir);
    if (mkdir(album, 0700) != 0 || mkdir(deep, 0700) != 0) {
        printf("FATAL: cannot create the nested fixture\n");
        return 1;
    }
    bool wrote = write_silence_wav(one, TRACK_SECONDS) &&
                 write_silence_wav(two, TRACK_SECONDS) &&
                 write_silence_wav(three, TRACK_SECONDS);
    CHECK(wrote, "three long WAV tracks were written under %s", dir);

    library_cfg_t lc = {.music_dir = dir, .build_json = false};
    library_t *lib = NULL;
    CHECK_EQ(library_open(&lc, &lib), LIB_OK, "the transport Library opens");
    char order[3][MINI_ICECAST_STATUS_FILE_MAX];
    char paths[3][LIB_PATH_MAX];
    bool listed = read_order(lib, order, 3);
    CHECK(listed, "the broadcast order has all three tracks");
    for (int i = 0; i < 3; i++) {
        lib_node_t n = library_track_at(lib, (uint32_t)i);
        CHECK(library_relpath(lib, n, paths[i], sizeof paths[i]) != (size_t)-1,
              "path %d builds", i);
    }
    mini_icecast_cfg_t cfg = {
        .library = lib,
        .station_name = "transport",
        .port = 18101,
        .out_hz = HZ,
        .bitrate = KBPS,
        .max_clients = 1,
    };
    CHECK(mini_icecast_start(&cfg) == ESP_OK, "the broadcast starts");

    char last[MINI_ICECAST_STATUS_FILE_MAX] = {0};
    if (listed) {
        CHECK(wait_for_file(order[0], last, sizeof last),
              "the first track is %s (saw %s)", order[0], last);
        mini_icecast_status_t st;
        mini_icecast_status(&st);
        CHECK(strcmp(st.path, paths[0]) == 0, "the status path is %s (got %s)", paths[0], st.path);
        CHECK(st.path[0] != 0 && strchr(st.path, '/') != NULL,
              "the first track is several levels deep: %s", st.path);
        CHECK_EQ(st.generation, 1, "the status names generation 1");
        CHECK_EQ(st.track_index, 0, "the first track is track index 0");
        CHECK_EQ(st.library_count, 3, "the library count is three");

        mini_icecast_skip();
        CHECK(wait_for_file(order[1], last, sizeof last),
              "skip moves to %s (saw %s)", order[1], last);

        mini_icecast_skip();
        CHECK(wait_for_file(order[2], last, sizeof last),
              "skip moves to %s (saw %s)", order[2], last);

        mini_icecast_prev();
        CHECK(wait_for_file(order[1], last, sizeof last),
              "prev returns to %s (saw %s)", order[1], last);

        mini_icecast_prev();
        CHECK(wait_for_file(order[0], last, sizeof last),
              "prev returns to %s (saw %s)", order[0], last);

        mini_icecast_prev();
        CHECK(wait_for_file(order[0], last, sizeof last),
              "prev at the first track replays it (saw %s)", last);

        mini_icecast_skip();
        CHECK(wait_for_file(order[1], last, sizeof last),
              "skip after prev moves forward through the history to %s (saw %s)", order[1], last);

        mini_icecast_skip();
        CHECK(wait_for_file(order[2], last, sizeof last),
              "skip moves to the last track %s (saw %s)", order[2], last);
        mini_icecast_skip();
        CHECK(wait_for_file(order[0], last, sizeof last),
              "skip at the last track wraps to %s (saw %s)", order[0], last);
        mini_icecast_prev();
        CHECK(wait_for_file(order[0], last, sizeof last),
              "prev at the first track of a new lap replays it (saw %s)", last);
    }

    /* A new generation resets the history and starts at its first Track. */
    char dir2[128], fresh[192];
    snprintf(dir2, sizeof dir2, "/tmp/xtape-transport2-%ld", (long)getpid());
    snprintf(fresh, sizeof fresh, "%s/new.wav", dir2);
    if (mkdir(dir2, 0700) != 0 || !write_silence_wav(fresh, TRACK_SECONDS)) {
        printf("FATAL: cannot create the second generation\n");
        return 1;
    }
    library_cfg_t lc2 = {.music_dir = dir2, .build_json = false};
    library_t *lib2 = NULL;
    CHECK_EQ(library_open(&lc2, &lib2), LIB_OK, "the second generation opens");
    mini_icecast_set_library(lib2);
    mini_icecast_skip(); /* leave the current generation's track */
    CHECK(wait_for_file("new.wav", last, sizeof last),
          "a generation change starts at its first Track (saw %s)", last);
    mini_icecast_status_t next;
    mini_icecast_status(&next);
    CHECK(strcmp(next.path, "new.wav") == 0, "the new generation's path is new.wav (got %s)", next.path);
    CHECK_EQ(next.track_index, 0, "the new generation starts at index 0");
    CHECK_EQ(next.library_count, 1, "the new generation has one Track");
    mini_icecast_prev();
    CHECK(wait_for_file("new.wav", last, sizeof last),
          "prev at the only Track replays it (saw %s)", last);

    unlink(fresh);
    rmdir(dir2);
    unlink(one);
    unlink(two);
    unlink(three);
    rmdir(deep);
    rmdir(album);
    rmdir(dir);
    TEST_END();
}
