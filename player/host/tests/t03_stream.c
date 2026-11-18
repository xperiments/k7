/*
 * T03: the stream client, host tier (k7player.md T1-T3).
 *
 * The K7's own answer, byte by byte: a head split across reads, MPEG frames whose boundaries
 * fall wherever the socket put them, a ring that discards the oldest instead of growing, a
 * connection that ends and one that goes quiet, and a body that is not audio at all. The
 * bytes are a scripted transport; the policy is the firmware's own.
 */
#include "test_util.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "handover.h"
#include "player_board.h"
#include "player_net.h"
#include "player_ring.h"
#include "player_stream.h"

extern void host_player_reset(void);
extern void host_player_wifi_script(const char *ssid, bool ok, int disconnects, int reason);
extern void host_player_resolve(const char *ip);
extern void host_player_stream_script(const uint8_t *data, size_t len, size_t chunk);
extern void host_player_stream_stall(void);
extern size_t host_player_stream_request(char *out, size_t cap);
extern int host_player_stream_connects(void);
extern void host_player_stream_reset(void);

static uint32_t g_now;

/* One MPEG-1 Layer III frame at 128 kbps / 44.1 kHz: 417 bytes, header 0xFF 0xFB 0x90 0x00. */
#define FRAME_LEN 417

static size_t mp3_frame(uint8_t *out, uint8_t tag)
{
    memset(out, 0x11, FRAME_LEN);
    out[0] = 0xFF;
    out[1] = 0xFB; /* MPEG-1, Layer III, no CRC */
    out[2] = 0x90; /* 128 kbps, 44.1 kHz, no padding */
    out[3] = 0x00; /* stereo */
    out[4] = tag;  /* so a frame is identifiable */
    return FRAME_LEN;
}

static size_t build_response(uint8_t *out, int frames, bool noise)
{
    static const char head[] = "HTTP/1.0 200 OK\r\nContent-Type: audio/mpeg\r\n"
                               "Connection: close\r\n\r\n";
    size_t n = 0;
    memcpy(out, head, sizeof head - 1);
    n += sizeof head - 1;
    for (int i = 0; i < frames; i++) n += mp3_frame(out + n, (uint8_t)i);
    if (noise) {
        memset(out + n, 0x55, 5000);
        n += 5000;
    }
    return n;
}

/* Run both machines' ticks: the net has to have an address before the stream will connect. */
static void pump(int ticks)
{
    for (int i = 0; i < ticks; i++) {
        g_now += PLAYER_STREAM_TICK_MS;
        player_net_tick(g_now);
        player_stream_tick(g_now);
    }
}

/* Bring both machines up with a scripted K7 and, optionally, bytes already waiting or a
 * connection that never speaks. */
static void program(bool stall, const uint8_t *resp, size_t n, size_t chunk)
{
    host_player_reset();
    host_player_resolve("192.168.0.13");
    host_player_wifi_script("xperiments-2.4G", true, 0, 0);

    ho_record_t rec;
    memset(&rec, 0, sizeof rec);
    rec.has_home = true;
    snprintf(rec.home_ssid, sizeof rec.home_ssid, "%s", "xperiments-2.4G");
    snprintf(rec.home_pass, sizeof rec.home_pass, "%s", "viernes13");
    rec.has_ap = true;
    snprintf(rec.ap_ssid, sizeof rec.ap_ssid, "%s", "K7.local");
    snprintf(rec.ap_pass, sizeof rec.ap_pass, "%s", "12345678");
    snprintf(rec.host, sizeof rec.host, "%s", "k7");
    rec.ipv4 = 0xC0A8000Du;
    CHECK(player_board_record_save(&rec), "a record is stored");

    if (stall)
        host_player_stream_stall();
    else if (resp)
        host_player_stream_script(resp, n, chunk);

    g_now = 0;
    player_net_init();
    player_stream_init();
    pump(4);

    player_net_status_t st;
    player_net_status(&st);
    CHECK(st.state == PLAYER_NET_CONNECTED, "the network machine is connected");
}

