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

#include <math.h>
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
#include "freertos/idf_additions.h"
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
#include "mi_shuffle.h"
#include "mi_station.h"
#include "tone_eq.h"

#define MAX_CLIENTS     MINI_ICECAST_MAX_CLIENTS
#define TITLE_MAX       120
#define CHUNK_FRAMES    MINI_ICECAST_CHUNK_FRAMES
#define CHUNK_BYTES     (CHUNK_FRAMES * 4)
#define BROADCAST_TASK_STACK (16 * 1024) /* decode moved to ice_decode; this is resample + encode */
#define TELEMETRY_TASK_STACK 4096        /* %f formatting on newlib is stack-hungry */
#define TELEMETRY_PERIOD_MS 60000
#define QUIET_PASSES    30 /* log "no playable files" once a minute, not once a loop */
/* How far back `prev` can walk. The history holds directory positions, not names, so
 * an entry costs one long; the limit is a memory decision, not a display one. */
#define TRACK_HISTORY   16

static const char *TAG = "icecast";

static bool parked(void);

#define SENDER_TASK_STACK      4096
/* How much audio a client's ring can hold. This is the jitter budget: a Wi-Fi
 * stall has been measured lasting seconds, and the ring is what lets the
 * connection survive that instead of being dropped. Ten
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
    const library_t   *library;   /* the Library this broadcast consumes, in broadcast order */
    mi_station_t       station;   /* resolved once by mi_station_resolve() */
    client_t           clients[MAX_CLIENTS];
    SemaphoreHandle_t  lock;
    char               title[TITLE_MAX + 1];
    char               file[MINI_ICECAST_STATUS_FILE_MAX];
    char               path[LIB_PATH_MAX];
    mi_fmt_t           cur_fmt;
    uint32_t           generation;
    uint32_t           track_index;
    uint32_t           cur_src_hz;
    uint32_t           cur_total_ms;
    uint32_t           library_count;
    /* The order being walked: Library track indices with the generation they were
     * resolved in. `order` is read by the broadcast task for a whole pass and only the
     * broadcast task swaps or frees it; `order_pending` is published by any task and
     * applied at the top of the next pass. */
    struct { uint32_t *items; uint32_t count; uint32_t generation; } order;
    struct { uint32_t *items; uint32_t count; uint32_t generation; } order_pending;
    bool               order_pending_set;
    void             (*order_dropped_cb)(void *ctx);
    void              *order_dropped_ctx;
    /* Audio broadcast from the current track. Written only by the broadcast task and
     * read by mini_icecast_status(); an aligned 32-bit load or store is atomic on this
     * core, so the reader needs no lock for it. At 44.1 kHz it holds 27 hours. */
    uint32_t           track_frames;
    /* Level of the chunk being encoded, for the display. Same lock-free discipline
     * as track_frames; a 32-bit float store is atomic on this core. */
    volatile float     level_l, level_r;
    /* The broadcast's tone: the three band targets and the trim, written by the screen
     * and read by the broadcast task at its next chunk. Same lock-free discipline as
     * the level; a 32-bit float store is atomic on this core. The filter state itself
     * belongs to the broadcast task and is never touched from the screen. */
    tone_eq_t          eq;
    volatile float     eq_db[TONE_EQ_BANDS];
    volatile float     eq_gain_db;
    uint32_t           title_ver;
    volatile bool      skip;
    /* A request for the track before this one. The broadcast task owns the
     * position in the broadcast order it goes back to; the display only raises the flag. */
    volatile bool      prev;
    volatile bool      play_pending;

    char               pending_path[LIB_PATH_MAX];
    uint32_t           pending_index;
    uint32_t           pending_generation;
    int64_t            t0;            /* pacing clock origin (us) */
    int64_t            played_frames; /* encoded frames broadcast since t0 */
    int16_t            chunk[CHUNK_FRAMES * 2]; /* output chunker, persists across tracks */
    size_t             chunk_n;
    /* While the card is parked the broadcast plays a noise bed instead of
     * reading the SD. park_until is a deadline in esp_timer microseconds, so a
     * caller that dies cannot park the station forever. */
    volatile int64_t   park_until;
    int                park_refs;     /* reference-counted: two owners may park */
    uint64_t           idle_frames; /* frames of bed, kept out of the track position */
    /* Per-stage cost since the last telemetry line, in milliseconds. Written by the
     * broadcast task and reset by the telemetry task, which is why they are plain
     * 32-bit cells: the reset may lose a tick, never a correct attribution. They
     * exist because the encoder's cost alone does not say which stage starves a
     * hi-res FLAC, and the board's log is the only place that split is visible. */
    volatile uint32_t  dec_ms, dec_io_ms, rs_ms, eq_ms;
    mi_burst_t        *burst;         /* history for a client connecting mid-track */
    TaskHandle_t       broadcast_handle; /* for the stack-headroom telemetry */
    mi_enc_t          *enc;
    uint32_t           rebases;       /* starvation rebases: 0 over a healthy soak */
    /* How far behind real time the last pacing call found the broadcast. Read by
     * the artwork loader to decide when to yield the card; a 32-bit store/load is
     * atomic on this core, so it needs no lock. */
    volatile uint32_t  lag_ms;
    /* The shuffle mode (docs/adr/0023). The Menu only flips `shuffle_on`; the
     * broadcast builds the order at the top of its next pass, while it holds the
     * Library lease, and owns it for the pass. `shuffle.order` is one count*4 B
     * allocation (80,000 B at 20,000 Tracks), PSRAM first; shuffle_generation is
     * the generation the order was built from, so a rescan rebuilds it. */
    bool               shuffle_on;
    mi_shuffle_t       shuffle;
    uint32_t           shuffle_generation;
    uint32_t           shuffle_seed; /* published by mini_icecast_shuffle_seed() */
} S;

/* ------------------------------------------------------------- clients */

/* What the station is playing, as a listener and the index should read it. While
 * the card is parked the broadcast plays its bed, so announcing the track would
 * be a lie for however long the scan takes. */
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

static const char *json_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

