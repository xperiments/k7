/*
 * mini_icecast - tiny live stream server for ESP-IDF.
 *
 * Decodes .mp3 / .flac / .wav at any sample rate, resamples everything to one
 * station format and encodes it to MP3, then broadcasts that to every connected
 * client in real time. Track changes are seamless because the stream format
 * never changes.
 *
 * The listening socket and the request parsing live in http_server; this file is
 * the audio producer plus the client fan-out, and it plugs into the HTTP layer
 * by registering one route.
 */
#include "mini_icecast.h"

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "http_server.h"
#include "lwip/sockets.h"
#include "mi_decode.h"
#include "mi_encode.h"
#include "mi_resample.h"

#define MAX_CLIENTS     MINI_ICECAST_MAX_CLIENTS
#define TITLE_MAX       120
#define CHUNK_FRAMES    MINI_ICECAST_CHUNK_FRAMES
#define CHUNK_BYTES     (CHUNK_FRAMES * 4)
#define RADIO_TASK_STACK (24 * 1024)     /* the FLAC/MP3 decoder scratch is the big one */
#define TELEMETRY_TASK_STACK 4096        /* %f formatting on newlib is stack-hungry */
#define TELEMETRY_PERIOD_MS 60000
#define QUIET_PASSES    30 /* log "no playable files" once a minute, not once a loop */

static const char *TAG = "icecast";

typedef struct {
    int      fd;
    bool     reserved;    /* claimed by a connecting client, not yet on the air */
    bool     meta;        /* client asked for ICY metadata */
    uint32_t since_meta;  /* encoded bytes sent since the last metadata block */
    uint32_t title_ver;   /* last title version this client has received */
} client_t;

static struct {
    mini_icecast_cfg_t cfg;
    client_t           clients[MAX_CLIENTS];
    SemaphoreHandle_t  lock;
    char               title[TITLE_MAX + 1];
    uint32_t           title_ver;
    volatile bool      skip;
    int64_t            t0;            /* pacing clock origin (us) */
    int64_t            played_frames; /* encoded frames broadcast since t0 */
    int16_t            chunk[CHUNK_FRAMES * 2]; /* output chunker, persists across tracks */
    size_t             chunk_n;
    uint8_t           *ring;          /* burst-on-connect history: records of [len16 LE][frame] */
    size_t             ring_size, ring_head, ring_tail, ring_used;
    bool               ring_external;
    mi_enc_t          *enc;
    uint32_t           rebases;       /* starvation rebases: 0 over a healthy soak */
} S;

/* ------------------------------------------------------------- clients */

static bool send_meta(client_t *c)
{
    if (c->title_ver == S.title_ver) {
        uint8_t zero = 0;
        return http_send_all(c->fd, &zero, 1);
    }
    char tmp[TITLE_MAX + 24];
    int len = snprintf(tmp, sizeof tmp, "StreamTitle='%s';", S.title);
    int blocks = (len + 15) / 16; /* the length byte counts 16-byte blocks */
    uint8_t buf[1 + 16 * 10];
    memset(buf, 0, 1 + blocks * 16);
    buf[0] = (uint8_t)blocks;
    memcpy(buf + 1, tmp, len);
    c->title_ver = S.title_ver;
    return http_send_all(c->fd, buf, 1 + blocks * 16);
}

/* Sends audio, splitting it so a metadata block lands exactly every `metaint`
 * *encoded* bytes. */
static bool client_push(client_t *c, const uint8_t *d, size_t n)
{
    if (!c->meta) return http_send_all(c->fd, d, n);
    while (n) {
        size_t k = S.cfg.metaint - c->since_meta;
        if (k > n) k = n;
        if (!http_send_all(c->fd, d, k)) return false;
        d += k;
        n -= k;
        c->since_meta += k;
        if (c->since_meta == S.cfg.metaint) {
            c->since_meta = 0;
            if (!send_meta(c)) return false;
        }
    }
    return true;
}

/* ---- burst-on-connect history: the last ~burst_ms of whole encoded frames ---- */

static void ring_peek(size_t pos, uint8_t *out, size_t n)
{
    size_t first = S.ring_size - pos;
    if (first > n) first = n;
    memcpy(out, S.ring + pos, first);
    memcpy(out + first, S.ring, n - first);
}

