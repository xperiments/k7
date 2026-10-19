/*
 * appliance - the device's entry policy, moved out of main.c.
 *
 * The ordering the appliance depends on is in app_services: the station is resolved
 * before anything allocates, the network comes up before the encoder takes its internal
 * RAM, and the listener starts last. The appliance adds what is above that: NVS, the
 * station format this product ships, the card retry after a missing card, the console
 * line and the BOOT button.
 */
#include "appliance.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "app_board.h"
#include "app_services.h"
#include "mini_files.h"
#include "mini_icecast.h"
#include "screen_display.h"

#define MUSIC_DIR    "/sdcard/music"
#define STATION_NAME "ESP32 Radio"
#define HTTP_PORT    8000

/* A card that will not mount is the most likely fault this hardware has, so a
 * failed mount is not fatal: the device stays on the network and retries. */
#define CARD_RETRY_MS 5000
#define CARD_TASK_STACK 6144 /* FATFS mount and the card-print formatting are not shallow */

static const char *TAG = "app";

/* ------------------------------------------------------------ card retry */

/* Retries are paced, and the log line is one per attempt: a device with no card
 * neither tight-loops nor floods the console. */
static void card_task(void *arg)
{
    (void)arg;
    int attempt = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(CARD_RETRY_MS));
        app_board_card_t card = app_board_card_mount(MUSIC_DIR);
        if (card.music_dir) {
            mini_files_set_card(card.music_dir, NULL);
            ESP_LOGI(TAG, "card mounted after %d retries: %s is now available", attempt + 1, MUSIC_DIR);
            vTaskDelete(NULL);
            return;
        }
        mini_files_set_card(NULL, card.mount_error);
        ESP_LOGW(TAG, "still no card: %s; retrying in %d s",
                 card.mount_error ? card.mount_error : "unknown error", CARD_RETRY_MS / 1000);
        attempt++;
    }
}

/* At most `max` bytes of `s`, ending on a UTF-8 character boundary. Titles come off
 * the card with multi-byte characters in them, and a byte cut through one leaves the
 * line invalid UTF-8; a line like that repeated once a second makes idf_monitor give
 * up decoding and blame the baud rate. */
static void utf8_prefix(char *out, size_t out_size, const char *s, size_t max)
{
    size_t len = strlen(s);
    if (len > max) {
        len = max;
        while (len > 0 && ((unsigned char)s[len] & 0xc0) == 0x80) len--;
    }
    if (len >= out_size) len = out_size - 1;
    memcpy(out, s, len);
    out[len] = '\0';
}

/* --------------------------------------------------------------- the appliance */

