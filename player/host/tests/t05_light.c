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
#include "player_playout.h"
#include "player_stream.h"

extern void host_player_reset(void);
extern void host_player_wifi_script(const char *ssid, bool ok, int disconnects, int reason);
extern void host_player_resolve(const char *ip);
extern void host_player_stream_script(const uint8_t *data, size_t len, size_t chunk);
extern void host_player_stream_stall(void);
extern void host_player_output_fail(bool fail);
extern uint8_t host_player_light(void);
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

static uint8_t *response_with(const uint8_t *body, size_t body_len, size_t *out_len)
{
    static const char head[] = "HTTP/1.0 200 OK\r\nContent-Type: audio/mpeg\r\n"
                               "Connection: close\r\n\r\n";
    size_t head_len = sizeof head - 1;
    uint8_t *resp = malloc(head_len + body_len);
    if (!resp) return NULL;
    memcpy(resp, head, head_len);
    memcpy(resp + head_len, body, body_len);
    *out_len = head_len + body_len;
    return resp;
}

/* Bring every machine up with a scripted K7. `resp` may be NULL: then nothing is sent. */
static void program(const uint8_t *resp, size_t len, size_t chunk)
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

    if (resp) host_player_stream_script(resp, len, chunk);

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
    program(NULL, 0, 0);
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

    size_t resp_len = 0;
    uint8_t *resp = response_with(body, body_len, &resp_len);
    program(resp, resp_len, 100);

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

    free(resp);
    free(body);
}

/* A body that is not audio: fault, and the 4 Hz pattern's two halves. */
static void test_fault(void)
{
    static const char head[] = "HTTP/1.0 200 OK\r\nContent-Type: audio/mpeg\r\n\r\n";
    size_t len = sizeof head - 1 + 5000;
    uint8_t *resp = malloc(len);
    CHECK(resp != NULL, "a noise body");
    if (!resp) return;
    memcpy(resp, head, sizeof head - 1);
    memset(resp + sizeof head - 1, 0x55, 5000);

    program(resp, len, 256);

    player_light_t state = PLAYER_LIGHT_SEARCHING;
    int guard = 0;
    while (state != PLAYER_LIGHT_FAULT && guard++ < 300) {
        pump(1);
        state = player_light_tick(g_now);
    }
    CHECK_EQ(state, PLAYER_LIGHT_FAULT, "noise is a fault, not a retry loop");

    CHECK_EQ(player_light_tick(0), PLAYER_LIGHT_FAULT, "at the start of the cycle");
    CHECK_EQ(player_light_level(), PLAYER_LIGHT_BRIGHT, "lit");
    CHECK_EQ(player_light_tick(100), PLAYER_LIGHT_FAULT, "100 ms");
    CHECK_EQ(player_light_level(), PLAYER_LIGHT_BRIGHT, "still lit");
    CHECK_EQ(player_light_tick(130), PLAYER_LIGHT_FAULT, "past the half period");
    CHECK_EQ(player_light_level(), 0, "and out - four cycles a second");

    free(resp);
}

/* An output that will not open is the other fault R31 names. */
static void test_no_output(void)
{
    size_t body_len = 0;
    uint8_t *body = read_fixture("long-44100.mp3", &body_len);
    if (!body) return;
    size_t resp_len = 0;
    uint8_t *resp = response_with(body, body_len, &resp_len);

    program(resp, resp_len, 100);
    host_player_output_fail(true); /* after program: a fresh fake has no failure armed */

    player_light_t state = PLAYER_LIGHT_SEARCHING;
    int guard = 0;
    while (state != PLAYER_LIGHT_FAULT && guard++ < 300) {
        pump(1);
        state = player_light_tick(g_now);
    }
    CHECK_EQ(state, PLAYER_LIGHT_FAULT, "an output that cannot open is a fault");
    host_player_output_fail(false);

    free(resp);
    free(body);
}

int main(void)
{
    test_searching();
    test_ready();
    test_playing();
    test_fault();
    test_no_output();
    TEST_END();
}
