#include "mini_files.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
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
#include "mi_decode.h"     /* the authority on which source formats exist */
#include "mini_icecast.h"  /* the broadcast's current title, for the index */
#include "music_dir.h"     /* the music directory's rules, in one place */

#define TAG "files"

#define ML_PATH_MAX MUSIC_PATH_MAX /* the card state's directory buffer */
#define ML_CHUNK    16384 /* download read size (R7d: bounded chunks) */
/* The download's steady ceiling. The stream needs 32 KB/s; 16 KB every 12 ms is
 * about 1.3 MB/s, so a 40 MB track still takes half a minute and the radio has a
 * predictable gap between the download's packets instead of a saturating burst.
 * This is the part the lag-driven backoff cannot do: it reacts after the stream
 * has already been pushed behind. */
#define ML_CHUNK_PAUSE_MS 12

static struct {
    SemaphoreHandle_t lock;   /* guards the card state below */
    SemaphoreHandle_t dl;     /* taken for the duration of one download (D6) */
    char  dir[ML_PATH_MAX];
    char  err[160];
    char  station[64];
    uint8_t *buf;             /* one shared download buffer: only one download at a time */
    size_t buf_size;
    bool  buf_external;
} F;

/* ------------------------------------------------------------------ helpers */

static const char *content_type_for(const char *name)
{
    switch (mi_dec_format_of(name)) {
    case MI_FMT_FLAC: return "audio/flac";
    case MI_FMT_WAV:  return "audio/wav";
    default:          return "audio/mpeg";
    }
}

/* A bounded output buffer: the index can be any size, memory use is not. */
typedef struct {
    int    fd;
    char   buf[1024]; /* bounded on purpose: this is the deepest frame in the
                       * connection task's stack budget (see http_server.c) */
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

static void w_url_encoded(writer_t *w, const char *s)
{
    static const char hex[] = "0123456789ABCDEF";
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            w_raw(w, (const char *)s, 1);
        } else {
            char e[3] = {'%', hex[c >> 4], hex[c & 15]};
            w_raw(w, e, 3);
        }
    }
}

