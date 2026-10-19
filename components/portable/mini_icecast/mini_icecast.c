/*
 * mini_icecast - tiny live stream server for ESP-IDF.
 *
 * Decodes .mp3 / .flac / .wav at any sample rate, resamples everything to one
 * station format and encodes it to MP3, then broadcasts that to every connected
 * client in real time. Track changes are seamless because the station format
 * never changes.
 *
 * The listening socket and the request parsing live in http_server; this file is
 * the audio producer plus the client fan-out, and it plugs into the HTTP layer
 * by registering one route.
 */
#include "mini_icecast.h"

#include <dirent.h>
#include <math.h>
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
#include "mi_burst.h"
#include "mi_decode.h"
#include "mi_encode.h"
#include "mi_icy.h"
#include "mi_ring.h"
#include "mi_resample.h"
#include "mi_station.h"
#include "tape_fx.h"

#define MAX_CLIENTS     MINI_ICECAST_MAX_CLIENTS
#define TITLE_MAX       120
#define CHUNK_FRAMES    MINI_ICECAST_CHUNK_FRAMES
#define CHUNK_BYTES     (CHUNK_FRAMES * 4)
#define BROADCAST_TASK_STACK (24 * 1024) /* the FLAC/MP3 decoder scratch is the big one */
#define TELEMETRY_TASK_STACK 4096        /* %f formatting on newlib is stack-hungry */
#define TELEMETRY_PERIOD_MS 60000
#define QUIET_PASSES    30 /* log "no playable files" once a minute, not once a loop */
/* How far back `prev` can walk. The history holds directory positions, not names, so
 * an entry costs one long; the limit is a memory decision, not a display one. */
#define TRACK_HISTORY   16

static const char *TAG = "icecast";

static bool parked(void);

#define SENDER_TASK_STACK      4096
/* How much audio a client's ring can hold. This is the jitter budget: a download
 * on the same Wi-Fi has been measured stalling the stream for seconds, and the
 * ring is what lets the connection survive that instead of being dropped. Ten
 * seconds at the station rate, so it scales down with a lower-bit-rate station. */
#define CLIENT_RING_MS         10000
#define CLIENT_SEND_TIMEOUT_MS 10000       /* one send may wait this long for the network */
#define HEADER_SEND_TIMEOUT_MS 250         /* a connecting client must take its headers */
#define SEND_BUF_BYTES         (MI_ENC_MAX_FRAME_BYTES + 64) /* one ring read */
#define SEND_BUF_FALLBACK      4096

typedef struct {
    int       fd;
    bool      reserved;   /* claimed by a connecting client, not yet on the air */
    bool      dead;       /* the producer found it too slow; the sender must close it */
    mi_icy_t *icy;        /* NULL when the client did not ask for metadata */
    mi_ring_t *ring;      /* framed bytes waiting for the sender */
    uint8_t   *send_buf;  /* per-client: the sender tasks run concurrently */
    SemaphoreHandle_t wake; /* given whenever the ring gains data */
} client_t;

static struct {
    const char        *music_dir; /* the card's directory, from the configuration */
    mi_station_t       station;   /* resolved once by mi_station_resolve() */
    client_t           clients[MAX_CLIENTS];
    SemaphoreHandle_t  lock;
    char               title[TITLE_MAX + 1];
    char               file[MINI_ICECAST_STATUS_FILE_MAX];
    mi_fmt_t           cur_fmt;
    uint32_t           cur_src_hz;
    uint32_t           cur_total_ms;
    uint32_t           library_count;
    /* Audio broadcast from the current track. Written only by the broadcast task and
     * read by mini_icecast_status(); an aligned 32-bit load or store is atomic on this
     * core, so the reader needs no lock for it. At 44.1 kHz it holds 27 hours. */
    uint32_t           track_frames;
    /* Level of the chunk being encoded, for the display. Same lock-free discipline
     * as track_frames; a 32-bit float store is atomic on this core. */
    volatile float     level_l, level_r;
    uint32_t           title_ver;
    volatile bool      skip;
    /* A request for the track before this one. The broadcast task owns the directory
     * position it goes back to; the display only raises the flag. */
    volatile bool      prev;
    int64_t            t0;            /* pacing clock origin (us) */
    int64_t            played_frames; /* encoded frames broadcast since t0 */
    int16_t            chunk[CHUNK_FRAMES * 2]; /* output chunker, persists across tracks */
    size_t             chunk_n;
    /* The tape effect, when the station is configured with it. 4 KB of internal
     * RAM, touched only by the broadcast task, and it keeps its state across
     * chunks and tracks so the tape never stops rolling. */
    tape_fx_t          fx;
    bool               fx_supported; /* the station was built with the effect */
    volatile bool      fx_enabled;   /* the user has it switched on */
    float              fx_gain;      /* wet amount, ramped between 0 and 1 */
    bool               fx_ready;     /* the effect's state matches the audio */
    int16_t            fx_scratch[CHUNK_FRAMES * 2]; /* the dry copy while fading */
    uint32_t           fx_gen;     /* the tuning generation currently applied */
    /* While a download owns the card the broadcast plays a noise bed instead of
     * reading the SD. park_until is a deadline in esp_timer microseconds, so a
     * caller that dies cannot park the station forever. */
    volatile int64_t   park_until;
    uint64_t           idle_frames; /* frames of bed, kept out of the track position */
    uint64_t           fx_us;      /* time in the effect, reported by the telemetry task */
    uint64_t           fx_frames;
    mi_burst_t        *burst;         /* history for a client connecting mid-track */
    TaskHandle_t       broadcast_handle; /* for the stack-headroom telemetry */
    mi_enc_t          *enc;
    uint32_t           rebases;       /* starvation rebases: 0 over a healthy soak */
    /* How far behind real time the last pacing call found the broadcast. Read by
     * the download path to decide when to yield the card; a 32-bit store/load is
     * atomic on this core, so it needs no lock. */
    volatile uint32_t  lag_ms;
} S;

