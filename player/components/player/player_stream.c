/*
 * player_stream - the socket, the frames and the ring.
 *
 * One lock covers the machine and the ring: the stream task pushes, the output task pops, and
 * the light reads a copy. The board's socket is polled, never blocked on, so a quiet second on
 * the wire costs a tick and not a stalled task.
 */
#include "player_stream.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "handover.h"
#include "player_board.h"
#include "player_http.h"
#include "player_net.h"

static const char *TAG = "stream";

#define STALL_MS     PLAYER_STREAM_STALL_MS
#define NOT_MP3_BYTES 4096

typedef struct {
    player_stream_state_t state;
    bool fault;
    int fd;
    char host[64];
    uint32_t retries;
    uint32_t retry_at_ms;
    uint32_t last_data_ms;
    uint32_t last_frame_ms;
    uint32_t bytes;      /* body bytes taken from the socket */
    uint32_t body_bytes; /* bytes handed to the framer since the head */
    uint32_t frames;
    player_http_t http;
    player_mp3_t mp3;
    player_ring_t ring;
} machine_t;

static machine_t M;
static SemaphoreHandle_t s_lock;

static void player_stream_task(void *arg);

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static bool lock(void)
{
    if (!s_lock) return true;
    return xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE;
}

static void unlock(void)
{
    if (s_lock) xSemaphoreGive(s_lock);
}

static bool reached(uint32_t now, uint32_t when) { return (int32_t)(now - when) >= 0; }

static void close_fd(void)
{
    if (M.fd >= 0) player_board_stream_close(M.fd);
    M.fd = -1;
}

static void on_frame(void *ctx, const uint8_t *frame, size_t len)
{
    (void)ctx;
    player_ring_push(&M.ring, frame, len);
    M.frames++;
}

static void feed_body(const uint8_t *data, size_t len, uint32_t now)
{
    M.body_bytes += (uint32_t)len;
    /* The machine's clock, which is the caller's: stamping this with a fresh reading would
     * mix two clocks and turn the stall comparison into nonsense. */
    if (player_mp3_feed(&M.mp3, data, len, on_frame, NULL) > 0) M.last_frame_ms = now;
    if (!player_mp3_streaming(&M.mp3) && M.body_bytes > NOT_MP3_BYTES) {
        /* Something is wrong that retrying will not fix (R26). */
        ESP_LOGE(TAG, "not MPEG audio after %u bytes: fault", (unsigned)M.body_bytes);
        M.fault = true;
        M.state = PLAYER_STREAM_FAULT;
        close_fd();
    }
}

static void backoff(uint32_t now)
{
    close_fd();
    M.state = PLAYER_STREAM_BACKOFF;
    uint32_t wait = (M.retries == 0) ? PLAYER_STREAM_BACKOFF_FIRST_MS : PLAYER_STREAM_BACKOFF_MS;
    M.retries++;
    M.retry_at_ms = now + wait;
    M.last_data_ms = now;
    ESP_LOGW(TAG, "retrying in %u ms", (unsigned)wait);
}

static void connect_and_ask(uint32_t now)
{
    M.fd = player_board_stream_connect(M.host, 80);
    if (M.fd < 0) {
        ESP_LOGW(TAG, "no connection to %s", M.host);
        backoff(now);
        return;
    }

    char req[128];
    int n = snprintf(req, sizeof req, "GET /stream HTTP/1.0\r\nHost: %s\r\n\r\n", M.host);
    if (n <= 0 || player_board_stream_send(M.fd, (const uint8_t *)req, (size_t)n) < 0) {
        backoff(now);
        return;
    }

    player_http_init(&M.http);
    player_mp3_init(&M.mp3);
    M.body_bytes = 0;
    M.last_data_ms = now;
    M.last_frame_ms = now;
    M.state = PLAYER_STREAM_HEAD;
}

/* Drain what the socket has, not one buffer of it: the station format is 256 kbps, and a
 * single 512-byte read per 50 ms tick is 10 KB/s - a player that parses the head and then
 * falls behind the audio for ever. Bounded so a flood cannot starve the tick. */