static void ring_drop_oldest(void)
{
    uint8_t lb[2];
    ring_peek(S.ring_tail, lb, 2);
    size_t n = 2 + (size_t)(lb[0] | (lb[1] << 8));
    S.ring_tail = (S.ring_tail + n) % S.ring_size;
    S.ring_used -= n;
}

static void ring_put(const uint8_t *d, size_t n) /* lock held */
{
    while (S.ring_used + n + 2 > S.ring_size) ring_drop_oldest();
    uint8_t lb[2] = {(uint8_t)(n & 0xFF), (uint8_t)(n >> 8)};
    for (int part = 0; part < 2; part++) {
        const uint8_t *src = part ? d : lb;
        size_t len = part ? n : 2, first = S.ring_size - S.ring_head;
        if (first > len) first = len;
        memcpy(S.ring + S.ring_head, src, first);
        memcpy(S.ring, src + first, len - first);
        S.ring_head = (S.ring_head + len) % S.ring_size;
        S.ring_used += len;
    }
}

static bool ring_burst(client_t *c) /* lock held */
{
    static uint8_t tmp[CHUNK_BYTES > MI_ENC_MAX_FRAME_BYTES ? CHUNK_BYTES : MI_ENC_MAX_FRAME_BYTES];
    size_t pos = S.ring_tail, left = S.ring_used;
    while (left) {
        uint8_t lb[2];
        ring_peek(pos, lb, 2);
        size_t n = (size_t)(lb[0] | (lb[1] << 8));
        if (n > sizeof tmp) break; /* corrupt record: stop rather than overrun */
        ring_peek((pos + 2) % S.ring_size, tmp, n);
        if (!client_push(c, tmp, n)) return false; /* goes through ICY splitting too */
        pos = (pos + 2 + n) % S.ring_size;
        left -= 2 + n;
    }
    return true;
}

static void broadcast(const uint8_t *d, size_t n)
{
    xSemaphoreTake(S.lock, portMAX_DELAY);
    ring_put(d, n);
    for (int i = 0; i < S.cfg.max_clients; i++) {
        client_t *c = &S.clients[i];
        if (c->fd < 0) continue;
        if (!client_push(c, d, n)) {
            ESP_LOGI(TAG, "client %d gone", i);
            close(c->fd);
            c->fd = -1;
        }
    }
    xSemaphoreGive(S.lock);
}

/* ------------------------------------------------------------ HTTP route */

static http_result_t stream_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    switch (mini_icecast_register_client(fd, req->want_icy_metadata)) {
    case MINI_ICECAST_REGISTERED:
        return HTTP_UPGRADED; /* the broadcast owns the fd now; do not close it */
    case MINI_ICECAST_REGISTER_FULL:
        ESP_LOGW(TAG, "refusing a client: all %u slots are taken", (unsigned)S.cfg.max_clients);
        http_send_error(fd, 503, "every stream slot is taken; try again in a moment");
        return HTTP_HANDLED;
    default:
        return HTTP_HANDLED; /* headers or burst failed: the connection is already dead */
    }
}

mini_icecast_register_result_t mini_icecast_register_client(int fd, bool want_icy_metadata)
{
    /* Reserve a slot under the lock, so two simultaneous connects cannot claim
     * the same one. A reserved slot still has fd < 0, so broadcast() skips it. */
    xSemaphoreTake(S.lock, portMAX_DELAY);
    int slot = -1;
    for (int i = 0; i < S.cfg.max_clients; i++)
        if (S.clients[i].fd < 0 && !S.clients[i].reserved) { slot = i; break; }
    if (slot < 0) {
        xSemaphoreGive(S.lock);
        return MINI_ICECAST_REGISTER_FULL;
    }
    client_t *c = &S.clients[slot];
    c->reserved = true;
    c->meta = want_icy_metadata;
    c->since_meta = 0;
    c->title_ver = 0; /* forces the current title into the first metadata block */
    xSemaphoreGive(S.lock);

    /* Headers first: MP3 needs no container header at all, and the station name
     * goes out in canonical Icecast form - no space after the colon (defect D2). */
    char h[256];
    int n = snprintf(h, sizeof h,
                     "HTTP/1.0 200 OK\r\nContent-Type: audio/mpeg\r\n"
                     "icy-name:%s\r\nCache-Control: no-cache\r\nConnection: close\r\n",
                     S.cfg.station_name);
    if (want_icy_metadata) n += snprintf(h + n, sizeof h - n, "icy-metaint: %u\r\n", (unsigned)S.cfg.metaint);
    n += snprintf(h + n, sizeof h - n, "\r\n");
    bool sent = n > 0 && n < (int)sizeof h && http_send_all(fd, h, (size_t)n);
    if (!sent) {
        xSemaphoreTake(S.lock, portMAX_DELAY);
        c->reserved = false;
        xSemaphoreGive(S.lock);
        return MINI_ICECAST_REGISTER_FAILED;
    }

    /* Register + burst under the lock broadcast() uses: the client gets the
     * history and then the next live frame, with no gap and no duplicate. */
    struct timeval burst_to = {.tv_sec = 1};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &burst_to, sizeof burst_to);
    xSemaphoreTake(S.lock, portMAX_DELAY);
    c->fd = fd;
    bool ok = ring_burst(c);
    size_t burst = S.ring_used;
    if (!ok) c->fd = -1;
    c->reserved = false;
    xSemaphoreGive(S.lock);
    if (!ok) return MINI_ICECAST_REGISTER_FAILED;

    struct timeval snd = {.tv_sec = HTTP_CLIENT_STALL_MS / 1000,
                          .tv_usec = (HTTP_CLIENT_STALL_MS % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &snd, sizeof snd);
    ESP_LOGI(TAG, "client %d connected (icy=%d, burst=%u B)", slot, want_icy_metadata, (unsigned)burst);
    return MINI_ICECAST_REGISTERED;
}

