/*
 * The K7Player's host adapter: the second implementation of player_board, so the seam is
 * real rather than hypothetical. There is no NVS, no radio and no access point here - the
 * tests script every one of them, and the hooks below are the script.
 *
 * The stand-in curve is `pub = priv ^ 0xA5` and `shared = priv ^ peer ^ 0x3C`, and random
 * bytes count from zero, so a test can compute the key it expects. What that proves is the
 * transcript and the policy, not X25519: the curve is the board's (PL-09).
 */
#include "player_board.h"
#include "player_output.h"
#include "player_update.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ the script */

typedef struct {
    char ssid[33];
    bool ok;
    int disconnects;
    int reason;
} wifi_script_t;

static wifi_script_t s_wifi[4];
static int s_wifi_n;

static bool s_associated;
static int s_disconnects;
static int s_reason;
static char s_ip[16];

#define HOST_JOIN_MAX 8
static char s_joined[HOST_JOIN_MAX][33];
static int s_joined_n;

static bool s_has_record;
static ho_record_t s_record;

static char s_resolve_ip[32];

/* A scripted resolver: each name with the answer it gives, and every name asked, in order.
 * The player's lookup order is a fact a test can only read if it knows which names were
 * asked; one answer for every name cannot tell the first ask from the second (HA-05). */
#define HOST_RESOLVE_MAX 8
typedef struct {
    char name[64];
    char ip[32];
} host_answer_t;
static host_answer_t s_answers[HOST_RESOLVE_MAX];
static int s_answers_n;
static char s_asked[HOST_RESOLVE_MAX][64];
static int s_asked_n;


static player_board_frame_fn s_frame_cb;
static player_board_packet_fn s_audio_cb;
static player_board_packet_fn s_tone_cb;
static player_board_update_fn s_update_cb;
static int s_audio_sent;
static void *s_frame_ctx;
static bool s_now_on;
static uint8_t s_channel;

#define HOST_SENT_MAX 8
typedef struct {
    uint8_t dst[6];
    uint8_t frame[HO_REQUEST_SIZE];
    size_t len;
    bool encrypted; /* what the peer registration said when the frame went out */
} sent_t;
static sent_t s_sent[HOST_SENT_MAX];
static int s_sent_n;

static bool s_has_address;
static uint8_t s_address[6];
static uint8_t s_light_level;
static int s_light_sets;
static bool s_peer_encrypted; /* the flag ESP-NOW would apply in both directions */
static bool s_has_peer;
static uint8_t s_peer[6];
static uint8_t s_peer_lmk[HO_LMK_LEN];

void host_player_stream_reset(void); /* defined with the stream section below */
void host_player_output_reset(void); /* defined with the output section below */

void host_player_ota_reset(void); /* defined with the update section below */

void host_player_reset(void)
{
    host_player_ota_reset();
    host_player_stream_reset();
    host_player_output_reset();
    memset(s_wifi, 0, sizeof s_wifi);
    s_wifi_n = 0;
    s_associated = false;
    s_disconnects = 0;
    s_reason = 0;
    s_ip[0] = '\0';
    s_has_record = false;
    memset(&s_record, 0, sizeof s_record);
    s_resolve_ip[0] = '\0';
    s_answers_n = 0;
    s_asked_n = 0;
    s_now_on = false;
    s_frame_cb = NULL;
    s_channel = 0;
    s_sent_n = 0;
    s_has_peer = false;
    s_has_address = false;
    s_peer_encrypted = false;
    s_joined_n = 0;
}

bool host_player_address(uint8_t mac[6])
{
    if (!s_has_address) return false;
    if (mac) memcpy(mac, s_address, 6);
    return true;
}

int host_player_joins(void) { return s_joined_n; }

bool host_player_joined(int index, char *out, size_t cap)
{
    if (index < 0 || index >= s_joined_n || !out || cap == 0) return false;
    snprintf(out, cap, "%s", s_joined[index]);
    return true;
}

