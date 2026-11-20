/*
 * The ESP32-S3 board adapter: Wi-Fi, the SPI microSD card, and the hardware census.
 *
 * Everything here is board-specific and moves as one: the pin map, the Wi-Fi
 * credentials, the retry-safe SPI bus setup, and the mount diagnostics. The
 * services composition (components/portable/app_services) calls the four functions in
 * app_board.h and knows none of it. The host build has the other adapter.
 */
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_chip_info.h"
#include "esp_event.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "espnow_stream.h"
#include "freertos/semphr.h"
#include "esp_random.h"
#include "esp_partition.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"

#include "mdns.h"
#include "nvs.h"
#include "psa/crypto.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "snapshot.h"

#include "app_board.h"
#include "label_pack.h"
#include "label_store.h"

/* No network is compiled in: the one this device joins lives in an NVS record and is
 * chosen on the device's own setup network (ADR 0076). The record's bytes and its rules
 * are portable (components/portable/wifi_record); this file owns the store and the radio.
 * It is the only flash write that carries a password, and it is plaintext - see the
 * record's own header for why that is stated rather than lamented. */

/* The card is wired over SPI, not the 1-bit SDMMC the specification's §2.3
 * assumed (which read the pins off the original example rather than off this
 * board). The SDMMC pins were connected to nothing, and the mount timed out:
 * "sdmmc_init_ocr: send_op_cond (1) returned 0x107". SPI also leaves the
 * USB-JTAG pins (39/40) alone, so on-chip debugging survives.
 *
 * GPIO0 is the BOOT strapping pin; holding it during reset enters download mode, which
 * is how you flash. It is not a runtime button: next and previous are the screen's DJ
 * block. */
#define SD_CS   10
#define SD_CLK  12
#define SD_MOSI 11
#define SD_MISO 13

#define MOUNT_POINT "/sdcard"

static const char *TAG = "board";

/* ------------------------------------------------------------------ Wi-Fi */

static EventGroupHandle_t wifi_ev;
static int wifi_attempts;
static bool s_provisioned;
static char s_ssid[WIFI_RECORD_SSID_MAX + 1];
static bool s_got_ip;
static int s_disconnects;
static int s_last_reason;
static bool s_ap_on;
static esp_netif_t *s_ap_netif;
static uint16_t s_http_port = 8000;
static char s_local_ip[16];

static void wifi_evt(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        /* Unprovisioned is a state, not a failure: there is no network to dial, so the
         * station stays up and silent until a person chooses one. */
        if (s_provisioned) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (!s_provisioned) return;
        s_got_ip = false;
        s_disconnects++;
        s_last_reason = ((wifi_event_sta_disconnected_t *)data)->reason;
        /* Retrying in silence makes a mistyped SSID look like a dead device, and
         * this device is meant to be diagnosable. First failure, then every tenth,
         * so the log stays bounded. */
        wifi_event_sta_disconnected_t *e = data;
        if (++wifi_attempts == 1 || wifi_attempts % 10 == 0)
            ESP_LOGW(TAG, "Wi-Fi: cannot join \"%s\" (reason %d), attempt %d; "
                          "join K7.local and choose a network at http://192.168.4.1/setup",
                     s_ssid, e->reason, wifi_attempts);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_got_ip = true;
        ip_event_got_ip_t *e = data;
        snprintf(s_local_ip, sizeof s_local_ip, IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "app: http://" IPSTR "/  stream: http://" IPSTR "/stream",
                 IP2STR(&e->ip_info.ip), IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(wifi_ev, 1);
    }
}

/* The panel shares the memory bus with the flash: a write takes the PSRAM window away from
 * the scan for a few microseconds, and the picture stays a band to one side until the LCD
 * engine is restarted (ADR 0055). The composition installs the screen's re-establish here,
 * and every runtime NVS write below calls it once the commit has returned. */
static app_board_flash_settled_fn s_flash_settled;

void app_board_set_flash_settled(app_board_flash_settled_fn fn) { s_flash_settled = fn; }

static void flash_settled(void)
{
    app_board_flash_settled_fn fn = s_flash_settled;
    if (fn) fn();
}

/* An earlier build compiled the network in, and ESP-IDF persisted that station
 * configuration into its own NVS namespace on the first boot. The record is the only copy
 * now, so the old one is erased: wifi-provisioning.md P24 says the flash holds the
 * password once, and a leftover copy is still a password on the flash. Opening READONLY
 * first keeps this from creating the namespace it is trying to remove; a device that
 * never ran the old build has nothing there and costs one failed open. */
static void erase_stale_station_copy(void)
{
    nvs_handle_t h;
    if (nvs_open("nvs.net80211", NVS_READONLY, &h) != ESP_OK) return;
    nvs_close(h);
    if (nvs_open("nvs.net80211", NVS_READWRITE, &h) != ESP_OK) return;
    esp_err_t e = nvs_erase_all(h);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    ESP_LOGW(TAG, "erased the station copy an earlier build left in NVS: %s",
             esp_err_to_name(e));
}

esp_err_t app_board_net_start(uint16_t http_port)
{
    s_http_port = http_port ? http_port : 8000;
    wifi_ev = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_evt, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_evt, NULL));

    erase_stale_station_copy();

    /* The record is the only copy on the flash. ESP-IDF would otherwise persist the
     * station configuration into its own NVS namespace on the first boot, and the two
     * copies would then disagree about which one is the truth. */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    wifi_record_t rec;
    s_provisioned = app_board_wifi_record_load(&rec);
    if (s_provisioned) {
        snprintf(s_ssid, sizeof s_ssid, "%s", rec.ssid);
        /* WIFI_AUTH_OPEN as the threshold is deliberate: it admits an open network and
         * every PSK/SAE mode above it, and the record's validation has already decided
         * that the password is well formed. */
        wifi_config_t wc = {0};
        strlcpy((char *)wc.sta.ssid, rec.ssid, sizeof wc.sta.ssid);
        strlcpy((char *)wc.sta.password, rec.pass, sizeof wc.sta.password);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    } else {
        ESP_LOGW(TAG, "no network stored: join K7.local and choose one at "
                      "http://192.168.4.1/setup");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE); /* modem sleep adds latency spikes: bad for streaming */
    return ESP_OK;
}

const char *app_board_name(void)
{
    return "8048S050C";
}

void app_board_wait_online(void)
{
    if (!wifi_ev) return;
    /* Bounded, and that is the change: an unprovisioned device has no address coming, and
     * waiting forever would keep it from mDNS on its own setup network and from the loop
     * that applies a card's update file. The setup machine owns what happens next. */
    xEventGroupWaitBits(wifi_ev, 1, pdFALSE, pdTRUE, pdMS_TO_TICKS(SETUP_BOOT_WINDOW_MS + 5000));
}

bool app_board_local_ip(char *out, size_t out_size)
{
    if (!out || !out_size || !s_local_ip[0]) return false;
    snprintf(out, out_size, "%s", s_local_ip);
    return true;
}

/* The record. NVS validates the blob's own checksum on read, so a failure here is
 * "there is no usable record", which is the unprovisioned state - never a partial one. */
