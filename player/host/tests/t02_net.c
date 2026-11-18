/*
 * T02: the player's network state machine, host tier (network-handover.md T7-T9).
 *
 * A fake clock, a fake radio and a scripted station. What is checked is the policy the spec
 * fixes: the sweep's order and dwell, one request and its three retries, a record refused
 * whole when it is damaged, the store, the preference order and its ceiling, the failing
 * alternation's two phases, and the address rule. The curve is the host adapter's stand-in,
 * so the key assertion is about the transcript it was derived from, not about X25519.
 */
#include "test_util.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "handover.h"
#include "player_board.h"
#include "player_net.h"

extern void host_player_reset(void);
extern void host_player_wifi_script(const char *ssid, bool ok, int disconnects, int reason);
extern void host_player_resolve(const char *ip);
extern void host_player_drop(void);
extern bool host_player_deliver_beacon(const uint8_t mac[6], const uint8_t pub[HO_KEY_LEN]);
extern bool host_player_deliver_record(const uint8_t *frame, size_t len);
extern int host_player_sent_count(void);
extern bool host_player_sent(int index, uint8_t dst[6], uint8_t frame[HO_REQUEST_SIZE],
                             size_t *len);
extern bool host_player_peer(uint8_t mac[6], uint8_t lmk[HO_LMK_LEN]);
extern bool host_player_stored(ho_record_t *out);
extern int host_player_joins(void);
extern bool host_player_sent_encrypted(int index);
extern bool host_player_joined(int index, char *out, size_t cap);

static const uint8_t k_k7[6] = {0x28, 0x37, 0x2f, 0x87, 0xe4, 0xd8};
static const char *k_home = "xperiments-2.4G";

static void k7_key(uint8_t pub[HO_KEY_LEN])
{
    for (int i = 0; i < HO_KEY_LEN; i++) pub[i] = (uint8_t)(0x30 + i);
}

static ho_record_t a_record(bool home, bool ap)
{
    ho_record_t r;
    memset(&r, 0, sizeof r);
    r.has_home = home;
    r.has_ap = ap;
    if (home) {
        snprintf(r.home_ssid, sizeof r.home_ssid, "%s", k_home);
        snprintf(r.home_pass, sizeof r.home_pass, "%s", "viernes13");
    }
    if (ap) {
        snprintf(r.ap_ssid, sizeof r.ap_ssid, "%s", "K7.local");
        snprintf(r.ap_pass, sizeof r.ap_pass, "%s", "12345678");
    }
    snprintf(r.host, sizeof r.host, "%s", "k7");
    r.ipv4 = 0xC0A8000Du; /* 192.168.0.13 */
    return r;
}

static void expected_lmk(const uint8_t k7_pub[HO_KEY_LEN], uint8_t out[HO_LMK_LEN])
{
    uint8_t priv[HO_KEY_LEN], pub[HO_KEY_LEN], shared[HO_KEY_LEN];
    for (int i = 0; i < HO_KEY_LEN; i++) priv[i] = (uint8_t)i;     /* the fake's random */
    for (int i = 0; i < HO_KEY_LEN; i++) pub[i] = (uint8_t)(priv[i] ^ 0xA5);
    for (int i = 0; i < HO_KEY_LEN; i++) shared[i] = (uint8_t)(priv[i] ^ k7_pub[i] ^ 0x3C);
    CHECK(ho_lmk(shared, k7_pub, pub, out), "the expected key derives");
}

/* ------------------------------------------------------------------ the sweep */

static void test_sweep(void)
{
    host_player_reset();
    player_net_init();
    player_net_status_t st;
    player_net_status(&st);
    CHECK(st.state == PLAYER_NET_SWEEPING, "with no record it sweeps");
    CHECK(!st.provisioned, "and has nothing stored");

    player_net_tick(0);
    player_net_status(&st);
    CHECK_EQ(st.channel, 1, "the first visit is channel 1");
    player_net_tick(249);
    player_net_status(&st);
    CHECK_EQ(st.channel, 1, "the visit lasts its dwell");
    player_net_tick(250);
    player_net_status(&st);
    CHECK_EQ(st.channel, 2, "then the next channel");
    player_net_tick(500);
    player_net_status(&st);
    CHECK_EQ(st.channel, 3, "in order, 250 ms apart");

    /* A full sweep is 3.25 s: 1 at 0 ms, 13 at 3 000 ms. */
    player_net_tick(3000);
    player_net_status(&st);
    CHECK_EQ(st.channel, 13, "the thirteenth is last");
    player_net_tick(3250);
    player_net_status(&st);
    CHECK_EQ(st.channel, 1, "and the sweep starts again");
}

