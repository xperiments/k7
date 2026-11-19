/*
 * T04: the line output, host tier (k7player.md T3, T6).
 *
 * The decoder is real - dr_mp3's frame decoder over frames from the repo's own fixture corpus -
 * and the output is the seam's null implementation, which counts what it is given. That is
 * enough to show the three things the spec fixes: the output opens at the stream's rate and not
 * at an assumed one, decoded audio reaches it, and a ring with nothing in it produces silence
 * rather than a stall.
 */
#include "test_util.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "handover.h"
#include "player_board.h"
#include "player_net.h"
#include "espnow_stream.h"
#include "player_playout.h"
#include "player_stream.h"

extern void host_player_reset(void);
extern void host_player_wifi_script(const char *ssid, bool ok, int disconnects, int reason);
extern void host_player_resolve(const char *ip);
extern int player_output_opens(void);
extern uint32_t player_output_rate(void);
extern uint32_t player_output_frames(void);
extern int32_t player_output_peak(void);

static uint32_t g_now;

static uint8_t *read_fixture(const char *name, size_t *len)
{
    static const char *dirs[] = {"player/host/fixtures/", "../player/host/fixtures/",
                                 "host/fixtures/", "../host/fixtures/",
                                 "../../host/fixtures/"};
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        char path[256];
        snprintf(path, sizeof path, "%s%s", dirs[i], name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t *buf = malloc((size_t)n);
        if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n) {
            fclose(f);
            *len = (size_t)n;
            return buf;
        }
        free(buf);
        fclose(f);
    }
    *len = 0;
    return NULL;
}

/* The fixture, as the K7's emitter would send it: whole MP3 frames, fragmented into ESP-NOW
 * packets and handed to the stream where the radio's callback would hand them. A couple of
 * frames a tick keeps the shape a live stream has - the ring fills over time, which is what
 * every case here is about. */
static const uint8_t *s_body;
static size_t s_body_len, s_body_off;
static uint16_t s_seq;

static void deliver(int frames)
{
    while (frames-- > 0 && s_body_off + 4 <= s_body_len) {
        size_t flen = mp3_frame_len(s_body + s_body_off);
        if (!flen) { s_body_off++; continue; } /* an ID3 tag or padding before the first sync */
        if (s_body_off + flen > s_body_len) break;
        uint8_t pkts[ES_MAX_FRAGS * ES_MTU];
        size_t lens[ES_MAX_FRAGS];
        size_t n = es_fragment(s_seq++, s_body + s_body_off, flen, pkts, lens);
        for (size_t i = 0; i < n; i++) player_stream_packet(pkts + i * ES_MTU, lens[i]);
        s_body_off += flen;
    }
}

static void pump(int ticks)
{
    for (int i = 0; i < ticks; i++) {
        g_now += 10;
        deliver(2);
        player_net_tick(g_now);
        player_stream_tick(g_now);
        player_playout_tick();
    }
}

static void program(const uint8_t *body, size_t len)
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

    s_body = body;
    s_body_len = len;
    s_body_off = 0;
    s_seq = 0;

    g_now = 0;
    player_net_init();
    player_stream_init();
    player_playout_init();
    pump(4);
}

/* A 44.1 kHz tone: the output opens at the frame header's rate and real audio reaches it. */
static void test_line_44100(void)
{
    size_t body_len = 0;
    uint8_t *body = read_fixture("long-44100.mp3", &body_len);
    CHECK(body != NULL, "the fixture is readable (run make -C host fixtures)");
    if (!body) return;

    program(body, body_len);
    pump(200);

    CHECK_EQ(player_output_opens(), 1, "the output was opened once");
    CHECK_EQ(player_output_rate(), 44100, "at the rate the first frame header gave");
    CHECK(player_output_frames() > 0, "and audio reached it (%u frames)",
          (unsigned)player_output_frames());
    CHECK(player_output_peak() > 1000, "which is a tone, not silence (peak %d)",
          (int)player_output_peak());

    player_playout_status_t st;
    player_playout_status(&st);
    CHECK(st.decoded > 10, "the decoder took the frames (%u)", (unsigned)st.decoded);
    CHECK_EQ(st.refused, 0, "and refused none of them");
    CHECK(st.primes >= 1, "and it waited for the ring to hold a buffer first (%u)", st.primes);

    free(body);
}

/* A 32 kHz station: the output follows the stream rather than assuming 44.1 (R27). */
static void test_line_32000(void)
{
    size_t body_len = 0;
    uint8_t *body = read_fixture("long-32000.mp3", &body_len);
    CHECK(body != NULL, "the 32 kHz fixture is readable");
    if (!body) return;

    program(body, body_len);
    pump(200);

    CHECK_EQ(player_output_rate(), 32000, "the output followed the station's rate");
    CHECK(player_output_peak() > 1000, "and still made sound");

    free(body);
}

/* No frames to play: silence, not a stall and not a stop. */
static void test_silence(void)
{
    size_t body_len = 0;
    uint8_t *body = read_fixture("tone-44100.mp3", &body_len);
    CHECK(body != NULL, "the fixture is readable");
    if (!body) return;

    program(body, body_len);

    /* Until there is something to play, then past the end of it. */
    player_playout_status_t st;
    player_playout_status(&st);
    int guard = 0;
    while (!st.open && guard++ < 200) {
        pump(1);
        player_playout_status(&st);
    }
    CHECK(st.open, "the output opened on the fixture's rate");

    /* The frames stop. Three seconds of no packets is what releases the prime (E8) - the
     * fixture is 0.65 s, too short to reach a 128 kbps station's buffer - and then the ring
     * plays out and the line gets silence. */
    pump(400);
    player_playout_status(&st);
    CHECK(st.silent > 0, "silence was written when there was nothing to play (%u frames)",
          (unsigned)st.silent);
    CHECK(player_output_peak() > 1000, "after the audio had played");

    free(body);
}

/* The buffer is the point: the output holds silence while the ring is shallow - even with audio
 * already in it - and plays only once it holds the 1.5 s an outage has to eat (R28). Without
 * this the ring's depth is whatever the network's bursts happened to leave. */
static void test_primes(void)
{
    size_t body_len = 0;
    uint8_t *body = read_fixture("long-44100.mp3", &body_len);
    CHECK(body != NULL, "the fixture is readable");
    if (!body) return;

    program(body, body_len);

    player_playout_status_t po;
    player_stream_status_t sr;
    bool held = false;
    int guard = 0;
    do {
        pump(1);
        player_playout_status(&po);
        player_stream_status(&sr);
        if (po.frames == 0 && sr.ring_used > 0) held = true;
    } while (po.frames == 0 && guard++ < 400);

    CHECK(po.primes >= 1, "it primed rather than playing the first frames it saw (%u)",
          (unsigned)po.primes);
    CHECK(held, "and held silence while it already had audio to play");
    CHECK(po.frames > 0, "then it played (%u PCM frames)", (unsigned)po.frames);

    free(body);
}

int main(void)
{
    test_line_44100();
    test_line_32000();
    test_silence();
    test_primes();
    TEST_END();
}
