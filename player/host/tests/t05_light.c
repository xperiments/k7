/*
 * T05: the status light, host tier (k7player.md T5, R31-R32).
 *
 * The four states come from three machines, so each case puts the real machines in a state and
 * then asks the light what it would show - including the patterns, at the phases the spec names.
 * The board is the host's null light, which records the level it was told.
 */
#include "test_util.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "handover.h"
#include "player_board.h"
#include "player_light.h"
#include "player_net.h"
#include "espnow_stream.h"
#include "player_ota.h"
#include "player_playout.h"
#include "player_stream.h"
#include "player_update.h"

extern void host_player_reset(void);
extern void host_player_wifi_script(const char *ssid, bool ok, int disconnects, int reason);
extern void host_player_resolve(const char *ip);
extern void host_player_output_fail(bool fail);
extern uint8_t host_player_light(void);
extern void host_player_ota_record(bool present);
extern int host_player_light_sets(void);

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

/* The fixture's frames, or - for the fault case - frames that are not MP3 at all, handed to
 * the stream as the radio's callback would. Two frames a tick, so the ring fills over time. */
static const uint8_t *s_body;
static size_t s_body_len, s_body_off;
static uint16_t s_seq;
static bool s_noise;

static void deliver(int frames)
{
    while (frames-- > 0) {
        uint8_t pkts[ES_MAX_FRAGS * ES_MTU];
        size_t lens[ES_MAX_FRAGS];
        if (s_noise) {
            static uint8_t junk[417];
            memset(junk, 0x55, sizeof junk);
            size_t n = es_fragment(s_seq++, junk, sizeof junk, pkts, lens);
            for (size_t i = 0; i < n; i++) player_stream_packet(pkts + i * ES_MTU, lens[i]);
            continue;
        }
        if (s_body_off + 4 > s_body_len) return;
        size_t flen = mp3_frame_len(s_body + s_body_off);
        if (!flen) { s_body_off++; continue; } /* an ID3 tag or padding before the first sync */
        if (s_body_off + flen > s_body_len) return;
        size_t n = es_fragment(s_seq++, s_body + s_body_off, flen, pkts, lens);
        for (size_t i = 0; i < n; i++) player_stream_packet(pkts + i * ES_MTU, lens[i]);
        s_body_off += flen;
    }
}

/* Bring every machine up. `body` may be NULL: then nothing is sent. */
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
    s_noise = false;

    g_now = 0;
    player_net_init();
    player_stream_init();
    player_playout_init();
    player_light_init();
}

static void pump(int ticks)
{
    for (int i = 0; i < ticks; i++) {
        g_now += 50;
        deliver(2);
        player_net_tick(g_now);
        player_stream_tick(g_now);
        player_playout_tick();
        player_light_tick(g_now);
    }
}

/* No network at all: searching, and the 1 Hz pattern is a second long. */
static void test_searching(void)
{
    host_player_reset();
    g_now = 0;
    player_net_init();
    player_stream_init();
    player_playout_init();
    player_light_init();
    pump(2);

    CHECK_EQ(player_light_tick(0), PLAYER_LIGHT_SEARCHING, "no network, no K7: searching");
    CHECK_EQ(player_light_level(), PLAYER_LIGHT_BRIGHT, "the first half of the second is lit");
    CHECK_EQ(player_light_tick(400), PLAYER_LIGHT_SEARCHING, "still searching");
    CHECK_EQ(player_light_level(), PLAYER_LIGHT_BRIGHT, "still lit at 400 ms");
    CHECK_EQ(player_light_tick(500), PLAYER_LIGHT_SEARCHING, "half a second");
    CHECK_EQ(player_light_level(), 0, "and the light goes out");
    CHECK_EQ(player_light_tick(999), PLAYER_LIGHT_SEARCHING, "to the end of the second");
    CHECK_EQ(player_light_level(), 0, "still out");
    CHECK(player_light_tick(1000) != PLAYER_LIGHT_FAULT, "no fault was invented");
    CHECK(host_player_light_sets() > 0, "the board was told a level");
}

/* On a network, nothing playing: ready, and steady rather than blinking. */
static void test_ready(void)
{
    program(NULL, 0);
    pump(6); /* the network machine connects; the stream has nothing to read */

    CHECK_EQ(player_light_tick(0), PLAYER_LIGHT_READY, "a network and no stream: ready");
    CHECK_EQ(player_light_level(), PLAYER_LIGHT_DIM, "dim");
    CHECK_EQ(player_light_tick(700), PLAYER_LIGHT_READY, "and it does not blink");
    CHECK_EQ(player_light_level(), PLAYER_LIGHT_DIM, "dim all the same");
}

