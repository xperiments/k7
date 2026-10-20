#include "mini_files.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "http_server.h"
#include "audio_format.h"
#include "library.h"
#include "mini_icecast.h" /* the broadcast's current title, for the index */

#define TAG "files"

#define ML_CHUNK    16384 /* download read size (R7d: bounded chunks) */
/* The download's steady ceiling. The stream needs 32 KB/s; 16 KB every 12 ms is
 * about 1.3 MB/s, so a 40 MB track still takes half a minute and the radio has
 * a predictable gap between the download's packets instead of a saturating
 * burst. */
#define ML_CHUNK_PAUSE_MS 12

static struct {
    SemaphoreHandle_t lock;   /* guards the card error text below */
    SemaphoreHandle_t dl;     /* taken for the duration of one download (D6) */
    const library_t *lib;     /* the immutable generation the routes read */
    bool  card_mounted;       /* a retry has seen a card since boot */
    char  err[160];           /* the mount error, when there is no card */
    char  station[64];
    uint8_t *buf;             /* one shared download buffer: only one download at a time */
    size_t buf_size;
    bool  buf_external;
} F;

/* ------------------------------------------------------------------ helpers */

static const char *content_type_for(mi_fmt_t f)
{
    switch (f) {
    case MI_FMT_FLAC: return "audio/flac";
    case MI_FMT_WAV:  return "audio/wav";
    default:          return "audio/mpeg";
    }
}

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

/* Percent-encode one path segment at a time, preserving the '/' separators so
 * the href is also readable. A literal '/' inside a segment is impossible: the
 * Library never indexes a name containing one. */
