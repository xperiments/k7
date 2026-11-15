/*
 * T39: the boot default.
 *
 * The player boots shuffled (docs/adr/0091). A fresh broadcast therefore has the mode on
 * and its first pass walks the order the seed builds, not the library's own row order. The
 * seed is fixed here, so the permutation is exactly the one `mi_shuffle` builds and the
 * test can check the first two Tracks the broadcast plays.
 */
#include "test_util.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mi_shuffle.h"
#include "mini_icecast.h"

#define HZ 44100
#define KBPS 128
#define TRACK_SECONDS 8
#define COUNT 3
#define SEED 42u
#define PORT 18102

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
    snprintf(dir, sizeof dir, "/tmp/xtape-shuffle-default-%ld", (long)getpid());
    if (mkdir(dir, 0700) != 0) {
        printf("FATAL: cannot create %s\n", dir);
        return 1;
    }
    for (int i = 0; i < COUNT; i++) {
        char path[192];
        snprintf(path, sizeof path, "%s/%d.wav", dir, i + 1);
        if (!write_silence_wav(path, TRACK_SECONDS)) {
            printf("FATAL: cannot write %s\n", path);
            return 1;
        }
    }

    library_cfg_t lc = {.music_dir = dir};
    library_t *lib = NULL;
    CHECK_EQ(library_open(&lc, &lib), LIB_OK, "the Library opens");
    char names[COUNT][MINI_ICECAST_STATUS_FILE_MAX];
    CHECK_EQ(library_track_count(lib), (uint32_t)COUNT, "the Library has three Tracks");
    for (int i = 0; i < COUNT; i++) {
        lib_node_t n = library_track_at(lib, (uint32_t)i);
        uint16_t len = 0;
        const char *name = library_name(lib, n, &len);
        snprintf(names[i], sizeof names[i], "%.*s", (int)len, name);
    }

    /* The app seeds the order before the station starts; the test does the same, so the
     * permutation is known. */
    mini_icecast_shuffle_seed(SEED);

    mini_icecast_cfg_t cfg = {
        .library = lib,
        .station_name = "shuffle-default",
        .port = PORT,
        .out_hz = HZ,
        .bitrate = KBPS,
        .max_clients = 1,
    };
    CHECK(mini_icecast_start(&cfg) == ESP_OK, "the broadcast starts");
    CHECK(mini_icecast_shuffle_enabled(), "a fresh broadcast has shuffle on");

    mi_shuffle_t s = {0};
    mi_shuffle_seed(&s, SEED);
    uint32_t expected[COUNT];
    CHECK(mi_shuffle_build(&s, expected, COUNT, COUNT), "the expected order builds");
    CHECK(!(expected[0] == 0 && expected[1] == 1 && expected[2] == 2),
          "the default order is not the library's own row order");

    char seen[MINI_ICECAST_STATUS_FILE_MAX] = {0};
    CHECK(wait_for_file(names[expected[0]], seen, sizeof seen),
          "the first Track is the order's first (%s, saw %s)", names[expected[0]], seen);
    mini_icecast_status_t st;
    mini_icecast_status(&st);
    CHECK_EQ(st.track_index, expected[0], "the status names the order's first Track");

    mini_icecast_skip();
    CHECK(wait_for_file(names[expected[1]], seen, sizeof seen),
          "skip walks the order (%s, saw %s)", names[expected[1]], seen);
    return 0;
}
