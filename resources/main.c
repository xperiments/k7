/*
 * Radio example: SD card (any mix of .mp3 / .flac / .wav, any sample rate)
 *   -> mini_icecast (port 8000) -> any player / <audio> tag.
 * The component decodes + resamples everything to one PCM format; the very same PCM
 * that goes on the wire reaches pcm_cb(), which is where your visualizer plugs in.
 *
 * Edit: WIFI_SSID / WIFI_PASS and the SD pins for your board.
 */
#include <math.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdmmc_cmd.h"

#include "mini_icecast.h"

#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"

/* SDMMC, 1-bit mode (GPIO matrix on the S3). Example pins: change them. */
#define SD_CLK 39
#define SD_CMD 38
#define SD_D0  40

static const char *TAG = "radio";

/* ------------------------------------------------------------------ Wi-Fi */

static EventGroupHandle_t wifi_ev;

static void wifi_evt(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && (id == WIFI_EVENT_STA_START || id == WIFI_EVENT_STA_DISCONNECTED)) {
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "stream: http://" IPSTR ":8000/stream", IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(wifi_ev, 1);
    }
}

static void wifi_start(void)
{
    wifi_ev = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_evt, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_evt, NULL));

    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid, WIFI_SSID, sizeof wc.sta.ssid);
    strlcpy((char *)wc.sta.password, WIFI_PASS, sizeof wc.sta.password);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE); /* modem sleep adds latency spikes: bad for streaming */
    xEventGroupWaitBits(wifi_ev, 1, pdFALSE, pdTRUE, portMAX_DELAY);
}

/* ---------------------------------------------------------------- SD card */

static esp_err_t sd_mount(void)
{
    esp_vfs_fat_sdmmc_mount_config_t mc = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = SD_CLK;
    slot.cmd = SD_CMD;
    slot.d0 = SD_D0;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP; /* add real 10k pull-ups on CMD/D0 if it is flaky */

    sdmmc_card_t *card;
    esp_err_t err = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mc, &card);
    if (err == ESP_OK) sdmmc_card_print_info(stdout, card);
    return err;
}

/* ------------------------------------------------- pcm_cb -> visualizer */

static volatile float level_l, level_r; /* 0..1 RMS, read these from your UI task */

/* Runs in the radio task for every 1024-frame chunk (23 ms @ 44.1 kHz), stereo int16.
 * Keep it light: for an FFT, copy the chunk into a ring buffer and process it elsewhere. */
static void on_pcm(const int16_t *pcm, size_t frames, void *ctx)
{
    int64_t sl = 0, sr = 0;
    for (size_t i = 0; i < frames; i++) {
        int l = pcm[2 * i], r = pcm[2 * i + 1];
        sl += l * l;
        sr += r * r;
    }
    level_l = sqrtf((float)sl / frames) / 32768.0f;
    level_r = sqrtf((float)sr / frames) / 32768.0f;
}

/* ------------------------------------------------------------------- main */

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(sd_mount());
    wifi_start();

    mini_icecast_cfg_t cfg = {
        .music_dir = "/sdcard/music",
        .station_name = "ESP32 Radio",
        .pcm_cb = on_pcm,
        /* .out_hz = 48000,   station rate (default 44100) */
    };
    ESP_ERROR_CHECK(mini_icecast_start(&cfg));

    /* BOOT button (GPIO0) = next track. */
    gpio_config_t io = {.pin_bit_mask = 1ULL << GPIO_NUM_0, .mode = GPIO_MODE_INPUT, .pull_up_en = 1};
    gpio_config(&io);

    for (;;) {
        char title[121];
        mini_icecast_now_playing(title, sizeof title);
        ESP_LOGI(TAG, "%-40.40s  L %3d%%  R %3d%%", title, (int)(level_l * 100), (int)(level_r * 100));
        for (int i = 0; i < 10; i++) {
            if (!gpio_get_level(GPIO_NUM_0)) {
                mini_icecast_skip();
                vTaskDelay(pdMS_TO_TICKS(500)); /* debounce */
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}