bool app_board_wifi_record_load(wifi_record_t *out)
{
    if (!out) return false;
    wifi_record_clear(out);

    nvs_handle_t h;
    if (nvs_open(WIFI_RECORD_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    uint8_t blob[WIFI_RECORD_BLOB_MAX];
    size_t len = sizeof blob;
    esp_err_t e = nvs_get_blob(h, WIFI_RECORD_KEY, blob, &len);
    nvs_close(h);
    if (e != ESP_OK) return false;
    return wifi_record_decode(out, blob, len);
}

esp_err_t app_board_wifi_record_save(const wifi_record_t *rec)
{
    if (!rec) return ESP_ERR_INVALID_ARG;

    /* Encode first: a record the codec refuses is never opened, never erased and never
     * written, so a bad password cannot reach the flash or take a good record with it. */
    uint8_t blob[WIFI_RECORD_BLOB_MAX];
    size_t len = wifi_record_encode(rec, blob, sizeof blob);
    if (len == 0 && wifi_record_provisioned(rec)) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    esp_err_t e = nvs_open(WIFI_RECORD_NAMESPACE, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;

    if (!wifi_record_provisioned(rec)) {
        /* "Forget this network": the record goes, and absence is the one way to be
         * unprovisioned. Erasing a key that is not there is not an error. */
        e = nvs_erase_key(h, WIFI_RECORD_KEY);
        if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
    } else {
        e = nvs_set_blob(h, WIFI_RECORD_KEY, blob, len);
    }
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    flash_settled();
    return e;
}

/* The setup network's password: a string key beside the network's record (P25). */
#define SETUP_PASSWORD_KEY "ap"

bool app_board_setup_password_load(char *out, size_t cap)
{
    if (!out || cap == 0) return false;
    out[0] = '\0';
    nvs_handle_t h;
    if (nvs_open(WIFI_RECORD_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = cap;
    esp_err_t e = nvs_get_str(h, SETUP_PASSWORD_KEY, out, &len);
    nvs_close(h);
    if (e != ESP_OK || !wifi_record_valid_password(out)) {
        out[0] = '\0';
        return false;
    }
    return true;
}

esp_err_t app_board_setup_password_save(const char *password)
{
    if (!password) return ESP_ERR_INVALID_ARG;
    if (*password && !wifi_record_valid_password(password)) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    esp_err_t e = nvs_open(WIFI_RECORD_NAMESPACE, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    if (*password) {
        e = nvs_set_str(h, SETUP_PASSWORD_KEY, password);
    } else {
        e = nvs_erase_key(h, SETUP_PASSWORD_KEY);
        if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
    }
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    flash_settled();
    return e;
}

/* ----------------------------------------------------------------- the radio */

void app_board_wifi_state(app_board_wifi_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof *out);
    out->associated = s_got_ip;
    out->disconnects = s_disconnects;
    out->last_reason = s_last_reason;
    if (s_got_ip) snprintf(out->ip, sizeof out->ip, "%s", s_local_ip);
}

esp_err_t app_board_wifi_join(const char *ssid, const char *pass)
{
    if (!ssid || !wifi_record_valid_ssid(ssid)) return ESP_ERR_INVALID_ARG;
    if (pass && *pass && !wifi_record_valid_password(pass)) return ESP_ERR_INVALID_ARG;

    s_disconnects = 0;
    s_last_reason = 0;
    s_got_ip = false;

    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid, ssid, sizeof wc.sta.ssid);
    strlcpy((char *)wc.sta.password, pass ? pass : "", sizeof wc.sta.password);

    (void)esp_wifi_disconnect(); /* not associated is not an error here */
    esp_err_t e = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (e != ESP_OK) return e;

    s_provisioned = true;
    snprintf(s_ssid, sizeof s_ssid, "%s", ssid);
    return esp_wifi_connect();
}

/* The census P40 asks for: with the setup network up AND the stream playing. The
 * access point starts before the encoder takes its ~96 KB, so the honest number is a
 * second one, ten seconds later, when the broadcast is live. One line, once per start. */
static void census_with_stream(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "memory with the setup network up and the stream playing: internal free %u B, "
                  "largest %u B",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

static void census_in_ten_seconds(void)
{
    static esp_timer_handle_t once;
    const esp_timer_create_args_t args = {
        .callback = census_with_stream,
        .name = "census",
    };
    if (!once && esp_timer_create(&args, &once) != ESP_OK) return;
    if (once) esp_timer_start_once(once, 10 * 1000 * 1000);
}

void app_board_wifi_forget(void)
{
    wifi_config_t empty = {0}; /* an empty SSID is not a network: the station idles */
    (void)esp_wifi_disconnect();
    (void)esp_wifi_set_config(WIFI_IF_STA, &empty);
    s_provisioned = false;
    s_got_ip = false;
    s_disconnects = 0;
    s_last_reason = 0;
    s_ssid[0] = '\0';
}

esp_err_t app_board_setup_ap(bool on)
{
    if (on == s_ap_on) return ESP_OK;

    if (on) {
        if (!s_ap_netif) {
            s_ap_netif = esp_netif_create_default_wifi_ap();
            if (!s_ap_netif) return ESP_ERR_NO_MEM;
        }
        wifi_record_t rec;
        const char *pass = app_board_wifi_record_load(&rec)
                               ? wifi_record_setup_password(&rec)
                               : WIFI_RECORD_DEFAULT_SETUP_PASSWORD;
        wifi_config_t ap = {0};
        strlcpy((char *)ap.ap.ssid, SETUP_NETWORK_SSID, sizeof ap.ap.ssid);
        ap.ap.ssid_len = (uint8_t)strlen(SETUP_NETWORK_SSID);
        strlcpy((char *)ap.ap.password, pass, sizeof ap.ap.password);
        ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
        ap.ap.max_connection = 4;
        ap.ap.channel = 1;

        esp_err_t e = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (e != ESP_OK) return e;
        e = esp_wifi_set_config(WIFI_IF_AP, &ap);
        if (e != ESP_OK) return e;
        /* Already started is not a failure: switching the mode restarts the radio. */
        e = esp_wifi_start();
        if (e != ESP_OK && e != ESP_ERR_WIFI_STATE) return e;
        /* The census the specification demands at the exact moment it demands it
         * (wifi-provisioning.md P40): the access point's allocation lands after the
         * encoder's, which is the tightest the device ever is. */
        ESP_LOGI(TAG, "setup network up: %s at http://%s/setup (password %s); "
                      "internal free %u B, largest %u B",
                 SETUP_NETWORK_SSID, SETUP_NETWORK_IP, pass,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        s_ap_on = true;
        census_in_ten_seconds();
        return ESP_OK;
    }

    esp_err_t e = esp_wifi_set_mode(WIFI_MODE_STA);
    if (e != ESP_OK) return e;
    s_ap_on = false;
    ESP_LOGI(TAG, "setup network down");
    /* The station was disconnected by the mode change; the retry loop takes it back. */
    if (s_provisioned) (void)esp_wifi_connect();
    return ESP_OK;
}

int app_board_wifi_scan(app_board_scan_t *out, int max)
{
    if (!out || max <= 0) return -1;

    wifi_scan_config_t sc = {0};
    sc.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    sc.scan_time.active.min = 60;
    sc.scan_time.active.max = 120; /* 13 channels at <=120 ms: about 1.6 s */
    /* A station that is mid-connection refuses a scan (ESP_ERR_WIFI_STATE). The person
     * asking for one is in front of the device, so a short wait and a second try is the
     * right answer - and it is what a join that has just failed leaves behind. */
    esp_err_t se = esp_wifi_scan_start(&sc, true);
    if (se != ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(250));
        se = esp_wifi_scan_start(&sc, true);
        if (se != ESP_OK) {
            ESP_LOGW(TAG, "scan refused: %s", esp_err_to_name(se));
            return -1;
        }
    }

    uint16_t n = 0;
    if (esp_wifi_scan_get_ap_num(&n) != ESP_OK) return -1;
    if (n == 0) {
        /* The driver keeps the scan's buffer until the records are read, even when there
         * are none: skipping this made the *next* scan fail with ESP_ERR_WIFI_STATE. */
        uint16_t none = 0;
        (void)esp_wifi_scan_get_ap_records(&none, NULL);
        return 0;
    }
    if (n > 32) n = 32;

    wifi_ap_record_t *found = heap_caps_malloc(n * sizeof *found, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!found) found = malloc(n * sizeof *found);
    if (!found) return -1;
    uint16_t got = n;
    if (esp_wifi_scan_get_ap_records(&got, found) != ESP_OK) {
        free(found);
        return -1;
    }

    /* One row per network, not per BSSID: several access points broadcast one SSID, and a
     * person is picking a network. The strongest wins, and the list is sorted by signal.
     * A hidden network is left out: the page's "other network" row is its way in. */
    int count = 0;
    for (uint16_t i = 0; i < got && count < max; i++) {
        if (found[i].ssid[0] == '\0') continue;
        const char *ssid = (const char *)found[i].ssid;
        int at = -1;
        for (int k = 0; k < count; k++)
            if (strcmp(out[k].ssid, ssid) == 0) { at = k; break; }
        if (at >= 0) {
            if (found[i].rssi <= out[at].rssi) continue;
        } else {
            at = count++;
            snprintf(out[at].ssid, sizeof out[at].ssid, "%s", ssid);
        }
        out[at].rssi = found[i].rssi;
        out[at].secure = found[i].authmode != WIFI_AUTH_OPEN;
    }
    free(found);

    for (int i = 1; i < count; i++) { /* insertion sort, strongest first */
        app_board_scan_t row = out[i];
        int k = i - 1;
        while (k >= 0 && out[k].rssi < row.rssi) { out[k + 1] = out[k]; k--; }
        out[k + 1] = row;
    }
    return count;
}

esp_err_t app_board_mdns_start(const char *hostname, uint16_t port, const char *path)
{
    if (!hostname || !hostname[0] || !path || !path[0]) return ESP_ERR_INVALID_ARG;
    esp_err_t e = mdns_init();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "mdns_init: %s", esp_err_to_name(e));
        return e;
    }
    mdns_hostname_set(hostname);
    mdns_instance_name_set(hostname);
    char endpoint[96];
    snprintf(endpoint, sizeof endpoint, "http://%s.local:%u%s", hostname, (unsigned)port, path);
    mdns_txt_item_t txt[] = {
        {"path", path},
        {"endpoint", endpoint},
        {"device_type", "mcp:device"},
    };
    e = mdns_service_add(hostname, "_mcp", "_tcp", port, txt, sizeof txt / sizeof txt[0]);
    if (e == ESP_OK) {
        ESP_LOGI(TAG, "mDNS: %s.local _mcp._tcp port %u, endpoint %s",
                 hostname, (unsigned)port, endpoint);
    } else {
        ESP_LOGE(TAG, "mdns_service_add: %s", esp_err_to_name(e));
    }
    return e;
}

/* ---------------------------------------------------------------- SD card */

static bool s_spi_bus_up;
static sdmmc_card_t *s_card;

static esp_err_t sd_mount(const char *music_dir, char *err, size_t err_size)
{
    esp_vfs_fat_sdmmc_mount_config_t mc = {
        .format_if_mount_failed = false,
        /* The broadcast's library scan, the index and the decoder's file are the
         * likely simultaneous users; eight leaves headroom rather than sitting
         * exactly on the limit. */
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t host = SDSPI_HOST_DEFAULT(); /* 20 MHz: reliable on hand wiring */

    /* The retry task comes back through here, and the bus may only be brought up
     * once. A failed mount removes its own device, so retrying is safe. */
    if (!s_spi_bus_up) {
        spi_bus_config_t bus = {
            .mosi_io_num = SD_MOSI,
            .miso_io_num = SD_MISO,
            .sclk_io_num = SD_CLK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            /* 32 KB, not 4 KB: the decoder reads 32 KB at a time, and a 24-bit
             * 192 kHz FLAC needs ~700 KB/s. At 4 KB the SPI driver split each
             * read across two transactions and the card spent more time in
             * command overhead than in data. */
            .max_transfer_sz = 32768,
        };
        esp_err_t e = spi_bus_initialize(host.slot, &bus, SDSPI_DEFAULT_DMA);
        if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(e));
            snprintf(err, err_size, "spi_bus_initialize: %s", esp_err_to_name(e));
            return e;
        }
        s_spi_bus_up = true;
    }

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = SD_CS;
    slot.host_id = host.slot;

    sdmmc_card_t *card = NULL;
    esp_err_t e = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot, &mc, &card);
    if (e == ESP_OK) {
        s_card = card;
        sdmmc_card_print_info(stdout, card);
        ESP_LOGI(TAG, "card on SPI: CS %d, CLK %d, MOSI %d, MISO %d",
                 SD_CS, SD_CLK, SD_MOSI, SD_MISO);
        /* Without this line, "the card mounted but there is nothing to play" and
         * "the card mounted but there is no music directory" look identical from
         * the outside - which is the one question a cardless diagnosis asks. */
        uint64_t total = 0, freeb = 0;
        if (esp_vfs_fat_info(MOUNT_POINT, &total, &freeb) == ESP_OK)
            ESP_LOGI(TAG, "card: %u MB total, %u MB free",
                     (unsigned)(total >> 20), (unsigned)(freeb >> 20));
        struct stat st;
        ESP_LOGI(TAG, "%s %s", music_dir,
                 (stat(music_dir, &st) == 0 && S_ISDIR(st.st_mode)) ? "exists" : "does not exist");
    } else {
        snprintf(err, err_size, "%s (0x%x)", esp_err_to_name(e), (unsigned)e);
    }
    return e;
}

app_board_card_t app_board_card_mount(const char *expected_music_dir)
{
    static char mount_err[160];
    app_board_card_t card = {0};
    mount_err[0] = 0;

    esp_err_t e = sd_mount(expected_music_dir, mount_err, sizeof mount_err);
    if (e == ESP_OK) {
        card.music_dir = expected_music_dir;
    } else {
        card.mount_error = mount_err;
        if (!mount_err[0]) snprintf(mount_err, sizeof mount_err, "%s", esp_err_to_name(e));
    }
    return card;
}

/* The Update file's name changed with the product (ADR 0062): a card written by an older
 * app still holds xtape.db. The reader takes the current name first and falls back to the
 * old one, and reports which it used, because only the file that was actually flashed may
 * be deleted. `st` is filled for the caller's regular-file check. */
static const char *snapshot_update_file(struct stat *st)
{
    static const char *const names[] = {MOUNT_POINT "/k7.db", MOUNT_POINT "/xtape.db"};

    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (stat(names[i], st) == 0) return names[i];
    return NULL;
}

#define SNAPSHOT_UPDATE_PART 0x40

bool app_board_card_probe(int *err)
{
    if (err) *err = 0;
    if (!s_card) {
        if (err) *err = ENODEV;
        return false;
    }

    /* A directory read, not a mount-point stat: the VFS answers "/sdcard" itself, so
     * only a path under it makes the FAT layer ask the card. Failure is the errno the
     * VFS mapped from FATFS - EIO for a card that did not answer, ETIMEDOUT for the
     * volume lock, ENOENT for a missing directory - which is what lets the caller tell
     * one bad transaction from a card that has gone. */
    struct stat st;
    errno = 0;
    if (stat(MOUNT_POINT "/music", &st) == 0) return true;
    if (err && errno) *err = errno;
    return false;
}

void app_board_card_forget(void)
{
    if (!s_card) return;
    esp_err_t e = esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
    if (e != ESP_OK)
        ESP_LOGW(TAG, "the card's volume did not unmount (%s); a reinsert may need a reboot",
                 esp_err_to_name(e));
    s_card = NULL;
}

bool app_board_snapshot_pending(void)
{
    struct stat st;
    const char *file = snapshot_update_file(&st);
    return file && S_ISREG(st.st_mode);
}

/* ------------------------------------------------------------- snapshot */

/* --- the image, mapped ------------------------------------------------- */

/* The mapping the caller holds until app_board_snapshot_release(): one at a time, because
 * the image is copied once at boot and the upload path owns the partition afterwards. */
static const uint8_t *mapped;
static size_t         mapped_size;
static esp_partition_mmap_handle_t mapped_handle;

const uint8_t *app_board_snapshot_image(size_t *size, const char **why, int *state)
{
    static char err[96];

    if (mapped) {
        if (size) *size = mapped_size;
        return mapped;
    }
    if (why) *why = "";
    if (state) *state = SNAPSHOT_NO_PARTITION;

    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, SNAPSHOT_UPDATE_PART, NULL);
    if (!part) {
        if (why) *why = "no snapshot partition in this build";
        return NULL;
    }

    /* The image carries its own length; mapping the whole partition would hand the
     * validator 2 MB of erased tail to reject. Read the header first, then map
     * exactly the image. */
    uint8_t hdr[SNAPSHOT_HEADER_SIZE];
    esp_err_t e = esp_partition_read(part, 0, hdr, sizeof hdr);
    if (e != ESP_OK) {
        snprintf(err, sizeof err, "snapshot partition unreadable: %s", esp_err_to_name(e));
        if (why) *why = err;
        if (state) *state = SNAPSHOT_INVALID;
        return NULL;
    }
    if (!snapshot_magic_ok(hdr)) {
        if (why) *why = "the snapshot partition holds no image";
        if (state) *state = SNAPSHOT_NO_UPDATE;
        return NULL;
    }
    uint32_t total = (uint32_t)hdr[12] | ((uint32_t)hdr[13] << 8) |
                     ((uint32_t)hdr[14] << 16) | ((uint32_t)hdr[15] << 24);
    if (total < SNAPSHOT_HEADER_SIZE || total > part->size) {
        if (why) *why = "the snapshot image's length is not in its partition";
        if (state) *state = SNAPSHOT_INVALID;
        return NULL;
    }

    const void *ptr = NULL;
    esp_partition_mmap_handle_t handle;
    e = esp_partition_mmap(part, 0, total, ESP_PARTITION_MMAP_DATA, &ptr, &handle);
    if (e != ESP_OK) {
        snprintf(err, sizeof err, "snapshot partition unmappable: %s", esp_err_to_name(e));
        if (why) *why = err;
        if (state) *state = SNAPSHOT_INVALID;
        return NULL;
    }
    mapped = (const uint8_t *)ptr;
    mapped_size = total;
    mapped_handle = handle;
    if (state) *state = SNAPSHOT_OK;
    if (size) *size = mapped_size;
    return mapped;
}