/* ------------------------------------------------------------- clients */

/* What the station is playing, as a listener and the index should read it. While
 * a download owns the card the broadcast plays its bed, so announcing the track
 * would be a lie for however long the transfer takes. */
static const char *live_title(void)
{
    return parked() ? "Processing" : S.title;
}

/* The producer's sink for the ICY splitter. It writes framed bytes into the
 * client's ring and never touches the socket: the sender task owns the fd. A full
 * ring means the client is too slow, so it is marked dead and the broadcast moves
 * on. The broadcast lock is held, which is what makes the dead flag safe. */
static bool client_sink(void *ctx, mi_icy_kind_t kind, const uint8_t *d, size_t n)
{
    (void)kind;
    client_t *c = ctx;
    if (!c->ring) return false;
    if (mi_ring_put(c->ring, d, n) != n) {
        c->dead = true;
        return false;
    }
    xSemaphoreGive(c->wake);
    return true;
}

/* Frame the audio and queue it. mi_icy owns the metadata interval and the block
 * framing; the sender only sees bytes. */
static bool client_push(client_t *c, const uint8_t *d, size_t n)
{
    if (c->fd < 0 || c->dead) return false;
    if (!c->icy) return client_sink(c, MI_ICY_AUDIO, d, n);
    return mi_icy_push(c->icy, d, n, live_title(), S.title_ver, client_sink, c);
}

/* Queue the history, oldest first, through the same splitter the live stream uses.
 * Called with the broadcast lock held, before the sender is woken, so the burst and
 * the next live frame cannot interleave. */
static bool queue_history(client_t *c, size_t *queued_bytes) /* lock held */
{
    *queued_bytes = 0;
    static uint8_t tmp[CHUNK_BYTES > MI_ENC_MAX_FRAME_BYTES ? CHUNK_BYTES : MI_ENC_MAX_FRAME_BYTES];
    mi_burst_cursor_t cur;
    mi_burst_begin(S.burst, &cur);
    size_t n;
    while ((n = mi_burst_next(S.burst, &cur, tmp, sizeof tmp)) > 0) {
        if (!client_push(c, tmp, n)) return false;
        *queued_bytes += n;
    }
    return true;
}

/* The sender's teardown. `fd` is the descriptor it was using; if the slot has
 * already been reused the descriptor is not ours to close. */
static void sender_close(int slot, int fd, const char *why)
{
    client_t *c = &S.clients[slot];
    xSemaphoreTake(S.lock, portMAX_DELAY);
    if (c->fd == fd) {
        ESP_LOGI(TAG, "client %d gone (%s)", slot, why);
        close(fd);
        c->fd = -1;
        if (c->icy) {
            mi_icy_free(c->icy);
            c->icy = NULL;
        }
        c->dead = false;
    }
    xSemaphoreGive(S.lock);
}

/* One permanent task per client slot. It owns the socket: the audio task only
 * queues bytes, so a stalled listener costs its own sender and nothing else. */
static void sender_task(void *arg)
{
    int slot = (int)(intptr_t)arg;
    client_t *c = &S.clients[slot];
    for (;;) {
        xSemaphoreTake(c->wake, portMAX_DELAY);
        for (;;) {
            xSemaphoreTake(S.lock, portMAX_DELAY);
            int fd = c->fd;
            bool dead = c->dead;
            xSemaphoreGive(S.lock);
            if (fd < 0) break;
            if (dead) {
                sender_close(slot, fd, "too slow for the stream");
                break;
            }
            size_t n = mi_ring_get(c->ring, c->send_buf, SEND_BUF_BYTES);
            if (!n) break;
            if (!http_send_all(fd, c->send_buf, n)) {
                sender_close(slot, fd, "send failed");
                break;
            }
        }
    }
}

static void broadcast(const uint8_t *d, size_t n)
{
    xSemaphoreTake(S.lock, portMAX_DELAY);
    mi_burst_put(S.burst, d, n);
    for (int i = 0; i < S.station.max_clients; i++) {
        client_t *c = &S.clients[i];
        if (c->fd < 0 || c->dead) continue;
        bool was_dead = c->dead;
        client_push(c, d, n); /* a full ring marks it dead; its sender closes it */
        if (!was_dead && c->dead)
            ESP_LOGW(TAG, "client %d fell behind and was dropped", i);
    }
    xSemaphoreGive(S.lock);
}

/* ------------------------------------------------------------ HTTP route */

/* What the stream route did with a connection. Private: the route below is the only
 * caller, so the fd hand-off is not part of the broadcast's public surface. */