static bool json_utf8(uint32_t cp, char *out, size_t *n, size_t cap)
{
    if (cp < 0x80) {
        if (*n + 1 >= cap) return false;
        out[(*n)++] = (char)cp;
    } else if (cp < 0x800) {
        if (*n + 2 >= cap) return false;
        out[(*n)++] = (char)(0xC0 | (cp >> 6));
        out[(*n)++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        if (*n + 3 >= cap) return false;
        out[(*n)++] = (char)(0xE0 | (cp >> 12));
        out[(*n)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[(*n)++] = (char)(0x80 | (cp & 0x3F));
    } else {
        if (*n + 4 >= cap) return false;
        out[(*n)++] = (char)(0xF0 | (cp >> 18));
        out[(*n)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[(*n)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[(*n)++] = (char)(0x80 | (cp & 0x3F));
    }
    return true;
}

static bool json_string(const char **pp, char *out, size_t cap)
{
    const char *p = *pp;
    if (*p++ != '"') return false;
    size_t n = 0;
    while (*p && *p != '"') {
        unsigned char c = (unsigned char)*p++;
        if (c < 0x20) return false;
        if (c != '\\') {
            if (n + 1 >= cap) return false;
            out[n++] = (char)c;
            continue;
        }
        c = (unsigned char)*p++;
        switch (c) {
        case '"': case '\\': case '/': out[n++] = (char)c; break;
        case 'b': out[n++] = '\b'; break;
        case 'f': out[n++] = '\f'; break;
        case 'n': out[n++] = '\n'; break;
        case 'r': out[n++] = '\r'; break;
        case 't': out[n++] = '\t'; break;
        case 'u': {
            uint32_t cp = 0;
            for (int i = 0; i < 4; i++) {
                char h = *p++;
                int v = (h >= '0' && h <= '9') ? h - '0'
                      : (h >= 'a' && h <= 'f') ? h - 'a' + 10
                      : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : -1;
                if (v < 0) return false;
                cp = cp * 16 + (uint32_t)v;
            }
            if (cp >= 0xD800 && cp <= 0xDBFF && p[0] == '\\' && p[1] == 'u') {
                uint32_t low = 0;
                const char *q = p + 2;
                for (int i = 0; i < 4; i++) {
                    char h = *q++;
                    int v = (h >= '0' && h <= '9') ? h - '0'
                          : (h >= 'a' && h <= 'f') ? h - 'a' + 10
                          : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : -1;
                    if (v < 0) return false;
                    low = low * 16 + (uint32_t)v;
                }
                if (low < 0xDC00 || low > 0xDFFF) return false;
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                p = q;
            } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                return false;
            }
            if (cp < 0x20) return false;
            if (!json_utf8(cp, out, &n, cap)) return false;
            break;
        }
        default: return false;
        }
        if (n + 1 >= cap) return false;
    }
    if (*p != '"') return false;
    out[n] = 0;
    *pp = p + 1;
    return true;
}

/* The body is exactly {"path":"..."}; unknown keys are refused rather than
 * silently ignored, so a typo cannot look like a successful play request. */
static bool json_play_path(const char *body, char *out, size_t cap)
{
    const char *p = json_ws(body);
    if (*p++ != '{') return false;
    p = json_ws(p);
    if (*p == '}') return false;
    char key[32];
    if (!json_string(&p, key, sizeof key)) return false;
    if (strcmp(key, "path") != 0) return false;
    p = json_ws(p);
    if (*p++ != ':') return false;
    p = json_ws(p);
    if (!json_string(&p, out, cap)) return false;
    p = json_ws(p);
    if (*p++ != '}') return false;
    p = json_ws(p);
    return *p == 0;
}

static void send_json(int fd, int status, const char *body)
{
    if (http_send_headers(fd, status, "application/json", strlen(body), NULL) == ESP_OK)
        http_send_all(fd, body, strlen(body));
}

static http_result_t play_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    char rel[LIB_PATH_MAX];
    if (!req->body || !json_play_path((const char *)req->body, rel, sizeof rel)) {
        send_json(fd, 400, "{\"ok\":false,\"error\":\"bad request\"}");
        return HTTP_HANDLED;
    }
    if (!mini_icecast_play(rel)) {
        send_json(fd, 404, "{\"ok\":false,\"error\":\"no such track\"}");
        return HTTP_HANDLED;
    }
    send_json(fd, 200, "{\"ok\":true}");
    return HTTP_HANDLED;
}

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

/* A new Track: its name, relative path, position in the broadcast order and container, with a
 * reset progress. Called before the file is opened, so the display shows the name
 * while the decoder is still working. */
static void set_track(const library_t *lib, uint32_t index, lib_node_t n)
{
    uint16_t nlen = 0;
    const char *name = library_name(lib, n, &nlen);
    char file[MINI_ICECAST_STATUS_FILE_MAX];
    snprintf(file, sizeof file, "%.*s", (int)nlen, name);
    size_t stem = strlen(file);
    const char *dot = strrchr(file, '.');
    if (dot) stem = (size_t)(dot - file);
    set_title(file, stem);

    char rel[LIB_PATH_MAX];
    if (library_relpath(lib, n, rel, sizeof rel) == (size_t)-1) rel[0] = 0;
    xSemaphoreTake(S.lock, portMAX_DELAY);
    snprintf(S.file, sizeof S.file, "%s", file);
    snprintf(S.path, sizeof S.path, "%s", rel);
    S.cur_fmt = library_format(lib, n);
    S.cur_src_hz = 0;      /* known once the decoder is open */
    S.cur_total_ms = 0;
    S.generation = library_generation(lib);
    S.track_index = index;
    S.track_frames = 0;
    /* The bed's frames are subtracted from the position, so this counter has to
     * start over with the track. It did not, and after the first park every later
     * track computed a negative position and the display sat at zero. */
    S.idle_frames = 0;
    xSemaphoreGive(S.lock);
}

static void clear_track(void)
{
    xSemaphoreTake(S.lock, portMAX_DELAY);
    bool changed = S.title[0] || S.file[0] || S.path[0] || S.cur_fmt != MI_FMT_NONE ||
                   S.track_index != 0;
    S.title[0] = 0;
    S.file[0] = 0;
    S.path[0] = 0;
    S.cur_fmt = MI_FMT_NONE;
    S.cur_src_hz = 0;
    S.cur_total_ms = 0;
    S.generation = 0;
    S.track_index = 0;
    S.track_frames = 0;
    S.idle_frames = 0;
    if (changed) S.title_ver++;
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

static bool parked(void)
{
    int64_t until = S.park_until;
    return until != 0 && esp_timer_get_time() < until;
}

void mini_icecast_park_card(uint32_t max_ms)
{
    /* More than one owner can need the card. A reference count means the second
     * unpark cannot release the first owner's park. The deadline still protects
     * against a caller that dies outright. */
    xSemaphoreTake(S.lock, portMAX_DELAY);
    int64_t now = esp_timer_get_time();
    if (S.park_until == 0 || now >= S.park_until) S.park_refs = 0; /* expired owner */
    int64_t until = now + (int64_t)max_ms * 1000;
    bool first = S.park_refs == 0;
    if (first) {
        S.park_until = until;
        S.title_ver++;
    } else if (until > S.park_until) {
        S.park_until = until;
    }
    S.park_refs++;
    xSemaphoreGive(S.lock);
    if (first)
        ESP_LOGI(TAG, "card parked for up to %u ms: the stream plays the idle bed", (unsigned)max_ms);
}

void mini_icecast_unpark_card(void)
{
    xSemaphoreTake(S.lock, portMAX_DELAY);
    bool was = S.park_refs > 0;
    if (S.park_refs > 0) S.park_refs--;
    bool last = was && S.park_refs == 0;
    if (last) {
        S.park_until = 0;
        S.title_ver++;
    }
    xSemaphoreGive(S.lock);
    if (last) ESP_LOGI(TAG, "card released: the track resumes where it stopped");
}

bool mini_icecast_card_parked(void) { return parked(); }

/* Publish the screen's tone targets and run the EQ on `frames` of S.chunk. Both the full
 * chunk and the partial tail at a track boundary take this path, so the trim and the bands
 * never miss the last 23 ms. */
static void tone_apply(size_t frames)
{
    for (int b = 0; b < TONE_EQ_BANDS; b++)
        tone_eq_set_band(&S.eq, b, S.eq_db[b]);
    tone_eq_set_gain(&S.eq, S.eq_gain_db);
    int64_t t0 = esp_timer_get_time();
    tone_eq_process(&S.eq, S.chunk, frames);
    S.eq_ms += (uint32_t)((esp_timer_get_time() - t0) / 1000);
}

static void emit_chunk(void)
{
    /* The tone first, so the display's level is the level that goes on the wire. The
     * targets are read once per chunk; the module slews them and owns the filters. */
    tone_apply(CHUNK_FRAMES);

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

/* What the station plays while the card is parked: a quiet noise bed, so the
 * stream keeps flowing and a listener hears "busy" rather than dead air.
 * IDLE_BED_DBFS is its peak level. */
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
        tone_apply(S.chunk_n);
        mi_enc_feed(S.enc, S.chunk, S.chunk_n, on_frame, NULL);
        S.chunk_n = 0;
    }
    mi_enc_flush(S.enc, on_frame, NULL);
    size_t desync = mi_enc_desync_bytes(S.enc);
    if (desync) ESP_LOGW(TAG, "encoder re-framing skipped %u bytes (expected 0)", (unsigned)desync);
}

/* ------------------------------------------------------- the decoded source */

/* The broadcast task used to read the card, decode, resample and encode in one
 * loop. Those stages have very different bottlenecks: the card is a blocking SPI
 * transfer, the decoder is CPU, and the encoder is the only stage that must run
 * at real time. Serialised, a 24-bit 192 kHz FLAC spent 26 % of its audio time
 * waiting for the card and 30 % decoding, which left less than half of one core
 * for the encoder and starved the stream.
 *
 * The source is now its own task on core 0: it opens the track, decodes at the
 * file's rate and fills a ring of decoded stereo frames, while the broadcast task
 * on core 1 only resamples and encodes. The hand-off is that ring, so the card and
 * the decoder stop being on the encoder's critical path, and the station's pacing
 * is unchanged.
 *
 * The ring holds whole stereo frames at the source rate. 384 KB is half a second
 * of 192 kHz audio and two seconds of 44.1 kHz: enough that a card stall shows up
 * as the producer waiting, never as the encoder running dry.
 */
#define SRC_FIFO_BYTES (384 * 1024)
#define SRC_TASK_STACK (32 * 1024) /* dr_flac's open keeps 16 KB of stream state */
#define SRC_PRIO       3           /* below the LVGL port (4) and Wi-Fi on core 0 */

typedef struct {
    mi_dec_t         *dec;
    mi_ring_t        *fifo;
    SemaphoreHandle_t data, space, ready, done;
    TaskHandle_t      task;
    volatile bool     stop, eof, ok;
    uint32_t          hz, duration_ms;
    char              path[LIB_PATH_MAX];
} src_job_t;

static void src_task(void *arg)
{
    src_job_t *j = arg;
    j->dec = mi_dec_open(j->path);
    if (j->dec) {
        j->hz = mi_dec_hz(j->dec);
        j->duration_ms = mi_dec_duration_ms(j->dec);
        j->ok = j->hz != 0;
    }
    xSemaphoreGive(j->ready);
    if (!j->ok) {
        xSemaphoreGive(j->done);
        vTaskDeleteWithCaps(NULL); /* owns its external stack; only it may free it */
        return;
    }
    int16_t pcm[MI_READ_FRAMES * 2];
    while (!j->stop) {
        if (parked()) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        int64_t t0 = esp_timer_get_time();
        uint64_t io0 = mi_dec_io_us(j->dec);
        size_t n = mi_dec_read(j->dec, pcm, MI_READ_FRAMES);
        uint64_t io1 = mi_dec_io_us(j->dec);
        S.dec_ms += (uint32_t)((esp_timer_get_time() - t0) / 1000);
        S.dec_io_ms += (uint32_t)((io1 - io0) / 1000);
        if (n == 0) break;
        const uint8_t *p = (const uint8_t *)pcm;
        size_t bytes = n * 4;
        while (bytes && !j->stop) {
            size_t k = mi_ring_put(j->fifo, p, bytes);
            if (k) {
                p += k;
                bytes -= k;
                xSemaphoreGive(j->data);
            }
            if (bytes) {
                if (xSemaphoreTake(j->space, pdMS_TO_TICKS(50)) == pdTRUE &&
                    mi_ring_used(j->fifo) == mi_ring_capacity(j->fifo))
                    vTaskDelay(1); /* stale hint: yield, do not spin */
            }
        }
    }
    j->eof = true;
    xSemaphoreGive(j->data); /* wake the consumer so it sees the end */
    if (j->dec) mi_dec_close(j->dec);
    j->dec = NULL;
    xSemaphoreGive(j->done);
    vTaskDeleteWithCaps(NULL); /* owns its external stack; only it may free it */
}

static void src_job_free(src_job_t *j)
{
    if (j->fifo) mi_ring_free(j->fifo);
    if (j->data) vSemaphoreDelete(j->data);
    if (j->space) vSemaphoreDelete(j->space);
    if (j->ready) vSemaphoreDelete(j->ready);
    if (j->done) vSemaphoreDelete(j->done);
    j->fifo = NULL;
    j->data = j->space = j->ready = j->done = NULL;
}

static void stream_file(const char *path)
{
    src_job_t j;
    memset(&j, 0, sizeof j);
    snprintf(j.path, sizeof j.path, "%s", path);

    j.fifo = mi_ring_new(SRC_FIFO_BYTES, true);
    j.data = xSemaphoreCreateBinary();
    j.space = xSemaphoreCreateBinary();
    j.ready = xSemaphoreCreateBinary();
    j.done = xSemaphoreCreateBinary();
    if (!j.fifo || !j.data || !j.space || !j.ready || !j.done) {
        ESP_LOGE(TAG, "no memory for the decoded-audio ring; skipping %s", path);
        src_job_free(&j);
        return;
    }

    /* The stack is external: dr_flac's open keeps 32 KB of stream state, which must
     * not take internal RAM from Wi-Fi, and this task is mostly blocked on the
     * card. There is no same-task fallback, because that 32 KB would not fit the
     * broadcast task's 16 KB stack; a failed allocation skips the track instead. */
    if (xTaskCreatePinnedToCoreWithCaps(src_task, "ice_decode", SRC_TASK_STACK, &j, SRC_PRIO,
                                       &j.task, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGE(TAG, "no memory for the decode task; skipping %s", path);
        src_job_free(&j);
        return;
    }
    xSemaphoreTake(j.ready, portMAX_DELAY);
    if (!j.ok) {
        ESP_LOGW(TAG, "cannot decode %s (unsupported or corrupt): skipping", path);
        xSemaphoreTake(j.done, portMAX_DELAY);
        src_job_free(&j);
        return;
    }
    /* A source above the budget would not starve this Track alone: it would starve the
     * live stream for every listener until the Track ended. The decoder is already open
     * because only it knows the source rate; stop it and let the broadcast advance. The
     * Track stays in the Library and is skipped, not deleted. */
    if (!mini_icecast_source_rate_ok(j.hz)) {
        ESP_LOGW(TAG, "skipping %s: %u Hz source is above the %u Hz playback budget "
                      "(transcode it on the card to play it)",
                 path, (unsigned)j.hz, (unsigned)MINI_ICECAST_MAX_SOURCE_HZ);
        j.stop = true;
        xSemaphoreGive(j.space);
        xSemaphoreTake(j.done, portMAX_DELAY);
        src_job_free(&j);
        return;
    }

    char rs_err[160];
    mi_rs_t *rs = mi_rs_new(j.hz, S.station.out_hz, rs_err, sizeof rs_err);
    if (!rs) {
        ESP_LOGE(TAG, "cannot resample %s: %s", path, rs_err);
        j.stop = true;
        xSemaphoreGive(j.space);
        xSemaphoreTake(j.done, portMAX_DELAY);
        src_job_free(&j);
        return;
    }
    xSemaphoreTake(S.lock, portMAX_DELAY);
    S.cur_src_hz = j.hz;
    S.cur_total_ms = j.duration_ms;
    xSemaphoreGive(S.lock);
    ESP_LOGI(TAG, "now playing: %s (%u Hz -> %u Hz, %u ms, source on core 0)", S.title,
             (unsigned)j.hz, (unsigned)S.station.out_hz, (unsigned)j.duration_ms);

    static int16_t in[MI_READ_FRAMES * 2];
    for (;;) {
        if (S.skip || S.prev || S.play_pending) break;
        if (parked()) { idle_pump(); continue; }   /* a scan owns the card */
        size_t got = mi_ring_get(j.fifo, in, sizeof in);
        if (got == 0) {
            if (j.eof) break;
            (void)xSemaphoreTake(j.data, pdMS_TO_TICKS(20));
            vTaskDelay(1); /* a stale hint must not spin the broadcast task */
            continue;
        }
        xSemaphoreGive(j.space);
        uint64_t rs_cpu0 = mi_rs_cpu_us(rs);
        mi_rs_process(rs, in, got / 4, chunk_append, NULL);
        S.rs_ms += (uint32_t)((mi_rs_cpu_us(rs) - rs_cpu0) / 1000);
    }

    j.stop = true;
    xSemaphoreGive(j.space); /* unblock the producer if the ring filled */
    xSemaphoreTake(j.done, portMAX_DELAY); /* the task deletes itself after this */
    flush_pipeline();
    mi_rs_free(rs);
    src_job_free(&j);
}

static bool take_pending(const library_t *lib, uint32_t generation, uint32_t *index)
{
    xSemaphoreTake(S.lock, portMAX_DELAY);
    bool pending = S.play_pending;
    uint32_t got_index = S.pending_index;
    uint32_t got_generation = S.pending_generation;
    char pending_path[LIB_PATH_MAX];
    snprintf(pending_path, sizeof pending_path, "%s", S.pending_path);
    if (!pending) {
        xSemaphoreGive(S.lock);
        return false;
    }
    if (got_generation == generation) {
        S.play_pending = false;
        xSemaphoreGive(S.lock);
        *index = got_index;
        return true;
    }
    xSemaphoreGive(S.lock);

    /* A rescan may have replaced the generation while the request was pending.
     * The relative path is the stable key. Keep the request pending through the
     * old pass; it is consumed when the new generation's pass resolves it, and
     * dropped only once the pass is already on the current generation. */
    if (!lib) return false;
    lib_node_t n = library_find(lib, pending_path);
    if (n != LIB_NODE_NONE && library_kind(lib, n) == LIB_TRACK) {
        uint32_t fresh = library_track_index(lib, n);
        if (fresh != LIB_NODE_NONE) {
            xSemaphoreTake(S.lock, portMAX_DELAY);
            S.play_pending = false;
            xSemaphoreGive(S.lock);
            *index = fresh;
            return true;
        }
    }
    if (library_generation(lib) == library_current_generation()) {
        xSemaphoreTake(S.lock, portMAX_DELAY);
        S.play_pending = false;
        xSemaphoreGive(S.lock);
    }
    return false;
}

/* --------------------------------------------------------------- shuffle */

/* Free the order: the mode is off, or the generation that built it is gone. The
 * pointer is cleared under the lock the pass reads it with, then the memory is
 * released outside it, so no reader can still hold it. */
static void shuffle_forget(void)
{
    xSemaphoreTake(S.lock, portMAX_DELAY);
    uint32_t *order = S.shuffle.order;
    S.shuffle.order = NULL;
    S.shuffle.count = 0;
    S.shuffle.cursor = 0;
    S.shuffle.ready = false;
    S.shuffle_generation = 0;
    xSemaphoreGive(S.lock);
    free(order);
}

/* Move the order's cursor to a picked Track, so the next advance continues from the
 * pick instead of from where the order had got to. The order is not rebuilt. A pick
 * made while the mode is off, or before the order covers the whole Library, is ignored. */
static void shuffle_seek(uint32_t index)
{
    xSemaphoreTake(S.lock, portMAX_DELAY);
    if (S.shuffle.ready && index < S.shuffle.count) (void)mi_shuffle_seek(&S.shuffle, index);
    xSemaphoreGive(S.lock);
}

/* Start a pass with the shuffle order when the mode is on: build it here, at the top
 * of the pass, where the Library lease is held (ADR 0023). The Menu only flips a
 * flag, so this is where the allocation and the swaps run - never the UI thread.
 *
 * `have_current` says a Track of this generation is playing (a pass that restarted at
 * a boundary after the mode changed); the order is then anchored at it, so the first
 * advance takes the element after it. A fresh pass has no Track playing, so the order is
 * anchored at track index 0 like the linear pass, and *first_is_cursor tells the pass
 * to play the cursor rather than advance it.
 *
 * Returns true when the pass is shuffled. On an allocation failure the mode is turned
 * back off - mini_icecast_shuffle_enabled() must not claim a mode that is not running
 * - and the pass continues in broadcast order. */
/* ------------------------------------------------------------- the order */

static bool order_has(const uint32_t *items, uint32_t n, uint32_t index)
{
    for (uint32_t i = 0; i < n; i++)
        if (items[i] == index) return true;
    return false;
}

/* Where a track sits in the order, or n when it is not in it. */
static uint32_t order_position(const uint32_t *items, uint32_t n, uint32_t index)
{
    for (uint32_t i = 0; i < n; i++)
        if (items[i] == index) return i;
    return n;
}

static bool shuffle_pass_begin(uint32_t count, uint32_t generation, bool have_current,
                               uint32_t current, bool *first_is_cursor)
{
    *first_is_cursor = false;
    xSemaphoreTake(S.lock, portMAX_DELAY);
    bool on = S.shuffle_on;
    uint32_t rng = S.shuffle.rng;
    uint32_t seed = S.shuffle_seed;
    bool reuse = on && S.shuffle.ready && S.shuffle.count == count &&
                 S.shuffle_generation == generation;
    xSemaphoreGive(S.lock);
    if (!on) return false;

    if (reuse) {
        /* Same generation: the order stands. A pass that restarts mid-generation may
         * have moved the cursor, so put it back on the Track playing now. */
        if (have_current) shuffle_seek(current);
        *first_is_cursor = !have_current;
        return true;
    }

    size_t bytes = (size_t)count * sizeof(uint32_t);
    uint32_t *order = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!order) order = malloc(bytes);
    if (!order) {
        ESP_LOGE(TAG, "shuffle: no memory for the %u B order; the mode stays off, "
                      "playback continues in broadcast order", (unsigned)bytes);
        xSemaphoreTake(S.lock, portMAX_DELAY);
        S.shuffle_on = false;
        uint32_t *stale = S.shuffle.order;
        S.shuffle.order = NULL;
        S.shuffle.count = 0;
        S.shuffle.cursor = 0;
        S.shuffle.ready = false;
        S.shuffle_generation = 0;
        xSemaphoreGive(S.lock);
        free(stale);
        return false;
    }

    /* Build a private copy: the swaps and the RNG must not run under the lock, which
     * the Menu polls through mini_icecast_shuffle_enabled(). The copy is installed
     * only if the mode is still on afterwards; a seed that arrived before start()
     * is applied here, where the RNG is still 0. */
    mi_shuffle_t built = {0};
    built.rng = rng;
    if (!built.rng) mi_shuffle_seed(&built, seed);
    (void)mi_shuffle_build(&built, order, count, have_current ? current : 0);

    uint32_t *old = NULL;
    xSemaphoreTake(S.lock, portMAX_DELAY);
    bool install = S.shuffle_on;
    if (install) {
        old = S.shuffle.order;
        S.shuffle = built;
        S.shuffle_generation = generation;
    }
    xSemaphoreGive(S.lock);

    if (!install) { /* the mode was turned off while the order was being built */
        free(order);
        return false;
    }
    free(old);
    *first_is_cursor = !have_current;
    return true;
}

static void broadcast_task(void *arg)
{
    (void)arg;
    S.t0 = esp_timer_get_time();
    int quiet = 0;
    /* Positions in the broadcast order the pass has played. `prev` walks back through them and
     * `skip` walks forward; only at its end is a new position appended. They live
     * across passes so a pass that restarts when the shuffle mode changes keeps the
     * history `prev` walks, and are reset when the generation changes. */
    uint32_t history[TRACK_HISTORY];
    int history_count = 0;
    int history_pos = -1;
    bool back = false;
    uint32_t history_generation = 0;
    uint32_t history_tracks = 0;
    for (;;) {
        bool owned = false;
        xSemaphoreTake(S.lock, portMAX_DELAY);
        const library_t *lib = S.library;
        xSemaphoreGive(S.lock);
        if (!lib) {
            lib = library_acquire();
            owned = true;
        }
        uint32_t count = lib ? library_track_count(lib) : 0;
        xSemaphoreTake(S.lock, portMAX_DELAY);
        S.library_count = count;
        xSemaphoreGive(S.lock);

        if (!lib || !count) {
            clear_track();
            S.skip = false;
            S.prev = false;
            if (quiet++ % QUIET_PASSES == 0) {
                if (!lib) ESP_LOGW(TAG, "no Library generation; idling");
                else ESP_LOGW(TAG, "no Tracks in the Library (state %s); idling",
                              library_state_name(library_state(lib)));
            }
            if (owned) library_release(lib);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        quiet = 0;

        uint32_t generation = library_generation(lib);
        if (generation != history_generation || count != history_tracks) {
            /* A new generation: the positions the old broadcast order had mean nothing now. */
            history_count = 0;
            history_pos = -1;
            back = false;
            history_generation = generation;
            history_tracks = count;
        }

        /* The order this pass walks: a published change lands here, and an order
         * resolved for a generation that is gone is dropped rather than guessed. Only
         * this task swaps or frees the active array, so a pass reads one array for its
         * whole life. */
        bool order_dropped = false;
        const uint32_t *order = NULL;
        uint32_t walk = count;
        xSemaphoreTake(S.lock, portMAX_DELAY);
        if (S.order_pending_set) {
            free(S.order.items);
            S.order.items = S.order_pending.items;
            S.order.count = S.order_pending.count;
            S.order.generation = S.order_pending.generation;
            S.order_pending.items = NULL;
            S.order_pending.count = 0;
            S.order_pending.generation = 0;
            S.order_pending_set = false;
        }
        if (S.order.count && S.order.generation != generation) {
            ESP_LOGW(TAG, "order: resolved for generation %u, playing %u; back to the Library's",
                     (unsigned)S.order.generation, (unsigned)generation);
            free(S.order.items);
            S.order.items = NULL;
            S.order.count = 0;
            S.order.generation = 0;
            order_dropped = true;
        }
        order = S.order.items;
        walk = S.order.count ? S.order.count : count;
        xSemaphoreGive(S.lock);
        if (order_dropped && S.order_dropped_cb) S.order_dropped_cb(S.order_dropped_ctx);

        /* The Track this pass continues from, when one of this generation is playing:
         * a pass that restarts at a Track boundary after the mode changed. A fresh
         * pass has none and starts at the first Track of the broadcast order, as it always has. */
        xSemaphoreTake(S.lock, portMAX_DELAY);
        bool have_current = S.generation == generation && S.track_index < count;
        uint32_t current = S.track_index;
        xSemaphoreGive(S.lock);
        uint32_t current_pos = order ? order_position(order, walk, current) : current;
        bool have_pos = have_current && current_pos < walk;

        /* The shuffle order is built at the top of the pass, while the Library lease
         * is held; enabling the mode only flips a flag (ADR 0023). It permutes the
         * order being walked, so with a Playlist selected it shuffles that. */
        bool first_is_cursor = false;
        bool pass_shuffle = shuffle_pass_begin(walk, generation, have_pos,
                                               have_pos ? current_pos : 0, &first_is_cursor);
        int played = 0;

        for (;;) {
            xSemaphoreTake(S.lock, portMAX_DELAY);
            bool same_library = S.library ? S.library == lib
                                          : library_current_generation() == generation;
            bool shuffle_mode = S.shuffle_on;
            bool order_change = S.order_pending_set;
            xSemaphoreGive(S.lock);
            if (order_change) {
                /* A selection changed: end the pass at this Track boundary so the next
                 * pass walks the new order. No Track is interrupted. */
                break;
            }
            if (!same_library || library_generation(lib) != generation) {
                /* A new generation ends the pass; its order describes a Library that
                 * is gone, and the next pass builds a new one with the mode still on. */
                shuffle_forget();
                break;
            }
            if (shuffle_mode != pass_shuffle) {
                /* The Menu flipped the mode: end the pass here, at a Track boundary,
                 * so the next pass builds or drops the order. No Track is interrupted. */
                break;
            }

            uint32_t index;
            bool forced = take_pending(lib, generation, &index);
            if (forced) {
                /* A pick moves the order's cursor instead of rebuilding the order: the
                 * next advance continues from the picked Track (ADR 0023). */
                if (pass_shuffle) {
                    uint32_t p = order ? order_position(order, walk, index) : index;
                    if (p < walk) shuffle_seek(p);
                }
                first_is_cursor = false;
                if (history_count == TRACK_HISTORY) {
                    memmove(history, history + 1, sizeof history[0] * (TRACK_HISTORY - 1));
                    history_count--;
                }
                history[history_count++] = index;
                history_pos = history_count - 1;
                back = false;
            } else if (back) {
                if (history_pos > 0) history_pos--;
                else if (history_count > 0) history_pos = 0; /* no earlier track: replay */
                else break;
                index = history[history_pos];
            } else if (history_pos + 1 < history_count) {
                history_pos++;
                index = history[history_pos];
            } else {
                /* A new position in the broadcast order: the shuffle order's next element when the
                 * mode is on, else the position after the last one played. */
                bool wrapped = false;
                bool ordered = false;
                if (pass_shuffle) {
                    /* The order is read under the lock set(false) frees it with, so a
                     * toggle between the check above and here cannot dangle. */
                    xSemaphoreTake(S.lock, portMAX_DELAY);
                    if (S.shuffle.ready && S.shuffle.count == walk) {
                        uint32_t p = first_is_cursor ? mi_shuffle_current(&S.shuffle)
                                                     : mi_shuffle_next(&S.shuffle, &wrapped);
                        first_is_cursor = false;
                        if (p < walk) {
                            index = order ? order[p] : p;
                            ordered = true;
                        }
                    }
                    xSemaphoreGive(S.lock);
                }
                if (!ordered && order) {
                    /* The next Track in the selected order, wrapping at its end. A pick
                     * that is not in the order (it can only arrive that way if the order
                     * changed under it) restarts at the first row. */
                    uint32_t pos = history_count
                                       ? order_position(order, walk, history[history_count - 1])
                                       : walk;
                    if (pos >= walk) pos = 0;
                    else pos = (pos + 1) % walk;
                    wrapped = history_count && pos == 0;
                    index = order[pos];
                } else if (!ordered) {
                    index = history_count ? (history[history_count - 1] + 1) % count : 0;
                    wrapped = history_count && index == 0;
                }
                if (wrapped) {
                    /* A new lap starts with a fresh history, as the old directory
                     * pass did; `prev` at the first Track replays it. */
                    history_count = 0;
                    history_pos = -1;
                }
                if (history_count == TRACK_HISTORY) {
                    memmove(history, history + 1, sizeof history[0] * (TRACK_HISTORY - 1));
                    history_count--;
                    history_pos--;
                }
                history[history_count++] = index;
                history_pos = history_count - 1;
            }

            lib_node_t n = library_track_at(lib, index);
            if (n == LIB_NODE_NONE) break;
            char path[LIB_PATH_MAX];
            if (library_abspath(lib, n, path, sizeof path) == (size_t)-1) {
                ESP_LOGW(TAG, "track %u has no absolute path; stopping this pass", (unsigned)index);
                break;
            }
            set_track(lib, index, n);
            S.skip = false;
            S.prev = false;
            stream_file(path);
            played++;
            back = S.prev;
        }

        if (owned) library_release(lib);
        if (!played) {
            if (quiet++ % QUIET_PASSES == 0)
                ESP_LOGW(TAG, "no Track could be played from the Library");
            vTaskDelay(pdMS_TO_TICKS(2000));
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
        /* The decoder and resampler's share of the same window. Reading and then
         * clearing each cell is not a transaction with the broadcast task: a tick
         * that lands between the read and the clear is dropped, which biases one
         * line low by less than a millisecond, never attributes it to the wrong
         * stage. */
        uint32_t dec = S.dec_ms, io = S.dec_io_ms, rs = S.rs_ms, eq = S.eq_ms;
        S.dec_ms = 0;
        S.dec_io_ms = 0;
        S.rs_ms = 0;
        S.eq_ms = 0;
        ESP_LOGI(TAG, "decode %" PRIu32 " ms (of which SD %" PRIu32 " ms), resample %" PRIu32
                      " ms, tone %" PRIu32 " ms = %.1f%% / %.1f%% / %.1f%% of one core",
                 dec, io, rs, eq, 100.0 * (double)(dec - (io < dec ? io : dec)) / TELEMETRY_PERIOD_MS,
                 100.0 * (double)rs / TELEMETRY_PERIOD_MS,
                 100.0 * (double)eq / TELEMETRY_PERIOD_MS);
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
    if (!cfg) {
        if (err && err_size) snprintf(err, err_size, "no configuration given");
        return ESP_ERR_INVALID_ARG;
    }
    /* Resolve-and-discard. The defaults, the client limit and the format
     * arithmetic are mi_station's, so this cannot disagree with start(). */
    return mi_station_resolve(cfg, NULL, err, err_size);
}

esp_err_t mini_icecast_start(const mini_icecast_cfg_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;

    /* Resolve once, before anything is allocated: a format that cannot be
     * served is refused without touching the heap or the network. */
    char err[192];
    if (mi_station_resolve(cfg, &S.station, err, sizeof err) != ESP_OK) {
        ESP_LOGE(TAG, "station configuration refused: %s", err);
        return ESP_ERR_INVALID_ARG;
    }
    S.library = cfg->library;

    /* Neutral tone: the bands and the trim start at 0 dB, the same as the screen's
     * knobs. A hot source is the owner's to trim down. */
    tone_eq_init(&S.eq, S.station.out_hz);
    for (int b = 0; b < TONE_EQ_BANDS; b++) S.eq_db[b] = 0.0f;
    S.eq_gain_db = TONE_EQ_GAIN_DEFAULT;

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

    /* /stream is the one route on the plain listener: port 80, no TLS, ever
     * (front door R2 - hardware players and car head units cannot trust a private
     * CA). /api/play is the app's, so it stays on the TLS listener. */
    esp_err_t err2 = http_server_register_route_on(HTTP_LISTENER_PLAIN, "/stream", stream_route,
                                                  NULL);
    if (err2 == ESP_OK)
        err2 = http_server_register_route_method("/api/play", "POST", play_route, NULL);
    if (err2 != ESP_OK) {
        ESP_LOGE(TAG, "cannot register the broadcast routes: %s", esp_err_to_name(err2));
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
    ESP_LOGI(TAG, "GET /stream is ready on port 80, beside the app");
    return ESP_OK;
}

void mini_icecast_set_order(const uint32_t *indices, uint32_t count, uint32_t generation)
{
    uint32_t *copy = NULL;
    if (indices && count) {
        size_t bytes = (size_t)count * sizeof *copy;
        copy = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!copy) copy = malloc(bytes);
        if (!copy) {
            ESP_LOGE(TAG, "order: no memory for %u B; the order in force stays",
                     (unsigned)bytes);
            return;
        }
        memcpy(copy, indices, bytes);
    }
    if (!S.lock) {
        /* Before start(): there is no pass yet; the order is kept for the first one. */
        free(S.order_pending.items);
        S.order_pending.items = copy;
        S.order_pending.count = copy ? count : 0;
        S.order_pending.generation = copy ? generation : 0;
        S.order_pending_set = true;
        return;
    }
    xSemaphoreTake(S.lock, portMAX_DELAY);
    free(S.order_pending.items);
    S.order_pending.items = copy;
    S.order_pending.count = copy ? count : 0;
    S.order_pending.generation = copy ? generation : 0;
    S.order_pending_set = true;
    xSemaphoreGive(S.lock);
}

bool mini_icecast_order_active(void)
{
    if (!S.lock) {
        return S.order_pending_set ? S.order_pending.count > 0 : S.order.count > 0;
    }
    xSemaphoreTake(S.lock, portMAX_DELAY);
    bool on = S.order_pending_set ? S.order_pending.count > 0 : S.order.count > 0;
    xSemaphoreGive(S.lock);
    return on;
}

bool mini_icecast_order_contains(uint32_t track_index)
{
    if (!S.lock) return true;
    xSemaphoreTake(S.lock, portMAX_DELAY);
    const uint32_t *items = S.order_pending_set ? S.order_pending.items : S.order.items;
    uint32_t n = S.order_pending_set ? S.order_pending.count : S.order.count;
    bool in = !n || order_has(items, n, track_index);
    xSemaphoreGive(S.lock);
    return in;
}

void mini_icecast_set_order_dropped_cb(void (*cb)(void *ctx), void *ctx)
{
    if (S.lock) xSemaphoreTake(S.lock, portMAX_DELAY);
    S.order_dropped_cb = cb;
    S.order_dropped_ctx = ctx;
    if (S.lock) xSemaphoreGive(S.lock);
}

bool mini_icecast_play(const char *relpath)
{
    if (!relpath || !relpath[0]) return false;
    xSemaphoreTake(S.lock, portMAX_DELAY);
    const library_t *lib = S.library;
    xSemaphoreGive(S.lock);
    bool owned = false;
    if (!lib) {
        lib = library_acquire();
        owned = true;
    }
    if (!lib) return false;
    lib_node_t n = library_find(lib, relpath);
    if (n == LIB_NODE_NONE || library_kind(lib, n) != LIB_TRACK) {
        if (owned) library_release(lib);
        return false;
    }
    uint32_t index = library_track_index(lib, n);
    if (index == LIB_NODE_NONE) {
        if (owned) library_release(lib);
        return false;
    }
    bool dropped = false;
    xSemaphoreTake(S.lock, portMAX_DELAY);
    const uint32_t *walking = S.order_pending_set ? S.order_pending.items : S.order.items;
    uint32_t walking_n = S.order_pending_set ? S.order_pending.count : S.order.count;
    if (walking_n && !order_has(walking, walking_n, index)) {
        /* The pick is outside the Playlist being walked: the walk is over. Published
         * like any other order change, so the pass swaps it at the next boundary. */
        free(S.order_pending.items);
        S.order_pending.items = NULL;
        S.order_pending.count = 0;
        S.order_pending.generation = 0;
        S.order_pending_set = true;
        dropped = true;
    }
    snprintf(S.pending_path, sizeof S.pending_path, "%s", relpath);
    S.pending_index = index;
    S.pending_generation = library_generation(lib);
    S.play_pending = true;
    S.skip = false;
    S.prev = false;
    xSemaphoreGive(S.lock);
    if (dropped && S.order_dropped_cb) S.order_dropped_cb(S.order_dropped_ctx);
    if (owned) library_release(lib);
    return true;
}

void mini_icecast_set_library(const library_t *lib)
{
    xSemaphoreTake(S.lock, portMAX_DELAY);
    S.library = lib;
    S.skip = false;
    S.prev = false;
    xSemaphoreGive(S.lock);
}

void mini_icecast_skip(void) { S.skip = true; }

void mini_icecast_prev(void) { S.prev = true; }

/* ---------------------------------------------------------- shuffle mode */

void mini_icecast_shuffle_seed(uint32_t seed)
{
    /* Before start() there is no lock and no broadcast task yet: keep the seed for
     * the first build. appliance.c seeds after the services start, but a boot path
     * that seeds earlier must not crash on an uncreated mutex. */
    if (!S.lock) {
        S.shuffle_seed = seed;
        return;
    }
    xSemaphoreTake(S.lock, portMAX_DELAY);
    S.shuffle_seed = seed;
    mi_shuffle_seed(&S.shuffle, seed);
    xSemaphoreGive(S.lock);
}

void mini_icecast_shuffle_set(bool on)
{
    /* Only the flag is flipped here: the order is built (or freed) by the broadcast
     * task at the next Track boundary, under its Library lease (ADR 0023). */
    xSemaphoreTake(S.lock, portMAX_DELAY);
    S.shuffle_on = on;
    uint32_t *order = NULL;
    if (!on) {
        order = S.shuffle.order;
        S.shuffle.order = NULL;
        S.shuffle.count = 0;
        S.shuffle.cursor = 0;
        S.shuffle.ready = false;
        S.shuffle_generation = 0;
    }
    xSemaphoreGive(S.lock);
    free(order);
}

bool mini_icecast_shuffle_enabled(void)
{
    xSemaphoreTake(S.lock, portMAX_DELAY);
    bool on = S.shuffle_on;
    xSemaphoreGive(S.lock);
    return on;
}

/* The screen's four knobs. The values are targets for the broadcast task's next chunk;
 * the filter state and the slew belong to that task, so these only publish a float. The
 * ranges are enforced where the values are consumed (tone_eq_set_band/gain). */
void mini_icecast_eq_set(int band, float db)
{
    if (band < 0 || band >= TONE_EQ_BANDS) return;
    S.eq_db[band] = db;
}

void mini_icecast_gain_set(float db)
{
    S.eq_gain_db = db;
}

uint32_t mini_icecast_lag_ms(void) { return S.lag_ms; }

void mini_icecast_status(mini_icecast_status_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof *out);
    out->format = MI_FMT_NONE;
    xSemaphoreTake(S.lock, portMAX_DELAY);
    snprintf(out->title, sizeof out->title, "%s", live_title());
    snprintf(out->file, sizeof out->file, "%s", S.file);
    snprintf(out->path, sizeof out->path, "%s", S.path);
    out->format = S.cur_fmt;
    out->generation = S.generation;
    out->track_index = S.track_index;
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