void app_board_snapshot_release(void)
{
    if (!mapped) return;
    esp_partition_munmap(mapped_handle);
    mapped = NULL;
    mapped_size = 0;
    mapped_handle = 0;
}

/* ------------------------------------------------------------------- art */

/* The user's one K7Art pack (ADR 0111, art-packs.md §5): 1 MB at 0x610000, read in place
 * and written whole. The factory sheet is not here - it stays embedded in the app - so an
 * erased partition means "factory alone", not "no art". Two images may be open at once
 * (the active pack and one being read out by GET /api/art/pack), which is why the mapping
 * is per-image and the writer only runs after the portable endpoint has let its own go. */
#define ART_PART 0x41

struct label_store_image {
    const uint8_t             *bytes;
    size_t                     size;
    esp_partition_mmap_handle_t handle;
};

static const esp_partition_t *art_partition(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ART_PART, NULL);
}

label_store_state_t label_store_image_get(label_store_image_t **out, const uint8_t **bytes,
                                          size_t *size, char *why, size_t why_size)
{
    if (out) *out = NULL;
    if (bytes) *bytes = NULL;
    if (size) *size = 0;
    if (why && why_size) why[0] = 0;

    const esp_partition_t *part = art_partition();
    if (!part) {
        if (why) snprintf(why, why_size, "no art partition in this build");
        return LABEL_STORE_NONE;
    }

    /* A full erase leaves the partition at 0xFF. That is the factory state, not a bad
     * pack: the portable reader would otherwise report "magic is not K7ART" on a device
     * that has never been given art. */
    uint8_t hdr[16];
    esp_err_t e = esp_partition_read(part, 0, hdr, sizeof hdr);
    if (e != ESP_OK) {
        if (why) snprintf(why, why_size, "the art partition is unreadable: %s", esp_err_to_name(e));
        return LABEL_STORE_UNREADABLE;
    }
    bool erased = true;
    for (size_t i = 0; i < sizeof hdr; i++)
        if (hdr[i] != 0xFF) erased = false;
    if (erased) {
        if (why) snprintf(why, why_size, "the art partition holds no pack");
        return LABEL_STORE_NONE;
    }

    label_store_image_t *image = calloc(1, sizeof *image);
    if (!image) {
        if (why) snprintf(why, why_size, "no memory for the art image");
        return LABEL_STORE_UNREADABLE;
    }
    const void *ptr = NULL;
    e = esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, &ptr, &image->handle);
    if (e != ESP_OK) {
        free(image);
        if (why) snprintf(why, why_size, "the art partition is unmappable: %s", esp_err_to_name(e));
        return LABEL_STORE_UNREADABLE;
    }
    image->bytes = (const uint8_t *)ptr;
    image->size = part->size;
    if (out) *out = image;
    if (bytes) *bytes = image->bytes;
    if (size) *size = image->size;
    return LABEL_STORE_PRESENT;
}