void host_player_wifi_script(const char *ssid, bool ok, int disconnects, int reason)
{
    if (!ssid || s_wifi_n >= 4) return;
    snprintf(s_wifi[s_wifi_n].ssid, sizeof s_wifi[0].ssid, "%s", ssid);
    s_wifi[s_wifi_n].ok = ok;
    s_wifi[s_wifi_n].disconnects = disconnects;
    s_wifi[s_wifi_n].reason = reason;
    s_wifi_n++;
}

void host_player_resolve(const char *ip)
{
    snprintf(s_resolve_ip, sizeof s_resolve_ip, "%s", ip ? ip : "");
}

void host_player_resolves(const char *name, const char *ip)
{
    if (!name || s_answers_n >= HOST_RESOLVE_MAX) return;
    snprintf(s_answers[s_answers_n].name, sizeof s_answers[0].name, "%s", name);
    snprintf(s_answers[s_answers_n].ip, sizeof s_answers[0].ip, "%s", ip ? ip : "");
    s_answers_n++;
}

int host_player_asked_count(void) { return s_asked_n; }

bool host_player_asked(int at, char *out, size_t cap)
{
    if (!out || at < 0 || at >= s_asked_n) return false;
    snprintf(out, cap, "%s", s_asked[at]);
    return true;
}

void host_player_drop(void)
{
    s_associated = false;
    s_ip[0] = '\0';
}

/* Deliver a frame as the radio would, from outside any lock the machine holds. */
bool host_player_deliver_beacon(const uint8_t mac[6], const uint8_t pub[HO_KEY_LEN])
{
    if (!s_now_on || !s_frame_cb) return false;
    ho_beacon_t b;
    memset(&b, 0, sizeof b);
    memcpy(b.pub, pub, HO_KEY_LEN);
    b.channel = s_channel;
    b.seconds_left = 60;
    uint8_t frame[HO_BEACON_SIZE];
    size_t n = ho_beacon_encode(&b, frame, sizeof frame);
    if (!n) return false;
    s_frame_cb(mac, frame, n, s_frame_ctx);
    return true;
}

bool host_player_deliver_record(const uint8_t *frame, size_t len)
{
    if (!s_now_on || !s_frame_cb || !frame || !len) return false;
    s_frame_cb(s_peer, frame, len, s_frame_ctx);
    return true;
}

uint8_t host_player_channel(void) { return s_channel; }

int host_player_sent_count(void) { return s_sent_n; }

bool host_player_sent(int index, uint8_t dst[6], uint8_t frame[HO_REQUEST_SIZE], size_t *len)
{
    if (index < 0 || index >= s_sent_n) return false;
    if (dst) memcpy(dst, s_sent[index].dst, 6);
    if (frame) memcpy(frame, s_sent[index].frame, s_sent[index].len);
    if (len) *len = s_sent[index].len;
    return true;
}

bool host_player_sent_encrypted(int index)
{
    if (index < 0 || index >= s_sent_n) return false;
    return s_sent[index].encrypted;
}

bool host_player_peer(uint8_t mac[6], uint8_t lmk[HO_LMK_LEN])
{
    if (!s_has_peer) return false;
    if (mac) memcpy(mac, s_peer, 6);
    if (lmk) memcpy(lmk, s_peer_lmk, HO_LMK_LEN);
    return true;
}

bool host_player_stored(ho_record_t *out)
{
    if (!s_has_record) return false;
    if (out) *out = s_record;
    return true;
}

/* ------------------------------------------------------------------ the seam */

const char *player_board_name(void) { return "host"; }

bool player_board_init(void) { return true; }

bool player_board_record_load(ho_record_t *out)
{
    if (!s_has_record || !out) return false;
    *out = s_record;
    return true;
}

bool player_board_record_save(const ho_record_t *rec)
{
    if (!rec) return false;
    s_record = *rec;
    s_has_record = true;
    return true;
}

void player_board_record_drop(void)
{
    s_has_record = false;
    memset(&s_record, 0, sizeof s_record);
}

