/*
 * appliance - the device's entry policy, moved out of main.c.
 *
 * The ordering the appliance depends on is in app_services: the station is resolved
 * before anything allocates, the network comes up before the encoder takes its internal
 * RAM, and the listener starts last. The appliance adds what is above that: NVS, the boot
 * screen, the station format this product ships, the card retry after a missing card, and
 * the console line. The transport is the screen's, not a physical button's.
 *
 * The screen is the one thing that comes first rather than last, and it comes in two stages.
 * The panel's hardware and the LVGL port are taken before the services (its 16 KB internal
 * stack has to win the RAM the Wi-Fi DMA buffers want), but its first clock is not started
 * until app_services_start() has returned: the boot's flash work - the Snapshot and Wi-Fi's
 * NVS - then happens on a dark panel, because a flash access during a scan displaces the
 * picture permanently (ADR 0055). The mark is painted after that work, lit, and
 * held for its dwell; then the cassette replaces it.
 */
#include "appliance.h"

#include <string.h>
#include <sys/stat.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "app_board.h"
#include "app_services.h"
#include "card_select.h"
#include "mini_files.h"
#include "mini_icecast.h"
#include "screen_display.h"

#define MUSIC_DIR    "/sdcard/music"
#define STATION_NAME "ESP32 Radio"
#define HTTP_PORT    80

/* A card that will not mount is the most likely fault this hardware has, so a
 * failed mount is not fatal: the device stays on the network and retries. */

static const char *TAG = "app";

/* The selected Playlist lives on the card, beside the Snapshot: one 32-byte record at
 * the card root whose id 0 means "no selection". The card is the only store - there is
 * no NVS fallback - so a card that is out or read-only leaves the RAM choice standing
 * for the session, and the writer logs that once rather than on every attempt. */
#define SELECTION_PATH       "/sdcard/k7.select"
#define SELECTION_TASK_STACK 4096
#define SELECTION_TASK_PRIO  2 /* below the decoder (3) and the broadcast (6) */

/* The latest id waiting to be written, length one: a newer choice replaces one that has
 * not reached the card yet, which is the only behaviour that matters when two arrive
 * close together. The writer is the only task that touches the file. */
static QueueHandle_t s_selection_queue;

/* Boot reads the remembered choice after the card mount: make sure the record exists,
 * then read it. Any failure is "no choice", not a failed boot. */
static bool selection_load_path(const char *path, uint64_t *id)
{
    if (!card_select_ensure(path)) {
        ESP_LOGW(TAG, "cannot create %s (card absent or read-only); starting with no Playlist",
                 path);
        return false;
    }
    if (!card_select_load(path, id)) {
        ESP_LOGW(TAG, "%s is not a valid selection record; starting with no Playlist", path);
        return false;
    }
    return true;
}

/* The store's load callback, with the card root's path bound here rather than passed
 * through app_services: the store seam is portable and knows no filesystem. */
static bool selection_load(uint64_t *id)
{
    return selection_load_path(SELECTION_PATH, id);
}

/* Called from the broadcast task (a dropped order) and from the HTTP select route.
 * Both callers may have their stack in PSRAM, so this only posts to the writer; it
 * never opens, reads or writes the file itself. */
static bool selection_save(uint64_t id)
{
    if (!s_selection_queue) return false;
    return xQueueOverwrite(s_selection_queue, &id) == pdTRUE;
}

static void selection_writer(void *arg)
{
    (void)arg;
    uint64_t id;
    bool failing = false;
    for (;;) {
        if (xQueueReceive(s_selection_queue, &id, portMAX_DELAY) != pdTRUE) continue;
        if (card_select_save(SELECTION_PATH, id)) {
            if (failing)
                ESP_LOGI(TAG, "the card takes the Playlist selection again");
            failing = false;
        } else if (!failing) {
            /* Once per transition: a read-only or missing card would otherwise log on
             * every order-dropped and every select for the life of the session. */
            ESP_LOGW(TAG, "cannot write %s; the Playlist choice lasts this session only",
                     SELECTION_PATH);
            failing = true;
        }
    }
}