void label_store_image_put(label_store_image_t *image)
{
    if (!image) return;
    esp_partition_munmap(image->handle);
    free(image);
}

/* Read the partition back over what was just written. The writer has an internal stack;
 * the comparison is done in small pages so neither buffer is large. */
static bool art_reads_back(const esp_partition_t *part, const uint8_t *bytes, size_t len,
                           char *why, size_t why_size)
{
    uint8_t page[256];
    for (size_t off = 0; off < len; off += sizeof page) {
        size_t n = len - off < sizeof page ? len - off : sizeof page;
        esp_err_t e = esp_partition_read(part, off, page, n);
        if (e != ESP_OK) {
            if (why) snprintf(why, why_size, "the art partition did not read back: %s",
                              esp_err_to_name(e));
            return false;
        }
        if (memcmp(page, bytes + off, n) != 0) {
            if (why) snprintf(why, why_size, "the art partition did not read back the same bytes");
            return false;
        }
    }
    return true;
}

bool label_store_write(const uint8_t *bytes, size_t len, char *why, size_t why_size)
{
    if (why && why_size) why[0] = 0;
    const esp_partition_t *part = art_partition();
    if (!part) {
        if (why) snprintf(why, why_size, "no art partition in this build");
        return false;
    }
    if (!bytes || len < LABEL_PACK_HEADER_SIZE || len > LABEL_PACK_MAX || len > part->size) {
        if (why) snprintf(why, why_size, "the pack is not between %u and %u bytes",
                          (unsigned)LABEL_PACK_HEADER_SIZE, (unsigned)LABEL_PACK_MAX);
        return false;
    }
    esp_err_t e = esp_partition_erase_range(part, 0, part->size);
    if (e != ESP_OK) {
        if (why) snprintf(why, why_size, "the art partition would not erase: %s", esp_err_to_name(e));
        return false;
    }
    e = esp_partition_write(part, 0, bytes, len);
    if (e != ESP_OK) {
        if (why) snprintf(why, why_size, "the art partition refused the write: %s", esp_err_to_name(e));
        return false;
    }
    if (!art_reads_back(part, bytes, len, why, why_size)) return false;
    ESP_LOGI(TAG, "art: %u bytes written and verified", (unsigned)len);
    return true;
}

bool label_store_erase(char *why, size_t why_size)
{
    if (why && why_size) why[0] = 0;
    const esp_partition_t *part = art_partition();
    if (!part) {
        if (why) snprintf(why, why_size, "no art partition in this build");
        return false;
    }
    esp_err_t e = esp_partition_erase_range(part, 0, part->size);
    if (e != ESP_OK) {
        if (why) snprintf(why, why_size, "the art partition would not erase: %s", esp_err_to_name(e));
        return false;
    }
    ESP_LOGI(TAG, "art: erased; the factory sheet alone");
    return true;
}

/* --- the update -------------------------------------------------------- */

typedef struct {
    FILE *f;
} file_src_t;

static uint32_t file_size_cb(void *ctx)
{
    file_src_t *s = (file_src_t *)ctx;
    long n = -1;
    if (fseek(s->f, 0, SEEK_END) == 0) n = ftell(s->f);
    if (fseek(s->f, 0, SEEK_SET) != 0) return 0;
    return n > 0 ? (uint32_t)n : 0;
}

static bool file_read_cb(void *ctx, uint32_t off, uint8_t *data, uint32_t n)
{
    file_src_t *s = (file_src_t *)ctx;
    if (fseek(s->f, (long)off, SEEK_SET) != 0) return false;
    return fread(data, 1, n, s->f) == n;
}

typedef struct {
    const esp_partition_t *part;
} flash_tgt_t;

static bool flash_erase_cb(void *ctx, uint32_t off, uint32_t n)
{
    flash_tgt_t *t = (flash_tgt_t *)ctx;
    return esp_partition_erase_range(t->part, off, n) == ESP_OK;
}
static bool flash_write_cb(void *ctx, uint32_t off, const uint8_t *data, uint32_t n)
{
    flash_tgt_t *t = (flash_tgt_t *)ctx;
    return esp_partition_write(t->part, off, data, n) == ESP_OK;
}
static bool flash_read_cb(void *ctx, uint32_t off, uint8_t *data, uint32_t n)
{
    flash_tgt_t *t = (flash_tgt_t *)ctx;
    return esp_partition_read(t->part, off, data, n) == ESP_OK;
}

/* --- an image pushed over HTTP ------------------------------------------ */

typedef struct {
    const uint8_t *data;
    size_t         len;
} mem_src_t;

static uint32_t mem_size_cb(void *ctx) { return (uint32_t)((mem_src_t *)ctx)->len; }

static bool mem_read_cb(void *ctx, uint32_t off, uint8_t *out, uint32_t n)
{
    mem_src_t *s = (mem_src_t *)ctx;
    if (off > s->len || n > s->len - off) return false;
    memcpy(out, s->data + off, n);
    return true;
}

