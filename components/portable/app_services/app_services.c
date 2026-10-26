/*
 * app_services - the one place the services and their order live.
 *
 * The comments that explained why the network had to come before the encoder, and
 * why validation had to come before either, have moved here; that is what the
 * ordering is now.
 */
#include "app_services.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "app_board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_server.h"
#include "library.h"
#include "library_http.h"
#include "mcp.h"
#include "mi_station.h"
#include "mini_files.h"
#include "mini_icecast.h"

static const char *TAG = "app";

/* The configuration the rescan task needs after start() has returned. The
 * strings point at the caller's static configuration, which outlives boot. */
static app_services_cfg_t s_cfg;
static atomic_bool s_rescan_running;
static pthread_mutex_t s_card_lock = PTHREAD_MUTEX_INITIALIZER;
static bool s_card_present;
static char s_card_error[160];

static esp_err_t fail(app_services_report_t *r, esp_err_t err, const char *what)
{
    if (r) snprintf(r->failure, sizeof r->failure, "%s: %s", what, esp_err_to_name(err));
    return err;
}

static void rescan_task(void *arg)
{
    (void)arg;
    library_cfg_t cfg = {
        .music_dir = s_cfg.music_dir,
        .max_bytes = s_cfg.library_max_bytes,
        .max_tracks = s_cfg.library_max_tracks,
        .generation = library_next_generation(),
        .build_json = true,
    };
    int64_t t0 = esp_timer_get_time();
    uint32_t psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    /* One card user at a time: a download may already own the same lock. The
     * rescan waits for it before parking the broadcast, so the two never walk
     * FATFS concurrently (and a download cannot unpark the scan). */
    while (!mini_files_card_lock(1000))
        vTaskDelay(pdMS_TO_TICKS(1000));

    /* The card is shared with the broadcast. Parking it for the scan keeps the
     * current Track intact and avoids the IDF 5.4 VFS readdir/stat cache race. */
    mini_icecast_park_card(600000);
    vTaskDelay(pdMS_TO_TICKS(80));

    pthread_mutex_lock(&s_card_lock);
    bool card_present = s_card_present;
    char card_error[sizeof s_card_error];
    snprintf(card_error, sizeof card_error, "%s", s_card_error);
    pthread_mutex_unlock(&s_card_lock);

    library_t *next = NULL;
    lib_result_t lr = card_present ? library_open(&cfg, &next)
                                   : library_open_no_card(&cfg, card_error, &next);
    if (lr == LIB_OK && next) {
        uint32_t gen = library_generation(next);
        unsigned tracks = library_track_count(next);
        unsigned dirs = library_dir_count(next);
        size_t arena_len = 0, json_len = 0;
        (void)library_blob(next, &arena_len);
        (void)library_json(next, &json_len);
        int64_t scan_ms = (esp_timer_get_time() - t0) / 1000;
        if (library_publish(next)) {
            ESP_LOGI(TAG, "library rescan: generation %u, tracks=%u dirs=%u, "
                          "scan=%lld ms, arena=%u B json=%u B, psram %u -> %u B",
                     (unsigned)gen, tracks, dirs, (long long)scan_ms,
                     (unsigned)arena_len, (unsigned)json_len, (unsigned)psram_before,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        } else {
            ESP_LOGE(TAG, "library rescan: publish refused");
            library_close(next);
        }
    } else {
        ESP_LOGE(TAG, "library rescan: generation %u failed (%s); keeping the old one",
                 (unsigned)cfg.generation, lr == LIB_NO_MEM ? "out of memory" : "invalid scan");
        if (next) library_close(next);
    }

    mini_icecast_unpark_card();
    mini_files_card_unlock();
    atomic_store(&s_rescan_running, false);
    vTaskDelete(NULL);
}

static http_result_t rescan_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (strcmp(req->path, "/api/rescan") != 0) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    esp_err_t e = app_services_rescan();
    const char *body;
    int status;
    if (e == ESP_OK) {
        status = 202;
        body = "{\"ok\":true}";
    } else if (e == ESP_ERR_INVALID_STATE) {
        status = 409;
        body = "{\"ok\":false,\"error\":\"a scan is already running\"}";
    } else {
        status = 500;
        body = "{\"ok\":false,\"error\":\"cannot start the scan\"}";
    }
    size_t len = strlen(body);
    http_send_headers(fd, status, "application/json", len, NULL);
    http_send_all(fd, body, len);
    return HTTP_HANDLED;
}