/* A stream playing: bright, and nothing to do with the network's name. */
static void test_playing(void)
{
    size_t body_len = 0;
    uint8_t *body = read_fixture("long-44100.mp3", &body_len);
    CHECK(body != NULL, "the fixture is readable");
    if (!body) return;

    program(body, body_len);

    player_light_t state = PLAYER_LIGHT_SEARCHING;
    int guard = 0;
    while (state != PLAYER_LIGHT_PLAYING && guard++ < 300) {
        pump(1);
        state = player_light_tick(g_now);
    }
    CHECK_EQ(state, PLAYER_LIGHT_PLAYING, "a stream with audio going out is playing");
    CHECK_EQ(player_light_level(), PLAYER_LIGHT_BRIGHT, "bright");
    CHECK_EQ(player_light_tick(g_now + 300), PLAYER_LIGHT_PLAYING, "and steady");
    CHECK_EQ(player_light_level(), PLAYER_LIGHT_BRIGHT, "still bright");

    free(body);
}

/* An image being written: Updating, two short flashes a second, over Ready and Play (R31, R38). */
static void test_updating(void)
{
    program(NULL, 0);
    pump(6);
    CHECK_EQ(player_light_tick(0), PLAYER_LIGHT_READY, "the player would be ready");

    player_ota_init();
    pu_begin_t begin = {
        .version = {0, 7, 0},
        .board = PU_BOARD_ESP32,
        .image_len = 4096,
        .sig_len = 70,
    };
    memset(begin.sha256, 0x33, sizeof begin.sha256);
    memset(begin.sig, 1, sizeof begin.sig);
    uint8_t pkt[PU_BEGIN_SIZE];
    CHECK_EQ(pu_begin_encode(&begin, pkt), PU_BEGIN_SIZE, "the begin encodes");
    const uint8_t k7[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    player_ota_packet(k7, pkt, sizeof pkt);
    player_ota_tick(g_now);
    CHECK(player_ota_updating(), "the begin was taken");

    CHECK_EQ(player_light_tick(50), PLAYER_LIGHT_UPDATING, "writing the image: updating");
    CHECK_EQ(player_light_level(), PLAYER_LIGHT_BRIGHT, "the first flash");
    CHECK_EQ(player_light_tick(150), PLAYER_LIGHT_UPDATING, "between the flashes");
    CHECK_EQ(player_light_level(), 0, "dark");
    CHECK_EQ(player_light_tick(250), PLAYER_LIGHT_UPDATING, "the second flash");
    CHECK_EQ(player_light_level(), PLAYER_LIGHT_BRIGHT, "lit");
    CHECK_EQ(player_light_tick(500), PLAYER_LIGHT_UPDATING, "and the rest of the second");
    CHECK_EQ(player_light_level(), 0, "dark again, not Ready's steady dim");

    /* Leave the machine where the next case expects it: a K7 that says nothing abandons. */
    player_ota_tick(g_now + PLAYER_OTA_GAP_MS + 1);
    CHECK(!player_ota_updating(), "and the act is over when the K7 goes quiet");
}

/* A body that is not audio: fault, and the 4 Hz pattern's two halves. */
static void test_fault(void)
{
    /* R26 over the radio: two seconds of frames that never decode is two firmwares disagreeing
     * about the format, so it is a Fault and not a wait. */
    program(NULL, 0);
    s_noise = true;

    player_light_t state = PLAYER_LIGHT_SEARCHING;
    int guard = 0;
    while (state != PLAYER_LIGHT_FAULT && guard++ < 400) {
        pump(1);
        state = player_light_tick(g_now);
    }
    CHECK_EQ(state, PLAYER_LIGHT_FAULT, "two seconds of undecodable frames is a fault");

    CHECK_EQ(player_light_tick(0), PLAYER_LIGHT_FAULT, "at the start of the cycle");
    CHECK_EQ(player_light_level(), PLAYER_LIGHT_BRIGHT, "lit");
    CHECK_EQ(player_light_tick(100), PLAYER_LIGHT_FAULT, "100 ms");
    CHECK_EQ(player_light_level(), PLAYER_LIGHT_BRIGHT, "still lit");
    CHECK_EQ(player_light_tick(130), PLAYER_LIGHT_FAULT, "past the half period");
    CHECK_EQ(player_light_level(), 0, "and out - four cycles a second");
}

/* An output that will not open is the other fault R31 names. */
static void test_no_output(void)
{
    size_t body_len = 0;
    uint8_t *body = read_fixture("long-44100.mp3", &body_len);
    if (!body) return;
    program(body, body_len);
    host_player_output_fail(true); /* after program: a fresh fake has no failure armed */

    player_light_t state = PLAYER_LIGHT_SEARCHING;
    int guard = 0;
    while (state != PLAYER_LIGHT_FAULT && guard++ < 300) {
        pump(1);
        state = player_light_tick(g_now);
    }
    CHECK_EQ(state, PLAYER_LIGHT_FAULT, "an output that cannot open is a fault");
    host_player_output_fail(false);

    free(body);
}

int main(void)
{
    test_searching();
    test_ready();
    test_updating();
    test_playing();
    test_fault();
    test_no_output();
    TEST_END();
}