app_board_flash_t app_board_snapshot_flash(const uint8_t *image, size_t len, char *why,
                                           size_t why_size)
{
    if (why && why_size) why[0] = 0;
    if (!image || !len) {
        if (why) snprintf(why, why_size, "the upload is empty");
        return APP_BOARD_FLASH_INVALID;
    }
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, SNAPSHOT_UPDATE_PART, NULL);
    if (!part) {
        if (why) snprintf(why, why_size, "no snapshot partition in this build");
        return APP_BOARD_FLASH_NO_PARTITION;
    }
    if (len > part->size) {
        if (why) snprintf(why, why_size, "the upload is larger than the partition");
        return APP_BOARD_FLASH_INVALID;
    }

    mem_src_t   msrc = {image, len};
    flash_tgt_t ftgt = {part};
    snapshot_source_t src = {&msrc, mem_size_cb, mem_read_cb};
    snapshot_target_t tgt = {&ftgt, part->size, flash_erase_cb, flash_write_cb, flash_read_cb};
    snapshot_result_t r = snapshot_apply(&src, &tgt, why, why_size);
    if (r == SNAPSHOT_OK) {
        ESP_LOGI(TAG, "snapshot: %u bytes written over HTTP, verified", (unsigned)len);
        return APP_BOARD_FLASH_OK;
    }
    ESP_LOGW(TAG, "snapshot: the upload was refused (%s)", why && why[0] ? why : "no reason");
    return r == SNAPSHOT_NO_MEM ? APP_BOARD_FLASH_FAILED : APP_BOARD_FLASH_INVALID;
}

static void reboot_cb(void *arg) { (void)arg; esp_restart(); }

void app_board_reboot_soon(uint32_t ms)
{
    esp_timer_handle_t t = NULL;
    const esp_timer_create_args_t args = {.callback = reboot_cb, .name = "xtape-restart"};
    if (esp_timer_create(&args, &t) == ESP_OK) esp_timer_start_once(t, (uint64_t)ms * 1000u);
}

app_board_update_t app_board_snapshot_update(void)
{
    char err[96] = {0};
    if (sd_mount(MOUNT_POINT "/music", err, sizeof err) != ESP_OK) {
        ESP_LOGW(TAG, "snapshot: no card to check for an update (%s)", err);
        return APP_BOARD_UPDATE_NONE;
    }

    app_board_update_t out = APP_BOARD_UPDATE_FAILED;
    FILE *f = NULL;

    struct stat st;
    const char *file = snapshot_update_file(&st);
    if (!file) {
        out = APP_BOARD_UPDATE_NONE;
        goto done;
    }
    if (!S_ISREG(st.st_mode)) {
        ESP_LOGE(TAG, "snapshot: %s is not a file; left alone", file);
        goto done;
    }

    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, SNAPSHOT_UPDATE_PART, NULL);
    if (!part) {
        ESP_LOGE(TAG, "snapshot: no snapshot partition in this build");
        goto done;
    }

    f = fopen(file, "rb");
    if (!f) {
        ESP_LOGE(TAG, "snapshot: cannot open %s", file);
        goto done;
    }

    file_src_t  fsrc = {f};
    flash_tgt_t ftgt = {part};
    snapshot_source_t src = {&fsrc, file_size_cb, file_read_cb};
    snapshot_target_t tgt = {&ftgt, part->size, flash_erase_cb, flash_write_cb, flash_read_cb};

    char why[96] = {0};
    snapshot_result_t r = snapshot_apply(&src, &tgt, why, sizeof why);
    fclose(f);
    f = NULL;

    if (r != SNAPSHOT_OK) {
        ESP_LOGE(TAG, "snapshot: not applied (%s); %s is left in place", why, file);
        goto done;
    }
    /* Only now: the card holds the master copy until the flash has agreed. */
    if (remove(file) == 0)
        ESP_LOGI(TAG, "snapshot: flashed and verified; the update file is deleted");
    else
        ESP_LOGW(TAG, "snapshot: flashed and verified, but %s could not be deleted", file);
    out = APP_BOARD_UPDATE_APPLIED;

done:
    if (f) fclose(f);
    esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
    s_card = NULL;
    return out;
}

/* P2/P3: what the memory available actually is, before anything is allocated, so
 * a PSRAM-less or wrongly-configured board is diagnosable rather than mysterious. */