/* ------------------------------------------------------------ broadcaster */

static void set_title(const char *name, size_t n)
{
    if (n > TITLE_MAX) n = TITLE_MAX;
    xSemaphoreTake(S.lock, portMAX_DELAY);
    for (size_t i = 0; i < n; i++) S.title[i] = (name[i] == '\'') ? ' ' : name[i];
    S.title[n] = 0;
    S.title_ver++;
    xSemaphoreGive(S.lock);
}

/* Real-time pacing driven by the number of encoded frames sent (sample-exact):
 * one call per encoder frame, which is the unit actually transmitted. */
static void pace(uint32_t frames)
{
    S.played_frames += frames;
    int64_t played_us = S.played_frames * 1000000 / S.cfg.out_hz;
    int64_t now = esp_timer_get_time() - S.t0;
    if (now - played_us > 1000000) { /* we starved (no files, card hiccup): rebase the clock */
        S.rebases++;
        ESP_LOGW(TAG, "starved for %d ms, rebasing the broadcast clock (#%" PRIu32 ")",
                 (int)((now - played_us) / 1000), S.rebases);
        S.t0 += now - played_us;
        now = played_us;
    }
    int64_t ahead = played_us - now - (int64_t)S.cfg.lead_ms * 1000;
    if (ahead > 0) vTaskDelay(pdMS_TO_TICKS(ahead / 1000)); /* CONFIG_FREERTOS_HZ=1000 */
}

/* One complete MP3 frame from the encoder: onto the wire, then pace. */
static void on_frame(const uint8_t *frame, size_t bytes, void *ctx)
{
    (void)ctx;
    broadcast(frame, bytes);
    pace((uint32_t)mi_enc_samples_per_frame(S.enc));
}

static void emit_chunk(void)
{
    if (S.cfg.pcm_cb) S.cfg.pcm_cb(S.chunk, CHUNK_FRAMES, S.cfg.pcm_cb_ctx);
    mi_enc_feed(S.enc, S.chunk, CHUNK_FRAMES, on_frame, NULL);
}

/* Resampler output -> fixed-size chunks. Lives across tracks, so chunk
 * boundaries never shift. */
static void chunk_append(const int16_t *s, size_t frames, void *ctx)
{
    (void)ctx;
    while (frames) {
        size_t room = CHUNK_FRAMES - S.chunk_n, k = frames < room ? frames : room;
        memcpy(S.chunk + 2 * S.chunk_n, s, k * 4);
        S.chunk_n += k;
        s += 2 * k;
        frames -= k;
        if (S.chunk_n == CHUNK_FRAMES) {
            emit_chunk();
            S.chunk_n = 0;
        }
    }
}

/* End of track (or skip): hand over the partial chunk, complete the encoder's
 * partial frame with silence, and emit every frame that completes. Without this
 * the tail of every track - up to 26 ms - would be lost. */