static bool selection_writer_start(void)
{
    if (s_selection_queue) return true;
    s_selection_queue = xQueueCreate(1, sizeof(uint64_t));
    if (!s_selection_queue) return false;
    /* No capability caps: the default stack is internal DRAM, which is the point. */
    if (xTaskCreate(selection_writer, "sel_write", SELECTION_TASK_STACK, NULL,
                    SELECTION_TASK_PRIO, NULL) != pdPASS) {
        vQueueDelete(s_selection_queue);
        s_selection_queue = NULL;
        return false;
    }
    return true;
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

    /* The Playlist selection's writer exists before a route or the broadcast can ask
     * for a write. A failure costs persistence, not the appliance: the RAM choice stands
     * for this session and the reason is in the log. */
    if (!selection_writer_start())
        ESP_LOGE(TAG, "no Playlist selection writer; a choice lasts this session only");

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
    /* The chosen Playlist survives a power cut in the card's k7.select. The composition
     * takes it as two callbacks so playlist_select stays portable (ADR 0051); a card
     * that has never carried a record is "no choice", not an error. */
    app_services_cfg_t svc = {
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
        .selection_load = selection_load,
        .selection_save = selection_save,
    };

#if CONFIG_APP_UI_SCREEN_HTTP
    /* The screen's diagnostic routes are the front door's to serve, and the listener starts
     * inside app_services_start(), so they are registered before it. The screen itself comes
     * up later; its handlers answer 503 until it does. A failure here costs the picture, not
     * the appliance. */
    esp_err_t http_err = screen_display_http_start();
    if (http_err != ESP_OK)
        ESP_LOGW(TAG, "the screen's HTTP diagnostics did not register: %s; the screen still runs",
                 esp_err_to_name(http_err));
#endif

    /* The Snapshot update runs first, before the screen and before the services. A
     * flash write disables the cache and this build keeps its code and rodata in
     * PSRAM, so there is no later moment where the write is invisible: the panel's
     * DMA simply cannot fetch while it happens (ADR 0047). A failure here costs the
     * measurements, never the appliance. */
    app_board_update_t update = app_board_snapshot_update();
    if (update == APP_BOARD_UPDATE_FAILED)
        ESP_LOGE(TAG, "the snapshot update was not applied; the device serves without it");

    /* The screen's hardware comes up before the services; its first clock does not. Two
     * reasons, in this order:
     *
     *  - internal RAM: the panel's LVGL port task takes a 16 KB internal stack, and that has
     *    to be taken before esp_wifi_init() asks for its contiguous DMA buffers, where the
     *    rule used to be the other way round (ADR 0004). LVGL's own allocations are PSRAM by
     *    policy, so that one block is what moves; the line screen_display_boot() logs is the
     *    margin left for Wi-Fi - check it there rather than guess.
     *  - flash: the boot's flash work - the Snapshot and Wi-Fi's NVS - is
     *    inside app_services_start(), and a flash access takes the PSRAM window from the
     *    panel's scan-out for a few microseconds, which displaces the picture permanently
     *    (ADR 0055). So screen_display_boot() stops short of esp_lcd_panel_init(): the clock
     *    and the scan start only after that call has returned, and the mark goes up then. */
    esp_err_t boot_err = screen_display_boot();
    if (boot_err != ESP_OK)
        ESP_LOGE(TAG, "the screen did not come up: %s; the device still serves",
                 esp_err_to_name(boot_err));

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

    /* The shuffle mode's order draws from the hardware RNG, and the seed is the app's
     * to give: mini_icecast never calls esp_random() itself, so the host shim needs no
     * such symbol (ADR 0023). The mode itself starts Off; this only arms the order the
     * Menu's row toggles. */
    mini_icecast_shuffle_seed((uint32_t)esp_random());

    /* The boot's flash work is behind us, so the panel's first clock can start now. The mark
     * is painted on a quiet bus, the backlight is lit over it, and it is held for its dwell
     * before the cassette replaces it. app_services_start() has mounted and scanned the card,
     * or refused it and left the retry task to own it, so the cassette has something to show.
     * MUSIC_DIR is also the root the Artwork level reads a Track's own picture from. */
    if (boot_err == ESP_OK) {
        esp_err_t mark_err = screen_display_mark();
        if (mark_err != ESP_OK)
            ESP_LOGE(TAG, "the boot mark did not come up: %s; the device still serves",
                     esp_err_to_name(mark_err));

        esp_err_t screen_err = screen_display_start(MUSIC_DIR);
        if (screen_err != ESP_OK) {
            ESP_LOGE(TAG, "the screen did not start: %s; the device still serves",
                     esp_err_to_name(screen_err));
        }
    }

    /* A card that failed to mount at boot, a card pulled while running and a card put
     * back are all one story now: app_services watches the mount point and reports each
     * transition, so the appliance keeps no retry of its own (R8, snapshot.md U5). */

    app_board_wait_online();

    /* Discovery is advertised only after the listener is up and Wi-Fi has an
     * address: a record for a dead endpoint is worse than no record. */
    esp_err_t mdns_err = app_board_mdns_start("k7", HTTP_PORT, "/mcp");
    if (mdns_err != ESP_OK)
        ESP_LOGE(TAG, "mDNS did not start: %s; the device still serves", esp_err_to_name(mdns_err));

    /* No BOOT-button transport: next and previous belong to the screen's DJ block, and
     * the BOOT pin is left as the strapping input it is at reset. The status line below
     * is the only thing this loop still does. */
    for (;;) {
        /* A card that arrives carrying an Update file is not applied here: the write needs
         * the boot window, before the panel and the broadcast, so the device reboots once
         * and does it there. Nothing else in the loop can start that reboot. */
        if (app_board_snapshot_pending()) {
            ESP_LOGI(TAG, "an update file arrived with the card; rebooting to apply it");
            vTaskDelay(pdMS_TO_TICKS(100)); /* let the log line reach the wire */
            esp_restart();
        }
        char shown[41];
        mini_icecast_status_t st;
        mini_icecast_status(&st);
        utf8_prefix(shown, sizeof shown, st.title, 40);
        ESP_LOGI(TAG, "%-40s  L %3d%%  R %3d%%", shown,
                 (int)(st.level_l * 100), (int)(st.level_r * 100));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