/* The head split across reads, and frames whose boundaries fall wherever the reads do. */
static void test_byte_by_byte(void)
{
    uint8_t resp[4096];
    size_t n = build_response(resp, 3, false);
    program(false, resp, n, 1); /* a read of one byte at a time */

    player_stream_status_t st;
    player_stream_status(&st);
    int guard = 0;
    while (st.frames < 3 && guard++ < 4000) {
        pump(1);
        player_stream_status(&st);
    }

    /* The script ends with the third frame, and the same tick that emits it drains on to the
     * end of the connection - so the state is the body's or the backoff that followed it. */
    CHECK(st.state == PLAYER_STREAM_BODY || st.state == PLAYER_STREAM_BACKOFF,
          "the head was accepted and the body read");
    CHECK_EQ(st.frames, 3, "all three frames came out whole");
    CHECK_EQ(st.rate, 44100, "the rate came from the frame header, not an assumption");
    CHECK_EQ(st.bitrate, 128, "and so did the bit rate");
    CHECK_EQ(st.ring_used, 3 * (FRAME_LEN + 2), "and the ring holds them");

    char req[256];
    CHECK(host_player_stream_request(req, sizeof req) > 0, "a request was sent");
    CHECK(strncmp(req, "GET /stream HTTP/1.0\r\n", 21) == 0, "asking for the stream");
    CHECK(strstr(req, "Host: 192.168.0.13") != NULL, "from the address the network found");

    /* The frames are whole: the first five bytes of the oldest one are its header and tag. */
    uint8_t frame[PLAYER_RING_SLOT];
    size_t len = 0;
    CHECK(player_stream_pop(frame, sizeof frame, &len), "a frame can be taken");
    CHECK_EQ(len, FRAME_LEN, "at its own length");
    CHECK_EQ(frame[0], 0xFF, "with a sync");
    CHECK_EQ(frame[1], 0xFB, "and the Layer III header it was built with");
    CHECK_EQ(frame[4], 0x00, "the oldest frame first");
}

/* A body that arrives faster than one read per tick: the machine drains it rather than
 * falling behind it. This is the shape a 256 kbps stream has on a real socket. */
static void test_burst(void)
{
    uint8_t resp[4096];
    size_t n = build_response(resp, 6, false);
    program(false, resp, n, 512); /* 512-byte reads: two of them per tick are needed */

    player_stream_status_t st;
    player_stream_status(&st);
    int guard = 0;
    while (st.frames < 6 && guard++ < 20) {
        pump(1);
        player_stream_status(&st);
    }
    CHECK_EQ(st.frames, 6, "all six frames came out, not one per tick");
    CHECK_EQ(st.ring_used, 6 * (FRAME_LEN + 2), "and all six are in the ring");
}

/* A connection that stays open and keeps delivering, but never a frame: the wedge that
 * stalls the origin for every listener, and that a byte-based stall rule cannot see. */
static void test_frame_stall(void)
{
    static const char head[] = "HTTP/1.0 200 OK\r\nContent-Type: audio/mpeg\r\n\r\n";
    size_t len = sizeof head - 1 + 3000;
    uint8_t *resp = malloc(len);
    CHECK(resp != NULL, "a dribbling body");
    if (!resp) return;
    memcpy(resp, head, sizeof head - 1);
    memset(resp + sizeof head - 1, 0x55, 3000); /* not audio, and short of the fault's 4 KB */

    program(false, resp, len, 50);

    player_stream_status_t st;
    player_stream_status(&st);
    int guard = 0;
    while (st.state != PLAYER_STREAM_BACKOFF && guard++ < 200) {
        pump(1);
        player_stream_status(&st);
    }
    CHECK_EQ(st.state, PLAYER_STREAM_BACKOFF, "bytes but no frame for three seconds reconnects");
    CHECK_EQ(st.frames, 0, "nothing was decoded from it");

    free(resp);
}

/* The connection ends: 1 s, then 5 s, and it keeps trying. */
static void test_backoff(void)
{
    uint8_t resp[512];
    size_t n = build_response(resp, 1, false);
    program(false, resp, n, 64);

    player_stream_status_t st;
    player_stream_status(&st);
    int guard = 0;
    while (st.state != PLAYER_STREAM_BACKOFF && guard++ < 200) {
        pump(1);
        player_stream_status(&st);
    }
    CHECK_EQ(st.state, PLAYER_STREAM_BACKOFF, "a closed stream backs off");
    CHECK_EQ(st.frames, 1, "after the frame it did deliver");
    CHECK_EQ(host_player_stream_connects(), 1, "one connection so far");

    /* The first wait is 1 s. The backoff started inside the last pump, so give it its
     * second and check that it has not reconnected before then. */
    uint32_t started = g_now;
    while (host_player_stream_connects() == 1 && g_now < started + 900) pump(1);
    CHECK_EQ(host_player_stream_connects(), 1, "no reconnect before the first second");
    while (host_player_stream_connects() == 1 && g_now < started + 1200) pump(1);
    CHECK_EQ(host_player_stream_connects(), 2, "and one after it");

    /* The script was consumed by the first connection, so this one ends at once: the wait is
     * five seconds now. */
    uint32_t started2 = g_now;
    while (host_player_stream_connects() == 2 && g_now < started2 + 4000) pump(1);
    CHECK_EQ(host_player_stream_connects(), 2, "nothing at four seconds");
    while (host_player_stream_connects() == 2 && g_now < started2 + 6000) pump(1);
    CHECK_EQ(host_player_stream_connects(), 3, "and a retry at five");
}

