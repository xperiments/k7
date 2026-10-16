/*
 * SD stream server: an ESP32-S3 that joins Wi-Fi, mounts a microSD card and
 * serves one continuous MP3 stream plus a browsable, downloadable library.
 *
 * Edit WIFI_SSID / WIFI_PASS and the SD pins for your board. Everything else has
 * defaults that work.
 *
 * Two things this firmware does that a deployer must know (README, deployment):
 *   - the Wi-Fi credentials are compiled into the binary;
 *   - the root address and /music/<name> are open to anyone who can reach the
 *     port, with no authentication.
 * Do not put it on an untrusted network or forward its port.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_chip_info.h"
#include "esp_event.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_psram.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "http_server.h"
#include "mini_files.h"
#include "mini_icecast.h"
#include "nvs_flash.h"
#include "sdmmc_cmd.h"

#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"

/* SDMMC, 1-bit mode (GPIO matrix on the S3). Change them for your board.
 * GPIO39/40 are the USB-JTAG pins: using them costs on-chip JTAG, and the board's
 * separate USB-UART bridge is then the way to see this log. GPIO0 is the BOOT
 * strapping pin and is the skip button; holding it during reset enters download
 * mode, which is how you flash. */
#define SD_CLK 39
#define SD_CMD 38
#define SD_D0  40

#define MUSIC_DIR    "/sdcard/music"
#define STATION_NAME "ESP32 Radio"
#define HTTP_PORT    8000

/* A card that will not mount is the most likely fault this hardware has, so a
 * failed mount is not fatal: the device stays on the network and retries. */
#define CARD_RETRY_MS 5000
#define CARD_TASK_STACK 4096

static const char *TAG = "app";

/* ------------------------------------------------------------------ Wi-Fi */

static EventGroupHandle_t wifi_ev;

static void wifi_evt(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && (id == WIFI_EVENT_STA_START || id == WIFI_EVENT_STA_DISCONNECTED)) {
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "stream: http://" IPSTR ":%d/stream  library: http://" IPSTR ":%d/",
                 IP2STR(&e->ip_info.ip), HTTP_PORT, IP2STR(&e->ip_info.ip), HTTP_PORT);
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
        /* Two directory handles (the broadcast's library scan and the index) plus
         * the decoder's file and one download is four; six leaves headroom rather
         * than sitting exactly on the limit. */
        .max_files = 6,
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

/* P2/P3: what the memory available actually is, before anything is allocated, so
 * a PSRAM-less or wrongly-configured board is diagnosable rather than mysterious. */
static void log_hardware(void)
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

/* ------------------------------------------------- pcm_cb -> visualizer */

static volatile float level_l, level_r; /* 0..1 RMS, read these from your UI task */

/* Runs in the radio task for every 1024-frame chunk, stereo int16, at the station
 * rate. It sees the same PCM content that goes on the wire, but as unencoded
 * chunks, up to one encoder frame ahead of it (26 ms at 44.1 kHz). */
static void on_pcm(const int16_t *pcm, size_t frames, void *ctx)
{
    (void)ctx;
    int64_t sl = 0, sr = 0;
    for (size_t i = 0; i < frames; i++) {
        int l = pcm[2 * i], r = pcm[2 * i + 1];
        sl += (int64_t)l * l;
        sr += (int64_t)r * r;
    }
    level_l = sqrtf((float)sl / (float)frames) / 32768.0f;
    level_r = sqrtf((float)sr / (float)frames) / 32768.0f;
}

/* ------------------------------------------------------------ card retry */

/* Retries are paced, and the log line is one per attempt: a device with no card
 * neither tight-loops nor floods the console. */
static void card_task(void *arg)
{
    (void)arg;
    int attempt = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(CARD_RETRY_MS));
        esp_err_t err = sd_mount();
        if (err == ESP_OK) {
            mini_files_set_card(MUSIC_DIR, NULL);
            ESP_LOGI(TAG, "card mounted after %d retries: %s is now available", attempt + 1, MUSIC_DIR);
            vTaskDelete(NULL);
            return;
        }
        char why[160];
        snprintf(why, sizeof why, "%s (0x%x)", esp_err_to_name(err), (unsigned)err);
        mini_files_set_card(NULL, why);
        ESP_LOGW(TAG, "still no card: %s; retrying in %d s", why, CARD_RETRY_MS / 1000);
        attempt++;
    }
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

    log_hardware();

    /* A card that fails to mount must not take the network down with it: the
     * index reports it and the retry task keeps trying (R8). */
    char card_err[160] = {0};
    esp_err_t card = sd_mount();
    if (card != ESP_OK) {
        snprintf(card_err, sizeof card_err, "%s (0x%x)", esp_err_to_name(card), (unsigned)card);
        ESP_LOGE(TAG, "no card mounted: %s", card_err);
        ESP_LOGE(TAG, "the device will still serve on the network; the index will say so");
    }

    wifi_start();

    mini_icecast_cfg_t cfg = {
        .music_dir = MUSIC_DIR,
        .station_name = STATION_NAME,
        .port = HTTP_PORT,
        .pcm_cb = on_pcm,
        /* Leave the station format at its default: 44100 Hz / 128 kbps CBR, joint
         * stereo. 48000/192 is the fidelity alternative. If the device's own
         * telemetry shows the encoder over 35 % of one core, or starvation rebases
         * during a soak, the fallback is a LOWER format - 32000 Hz / 96 kbps -
         * never a higher bit rate: more bits per second costs more, not less. */
    };
    err = mini_icecast_start(&cfg);
    if (err != ESP_OK) {
        /* A refused station format is a configuration error: say so and stop,
         * rather than resample audio into an encoder that cannot represent it. */
        ESP_LOGE(TAG, "station refused: %s", esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    }

    mini_files_cfg_t files = {
        .station_name = STATION_NAME,
        .music_dir = (card == ESP_OK) ? MUSIC_DIR : NULL,
        .mount_error = (card == ESP_OK) ? NULL : card_err,
    };
    ESP_ERROR_CHECK(mini_files_start(&files));

    http_server_cfg_t http = {.port = HTTP_PORT, .server_name = "sd-stream/1.0", .max_conns = 4};
    ESP_ERROR_CHECK(http_server_start(&http));

    if (card != ESP_OK) xTaskCreate(card_task, "card_retry", CARD_TASK_STACK, NULL, 4, NULL);

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
