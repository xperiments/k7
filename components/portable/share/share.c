/*
 * share - the K7's share window.
 *
 * One lock guards the machine because three callers reach it: the window's task (beacons
 * and answers), the panel (start, stop, status) and the Wi-Fi task's receive callback. The
 * callback never does the work: it records who asked, and the next tick - on a task with
 * stack for X25519 - derives the key and sends the record.
 */
#include "share.h"

#include <stdio.h>
#include <string.h>

#include "app_board.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "handover.h"

static const char *TAG = "share";

/* The record's own address when the station has none: the setup network's, always
 * 192.168.4.1 (wifi-provisioning.md P8). */
#define SETUP_NETWORK_IPV4 0xC0A80401u

typedef struct {
    share_state_t state;
    bool     failed;
    uint32_t deadline_ms;
    uint32_t last_now_ms; /* the last clock the machine saw: what the countdown is read from */
    uint8_t  channel;

    uint8_t  priv[HO_KEY_LEN];
    uint8_t  pub[HO_KEY_LEN];

    ho_record_t record;

    bool     has_player;
    uint8_t  player[6];
    int      answers;

    /* A request the Wi-Fi task recorded, waiting for this task to answer it. */
    bool     pending;
    bool     pending_retry;
    uint8_t  pending_src[6];
    uint8_t  pending_pub[HO_KEY_LEN];

    uint8_t  frame[HO_FRAME_MAX];
} machine_t;

static machine_t M;
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task; /* the window's own task, NULL when no window is open */

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void share_task(void *arg);

static bool lock(void)
{
    if (!s_lock) return true; /* before init: the boot is single-threaded */
    return xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE;
}

static void unlock(void)
{
    if (s_lock) xSemaphoreGive(s_lock);
}

/* Wrap-safe "now has reached when", for an uptime that outlives 2^32 ms. */
static bool reached(uint32_t now, uint32_t when) { return (int32_t)(now - when) >= 0; }

/* ------------------------------------------------------------------ the record */

/* The record is the network the device is on: the home one when it has it, and its own
 * setup network always (H31). False when the board's facts cannot make a legal record -
 * which is the window refusing to open rather than opening to answer nothing. */
