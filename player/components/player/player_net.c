/*
 * player_net - the K7Player's half of the handover.
 *
 * One lock guards the machine: the tick task drives it, the Wi-Fi task's callback hands it
 * frames, and the light's task reads a copy. The callback only copies; the curve and the
 * sends happen here, on a task with stack for them.
 */
#include "player_net.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "handover.h"
#include "player_board.h"

static const char *TAG = "net";

typedef struct {
    player_net_state_t state;
    bool entry; /* the state was entered and its first work has not run */

    bool provisioned;
    ho_record_t rec;

    /* the sweep */
    uint8_t channel;
    uint32_t next_channel_ms;
    uint32_t sweep_until_ms; /* 0 = until something changes */

    /* the ask */
    uint8_t k7[6];
    uint8_t k7_pub[HO_KEY_LEN];
    uint8_t priv[HO_KEY_LEN], pub[HO_KEY_LEN];
    uint8_t lmk[HO_LMK_LEN];
    int requests;
    uint32_t request_due_ms;

    /* the join */
    int network; /* 0 the home network, 1 the K7's own */
    unsigned tried;
    bool joining;
    uint32_t attempt_due_ms;
    int disconnects0;

    /* the failing alternation */
    uint32_t phase_until_ms;

    bool associated;
    char host[64];

    bool radio_on;
    bool beacon_pending;
    uint8_t beacon_src[6];
    uint8_t beacon_pub[HO_KEY_LEN];
    bool record_pending;
    uint8_t record_frame[HO_RECORD_MAX];
    size_t record_len;
} machine_t;

static machine_t M;
static SemaphoreHandle_t s_lock;

static void player_net_task(void *arg);

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

/* ------------------------------------------------------------------ the record's networks */

static bool network_of(int index, const char **ssid, const char **pass)
{
    if (index == 0 && M.rec.has_home) {
        *ssid = M.rec.home_ssid;
        *pass = M.rec.home_pass;
        return true;
    }
    if (index == 1 && M.rec.has_ap) {
        *ssid = M.rec.ap_ssid;
        *pass = M.rec.ap_pass;
        return true;
    }
    return false;
}

static int preferred(void) { return M.rec.has_home ? 0 : 1; }

/* ------------------------------------------------------------------ the radio */

static void on_frame(const uint8_t src[6], const uint8_t *data, size_t len, void *ctx)
{
    (void)ctx;
    player_net_on_frame(src, data, len);
}

static void radio_start(void)
{
    if (M.radio_on) return;
    M.radio_on = player_board_handover_start(on_frame, NULL);
}

static void radio_stop(void)
{
    if (!M.radio_on) return;
    player_board_handover_stop();
    M.radio_on = false;
}

/* ------------------------------------------------------------------ entering the states */

static void enter_sweeping(uint32_t now, uint32_t until)
{
    radio_stop();
    M.state = PLAYER_NET_SWEEPING;
    M.sweep_until_ms = until;
    M.channel = 0;
    M.next_channel_ms = now;
    M.beacon_pending = false;
    M.record_pending = false;
    ESP_LOGI(TAG, "sweeping%s", until ? " for a new window" : "");
}

static void enter_failing(uint32_t now);

/* One join attempt: the board is told, and the attempt's ceiling starts. */
static bool begin_join(uint32_t now, int index)
{
    const char *ssid = NULL, *pass = NULL;
    if (!network_of(index, &ssid, &pass)) return false;
    if (!player_board_wifi_join(ssid, pass)) return false;

    M.network = index;
    M.joining = true;
    /* The adapter restarts the counter at the join (player_board.h), so the baseline is
     * zero: an attempt that already failed inside join() is counted on the next tick. */
    M.disconnects0 = 0;
    M.attempt_due_ms = now + PLAYER_NET_JOIN_MS;
    ESP_LOGI(TAG, "joining %s", ssid);
    return true;
}

static void enter_connecting(uint32_t now)
{
    radio_stop();
    M.state = PLAYER_NET_CONNECTING;
    M.tried = 0;
    M.joining = false;

    int first = preferred();
    if (begin_join(now, first)) {
        M.tried |= 1u << first;
        return;
    }
    M.tried |= 1u << first;
    int other = 1 - first;
    if (begin_join(now, other)) {
        M.tried |= 1u << other;
        return;
    }
    enter_failing(now);
}

