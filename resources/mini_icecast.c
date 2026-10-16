/*
 * mini_icecast - tiny live-radio server for ESP-IDF.
 *
 * Every track (.mp3 / .flac / .wav, any sample rate, mono or stereo) is decoded to PCM,
 * resampled to ONE station format (stereo, 16-bit, out_hz) and broadcast in real time to
 * all connected HTTP clients as a never-ending WAV stream. Because the *stream* format
 * never changes, track changes are seamless for every player, and the PCM that goes on
 * the wire is the same PCM you can tap for a visualizer (pcm_cb).
 *
 * Features: pacing, burst-on-connect, ICY metadata (StreamTitle), track change, skip.
 * Bandwidth: out_hz * 4 bytes/s per client (176 KB/s at 44.1 kHz): fine for a LAN.
 */
#include "mini_icecast.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mi_decode.h"
#include "mi_resample.h"

#define MAX_CLIENTS     4
#define TITLE_MAX       120
#define CHUNK_FRAMES    MINI_ICECAST_CHUNK_FRAMES
#define CHUNK_BYTES     (CHUNK_FRAMES * 4)
#define CLIENT_STALL_MS 400 /* a client that cannot take data for this long is dropped */

static const char *TAG = "icecast";

typedef struct {
    int      fd;
    bool     meta;        /* client asked for ICY metadata */
    uint32_t since_meta;  /* audio bytes sent since last metadata block */
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
    int64_t            played_frames; /* frames broadcast since t0 */
    int16_t            chunk[CHUNK_FRAMES * 2]; /* output chunker, persists across tracks */
    size_t             chunk_n;
    uint8_t           *ring;          /* burst-on-connect history: records of [len16 LE][chunk] */
    size_t             ring_size, ring_head, ring_tail, ring_used;
} S;

/* ------------------------------------------------------------- clients */

static bool send_all(int fd, const uint8_t *p, size_t n)
{
    while (n) {
        int r = send(fd, p, n, 0);
        if (r <= 0) return false; /* error or SO_SNDTIMEO expired: slow client -> drop */
        p += r;
        n -= r;
    }
    return true;
}

static bool send_meta(client_t *c)
{
    if (c->title_ver == S.title_ver) {
        uint8_t zero = 0;
        return send_all(c->fd, &zero, 1);
    }
    char tmp[TITLE_MAX + 24];
    int len = snprintf(tmp, sizeof tmp, "StreamTitle='%s';", S.title);
    int blocks = (len + 15) / 16; /* length byte counts 16-byte blocks */
    uint8_t buf[1 + 16 * 10];
    memset(buf, 0, 1 + blocks * 16);
    buf[0] = (uint8_t)blocks;
    memcpy(buf + 1, tmp, len);
    c->title_ver = S.title_ver;
    return send_all(c->fd, buf, 1 + blocks * 16);
}

