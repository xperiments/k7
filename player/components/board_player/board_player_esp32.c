/*
 * The K7Player's board: an ESP32 classic (ADR 0097).
 *
 * It provides what player_board promises - NVS for the record, the station, the handover's
 * ESP-NOW radio, the channel, randomness and the curve - and nothing above them. The policy
 * that decides when any of it is used is player_net's, and it is host-tested.
 *
 * The curve is PSA's, not ours: mbedTLS is already linked and the shared module takes over
 * from the shared secret (components/portable/handover).
 */
#include "player_board.h"

#include <stdio.h>
#include <string.h>

#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "errno.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "unistd.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "espnow_stream.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "psa/crypto.h"

static const char *TAG = "board";

/* The record's own key, in the player's own namespace: the K7's record namespace is the
 * K7's, and this device must not be able to be mistaken for one. */
#define RECORD_NAMESPACE "k7player"
#define RECORD_KEY       "record"

static bool s_associated;
static int s_disconnects;
static int s_reason;
static char s_ip[16];

const char *player_board_name(void) { return "esp32"; }

/* ------------------------------------------------------------------ the station */

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == WIFI_EVENT_STA_DISCONNECTED) {
        s_disconnects++;
        s_reason = ((wifi_event_sta_disconnected_t *)data)->reason;
        s_associated = false;
        s_ip[0] = '\0';
    } else if (id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        s_associated = true;
        snprintf(s_ip, sizeof s_ip, IPSTR, IP2STR(&e->ip_info.ip));
    }
}

bool player_board_wifi_join(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0]) return false;

    wifi_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof cfg.sta.ssid);
    if (pass) strlcpy((char *)cfg.sta.password, pass, sizeof cfg.sta.password);
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN; /* the network's own choice, not ours */

    (void)esp_wifi_disconnect();
    esp_err_t e = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "the station refused %s: %s", ssid, esp_err_to_name(e));
        return false;
    }
    s_disconnects = 0;
    s_reason = 0;
    s_associated = false;
    s_ip[0] = '\0';
    return esp_wifi_connect() == ESP_OK;
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
    if (!hostname || !out || cap == 0) return false;
    out[0] = '\0';

    static bool s_mdns;
    if (!s_mdns) {
        if (mdns_init() != ESP_OK) return false;
        s_mdns = true;
    }
    esp_ip4_addr_t addr;
    if (mdns_query_a(hostname, 1000, &addr) != ESP_OK) return false;
    snprintf(out, cap, IPSTR, IP2STR(&addr));
    return true;
}

/* ------------------------------------------------------------------ the stream's socket */

/* ------------------------------------------------------------------ the record */

