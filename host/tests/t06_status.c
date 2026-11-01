/*
 * T6: what the broadcast says it is playing.
 *
 * The cassette display reads this, so the assertions are about a display's needs
 * rather than about internals: a name to show, a container to report, the station's
 * format, a length for the progress bar, a position that advances with the audio, and
 * a library size to say "of N". Run against the real fixture corpus through the real
 * broadcast, because the figures come from the decoder and the directory scan.
 */
#include "test_util.h"

#include <dirent.h>
#include <string.h>
#include <unistd.h>

#include "mini_icecast.h"
#include "mi_decode.h"

#define HZ 44100
#define KBPS 128

static const char *find_music_dir(void)
{
    /* Unit tests run from host/; the integration script runs from the repository
     * root. Accept either rather than depending on which. */
    static const char *cands[] = {"fixtures", "host/fixtures"};
    for (size_t i = 0; i < sizeof cands / sizeof cands[0]; i++) {
        DIR *d = opendir(cands[i]);
        if (d) { closedir(d); return cands[i]; }
    }
    return NULL;
}

static const char *fmt_name(mi_fmt_t f)
{
    switch (f) {
    case MI_FMT_MP3: return "mp3";
    case MI_FMT_FLAC: return "flac";
    case MI_FMT_WAV: return "wav";
    default: return "none";
    }
}

int main(void)
{
    const char *dir = find_music_dir();
    if (!dir) {
        printf("FATAL: no fixture corpus found\n");
        return 1;
    }

    library_cfg_t lc = {.music_dir = dir, .build_json = false};
    library_t *lib = NULL;
    if (library_open(&lc, &lib) != LIB_OK || !lib) {
        printf("FATAL: the Library did not open\n");
        return 1;
    }
    mini_icecast_cfg_t cfg = {
        .library = lib,
        .station_name = "test",
        .port = 18099,
        .out_hz = HZ,
        .bitrate = KBPS,
        .max_clients = 1,
    };
    if (mini_icecast_start(&cfg) != 0) {
        printf("FATAL: the broadcast did not start\n");
        return 1;
    }

    mini_icecast_status_t st;
    bool got_playing = false;
    for (int i = 0; i < 40 && !got_playing; i++) {   /* the first track can take a moment */
        usleep(100 * 1000);
        mini_icecast_status(&st);
        got_playing = st.file[0] != 0 && st.source_hz != 0;
    }
    CHECK(got_playing, "a track is playing and its source rate is known");
    if (!got_playing) return 1;
    CHECK(st.path[0] != 0, "the status carries the Track's relative path: %s", st.path);
    CHECK_EQ(st.generation, library_generation(lib), "the status names the generation");
    CHECK(st.track_index < st.library_count, "the track index is inside the broadcast order");
    const char *slash = strrchr(st.path, '/');
    CHECK(strcmp(st.file, slash ? slash + 1 : st.path) == 0,
          "the file name is the last segment of the path");

    /* The display's level rides in the same snapshot (ticket 16): the callback it
     * used to come from is gone, so this is the only way a screen can see it. */
    bool got_level = false;
    for (int i = 0; i < 40 && !got_level; i++) {
        usleep(100 * 1000);
        mini_icecast_status(&st);
        got_level = st.level_l > 0.0f || st.level_r > 0.0f;
    }
    CHECK(st.level_l >= 0.0f && st.level_l <= 1.0f && st.level_r >= 0.0f && st.level_r <= 1.0f,
          "the level is 0..1 per channel (%f, %f)", (double)st.level_l, (double)st.level_r);
    CHECK(got_level, "the level is non-zero while a track plays (%f, %f)",
          (double)st.level_l, (double)st.level_r);

    /* The name and the facts a display puts next to it. */
    CHECK(st.title[0] != 0, "there is a title to show");
    CHECK(strncmp(st.title, st.file, strlen(st.title)) == 0,
          "the title is the file's name without its extension");
    const char *ext = strrchr(st.file, '.');
    CHECK(ext && mi_dec_format_of(st.file) == st.format,
          "the reported container matches the file's extension (%s -> %s)", st.file, fmt_name(st.format));
    CHECK(st.source_hz == 44100 || st.source_hz == 48000 || st.source_hz == 32000,
          "the source rate is one the fixture corpus uses (%u Hz)", (unsigned)st.source_hz);
    CHECK(st.out_hz == HZ && st.bitrate == KBPS,
          "the station's own format is reported (%u Hz, %u kbps)",
          (unsigned)st.out_hz, (unsigned)st.bitrate);
    CHECK(st.library_count == 11, "the library's size is known (%u files, expected 11)",
          (unsigned)st.library_count);

    /* A length and a position, which is what a progress bar needs. */
    CHECK(st.duration_ms > 100 && st.duration_ms < 120000,
          "the track has a plausible length (%u ms)", (unsigned)st.duration_ms);
    CHECK(st.elapsed_ms <= st.duration_ms + 200,
          "the position is inside the track (%u of %u ms)",
          (unsigned)st.elapsed_ms, (unsigned)st.duration_ms);

    uint32_t first = st.elapsed_ms, later = first;
    for (int i = 0; i < 30 && later <= first; i++) {
        usleep(100 * 1000);
        mini_icecast_status(&st);
        later = st.elapsed_ms;
    }
    CHECK(later > first, "the position advances with the audio (%u -> %u ms)", (unsigned)first, (unsigned)later);

    /* A track change must reset it: a bar that keeps climbing across tracks would be
     * worse than no bar at all. */
    char before[MINI_ICECAST_STATUS_FILE_MAX];
    snprintf(before, sizeof before, "%s", st.file);
    bool changed = false;
    for (int i = 0; i < 100 && !changed; i++) {
        usleep(100 * 1000);
        mini_icecast_status(&st);
        changed = strcmp(st.file, before) != 0;
    }
    CHECK(changed, "the broadcast moves on to another track (%s -> %s)", before, st.file);
    if (changed)
        CHECK(st.elapsed_ms < st.duration_ms + 200,
              "the position restarts with the new track (%u of %u ms)",
              (unsigned)st.elapsed_ms, (unsigned)st.duration_ms);

    TEST_END();
}
