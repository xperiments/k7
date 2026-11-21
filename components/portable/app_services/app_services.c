/*
 * app_services - the one place the services and their order live.
 *
 * The comments that explained why the network had to come before the encoder, and
 * why validation had to come before either, have moved here; that is what the
 * ordering is now.
 */
#include "app_services.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_board.h"
#include "art.h"
#include "card_path.h"
#include "card_use.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "http_server.h"
#include "covers_http.h"
#include "http_json.h"
#include "json.h"
#include "library.h"
#include "library_http.h"
#include "mcp.h"
#include "mi_station.h"
#include "mini_files.h"
#include "mini_icecast.h"
#include "snapshot.h"
#include "snapshot_http.h"
#include "front_door.h"
#include "setup.h"
#include "share.h"
#include "setup_http.h"
#include "flash_job.h"
#include "playlist_select.h"
#include "playlist_http.h"

static const char *TAG = "app";

/* The configuration the rescan task needs after start() has returned. The
 * strings point at the caller's static configuration, which outlives boot. */
static app_services_cfg_t s_cfg;
static atomic_bool s_rescan_running;
static pthread_mutex_t s_card_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile bool s_card_present;

/* The card's root, for the modules that read and write the playlists beside its music
 * directory. "" when no card is mounted: a list is the card's, and there is nothing to
 * read or write without one (ADR 0094). */
static void current_card_root(char *out, size_t cap);

static void current_card_root(char *out, size_t cap)
{
    if (!out || !cap) return;
    if (!s_card_present) {
        out[0] = 0;
        return;
    }
    card_path_root(out, cap);
}

void app_services_card_root(char *out, size_t cap) { current_card_root(out, cap); }
/* The card's file lifetime. The gate is a mutex + a binary "idle" semaphore rather than a
 * pthread condition: the decoder and the artwork loader call it from FreeRTOS tasks, and
 * the watcher waits here while the card is being pulled. */
static snapshot_t *s_snapshot;
/* The PSRAM copy the Snapshot reads in place; owned for the life of the process. */
static uint8_t *s_snapshot_copy;
static size_t s_snapshot_size;
static snapshot_result_t s_snapshot_state = SNAPSHOT_NO_PARTITION;
static char s_snapshot_reason[96];
static char s_card_error[160];
/* The card's k7.card id, read at mount; empty when the card has no usable mark. */
static char s_card_id[40];

/* The selection's persistence, as the injected store playlist_select takes. */
static bool selection_load(void *ctx, uint64_t *id)
{
    (void)ctx;
    return s_cfg.selection_load ? s_cfg.selection_load(id) : false;
}

