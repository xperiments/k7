/*
 * app_services - the one place the services and their order live.
 *
 * The comments that explained why the network had to come before the encoder, and
 * why validation had to come before either, have moved here; that is what the
 * ordering is now.
 */
#include "app_services.h"

#include <stdio.h>
#include <string.h>

#include "app_board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "http_server.h"
#include "library.h"
#include "library_http.h"
#include "mi_station.h"
#include "mini_files.h"
#include "mini_icecast.h"

static const char *TAG = "app";

/* One generation for this boot. Ticket 11 replaces this with the publish/lease
 * pair; the endpoint's pointer is stable until then. */
static library_t *s_library;

static esp_err_t fail(app_services_report_t *r, esp_err_t err, const char *what)
{
    if (r) snprintf(r->failure, sizeof r->failure, "%s: %s", what, esp_err_to_name(err));
    return err;
}

esp_err_t app_services_check(const app_services_cfg_t *cfg, char *err, size_t err_size)
{
    if (!cfg || !cfg->music_dir) {
        if (err && err_size) snprintf(err, err_size, "no music directory configured");
        return ESP_ERR_INVALID_ARG;
    }
    /* The station module is the authority on the defaults and the arithmetic.
     * music_dir is not part of the station format; it is passed so the cfg type
     * is the one the broadcast uses. */
    mini_icecast_cfg_t ice = {
        .music_dir = cfg->music_dir,
        .station_name = cfg->station_name,
        .port = cfg->port,
        .max_clients = cfg->max_clients,
        .out_hz = cfg->out_hz,
        .bitrate = cfg->bitrate,
        .metaint = cfg->metaint,
        .lead_ms = cfg->lead_ms,
        .burst_ms = cfg->burst_ms,
    };
    return mi_station_resolve(&ice, NULL, err, err_size);
}

esp_err_t app_services_start(const app_services_cfg_t *cfg, app_services_report_t *report)
{
    app_services_report_t local;
    memset(&local, 0, sizeof local);
    if (report) *report = local;
    if (!cfg || !cfg->music_dir) return ESP_ERR_INVALID_ARG;

    /* 1. The station, before anything allocates. Validation is arithmetic. */
    char err[192];
    if (app_services_check(cfg, err, sizeof err) != ESP_OK) {
        if (report) {
            snprintf(report->station_error, sizeof report->station_error, "%s", err);
            snprintf(report->failure, sizeof report->failure, "station format refused: %s", err);
        }
        return ESP_ERR_INVALID_ARG;
    }
    if (report) report->station_ok = true;

    /* 2. The network, before the services create sockets and before the encoder's
     * state competes for internal RAM. A failure here is fatal: nothing can be
     * served without it. */
    esp_err_t e = app_board_net_start(cfg->port);
    if (e != ESP_OK) return fail(report, e, "network");

    /* 3. The card. A missing card is a report, not a failure (R8). */
    app_board_card_t card = app_board_card_mount(cfg->music_dir);
    if (report) {
        report->card_present = card.music_dir != NULL;
        snprintf(report->card_error, sizeof report->card_error, "%s",
                 card.mount_error ? card.mount_error : "");
    }
    if (!card.music_dir) {
        ESP_LOGE(TAG, "no card mounted: %s", card.mount_error ? card.mount_error : "unknown error");
        ESP_LOGE(TAG, "the device will still serve on the network; the index will say why");
    }

    /* 4. The library. Built after the mount (the card state is known) and before
     * the file surface and the broadcast, so both start with a generation. A
     * missing card or a missing Music directory is a state, not a failure. */
    int64_t scan_t0 = esp_timer_get_time();
    uint32_t psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    library_cfg_t library_cfg = {
        .music_dir = cfg->music_dir,
        .max_bytes = cfg->library_max_bytes,
        .max_tracks = cfg->library_max_tracks,
        .build_json = true,
    };
    lib_result_t lr = card.music_dir
                          ? library_open(&library_cfg, &s_library)
                          : library_open_no_card(&library_cfg, card.mount_error, &s_library);
    int64_t scan_ms = (esp_timer_get_time() - scan_t0) / 1000;
    uint32_t psram_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (lr != LIB_OK || !s_library) {
        /* A failed generation must not take the device off the network. The
         * index and the broadcast still have their own paths; /api/library is
         * simply unavailable until the next boot/rescan. */
        ESP_LOGE(TAG, "library: cannot build a generation (%s); /api/library is unavailable"
                      " (scan %lld ms, psram %u -> %u B)",
                 lr == LIB_INVALID_ARG ? "invalid argument" : "out of memory",
                 (long long)scan_ms, (unsigned)psram_before, (unsigned)psram_after);
    } else {
        size_t arena_len = 0, json_len = 0;
        (void)library_blob(s_library, &arena_len);
        (void)library_json(s_library, &json_len);
        ESP_LOGI(TAG, "library: state=%s tracks=%u dirs=%u bytes=%llu scan=%lld ms "
                      "arena=%u B json=%u B psram=%u -> %u B truncated=%d",
                 library_state_name(library_state(s_library)),
                 (unsigned)library_track_count(s_library),
                 (unsigned)library_dir_count(s_library),
                 (unsigned long long)library_total_bytes(s_library), (long long)scan_ms,
                 (unsigned)arena_len, (unsigned)json_len, (unsigned)psram_before,
                 (unsigned)psram_after, library_truncated(s_library) ? 1 : 0);
    }

    /* 5. The file surface. It serves the index even with no card, which is how a
     * cardless device stays diagnosable. */
    mini_files_cfg_t files = {
        .station_name = cfg->station_name,
        .music_dir = card.music_dir,
        .mount_error = card.music_dir ? NULL : card.mount_error,
    };
    e = mini_files_start(&files);
    if (e != ESP_OK) return fail(report, e, "file surface");
    if (report) report->files_up = true;

    /* 6. The library endpoint. Registration only; the listener is still ahead. */
    if (s_library) {
        e = library_http_start(s_library);
        if (e != ESP_OK) return fail(report, e, "library endpoint");
    } else {
        ESP_LOGW(TAG, "library endpoint not registered: /api/library is unavailable");
    }

    /* 7. The broadcast, now that Wi-Fi's buffers exist. It starts even with no
     * card: the task waits for the directory rather than failing the boot. */
    mini_icecast_cfg_t ice = {
        .music_dir = cfg->music_dir,
        .station_name = cfg->station_name,
        .port = cfg->port,
        .max_clients = cfg->max_clients,
        .out_hz = cfg->out_hz,
        .bitrate = cfg->bitrate,
        .metaint = cfg->metaint,
        .lead_ms = cfg->lead_ms,
        .burst_ms = cfg->burst_ms,
    };
    e = mini_icecast_start(&ice);
    if (e != ESP_OK) return fail(report, e, "broadcast");
    if (report) report->broadcast_up = true;

    /* 8. The listener, last: every route is registered by now. */
    http_server_cfg_t http = {
        .port = cfg->port,
        .server_name = cfg->server_name ? cfg->server_name : "sd-stream/1.0",
        .max_conns = cfg->max_conns ? cfg->max_conns : 4,
    };
    e = http_server_start(&http);
    if (e != ESP_OK) return fail(report, e, "HTTP server");
    if (report) report->http_up = true;

    return ESP_OK;
}