static void pump(uint32_t now)
{
    uint8_t buf[512];
    for (int reads = 0; reads < 32; reads++) {
        int n = player_board_stream_poll(M.fd, buf, sizeof buf);
        if (n < 0) {
            ESP_LOGW(TAG, "the stream ended");
            backoff(now);
            return;
        }
        if (n == 0) {
            if (now - M.last_data_ms >= STALL_MS) {
                ESP_LOGW(TAG, "no bytes for %u s: the connection is dead",
                         (unsigned)(STALL_MS / 1000));
                backoff(now);
            }
            return;
        }
        M.last_data_ms = now;

        if (M.state == PLAYER_STREAM_HEAD) {
            size_t off = 0;
            int r = player_http_feed(&M.http, buf, (size_t)n, &off);
            if (r < 0) {
                ESP_LOGW(TAG, "the K7 did not answer HTTP");
                backoff(now);
                return;
            }
            if (r == 0) continue;
            if (M.http.status != 200) {
                ESP_LOGW(TAG, "the K7 answered %d", M.http.status);
                backoff(now);
                return;
            }
            M.state = PLAYER_STREAM_BODY;
            M.retries = 0; /* a connection that delivered starts the schedule again */
            M.bytes += (uint32_t)((size_t)n - off);
            feed_body(buf + off, (size_t)n - off, now);
        } else {
            M.bytes += (uint32_t)n;
            feed_body(buf, (size_t)n, now);
        }
        if (M.state == PLAYER_STREAM_FAULT) return;

        /* The frame stall: bytes are arriving, but nothing is being decoded. */
        if (M.state == PLAYER_STREAM_BODY &&
            now - M.last_frame_ms >= PLAYER_STREAM_FRAME_STALL_MS) {
            ESP_LOGW(TAG, "no frame for %u s (%u bytes/s): the stream is wedged, reconnecting",
                     (unsigned)(PLAYER_STREAM_FRAME_STALL_MS / 1000), (unsigned)M.body_bytes);
            backoff(now);
            return;
        }
    }
}

void player_stream_tick(uint32_t now)
{
    lock();
    if (M.state == PLAYER_STREAM_FAULT) {
        unlock();
        return;
    }

    char host[64];
    if (!player_net_host(host, sizeof host)) {
        close_fd();
        M.state = PLAYER_STREAM_IDLE;
        unlock();
        return;
    }
    if (strcmp(host, M.host) != 0) {
        snprintf(M.host, sizeof M.host, "%s", host);
        close_fd();
        M.state = PLAYER_STREAM_IDLE;
        M.retries = 0;
    }

    switch (M.state) {
    case PLAYER_STREAM_IDLE: connect_and_ask(now); break;
    case PLAYER_STREAM_HEAD:
    case PLAYER_STREAM_BODY: pump(now); break;
    case PLAYER_STREAM_BACKOFF:
        if (reached(now, M.retry_at_ms)) M.state = PLAYER_STREAM_IDLE;
        break;
    case PLAYER_STREAM_CONNECTING:
    case PLAYER_STREAM_FAULT: break;
    }
    unlock();
}

void player_stream_init(void)
{
    memset(&M, 0, sizeof M);
    M.fd = -1;
    M.state = PLAYER_STREAM_IDLE;
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
}

void player_stream_status(player_stream_status_t *out)
{
    if (!out) return;
    lock();
    memset(out, 0, sizeof *out);
    out->state = M.state;
    out->fault = M.fault;
    snprintf(out->host, sizeof out->host, "%s", M.host);
    out->bytes = M.bytes;
    out->frames = M.frames;
    out->ring_used = player_ring_used(&M.ring);
    out->ring_capacity = player_ring_capacity();
    out->ring_flushes = player_ring_flushes(&M.ring);
    out->ring_dropped_bytes = player_ring_dropped_bytes(&M.ring);
    out->retries = M.retries;
    if (M.mp3.info_set) {
        out->rate = M.mp3.info.rate;
        out->bitrate = M.mp3.info.bitrate;
    }
    unlock();
}

bool player_stream_pop(uint8_t *out, size_t cap, size_t *len)
{
    if (!out || !cap) return false;
    lock();
    bool ok = player_ring_pop(&M.ring, out, cap, len);
    unlock();
    return ok;
}

const char *player_stream_state_name(player_stream_state_t state)
{
    switch (state) {
    case PLAYER_STREAM_CONNECTING: return "connecting";
    case PLAYER_STREAM_HEAD: return "head";
    case PLAYER_STREAM_BODY: return "streaming";
    case PLAYER_STREAM_BACKOFF: return "backoff";
    case PLAYER_STREAM_FAULT: return "fault";
    case PLAYER_STREAM_IDLE: break;
    }
    return "idle";
}

static void player_stream_task(void *arg)
{
    (void)arg;
    uint32_t logged = 0;
    for (;;) {
        player_stream_tick(now_ms());
        /* A line a second while a stream is running: the board's own evidence, and the only
         * way to see the frames without an output. */
        uint32_t now = now_ms();
        if (now - logged >= 1000) {
            logged = now;
            player_stream_status_t st;
            player_stream_status(&st);
            if (st.state == PLAYER_STREAM_BODY)
                ESP_LOGI(TAG, "streaming %s: %u Hz, %u kbps, %u frames, %u B, ring %u/%u KB, "
                              "flushed %u, lost %u B",
                         st.host, (unsigned)st.rate, (unsigned)st.bitrate,
                         (unsigned)st.frames, (unsigned)st.bytes, st.ring_used / 1024,
                         st.ring_capacity / 1024, st.ring_flushes, st.ring_dropped_bytes);
        }
        vTaskDelay(pdMS_TO_TICKS(PLAYER_STREAM_TICK_MS));
    }
}

void player_stream_start_task(void)
{
    if (xTaskCreatePinnedToCore(player_stream_task, "stream", 4096, NULL, 4, NULL, 0) != pdPASS)
        ESP_LOGE(TAG, "the stream task did not start");
}