static bool selection_save(void *ctx, uint64_t id)
{
    (void)ctx;
    return s_cfg.selection_save ? s_cfg.selection_save(id) : false;
}

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
    };
    int64_t t0 = esp_timer_get_time();
    uint32_t psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    /* One card user at a time: the lock is the card's, so the rescan waits for
     * any other owner before parking the broadcast and never walks FATFS
     * concurrently. */
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
        size_t arena_len = 0;
        (void)library_blob(next, &arena_len);
        int64_t scan_ms = (esp_timer_get_time() - t0) / 1000;
        if (library_publish(next)) {
            /* The indices the walk was given describe the old generation; re-resolve
             * against the card's files. */
            char root[160];
            current_card_root(root, sizeof root);
            playlist_select_sync(root, next);
            /* The coverage follows the published generation, so it is re-walked once here on a
             * card change - never per `library_stats` request. This runs on core 0 with the panel
             * scanning, so a rescan still slips the picture for its flash reads; that is a card
             * event, not the per-request walk the cache removes. */
            mcp_refresh_coverage();
            ESP_LOGI(TAG, "library rescan: generation %u, tracks=%u dirs=%u, "
                          "scan=%lld ms, arena=%u B, psram %u -> %u B",
                     (unsigned)gen, tracks, dirs, (long long)scan_ms,
                     (unsigned)arena_len, (unsigned)psram_before,
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

/* The snapshot arrives over the network instead of on the card: the browser analysed the
 * card where the card is - in the computer - and the measurements are a few kilobytes.
 *
 * The content type is the guard, and it is not incidental: `application/octet-stream` is
 * not a CORS-safelisted type, so a page on another origin cannot send it without a
 * preflight this device never answers. A form post cannot forge one either. That is the
 * whole CSRF story, and it is cheaper than a token the user would have to type. */



/* Where this device is on the network. The server puts it in every reply's `X-K7-Net`, so
 * the App reads it off a request it was making anyway (front-door.md S2). */
static app_services_ip_fn s_ip_fn;

void app_services_set_ip(app_services_ip_fn fn) { s_ip_fn = fn; }

static http_result_t rescan_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (strcmp(req->path, "/api/rescan") != 0) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    esp_err_t e = app_services_rescan();
    if (e == ESP_OK)
        http_json_send(fd, 202, "{\"ok\":true}", NULL);
    else if (e == ESP_ERR_INVALID_STATE)
        http_json_send(fd, 409, "{\"ok\":false,\"error\":\"a scan is already running\"}", NULL);
    else
        http_json_send(fd, 500, "{\"ok\":false,\"error\":\"cannot start the scan\"}", NULL);
    return HTTP_HANDLED;
}



bool app_services_card_present(void) { return s_card_present; }


const snapshot_t *app_services_snapshot(void) { return s_snapshot; }

/* The image itself, as the device holds it: the PSRAM copy the reader reads in place, so a
 * caller serves it without touching the flash mapping (which cannot be mapped from a task
 * whose stack is in PSRAM - the HTTP workers'). */
const uint8_t *app_services_snapshot_image(size_t *size)
{
    if (size) *size = s_snapshot_copy ? s_snapshot_size : 0;
    return s_snapshot_copy;
}

/* The front door asks the question through a callback so it need not depend on this
 * module - the composition depends on both, and the arrow stays one-way. */
static bool front_door_card_present(void)
{
    return s_card_present;
}

/* The status route asks for a copy, not a pointer: the card watcher can replace the mark
 * between two polls, and the reply must carry one whole id. */
static bool front_door_card_mark(char *out, size_t cap)
{
    bool known;
    pthread_mutex_lock(&s_card_lock);
    known = s_card_id[0] != 0;
    if (known && out && cap) snprintf(out, cap, "%s", s_card_id);
    pthread_mutex_unlock(&s_card_lock);
    return known;
}

void app_services_card_state(bool present, const char *mount_error)
{
    pthread_mutex_lock(&s_card_lock);
    s_card_present = present;
    snprintf(s_card_error, sizeof s_card_error, "%s", mount_error ? mount_error : "");
    pthread_mutex_unlock(&s_card_lock);
}

static bool s_player_espnow = true;

bool app_services_player_espnow(void) { return s_player_espnow; }

/* The tone's one owner (ADR 0109). The screen publishes the four values here; the broadcast
 * asks the source below, and the board gets a copy for the player. The source is the one place
 * that decides whether the broadcast owns the tone: it answers false only when ESP-NOW is the
 * transport and the player's last status said it applies the tone. */
static volatile float s_tone_band_db[TONE_EQ_BANDS];
static volatile float s_tone_gain_db;

static bool tone_source(float band_db[TONE_EQ_BANDS], float *gain_db, void *ctx)
{
    (void)ctx;
    if (s_player_espnow && app_board_player_tone_capable()) return false;
    for (int b = 0; b < TONE_EQ_BANDS; b++) band_db[b] = s_tone_band_db[b];
    *gain_db = s_tone_gain_db;
    return true;
}

void app_services_tone_band_set(int band, float db)
{
    if (band < 0 || band >= TONE_EQ_BANDS) return;
    s_tone_band_db[band] = db;
    float all[TONE_EQ_BANDS] = {
        s_tone_band_db[0],
        s_tone_band_db[1],
        s_tone_band_db[2],
    };
    app_board_audio_tone(all, s_tone_gain_db);
}

void app_services_tone_gain_set(float db)
{
    s_tone_gain_db = db;
    float all[TONE_EQ_BANDS] = {
        s_tone_band_db[0],
        s_tone_band_db[1],
        s_tone_band_db[2],
    };
    app_board_audio_tone(all, s_tone_gain_db);
}

/* The emitter and the broadcast's sink move together, so no frame is fed to an emitter that is
 * off and none is missed by one that is on (espnow-stream.md E9). The Settings row and the micro
 * switch both end up here. */
/* Apply without storing: the boot reads a value, it does not write one. */
static void player_transport_apply(bool on)
{
    s_player_espnow = on;
    if (on) {
        if (app_board_audio_set(true)) {
            mini_icecast_set_frame_sink(app_board_audio_frame, NULL);
            ESP_LOGI(TAG, "the player's audio goes out over ESP-NOW");
        } else {
            mini_icecast_set_frame_sink(NULL, NULL);
            ESP_LOGW(TAG, "no ESP-NOW emitter: the player is left with HTTP");
        }
    } else {
        mini_icecast_set_frame_sink(NULL, NULL);
        app_board_audio_set(false);
        ESP_LOGI(TAG, "the player's audio is fetched over HTTP; the ESP-NOW emitter is off");
    }
}

void app_services_player_espnow_set(bool on)
{
    player_transport_apply(on);
    if (app_board_player_espnow_save(on) != ESP_OK)
        ESP_LOGW(TAG, "the transport choice did not store");
    /* The format follows the transport, and the encoder is built for one format: the station
     * stops and starts at the new rate, now rather than at the next boot (ADR 0106). */
    esp_err_t e = app_services_station_restart(app_services_station_bitrate(on));
    if (e != ESP_OK)
        ESP_LOGW(TAG, "the station did not restart at %u kbps: %s",
                 (unsigned)app_services_station_bitrate(on), esp_err_to_name(e));
}

void app_services_player_transport_init(void)
{
    bool stored = true;
    player_transport_apply(app_board_player_espnow_load(&stored) ? stored : true);
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

/* A card is pulled by hand while the device is running, and put back the same way. The
 * boot mount only covers the first one, so a card that goes and returns would otherwise
 * leave the device serving a library whose bytes are gone, with no way back but a reboot.
 *
 * The probe is a directory read of the music directory, not a cached mount flag - the
 * board has no card-detect line, so asking the volume is the only question there is. It
 * can fail for a card that has gone *and* for a card that was merely busy (one read
 * error, or the FATFS volume lock not taken before its timeout), so one failure is not a
 * removal: three in a row are. The other direction cannot be probed at all - an unmounted
 * volume cannot be stat'ed - so the watcher tries the mount again every few seconds.
 * That is also why the state cannot be "the result of the last probe": the mount has to
 * be attempted for a card to be seen to arrive. The update file is not its business: the
 * appliance reboots for that, because applying one needs the window before the panel. */
#define CARD_WATCH_MS        2000
#define CARD_FAILURES        3
#define CARD_MOUNT_RETRY_MS  5000
/* How long the watcher waits for the decoder and the artwork loader to close their files.
 * A card that was pulled can leave one in a blocking read; the wait bounds the watcher, and
 * a timeout leaves the volume mounted rather than unmounting under it. */
#define CARD_DRAIN_MS        5000

/* Close the card's file gate and stop the broadcast's current Track so its decoder closes.
 * True when every holder is out; only then may the volume be forgotten. */
static bool card_drain_files(void)
{
    mini_icecast_skip();
    return card_use_quiesce(CARD_DRAIN_MS);
}

static bool mark_id_valid(const char *s, size_t len)
{
    if (len != 36) return false;
    for (size_t i = 0; i < len; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-') return false;
        } else if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') ||
                     (s[i] >= 'A' && s[i] <= 'F'))) {
            return false;
        }
    }
    return true;
}