/* Ten seconds without a byte ends the connection. */
static void test_stall(void)
{
    program(true, NULL, 0, 0);

    player_stream_status_t st;
    player_stream_status(&st);
    CHECK_EQ(st.state, PLAYER_STREAM_HEAD, "waiting for a head");

    pump(100); /* 5 s */
    player_stream_status(&st);
    CHECK_EQ(st.state, PLAYER_STREAM_HEAD, "five seconds is not a dead connection");

    /* To the first transition, not past it: the backoff that follows the stall reconnects a
     * second later, and a fixed pump would land after that. */
    int guard = 0;
    while (st.state == PLAYER_STREAM_HEAD && guard++ < 250) {
        pump(1);
        player_stream_status(&st);
    }
    CHECK_EQ(st.state, PLAYER_STREAM_BACKOFF, "ten quiet seconds is");
}

/* A body that is not audio is a fault, not a retry loop. */
static void test_not_audio(void)
{
    uint8_t resp[6000];
    size_t n = build_response(resp, 0, true);
    program(false, resp, n, 256);

    int guard = 0;
    player_stream_status_t st;
    player_stream_status(&st);
    while (st.state != PLAYER_STREAM_FAULT && guard++ < 200) {
        pump(1);
        player_stream_status(&st);
    }
    CHECK_EQ(st.state, PLAYER_STREAM_FAULT, "a body of noise is a fault");
    CHECK(st.fault, "and says so");
    CHECK_EQ(st.frames, 0, "no frame was invented from it");

    int connects = host_player_stream_connects();
    pump(400); /* twenty seconds of ticks */
    CHECK_EQ(host_player_stream_connects(), connects, "a fault does not retry");
}

/* The ring: fixed, two seconds deep, and an overrun is one discontinuity, not one per frame. */
static void test_ring(void)
{
    /* The depth that matters, in the seconds it buys at the formats this station has run: six at
     * the 128 kbps it runs now, three at the 256 it ran before. A ring that held less would not
     * absorb the outages the link is measured throwing at it. */
    CHECK(player_ring_capacity() >= 6u * 16000u, "the ring holds six seconds at 128 kbps");
    CHECK(player_ring_capacity() >= 3u * 32000u, "and three seconds at 256 kbps");

    player_ring_t r;
    player_ring_init(&r);
    uint8_t frame[FRAME_LEN];
    memset(frame, 0x11, sizeof frame);
    frame[0] = 0xFF;
    frame[1] = 0xFB;
    size_t per = sizeof frame + 2;

    /* Fill it exactly: nothing is thrown away while the ring can hold the audio. */
    unsigned fit = player_ring_capacity() / per;
    for (unsigned i = 0; i < fit; i++)
        CHECK(player_ring_push(&r, frame, sizeof frame), "a frame fits");
    CHECK_EQ(player_ring_used(&r), fit * per, "the ring is full and nothing was discarded");
    CHECK_EQ(player_ring_flushes(&r), 0, "no flush for audio that fits");

    /* One more frame than it can hold is an outage it cannot ride: the ring clears once. */
    CHECK(player_ring_push(&r, frame, sizeof frame), "a push to a full ring still succeeds");
    CHECK_EQ(player_ring_flushes(&r), 1, "the ring was cleared once");
    CHECK_EQ(player_ring_dropped_bytes(&r), fit * per, "and it says how much audio went");
    CHECK_EQ(player_ring_used(&r), per, "the ring starts again from the newest frame");

    /* What survived the flush is whole, and the next frame after it is still whole. */
    uint8_t out[PLAYER_RING_SLOT];
    size_t len = 0;
    CHECK(player_ring_pop(&r, out, sizeof out, &len), "the newest frame is there");
    CHECK_EQ(len, FRAME_LEN, "at its length");
    CHECK_EQ(out[0], 0xFF, "with its sync intact");
    CHECK_EQ(out[1], 0xFB, "and its header");
    CHECK(!player_ring_pop(&r, out, sizeof out, &len), "an empty ring says so rather than blocking");

    /* A long outage is a handful of discontinuities, not one per frame: ten ringfuls pushed
     * as one stream cost at most the ten flushes they overflowed. */
    player_ring_init(&r);
    for (unsigned i = 0; i < fit * 10; i++)
        player_ring_push(&r, frame, sizeof frame);
    CHECK(player_ring_flushes(&r) <= 10, "a long burst is a few discontinuities, not hundreds");

    /* A frame too big for the largest legal one is refused, not truncated. */
    static uint8_t big[PLAYER_RING_SLOT + 1];
    CHECK(!player_ring_push(&r, big, sizeof big), "an oversized frame is refused");
}

int main(void)
{
    test_byte_by_byte();
    test_burst();
    test_frame_stall();
    test_backoff();
    test_stall();
    test_not_audio();
    test_ring();
    TEST_END();
}