/* Sends audio, splitting it so a metadata block lands exactly every `metaint` bytes. */
static bool client_push(client_t *c, const uint8_t *d, size_t n)
{
    if (!c->meta) return send_all(c->fd, d, n);
    while (n) {
        size_t k = S.cfg.metaint - c->since_meta;
        if (k > n) k = n;
        if (!send_all(c->fd, d, k)) return false;
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

/* ---- burst-on-connect history: the last ~burst_bytes of whole frames ---- */

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
    size_t n = 2 + (lb[0] | (lb[1] << 8));
    S.ring_tail = (S.ring_tail + n) % S.ring_size;
    S.ring_used -= n;
}

static void ring_put(const uint8_t *d, size_t n) /* lock held */
{
    while (S.ring_used + n + 2 > S.ring_size) ring_drop_oldest();
    uint8_t lb[2] = {n & 0xFF, n >> 8};
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
    static uint8_t tmp[CHUNK_BYTES];
    size_t pos = S.ring_tail, left = S.ring_used;
    while (left) {
        uint8_t lb[2];
        ring_peek(pos, lb, 2);
        size_t n = lb[0] | (lb[1] << 8);
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


/* ------------------------------------------------------------ HTTP side */

/* Endless WAV: unknown length (0xFFFFFFFF), 16-bit stereo PCM. */
static void wav_header(uint8_t h[44], uint32_t hz)
{
    uint32_t byte_rate = hz * 4, unknown = 0xFFFFFFFFu, fmt_len = 16;
    uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
    memcpy(h, "RIFF", 4);          memcpy(h + 4, &unknown, 4);
    memcpy(h + 8, "WAVEfmt ", 8);  memcpy(h + 16, &fmt_len, 4);
    memcpy(h + 20, &pcm, 2);       memcpy(h + 22, &ch, 2);
    memcpy(h + 24, &hz, 4);        memcpy(h + 28, &byte_rate, 4);
    memcpy(h + 32, &align, 2);     memcpy(h + 34, &bits, 2);
    memcpy(h + 36, "data", 4);     memcpy(h + 40, &unknown, 4);
}

static void handle_new(int fd)
{
    struct timeval rcv = {.tv_sec = 2}, snd = {.tv_usec = CLIENT_STALL_MS * 1000};
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcv, sizeof rcv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &snd, sizeof snd);
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);

    char req[512];
    int len = 0;
    while (len < (int)sizeof req - 1) {
        int r = recv(fd, req + len, sizeof req - 1 - len, 0);
        if (r <= 0) break;
        len += r;
        req[len] = 0;
        if (strstr(req, "\r\n\r\n")) break;
    }
    if (len < 4 || strncmp(req, "GET ", 4) || !strstr(req, "\r\n\r\n")) goto reject;

    int slot = -1;
    for (int i = 0; i < S.cfg.max_clients; i++)
        if (S.clients[i].fd < 0) { slot = i; break; }
    if (slot < 0) {
        static const char busy[] = "HTTP/1.0 503 Server Full\r\n\r\n";
        send(fd, busy, sizeof busy - 1, 0);
        goto reject;
    }

    bool meta = strcasestr(req, "icy-metadata: 1") != NULL;
    char h[256];
    int n = snprintf(h, sizeof h,
                     "HTTP/1.0 200 OK\r\nContent-Type: audio/wav\r\n"
                     "icy-name: %s\r\nCache-Control: no-cache\r\nConnection: close\r\n",
                     S.cfg.station_name);
    if (meta) n += snprintf(h + n, sizeof h - n, "icy-metaint: %u\r\n", (unsigned)S.cfg.metaint);
    n += snprintf(h + n, sizeof h - n, "\r\n");
    if (!send_all(fd, (uint8_t *)h, n)) goto reject;

    /* Register + WAV header + burst under the same lock broadcast() uses: the client gets
     * the history and then the next live chunk, with no gap and no duplicate. */
    struct timeval burst_to = {.tv_sec = 1};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &burst_to, sizeof burst_to);
    uint8_t wav[44];
    wav_header(wav, S.cfg.out_hz);
    xSemaphoreTake(S.lock, portMAX_DELAY);
    client_t *c = &S.clients[slot];
    c->meta = meta;
    c->since_meta = 0;
    c->title_ver = 0; /* forces the current title in the first metadata block */
    c->fd = fd;
    bool ok = client_push(c, wav, sizeof wav) && ring_burst(c);
    size_t burst = S.ring_used;
    if (!ok) c->fd = -1;
    xSemaphoreGive(S.lock);
    if (!ok) goto reject;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &snd, sizeof snd);
    ESP_LOGI(TAG, "client %d connected (icy=%d, burst=%u B)", slot, meta, (unsigned)burst);
    return;

reject:
    close(fd);
}