/* Read the mark at mount, not on every status poll: the card can vanish, and the App polls
 * status every few seconds. The result is copied under the same lock the card state uses. */
static void card_mark_refresh(void)
{
    char id[40] = {0};
    char path[256];
    card_path_file(path, sizeof path, "k7.card");
    FILE *f = fopen(path, "rb");
    if (f) {
        char buf[256];
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        buf[n] = 0;
        json_tok_t tok[24];
        int nt = 0;
        if (json_parse(buf, tok, 24, &nt) && nt > 0 && tok[0].type == JSON_TOK_OBJECT) {
            int at = json_tok_member(buf, tok, nt, 0, "id");
            if (at >= 0 && json_tok_unescape(buf, &tok[at], id, sizeof id)
                && !mark_id_valid(id, strlen(id))) {
                id[0] = 0;
            }
        }
    }
    pthread_mutex_lock(&s_card_lock);
    snprintf(s_card_id, sizeof s_card_id, "%s", id);
    pthread_mutex_unlock(&s_card_lock);
}

static void card_watch_task(void *arg)
{
    (void)arg;
    unsigned failures = 0;
    int64_t next_mount = 0; /* try at once: the boot may have had no card */

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(CARD_WATCH_MS));
        int64_t now_ms = esp_timer_get_time() / 1000;

        if (!app_services_card_present()) {
            failures = 0;
            if (now_ms < next_mount) continue;
            next_mount = now_ms + CARD_MOUNT_RETRY_MS;

            /* A previous removal may have left the volume mounted when its file users did
             * not drain in time. Close the gate, drop that volume, and only then mount. */
            if (!card_drain_files()) {
                ESP_LOGW(TAG, "the stream still holds the card; not remounting yet");
                continue;
            }
            if (!mini_files_card_lock(1000)) {
                ESP_LOGW(TAG, "the card's volume is busy; not remounting yet");
                continue;
            }
            app_board_card_forget();
            mini_files_card_unlock();

            app_board_card_t card = app_board_card_mount(s_cfg.music_dir);
            if (!card.music_dir) {
                /* Keep the index's reason current: a cardless boot's mount error is
                 * not the one the retry is seeing now. */
                mini_files_set_card(NULL, card.mount_error);
                ESP_LOGW(TAG, "still no card: %s; retrying in %u s",
                         card.mount_error ? card.mount_error : "unknown error",
                         CARD_MOUNT_RETRY_MS / 1000);
                continue;
            }
            app_services_card_state(true, NULL);
            card_use_resume();
            card_mark_refresh();
            mini_files_set_card(card.music_dir, NULL);
            ESP_LOGI(TAG, "card inserted: %s; rescanning", card.music_dir);
            app_services_rescan();
            continue;
        }

        /* A scan owns the card while it walks it, and the VFS readdir/stat cache is
         * not safe for a stat beside a readdir. A probe that cannot have the card
         * waits for the next tick: the transition is not urgent enough to race a scan. */
        if (!mini_files_card_lock(0)) continue;
        int err = 0;
        bool answered = app_board_card_probe(&err);
        mini_files_card_unlock();
        if (answered) {
            failures = 0;
            continue;
        }
        if (err == ENOENT) {
            /* The volume answered and the path is not on it. That is a Library state
             * (no_dir), not a card removal: keep the mount and let the scan say so. */
            failures = 0;
            continue;
        }
        if (++failures < CARD_FAILURES) {
            ESP_LOGW(TAG, "the card did not answer (%s); %u of %u before it counts as gone",
                     err ? strerror(err) : "no error", failures, (unsigned)CARD_FAILURES);
            continue;
        }

        failures = 0;
        char why[96];
        snprintf(why, sizeof why, "the card stopped answering (%s)",
                 err ? strerror(err) : "no error");
        ESP_LOGW(TAG, "%s; the index will say so until it returns", why);
        app_services_card_state(false, why);
        mini_files_set_card(NULL, why);
        card_mark_refresh();
        app_services_rescan();

        /* The volume must not be forgotten under an open decoder or artwork read: FATFS
         * would free the volume and the next f_open/f_read would run on it (dir_find on a
         * null fs, the panic a pulled card produced). Close the gate, stop the Track so its
         * decoder closes, and wait for every holder. A timeout leaves the volume mounted;
         * the retry path tries again, and a mounted volume is recoverable, a freed one is not. */
        if (card_drain_files()) {
            if (mini_files_card_lock(1000)) {
                app_board_card_forget();
                mini_files_card_unlock();
            } else {
                ESP_LOGW(TAG, "the card's volume was busy; leaving it mounted");
            }
        } else {
            ESP_LOGW(TAG, "the card's file users did not drain in %u ms; leaving the volume mounted",
                     CARD_DRAIN_MS);
        }
        next_mount = now_ms + CARD_MOUNT_RETRY_MS;
    }
}