static void flush_pipeline(void)
{
    if (S.chunk_n) {
        mi_enc_feed(S.enc, S.chunk, S.chunk_n, on_frame, NULL);
        S.chunk_n = 0;
    }
    mi_enc_flush(S.enc, on_frame, NULL);
    size_t desync = mi_enc_desync_bytes(S.enc);
    if (desync) ESP_LOGW(TAG, "encoder re-framing skipped %u bytes (expected 0)", (unsigned)desync);
}

static void stream_file(const char *path)
{
    mi_dec_t *d = mi_dec_open(path);
    if (!d) {
        ESP_LOGW(TAG, "cannot decode %s (unsupported or corrupt): skipping", path);
        return;
    }
    char rs_err[160];
    mi_rs_t *rs = mi_rs_new(mi_dec_hz(d), S.cfg.out_hz, rs_err, sizeof rs_err);
    if (!rs) {
        ESP_LOGE(TAG, "cannot resample %s: %s", path, rs_err);
        mi_dec_close(d);
        return;
    }
    ESP_LOGI(TAG, "now playing: %s (%u Hz -> %u Hz)", S.title, (unsigned)mi_dec_hz(d), (unsigned)S.cfg.out_hz);

    static int16_t in[MI_READ_FRAMES * 2];
    size_t n;
    while (!S.skip && (n = mi_dec_read(d, in, MI_READ_FRAMES)) > 0)
        mi_rs_process(rs, in, n, chunk_append, NULL);

    flush_pipeline();
    mi_rs_free(rs);
    mi_dec_close(d);
}

static bool is_audio(const char *name)
{
    if (name[0] == '.') return false; /* macOS "._foo.mp3" AppleDouble files on the SD */
    return mi_dec_format_of(name) != MI_FMT_NONE;
}

static void radio_task(void *arg)
{
    (void)arg;
    S.t0 = esp_timer_get_time();
    int quiet = 0;
    bool reported_missing = false;
    for (;;) {
        DIR *d = opendir(S.cfg.music_dir);
        int played = 0;
        struct dirent *e;
        if (!d && !reported_missing) {
            /* Distinguish "the directory is not there" from "it is there and has
             * nothing playable in it": they need different fixes. */
            ESP_LOGE(TAG, "cannot open %s: %s", S.cfg.music_dir, strerror(errno));
            reported_missing = true;
        } else if (d) {
            reported_missing = false;
        }
        while (d && (e = readdir(d))) {
            if (!is_audio(e->d_name)) continue;
            char path[320];
            snprintf(path, sizeof path, "%s/%s", S.cfg.music_dir, e->d_name);
            set_title(e->d_name, (size_t)(strrchr(e->d_name, '.') - e->d_name));
            S.skip = false;
            stream_file(path);
            played++;
        }
        if (d) closedir(d);
        if (!played) {
            if (quiet++ % QUIET_PASSES == 0)
                ESP_LOGW(TAG, "no playable files in %s", S.cfg.music_dir);
            vTaskDelay(pdMS_TO_TICKS(2000));
        } else {
            quiet = 0;
        }
    }
}

/* P2/P3/F4: what the station format costs, and how much memory is left. */
static void telemetry_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(TELEMETRY_PERIOD_MS));
        uint64_t frames = mi_enc_frames_encoded(S.enc);
        uint64_t us = mi_enc_encode_us(S.enc);
        double per_frame = frames ? (double)us / (double)frames : 0.0;
        double frame_us = 1000000.0 * (double)mi_enc_samples_per_frame(S.enc) / (double)S.cfg.out_hz;
        ESP_LOGI(TAG, "encoder: %" PRIu64 " frames, %.0f us/frame = %.1f%% of one core",
                 frames, per_frame, frame_us > 0 ? 100.0 * per_frame / frame_us : 0.0);
        ESP_LOGI(TAG, "memory: internal %u B free, external %u B free; starvation rebases %" PRIu32,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), S.rebases);
        /* The other half of fit: how much stack the deepest tasks have left. */
        ESP_LOGI(TAG, "stack headroom: broadcast %u B, worst connection %u B",
                 (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)),
                 (unsigned)http_server_conn_stack_headroom_bytes());
    }
}

/* ---------------------------------------------------------------- API */