static void enter_failing(uint32_t now)
{
    radio_stop();
    M.state = PLAYER_NET_FAILING;
    M.phase_until_ms = now + PLAYER_NET_RETRY_MS;
    M.joining = false;
    M.attempt_due_ms = now; /* try at once */
    ESP_LOGW(TAG, "nothing joins: retrying for %u s, then sweeping for %u s",
             (unsigned)(PLAYER_NET_RETRY_MS / 1000), (unsigned)(PLAYER_NET_SWEEP_MS / 1000));
}

/* The K7's address: the name first, because a moved lease is invisible to a name, and the
 * record's address second (H36, R19). */
static void resolve_host(void)
{
    char ip[40] = "";
    const char *name = M.rec.host[0] ? M.rec.host : "k7";
    if (player_board_resolve(name, ip, sizeof ip) && ip[0]) {
        snprintf(M.host, sizeof M.host, "%s", ip);
    } else if (M.network == 1) {
        /* The setup network is always at 192.168.4.1 (H35); the record's address describes
         * the home network, so it is the wrong hint for this one. */
        snprintf(M.host, sizeof M.host, "%s", "192.168.4.1");
    } else if (M.rec.ipv4) {
        snprintf(M.host, sizeof M.host, "%u.%u.%u.%u", (unsigned)(M.rec.ipv4 >> 24),
                 (unsigned)((M.rec.ipv4 >> 16) & 0xff), (unsigned)((M.rec.ipv4 >> 8) & 0xff),
                 (unsigned)(M.rec.ipv4 & 0xff));
    } else {
        M.host[0] = '\0';
    }
    ESP_LOGI(TAG, "the K7 is at %s", M.host[0] ? M.host : "(no address)");
}

static void enter_connected(const player_board_wifi_t *w)
{
    M.state = PLAYER_NET_CONNECTED;
    M.associated = true;
    M.joining = false;
    resolve_host();
    ESP_LOGI(TAG, "joined %s at %s", M.network == 1 ? M.rec.ap_ssid : M.rec.home_ssid,
             w->ip[0] ? w->ip : "?");
}

/* ------------------------------------------------------------------ the ask */

static void send_request(uint32_t now)
{
    ho_request_t req;
    memcpy(req.pub, M.pub, HO_KEY_LEN);
    uint8_t frame[HO_REQUEST_SIZE];
    size_t n = ho_request_encode(&req, frame, sizeof frame);

    /* The order is the protocol. The request goes to a plaintext peer, because the K7 can
     * only derive the key after reading the request's public key; the same address is then
     * flipped to encrypted so the answer can be read. Registering the encrypted peer first
     * encrypts the request too, and the K7 drops a frame it cannot decrypt. */
    player_board_handover_address(M.k7);
    bool sent = n && player_board_handover_send(M.k7, frame, n);
    player_board_handover_peer(M.k7, M.lmk);

    M.requests++;
    if (!sent) ESP_LOGW(TAG, "request %d did not go out", M.requests);
    M.request_due_ms = now + PLAYER_NET_REQUEST_MS;
}

static void abandon_ask(uint32_t now)
{
    ESP_LOGW(TAG, "the K7 did not answer %d requests", M.requests);
    player_board_handover_peer_drop(M.k7);
    if (M.provisioned)
        enter_failing(now);
    else
        enter_sweeping(now, 0);
}

static void enter_asking(uint32_t now)
{
    M.state = PLAYER_NET_ASKING;
    M.requests = 0;
    M.record_pending = false;

    player_board_random(M.priv, sizeof M.priv);
    uint8_t shared[HO_KEY_LEN], lmk[HO_LMK_LEN];
    if (!player_board_x25519_public(M.priv, M.pub) ||
        !player_board_x25519(M.priv, M.k7_pub, shared) || !ho_lmk(shared, M.k7_pub, M.pub, lmk)) {
        ESP_LOGW(TAG, "the curve refused this beacon's key");
        abandon_ask(now);
        return;
    }
    memcpy(M.lmk, lmk, sizeof M.lmk);
    ESP_LOGI(TAG, "heard a K7 on channel %u: asking", (unsigned)M.channel);
    send_request(now);
}