/* The deployed App's origin, for the CORS headers the browser needs before it will read
 * this device's JSON from another origin. The development build adds a second origin
 * through app_services_cfg_t.app_origin_dev; the reply names whichever of the two made
 * the request (http_server.c). */
#define K7_APP_ORIGIN "https://k7.xperiments.dev"

/* A preflight is answered with the headers http_send_headers adds, and no body. Registered
 * for /api/ and /mcp, the surfaces the App and its MCP clients read. */
static http_result_t cors_preflight_route(int fd, const http_request_t *req, void *ctx)
{
    (void)req;
    (void)ctx;
    http_send_headers(fd, 204, NULL, 0, NULL);
    return HTTP_HANDLED;
}

/* The station, composed identically at boot and on a restart: the same broadcast
 * configuration (a format the caller has already resolved into s_cfg) and the same Playlist
 * choice, so a restarted station walks the order the listener had selected. Only the station
 * is touched - the network, the card, the HTTP server and the panel are the boot's. */
static esp_err_t start_station(const library_t *lib)
{
    mini_icecast_cfg_t ice = {
        .library = NULL, /* the broadcast leases the published generation */
        .station_name = s_cfg.station_name,
        .port = s_cfg.port,
        .max_clients = s_cfg.max_clients,
        .out_hz = s_cfg.out_hz,
        .bitrate = s_cfg.bitrate,
        .metaint = s_cfg.metaint,
        .lead_ms = s_cfg.lead_ms,
        .burst_ms = s_cfg.burst_ms,
        .card_use_begin = card_use_begin,
        .card_use_end = card_use_end,
    };
    /* The broadcast's tone source is the composition's: the station asks on every chunk and
     * the source answers with the panel's four values, or with "the player owns it". */
    mini_icecast_set_tone_source(tone_source, NULL);
    esp_err_t e = mini_icecast_start(&ice);
    if (e != ESP_OK) return e;
    /* Which Playlist the broadcast walks, now that the card and the Library exist: the
     * remembered choice is resolved from its file and handed over, or the Library plays. */
    if (lib) {
        char root[160];
        current_card_root(root, sizeof root);
        playlist_select_sync(root, lib);
    }
    return ESP_OK;
}