bool player_board_wifi_join(const char *ssid, const char *pass)
{
    (void)pass;
    s_associated = false;
    s_disconnects = 0; /* the counter restarts at the join, as player_board.h promises */
    s_reason = 0;
    s_ip[0] = '\0';
    if (!ssid) return false;
    if (s_joined_n < HOST_JOIN_MAX) snprintf(s_joined[s_joined_n++], 33, "%s", ssid);

    for (int i = 0; i < s_wifi_n; i++) {
        if (strcmp(s_wifi[i].ssid, ssid) != 0) continue;
        if (s_wifi[i].ok) {
            s_associated = true;
            snprintf(s_ip, sizeof s_ip, "%s", "192.168.0.44");
        } else {
            s_disconnects = s_wifi[i].disconnects;
            s_reason = s_wifi[i].reason;
        }
        return true;
    }
    /* An unscripted network is a network that is not there. */
    s_disconnects = 5;
    s_reason = 201;
    return true;
}

void player_board_wifi_state(player_board_wifi_t *out)
{
    if (!out) return;
    out->associated = s_associated;
    out->disconnects = s_disconnects;
    out->last_reason = s_reason;
    snprintf(out->ip, sizeof out->ip, "%s", s_ip);
}

bool player_board_resolve(const char *hostname, char *out, size_t cap)
{
    if (!out || cap == 0) return false;
    if (s_asked_n < HOST_RESOLVE_MAX)
        snprintf(s_asked[s_asked_n++], sizeof s_asked[0], "%s", hostname ? hostname : "");
    /* A scripted name answers only itself, so a test can make one resolve and another not;
     * with no script, one address answers every name, as it always has. */
    for (int i = 0; i < s_answers_n; i++) {
        if (strcmp(s_answers[i].name, hostname ? hostname : "") != 0) continue;
        snprintf(out, cap, "%s", s_answers[i].ip);
        return s_answers[i].ip[0] != '\0';
    }
    snprintf(out, cap, "%s", s_resolve_ip);
    return s_resolve_ip[0] != '\0';
}

bool player_board_espnow_start(player_board_packet_fn on_packet)
{
    s_audio_cb = on_packet;
    return true;
}

void player_board_espnow_set_tone(player_board_packet_fn on_tone)
{
    s_tone_cb = on_tone;
}

/* ------------------------------------------------------------ the update's script
 *
 * A spare slot of 64 KB, a marker string, and flags for what the board was asked to do. The
 * curve is the board's, so the stand-in verify checks the digest and that a signature arrived:
 * what that proves is the plumbing and the decisions, not ECDSA (player-update.md U18).
 */

#define HOST_SLOT_MAX (64 * 1024)
#define HOST_TO_MAX 24

typedef struct {
    uint8_t dst[6];
    uint8_t data[32];
    size_t len;
} host_to_t;

static uint8_t s_ota_slot[HOST_SLOT_MAX];
static uint32_t s_ota_len;
static uint32_t s_ota_written;
static bool s_ota_open;
static int s_ota_begins;
static bool s_ota_committed;
static bool s_ota_rebooted;
static bool s_ota_rolled_back;
static bool s_ota_valid;
static bool s_ota_verify_ok = true;
static bool s_ota_pending;
static char s_ota_marker[33];
static char s_ota_committed_version[33];
static host_to_t s_to[HOST_TO_MAX];
static int s_to_n;

void host_player_ota_reset(void)
{
    memset(s_ota_slot, 0, sizeof s_ota_slot);
    s_ota_len = 0;
    s_ota_written = 0;
    s_ota_open = false;
    s_ota_begins = 0;
    s_ota_committed = false;
    s_ota_rebooted = false;
    s_ota_rolled_back = false;
    s_ota_valid = false;
    s_ota_verify_ok = true;
    s_ota_pending = false;
    memset(s_ota_marker, 0, sizeof s_ota_marker);
    memset(s_ota_committed_version, 0, sizeof s_ota_committed_version);
    s_to_n = 0;
}