static void snapshot(char *dir, size_t dn, char *err, size_t en)
{
    xSemaphoreTake(F.lock, portMAX_DELAY);
    if (dir && dn) snprintf(dir, dn, "%s", F.dir);
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

/* The index doubles as the diagnostic surface (I3). There are two distinct
 * failures and they need different fixes, so they must not share a heading: a
 * card that is not there, and a card that is there but holds no music directory.
 * Reporting the second as the first sends the reader looking for the wrong
 * problem. */
static void send_library_problem(int fd, const char *heading, const char *detail, const char *dir)
{
    http_send_headers(fd, 200, "text/html; charset=utf-8", (size_t)-1, NULL);
    writer_t w = {fd, {0}, 0, true};
    send_page_start(&w, heading);
    w_puts(&w, "<h1>");
    w_escaped(&w, heading);
    w_puts(&w, "</h1><p>");
    w_escaped(&w, detail);
    w_puts(&w, "</p>");
    if (dir && dir[0]) {
        w_puts(&w, "<p>Expected music directory: <code>");
        w_escaped(&w, dir);
        w_puts(&w, "</code></p>");
    }
    w_puts(&w, "<p>The device is on the network and reachable - which is the point of "
               "this page. A card inserted now is mounted automatically, and the library "
               "appears without a reboot.</p>");
    w_puts(&w, "<p><a href=\"/stream\">Listen to the live stream</a></p>");
    w_flush(&w);
}

/* ------------------------------------------------------------------- index */

/* The index's rendering of one accepted track. music_dir_list owns which names
 * get here; this owns the markup. */
typedef struct {
    writer_t *w;
    int       listed;
} index_ctx_t;

static bool index_entry(void *ctx, const char *name, unsigned long long size)
{
    index_ctx_t *c = ctx;
    writer_t *w = c->w;
    w_puts(w, "<li><a href=\"/music/");
    w_url_encoded(w, name);
    w_puts(w, "\">");
    w_escaped(w, name);
    w_printf(w, "</a> <span class=\"n\">%llu B</span></li>", size);
    c->listed++;
    return w->ok;
}

static http_result_t index_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    /* Registered as the catch-all "/", but it only answers the root: every other
     * path is a 404, not an index. */
    if (strcmp(req->path, "/") != 0) {
        ESP_LOGI(TAG, "GET %s -> 404", req->path);
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    char dir[ML_PATH_MAX], err[sizeof F.err];
    snapshot(dir, sizeof dir, err, sizeof err);

    if (!dir[0]) {
        char detail[320];
        snprintf(detail, sizeof detail,
                 "No card is mounted, so there is no audio library to serve. Mount failed: %.120s",
                 err[0] ? err : "unknown error");
        ESP_LOGI(TAG, "GET / -> no card (%s)", err[0] ? err : "not mounted");
        send_library_problem(fd, "No card mounted", detail, NULL);
        return HTTP_HANDLED;
    }

    /* Probe first: the two failures need different pages, and the walk below
     * happens after the headers, when it is too late to choose one. */
    DIR *probe = opendir(dir);
    if (!probe) {
        char detail[384];
        snprintf(detail, sizeof detail,
                 "The card is mounted, but %.120s could not be opened: %.80s. Create it and put "
                 "your tracks in it.",
                 dir, strerror(errno));
        ESP_LOGW(TAG, "GET / -> cannot read %s: %s", dir, strerror(errno));
        send_library_problem(fd, "No music directory", detail, dir);
        return HTTP_HANDLED;
    }
    closedir(probe);

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
    w_puts(&w, "<ul>");

    index_ctx_t listing = {&w, 0};
    int walked = music_dir_list(dir, index_entry, &listing);
    if (walked < 0) {
        /* It was readable a moment ago; say so rather than send a page that
         * silently claims there is no music. */
        w_puts(&w, "<li>The music directory became unreadable while listing it.</li>");
    }
    w_puts(&w, "</ul>");
    if (!listing.listed && walked >= 0)
        w_puts(&w, "<p>The music directory contains no .mp3, .flac or .wav files.</p>");
    w_flush(&w);
    ESP_LOGI(TAG, "GET / -> %d track(s)", listing.listed);
    return HTTP_HANDLED;
}

/* ---------------------------------------------------------------- download */

static http_result_t download_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    static const char prefix[] = "/music/";
    const char *raw = req->path + (sizeof prefix - 1);

    char name[MUSIC_NAME_MAX + 1];
    size_t name_len = http_url_decode(raw, name, sizeof name);
    if (name_len == (size_t)-1) {
        ESP_LOGW(TAG, "GET %s -> 400 (undecodable or over-long name)", req->path);
        http_send_error(fd, 400, "the file name is malformed or too long");
        return HTTP_HANDLED;
    }
    if (strlen(name) != name_len) {
        /* A %00 in the middle would otherwise silently truncate the name and
         * serve a different file than the one asked for. */
        ESP_LOGW(TAG, "GET %s -> 400 (embedded NUL in the name)", req->path);
        http_send_error(fd, 400, "that file name is not accepted");
        return HTTP_HANDLED;
    }

    char dir[ML_PATH_MAX];
    snapshot(dir, sizeof dir, NULL, 0);

    /* D5: one rule decides the name, the join and the containment, so this route
     * cannot be more permissive than the index that links here. */
    char path[MUSIC_PATH_MAX], why[MUSIC_WHY_MAX];
    switch (music_resolve(dir, name, path, sizeof path, why, sizeof why)) {
    case MUSIC_OK:
        break;
    case MUSIC_NO_CARD:
        http_send_error(fd, 503, "no card is mounted");
        return HTTP_HANDLED;
    case MUSIC_UNSUPPORTED:
        ESP_LOGI(TAG, "GET %s -> 404 (not an audio file)", req->path);
        http_send_error(fd, 404, "only .mp3, .flac and .wav are served");
        return HTTP_HANDLED;
    default:
        ESP_LOGW(TAG, "GET %s -> 400 (%s)", req->path, why);
        http_send_error(fd, 400, "that file name is not accepted");
        return HTTP_HANDLED;
    }

    /* D6: one download at a time. The broadcast is reading the same card, so a
     * second requester is declined rather than queued behind a 40 MB FLAC. */
    if (xSemaphoreTake(F.dl, 0) != pdTRUE) {
        ESP_LOGW(TAG, "GET %s -> 503 (a download is already running)", req->path);
        http_send_error(fd, 503, "only one download at a time; the live stream has priority");
        return HTTP_HANDLED;
    }

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        ESP_LOGI(TAG, "GET %s -> 404 (no such file)", req->path);
        http_send_error(fd, 404, "no such file in the music directory");
        goto done;
    }
    int f = open(path, O_RDONLY);
    if (f < 0) {
        ESP_LOGW(TAG, "GET %s -> 404 (open: %s)", req->path, strerror(errno));
        http_send_error(fd, 404, "the file cannot be opened");
        goto done;
    }
    if (http_send_headers(fd, 200, content_type_for(name), (size_t)st.st_size, NULL) != ESP_OK) {
        close(f);
        goto done;
    }
    int64_t t0 = esp_timer_get_time();
    size_t sent = 0;
    /* Start gently and stay gentle for a while after the broadcast reports it is
     * behind: the lag reading returns to zero as soon as the clock rebases, so a
     * one-frame reading would end the backoff before the next read is safe. */
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
            ESP_LOGW(TAG, "client stopped reading %s after %u B", name, (unsigned)sent);
            break;
        }
        sent += (size_t)r;
        /* The size came from stat(), so the last chunk is known: release the
         * download lock at once rather than sleeping after it. The next request
         * can arrive the moment the client has its last byte, and a 20 ms sleep
         * here made that request a 503. */
        if (sent >= (size_t)st.st_size) break;
        /* The steady ceiling, before the lag-driven backoff below. */
        vTaskDelay(pdMS_TO_TICKS(ML_CHUNK_PAUSE_MS));
        /* The live stream has priority on the one SD bus. Yield between chunks so
         * the broadcast's decoder read can take it, and back off much harder for
         * two seconds after the broadcast says it is behind: a fixed slice was
         * measured helping and then not, because the contention arrives in bursts
         * (card latency, Wi-Fi) rather than as a steady share. */
        uint32_t now_ticks = xTaskGetTickCount();
        if (mini_icecast_lag_ms() > 250) backoff_until = now_ticks + pdMS_TO_TICKS(2000);
        bool slow = (int32_t)(backoff_until - now_ticks) > 0;
        vTaskDelay(pdMS_TO_TICKS(slow ? 20 : 1));
    }
    close(f);
    int64_t ms = (esp_timer_get_time() - t0) / 1000;
    ESP_LOGI(TAG, "GET %s -> %u/%u B in %d ms", name, (unsigned)sent, (unsigned)st.st_size, (int)ms);

done:
    xSemaphoreGive(F.dl);
    return HTTP_HANDLED;
}

/* -------------------------------------------------------------------- API */

void mini_files_set_card(const char *music_dir, const char *mount_error)
{
    xSemaphoreTake(F.lock, portMAX_DELAY);
    snprintf(F.dir, sizeof F.dir, "%s", music_dir ? music_dir : "");
    snprintf(F.err, sizeof F.err, "%s", mount_error ? mount_error : "");
    xSemaphoreGive(F.lock);
}

esp_err_t mini_files_start(const mini_files_cfg_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;

    F.lock = xSemaphoreCreateMutex();
    F.dl = xSemaphoreCreateMutex();
    if (!F.lock || !F.dl) return ESP_ERR_NO_MEM;
    snprintf(F.station, sizeof F.station, "%s", cfg->station_name ? cfg->station_name : "ESP32 Radio");
    mini_files_set_card(cfg->music_dir, cfg->mount_error);

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
