/*
 * The host build's entry point.
 *
 * It is a *host* main, not a fork of the firmware: it calls exactly the same
 * component sources the firmware does (mini_icecast, mi_encode, mi_decode,
 * mi_resample, mini_files, http_server), linked against the host shim instead of
 * ESP-IDF. What it replaces is the board's Wi-Fi and microSD bring-up, which the
 * host has no equivalent of - here the music directory is a path and the
 * "card" is always mounted.
 *
 * Usage: icecast_host [--music-dir DIR] [--port N] [--out-hz N] [--bitrate N]
 *                     [--max-clients N] [--metaint N] [--lead-ms N] [--burst-ms N]
 *                     [--name NAME]
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_server.h"
#include "mini_files.h"
#include "mini_icecast.h"

static const char *TAG = "host";

/* The same visualiser the firmware wires up (main/main.c): it proves the pcm_cb
 * contract on the host, where a wrong frame count or a stale pointer is cheap to
 * notice. */
static volatile float level_l, level_r;

static void on_pcm(const int16_t *pcm, size_t frames, void *ctx)
{
    (void)ctx;
    int64_t sl = 0, sr = 0;
    for (size_t i = 0; i < frames; i++) {
        int l = pcm[2 * i], r = pcm[2 * i + 1];
        sl += (int64_t)l * l;
        sr += (int64_t)r * r;
    }
    level_l = sqrtf((float)sl / (float)frames) / 32768.0f;
    level_r = sqrtf((float)sr / (float)frames) / 32768.0f;
}

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
           "  --name NAME       station name (icy-name)\n",
           argv0, MINI_ICECAST_MAX_CLIENTS);
}

int main(int argc, char **argv)
{
    const char *music_dir = "host/fixtures";
    const char *station = "ESP32 Radio (host)";
    uint16_t port = 18000;
    uint32_t out_hz = 44100, bitrate = 128, metaint = 16000, lead_ms = 1500, burst_ms = 500;
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
        else if (!strcmp(a, "--name") && v) { station = v; i++; }
        else {
            fprintf(stderr, "%s: unrecognised argument %s\n", argv[0], a);
            usage(argv[0]);
            return 2;
        }
    }

    mini_icecast_cfg_t icfg = {
        .music_dir = music_dir,
        .station_name = station,
        .port = port,
        .max_clients = max_clients,
        .out_hz = out_hz,
        .bitrate = bitrate,
        .metaint = metaint,
        .lead_ms = lead_ms,
        .burst_ms = burst_ms,
        .pcm_cb = on_pcm,
    };
    esp_err_t err = mini_icecast_start(&icfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot start the broadcast: %s", esp_err_to_name(err));
        return 1;
    }

    mini_files_cfg_t fcfg = {.station_name = station, .music_dir = music_dir, .mount_error = NULL};
    err = mini_files_start(&fcfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot start the file surface: %s", esp_err_to_name(err));
        return 1;
    }

    http_server_cfg_t scfg = {.port = port, .server_name = "mini_icecast/host", .max_conns = 4};
    err = http_server_start(&scfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot start the HTTP server: %s", esp_err_to_name(err));
        return 1;
    }

    printf("listening on http://127.0.0.1:%u/  (stream: /stream, downloads: /music/<name>)\n", port);
    fflush(stdout);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        char title[121];
        mini_icecast_now_playing(title, sizeof title);
        printf("now playing: %-40.40s  L %3d%%  R %3d%%\n", title, (int)(level_l * 100),
               (int)(level_r * 100));
        fflush(stdout);
    }
    return 0;
}