void host_player_ota_record(bool present) { s_has_record = present; }
void host_player_ota_marker(const char *version)
{
    s_ota_pending = true;
    snprintf(s_ota_marker, sizeof s_ota_marker, "%s", version ? version : "");
}
void host_player_ota_fail_verify(bool fail) { s_ota_verify_ok = !fail; }
const uint8_t *host_player_ota_slot(void) { return s_ota_slot; }
uint32_t host_player_ota_written(void) { return s_ota_written; }
int host_player_ota_begins(void) { return s_ota_begins; }
bool host_player_ota_committed(void) { return s_ota_committed; }
const char *host_player_ota_committed_version(void) { return s_ota_committed_version; }
bool host_player_ota_rebooted(void) { return s_ota_rebooted; }
bool host_player_ota_rolled_back(void) { return s_ota_rolled_back; }
bool host_player_ota_valid(void) { return s_ota_valid; }
int host_player_sent_to_count(void) { return s_to_n; }
bool host_player_sent_to(int index, uint8_t out[6], uint8_t *data, size_t *len)
{
    if (index < 0 || index >= s_to_n) return false;
    memcpy(out, s_to[index].dst, 6);
    memcpy(data, s_to[index].data, s_to[index].len);
    *len = s_to[index].len;
    return true;
}

void player_board_espnow_set_update(player_board_update_fn on_update) { s_update_cb = on_update; }

bool player_board_espnow_send_to(const uint8_t dst[6], const uint8_t *data, size_t len)
{
    if (!dst || !data || !len || len > sizeof s_to[0].data) return false;
    if (s_to_n >= HOST_TO_MAX) return false;
    memcpy(s_to[s_to_n].dst, dst, 6);
    memcpy(s_to[s_to_n].data, data, len);
    s_to[s_to_n].len = len;
    s_to_n++;
    return true;
}

const char *player_board_version(void) { return "0.6.2"; }

bool player_board_ota_pending(char *version, size_t cap)
{
    if (!version || !cap) return false;
    snprintf(version, cap, "%s", s_ota_marker);
    return s_ota_pending;
}

uint32_t player_board_ota_slot_size(void) { return HOST_SLOT_MAX; }

bool player_board_ota_begin(uint32_t image_len)
{
    player_board_ota_abort();
    if (image_len == 0 || image_len > HOST_SLOT_MAX) return false;
    s_ota_begins++;
    s_ota_open = true;
    s_ota_len = image_len;
    s_ota_written = 0;
    memset(s_ota_slot, 0, sizeof s_ota_slot);
    return true;
}

bool player_board_ota_write(uint32_t offset, const uint8_t *data, size_t len)
{
    if (!s_ota_open || !data || len == 0) return false;
    if (offset != s_ota_written || offset + len > s_ota_len) return false;
    memcpy(s_ota_slot + offset, data, len);
    s_ota_written += len;
    return true;
}

void player_board_ota_abort(void)
{
    s_ota_open = false;
    s_ota_len = 0;
    s_ota_written = 0;
}

bool player_board_ota_verify(const char *version, const uint8_t sha256[32], const uint8_t *sig,
                             size_t sig_len)
{
    if (!s_ota_len || !version || !sig || sig_len == 0) return false;
    if (!s_ota_verify_ok) return false;
    uint8_t digest[32];
    ho_sha256(s_ota_slot, s_ota_len, digest);
    if (memcmp(digest, sha256, 32) != 0) return false;
    /* The curve is the board's: a signature arrived is all this stub can say. */
    return sig[0] != 0;
}

bool player_board_ota_commit(const char *version)
{
    if (!s_ota_open || !version) return false;
    s_ota_open = false;
    s_ota_committed = true;
    snprintf(s_ota_committed_version, sizeof s_ota_committed_version, "%s", version);
    s_ota_pending = false;
    snprintf(s_ota_marker, sizeof s_ota_marker, "%s", version);
    return true;
}

void player_board_ota_mark_valid(void)
{
    s_ota_valid = true;
    s_ota_pending = false;
    s_ota_marker[0] = '\0';
}

void player_board_ota_rollback(void) { s_ota_rolled_back = true; }
void player_board_ota_reboot(void) { s_ota_rebooted = true; }

bool player_board_espnow_send(const uint8_t *data, size_t len)
{
    (void)data;
    s_audio_sent = (int)len;
    return true;
}

bool player_board_handover_start(player_board_frame_fn on_frame, void *ctx)
{
    s_frame_cb = on_frame;
    s_frame_ctx = ctx;
    s_now_on = true;
    return true;
}

void player_board_handover_stop(void)
{
    s_now_on = false;
    s_frame_cb = NULL;
    s_frame_ctx = NULL;
}

