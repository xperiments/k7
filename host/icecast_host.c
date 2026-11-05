/*
 * The host build's entry point.
 *
 * It is a *host* main, not a fork of the firmware: it calls the same service
 * composition the firmware does (app_services, which starts mini_files,
 * mini_icecast and http_server in the one order), linked against the host shim
 * and the host board adapter instead of ESP-IDF. What it replaces is the board's
 * Wi-Fi and microSD bring-up - here the music directory is a path and the "card"
 * is always mounted.
 *
 * Usage: icecast_host [--music-dir DIR] [--port N] [--out-hz N] [--bitrate N]
 *                     [--max-clients N] [--metaint N] [--lead-ms N] [--burst-ms N]
 *                     [--library-max-bytes N] [--library-max-tracks N] [--name NAME]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_services.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mini_icecast.h"

static const char *TAG = "host";

static void usage(const char *argv0)
{
    printf("usage: %s [options]\n"
           "  --music-dir DIR   directory of .mp3/.flac/.wav tracks (default host/fixtures)\n"
           "  --port N          listen port (default 18000: no root needed)\n"
           "  --out-hz N        station sample rate (default 44100)\n"
           "  --bitrate N       station bit rate in kbps (default 128)\n"
           "  --max-clients N   simultaneous stream clients, 1..%d (default 2)\n"
           "  --metaint N       encoded bytes between ICY metadata blocks (default 16000)\n"
           "  --lead-ms N       how far ahead of real time to send (default 1500)\n"
           "  --burst-ms N      history sent on connect (default 500)\n"
           "  --library-max-bytes N   library arena+JSON budget (default 4 MiB)\n"
           "  --library-max-tracks N  library track cap (default 20000)\n"
           "  --name NAME       station name (icy-name)\n",
           argv0, MINI_ICECAST_MAX_CLIENTS);
}

int main(int argc, char **argv)
{
    const char *music_dir = "host/fixtures";
    const char *station = "ESP32 Radio (host)";
    uint16_t port = 18000;
    uint32_t out_hz = 44100, bitrate = 128, metaint = 16000, lead_ms = 1500, burst_ms = 500;
    uint32_t library_max_bytes = 0, library_max_tracks = 0;
    uint8_t max_clients = 2;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage(argv[0]);
            return 0;
        } else if (!strcmp(a, "--music-dir") && v) { music_dir = v; i++; }
        else if (!strcmp(a, "--port") && v) { port = (uint16_t)atoi(v); i++; }
        else if (!strcmp(a, "--out-hz") && v) { out_hz = (uint32_t)atoi(v); i++; }
        else if (!strcmp(a, "--bitrate") && v) { bitrate = (uint32_t)atoi(v); i++; }
        else if (!strcmp(a, "--max-clients") && v) { max_clients = (uint8_t)atoi(v); i++; }
        else if (!strcmp(a, "--metaint") && v) { metaint = (uint32_t)atoi(v); i++; }
        else if (!strcmp(a, "--lead-ms") && v) { lead_ms = (uint32_t)atoi(v); i++; }
        else if (!strcmp(a, "--burst-ms") && v) { burst_ms = (uint32_t)atoi(v); i++; }
        else if (!strcmp(a, "--library-max-bytes") && v) { library_max_bytes = (uint32_t)atoi(v); i++; }
        else if (!strcmp(a, "--library-max-tracks") && v) { library_max_tracks = (uint32_t)atoi(v); i++; }
        else if (!strcmp(a, "--name") && v) { station = v; i++; }
        else {
            fprintf(stderr, "%s: unrecognised argument %s\n", argv[0], a);
            usage(argv[0]);
            return 2;
        }
    }

    /* The same composition the firmware runs, with the host's board adapter: the
     * fixture directory is always mounted and there is no network to bring up. */
    app_services_cfg_t cfg = {
        .music_dir = music_dir,
        .station_name = station,
        .port = port,
        .out_hz = out_hz,
        .bitrate = bitrate,
        .max_clients = max_clients,
        .max_conns = 4,
        .metaint = metaint,
        .lead_ms = lead_ms,
        .burst_ms = burst_ms,
        .library_max_bytes = library_max_bytes,
        .library_max_tracks = library_max_tracks,
        .server_name = "mini_icecast/host",
    };
    app_services_report_t report;
    esp_err_t err = app_services_start(&cfg, &report);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s", report.failure[0] ? report.failure : esp_err_to_name(err));
        return 1;
    }

    printf("listening on http://127.0.0.1:%u/  (library: /api/library, stream: /stream; "
           "no Track routes)\n", port);
    fflush(stdout);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        mini_icecast_status_t st;
        mini_icecast_status(&st);
        printf("now playing: %-40.40s  L %3d%%  R %3d%%\n", st.title,
               (int)(st.level_l * 100), (int)(st.level_r * 100));
        fflush(stdout);
    }
    return 0;
}