/* ------------------------------------------------------------------ the tick */

static void tick_sweeping(uint32_t now)
{
    radio_start();
    if (M.radio_on) {
        /* The dwell is a schedule, not a sleep: a tick that arrives late (a busy task, a
         * Wi-Fi interrupt) visits the channels it missed instead of stretching one visit and
         * losing the beacon that a player would have heard on the next channel. */
        while (reached(now, M.next_channel_ms)) {
            M.channel = (uint8_t)(M.channel % PLAYER_NET_CHANNELS + 1);
            player_board_set_channel(M.channel);
            M.next_channel_ms += PLAYER_NET_DWELL_MS;
        }
    }
    if (M.beacon_pending) {
        M.beacon_pending = false;
        memcpy(M.k7, M.beacon_src, sizeof M.k7);
        memcpy(M.k7_pub, M.beacon_pub, sizeof M.k7_pub);
        enter_asking(now);
        return;
    }
    if (M.sweep_until_ms && reached(now, M.sweep_until_ms)) enter_failing(now);
}

static void tick_asking(uint32_t now)
{
    if (M.record_pending) {
        M.record_pending = false;
        ho_record_t rec;
        if (!ho_record_decode(M.record_frame, M.record_len, &rec)) {
            /* A record that fails any of its own rules is not a record (H37). */
            ESP_LOGW(TAG, "a record that does not decode: refused whole");
        } else if (!player_board_record_save(&rec)) {
            ESP_LOGW(TAG, "the record did not store");
        } else {
            M.rec = rec;
            M.provisioned = true;
            player_board_handover_peer_drop(M.k7);
            ESP_LOGI(TAG, "record stored: %s%s", rec.has_home ? rec.home_ssid : "(no home network)",
                     rec.has_ap ? " and K7.local" : "");
            enter_connecting(now);
            return;
        }
    }
    if (reached(now, M.request_due_ms)) {
        if (M.requests < PLAYER_NET_REQUESTS)
            send_request(now);
        else
            abandon_ask(now);
    }
}

static void tick_connecting(uint32_t now)
{
    player_board_wifi_t w;
    player_board_wifi_state(&w);
    if (w.associated) {
        enter_connected(&w);
        return;
    }
    if (w.disconnects - M.disconnects0 < PLAYER_NET_JOIN_TRIES &&
        !reached(now, M.attempt_due_ms))
        return;

    int other = 1 - M.network;
    if (!(M.tried & (1u << other)) && begin_join(now, other)) {
        M.tried |= 1u << other;
        return;
    }
    enter_failing(now);
}

static void tick_connected(void)
{
    player_board_wifi_t w;
    player_board_wifi_state(&w);
    if (!w.associated) {
        M.associated = false;
        M.host[0] = '\0';
        ESP_LOGW(TAG, "the network dropped");
        enter_failing(now_ms());
    }
}

static void tick_failing(uint32_t now)
{
    player_board_wifi_t w;
    player_board_wifi_state(&w);
    if (w.associated) {
        enter_connected(&w);
        return;
    }
    if (reached(now, M.phase_until_ms)) {
        enter_sweeping(now, now + PLAYER_NET_SWEEP_MS);
        return;
    }
    if (M.joining &&
        (w.disconnects - M.disconnects0 >= PLAYER_NET_JOIN_TRIES || reached(now, M.attempt_due_ms))) {
        M.joining = false;
        M.attempt_due_ms = now + 1000;
    }
    if (!M.joining && reached(now, M.attempt_due_ms)) {
        M.joining = begin_join(now, preferred());
        if (!M.joining) M.attempt_due_ms = now + 2000;
    }
}

void player_net_tick(uint32_t now)
{
    lock();
    if (M.entry) {
        M.entry = false;
        if (M.state == PLAYER_NET_CONNECTING) enter_connecting(now);
    }
    switch (M.state) {
    case PLAYER_NET_IDLE: break;
    case PLAYER_NET_SWEEPING: tick_sweeping(now); break;
    case PLAYER_NET_ASKING: tick_asking(now); break;
    case PLAYER_NET_CONNECTING: tick_connecting(now); break;
    case PLAYER_NET_CONNECTED: tick_connected(); break;
    case PLAYER_NET_FAILING: tick_failing(now); break;
    }
    unlock();
}