bool player_board_handover_send(const uint8_t *dst, const uint8_t *data, size_t len)
{
    if (!s_now_on || !dst || !data || !len || s_sent_n >= HOST_SENT_MAX) return false;
    memcpy(s_sent[s_sent_n].dst, dst, 6);
    size_t n = len < sizeof s_sent[0].frame ? len : sizeof s_sent[0].frame;
    memcpy(s_sent[s_sent_n].frame, data, n);
    s_sent[s_sent_n].len = n;
    s_sent[s_sent_n].encrypted = s_peer_encrypted;
    s_sent_n++;
    return true;
}

bool player_board_handover_address(const uint8_t mac[6])
{
    if (!mac) return false;
    memcpy(s_address, mac, 6);
    s_has_address = true;
    s_peer_encrypted = false;
    return true;
}

bool player_board_handover_peer(const uint8_t mac[6], const uint8_t lmk[16])
{
    if (!mac || !lmk) return false;
    memcpy(s_peer, mac, 6);
    memcpy(s_peer_lmk, lmk, HO_LMK_LEN);
    s_has_peer = true;
    s_peer_encrypted = true;
    return true;
}

void player_board_handover_peer_drop(const uint8_t mac[6])
{
    (void)mac;
    s_has_peer = false;
}

bool player_board_set_channel(uint8_t channel)
{
    if (channel < 1 || channel > 13) return false;
    s_channel = channel;
    return true;
}

void player_board_random(uint8_t *out, size_t len)
{
    if (!out) return;
    for (size_t i = 0; i < len; i++) out[i] = (uint8_t)i;
}

bool player_board_x25519_public(const uint8_t priv[32], uint8_t pub[32])
{
    if (!priv || !pub) return false;
    for (int i = 0; i < 32; i++) pub[i] = (uint8_t)(priv[i] ^ 0xA5);
    return true;
}

bool player_board_x25519(const uint8_t priv[32], const uint8_t peer[32], uint8_t shared[32])
{
    if (!priv || !peer || !shared) return false;
    for (int i = 0; i < 32; i++) shared[i] = (uint8_t)(priv[i] ^ peer[i] ^ 0x3C);
    return true;
}

/* --------------------------------------------------------------- the stream's transport
 *
 * There is no socket any more: the firmware's stream module takes whole ESP-NOW packets from
 * the radio's callback, and a host test hands them to player_stream_packet itself. This stub
 * stays for the reset path the other suites share.
 */

void host_player_stream_reset(void) { }

/* ------------------------------------------------------------------ the line output
 *
 * Does nothing, and counts: the seam's second implementation, which is what makes the first
 * one a seam rather than the only path. The peak is kept so a test can tell decoded audio
 * from the silence the playout writes when it has nothing.
 */

static int s_out_opens;
static bool s_out_fail;
static uint32_t s_out_rate;
static uint32_t s_out_frames;
static int32_t s_out_peak;

void host_player_output_reset(void)
{
    s_out_opens = 0;
    s_out_fail = false;
    s_out_rate = 0;
    s_light_level = 0;
    s_light_sets = 0;
    s_out_frames = 0;
    s_out_peak = 0;
}

void host_player_output_fail(bool fail) { s_out_fail = fail; }

bool player_output_open(uint32_t hz)
{
    s_out_opens++;
    s_out_rate = hz;
    return !s_out_fail;
}

/* The light, as a recorded level. */
uint8_t host_player_light(void) { return s_light_level; }
int host_player_light_sets(void) { return s_light_sets; }

void player_board_light(uint8_t level)
{
    s_light_level = level;
    s_light_sets++;
}

void player_output_write(const int16_t *stereo, size_t frames)
{
    s_out_frames += (uint32_t)frames;
    for (size_t i = 0; i < frames * 2; i++) {
        int32_t a = stereo[i] < 0 ? -stereo[i] : stereo[i];
        if (a > s_out_peak) s_out_peak = a;
    }
}

void player_output_close(void) {}

int player_output_opens(void) { return s_out_opens; }
uint32_t player_output_rate(void) { return s_out_rate; }
uint32_t player_output_frames(void) { return s_out_frames; }
int32_t player_output_peak(void) { return s_out_peak; }