static void accept_task(void *arg)
{
    (void)arg;
    int ls = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a = {
        .sin_family = AF_INET,
        .sin_port = htons(S.cfg.port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(ls, (struct sockaddr *)&a, sizeof a) || listen(ls, 2)) {
        ESP_LOGE(TAG, "bind/listen failed on port %u", S.cfg.port);
        vTaskDelete(NULL);
    }
    ESP_LOGI(TAG, "listening on :%u", S.cfg.port);
    for (;;) {
        int fd = accept(ls, NULL, NULL);
        if (fd >= 0) handle_new(fd);
    }
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

/* Real-time pacing driven by the number of PCM frames sent (sample-exact). */
static void pace(uint32_t frames)
{
    S.played_frames += frames;
    int64_t played_us = S.played_frames * 1000000 / S.cfg.out_hz;
    int64_t now = esp_timer_get_time() - S.t0;
    if (now - played_us > 1000000) { /* we starved (no files, SD hiccup): rebase the clock */
        S.t0 += now - played_us;
        now = played_us;
    }
    int64_t ahead = played_us - now - (int64_t)S.cfg.lead_ms * 1000;
    if (ahead > 0) vTaskDelay(pdMS_TO_TICKS(ahead / 1000)); /* CONFIG_FREERTOS_HZ=1000 recommended */
}

static void emit_chunk(void)
{
    broadcast((const uint8_t *)S.chunk, CHUNK_BYTES);
    if (S.cfg.pcm_cb) S.cfg.pcm_cb(S.chunk, CHUNK_FRAMES, S.cfg.pcm_cb_ctx);
    pace(CHUNK_FRAMES);
}

/* Resampler output -> fixed-size chunks. Lives across tracks, so chunk boundaries never shift. */
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

static void stream_file(const char *path)
{
    mi_dec_t *d = mi_dec_open(path);
    if (!d) {
        ESP_LOGW(TAG, "cannot decode %s (unsupported or corrupt): skipping", path);
        return;
    }
    mi_rs_t *rs = mi_rs_new(mi_dec_hz(d), S.cfg.out_hz);
    if (!rs) {
        ESP_LOGE(TAG, "out of memory for the resampler");
        mi_dec_close(d);
        return;
    }
    ESP_LOGI(TAG, "now playing: %s (%u Hz -> %u Hz)", S.title, (unsigned)mi_dec_hz(d), (unsigned)S.cfg.out_hz);

    static int16_t in[MI_READ_FRAMES * 2];
    size_t n;
    while (!S.skip && (n = mi_dec_read(d, in, MI_READ_FRAMES)) > 0) mi_rs_process(rs, in, n, chunk_append, NULL);

    mi_rs_free(rs);
    mi_dec_close(d);
}

static bool is_audio(const char *name)
{
    if (name[0] == '.') return false; /* macOS "._foo.mp3" AppleDouble files on the SD */
    const char *e = strrchr(name, '.');
    return e && (!strcasecmp(e, ".mp3") || !strcasecmp(e, ".flac") || !strcasecmp(e, ".wav"));
}

static void radio_task(void *arg)
{
    (void)arg;
    S.t0 = esp_timer_get_time();
    for (;;) {
        DIR *d = opendir(S.cfg.music_dir);
        int played = 0;
        struct dirent *e;
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
            ESP_LOGW(TAG, "no playable files in %s", S.cfg.music_dir);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
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
    if (!S.cfg.metaint) S.cfg.metaint = 16000;
    if (!S.cfg.lead_ms) S.cfg.lead_ms = 1500;
    if (!S.cfg.burst_ms) S.cfg.burst_ms = 500;
    if (!S.cfg.max_clients || S.cfg.max_clients > MAX_CLIENTS) S.cfg.max_clients = 2;

    S.ring_size = (size_t)S.cfg.out_hz * 4 * S.cfg.burst_ms / 1000 + CHUNK_BYTES + 2; /* + slack: whole chunks */
    S.ring = heap_caps_malloc(S.ring_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!S.ring) S.ring = malloc(S.ring_size);
    if (!S.ring) return ESP_ERR_NO_MEM;

    S.lock = xSemaphoreCreateMutex();
    for (int i = 0; i < MAX_CLIENTS; i++) S.clients[i].fd = -1;

    /* Wi-Fi lives on core 0; keep the radio on core 1. The big stack is for the MP3 decoder scratch. */
    xTaskCreatePinnedToCore(accept_task, "ice_accept", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(radio_task, "ice_radio", 24 * 1024, NULL, 6, NULL, 1);
    return ESP_OK;
}

void mini_icecast_skip(void) { S.skip = true; }

size_t mini_icecast_now_playing(char *out, size_t out_size)
{
    xSemaphoreTake(S.lock, portMAX_DELAY);
    size_t n = snprintf(out, out_size, "%s", S.title);
    xSemaphoreGive(S.lock);
    return n;
}