static void w_url_path(writer_t *w, const char *s)
{
    static const char hex[] = "0123456789ABCDEF";
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '/') {
            w_raw(w, "/", 1);
        } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                   (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            w_raw(w, (const char *)s, 1);
        } else {
            char e[3] = {'%', hex[c >> 4], hex[c & 15]};
            w_raw(w, e, 3);
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
        w_puts(&w, "<li><a href=\"/music/");
        w_url_path(&w, rel);
        w_puts(&w, "\">");
        w_escaped(&w, rel);
        w_printf(&w, "</a> <span class=\"n\">%llu B</span></li>",
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

/* ---------------------------------------------------------------- download */

/* Decode the request's whole remainder one segment at a time. Keeping the
 * separators literal means a %2F inside a segment is refused instead of turning
 * into a directory separator the client did not send. */
static bool decode_relpath(const char *raw, char *out, size_t cap)
{
    if (!raw || !raw[0]) return false;
    size_t olen = 0;
    const char *p = raw;
    for (;;) {
        const char *slash = strchr(p, '/');
        size_t rawlen = slash ? (size_t)(slash - p) : strlen(p);
        if (rawlen == 0 || rawlen >= HTTP_PATH_MAX) return false;

        char enc[HTTP_PATH_MAX];
        memcpy(enc, p, rawlen);
        enc[rawlen] = 0;
        char seg[LIB_NAME_MAX + 1];
        size_t n = http_url_decode(enc, seg, sizeof seg);
        if (n == (size_t)-1 || strlen(seg) != n) return false;
        if (n == 0 || n > LIB_NAME_MAX || seg[0] == '.') return false;
        if (strstr(seg, "..")) return false;
        if (strchr(seg, '/') || strchr(seg, '\\')) return false;
        if (olen + (olen ? 1u : 0u) + n >= cap) return false;
        if (olen) out[olen++] = '/';
        memcpy(out + olen, seg, n);
        olen += n;

        if (!slash) break;
        p = slash + 1;
        if (!*p) return false;
    }
    out[olen] = 0;
    return true;
}

static http_result_t download_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    static const char prefix[] = "/music/";
    bool owned = false;
    const library_t *lib = files_acquire(&owned);
    if (!lib) {
        http_send_error(fd, 503, "the library is not available");
        return HTTP_HANDLED;
    }
    lib_state_t state = library_state(lib);
    if (state == LIB_STATE_NO_CARD) {
        http_send_error(fd, 503, "no card is mounted");
        if (owned) library_release(lib);
        return HTTP_HANDLED;
    }
    if (state == LIB_STATE_NO_DIR || state == LIB_STATE_EMPTY) {
        http_send_error(fd, 404, "no such track");
        if (owned) library_release(lib);
        return HTTP_HANDLED;
    }

    const char *raw = req->path + (sizeof prefix - 1);
    char rel[LIB_PATH_MAX];
    if (!decode_relpath(raw, rel, sizeof rel)) {
        ESP_LOGW(TAG, "GET %s -> 400 (malformed path)", req->path);
        http_send_error(fd, 400, "that track path is not accepted");
        if (owned) library_release(lib);
        return HTTP_HANDLED;
    }

    lib_node_t n = library_find(lib, rel);
    if (n == LIB_NODE_NONE || library_kind(lib, n) != LIB_TRACK) {
        ESP_LOGI(TAG, "GET %s -> 404 (not a track in the library)", req->path);
        http_send_error(fd, 404, "no such track");
        if (owned) library_release(lib);
        return HTTP_HANDLED;
    }

    char path[LIB_PATH_MAX];
    if (library_abspath(lib, n, path, sizeof path) == (size_t)-1) {
        ESP_LOGW(TAG, "GET %s -> 404 (absolute path does not fit)", req->path);
        http_send_error(fd, 404, "no such track");
        if (owned) library_release(lib);
        return HTTP_HANDLED;
    }

    /* D6: one download at a time. The broadcast is reading the same card, so a
     * second requester is declined rather than queued behind a 40 MB FLAC. */
    if (xSemaphoreTake(F.dl, 0) != pdTRUE) {
        ESP_LOGW(TAG, "GET %s -> 503 (a download is already running)", req->path);
        http_send_error(fd, 503, "only one download at a time; the live stream has priority");
        if (owned) library_release(lib);
        return HTTP_HANDLED;
    }

    /* The broadcast steps off the card for the duration: sharing it measured
     * 55 KB/s for the download and still made the stream miss deadlines; alone
     * the card gives 250 KB/s. The broadcast plays its idle bed meanwhile and
     * resumes the track where it stopped. */
    mini_icecast_park_card(600000);
    vTaskDelay(pdMS_TO_TICKS(80));
    bool parked_here = true;

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        ESP_LOGI(TAG, "GET %s -> 404 (no such file)", req->path);
        http_send_error(fd, 404, "no such track");
        goto done;
    }
    int f = open(path, O_RDONLY);
    if (f < 0) {
        ESP_LOGW(TAG, "GET %s -> 404 (open: %s)", req->path, strerror(errno));
        http_send_error(fd, 404, "the file cannot be opened");
        goto done;
    }
    if (http_send_headers(fd, 200, content_type_for(library_format(lib, n)),
                          (size_t)st.st_size, NULL) != ESP_OK) {
        close(f);
        goto done;
    }
    /* A download is a bulk transfer on a link that stalls. Wi-Fi now and then
     * holds a send for more than the server-wide 400 ms deadline, which ends a
     * transfer after a few tens of kilobytes; a 26 MB file needs the longer
     * wait. */
    http_send_timeout_ms(fd, 5000);
    int64_t t0 = esp_timer_get_time();
    size_t sent = 0;
    static uint32_t backoff_until;
    backoff_until = xTaskGetTickCount() + pdMS_TO_TICKS(1000);
    for (;;) {
        ssize_t r = read(f, F.buf, F.buf_size);
        if (r < 0) {
            ESP_LOGW(TAG, "read failed after %u B: %s", (unsigned)sent, strerror(errno));
            break;
        }
        if (r == 0) break;
        if (!http_send_all(fd, F.buf, (size_t)r)) {
            ESP_LOGW(TAG, "client stopped reading %s after %u B", rel, (unsigned)sent);
            break;
        }
        sent += (size_t)r;
        /* The size came from stat(), so the last chunk is known: release the
         * download lock at once rather than sleeping after it. */
        if (sent >= (size_t)st.st_size) break;
        /* The lag reading returns to zero as soon as the clock rebases, so a
         * one-frame reading would end the backoff before the next read is safe. */
        uint32_t now_ticks = xTaskGetTickCount();
        if (mini_icecast_lag_ms() > 250) backoff_until = now_ticks + pdMS_TO_TICKS(2000);
        bool slow = (int32_t)(backoff_until - now_ticks) > 0;
        if (!parked_here) vTaskDelay(pdMS_TO_TICKS(slow ? 100 : ML_CHUNK_PAUSE_MS));
    }
    close(f);
    int64_t ms = (esp_timer_get_time() - t0) / 1000;
    ESP_LOGI(TAG, "GET %s -> %u/%u B in %d ms", rel, (unsigned)sent,
             (unsigned)st.st_size, (int)ms);

done:
    if (parked_here) mini_icecast_unpark_card();
    xSemaphoreGive(F.dl);
    if (owned) library_release(lib);
    return HTTP_HANDLED;
}

/* -------------------------------------------------------------------- API */

bool mini_files_card_lock(uint32_t timeout_ms)
{
    return xSemaphoreTake(F.dl, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void mini_files_card_unlock(void)
{
    xSemaphoreGive(F.dl);
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
    F.dl = xSemaphoreCreateMutex();
    if (!F.lock || !F.dl) return ESP_ERR_NO_MEM;
    F.lib = cfg->library;
    snprintf(F.station, sizeof F.station, "%s", cfg->station_name ? cfg->station_name : "ESP32 Radio");
    mini_files_set_card(NULL, cfg->mount_error);

    /* One shared read buffer, because only one download runs at a time. */
    F.buf_size = ML_CHUNK;
    F.buf = heap_caps_malloc(F.buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    F.buf_external = F.buf != NULL;
    if (!F.buf) F.buf = malloc(F.buf_size);
    if (!F.buf) {
        ESP_LOGE(TAG, "out of memory for the %u B download buffer", (unsigned)F.buf_size);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "download buffer: %u B in %s", (unsigned)F.buf_size,
             F.buf_external ? "external memory" : "internal RAM (fallback)");

    esp_err_t err = http_server_register_route("/music/", download_route, NULL);
    if (err == ESP_OK) err = http_server_register_route("/", index_route, NULL);
    if (err != ESP_OK) ESP_LOGE(TAG, "cannot register the index/download routes: %s", esp_err_to_name(err));
    return err;
}