uint32_t app_services_station_bitrate(bool player_espnow)
{
    return player_espnow ? 128 : 256;
}

esp_err_t app_services_station_restart(uint32_t kbps)
{
    if (!s_cfg.music_dir) {
        ESP_LOGW(TAG, "the station has never started; there is nothing to restart");
        return ESP_ERR_INVALID_STATE;
    }
    /* What was playing, so the new station can continue it after the start (below). Position
     * is not kept: the station starts at the top of a Track, which is what a restart of the
     * encoder means. */
    mini_icecast_status_t before;
    mini_icecast_status(&before);
    mini_icecast_stop();
    s_cfg.bitrate = kbps;
    const library_t *lib = library_acquire();
    esp_err_t e = start_station(lib);
    if (lib) library_release(lib);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "the station did not come back at %u kbps: %s", (unsigned)kbps,
                 esp_err_to_name(e));
        return e;
    }
    ESP_LOGI(TAG, "the station restarted at %u kbps", (unsigned)kbps);
    /* The Track that was playing continues on the new station, so a format change does not
     * sound like a skip. Only with no Playlist walking: a pick that names a Track outside
     * the selected order drops the order (and with it the owner's choice), and a restart
     * must never do that to a selection the row did not touch. */
    if (!mini_icecast_order_active() && before.path[0] && !mini_icecast_play(before.path))
        ESP_LOGW(TAG, "the Track that was playing is not in the Library; the station walks on");
    return ESP_OK;
}