/* ------------------------------------------------------------------ the ask */

static void test_asks_and_stores(void)
{
    host_player_reset();
    host_player_resolve("192.168.0.13");
    host_player_wifi_script(k_home, true, 0, 0);
    player_net_init();
    player_net_tick(0);

    uint8_t pub[HO_KEY_LEN];
    k7_key(pub);
    CHECK(host_player_deliver_beacon(k_k7, pub), "a beacon arrives on channel 1");
    player_net_tick(100);

    player_net_status_t st;
    player_net_status(&st);
    CHECK(st.state == PLAYER_NET_ASKING, "the beacon starts an ask");
    CHECK_EQ(host_player_sent_count(), 1, "one request went out");

    uint8_t dst[6], frame[HO_REQUEST_SIZE];
    size_t len = 0;
    CHECK(host_player_sent(0, dst, frame, &len), "the request was captured");
    CHECK(memcmp(dst, k_k7, 6) == 0, "to the K7 that beacond");
    ho_request_t req;
    CHECK(ho_request_decode(frame, len, &req), "and it decodes");
    uint8_t want_pub[HO_KEY_LEN], want_lmk[HO_LMK_LEN], lmk[HO_LMK_LEN], mac[6];
    for (int i = 0; i < HO_KEY_LEN; i++) want_pub[i] = (uint8_t)(i ^ 0xA5);
    CHECK(memcmp(req.pub, want_pub, HO_KEY_LEN) == 0, "the request carries this ask's key");
    CHECK(host_player_peer(mac, lmk), "the K7 is registered as the one peer");
    CHECK(memcmp(mac, k_k7, 6) == 0, "with the beacon's address");
    expected_lmk(pub, want_lmk);
    CHECK(memcmp(lmk, want_lmk, HO_LMK_LEN) == 0,
          "and the key derived from both public keys, in the spec's order");

    /* The request must travel in clear: registering the K7 encrypted first encrypts it, and
     * the K7 cannot derive the key until it has read the request. */
    CHECK(!host_player_sent_encrypted(0), "the request went out unencrypted");

    /* The K7 answers. */
    ho_record_t rec = a_record(true, true);
    uint8_t record[HO_RECORD_MAX];
    size_t n = ho_record_encode(&rec, record, sizeof record);
    CHECK(host_player_deliver_record(record, n), "the record arrives");
    player_net_tick(200);
    player_net_status(&st);
    CHECK(st.state == PLAYER_NET_CONNECTING, "a valid record starts the join");
    CHECK(st.provisioned, "and is held");
    ho_record_t stored;
    CHECK(host_player_stored(&stored), "the board stored it");
    CHECK(strcmp(stored.home_ssid, k_home) == 0, "with the home network");
    CHECK(strcmp(stored.ap_ssid, "K7.local") == 0, "and the K7's own");

    player_net_tick(250);
    player_net_status(&st);
    CHECK(st.state == PLAYER_NET_CONNECTED, "the join associates");
    CHECK(st.associated, "it reads as connected");
    CHECK(strcmp(st.host, "192.168.0.13") == 0, "and the K7's address is the one the name gave");
    CHECK(strcmp(st.network, k_home) == 0, "it joined the home network");
}

/* A record that fails its own rules is refused whole, and the ask runs out of retries. */
static void test_bad_record(void)
{
    host_player_reset();
    player_net_init();
    player_net_tick(0);

    uint8_t pub[HO_KEY_LEN];
    k7_key(pub);
    CHECK(host_player_deliver_beacon(k_k7, pub), "a beacon arrives");
    player_net_tick(100);

    ho_record_t rec = a_record(true, true);
    uint8_t record[HO_RECORD_MAX];
    size_t n = ho_record_encode(&rec, record, sizeof record);
    record[n - 1] ^= 0xFF; /* the checksum stops agreeing */
    CHECK(host_player_deliver_record(record, n), "a damaged record arrives");
    player_net_tick(200);

    player_net_status_t st;
    player_net_status(&st);
    CHECK(st.state == PLAYER_NET_ASKING, "it is refused, and the ask continues");
    CHECK(!st.provisioned, "nothing was stored");
    ho_record_t stored;
    CHECK(!host_player_stored(&stored), "the board holds no record");

    /* The retries: one request at 100 ms and three more, 400 ms apart. */
    player_net_tick(500);
    player_net_tick(900);
    player_net_tick(1300);
    CHECK_EQ(host_player_sent_count(), 4, "the request and its three retries, and no more");
    for (int i = 0; i < host_player_sent_count(); i++)
        CHECK(!host_player_sent_encrypted(i), "every request went out unencrypted");
    player_net_tick(1700);
    player_net_status(&st);
    CHECK(st.state == PLAYER_NET_SWEEPING, "with no answer it sweeps again");
    CHECK(!st.provisioned, "and still has no record");
}

