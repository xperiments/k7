#include "mini_files.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "http_server.h"
#include "library.h"
#include "mini_icecast.h" /* the broadcast's current title, for the index */

#define TAG "files"

static struct {
    SemaphoreHandle_t lock;   /* guards the card error text below */
    SemaphoreHandle_t card;   /* held while a scan owns the card */
    const library_t *lib;     /* the immutable generation the index reads */
    bool  card_mounted;       /* a retry has seen a card since boot */
    char  err[160];           /* the mount error, when there is no card */
    char  station[64];
} F;

/* ------------------------------------------------------------------ helpers */

/* A bounded output buffer: the index can be any size, memory use is not. */
typedef struct {
    int    fd;
    char   buf[1024];
    size_t n;
    bool   ok;
} writer_t;

static void w_flush(writer_t *w)
{
    if (w->n && w->ok && !http_send_all(w->fd, w->buf, w->n)) w->ok = false;
    w->n = 0;
}

static void w_raw(writer_t *w, const char *s, size_t n)
{
    while (w->ok && n) {
        size_t room = sizeof w->buf - w->n;
        if (!room) {
            w_flush(w);
            room = sizeof w->buf;
        }
        size_t k = n < room ? n : room;
        memcpy(w->buf + w->n, s, k);
        w->n += k;
        s += k;
        n -= k;
    }
}

static void w_puts(writer_t *w, const char *s) { w_raw(w, s, strlen(s)); }