bool player_board_record_load(ho_record_t *out)
{
    if (!out) return false;
    nvs_handle_t h;
    if (nvs_open(RECORD_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;

    uint8_t blob[HO_RECORD_MAX];
    size_t len = sizeof blob;
    esp_err_t e = nvs_get_blob(h, RECORD_KEY, blob, &len);
    nvs_close(h);
    if (e != ESP_OK) return false;
    /* Decoded through the wire format, so a blob damaged in flash is no record at all. */
    return ho_record_decode(blob, len, out);
}

bool player_board_record_save(const ho_record_t *rec)
{
    if (!rec) return false;
    uint8_t blob[HO_RECORD_MAX];
    size_t len = ho_record_encode(rec, blob, sizeof blob);
    if (len == 0) return false;

    nvs_handle_t h;
    if (nvs_open(RECORD_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = nvs_set_blob(h, RECORD_KEY, blob, len);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK;
}

void player_board_record_drop(void)
{
    nvs_handle_t h;
    if (nvs_open(RECORD_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    (void)nvs_erase_key(h, RECORD_KEY);
    (void)nvs_commit(h);
    nvs_close(h);
}

/* ------------------------------------------------------------------ the handover's radio
 *
 * The mirror of the K7's share seam. The callback runs on the Wi-Fi task and only hands the
 * frame on; the answer is built by player_net on its own task.
 */

static player_board_frame_fn s_frame_cb;
static void *s_frame_ctx;
static player_board_packet_fn s_audio_cb;
static player_board_packet_fn s_tone_cb;

/* The radio is the board's and it has two callers, so it is initialised once and stays up while
 * either of them needs it (the K7's rule, mirrored: espnow-stream.md E10). */
static bool s_now_on;

static bool espnow_up(void)
{
    if (s_now_on) return true;
    if (esp_now_init() != ESP_OK) return false;
    s_now_on = true;
    return true;
}

/* One dispatcher: audio to the stream, the tone to the output, everything else to the
 * handover. */
static void recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!info || !data || len < 2) return;
    if (data[0] == ES_VERSION && data[1] == ES_TYPE_AUDIO) {
        if (s_audio_cb) s_audio_cb(data, (size_t)len);
        return;
    }
    if (data[0] == ES_VERSION && data[1] == ES_TYPE_TONE) {
        if (s_tone_cb) s_tone_cb(data, (size_t)len);
        return;
    }
    if (!s_frame_cb || len <= 0) return;
    s_frame_cb(info->src_addr, data, (size_t)len, s_frame_ctx);
}

bool player_board_espnow_start(player_board_packet_fn on_packet)
{
    if (!espnow_up()) return false;
    s_audio_cb = on_packet;
    esp_now_register_recv_cb(recv_cb);

    /* The status packet goes to everyone: the player cannot know the K7's address, and the K7
     * turns the packet's source into the peer it sends audio to. */
    esp_now_peer_info_t bcast;
    memset(&bcast, 0, sizeof bcast);
    memset(bcast.peer_addr, 0xFF, ESP_NOW_ETH_ALEN);
    bcast.channel = 0;
    bcast.ifidx = WIFI_IF_STA;
    bcast.encrypt = false;
    esp_err_t e = esp_now_add_peer(&bcast);
    return e == ESP_OK || e == ESP_ERR_ESPNOW_EXIST;
}

void player_board_espnow_set_tone(player_board_packet_fn on_tone)
{
    s_tone_cb = on_tone;
}

bool player_board_espnow_send(const uint8_t *data, size_t len)
{
    static const uint8_t bcast[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (!s_now_on || !data || len == 0 || len > ESP_NOW_MAX_DATA_LEN) return false;
    return esp_now_send(bcast, data, len) == ESP_OK;
}

bool player_board_handover_start(player_board_frame_fn on_frame, void *ctx)
{
    if (!espnow_up()) return false;
    s_frame_cb = on_frame;
    s_frame_ctx = ctx;
    esp_now_register_recv_cb(recv_cb);
    return true;
}

void player_board_handover_stop(void)
{
    s_frame_cb = NULL;
    s_frame_ctx = NULL;
    /* The stream still owns the radio when it is on: the sweep borrows it. */
    if (s_audio_cb) return;
    if (!s_now_on) return;
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    s_now_on = false;
}

bool player_board_handover_send(const uint8_t *dst, const uint8_t *data, size_t len)
{
    if (!s_now_on || !dst || !data || len == 0 || len > ESP_NOW_MAX_DATA_LEN) return false;
    return esp_now_send(dst, data, len) == ESP_OK;
}

/* Add a peer, or modify it when it is already there. ESP-NOW has one entry per address, and
 * its `encrypt` flag applies in both directions: the request needs the same address with the
 * flag off, the answer with it on. */
static bool peer_apply(const uint8_t mac[6], bool encrypt, const uint8_t *lmk)
{
    if (!s_now_on || !mac) return false;

    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof peer);
    memcpy(peer.peer_addr, mac, ESP_NOW_ETH_ALEN);
    if (lmk) memcpy(peer.lmk, lmk, ESP_NOW_KEY_LEN);
    peer.channel = 0; /* the radio's current channel */
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = encrypt;

    esp_err_t e = esp_now_add_peer(&peer);
    if (e == ESP_ERR_ESPNOW_EXIST) e = esp_now_mod_peer(&peer);
    return e == ESP_OK;
}

bool player_board_handover_address(const uint8_t mac[6])
{
    return peer_apply(mac, false, NULL);
}

bool player_board_handover_peer(const uint8_t mac[6], const uint8_t lmk[16])
{
    if (!lmk) return false;
    return peer_apply(mac, true, lmk);
}

void player_board_handover_peer_drop(const uint8_t mac[6])
{
    if (!s_now_on || !mac) return;
    esp_now_del_peer(mac);
}

bool player_board_set_channel(uint8_t channel)
{
    if (channel < 1 || channel > 13) return false;
    return esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) == ESP_OK;
}

/* ------------------------------------------------------------------ the curve */

void player_board_random(uint8_t *out, size_t len)
{
    if (out && len) esp_fill_random(out, len);
}

static bool psa_x25519_key(const uint8_t priv[32], mbedtls_svc_key_id_t *key)
{
    static bool s_psa_ready;
    if (!s_psa_ready) {
        if (psa_crypto_init() != PSA_SUCCESS) return false;
        s_psa_ready = true;
    }
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY));
    psa_set_key_bits(&attr, 255);
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_DERIVE | PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attr, PSA_ALG_ECDH);
    return psa_import_key(&attr, priv, 32, key) == PSA_SUCCESS;
}

bool player_board_x25519_public(const uint8_t priv[32], uint8_t pub[32])
{
    mbedtls_svc_key_id_t key;
    if (!priv || !pub || !psa_x25519_key(priv, &key)) return false;
    size_t n = 0;
    psa_status_t st = psa_export_public_key(key, pub, 32, &n);
    psa_destroy_key(key);
    return st == PSA_SUCCESS && n == 32;
}

bool player_board_x25519(const uint8_t priv[32], const uint8_t peer_pub[32], uint8_t shared[32])
{
    mbedtls_svc_key_id_t key;
    if (!priv || !peer_pub || !shared || !psa_x25519_key(priv, &key)) return false;
    size_t n = 0;
    psa_status_t st = psa_raw_key_agreement(PSA_ALG_ECDH, key, peer_pub, 32, shared, 32, &n);
    psa_destroy_key(key);
    if (st != PSA_SUCCESS || n != 32) return false;

    uint8_t any = 0;
    for (int i = 0; i < 32; i++) any |= shared[i];
    return any != 0; /* a low-order point is not a key (H28) */
}

/* ------------------------------------------------------------------ the board */

bool player_board_init(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        if (nvs_flash_erase() != ESP_OK) return false;
        e = nvs_flash_init();
    }
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "nvs: %s", esp_err_to_name(e));
        return false;
    }

    esp_netif_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) return false;
    esp_wifi_set_storage(WIFI_STORAGE_RAM); /* the record is ours, not the stack's */
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) return false;

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL);
    if (esp_wifi_start() != ESP_OK) return false;

    /* No modem sleep. It is IDF's default, and it parks the radio between DTIM beacons: on
     * this side that is a burst of audio every 100 ms, and the access point's buffer cannot
     * hold 3.2 KB of stream, so frames are dropped and TCP spends its time recovering. The
     * K7's adapter disables it for the same reason (board_esp32.c, "bad for streaming"); a
     * player that receives 32 KB/s has an even stronger claim. It is mains-powered. */
    esp_wifi_set_ps(WIFI_PS_NONE);

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_LOGI(TAG, "esp32 classic: %d core(s), revision %d.%d; heap %u B free, largest %u B",
             chip.cores, chip.revision / 100, chip.revision % 100,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    return true;
}