/* The home network fails; the K7's own is next, and its address is the fixed one. */
static void test_home_fails_then_ap(void)
{
    host_player_reset();
    host_player_resolve(""); /* mDNS answers nothing */
    host_player_wifi_script(k_home, false, 5, 15);
    host_player_wifi_script("K7.local", true, 0, 0);

    /* A record already held: the machine starts by joining. */
    ho_record_t held = a_record(true, true);
    CHECK(player_board_record_save(&held), "a record is stored");
    player_net_init();

    player_net_tick(0);
    player_net_status_t st;
    player_net_status(&st);
    CHECK(st.state == PLAYER_NET_CONNECTING || st.state == PLAYER_NET_CONNECTED,
          "it starts joining");
    CHECK(host_player_joins() >= 1, "a join was made");
    char first[33] = "";
    CHECK(host_player_joined(0, first, sizeof first), "and recorded");
    CHECK(strcmp(first, k_home) == 0, "the home network first");

    player_net_tick(50);
    player_net_status(&st);
    CHECK(st.state == PLAYER_NET_CONNECTED, "the K7's own associates");
    CHECK(strcmp(st.host, "192.168.4.1") == 0, "at the setup network's fixed address");
    CHECK(host_player_joins() >= 2, "the other network was tried");
    char second[33] = "";
    CHECK(host_player_joined(1, second, sizeof second), "and recorded");
    CHECK(strcmp(second, "K7.local") == 0, "as the K7's own");
}

/* Both networks fail: 20 s of retrying, then 10 s of sweeping, for ever. */
static void test_alternation(void)
{
    host_player_reset();
    host_player_resolve("");
    host_player_wifi_script(k_home, false, 5, 15);
    ho_record_t held = a_record(true, false);
    CHECK(player_board_record_save(&held), "a home-only record");
    player_net_init();

    player_net_tick(0);
    player_net_tick(50);
    player_net_status_t st;
    player_net_status(&st);
    CHECK(st.state == PLAYER_NET_FAILING, "with nothing left to try, it is failing");

    /* The retry phase lasts 20 s from the failure, then the sweep phase 10 s. */
    player_net_tick(2050);
    player_net_status(&st);
    CHECK(st.state == PLAYER_NET_FAILING, "still retrying inside the window");
    player_net_tick(20050);
    player_net_status(&st);
    CHECK(st.state == PLAYER_NET_SWEEPING, "then it sweeps for a new window");
    player_net_tick(30050);
    player_net_status(&st);
    CHECK(st.state == PLAYER_NET_FAILING, "and then retries again");
}

/* A connection that drops goes back to the alternation, not to a reboot. */
static void test_drop(void)
{
    host_player_reset();
    host_player_resolve("192.168.0.13");
    host_player_wifi_script(k_home, true, 0, 0);
    ho_record_t held = a_record(true, true);
    CHECK(player_board_record_save(&held), "a record");
    player_net_init();

    player_net_tick(0);
    player_net_tick(50);
    player_net_status_t st;
    player_net_status(&st);
    CHECK(st.state == PLAYER_NET_CONNECTED, "it connects");

    host_player_drop();
    player_net_tick(100);
    player_net_status(&st);
    CHECK(st.state == PLAYER_NET_FAILING, "a drop is handled by retrying");
}

/* The record survives the machine being started again: a reflash is not a re-provision. */
static void test_record_survives(void)
{
    host_player_reset();
    host_player_resolve("192.168.0.13");
    host_player_wifi_script(k_home, true, 0, 0);
    player_net_init();
    player_net_tick(0);

    uint8_t pub[HO_KEY_LEN];
    k7_key(pub);
    CHECK(host_player_deliver_beacon(k_k7, pub), "a beacon");
    player_net_tick(100);
    ho_record_t rec = a_record(true, true);
    uint8_t record[HO_RECORD_MAX];
    size_t n = ho_record_encode(&rec, record, sizeof record);
    CHECK(host_player_deliver_record(record, n), "a record");
    player_net_tick(200);

    player_net_init();
    player_net_status_t st;
    player_net_status(&st);
    CHECK(st.provisioned, "the next start still has the record");
    CHECK(st.state == PLAYER_NET_CONNECTING, "and goes straight to joining");
}

int main(void)
{
    test_sweep();
    test_asks_and_stores();
    test_bad_record();
    test_home_fails_then_ap();
    test_alternation();
    test_drop();
    test_record_survives();
    TEST_END();
}