static void w_printf(writer_t *w, const char *fmt, ...)
{
    char t[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(t, sizeof t, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof t) n = (int)sizeof t - 1;
    w_raw(w, t, (size_t)n);
}

static void w_escaped(writer_t *w, const char *s)
{
    for (; *s; s++) {
        switch (*s) {
        case '&':  w_puts(w, "&amp;"); break;
        case '<':  w_puts(w, "&lt;"); break;
        case '>':  w_puts(w, "&gt;"); break;
        case '"':  w_puts(w, "&quot;"); break;
        case '\'': w_puts(w, "&#39;"); break;
        default:   w_raw(w, s, 1); break;
        }
    }
}

static void snapshot_card(bool *mounted, char *err, size_t en)
{
    xSemaphoreTake(F.lock, portMAX_DELAY);
    if (mounted) *mounted = F.card_mounted;
    if (err && en) snprintf(err, en, "%s", F.err);
    xSemaphoreGive(F.lock);
}

static void send_page_start(writer_t *w, const char *title)
{
    w_puts(w, "<!doctype html><meta charset=\"utf-8\">");
    w_puts(w, "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
    w_printf(w, "<title>%s</title>", title);
    w_puts(w, "<style>body{font:16px system-ui,sans-serif;margin:2rem;max-width:44rem}"
               "li{margin:.35rem 0}code{background:#eee;padding:.1rem .3rem}"
               ".n{color:#666;font-variant-numeric:tabular-nums}</style>");
}

/* The index doubles as the diagnostic surface. No card and no music directory
 * are different faults and must not share a heading. */
static void send_library_problem(int fd, const char *heading, const char *detail)
{
    http_send_headers(fd, 200, "text/html; charset=utf-8", (size_t)-1, NULL);
    writer_t w = {fd, {0}, 0, true};
    send_page_start(&w, heading);
    w_puts(&w, "<h1>");
    w_escaped(&w, heading);
    w_puts(&w, "</h1><p>");
    w_escaped(&w, detail);
    w_puts(&w, "</p>");
    w_puts(&w, "<p>The device is on the network and reachable - which is the point of "
               "this page. The Library is rebuilt at boot and by a rescan.</p>");
    w_puts(&w, "<p><a href=\"/stream\">Listen to the live stream</a></p>");
    w_flush(&w);
}

/* ------------------------------------------------------------------- index */

static const library_t *files_acquire(bool *owned)
{
    if (F.lib) { *owned = false; return F.lib; }
    *owned = true;
    return library_acquire();
}

static http_result_t index_with_lib(int fd, const http_request_t *req, const library_t *lib)
{
    if (strcmp(req->path, "/") != 0) {
        ESP_LOGI(TAG, "GET %s -> 404", req->path);
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    lib_state_t state = library_state(lib);
    if (state == LIB_STATE_NO_CARD) {
        bool card_mounted = false;
        char err[sizeof F.err];
        snapshot_card(&card_mounted, err, sizeof err);
        if (card_mounted) {
            ESP_LOGI(TAG, "GET / -> card mounted, library generation is still the boot one");
            send_library_problem(fd, "Library not rebuilt",
                                 "A card is mounted, but the Library generation is still the "
                                 "boot-time no-card one. The rescan path replaces it.");
        } else {
            if (!err[0]) snprintf(err, sizeof err, "%s", library_error(lib));
            ESP_LOGI(TAG, "GET / -> no card (%s)", err[0] ? err : "not mounted");
            send_library_problem(fd, "No card mounted",
                                 err[0] ? err : "No card is mounted, so there is no audio library.");
        }
        return HTTP_HANDLED;
    }
    if (state == LIB_STATE_NO_DIR) {
        char detail[256];
        snprintf(detail, sizeof detail, "%.200s. Create it and put your tracks there.",
                 library_error(lib));
        ESP_LOGW(TAG, "GET / -> no music directory (%s)", library_error(lib));
        send_library_problem(fd, "No music directory", detail);
        return HTTP_HANDLED;
    }
    if (state == LIB_STATE_ERROR) {
        send_library_problem(fd, "Library error", library_error(lib));
        return HTTP_HANDLED;
    }

    char title[121];
    mini_icecast_now_playing(title, sizeof title);

    http_send_headers(fd, 200, "text/html; charset=utf-8", (size_t)-1, NULL);
    writer_t w = {fd, {0}, 0, true};
    send_page_start(&w, F.station);
    w_puts(&w, "<h1>");
    w_escaped(&w, F.station);
    w_puts(&w, " - music library</h1>");
    w_puts(&w, "<p>Now playing: <strong>");
    w_escaped(&w, title);
    w_puts(&w, "</strong> &middot; <a href=\"/stream\">live stream (MP3)</a></p>");

    uint32_t count = library_track_count(lib);
    if (library_truncated(lib)) {
        w_printf(&w, "<p>The library is truncated at %u tracks.</p>", (unsigned)count);
    }
    w_puts(&w, "<ul>");
    uint32_t listed = 0;
    for (uint32_t i = 0; i < count && w.ok; i++) {
        lib_node_t n = library_track_at(lib, i);
        if (n == LIB_NODE_NONE) continue;
        char rel[LIB_PATH_MAX];
        size_t rlen = library_relpath(lib, n, rel, sizeof rel);
        if (rlen == (size_t)-1) continue;
        /* A listing, not a browser: the row names the Track and its size, and
         * there is no href because the device serves no Track. */
        w_puts(&w, "<li>");
        w_escaped(&w, rel);
        w_printf(&w, " <span class=\"n\">%llu B</span></li>",
                 (unsigned long long)library_size(lib, n));
        listed++;
    }
    w_puts(&w, "</ul>");
    if (!listed) w_puts(&w, "<p>The music directory contains no tracks.</p>");
    w_flush(&w);
    ESP_LOGI(TAG, "GET / -> %u track(s)", (unsigned)listed);
    return HTTP_HANDLED;
}

static http_result_t index_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    bool owned = false;
    const library_t *lib = files_acquire(&owned);
    if (!lib) {
        ESP_LOGE(TAG, "GET / -> no library generation");
        send_library_problem(fd, "No library generation",
                             "The Library was not built at boot; the serial log says why.");
        return HTTP_HANDLED;
    }
    http_result_t r = index_with_lib(fd, req, lib);
    if (owned) library_release(lib);
    return r;
}

/* -------------------------------------------------------------------- API */

bool mini_files_card_lock(uint32_t timeout_ms)
{
    return xSemaphoreTake(F.card, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void mini_files_card_unlock(void)
{
    xSemaphoreGive(F.card);
}

void mini_files_set_card(const char *music_dir, const char *mount_error)
{
    xSemaphoreTake(F.lock, portMAX_DELAY);
    F.card_mounted = music_dir && music_dir[0];
    snprintf(F.err, sizeof F.err, "%s", mount_error ? mount_error : "");
    xSemaphoreGive(F.lock);
}

esp_err_t mini_files_start(const mini_files_cfg_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;

    F.lock = xSemaphoreCreateMutex();
    F.card = xSemaphoreCreateMutex();
    if (!F.lock || !F.card) return ESP_ERR_NO_MEM;
    F.lib = cfg->library;
    snprintf(F.station, sizeof F.station, "%s", cfg->station_name ? cfg->station_name : "ESP32 Radio");
    mini_files_set_card(NULL, cfg->mount_error);

    /* The index is the only route: the device serves no Track and accepts no
     * upload (the global premise in docs/specs/CONTEXT.md). */
    esp_err_t err = http_server_register_route("/", index_route, NULL);
    if (err != ESP_OK) ESP_LOGE(TAG, "cannot register the index route: %s", esp_err_to_name(err));
    return err;
}
