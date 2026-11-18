/*
 * T42: the share window, host tier.
 *
 * The window's policy is driven by a fake clock and a fake radio. The fake records every
 * frame it was asked to send and the peer it was asked to register, and its curve is a
 * stand-in (`pub = priv ^ 0xA5`, `shared = priv ^ peer ^ 0x3C`), so what this file proves is
 * the policy and the transcript: that the beacon carries the window's own key, that the
 * record travels to the player who asked and to nobody else, that the key registered for it
 * is the one both keys derive, and that the window takes everything with it when it closes.
 * Whether real X25519 agrees with itself is PL-01's vectors and PL-09's two boards.
 */
#include "test_util.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app_board.h"
#include "handover.h"
#include "share.h"
#include "wifi_record.h"

/* The host adapter's test hook: deliver a frame through the callback the window registered. */
extern bool host_share_deliver(const uint8_t src[6], const uint8_t *data, size_t len);

static char s_dir[64];
static char s_sent[160];
static char s_peer[160];
static char s_state[160];

static const uint8_t k_a[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
static const uint8_t k_b[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};

static size_t read_all(const char *path, char *buf, size_t cap)
{
    buf[0] = '\0';
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = '\0';
    return n;
}

static int count_lines(const char *path)
{
    char buf[8192];
    size_t n = read_all(path, buf, sizeof buf);
    int lines = 0;
    for (size_t i = 0; i < n; i++)
        if (buf[i] == '\n') lines++;
    return lines;
}

static bool nth_line(const char *path, int idx, char *out, size_t cap)
{
    char buf[8192];
    if (read_all(path, buf, sizeof buf) == 0) return false;
    char *p = buf;
    for (int i = 0; i < idx; i++) {
        p = strchr(p, '\n');
        if (!p) return false;
        p++;
    }
    char *end = strchr(p, '\n');
    if (!end) return false;
    size_t n = (size_t)(end - p);
    if (n >= cap) n = cap - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static size_t unhex(const char *s, uint8_t *out, size_t cap)
{
    size_t n = 0;
    for (const char *p = s; p[0] && p[1]; p += 2) {
        int hi = hexval(p[0]), lo = hexval(p[1]);
        if (hi < 0 || lo < 0 || n >= cap) break;
        out[n++] = (uint8_t)(hi * 16 + lo);
    }
    return n;
}

typedef struct {
    bool     broadcast;
    uint8_t  dst[6];
    uint8_t  data[HO_FRAME_MAX];
    size_t   len;
} sent_t;

static bool sent_at(int idx, sent_t *out)
{
    char line[1024];
    if (!nth_line(s_sent, idx, line, sizeof line)) return false;
    char *bar = strchr(line, '|');
    if (!bar) return false;
    *bar = '\0';
    memset(out, 0, sizeof *out);
    out->broadcast = strcmp(line, "bc") == 0;
    if (!out->broadcast && unhex(line, out->dst, sizeof out->dst) != 6) return false;
    out->len = unhex(bar + 1, out->data, sizeof out->data);
    return out->len > 0;
}

static void fresh(void)
{
    snprintf(s_dir, sizeof s_dir, "/tmp/t42_share_XXXXXX");
    CHECK(mkdtemp(s_dir) != NULL, "a scratch directory");
    snprintf(s_sent, sizeof s_sent, "%s/sent", s_dir);
    snprintf(s_peer, sizeof s_peer, "%s/peer", s_dir);
    snprintf(s_state, sizeof s_state, "%s/state", s_dir);
    setenv("XTAPE_HOST_SHARE_SENT", s_sent, 1);
    setenv("XTAPE_HOST_SHARE_PEER", s_peer, 1);
    setenv("XTAPE_HOST_SHARE_STATE", s_state, 1);
    setenv("XTAPE_HOST_SHARE_CHANNEL", "6", 1);
    setenv("XTAPE_HOST_SHARE_AP_PASS", "87654321", 1);
    unsetenv("XTAPE_HOST_SHARE_FAIL");
}

static void provision(bool provisioned)
{
    wifi_record_t rec;
    memset(&rec, 0, sizeof rec);
    if (provisioned) {
        snprintf(rec.ssid, sizeof rec.ssid, "%s", "xperiments-2.4G");
        snprintf(rec.pass, sizeof rec.pass, "%s", "viernes13");
    }
    CHECK(app_board_wifi_record_save(&rec) == ESP_OK, "the fake stores the network");
    if (provisioned) {
        CHECK(app_board_wifi_join(rec.ssid, rec.pass) == ESP_OK, "and associates");
    } else {
        app_board_wifi_forget();
    }
}

/* The fake's stand-in curve, written out independently of it: the window's own key pair
 * comes from the fake's deterministic bytes, so the expected key can be derived here. */
static void expected_key(const uint8_t peer_pub[HO_KEY_LEN], uint8_t out[HO_LMK_LEN])
{
    uint8_t priv[HO_KEY_LEN], pub[HO_KEY_LEN], shared[HO_KEY_LEN];
    for (int i = 0; i < HO_KEY_LEN; i++) priv[i] = (uint8_t)i;
    for (int i = 0; i < HO_KEY_LEN; i++) pub[i] = (uint8_t)(priv[i] ^ 0xA5);
    for (int i = 0; i < HO_KEY_LEN; i++) shared[i] = (uint8_t)(priv[i] ^ peer_pub[i] ^ 0x3C);
    CHECK(ho_lmk(shared, pub, peer_pub, out), "the expected key derives");
}

static void request_for(uint8_t seed, ho_request_t *req)
{
    for (int i = 0; i < HO_KEY_LEN; i++) req->pub[i] = (uint8_t)(seed + i * 7);
}

/* ------------------------------------------------------------------ the window */

static void test_closed_by_default(void)
{
    fresh();
    provision(true);
    share_init();
    share_status_t st;
    share_status(&st);
    CHECK(st.state == SHARE_CLOSED, "a booted device has no window");
    CHECK(!st.failed, "and nothing has failed");
    char state[16];
    read_all(s_state, state, sizeof state);
    CHECK(state[0] == '\0', "the radio was never asked for a window");
}

static void test_beacons(void)
{
    fresh();
    provision(true);
    share_init();

    CHECK(share_start(1000), "a window opens");
    share_status_t st;
    share_status(&st);
    CHECK(st.state == SHARE_OPEN, "it reads as open");
    CHECK_EQ(st.left_ms, 60000, "with the whole window left");

    char state[8];
    read_all(s_state, state, sizeof state);
    CHECK(strcmp(state, "on") == 0, "the radio was asked to start");
    CHECK_EQ(count_lines(s_sent), 1, "opening sends the first beacon at once");

    sent_t f;
    CHECK(sent_at(0, &f), "the beacon was captured");
    CHECK(f.broadcast, "a beacon is a broadcast");
    CHECK_EQ(f.len, HO_BEACON_SIZE, "of the specified size");
    ho_beacon_t b;
    CHECK(ho_beacon_decode(f.data, f.len, &b), "and it decodes");
    CHECK_EQ(b.channel, 6, "the channel the radio is on");
    CHECK_EQ(b.seconds_left, 60, "the window's length");

    uint8_t priv[32], pub[32];
    for (int i = 0; i < 32; i++) priv[i] = (uint8_t)i;
    app_board_x25519_public(priv, pub);
    CHECK(memcmp(b.pub, pub, 32) == 0, "the beacon carries this window's own key");

    share_tick(1200);
    CHECK_EQ(count_lines(s_sent), 2, "a tick beacons again");
    CHECK(sent_at(1, &f) && ho_beacon_decode(f.data, f.len, &b), "the second beacon decodes");
    CHECK_EQ(b.seconds_left, 60, "still 60 seconds left");

    share_tick(30000);
    CHECK_EQ(count_lines(s_sent), 3, "and again");
    CHECK(sent_at(2, &f) && ho_beacon_decode(f.data, f.len, &b), "the third decodes");
    CHECK_EQ(b.seconds_left, 31, "the countdown is the ceil of what is left");

    share_stop(30000);
}

/* One player answered, and nobody else. */
static void test_one_player(void)
{
    fresh();
    provision(true);
    share_init();
    CHECK(share_start(1000), "a window opens");

    ho_request_t req;
    request_for(0x10, &req);
    uint8_t frame[HO_REQUEST_SIZE];
    size_t n = ho_request_encode(&req, frame, sizeof frame);
    CHECK_EQ(n, HO_REQUEST_SIZE, "a request encodes");

    CHECK(host_share_deliver(k_a, frame, n), "the radio delivers the request");
    share_status_t st;
    share_status(&st);
    CHECK(st.state == SHARE_OPEN, "the callback only records it: the answer is the tick's");
    CHECK_EQ(count_lines(s_sent), 1, "and nothing has been sent for it yet");

    share_tick(1400);
    share_status(&st);
    CHECK(st.state == SHARE_ANSWERED, "the tick answers it");
    CHECK(st.has_player, "and names the player");
    CHECK(memcmp(st.player, k_a, 6) == 0, "the player that asked");

    /* The key registered for the player is the one the two keys derive. */
    char peer[256];
    read_all(s_peer, peer, sizeof peer);
    char *bar = strchr(peer, '|');
    CHECK(bar != NULL, "a peer was registered");
    if (bar) {
        *bar = '\0';
        uint8_t mac[6], lmk[16], want[16];
        CHECK_EQ(unhex(peer, mac, sizeof mac), 6, "the peer's MAC");
        CHECK(memcmp(mac, k_a, 6) == 0, "and it is the player that asked");
        CHECK_EQ(unhex(bar + 1, lmk, sizeof lmk), 16, "the peer's key");
        expected_key(req.pub, want);
        CHECK(memcmp(lmk, want, 16) == 0, "derived from the window's key and the request's");
    }

    CHECK_EQ(count_lines(s_sent), 2, "the record went out");
    sent_t f;
    CHECK(sent_at(1, &f), "the record was captured");
    CHECK(!f.broadcast, "it is unicast");
    CHECK(memcmp(f.dst, k_a, 6) == 0, "to the player");
    ho_record_t rec;
    CHECK(ho_record_decode(f.data, f.len, &rec), "and it decodes");
    CHECK(rec.has_home, "it carries the home network");
    CHECK(strcmp(rec.home_ssid, "xperiments-2.4G") == 0, "its name");
    CHECK(strcmp(rec.home_pass, "viernes13") == 0, "its password");
    CHECK(rec.has_ap, "and the setup network");
    CHECK(strcmp(rec.ap_ssid, "K7.local") == 0, "named K7.local");
    CHECK(strcmp(rec.ap_pass, "87654321") == 0, "with the setup password in force");
    CHECK(strcmp(rec.host, "k7") == 0, "the hostname");
    CHECK_EQ(rec.ipv4, 0xC0A8002Cu, "and the station's address");

    /* No more beacons once a player has been answered: the beacon's job is done. */
    share_tick(1600);
    CHECK_EQ(count_lines(s_sent), 2, "an answered window does not beacon");

    /* Another player is ignored, for the rest of the window. */
    ho_request_t other;
    request_for(0x50, &other);
    n = ho_request_encode(&other, frame, sizeof frame);
    CHECK(host_share_deliver(k_b, frame, n), "a second player asks");
    share_tick(1800);
    CHECK_EQ(count_lines(s_sent), 2, "and is not answered");

    /* The player that was answered may ask again, and is answered again. */
    CHECK(host_share_deliver(k_a, frame, n), "the player asks again");
    share_tick(2000);
    CHECK_EQ(count_lines(s_sent), 3, "and gets the record again");

    share_stop(2000);
    read_all(s_peer, peer, sizeof peer);
    CHECK(strcmp(peer, "none") == 0, "closing drops the peer");
}

/* Four answers, then silence: H25's three retries. */
static void test_answers_are_capped(void)
{
    fresh();
    provision(true);
    share_init();
    CHECK(share_start(1000), "a window opens");

    ho_request_t req;
    request_for(0x10, &req);
    uint8_t frame[HO_REQUEST_SIZE];
    size_t n = ho_request_encode(&req, frame, sizeof frame);

    CHECK(host_share_deliver(k_a, frame, n), "the first request");
    share_tick(1200);
    for (int i = 0; i < 6; i++) {
        CHECK(host_share_deliver(k_a, frame, n), "a retry");
        share_tick(1400 + (uint32_t)i * 200);
    }
    CHECK_EQ(count_lines(s_sent), 1 + SHARE_ANSWERS, "the record and three retries, and no more");
    share_stop(3000);
}

static void test_expiry(void)
{
    fresh();
    provision(true);
    share_init();
    CHECK(share_start(1000), "a window opens");
    share_tick(1000 + SHARE_WINDOW_MS);
    share_status_t st;
    share_status(&st);
    CHECK(st.state == SHARE_ANSWERED || st.state == SHARE_CLOSED, "the deadline was reached");
    CHECK(st.state == SHARE_CLOSED, "and the window closed itself");
    CHECK_EQ(st.left_ms, 0, "with nothing left");

    char state[8];
    read_all(s_state, state, sizeof state);
    CHECK(strcmp(state, "off") == 0, "the radio was asked to stop");

    /* A frame after the window closed is not answered. */
    ho_request_t req;
    request_for(0x10, &req);
    uint8_t frame[HO_REQUEST_SIZE];
    size_t n = ho_request_encode(&req, frame, sizeof frame);
    CHECK(!host_share_deliver(k_a, frame, n), "the radio is not listening");
    share_tick(1000 + SHARE_WINDOW_MS + 200);
    CHECK_EQ(count_lines(s_sent), 1, "and nothing was sent"); /* the beacon it opened with */
}

static void test_stop(void)
{
    fresh();
    provision(true);
    share_init();
    CHECK(share_start(1000), "a window opens");
    share_stop(2000);
    share_status_t st;
    share_status(&st);
    CHECK(st.state == SHARE_CLOSED, "stop closes it");
    char state[8];
    read_all(s_state, state, sizeof state);
    CHECK(strcmp(state, "off") == 0, "and stops the radio");
    share_stop(2100); /* idempotent */
}

/* A device with no home network offers only its own. */
static void test_unprovisioned(void)
{
    fresh();
    provision(false);
    share_init();
    CHECK(share_start(1000), "an unprovisioned device opens a window");

    ho_request_t req;
    request_for(0x10, &req);
    uint8_t frame[HO_REQUEST_SIZE];
    size_t n = ho_request_encode(&req, frame, sizeof frame);
    CHECK(host_share_deliver(k_a, frame, n), "a player asks");
    share_tick(1200);

    sent_t f;
    CHECK(sent_at(1, &f), "the record went out");
    ho_record_t rec;
    CHECK(ho_record_decode(f.data, f.len, &rec), "and it decodes");
    CHECK(!rec.has_home, "there is no home network in it");
    CHECK(rec.has_ap, "but the setup network is");
    CHECK_EQ(rec.ipv4, 0xC0A80401u, "and the address is the setup network's");
    share_stop(2000);
}

static void test_radio_refused(void)
{
    fresh();
    provision(true);
    share_init();
    setenv("XTAPE_HOST_SHARE_FAIL", "1", 1);
    CHECK(!share_start(1000), "ESP-NOW refusing is a failed window");
    share_status_t st;
    share_status(&st);
    CHECK(st.state == SHARE_CLOSED, "which stays closed");
    CHECK(st.failed, "and says so");

    unsetenv("XTAPE_HOST_SHARE_FAIL");
    CHECK(share_start(2000), "a later attempt can open");
    share_status(&st);
    CHECK(!st.failed, "and clears the failure");
    CHECK(share_start(2100), "opening an open window is not an error");
    share_stop(2200);
}

int main(void)
{
    test_closed_by_default();
    test_beacons();
    test_one_player();
    test_answers_are_capped();
    test_expiry();
    test_stop();
    test_unprovisioned();
    test_radio_refused();
    TEST_END();
}