typedef enum {
    MINI_ICECAST_REGISTERED = 0,  /* the fd is on the air and outlives the request */
    MINI_ICECAST_REGISTER_FULL,   /* every client slot is taken: answer 503 */
    MINI_ICECAST_REGISTER_FAILED, /* headers or the send ring could not be prepared: just close */
} mini_icecast_register_result_t;

/* Put an already-accepted connection on the air: write the stream headers, queue the
 * burst into that client's send ring, and hand the fd to its sender task. The caller
 * must not close the fd when this returns MINI_ICECAST_REGISTERED. */
static mini_icecast_register_result_t mini_icecast_register_client(int fd, bool want_icy_metadata);

static http_result_t stream_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    switch (mini_icecast_register_client(fd, req->want_icy_metadata)) {
    case MINI_ICECAST_REGISTERED:
        return HTTP_UPGRADED; /* the broadcast owns the fd now; do not close it */
    case MINI_ICECAST_REGISTER_FULL:
        ESP_LOGW(TAG, "refusing a client: all %u slots are taken", (unsigned)S.station.max_clients);
        http_send_error(fd, 503, "every stream slot is taken; try again in a moment");
        return HTTP_HANDLED;
    default:
        return HTTP_HANDLED; /* headers or burst failed: the connection is already dead */
    }
}