void app_board_log_hardware(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash = 0;
    esp_flash_get_size(NULL, &flash);
#if defined(CONFIG_SPIRAM_MODE_OCT)
    const char *mode = "octal";
#elif defined(CONFIG_SPIRAM_MODE_QUAD)
    const char *mode = "quad";
#else
    const char *mode = "none";
#endif
    ESP_LOGI(TAG, "chip: %s, %d core(s), revision %d.%d", CONFIG_IDF_TARGET,
             chip.cores, chip.revision / 100, chip.revision % 100);
    ESP_LOGI(TAG, "flash: %u MB; psram: %u KB (%s mode, CONFIG_SPIRAM_MODE is not auto-detected)",
             (unsigned)(flash / (1024 * 1024)), (unsigned)(esp_psram_get_size() / 1024), mode);
    ESP_LOGI(TAG, "heap at startup: internal %u B free, external %u B free",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

/* --------------------------------------------------------------- the share window
 *
 * ESP-NOW is the board's; the window's policy is components/portable/share's. The receive
 * callback runs on the Wi-Fi task and only hands the frame on: the portable side records
 * it and answers from its own task, where there is stack for X25519.
 *
 * The interface matters. A provisioned, associated device sends on its station; a device
 * with no network is an access point (the setup network is up), and its station interface
 * has no channel of its own - so the window rides the AP interface, on the AP's channel.
 */

static app_board_share_frame_fn s_share_cb;
static void *s_share_ctx;
static bool s_share_on;
static uint8_t s_share_ifidx = WIFI_IF_STA;

/* The radio is the board's, and it now has two callers: the share window and the player's
 * audio (ADR 0103). ESP-NOW is initialised once and stays up while the audio uses it, so a
 * window opening and closing does not take the stream's radio with it. */
static bool s_now_up;

static bool espnow_up(void)
{
    if (s_now_up) return true;
    if (esp_now_init() != ESP_OK) return false;
    s_now_up = true;
    return true;
}

/* ------------------------------------------------------------------ the audio emitter */

static es_emitter_t s_audio;
static uint8_t *s_audio_arena;
static SemaphoreHandle_t s_audio_lock;
static uint8_t s_audio_peer[ESP_NOW_ETH_ALEN];
static bool s_audio_has_peer;
/* The interface the peer was registered on. A player can speak before the station has an IP,
 * and the peer then lands on the AP interface; it has to follow the station when the IP
 * arrives, or every send waits for the setup AP (and stops when the AP closes). */
static uint8_t s_audio_peer_ifidx;
/* The stream's PHY rate. ESP-NOW defaults to 1 Mbps, which at 256 kbps is a third of the radio;
 * HT20/MCS0 is 6.5 Mbps, and the peer is one ESP32 a room away, so the rate has margin. */
#define AUDIO_ESPNOW_RATE WIFI_PHY_RATE_MCS0_LGI
static bool s_audio_on;
static int64_t s_audio_logged_us;

/* The emitter's radio: unicast, plaintext, to the player that last spoke. A player that has not
 * spoken has no peer and nothing is sent to it - the status packet opens the credit first. */
/* A run of failed sends is the association, not the packet: the station can sit on an
 * access point that has stopped serving it without ever getting WIFI_EVENT_STA_DISCONNECTED,
 * and then nothing reconnects it - the board is alive, its counters freeze, and only a reset
 * brings it back (found on the bench, 2026-11-19). So the stream's own failures are the
 * watchdog: after this many in a row, drop the association and dial it again. */
#define AUDIO_SEND_DISCONNECTED 30 /* about a second of frames at 38 a second */

/* ------------------------------------------------------------- the radio's work queue
 *
 * Nothing that can block runs in the Wi-Fi task. The receive callback copies the packet into
 * this queue and returns; one task of ours does the sends, the credit and the NACKs, and the
 * recovery. Before it, a status packet made the callback take the emitter's lock and send from
 * inside the Wi-Fi task - and when a send, or the re-association it starts, wedged, the lock
 * stayed taken: the broadcast blocked on its way to the radio and the station stopped encoding
 * while the device still looked alive (EN-10, found on the bench after 9 h 46 m).
 *
 * The queue is also what keeps the producer out of the lock: the broadcast copies a frame and
 * returns. A full queue drops the frame; the player's next gap names it and the history answers
 * it if it is still there.
 */
#define AUDIO_FRAME_QUEUE 4
#define AUDIO_CTRL_QUEUE  4

typedef struct {
    uint16_t len;
    uint8_t data[ES_MAX_FRAME];
} audio_frame_item_t;

typedef struct {
    uint8_t kind; /* 0 = the player's status, 1 = a share-window frame */
    uint8_t src[ESP_NOW_ETH_ALEN];
    uint16_t len;
    uint8_t data[ESP_NOW_MAX_DATA_LEN];
} audio_ctrl_item_t;

static QueueHandle_t s_audio_frames;
static QueueHandle_t s_audio_ctrl;
static StaticQueue_t s_audio_frames_struct;
static StaticQueue_t s_audio_ctrl_struct;
/* The frame queue's storage is external: 4 frames of room is 5.8 KB, and internal RAM is what
 * the card's DMA and the encoder need. The control queue is small enough to stay in .bss. */
static uint8_t s_audio_ctrl_store[AUDIO_CTRL_QUEUE * sizeof(audio_ctrl_item_t)];
static SemaphoreHandle_t s_audio_signal;
static TaskHandle_t s_audio_worker;
static uint32_t s_audio_lost;

/* The tone: the panel's four values, copied by the UI task and sent by the worker only when the
 * player's last status said it applies them (espnow-stream.md E12/E13). The values are absolute
 * and the worker repeats them once a second, so a lost packet is repaired without an ack. The
 * lock is only ever held for the four floats, never across a send. */
static SemaphoreHandle_t s_tone_lock;
static float s_tone_band_db[3];
static float s_tone_gain_db;
static uint32_t s_tone_gen;   /* bumped on every copy, so a send cannot clear a newer change */
static bool s_tone_dirty;
static int64_t s_tone_sent_us;
static volatile bool s_tone_capable;

static void audio_apply_status(const uint8_t src[6], const uint8_t *data, size_t len);

static bool audio_send(const uint8_t *pkt, size_t len, void *ctx)
{
    (void)ctx;
    if (!s_audio_has_peer || !s_now_up) return false;

    static uint16_t s_failures;
    static int64_t s_last_reconnect_us;
    static uint8_t s_reconnects;
    if (esp_now_send(s_audio_peer, pkt, len) == ESP_OK) {
        s_failures = 0;
        s_reconnects = 0;
        return true;
    }

    if (++s_failures >= AUDIO_SEND_DISCONNECTED) {
        int64_t now = esp_timer_get_time();
        if (now - s_last_reconnect_us > 10000000) { /* at most once every ten seconds */
            s_last_reconnect_us = now;
            s_reconnects++;
            if (s_reconnects > 3) {
                /* Three re-associations in a row and still nothing: the station is wedged in a
                 * way disconnect/connect does not clear, and a dead K7 is worse than a reboot. */
                ESP_LOGE(TAG, "the radio will not come back: rebooting");
                vTaskDelay(pdMS_TO_TICKS(100));
                esp_restart();
            }
            ESP_LOGW(TAG, "the radio stopped sending: re-associating (%u)", s_reconnects);
            esp_wifi_disconnect();
            esp_wifi_connect();
        }
        s_failures = 0;
    }
    return false;
}

static void audio_log_census(void)
{
    int64_t now = esp_timer_get_time();
    if (now - s_audio_logged_us < 5000000) return;
    s_audio_logged_us = now;

    es_emitter_stats_t st;
    if (xSemaphoreTake(s_audio_lock, portMAX_DELAY) == pdTRUE) {
        es_emitter_stats(&s_audio, &st);
        xSemaphoreGive(s_audio_lock);
    }
    ESP_LOGI(TAG, "espnow audio: %u frames, %u resent, %u held, %u dropped, %u packets, %s",
             (unsigned)st.sent, (unsigned)st.resent, (unsigned)st.held, (unsigned)st.dropped,
             (unsigned)st.packets, s_audio_has_peer ? "a player is listening" : "no player yet");
    /* A frame the worker could not take: the station never waited for it, which is the point,
     * but the player's gap will name it and the history has it only if it got in. */
    if (s_audio_lost) {
        ESP_LOGW(TAG, "espnow audio: %u frames lost to a full queue", (unsigned)s_audio_lost);
        s_audio_lost = 0;
    }
}

/* The player's status packet: the first one names the player, and every one opens the credit
 * and asks for frames again. Runs in the worker, so every send and the re-association it may
 * start happen in a task. */
static void audio_apply_status(const uint8_t src[6], const uint8_t *data, size_t len)
{
    if (!s_audio_on || !s_audio_lock) return;

    if (!s_audio_has_peer) {
        esp_now_peer_info_t peer;
        memset(&peer, 0, sizeof peer);
        memcpy(peer.peer_addr, src, ESP_NOW_ETH_ALEN);
        peer.channel = 0; /* the current channel */
        peer.ifidx = s_got_ip ? WIFI_IF_STA : WIFI_IF_AP;
        peer.encrypt = false;
        esp_err_t e = esp_now_add_peer(&peer);
        if (e != ESP_OK && e != ESP_ERR_ESPNOW_EXIST) return;
        memcpy(s_audio_peer, src, ESP_NOW_ETH_ALEN);
        s_audio_has_peer = true;
        s_audio_peer_ifidx = (uint8_t)peer.ifidx;

        esp_now_rate_config_t rate = {
            .phymode = WIFI_PHY_MODE_HT20,
            .rate = AUDIO_ESPNOW_RATE,
            .ersu = false,
            .dcm = false,
        };
        esp_err_t re = esp_now_set_peer_rate_config(s_audio_peer, &rate);
        if (re != ESP_OK)
            ESP_LOGW(TAG, "the player's radio would not take the pinned rate: %s",
                     esp_err_to_name(re));
        else
            ESP_LOGI(TAG, "espnow audio: the player pinned to HT20/MCS0, 6.5 Mbps");
        ESP_LOGI(TAG, "espnow audio: a player at %02x:%02x:%02x:%02x:%02x:%02x",
                 src[0], src[1], src[2], src[3], src[4], src[5]);
    }

    /* The peer follows the station, not the moment the player first spoke: a status can
     * arrive before the IP event, and a peer left on the AP interface sends only while the
     * setup AP is up. Move it once, on the status after the interface changes. */
    if (s_audio_has_peer) {
        uint8_t want = (uint8_t)(s_got_ip ? WIFI_IF_STA : WIFI_IF_AP);
        if (s_audio_peer_ifidx != want) {
            esp_now_peer_info_t peer;
            memset(&peer, 0, sizeof peer);
            memcpy(peer.peer_addr, s_audio_peer, ESP_NOW_ETH_ALEN);
            peer.channel = 0;
            peer.ifidx = (wifi_interface_t)want;
            peer.encrypt = false;
            esp_err_t e = esp_now_mod_peer(&peer);
            if (e == ESP_OK) {
                s_audio_peer_ifidx = want;
                ESP_LOGI(TAG, "espnow audio: the player's peer follows the %s",
                         want == WIFI_IF_STA ? "station" : "access point");
            } else {
                ESP_LOGW(TAG, "espnow audio: the player's peer will not move: %s",
                         esp_err_to_name(e));
            }
        }
    }

    bool from_peer = s_audio_has_peer && memcmp(src, s_audio_peer, ESP_NOW_ETH_ALEN) == 0;
    if (xSemaphoreTake(s_audio_lock, portMAX_DELAY) == pdTRUE) {
        es_emitter_status(&s_audio, data, len);
        /* Only the peer's status may speak for the player: another device's status must not
         * clear the capability the player set. */
        bool capable = s_tone_capable;
        if (from_peer) capable = s_audio.tone_capable && s_tone_lock != NULL;
        if (capable != s_tone_capable) {
            s_tone_capable = capable;
            if (capable) {
                if (s_tone_lock && xSemaphoreTake(s_tone_lock, portMAX_DELAY) == pdTRUE) {
                    s_tone_gen++;
                    s_tone_dirty = true;
                    xSemaphoreGive(s_tone_lock);
                }
                ESP_LOGI(TAG, "espnow tone: the player applies the tone; the broadcast is flat");
            } else {
                ESP_LOGI(TAG, "espnow tone: the tone stays on the broadcast");
            }
        }
        xSemaphoreGive(s_audio_lock);
    }
}

/* The tone packet: one 10-byte unicast to the player, sent by the worker. The repeat is the
 * repair; a send that fails leaves the values dirty and the next pass tries again. */
static void audio_send_tone(void)
{
    if (!s_audio_on || !s_audio_has_peer || !s_tone_capable || !s_tone_lock) return;
    int64_t now = esp_timer_get_time();

    float band[3], gain;
    uint32_t gen;
    bool dirty;
    if (xSemaphoreTake(s_tone_lock, portMAX_DELAY) != pdTRUE) return;
    band[0] = s_tone_band_db[0];
    band[1] = s_tone_band_db[1];
    band[2] = s_tone_band_db[2];
    gain = s_tone_gain_db;
    gen = s_tone_gen;
    dirty = s_tone_dirty;
    xSemaphoreGive(s_tone_lock);

    if (!dirty && s_tone_sent_us != 0 && now - s_tone_sent_us < 1000000) return;

    es_tone_t t = {
        .gain_cdb = (int16_t)lroundf(gain * 100.0f),
        .low_cdb = (int16_t)lroundf(band[0] * 100.0f),
        .mid_cdb = (int16_t)lroundf(band[1] * 100.0f),
        .high_cdb = (int16_t)lroundf(band[2] * 100.0f),
    };
    uint8_t pkt[ES_TONE_SIZE];
    if (!es_tone_encode(&t, pkt, sizeof pkt)) return;
    if (!audio_send(pkt, sizeof pkt, NULL)) return;

    s_tone_sent_us = now;
    if (xSemaphoreTake(s_tone_lock, portMAX_DELAY) != pdTRUE) return;
    if (s_tone_gen == gen) s_tone_dirty = false; /* a newer value stays dirty */
    xSemaphoreGive(s_tone_lock);
}

/* A share-window frame, handled by the window's own code - in the worker, not in the Wi-Fi
 * task, for the same reason: the answer is a send. */
static void audio_dispatch_share(const uint8_t src[6], const uint8_t *data, size_t len)
{
    app_board_share_frame_fn cb = s_share_cb;
    void *ctx = s_share_ctx;
    if (cb) cb(src, data, len, ctx);
}

static void audio_worker_task(void *arg)
{
    (void)arg;
    audio_frame_item_t frame;
    audio_ctrl_item_t ctrl;
    for (;;) {
        if (xSemaphoreTake(s_audio_signal, portMAX_DELAY) != pdTRUE) continue;
        for (;;) {
            bool worked = false;
            while (s_audio_frames && xQueueReceive(s_audio_frames, &frame, 0) == pdTRUE) {
                if (s_audio_on && s_audio_lock &&
                    xSemaphoreTake(s_audio_lock, portMAX_DELAY) == pdTRUE) {
                    es_emitter_push(&s_audio, frame.data, frame.len);
                    xSemaphoreGive(s_audio_lock);
                }
                worked = true;
            }
            while (s_audio_ctrl && xQueueReceive(s_audio_ctrl, &ctrl, 0) == pdTRUE) {
                if (ctrl.kind == 0) audio_apply_status(ctrl.src, ctrl.data, ctrl.len);
                else audio_dispatch_share(ctrl.src, ctrl.data, ctrl.len);
                worked = true;
            }
            audio_send_tone();
            if (!worked) break;
            audio_log_census();
        }
    }
}

/* One task, created on first use: the worker carries the audio's sends, so it must exist even
 * when the transport is HTTP, because the share window uses the same radio and the same queue. */
static bool audio_worker_start(void)
{
    if (s_audio_worker) return true;
    if (!s_audio_signal &&
        !(s_audio_signal = xSemaphoreCreateCounting(AUDIO_FRAME_QUEUE + AUDIO_CTRL_QUEUE, 0)))
        return false;
    if (!s_audio_frames) {
        size_t bytes = AUDIO_FRAME_QUEUE * sizeof(audio_frame_item_t);
        uint8_t *store = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!store) store = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
        if (!store) return false;
        s_audio_frames = xQueueCreateStatic(AUDIO_FRAME_QUEUE, sizeof(audio_frame_item_t), store,
                                            &s_audio_frames_struct);
        if (!s_audio_frames) return false;
    }
    if (!s_audio_ctrl) {
        s_audio_ctrl = xQueueCreateStatic(AUDIO_CTRL_QUEUE, sizeof(audio_ctrl_item_t),
                                          s_audio_ctrl_store, &s_audio_ctrl_struct);
        if (!s_audio_ctrl) return false;
    }
    if (xTaskCreatePinnedToCore(audio_worker_task, "espnow", 6144, NULL, 5, &s_audio_worker, 0) !=
        pdPASS) {
        s_audio_worker = NULL;
        return false;
    }
    return true;
}

/* The producer's side: copy and return. Never a lock, never a send, never a wait. */
static void audio_post_frame(const uint8_t *data, size_t len)
{
    if (!s_audio_on || !s_audio_frames || !data || len == 0 || len > ES_MAX_FRAME) return;
    audio_frame_item_t it;
    it.len = (uint16_t)len;
    memcpy(it.data, data, len);
    if (xQueueSend(s_audio_frames, &it, 0) == pdTRUE)
        xSemaphoreGive(s_audio_signal);
    else
        s_audio_lost++;
}

static void audio_post_status(const uint8_t src[6], const uint8_t *data, size_t len)
{
    if (!s_audio_ctrl || !data || len == 0 || len > ESP_NOW_MAX_DATA_LEN) return;
    audio_ctrl_item_t it = {.kind = 0, .len = (uint16_t)len};
    memcpy(it.src, src, sizeof it.src);
    memcpy(it.data, data, len);
    if (xQueueSend(s_audio_ctrl, &it, 0) == pdTRUE)
        xSemaphoreGive(s_audio_signal);
    else
        s_audio_lost++;
}

static void audio_post_share(const uint8_t src[6], const uint8_t *data, size_t len)
{
    if (!s_audio_ctrl || !data || len == 0 || len > ESP_NOW_MAX_DATA_LEN) return;
    audio_ctrl_item_t it = {.kind = 1, .len = (uint16_t)len};
    memcpy(it.src, src, sizeof it.src);
    memcpy(it.data, data, len);
    if (xQueueSend(s_audio_ctrl, &it, 0) == pdTRUE)
        xSemaphoreGive(s_audio_signal);
    else
        s_audio_lost++;
}

/* The radio's only receive path, and it does nothing but hand over: the Wi-Fi task must return
 * to the driver, whatever the packet asked for. */
static void espnow_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!info || !data || len < 2) return;
    if (data[0] == ES_VERSION && data[1] == ES_TYPE_STATUS) {
        audio_post_status(info->src_addr, data, (size_t)len);
        return;
    }
    if (!s_share_cb || len <= 0) return;
    audio_post_share(info->src_addr, data, (size_t)len);
}

