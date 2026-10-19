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

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mini_icecast.h"
#include "mi_decode.h"

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

/* The directory order the broadcast will see, restricted to the names it accepts. */
static bool read_order(const char *dir, char names[][MINI_ICECAST_STATUS_FILE_MAX], int want)
{
    int count = 0;
    DIR *d = opendir(dir);
    if (!d) return false;
    struct dirent *e;
    while ((e = readdir(d)) && count < want) {
        if (e->d_name[0] == '.' || mi_dec_format_of(e->d_name) == MI_FMT_NONE) continue;
        snprintf(names[count], MINI_ICECAST_STATUS_FILE_MAX, "%s", e->d_name);
        count++;
    }
    closedir(d);
    return count == want;
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
    char one[192], two[192];
    snprintf(one, sizeof one, "%s/one.wav", dir);
    snprintf(two, sizeof two, "%s/two.wav", dir);

    bool wrote = write_silence_wav(one, TRACK_SECONDS) && write_silence_wav(two, TRACK_SECONDS);
    char order[2][MINI_ICECAST_STATUS_FILE_MAX];
    bool listed = read_order(dir, order, 2);
    CHECK(wrote, "two long WAV tracks were written to %s", dir);
    CHECK(listed, "the directory lists both tracks");

    mini_icecast_cfg_t cfg = {
        .music_dir = dir,
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

        mini_icecast_skip();
        CHECK(wait_for_file(order[1], last, sizeof last),
              "skip moves to the next track %s (saw %s)", order[1], last);

        mini_icecast_prev();
        CHECK(wait_for_file(order[0], last, sizeof last),
              "prev returns to %s (saw %s)", order[0], last);

        mini_icecast_skip();
        CHECK(wait_for_file(order[1], last, sizeof last),
              "skip after prev moves forward through the history to %s (saw %s)", order[1], last);
    }

    unlink(one);
    unlink(two);
    rmdir(dir);
    TEST_END();
}