esp_err_t mini_icecast_start(const mini_icecast_cfg_t *cfg)
{
    if (!cfg || !cfg->music_dir) return ESP_ERR_INVALID_ARG;
    S.cfg = *cfg;
    if (!S.cfg.station_name) S.cfg.station_name = "ESP32 Radio";
    if (!S.cfg.port) S.cfg.port = 8000;
    if (!S.cfg.out_hz) S.cfg.out_hz = 44100;
    if (!S.cfg.bitrate) S.cfg.bitrate = MINI_ICECAST_BITRATE_DEFAULT;
    if (!S.cfg.metaint) S.cfg.metaint = MINI_ICECAST_METAINT_DEFAULT;
    if (!S.cfg.lead_ms) S.cfg.lead_ms = 1500;
    if (!S.cfg.burst_ms) S.cfg.burst_ms = 500;
    if (!S.cfg.max_clients) S.cfg.max_clients = 2; /* documented default */
    if (S.cfg.max_clients > MAX_CLIENTS) {
        ESP_LOGE(TAG, "max_clients %u is outside the documented 1..%d range",
                 (unsigned)S.cfg.max_clients, MAX_CLIENTS);
        return ESP_ERR_INVALID_ARG; /* refuse rather than silently coerce */
    }

    /* Station format: checked here, before anything is served. */
    char err[192];
    int mpeg = mi_enc_check_format(S.cfg.out_hz, S.cfg.bitrate, err, sizeof err);
    if (mpeg < 0) {
        ESP_LOGE(TAG, "station format refused: %s", err);
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "station format: %u Hz, %u kbps CBR, joint stereo, %s",
             (unsigned)S.cfg.out_hz, (unsigned)S.cfg.bitrate, mi_enc_mpeg_name(mpeg));

    S.enc = mi_enc_new(S.cfg.out_hz, S.cfg.bitrate, err, sizeof err);
    if (!S.enc) {
        ESP_LOGE(TAG, "encoder: %s", err);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "encoder: %u samples/frame (%.1f ms), %u B of state inside the vendored "
                  "library, staging buffers in %s",
             (unsigned)mi_enc_samples_per_frame(S.enc),
             1000.0 * (double)mi_enc_samples_per_frame(S.enc) / (double)S.cfg.out_hz,
             (unsigned)mi_enc_state_bytes(),
             mi_enc_uses_external_memory(S.enc) ? "external memory" : "internal RAM (fallback)");

    /* The ring now holds *encoded* bytes, so it is sized by the station's byte
     * rate; burst_ms still means the same duration of audio. The slack covers a
     * whole record so ring_put() can always make room. */
    S.ring_size = (size_t)S.cfg.bitrate * 1000 / 8 * S.cfg.burst_ms / 1000 + CHUNK_BYTES + 2;
    S.ring = heap_caps_malloc(S.ring_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    S.ring_external = S.ring != NULL;
    if (!S.ring) S.ring = malloc(S.ring_size);
    if (!S.ring) {
        ESP_LOGE(TAG, "out of memory for the %u B burst ring", (unsigned)S.ring_size);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "burst ring: %u B (~%u ms of audio) in %s, sends at most %u clients",
             (unsigned)S.ring_size, (unsigned)S.cfg.burst_ms,
             S.ring_external ? "external memory" : "internal RAM (fallback)",
             (unsigned)S.cfg.max_clients);

    S.lock = xSemaphoreCreateMutex();
    if (!S.lock) return ESP_ERR_NO_MEM;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        S.clients[i].fd = -1;
        S.clients[i].reserved = false;
    }
    S.skip = false;

    esp_err_t err2 = http_server_register_route("/stream", stream_route, NULL);
    if (err2 != ESP_OK) {
        ESP_LOGE(TAG, "cannot register /stream: %s", esp_err_to_name(err2));
        return err2;
    }

    /* Wi-Fi lives on core 0; keep the broadcast on core 1. */
    if (xTaskCreatePinnedToCore(radio_task, "ice_broadcast", RADIO_TASK_STACK, NULL, 6, NULL, 1) != pdPASS ||
        xTaskCreatePinnedToCore(telemetry_task, "ice_telem", TELEMETRY_TASK_STACK, NULL, 3, NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "no memory for the radio task");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "GET /stream is ready; call http_server_start() to listen on port %u",
             (unsigned)S.cfg.port);
    return ESP_OK;
}

void mini_icecast_skip(void) { S.skip = true; }

size_t mini_icecast_now_playing(char *out, size_t out_size)
{
    if (!out || !out_size) return 0;
    xSemaphoreTake(S.lock, portMAX_DELAY);
    size_t n = snprintf(out, out_size, "%s", S.title);
    xSemaphoreGive(S.lock);
    return n;
}