static mini_icecast_register_result_t mini_icecast_register_client(int fd, bool want_icy_metadata)
{
    /* The splitter holds no socket, so it can be made before the slot is claimed.
     * Failing to allocate it is a failed registration, not a silent downgrade:
     * the headers below promise a metadata interval. */
    mi_icy_t *icy = want_icy_metadata ? mi_icy_new(S.station.metaint) : NULL;
    if (want_icy_metadata && !icy) return MINI_ICECAST_REGISTER_FAILED;

    /* Reserve a slot under the lock, so two simultaneous connects cannot claim
     * the same one. A reserved slot still has fd < 0, so broadcast() skips it. */
    xSemaphoreTake(S.lock, portMAX_DELAY);
    int slot = -1;
    for (int i = 0; i < S.station.max_clients; i++)
        if (S.clients[i].fd < 0 && !S.clients[i].reserved) { slot = i; break; }
    if (slot < 0) {
        xSemaphoreGive(S.lock);
        mi_icy_free(icy);
        return MINI_ICECAST_REGISTER_FULL;
    }
    client_t *c = &S.clients[slot];
    c->reserved = true;
    c->dead = false;
    c->icy = icy;
    mi_ring_reset(c->ring);
    xSemaphoreGive(S.lock);

    /* Bound the header write: it happens before the sender owns the fd, and an
     * unbounded send there would pin a connection worker on a silent client. */
    struct timeval to = {.tv_sec = 0, .tv_usec = HEADER_SEND_TIMEOUT_MS * 1000};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof to);

    /* Headers first: MP3 needs no container header at all, and the station name
     * goes out in canonical Icecast form - no space after the colon (defect D2). */
    char h[256];
    int n = snprintf(h, sizeof h,
                     "HTTP/1.0 200 OK\r\nContent-Type: audio/mpeg\r\n"
                     "icy-name:%s\r\nCache-Control: no-cache\r\nConnection: close\r\n",
                     S.station.station_name);
    if (want_icy_metadata) n += snprintf(h + n, sizeof h - n, "icy-metaint: %u\r\n", (unsigned)S.station.metaint);
    n += snprintf(h + n, sizeof h - n, "\r\n");
    bool sent = n > 0 && n < (int)sizeof h && http_send_all(fd, h, (size_t)n);
    if (!sent) {
        xSemaphoreTake(S.lock, portMAX_DELAY);
        c->reserved = false;
        c->icy = NULL;
        xSemaphoreGive(S.lock);
        mi_icy_free(icy);
        return MINI_ICECAST_REGISTER_FAILED;
    }

    /* From here the sender owns the socket and may wait longer for the network. */
    struct timeval snd = {.tv_sec = CLIENT_SEND_TIMEOUT_MS / 1000,
                          .tv_usec = (CLIENT_SEND_TIMEOUT_MS % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &snd, sizeof snd);

    /* Queue the history, then make the client live, both under the broadcast
     * lock: the burst is followed by the next live frame with no gap and no
     * duplicate. The sender drains it at the network's pace; the audio task does
     * not wait for a single byte of it. */
    xSemaphoreTake(S.lock, portMAX_DELAY);
    c->fd = fd;
    size_t burst = 0;
    bool queued = queue_history(c, &burst);
    if (!queued) {
        c->fd = -1;
        c->icy = NULL;
        c->dead = false;
        c->reserved = false;
        close(fd); /* the sender was never given it */
        xSemaphoreGive(S.lock);
        mi_icy_free(icy);
        return MINI_ICECAST_REGISTER_FAILED;
    }
    c->reserved = false;
    xSemaphoreGive(S.lock);
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

/* A new track: its name, its container, and a reset progress. Called before the file
 * is opened, so the display shows the name while the decoder is still working. */
static void set_track(const char *name)
{
    size_t n = strlen(name);
    const char *dot = strrchr(name, '.');
    if (dot) n = (size_t)(dot - name);
    set_title(name, n);
    xSemaphoreTake(S.lock, portMAX_DELAY);
    snprintf(S.file, sizeof S.file, "%s", name);
    S.cur_fmt = mi_dec_format_of(name);
    S.cur_src_hz = 0;      /* known once the decoder is open */
    S.cur_total_ms = 0;
    S.track_frames = 0;
    /* The bed's frames are subtracted from the position, so this counter has to
     * start over with the track. It did not, and after the first park every later
     * track computed a negative position and the display sat at zero. */
    S.idle_frames = 0;
    xSemaphoreGive(S.lock);
}

/* Real-time pacing driven by the number of encoded frames sent (sample-exact):
 * one call per encoder frame, which is the unit actually transmitted. */
static void pace(uint32_t frames)
{
    S.played_frames += frames;
    S.track_frames += frames;
    int64_t played_us = S.played_frames * 1000000 / S.station.out_hz;
    int64_t now = esp_timer_get_time() - S.t0;
    int64_t behind = now - played_us;
    S.lag_ms = behind > 0 ? (uint32_t)(behind / 1000) : 0;
    if (behind > 1000000) { /* we starved (no files, card hiccup): rebase the clock */
        S.rebases++;
        /* A starving device rebases every couple of seconds; the first few matter,
         * then the running count in the periodic telemetry is enough. */
        if (S.rebases <= 5 || S.rebases % 25 == 0)
            ESP_LOGW(TAG, "starved for %d ms, rebasing the broadcast clock (#%" PRIu32 ")",
                     (int)((now - played_us) / 1000), S.rebases);
        S.t0 += now - played_us;
        now = played_us;
    }
    int64_t ahead = played_us - now - (int64_t)S.station.lead_ms * 1000;
    if (ahead > 0) vTaskDelay(pdMS_TO_TICKS(ahead / 1000)); /* CONFIG_FREERTOS_HZ=1000 */
}

/* One complete MP3 frame from the encoder: onto the wire, then pace. */
static void on_frame(const uint8_t *frame, size_t bytes, void *ctx)
{
    (void)ctx;
    broadcast(frame, bytes);
    pace((uint32_t)mi_enc_samples_per_frame(S.enc));
}

/* The display's level, computed where the PCM already is: the same chunk the
 * encoder is about to take. RMS, normalised to 0..1. */
static void measure_level(void)
{
    int64_t sl = 0, sr = 0;
    for (size_t i = 0; i < CHUNK_FRAMES; i++) {
        int32_t l = S.chunk[2 * i], r = S.chunk[2 * i + 1];
        sl += (int64_t)l * l;
        sr += (int64_t)r * r;
    }
    S.level_l = sqrtf((float)sl / (float)CHUNK_FRAMES) / 32768.0f;
    S.level_r = sqrtf((float)sr / (float)CHUNK_FRAMES) / 32768.0f;
}

/* The effect's tuning is handed over lock-free: a control surface writes the
 * values and then bumps a generation counter with release semantics, and the
 * audio task reads the counter with acquire and applies the whole snapshot
 * before the next chunk. A partial update can never be applied, and the audio
 * task never blocks on a UI. */
static mini_icecast_fx_t fx_tune_next;
static uint32_t          fx_tune_gen;

static void fx_tune_apply(const mini_icecast_fx_t *v)
{
    if (!v->set) return;
    tape_fx_set_tape_type(&S.fx, (tape_type_t)(v->type > TAPE_METAL ? TAPE_METAL : v->type));
    tape_fx_set_dolby(&S.fx, (tape_dolby_t)(v->dolby > DOLBY_K ? DOLBY_K : v->dolby));
    tape_fx_set_tape_quality(&S.fx, v->tape_q);
    tape_fx_set_head_quality(&S.fx, v->head_q);
    tape_fx_set_motor_quality(&S.fx, v->motor_q);
}

esp_err_t mini_icecast_set_fx(const mini_icecast_fx_t *tune)
{
    if (!tune) return ESP_ERR_INVALID_ARG;
    if (!S.enc || !S.fx_supported) return ESP_ERR_INVALID_STATE;
    fx_tune_next = *tune;
    __atomic_store_n(&fx_tune_gen,
                     __atomic_load_n(&fx_tune_gen, __ATOMIC_RELAXED) + 1, __ATOMIC_RELEASE);
    return ESP_OK;
}

static bool parked(void)
{
    int64_t until = S.park_until;
    return until != 0 && esp_timer_get_time() < until;
}

void mini_icecast_park_card(uint32_t max_ms)
{
    S.park_until = esp_timer_get_time() + (int64_t)max_ms * 1000;
    /* The ICY splitter only rewrites a metadata block when the version changes,
     * so "Processing" has to bump it or clients keep reading the old title until
     * the next track. */
    xSemaphoreTake(S.lock, portMAX_DELAY);
    S.title_ver++;
    xSemaphoreGive(S.lock);
    ESP_LOGI(TAG, "card parked for up to %u ms: the stream plays the idle bed", (unsigned)max_ms);
}

void mini_icecast_unpark_card(void)
{
    bool was = S.park_until != 0;
    S.park_until = 0;
    if (was) {
        xSemaphoreTake(S.lock, portMAX_DELAY);
        S.title_ver++;
        xSemaphoreGive(S.lock);
        ESP_LOGI(TAG, "card released: the track resumes where it stopped");
    }
}

bool mini_icecast_card_parked(void) { return parked(); }

bool mini_icecast_fx_supported(void) { return S.fx_supported; }
const char *mini_icecast_music_dir(void) { return S.music_dir; }
bool mini_icecast_fx_enabled(void) { return S.fx_enabled; }

esp_err_t mini_icecast_set_fx_enabled(bool on)
{
    if (!S.fx_supported) return ESP_ERR_INVALID_STATE;
    if (S.fx_enabled == on) return ESP_OK;
    S.fx_enabled = on;   /* the audio task ramps to it at the next chunk */
    ESP_LOGI(TAG, "tape_fx: %s", on ? "on" : "off");
    return ESP_OK;
}

/* The tape effect, when the station is configured with it. Runs on whatever PCM
 * the encoder is about to take - a whole chunk, or the partial one at the end of
 * a track - because that is the only place the signal is whole and in place.
 * A chunk takes ~2.1 ms of the 23.2 ms it represents. */
static void fx_apply(size_t frames)
{
    if (!S.fx_supported) return;

    uint32_t gen = __atomic_load_n(&fx_tune_gen, __ATOMIC_ACQUIRE);
    if (gen != S.fx_gen) {
        fx_tune_apply(&fx_tune_next);
        S.fx_gen = gen;
    }

    /* Switching the effect in or out of a live stream cannot be a jump: enabling
     * one inserts a 4 ms delay line whose contents are whatever was there before,
     * so the two versions of the chunk are crossfaded over one chunk (~23 ms).
     * Fully on is the fast path and costs nothing extra. */
    float target = S.fx_enabled ? 1.f : 0.f;
    if (S.fx_gain == 0.f && target == 0.f) {
        S.fx_ready = false;   /* switched off: its state goes stale from here */
        return;
    }

    /* Coming back from off, the delay line and the filter states still hold
     * whatever was playing when it went off. Run one chunk through the effect and
     * throw it away: that costs one chunk of CPU once, and it is what makes the
     * fade below start from current audio instead of from minutes-old audio. */
    if (!S.fx_ready) {
        memcpy(S.fx_scratch, S.chunk, frames * sizeof(int16_t) * 2);
        tape_fx_process_s16(&S.fx, S.fx_scratch, frames, 2);
        S.fx_ready = true;
        return;               /* this chunk is sent dry, untouched */
    }

    int64_t t0 = esp_timer_get_time();
    if (S.fx_gain >= 1.f && target >= 1.f) {
        tape_fx_process_s16(&S.fx, S.chunk, frames, 2);
    } else {
        memcpy(S.fx_scratch, S.chunk, frames * sizeof(int16_t) * 2);
        tape_fx_process_s16(&S.fx, S.fx_scratch, frames, 2);
        const size_t n = frames * 2;
        const float step = (target - S.fx_gain) / (float)n;
        float g = S.fx_gain;
        for (size_t i = 0; i < n; i++) {
            g += step;
            float w = g < 0.f ? 0.f : (g > 1.f ? 1.f : g);
            float dry = (float)S.chunk[i], wet = (float)S.fx_scratch[i];
            S.chunk[i] = (int16_t)(dry + (wet - dry) * w);
        }
        S.fx_gain = target;
    }
    S.fx_us += (uint64_t)(esp_timer_get_time() - t0);
    S.fx_frames += frames;
}

static void emit_chunk(void)
{
    fx_apply(CHUNK_FRAMES);
    measure_level();
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

/* What the station plays while a download owns the card: a quiet noise bed, so the
 * stream keeps flowing and a listener hears "busy" rather than dead air. It goes
 * through the effect like everything else, which is why it sounds like tape hiss
 * rather than like a noise generator. IDLE_BED_DBFS is peak, before the effect. */
#define IDLE_BED_DBFS (-40.0f)                      /* 10^(-40/20) = 0.01 full scale */
#define IDLE_BED_AMP  (32767.f * 0.01f)

static void idle_pump(void)
{
    static int16_t bed[MI_READ_FRAMES * 2];
    static uint32_t rng_l = 0x9e3779b9u, rng_r = 0x7f4a7c15u;
    for (size_t i = 0; i < MI_READ_FRAMES; i++) {
        rng_l ^= rng_l << 13; rng_l ^= rng_l >> 17; rng_l ^= rng_l << 5;
        rng_r ^= rng_r << 13; rng_r ^= rng_r >> 17; rng_r ^= rng_r << 5;
        bed[2 * i]     = (int16_t)((float)(int16_t)(rng_l >> 16) * (IDLE_BED_AMP / 32768.f));
        bed[2 * i + 1] = (int16_t)((float)(int16_t)(rng_r >> 16) * (IDLE_BED_AMP / 32768.f));
    }
    /* The same chunker the decoder feeds, so a park that starts mid-chunk joins
     * the bed seamlessly instead of flushing and restarting the encoder. */
    chunk_append(bed, MI_READ_FRAMES, NULL);
    S.idle_frames += MI_READ_FRAMES;
}

/* End of track (or skip): hand over the partial chunk, complete the encoder's
 * partial frame with silence, and emit every frame that completes. Without this
 * the tail of every track - up to 26 ms - would be lost. */
static void flush_pipeline(void)
{
    if (S.chunk_n) {
        fx_apply(S.chunk_n);
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
    mi_rs_t *rs = mi_rs_new(mi_dec_hz(d), S.station.out_hz, rs_err, sizeof rs_err);
    if (!rs) {
        ESP_LOGE(TAG, "cannot resample %s: %s", path, rs_err);
        mi_dec_close(d);
        return;
    }
    xSemaphoreTake(S.lock, portMAX_DELAY);
    S.cur_src_hz = mi_dec_hz(d);
    S.cur_total_ms = mi_dec_duration_ms(d);
    xSemaphoreGive(S.lock);
    ESP_LOGI(TAG, "now playing: %s (%u Hz -> %u Hz, %u ms)", S.title, (unsigned)mi_dec_hz(d),
             (unsigned)S.station.out_hz, (unsigned)S.cur_total_ms);

    static int16_t in[MI_READ_FRAMES * 2];
    size_t n;
    /* `prev` leaves the loop the same way `skip` does: the transport below decides
     * which entry to play next. */
    for (;;) {
        if (S.skip || S.prev) break;
        if (parked()) { idle_pump(); continue; }   /* a download owns the card */
        n = mi_dec_read(d, in, MI_READ_FRAMES);
        if (n == 0) break;
        mi_rs_process(rs, in, n, chunk_append, NULL);
    }

    flush_pipeline();
    mi_rs_free(rs);
    mi_dec_close(d);
}

static bool is_audio(const char *name)
{
    if (name[0] == '.') return false; /* macOS "._foo.mp3" AppleDouble files on the SD */
    return mi_dec_format_of(name) != MI_FMT_NONE;
}

/* The playable entry a saved directory position names. The position came from
 * telldir() on this same directory, so it is the entry that was played then; the
 * is_audio() check catches a card that changed under the history. */
static bool seek_track(DIR *d, long pos, struct dirent **out)
{
    if (pos < 0) return false;
    seekdir(d, pos);
    struct dirent *e = readdir(d);
    if (!e || !is_audio(e->d_name)) return false;
    *out = e;
    return true;
}

static void broadcast_task(void *arg)
{
    (void)arg;
    S.t0 = esp_timer_get_time();
    int quiet = 0;
    bool reported_missing = false;
    for (;;) {
        DIR *d = opendir(S.music_dir);
        int played = 0;
        struct dirent *e;
        if (!d && !reported_missing) {
            /* Distinguish "the directory is not there" from "it is there and has
             * nothing playable in it": they need different fixes. */
            ESP_LOGE(TAG, "cannot open %s: %s", S.music_dir, strerror(errno));
            reported_missing = true;
        } else if (d) {
            reported_missing = false;
        }
        /* How many playable files there are, counted before playing them: a display
         * wants to say "of N", and a pass that streams as it scans cannot know N until
         * it has finished. A second readdir pass is cheap next to decoding one track. */
        if (d) {
            rewinddir(d);
            uint32_t count = 0;
            while ((e = readdir(d)))
                if (is_audio(e->d_name)) count++;
            rewinddir(d);
            xSemaphoreTake(S.lock, portMAX_DELAY);
            S.library_count = count;
            xSemaphoreGive(S.lock);
        }
        /* The entries this pass has played, by directory position. `prev` walks back
         * through it and `skip` walks forward; only when the cursor is at the end does
         * the next track come from readdir(). The positions are valid while `d` is
         * open, so the history is rebuilt with the next pass. */
        long history[TRACK_HISTORY];
        int history_count = 0;
        int history_pos = -1; /* the entry playing now */
        bool back = false;    /* the last request was prev */
        while (d) {
            if (back) {
                if (history_pos > 0) history_pos--;
                else if (history_count > 0) history_pos = 0; /* no earlier track: replay */
                else break;
                if (!seek_track(d, history[history_pos], &e)) break;
            } else if (history_pos + 1 < history_count) {
                /* The user is walking forward again through tracks already played. */
                history_pos++;
                if (!seek_track(d, history[history_pos], &e)) break;
            } else {
                long pos = -1;
                do {
                    pos = telldir(d);
                    e = readdir(d);
                } while (e && !is_audio(e->d_name));
                if (!e || pos < 0) break;
                if (history_count == TRACK_HISTORY) {
                    /* The oldest entry leaves; the cursor follows the array. */
                    memmove(history, history + 1, sizeof history[0] * (TRACK_HISTORY - 1));
                    history_count--;
                    history_pos--;
                }
                history[history_count++] = pos;
                history_pos = history_count - 1;
            }
            char path[320];
            snprintf(path, sizeof path, "%s/%s", S.music_dir, e->d_name);
            set_track(e->d_name);
            S.skip = false;
            S.prev = false;
            stream_file(path);
            played++;
            back = S.prev;
        }
        if (d) closedir(d);
        if (!played) {
            if (quiet++ % QUIET_PASSES == 0)
                ESP_LOGW(TAG, "no playable files in %s", S.music_dir);
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
        double frame_us = 1000000.0 * (double)mi_enc_samples_per_frame(S.enc) / (double)S.station.out_hz;
        ESP_LOGI(TAG, "encoder: %" PRIu64 " frames, %.0f us/frame = %.1f%% of one core",
                 frames, per_frame, frame_us > 0 ? 100.0 * per_frame / frame_us : 0.0);
        /* What the effect costs, measured the same way as the encoder's number, so
         * the two are directly comparable and the headroom is a subtraction. */
        if (S.fx_supported && S.fx_frames) {
            double per = (double)S.fx_us / (double)S.fx_frames;
            ESP_LOGI(TAG, "tape_fx: %.2f us/chunk = %.1f%% of one core",
                     per * CHUNK_FRAMES, per * (double)S.station.out_hz / 10000.0);
        }
        /* The largest single free block is reported next to the free total because
         * it is the number that actually constrains this firmware: esp_wifi_init()
         * needs roughly 25 KB of contiguous DMA-capable internal RAM, and a
         * fragmented heap with plenty free can still fail to produce it. */
        ESP_LOGI(TAG, "memory: internal %u B free (largest block %u B), external %u B free; starvation rebases %" PRIu32,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), S.rebases);
        /* The other half of fit: how much stack the deepest tasks have left. */
        /* The broadcast's own high-water mark, not this task's: the old line
         * passed NULL, which asks about the task doing the asking. */
        ESP_LOGI(TAG, "stack headroom: broadcast %u B, worst connection %u B",
                 (unsigned)(S.broadcast_handle
                                ? uxTaskGetStackHighWaterMark(S.broadcast_handle) * sizeof(StackType_t)
                                : 0),
                 (unsigned)http_server_conn_stack_headroom_bytes());
    }
}

/* ---------------------------------------------------------------- API */

esp_err_t mini_icecast_check_station(const mini_icecast_cfg_t *cfg, char *err, size_t err_size)
{
    if (!cfg || !cfg->music_dir) {
        if (err && err_size) snprintf(err, err_size, "no music directory configured");
        return ESP_ERR_INVALID_ARG;
    }
    /* Resolve-and-discard. The defaults, the client limit and the format
     * arithmetic are mi_station's, so this cannot disagree with start(). */
    return mi_station_resolve(cfg, NULL, err, err_size);
}

esp_err_t mini_icecast_start(const mini_icecast_cfg_t *cfg)
{
    if (!cfg || !cfg->music_dir) return ESP_ERR_INVALID_ARG;

    /* Resolve once, before anything is allocated: a format that cannot be
     * served is refused without touching the heap or the network. */
    char err[192];
    if (mi_station_resolve(cfg, &S.station, err, sizeof err) != ESP_OK) {
        ESP_LOGE(TAG, "station configuration refused: %s", err);
        return ESP_ERR_INVALID_ARG;
    }
    S.music_dir = cfg->music_dir;
    S.fx_supported = cfg->fx != 0;
    S.fx_enabled = S.fx_supported;

    ESP_LOGI(TAG, "station format: %u Hz, %u kbps CBR, joint stereo, %s",
             (unsigned)S.station.out_hz, (unsigned)S.station.bitrate,
             mi_enc_mpeg_name(S.station.mpeg));

    size_t internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    S.enc = mi_enc_new(S.station.out_hz, S.station.bitrate, err, sizeof err);
    size_t internal_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!S.enc) {
        ESP_LOGE(TAG, "encoder: %s", err);
        return ESP_ERR_NO_MEM;
    }
    /* The encoder allocates its own state, so this delta is the only way to see
     * which memory class it landed in - and it is the difference between an
     * encoder that keeps up and one that does not. */
    ESP_LOGI(TAG, "encoder: %u B of internal RAM taken (the encoder's own state; near zero "
                  "means it landed in external memory)",
             (unsigned)(internal_before > internal_after ? internal_before - internal_after : 0));
    ESP_LOGI(TAG, "encoder: %u samples/frame (%.1f ms), %u B of state inside the vendored "
                  "library, staging buffers in %s",
             (unsigned)mi_enc_samples_per_frame(S.enc),
             1000.0 * (double)mi_enc_samples_per_frame(S.enc) / (double)S.station.out_hz,
             (unsigned)mi_enc_state_bytes(),
             mi_enc_uses_external_memory(S.enc) ? "external memory" : "internal RAM (fallback)");
    if (mi_enc_corrects_padding(S.enc))
        ESP_LOGW(TAG, "this station format needs the encoder's padding bit corrected (see the log above)");

    /* The effect is initialised after the encoder for the same reason the encoder
     * is allocated early: it is 4 KB of internal RAM, and the order memory is
     * taken in is part of what keeps Wi-Fi's DMA buffers available. */
    if (S.fx_supported) {
        tape_fx_init(&S.fx, (float)S.station.out_hz);
        fx_tune_apply(&cfg->fx_tune);   /* a no-op unless the caller set it */

        ESP_LOGI(TAG, "tape_fx: on (%u B of state), applied to the chunk before the encoder; "
                      "the stream now carries saturation, wow/flutter and hiss",
                 (unsigned)sizeof(tape_fx_t));
    } else {
        ESP_LOGI(TAG, "tape_fx: off; the stream is the decoder's output, sample for sample");
    }

    /* The ring holds *encoded* bytes, so it is sized by the station's byte rate;
     * burst_ms still means the same duration of audio. The slack is one internal
     * chunk, which is larger than any encoded frame, so the newest record can
     * always be kept. mi_burst refuses a capacity that cannot hold a record. */
    size_t ring_bytes =
        (size_t)S.station.bitrate * 1000 / 8 * S.station.burst_ms / 1000 + CHUNK_BYTES + 2;
    S.burst = mi_burst_new(ring_bytes, true);
    if (!S.burst) {
        ESP_LOGE(TAG, "out of memory for the %u B burst ring", (unsigned)ring_bytes);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "burst ring: %u B (~%u ms of audio) in %s, sends at most %u clients",
             (unsigned)mi_burst_capacity(S.burst), (unsigned)S.station.burst_ms,
             mi_burst_uses_external_memory(S.burst) ? "external memory" : "internal RAM (fallback)",
             (unsigned)S.station.max_clients);

    S.lock = xSemaphoreCreateMutex();
    if (!S.lock) return ESP_ERR_NO_MEM;
    size_t client_ring_bytes = (size_t)S.station.bitrate * 1000 / 8 * CLIENT_RING_MS / 1000 + 4096;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_t *c = &S.clients[i];
        c->fd = -1;
        c->reserved = false;
        c->dead = false;
        c->icy = NULL;
        c->ring = NULL;
        c->wake = NULL;
        c->send_buf = NULL;
        if (i >= S.station.max_clients) continue;
        c->ring = mi_ring_new(client_ring_bytes, true);
        c->wake = xSemaphoreCreateBinary();
        c->send_buf = heap_caps_malloc(SEND_BUF_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!c->send_buf) c->send_buf = malloc(SEND_BUF_BYTES);
        if (!c->ring || !c->wake || !c->send_buf) {
            ESP_LOGE(TAG, "no memory for client %d's send ring", i);
            return ESP_ERR_NO_MEM;
        }
        if (xTaskCreatePinnedToCore(sender_task, "ice_send", SENDER_TASK_STACK,
                                    (void *)(intptr_t)i, 4, NULL, 0) != pdPASS) {
            ESP_LOGE(TAG, "no memory for client %d's sender task", i);
            return ESP_ERR_NO_MEM;
        }
    }
    ESP_LOGI(TAG, "stream: %u client slot(s), %u B send ring each (~%u ms); the audio task never writes a socket",
             (unsigned)S.station.max_clients, (unsigned)client_ring_bytes, (unsigned)CLIENT_RING_MS);
    S.skip = false;
    S.prev = false;

    esp_err_t err2 = http_server_register_route("/stream", stream_route, NULL);
    if (err2 != ESP_OK) {
        ESP_LOGE(TAG, "cannot register /stream: %s", esp_err_to_name(err2));
        return err2;
    }

    /* Wi-Fi lives on core 0; keep the broadcast on core 1. The handle is kept for
     * the telemetry's stack-headroom line. */
    BaseType_t ok_broadcast =
        xTaskCreatePinnedToCore(broadcast_task, "ice_broadcast", BROADCAST_TASK_STACK, NULL, 6,
                                &S.broadcast_handle, 1);
    BaseType_t ok_telem =
        xTaskCreatePinnedToCore(telemetry_task, "ice_telem", TELEMETRY_TASK_STACK, NULL, 3, NULL, 1);
    if (ok_broadcast != pdPASS || ok_telem != pdPASS) {
        ESP_LOGE(TAG, "no memory for the broadcast task");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "GET /stream is ready; call http_server_start() to listen on port %u",
             (unsigned)S.station.port);
    return ESP_OK;
}

void mini_icecast_skip(void) { S.skip = true; }

void mini_icecast_prev(void) { S.prev = true; }

uint32_t mini_icecast_lag_ms(void) { return S.lag_ms; }

void mini_icecast_status(mini_icecast_status_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof *out);
    out->format = MI_FMT_NONE;
    xSemaphoreTake(S.lock, portMAX_DELAY);
    snprintf(out->title, sizeof out->title, "%s", live_title());
    out->fx_on    = S.fx_enabled;
    out->fx_type  = (uint8_t)S.fx.type;
    out->fx_dolby = (uint8_t)S.fx.dolby;
    out->fx_tape_q  = S.fx.tape_q;
    out->fx_head_q  = S.fx.head_q;
    out->fx_motor_q = S.fx.motor_q;
    snprintf(out->file, sizeof out->file, "%s", S.file);
    out->format = S.cur_fmt;
    out->source_hz = S.cur_src_hz;
    out->out_hz = S.station.out_hz;
    out->bitrate = S.station.bitrate;
    out->duration_ms = S.cur_total_ms;
    out->library_count = S.library_count;
    xSemaphoreGive(S.lock);
    /* Read outside the lock on purpose: see the field comments. */
    /* The bed is broadcast but it is not the track: a park that lasts 105 s must
     * not move the progress bar 105 s forward when the music resumes where it
     * stopped. */
    uint64_t played = S.track_frames > S.idle_frames ? S.track_frames - S.idle_frames : 0;
    if (S.station.out_hz) out->elapsed_ms = (uint32_t)(played * 1000ULL / S.station.out_hz);
    out->level_l = S.level_l;
    out->level_r = S.level_r;
}

size_t mini_icecast_now_playing(char *out, size_t out_size)
{
    if (!out || !out_size) return 0;
    xSemaphoreTake(S.lock, portMAX_DELAY);
    size_t n = snprintf(out, out_size, "%s", live_title());
    xSemaphoreGive(S.lock);
    return n;
}