void appliance_run(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    app_board_log_hardware();

    /* The station format is chosen from measurement, not preference, and the
     * measurement is surprising: the encoder's cost per frame is flat against
     * the bit rate. Measured on this board (ESP32-S3, 240 MHz, encoder state
     * in internal RAM, vendored encoder at -O3 in IRAM), per frame:
     *
     *     44100 Hz /  96 kbps   13.8 ms      48000 Hz / 192 kbps   14.0 ms
     *     44100 Hz / 128 kbps   14.0 ms      32000 Hz /  96 kbps   13.9 ms
     *     44100 Hz / 160 kbps   14.1 ms      22050 Hz /  64 kbps    6.3 ms
     *
     * So the sample rate is what costs CPU (frames per second), and the bit
     * rate is very nearly free: 64 to 320 kbps costs 10 % more. Quality at a
     * fixed sample rate improves with bit rate, and measurably so - the table
     * below is SNR of the encoded stream against its source, on pink noise at
     * -10 dBFS, which is about the hardest material a codec can be given:
     *
     *      96 kbps   2.4 dB      192 kbps   7.1 dB      320 kbps  15.5 dB
     *     128 kbps   3.3 dB      256 kbps  11.9 dB
     *
     * This station therefore runs 44100 Hz at 256 kbps: the specification's
     * recommended sample rate, with three and a half times the bits of the
     * fallback it started from, for 3 % more CPU than 128 kbps and 32 KB/s per
     * client. 320 kbps is the ceiling and is one line away. Above 192 kbps the
     * perceptual gain is likely smaller than the SNR suggests, because shine's
     * limit is its psychoacoustic model rather than its bit budget.
     *
     * What it costs is headroom, not correctness: 52.9 % of one core on that
     * synthetic material, about 40 % on the tonal fixture corpus. The
     * specification's 35 % threshold is not reachable at 44.1 kHz, and -
     * measured - not at 32 kHz either (37.7 % on synthetic material); only
     * 22.05 kHz meets it, at 23.6 %. The threshold was proposed rather than
     * measured (the specification says so itself), the functional requirement
     * is that the broadcast never starves, and that is what the soak checks.
     * 32000 Hz / 96 kbps remains the documented fallback if headroom is ever
     * needed for something else. */
    const app_services_cfg_t svc = {
        .music_dir = MUSIC_DIR,
        .station_name = STATION_NAME,
        .port = HTTP_PORT,
        .out_hz = 44100,
        .bitrate = 256,
        /* How many listeners the broadcast will carry at once. This is separate
         * from the HTTP connection pool (max_conns): a listener stops occupying a
         * connection worker as soon as it is registered. */
        .max_clients = 4,
        .max_conns = 4,
        /* The tape effect on the broadcast ("FX"), or the decoder's output as it
         * always was ("normal").
         *
         * Measured on this board, 44.1 kHz / 256 kbps, same material, two runs
         * each: encoder alone 60.6 % of one core; with the effect, encoder 59.8 %
         * plus the effect's 11.0 % = 70.8 %, with zero starvation rebases. The
         * encoder's own cost does not move, so the effect is simply additive, and
         * it is per stream rather than per listener.
         *
         * Set to 0 and the stream is the decoder's output sample for sample, at no
         * extra cost. It is not switchable at run time because two encoders would
         * not fit on the core the broadcast owns.
         *
         * The reasoning, the measurements and the two operating points are in
         * docs/research/tape-fx-output-stream.md (§6.5-§6.7). */
        .fx = 1,
    };

    /* The services resolve the station first (arithmetic only), then bring up the
     * network before the broadcast takes its internal RAM, then the file surface,
     * the broadcast and the listener. The order and its reasons are in
     * components/portable/app_services. */
    app_services_report_t report;
    err = app_services_start(&svc, &report);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "not serving: %s", report.failure[0] ? report.failure : esp_err_to_name(err));
        if (!report.station_ok) {
            /* F1: refuse, name the value, and do not serve. Halting rather than
             * aborting keeps the diagnosis in the log once - an abort here would
             * reboot the device and repeat the message forever, hiding everything
             * else. Reflashing with a valid out_hz is the fix. */
            ESP_LOGE(TAG, "fix out_hz in components/app/appliance/appliance.c and reflash; "
                          "the device is idle until then");
        }
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    /* The screen comes last, once there is something to show and a status to read. */
    esp_err_t screen_err = screen_display_start();
    if (screen_err != ESP_OK) {
        ESP_LOGE(TAG, "the screen did not start: %s; the device still serves",
                 esp_err_to_name(screen_err));
    }

    /* A card that failed to mount at boot is retried; the index reports the state
     * until it succeeds (R8). */
    if (!report.card_present)
        xTaskCreate(card_task, "card_retry", CARD_TASK_STACK, NULL, 4, NULL);

    app_board_wait_online();

    /* BOOT button (GPIO0) = next track. */
    gpio_config_t io = {.pin_bit_mask = 1ULL << GPIO_NUM_0, .mode = GPIO_MODE_INPUT, .pull_up_en = 1};
    gpio_config(&io);

    for (;;) {
        char shown[41];
        mini_icecast_status_t st;
        mini_icecast_status(&st);
        utf8_prefix(shown, sizeof shown, st.title, 40);
        ESP_LOGI(TAG, "%-40s  L %3d%%  R %3d%%", shown,
                 (int)(st.level_l * 100), (int)(st.level_r * 100));
        for (int i = 0; i < 10; i++) {
            if (!gpio_get_level(GPIO_NUM_0)) {
                mini_icecast_skip();
                vTaskDelay(pdMS_TO_TICKS(500)); /* debounce */
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}