esp_err_t app_services_start(const app_services_cfg_t *cfg, app_services_report_t *report)
{
    app_services_report_t local;
    memset(&local, 0, sizeof local);
    if (report) *report = local;
    if (!cfg || !cfg->music_dir) return ESP_ERR_INVALID_ARG;
    s_cfg = *cfg;
    card_path_init(cfg->music_dir);
    if (card_use_init() != CARD_USE_OK) return fail(report, ESP_ERR_NO_MEM, "card file gate");
    /* The writer exists before any route can be reached. */
    if (!flash_job_start()) return fail(report, ESP_ERR_NO_MEM, "flash writer task");
    playlist_select_store_t store = {NULL, selection_load, selection_save};
    playlist_select_init(&store);

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

    /* The setup network's policy starts here, right after the radio: a device with no
     * network raises its own access point before anything else can go wrong, and the tick
     * task is what decides the boot window, the grace close and the retries. It is not a
     * failure if the access point cannot start - the machine keeps asking and the panel
     * says so (wifi-provisioning.md P35). */
    setup_init();
    setup_start_task();

    /* The share window has no boot state: it is closed until a person opens it, and a
     * device that never shares carries nothing for it (network-handover.md H13) - not even
     * its task, which share_start() creates and which deletes itself when the window
     * closes. The exception H13 now names: when the player's audio travels over ESP-NOW the
     * radio is up from boot and the window only borrows it (espnow-stream.md E10). */
    share_init();

    /* 3. The card. A missing card is a report, not a failure (R8). */
    app_board_card_t card = app_board_card_mount(cfg->music_dir);
    if (report) {
        report->card_present = card.music_dir != NULL;
        snprintf(report->card_error, sizeof report->card_error, "%s",
                 card.mount_error ? card.mount_error : "");
    }
    app_services_card_state(card.music_dir != NULL, card.mount_error);
    card_mark_refresh();
    if (!card.music_dir) {
        ESP_LOGE(TAG, "no card mounted: %s", card.mount_error ? card.mount_error : "unknown error");
        ESP_LOGE(TAG, "the device will still serve on the network; the index will say why");
    }

    /* 3b. The Snapshot: the measurements, from the flash partition the board maps.
     * Independent of the card: a cardless device still answers questions about the
     * measurements, and a card full of new tracks still plays without them. This
     * runs before the Library because it is the cheapest thing in the boot and the
     * only one that can fail without changing what the device plays. */
    {
        size_t snap_size = 0;
        const char *why = "";
        int board_state = SNAPSHOT_NO_PARTITION;
        const uint8_t *img = app_board_snapshot_image(&snap_size, &why, &board_state);
        if (!img) {
            s_snapshot_state = (snapshot_result_t)board_state;
            snprintf(s_snapshot_reason, sizeof s_snapshot_reason, "%s", why ? why : "");
            /* A missing Snapshot is normal (a device that has never been given one,
             * or a build without the partition); an invalid one is not. Either way it
             * is a state worth one line, because "no measurements" is otherwise
             * indistinguishable from "measurements that did not load". */
            if (s_snapshot_state == SNAPSHOT_INVALID)
                ESP_LOGE(TAG, "snapshot: %s; queries answer without measurements",
                         s_snapshot_reason);
            else
                ESP_LOGI(TAG, "snapshot: %s; queries answer without measurements",
                         s_snapshot_reason);
        } else {
            /* The Snapshot is resident: copy the image into PSRAM, give the mapping back,
             * and hand the copy to the reader for the life of the process. On a failed
             * allocation the device answers without measurements rather than fall back to
             * a mapping the panel's scan-out would share a bus with. */
            uint8_t *copy = heap_caps_malloc(snap_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!copy) {
                app_board_snapshot_release();
                s_snapshot_state = SNAPSHOT_NO_MEM;
                snprintf(s_snapshot_reason, sizeof s_snapshot_reason,
                         "not enough PSRAM for the %u-byte image", (unsigned)snap_size);
                ESP_LOGE(TAG, "snapshot: %s; queries answer without measurements",
                         s_snapshot_reason);
            } else {
                memcpy(copy, img, snap_size);
                app_board_snapshot_release();
                char open_why[96] = {0};
                s_snapshot_state = snapshot_open_why(copy, snap_size, &s_snapshot, open_why,
                                                     sizeof open_why);
                if (s_snapshot_state == SNAPSHOT_OK) {
                    s_snapshot_copy = copy;
                    s_snapshot_size = snap_size;
                    ESP_LOGI(TAG, "snapshot: %u rows, generation %u, analyser %u",
                             (unsigned)snapshot_rows(s_snapshot),
                             (unsigned)snapshot_generation(s_snapshot),
                             (unsigned)snapshot_analyser(s_snapshot));
                } else {
                    heap_caps_free(copy);
                    snprintf(s_snapshot_reason, sizeof s_snapshot_reason, "%s",
                             open_why[0] ? open_why : "the image is unusable");
                    ESP_LOGE(TAG, "snapshot: %s; queries answer without measurements",
                             s_snapshot_reason);
                }
            }
        }
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
    };
    lib_result_t lr = card.music_dir
                          ? library_open(&library_cfg, &s_library)
                          : library_open_no_card(&library_cfg, card.mount_error, &s_library);
    int64_t scan_ms = (esp_timer_get_time() - scan_t0) / 1000;
    uint32_t psram_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (lr != LIB_OK || !s_library) {
        /* A failed generation must not take the device off the network. The
         * file surface still serves its diagnostic page; the broadcast idles and
         * /api/library.bin is unavailable until the next boot/rescan. */
        ESP_LOGE(TAG, "library: cannot build a generation (%s); /api/library.bin is unavailable"
                      " (scan %lld ms, psram %u -> %u B)",
                 lr == LIB_INVALID_ARG ? "invalid argument" : "out of memory",
                 (long long)scan_ms, (unsigned)psram_before, (unsigned)psram_after);
    } else {
        size_t arena_len = 0;
        (void)library_blob(s_library, &arena_len);
        ESP_LOGI(TAG, "library: state=%s tracks=%u dirs=%u bytes=%llu scan=%lld ms "
                      "arena=%u B psram=%u -> %u B truncated=%d",
                 library_state_name(library_state(s_library)),
                 (unsigned)library_track_count(s_library),
                 (unsigned)library_dir_count(s_library),
                 (unsigned long long)library_total_bytes(s_library), (long long)scan_ms,
                 (unsigned)arena_len, (unsigned)psram_before,
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

    /* 5b. The card state the App asks for. The device serves no page, so this is the
     * only route the front door registers; `/` is the listener's 404. */
    front_door_cfg_t door = {
        .card_present = front_door_card_present,
        .card_mark = front_door_card_mark,
        .rescan_running = app_services_rescan_running,
    };
    e = front_door_start(&door);
    if (e != ESP_OK) return fail(report, e, "front door");

    /* 6. The library endpoint. Registration only; the listener is still ahead. */
    e = library_http_start(NULL);
    if (e != ESP_OK) return fail(report, e, "library endpoint");

    /* 7. The broadcast, now that Wi-Fi's buffers exist. It starts even with no
     * card: the task waits for the directory rather than failing the boot. */
    e = start_station(s_library);
    if (e != ESP_OK) return fail(report, e, "broadcast");
    if (report) report->broadcast_up = true;

    /* The MCP endpoint is registered before the listener, after the broadcast
     * exists because player_status reads its snapshot. */
    mcp_cfg_t mcp = {
        .library = NULL, /* MCP leases the published generation per call */
        .scan_ms = (uint32_t)scan_ms,
        .local_ip = app_board_local_ip,
        .app_origin = K7_APP_ORIGIN,
        .dev_origin = cfg->app_origin_dev,
        .rescan = app_services_rescan,
        .port = cfg->port,
        .snapshot = s_snapshot,
        .snapshot_state = s_snapshot_state,
        .snapshot_reason = s_snapshot_reason,
    };
    e = mcp_start(&mcp);
    if (e != ESP_OK) return fail(report, e, "MCP endpoint");

    /* The Snapshot coverage is walked once here, at boot, against the Library just published:
     * the walk looks up every Track, and doing it on every `library_stats` request would walk
     * the whole Library per reload. */
    mcp_refresh_coverage();

    e = http_server_register_route_method("/api/rescan", "POST", rescan_route, NULL);
    if (e != ESP_OK) return fail(report, e, "rescan endpoint");

    e = covers_http_start();
    if (e != ESP_OK) return fail(report, e, "covers endpoint");

    e = playlist_http_start(current_card_root);
    if (e != ESP_OK) return fail(report, e, "playlist endpoints");

    e = snapshot_http_start();
    if (e != ESP_OK) return fail(report, e, "snapshot endpoint");

    /* The user's K7Art pack: the partition is opened here, before the listener, and the
     * four routes go on the same front door as the rest of the App's API (art-packs.md §5).
     * The factory sheet was handed over by the screen's boot; without one the pack still
     * installs and reports, it just has no factory designs to join. */
    e = app_services_art_start();
    if (e != ESP_OK) return fail(report, e, "art endpoints");

    /* The one page the device serves, and the routes behind it (ADR 0076). */
    e = setup_http_start();
    if (e != ESP_OK) return fail(report, e, "setup page");

    /* CORS preflight for the App's origin. The App fetches this device over plain HTTP
     * from an HTTPS origin, once the `local-network` permission lets the connection
     * through; a JSON POST is what makes the browser ask, and these two empty responses
     * are its answer. A development firmware names the dev origin too (http_server.c). */
    e = http_server_register_route_method("/api/", "OPTIONS", cors_preflight_route, NULL);
    if (e != ESP_OK) return fail(report, e, "CORS preflight");
    e = http_server_register_route_method("/mcp", "OPTIONS", cors_preflight_route, NULL);
    if (e != ESP_OK) return fail(report, e, "CORS preflight");

    /* 8. The listener, last: every route is registered by now. */
    http_server_cfg_t http = {
        .port = cfg->port,
        .net_ip = s_ip_fn,
        .server_name = cfg->server_name ? cfg->server_name : "sd-stream/1.0",
        .max_conns = cfg->max_conns ? cfg->max_conns : 4,
        .cors_origin = K7_APP_ORIGIN,
        .cors_origin_dev = cfg->app_origin_dev,
    };
    e = http_server_start(&http);
    if (e != ESP_OK) return fail(report, e, "HTTP server");
    if (report) report->http_up = true;

    /* One watcher, whichever way the card went at boot: it covers the card that was
     * missing then arriving now, and the card that leaves later. */
    xTaskCreatePinnedToCore(card_watch_task, "card_watch", 4096, NULL, 2, NULL, 0);

    return ESP_OK;
}