/* The player's transport, in NVS beside the Wi-Fi record: one byte, read once at boot and
 * written when the row is touched. */
#define PLAYER_PREFS_NS  "k7.prefs"
#define PLAYER_PREF_KEY  "player"

bool app_board_player_espnow_load(bool *on)
{
    if (!on) return false;
    nvs_handle_t h;
    if (nvs_open(PLAYER_PREFS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    uint8_t v = 1;
    esp_err_t e = nvs_get_u8(h, PLAYER_PREF_KEY, &v);
    nvs_close(h);
    if (e != ESP_OK) return false;
    *on = v != 0;
    return true;
}

esp_err_t app_board_player_espnow_save(bool on)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(PLAYER_PREFS_NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_u8(h, PLAYER_PREF_KEY, on ? 1 : 0);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    flash_settled();
    return e;
}

static bool audio_set_locked(bool on);

bool app_board_audio_set(bool on)
{
    if (!audio_worker_start()) {
        ESP_LOGE(TAG, "no memory for the radio's worker");
        return false;
    }
    if (!s_audio_lock && !(s_audio_lock = xSemaphoreCreateMutex())) return false;
    /* The tone needs a lock of its own so a copy can never race a send. Without it the tone
     * stays on the broadcast: app_board_player_tone_capable() then answers false. */
    if (!s_tone_lock && !(s_tone_lock = xSemaphoreCreateMutex()))
        ESP_LOGW(TAG, "no memory for the tone's lock: the tone stays on the broadcast");
    if (xSemaphoreTake(s_audio_lock, portMAX_DELAY) != pdTRUE) return false;
    bool ok = audio_set_locked(on);
    xSemaphoreGive(s_audio_lock);
    return ok;
}

/* Called with the emitter's lock held: the worker may be running and is the only other user. */
static bool audio_set_locked(bool on)
{
    if (!on) {
        s_audio_on = false;
        s_tone_capable = false;
        s_tone_sent_us = 0;
        if (s_audio_has_peer) {
            esp_now_del_peer(s_audio_peer);
            s_audio_has_peer = false;
            s_audio_peer_ifidx = 0;
        }
        if (s_audio_arena)
            es_emitter_init(&s_audio, s_audio_arena, ES_HISTORY_FRAMES * ES_MAX_FRAME, audio_send,
                            NULL);
        return true;
    }
    if (s_audio_on) return true;
    if (!s_audio_arena) {
        size_t bytes = ES_HISTORY_FRAMES * ES_MAX_FRAME;
        s_audio_arena = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_audio_arena) s_audio_arena = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
        if (!s_audio_arena) {
            ESP_LOGE(TAG, "no memory for the ESP-NOW emitter: %u bytes", (unsigned)bytes);
            return false;
        }
        ESP_LOGI(TAG, "espnow audio: %u slots of %u bytes (%u KB)",
                 (unsigned)ES_HISTORY_FRAMES, (unsigned)ES_MAX_FRAME,
                 (unsigned)(bytes / 1024));
    }
    es_emitter_init(&s_audio, s_audio_arena, ES_HISTORY_FRAMES * ES_MAX_FRAME, audio_send, NULL);
    if (!espnow_up()) return false;
    esp_now_register_recv_cb(espnow_recv_cb);
    s_audio_on = true;
    s_tone_capable = false;
    s_tone_sent_us = 0;
    return true;
}

void app_board_audio_frame(void *ctx, const uint8_t *frame, size_t bytes)
{
    (void)ctx;
    /* The broadcast's task: copy into the queue and go back to encoding. The worker does the
     * rest, because a send can block and the station must never wait on the radio (EN-10). */
    audio_post_frame(frame, bytes);
}

void app_board_audio_tone(const float band_db[3], float gain_db)
{
    if (!band_db || !s_tone_lock) return;
    if (xSemaphoreTake(s_tone_lock, portMAX_DELAY) != pdTRUE) return;
    s_tone_band_db[0] = band_db[0];
    s_tone_band_db[1] = band_db[1];
    s_tone_band_db[2] = band_db[2];
    s_tone_gain_db = gain_db;
    s_tone_gen++;
    s_tone_dirty = true;
    xSemaphoreGive(s_tone_lock);
    /* Wake the worker now; the next status would wake it anyway, but a knob should not wait
     * for it. */
    if (s_audio_signal) xSemaphoreGive(s_audio_signal);
}

bool app_board_player_tone_capable(void)
{
    return s_audio_on && s_tone_capable && s_tone_lock != NULL;
}

bool app_board_share_radio_start(app_board_share_frame_fn on_frame, void *ctx)
{
    if (s_share_on) app_board_share_radio_stop();

    if (!espnow_up()) return false;
    if (!audio_worker_start()) {
        ESP_LOGE(TAG, "no memory for the radio's worker");
        return false;
    }
    esp_now_register_recv_cb(espnow_recv_cb);
    s_share_cb = on_frame;
    s_share_ctx = ctx;
    s_share_ifidx = s_got_ip ? WIFI_IF_STA : WIFI_IF_AP;

    /* ESP-NOW sends to an address in its peer table, so the beacon's all-ones address has
     * to be added. A broadcast is unencrypted by definition - there is no per-peer key for
     * "everyone" - and it carries no secret (H15). */
    esp_now_peer_info_t bcast;
    memset(&bcast, 0, sizeof bcast);
    memset(bcast.peer_addr, 0xFF, ESP_NOW_ETH_ALEN);
    bcast.channel = 0; /* the current channel */
    bcast.ifidx = s_share_ifidx;
    bcast.encrypt = false;
    esp_err_t be = esp_now_add_peer(&bcast);
    if (be != ESP_OK && be != ESP_ERR_ESPNOW_EXIST) {
        s_share_cb = NULL;
        s_share_ctx = NULL;
        if (!s_audio_on) {
            esp_now_unregister_recv_cb();
            esp_now_deinit();
            s_now_up = false;
        }
        return false;
    }

    s_share_on = true;
    return true;
}

void app_board_share_radio_stop(void)
{
    if (!s_share_on) return;
    s_share_on = false;
    s_share_cb = NULL;
    s_share_ctx = NULL;
    /* The stream still owns the radio when the audio is on: the window borrows it. */
    if (s_audio_on) return;
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    s_now_up = false;
}

/* 192.168.0.44 -> 0xC0A8002C, the numeric address, without a byte-order question. */
static uint32_t ipv4_of(const char *s)
{
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (!s || sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return 0;
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;
}

void app_board_share_net(app_board_share_net_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof *out);

    wifi_record_t rec;
    if (app_board_wifi_record_load(&rec)) {
        out->has_home = true;
        snprintf(out->home_ssid, sizeof out->home_ssid, "%s", rec.ssid);
        snprintf(out->home_pass, sizeof out->home_pass, "%s", rec.pass);
    }
    if (!app_board_setup_password_load(out->ap_pass, sizeof out->ap_pass) || !out->ap_pass[0])
        snprintf(out->ap_pass, sizeof out->ap_pass, "%s", WIFI_RECORD_DEFAULT_SETUP_PASSWORD);

    out->ipv4 = s_got_ip ? ipv4_of(s_local_ip) : 0;

    uint8_t primary = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    if (esp_wifi_get_channel(&primary, &second) == ESP_OK) out->channel = primary;
}

bool app_board_share_send(const uint8_t *dst, const uint8_t *data, size_t len)
{
    static const uint8_t bcast[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (!s_share_on || !data || len == 0 || len > ESP_NOW_MAX_DATA_LEN) return false;
    return esp_now_send(dst ? dst : bcast, data, len) == ESP_OK;
}

bool app_board_share_peer(const uint8_t mac[6], const uint8_t lmk[16])
{
    if (!s_share_on || !mac || !lmk) return false;
    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof peer);
    memcpy(peer.peer_addr, mac, ESP_NOW_ETH_ALEN);
    memcpy(peer.lmk, lmk, ESP_NOW_KEY_LEN);
    peer.channel = 0;
    peer.ifidx = s_share_ifidx;
    peer.encrypt = true;

    esp_err_t e = esp_now_add_peer(&peer);
    return e == ESP_OK || e == ESP_ERR_ESPNOW_EXIST;
}

void app_board_share_peer_drop(const uint8_t mac[6])
{
    if (!s_share_on || !mac) return;
    esp_now_del_peer(mac);
}

void app_board_random(uint8_t *out, size_t len)
{
    if (out && len) esp_fill_random(out, len);
}

/* PSA rather than a curve of our own: mbedTLS is already linked, and the portable side
 * takes it from the shared secret on (components/portable/handover). */
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

bool app_board_x25519_public(const uint8_t priv[32], uint8_t pub[32])
{
    mbedtls_svc_key_id_t key;
    if (!priv || !pub || !psa_x25519_key(priv, &key)) return false;
    size_t n = 0;
    psa_status_t st = psa_export_public_key(key, pub, 32, &n);
    psa_destroy_key(key);
    return st == PSA_SUCCESS && n == 32;
}

bool app_board_x25519(const uint8_t priv[32], const uint8_t peer_pub[32], uint8_t shared[32])
{
    mbedtls_svc_key_id_t key;
    if (!priv || !peer_pub || !shared || !psa_x25519_key(priv, &key)) return false;
    size_t n = 0;
    psa_status_t st = psa_raw_key_agreement(PSA_ALG_ECDH, key, peer_pub, 32, shared, 32, &n);
    psa_destroy_key(key);
    if (st != PSA_SUCCESS || n != 32) return false;

    /* A low-order point gives all zeroes, and H28 says that is not a network record. */
    uint8_t any = 0;
    for (int i = 0; i < 32; i++) any |= shared[i];
    return any != 0;
}
