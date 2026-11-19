/*
 * T45: the station stops and starts, and the format follows.
 *
 * EN-08's acceptance is ten restarts on the board with the heap recorded; what the host
 * can hold still is the lifecycle itself. A stop gives the audio path back - the encoder,
 * the order, the rings and the burst - the station says it is stopped, and a start after
 * it comes back at the new format and plays again. A station cannot be started twice.
 */
#include "test_util.h"

#include <dirent.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "mini_icecast.h"

#define HZ 44100
#define KBPS_ESPNOW 128
#define KBPS_HTTP 256
#define PORT 18101

static const char *find_music_dir(void)
{
    /* Unit tests run from host/; the integration script runs from the repository
     * root. Accept either rather than depending on which. */
    static const char *cands[] = {"fixtures", "host/fixtures"};
    for (size_t i = 0; i < sizeof cands / sizeof cands[0]; i++) {
        DIR *d = opendir(cands[i]);
        if (d) {
            closedir(d);
            return cands[i];
        }
    }
    return NULL;
}

static mini_icecast_cfg_t cfg_for(const library_t *lib, uint32_t kbps)
{
    mini_icecast_cfg_t c = {
        .library = lib,
        .station_name = "test",
        .port = PORT,
        .out_hz = HZ,
        .bitrate = kbps,
        .max_clients = 1,
    };
    return c;
}

/* The first Track can take a moment: the decoder has to open the file. */
static bool wait_playing(int ms)
{
    mini_icecast_status_t st;
    for (int i = 0; i < ms / 100; i++) {
        usleep(100 * 1000);
        mini_icecast_status(&st);
        if (st.file[0]) return true;
    }
    return false;
}

static bool wait_audio(int ms)
{
    mini_icecast_status_t st;
    for (int i = 0; i < ms / 100; i++) {
        usleep(100 * 1000);
        mini_icecast_status(&st);
        if (st.elapsed_ms > 0) return true;
    }
    return false;
}

int main(void)
{
    const char *dir = find_music_dir();
    if (!dir) {
        printf("FATAL: no fixture corpus found\n");
        return 1;
    }
    library_cfg_t lc = {.music_dir = dir};
    library_t *lib = NULL;
    if (library_open(&lc, &lib) != LIB_OK || !lib) {
        printf("FATAL: the Library did not open\n");
        return 1;
    }

    mini_icecast_cfg_t cfg = cfg_for(lib, KBPS_ESPNOW);
    CHECK_EQ(mini_icecast_start(&cfg), 0, "the station starts at 128 kbps");
    CHECK(mini_icecast_running(), "and says it is running");
    CHECK(wait_playing(5000), "a Track is playing");

    /* Two stations in one process would share the encoder, the tasks and the routes.
     * The second start is refused rather than allowed to leak the first. */
    CHECK_EQ(mini_icecast_start(&cfg), ESP_ERR_INVALID_STATE, "a station cannot be started twice");
    CHECK(mini_icecast_running(), "and the running one is untouched by the refusal");

    mini_icecast_stop();
    CHECK(!mini_icecast_running(), "the stop leaves it stopped");
    mini_icecast_status_t st;
    mini_icecast_status(&st);
    CHECK(st.file[0] == 0, "and the Track it was playing is gone from the status");
    CHECK_EQ(st.format, MI_FMT_NONE, "with no container to report");

    /* The format follows the transport: the same station starts again at 256 and plays. */
    cfg = cfg_for(lib, KBPS_HTTP);
    CHECK_EQ(mini_icecast_start(&cfg), 0, "the station starts again at 256 kbps");
    CHECK(mini_icecast_running(), "and says it is running");
    mini_icecast_status(&st);
    CHECK_EQ(st.bitrate, KBPS_HTTP, "the new station reports the new bit rate");
    CHECK_EQ(st.out_hz, HZ, "at the same sample rate");
    CHECK(wait_playing(5000), "and a Track plays again");
    CHECK(wait_audio(3000), "whose position advances");

    /* The pair of allocate and free a restart makes, churned: a leak or a dangling ring
     * shows up here before it reaches the board's ten restarts. */
    for (int i = 0; i < 3; i++) {
        uint32_t kbps = (i % 2) ? KBPS_HTTP : KBPS_ESPNOW;
        mini_icecast_stop();
        mini_icecast_cfg_t again = cfg_for(lib, kbps);
        CHECK_EQ(mini_icecast_start(&again), 0, "a restart starts");
        CHECK(wait_playing(5000), "a Track plays after restart %d", i + 1);
        mini_icecast_status(&st);
        CHECK(st.bitrate == kbps, "restart %d reports %u kbps (got %u)", i + 1,
              (unsigned)kbps, (unsigned)st.bitrate);
    }
    mini_icecast_stop();
    CHECK(!mini_icecast_running(), "and the last stop leaves it stopped");

    library_close(lib);
    TEST_END();
}
