/*
 * player_stream - the radio's packets, the MP3 framer and the ring.
 *
 * One lock covers the receiver, the framer and the ring: the radio's callback pushes and the
 * playout pops. Both sides are short - a fragment copy, a frame copy - so neither can starve
 * the other.
 */
#include "player_stream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "player_board.h"
#include "player_net.h"

static const char *TAG = "stream";

typedef struct {
    player_stream_state_t state;
    bool     fault;
    player_recv_t recv;
    void *recv_mem;
    player_ring_t ring;
    player_mp3_t  mp3;
    uint32_t bytes;
    uint32_t frames;
    uint32_t last_packet_ms;
    uint32_t first_packet_ms;
    uint32_t last_frame_ms;
    uint32_t last_status_ms;
    uint32_t now;      /* the last tick's clock: packets between ticks belong to that instant */
    bool     ever_received;
    char     host[64];
} machine_t;

static machine_t M;
static SemaphoreHandle_t s_lock;

static bool lock(void)
{
    if (!s_lock) return true;
    return xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE;
}

static void unlock(void)
{
    if (s_lock) xSemaphoreGive(s_lock);
}

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* A whole frame out of the receiver: the MP3 framer proves it is audio and reads the station's
 * rate out of its header, then it goes in the ring. Called with the lock held. */
static void push_frame(void *ctx, const uint8_t *frame, size_t len)
{
    (void)ctx;
    unsigned before = M.mp3.frames;
    player_mp3_feed(&M.mp3, frame, len, NULL, NULL);
    if (M.mp3.frames == before) return; /* it was not audio; the tick decides what that means */
    M.last_frame_ms = M.now;
    M.fault = false;
    if (player_ring_push(&M.ring, frame, len)) M.frames++;
}

void player_stream_packet(const uint8_t *pkt, size_t len)
{
    if (!pkt || !len) return;
    lock();
    uint32_t now = M.now; /* the harness and the board agree on the tick's clock, not on ours */
    if (!M.ever_received) {
        M.ever_received = true;
        M.first_packet_ms = now;
    }
    uint32_t resyncs = M.recv.resyncs;
    player_recv_packet(&M.recv, pkt, len, now, push_frame, NULL);
    if (M.recv.resyncs != resyncs) {
        /* A sender that restarted may also be a station at a new format: forget the header we
         * read, so the next frame says what this stream actually is. */
        player_mp3_init(&M.mp3);
        ESP_LOGI(TAG, "a new stream on the radio: sequence restarted");
    }
    M.bytes += (uint32_t)len;
    M.last_packet_ms = now;
    M.state = PLAYER_STREAM_BODY;
    unlock();
}

/* The status packet: the ring's room and what the stream is missing (E3). Built under the lock,
 * sent outside it. */
static void send_status(void)
{
    es_status_t st;
    uint8_t pkt[ES_MTU];
    size_t n;

    lock();
    uint32_t free_bytes = player_ring_capacity() - player_ring_used(&M.ring);
    player_recv_status(&M.recv, free_bytes, &st);
    n = es_status_encode(&st, pkt, sizeof pkt);
    unlock();

    if (n) player_board_espnow_send(pkt, n);
}

void player_stream_tick(uint32_t now)
{
    lock();
    M.now = now;
    player_recv_tick(&M.recv, now, push_frame, NULL);

    if (M.state == PLAYER_STREAM_BODY && now - M.last_packet_ms >= PLAYER_STREAM_SILENT_MS) {
        M.state = PLAYER_STREAM_IDLE;
        /* The Fault is "frames arrive and do not decode", so a radio that has gone quiet is
         * not one: the light says Ready until frames come back (found on the bench, when the
         * K7's association died and the player lit its Fault at a stream that had simply
         * stopped arriving). */
        M.fault = false;
    }

    /* R26: two seconds of frames that never decode is a firmware mismatch, not a link. */
    if (M.ever_received) {
        uint32_t since = M.last_frame_ms ? M.last_frame_ms : M.first_packet_ms;
        if (now - since >= PLAYER_STREAM_FAULT_MS && M.state == PLAYER_STREAM_BODY)
            M.fault = true;
    }
    unlock();

    /* A status packet goes out whether or not packets are arriving: it is how the K7 learns the
     * player is there at all, and the first one is what names it. */
    if (now - M.last_status_ms >= PLAYER_STREAM_STATUS_MS) {
        M.last_status_ms = now;
        send_status();
    }
}

void player_stream_init(void)
{
    memset(&M, 0, sizeof M);
    /* Twelve kilobytes of reassembly buffers, on the heap: .dram0.bss has no room for them and
     * the ring's ninety-six kilobytes are worth more (ADR 0102, EN-03). */
    M.recv_mem = malloc(((size_t)PLAYER_RECV_HELD * sizeof(player_recv_held_t)) +
                        ((size_t)PLAYER_RECV_PARTS * sizeof(player_recv_part_t)));
    if (M.recv_mem) {
        player_recv_init(&M.recv, M.recv_mem,
                         ((size_t)PLAYER_RECV_HELD * sizeof(player_recv_held_t)) +
                         ((size_t)PLAYER_RECV_PARTS * sizeof(player_recv_part_t)));
    } else {
        ESP_LOGE(TAG, "no memory for the receiver's buffers: the stream is deaf");
    }
    player_ring_init(&M.ring);
    player_mp3_init(&M.mp3);
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
    out->lost = M.recv.lost;
    out->dups = M.recv.dups;
    out->abandoned = M.recv.abandoned;
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
    case PLAYER_STREAM_BODY: return "receiving";
    case PLAYER_STREAM_IDLE: break;
    }
    return "waiting";
}

/* The board's radio hands packets to player_stream_packet, so this task only ticks the machine
 * and prints the census: the board's own evidence, and the only way to see the frames without
 * an output. */
static void player_stream_task(void *arg)
{
    (void)arg;
    uint32_t logged = 0;
    if (!player_board_espnow_start(player_stream_packet))
        ESP_LOGE(TAG, "the radio did not start: the player cannot hear the K7");

    for (;;) {
        uint32_t now = now_ms();
        player_stream_tick(now);

        if (now - logged >= 1000) {
            logged = now;
            player_stream_status_t st;
            player_stream_status(&st);
            char ip[32] = "";
            player_net_host(ip, sizeof ip);
            snprintf(M.host, sizeof M.host, "%s", ip);
            if (st.state == PLAYER_STREAM_BODY)
                ESP_LOGI(TAG, "receiving %s: %u Hz, %u kbps, %u frames, %u B, ring %u/%u KB, "
                              "flushed %u, lost %u, dups %u, assembled %u, dropped %u%s",
                         st.host, (unsigned)st.rate, (unsigned)st.bitrate, (unsigned)st.frames,
                         (unsigned)st.bytes, st.ring_used / 1024, st.ring_capacity / 1024,
                         st.ring_flushes, (unsigned)st.lost, (unsigned)st.dups,
                         (unsigned)M.recv.frames, (unsigned)M.recv.parts_dropped,
                         st.fault ? ", FAULT: the frames are not MP3" : "");
        }
        vTaskDelay(pdMS_TO_TICKS(PLAYER_STREAM_TICK_MS));
    }
}

void player_stream_start_task(void)
{
    if (xTaskCreatePinnedToCore(player_stream_task, "stream", 6144, NULL, 4, NULL, 0) != pdPASS)
        ESP_LOGE(TAG, "the stream task did not start");
}