static bool build_record(const app_board_share_net_t *net, ho_record_t *rec)
{
    memset(rec, 0, sizeof(*rec));

    rec->has_home = net->has_home && net->home_ssid[0] != '\0';
    if (rec->has_home) {
        snprintf(rec->home_ssid, sizeof rec->home_ssid, "%s", net->home_ssid);
        snprintf(rec->home_pass, sizeof rec->home_pass, "%s", net->home_pass);
    }
    rec->has_ap = true;
    snprintf(rec->ap_ssid, sizeof rec->ap_ssid, "%s", SETUP_NETWORK_SSID);
    snprintf(rec->ap_pass, sizeof rec->ap_pass, "%s", net->ap_pass);
    /* The name the player will resolve, from the one place the device keeps it. */
    snprintf(rec->host, sizeof rec->host, "%s", app_board_hostname());
    rec->ipv4 = net->ipv4 ? net->ipv4 : SETUP_NETWORK_IPV4;

    uint8_t probe[HO_FRAME_MAX];
    if (!ho_record_encode(rec, probe, sizeof probe)) {
        ESP_LOGW(TAG, "the network record does not encode: the window stays closed");
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ sending */

static void send_beacon(uint32_t now)
{
    ho_beacon_t b;
    memset(&b, 0, sizeof b);
    memcpy(b.pub, M.pub, sizeof b.pub);
    b.channel = M.channel;

    uint32_t left = reached(now, M.deadline_ms) ? 0 : M.deadline_ms - now;
    uint32_t s = (left + 999) / 1000;
    b.seconds_left = (uint8_t)(s > 60 ? 60 : s);

    uint8_t buf[HO_BEACON_SIZE];
    size_t n = ho_beacon_encode(&b, buf, sizeof buf);
    if (n) app_board_share_send(NULL, buf, n);
}

/* Derive the key, register the player, send it the record. Called with the lock held, on
 * the window's task. */
static void answer(const uint8_t src[6], const uint8_t peer_pub[HO_KEY_LEN])
{
    uint8_t shared[HO_KEY_LEN], lmk[HO_LMK_LEN];
    if (!app_board_x25519(M.priv, peer_pub, shared)) {
        ESP_LOGW(TAG, "the request's key does not agree: not answered");
        return;
    }
    if (!ho_lmk(shared, M.pub, peer_pub, lmk)) {
        ESP_LOGW(TAG, "no handover key from that request: not answered");
        return;
    }
    if (!M.has_player) {
        if (!app_board_share_peer(src, lmk)) {
            ESP_LOGW(TAG, "the radio refused the peer: not answered");
            return;
        }
        memcpy(M.player, src, sizeof M.player);
        M.has_player = true;
        M.state = SHARE_ANSWERED;
        ESP_LOGI(TAG, "answered %02x:%02x:%02x:%02x:%02x:%02x", src[0], src[1], src[2], src[3],
                 src[4], src[5]);
    }

    size_t n = ho_record_encode(&M.record, M.frame, sizeof M.frame);
    if (n == 0) {
        ESP_LOGW(TAG, "the record stopped encoding: the player gets nothing");
        return;
    }
    if (app_board_share_send(M.player, M.frame, n)) M.answers++;
}

static void close_window(void)
{
    if (M.has_player) app_board_share_peer_drop(M.player);
    app_board_share_radio_stop();
    M.state = SHARE_CLOSED;
    M.has_player = false;
    M.answers = 0;
    M.pending = false;
    M.pending_retry = false;
    ESP_LOGI(TAG, "share window closed");
}

/* ------------------------------------------------------------------ the frame in */

static void on_frame(const uint8_t src[6], const uint8_t *data, size_t len, void *ctx)
{
    (void)ctx;
    share_on_frame(src, data, len);
}

void share_on_frame(const uint8_t src[6], const uint8_t *data, size_t len)
{
    ho_request_t req;
    if (!src || !ho_request_decode(data, len, &req)) return;

    lock();
    if (M.state == SHARE_OPEN && !M.pending) {
        memcpy(M.pending_src, src, sizeof M.pending_src);
        memcpy(M.pending_pub, req.pub, sizeof M.pending_pub);
        M.pending = true;
    } else if (M.state == SHARE_ANSWERED && M.has_player && M.answers < SHARE_ANSWERS &&
               memcmp(src, M.player, sizeof M.player) == 0 && !M.pending_retry) {
        M.pending_retry = true;
    }
    unlock();
}

/* ------------------------------------------------------------------ the clock */

void share_tick(uint32_t now)
{
    lock();
    M.last_now_ms = now;
    if (M.state == SHARE_CLOSED) {
        unlock();
        return;
    }
    if (reached(now, M.deadline_ms)) {
        close_window();
        unlock();
        return;
    }
    if (M.pending || M.pending_retry) {
        bool retry = M.pending_retry;
        uint8_t src[6];
        memcpy(src, retry ? M.player : M.pending_src, sizeof src);
        uint8_t pub[HO_KEY_LEN];
        memcpy(pub, M.pending_pub, sizeof pub);
        M.pending = false;
        M.pending_retry = false;
        answer(src, pub);
    } else if (M.state == SHARE_OPEN) {
        send_beacon(now);
    }
    unlock();
}

/* ------------------------------------------------------------------ the panel */

bool share_start(uint32_t now)
{
    lock();
    if (M.state != SHARE_CLOSED) {
        unlock();
        return true; /* already open: the row is a toggle, not a queue */
    }

    /* T15's two numbers: the internal-RAM picture before this window's allocations, and
     * after them, so the window's cost is a subtraction and not a claim. */
    size_t free_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    app_board_share_net_t net;
    memset(&net, 0, sizeof net);
    app_board_share_net(&net);

    ho_record_t rec;
    if (!build_record(&net, &rec)) {
        M.failed = true;
        unlock();
        return false;
    }

    M.record = rec;
    M.channel = net.channel ? net.channel : 1;
    app_board_random(M.priv, sizeof M.priv);
    if (!app_board_x25519_public(M.priv, M.pub)) {
        ESP_LOGW(TAG, "the radio's curve refused a new key");
        M.failed = true;
        unlock();
        return false;
    }
    if (!app_board_share_radio_start(on_frame, NULL)) {
        ESP_LOGW(TAG, "ESP-NOW did not start: the window stays closed");
        M.failed = true;
        unlock();
        return false;
    }

    M.deadline_ms = now + SHARE_WINDOW_MS;
    M.last_now_ms = now;
    M.state = SHARE_OPEN;
    M.failed = false;
    M.has_player = false;
    M.answers = 0;
    M.pending = false;
    M.pending_retry = false;

    /* The window's task exists only while the window does: 6 KB of internal RAM is not
     * reserved at boot for a thing a person uses ninety seconds a year (the same rule as
     * the setup network's access point, wifi-provisioning.md P41). Created here, and it
     * deletes itself once the window is closed. */
    if (!s_task &&
        xTaskCreatePinnedToCore(share_task, "share", 6144, NULL, 2, &s_task, 1) != pdPASS) {
        s_task = NULL;
        ESP_LOGE(TAG, "the share task did not start: the window cannot be driven");
        M.failed = true;
        app_board_share_radio_stop();
        M.state = SHARE_CLOSED;
        unlock();
        return false;
    }

    /* One beacon at once, so a player already sweeping hears the window open rather than
     * a tick later. */
    send_beacon(now);
    /* T15's number, at the moment the window's allocations land - the same census the
     * setup network logs when its access point starts (wifi-provisioning.md P40). */
    ESP_LOGI(TAG, "share window open for %u s on channel %u; internal free %u B before, %u B "
                  "after, largest %u B",
             (unsigned)(SHARE_WINDOW_MS / 1000), (unsigned)M.channel, (unsigned)free_before,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    unlock();
    return true;
}

void share_stop(uint32_t now)
{
    (void)now;
    lock();
    if (M.state != SHARE_CLOSED) close_window();
    unlock();
}

void share_status(share_status_t *out)
{
    if (!out) return;
    lock();
    out->state = M.state;
    out->failed = M.failed;
    out->has_player = M.has_player;
    memcpy(out->player, M.player, sizeof out->player);
    if (M.state == SHARE_CLOSED) {
        out->left_ms = 0;
    } else {
        /* Read from the clock the machine last saw, not a fresh reading: the panel asks
         * twenty-five times a second and the window's own clock is the 200 ms tick, so the
         * countdown is a function of the last tick and a test can own it. */
        out->left_ms =
            reached(M.last_now_ms, M.deadline_ms) ? 0 : M.deadline_ms - M.last_now_ms;
    }
    unlock();
}

/* ------------------------------------------------------------------ the task */

static void share_task(void *arg)
{
    (void)arg;
    for (;;) {
        /* The delay comes first: share_start() has already sent the opening beacon, and a
         * tick straight after it would put two beacons back to back. */
        vTaskDelay(pdMS_TO_TICKS(SHARE_TICK_MS));
        share_tick(now_ms());

        /* Leaving is decided under the same lock share_start() takes, so a window opened
         * while this task is on its way out either finds s_task still set (and this loop
         * carries on) or finds it cleared (and creates a new one). Never neither. */
        lock();
        bool closed = (M.state == SHARE_CLOSED);
        if (closed) s_task = NULL;
        unlock();
        if (closed) break;
    }
    vTaskDelete(NULL);
}

void share_init(void)
{
    memset(&M, 0, sizeof M);
    M.state = SHARE_CLOSED;
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) ESP_LOGE(TAG, "no lock: the window will answer without one");
}