/* ------------------------------------------------------------------ in from the radio */

void player_net_on_frame(const uint8_t src[6], const uint8_t *data, size_t len)
{
    if (!src || !data || len < 2) return;

    lock();
    if (data[1] == HO_TYPE_BEACON && M.state == PLAYER_NET_SWEEPING && !M.beacon_pending) {
        ho_beacon_t b;
        if (ho_beacon_decode(data, len, &b)) {
            memcpy(M.beacon_src, src, sizeof M.beacon_src);
            memcpy(M.beacon_pub, b.pub, sizeof M.beacon_pub);
            M.beacon_pending = true;
        }
    } else if (data[1] == HO_TYPE_RECORD && M.state == PLAYER_NET_ASKING && !M.record_pending) {
        size_t n = len < sizeof M.record_frame ? len : sizeof M.record_frame;
        memcpy(M.record_frame, data, n);
        M.record_len = n;
        M.record_pending = true;
    }
    unlock();
}

/* ------------------------------------------------------------------ the rest */

void player_net_init(void)
{
    memset(&M, 0, sizeof M);
    if (!s_lock) s_lock = xSemaphoreCreateMutex();

    ho_record_t rec;
    if (player_board_record_load(&rec)) {
        M.rec = rec;
        M.provisioned = true;
        M.state = PLAYER_NET_CONNECTING;
        ESP_LOGI(TAG, "a record is stored: %s", rec.has_home ? rec.home_ssid : "the K7's own");
    } else {
        M.state = PLAYER_NET_SWEEPING;
        ESP_LOGI(TAG, "no record: sweeping for a K7");
    }
    M.entry = true;
}

void player_net_status(player_net_status_t *out)
{
    if (!out) return;
    lock();
    memset(out, 0, sizeof *out);
    out->state = M.state;
    out->channel = M.channel;
    out->provisioned = M.provisioned;
    out->has_home = M.rec.has_home;
    out->associated = M.state == PLAYER_NET_CONNECTED;
    out->attempted = M.state == PLAYER_NET_CONNECTING || M.state == PLAYER_NET_FAILING ||
                     M.state == PLAYER_NET_CONNECTED;
    out->requests = M.requests;
    if (M.state == PLAYER_NET_CONNECTING || M.state == PLAYER_NET_FAILING ||
        M.state == PLAYER_NET_CONNECTED) {
        const char *ssid = NULL, *pass = NULL;
        if (network_of(M.network, &ssid, &pass)) snprintf(out->network, sizeof out->network, "%s", ssid);
    }
    snprintf(out->host, sizeof out->host, "%s", M.host);
    unlock();
}

bool player_net_host(char *out, size_t cap)
{
    if (!out || cap == 0) return false;
    lock();
    bool ok = M.host[0] != '\0';
    snprintf(out, cap, "%s", ok ? M.host : "");
    unlock();
    return ok;
}

const char *player_net_state_name(player_net_state_t state)
{
    switch (state) {
    case PLAYER_NET_SWEEPING: return "sweeping";
    case PLAYER_NET_ASKING: return "asking";
    case PLAYER_NET_CONNECTING: return "connecting";
    case PLAYER_NET_CONNECTED: return "connected";
    case PLAYER_NET_FAILING: return "failing";
    case PLAYER_NET_IDLE: break;
    }
    return "idle";
}

static void player_net_task(void *arg)
{
    (void)arg;
    for (;;) {
        player_net_tick(now_ms());
        vTaskDelay(pdMS_TO_TICKS(PLAYER_NET_TICK_MS));
    }
}

void player_net_start_task(void)
{
    /* 6 KB: this task is where the curve runs when a beacon is answered. */
    if (xTaskCreatePinnedToCore(player_net_task, "net", 6144, NULL, 3, NULL, 0) != pdPASS)
        ESP_LOGE(TAG, "the net task did not start");
}