void app_services_card_state(bool present, const char *mount_error)
{
    pthread_mutex_lock(&s_card_lock);
    s_card_present = present;
    snprintf(s_card_error, sizeof s_card_error, "%s", mount_error ? mount_error : "");
    pthread_mutex_unlock(&s_card_lock);
}

bool app_services_rescan_running(void)
{
    return atomic_load(&s_rescan_running);
}

esp_err_t app_services_rescan(void)
{
    if (atomic_exchange(&s_rescan_running, true)) return ESP_ERR_INVALID_STATE;
    if (xTaskCreatePinnedToCore(rescan_task, "library_rescan", 8192, NULL, 4, NULL, 0) != pdPASS) {
        atomic_store(&s_rescan_running, false);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t app_services_check(const app_services_cfg_t *cfg, char *err, size_t err_size)
{
    if (!cfg || !cfg->music_dir) {
        if (err && err_size) snprintf(err, err_size, "no music directory configured");
        return ESP_ERR_INVALID_ARG;
    }
    /* The station module is the authority on the defaults and the arithmetic.
     * The Library has not been built yet at validation time; only the station
     * fields are being checked here. */
    mini_icecast_cfg_t ice = {
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
    s_cfg = *cfg;

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
    app_services_card_state(card.music_dir != NULL, card.mount_error);
    if (!card.music_dir) {
        ESP_LOGE(TAG, "no card mounted: %s", card.mount_error ? card.mount_error : "unknown error");
        ESP_LOGE(TAG, "the device will still serve on the network; the index will say why");
    }

    /* 4. The library. Built after the mount (the card state is known) and before
     * the file surface and the broadcast, so both start with a generation. A
     * missing card or a missing Music directory is a state, not a failure. */
    int64_t scan_t0 = esp_timer_get_time();
    uint32_t psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    library_t *s_library = NULL;
    library_cfg_t library_cfg = {
        .music_dir = cfg->music_dir,
        .max_bytes = cfg->library_max_bytes,
        .max_tracks = cfg->library_max_tracks,
        .generation = library_next_generation(),
        .build_json = true,
    };
    lib_result_t lr = card.music_dir
                          ? library_open(&library_cfg, &s_library)
                          : library_open_no_card(&library_cfg, card.mount_error, &s_library);
    int64_t scan_ms = (esp_timer_get_time() - scan_t0) / 1000;
    uint32_t psram_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (lr != LIB_OK || !s_library) {
        /* A failed generation must not take the device off the network. The
         * file surface still serves its diagnostic page; the broadcast idles and
         * /api/library is unavailable until the next boot/rescan. */
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
        library_publish(s_library);
    }

    /* 5. The file surface. It serves the index even with no card, which is how a
     * cardless device stays diagnosable. */
    mini_files_cfg_t files = {
        .station_name = cfg->station_name,
        .library = NULL, /* the file surface leases the published generation */
        .mount_error = card.music_dir ? NULL : card.mount_error,
    };
    e = mini_files_start(&files);
    if (e != ESP_OK) return fail(report, e, "file surface");
    if (report) report->files_up = true;

    /* 6. The library endpoint. Registration only; the listener is still ahead. */
    e = library_http_start(NULL);
    if (e != ESP_OK) return fail(report, e, "library endpoint");

    /* 7. The broadcast, now that Wi-Fi's buffers exist. It starts even with no
     * card: the task waits for the directory rather than failing the boot. */
    mini_icecast_cfg_t ice = {
        .library = NULL, /* the broadcast leases the published generation */
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

    /* The MCP endpoint is registered before the listener, after the broadcast
     * exists because player_status reads its snapshot. */
    mcp_cfg_t mcp = {
        .library = NULL, /* MCP leases the published generation per call */
        .scan_ms = (uint32_t)scan_ms,
        .local_ip = app_board_local_ip,
        .rescan = app_services_rescan,
        .port = cfg->port,
    };
    e = mcp_start(&mcp);
    if (e != ESP_OK) return fail(report, e, "MCP endpoint");

    e = http_server_register_route_method("/api/rescan", "POST", rescan_route, NULL);
    if (e != ESP_OK) return fail(report, e, "rescan endpoint");

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
