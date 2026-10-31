/*
 * The ESP32-S3 board adapter: Wi-Fi, the SPI microSD card, and the hardware census.
 *
 * Everything here is board-specific and moves as one: the pin map, the Wi-Fi
 * credentials, the retry-safe SPI bus setup, and the mount diagnostics. The
 * services composition (components/portable/app_services) calls the four functions in
 * app_board.h and knows none of it. The host build has the other adapter.
 */
#include <stdio.h>
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
#include "esp_partition.h"
#include "esp_psram.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "snapshot.h"

#include "app_board.h"
#include "board_shell.h"

/* The credentials are compiled in, as the specification requires, but they live in
 * a gitignored header so a real network password does not end up in version
 * control. Copy components/app/board_esp32/wifi_credentials.h.example to
 * wifi_credentials.h and fill it in; the placeholders below keep a clean checkout
 * building. */
#if defined(__has_include)
#if __has_include("wifi_credentials.h")
#include "wifi_credentials.h"
#endif
#endif
#ifndef WIFI_SSID
#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"
#warning "components/app/board_esp32/wifi_credentials.h is missing: the device will not join a network (see README)"
#endif

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
static uint16_t s_http_port = 8000;
static char s_local_ip[16];

static void wifi_evt(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        /* Retrying in silence makes a mistyped SSID look like a dead device, and
         * this device is meant to be diagnosable. First failure, then every tenth,
         * so the log stays bounded. */
        wifi_event_sta_disconnected_t *e = data;
        if (++wifi_attempts == 1 || wifi_attempts % 10 == 0)
            ESP_LOGW(TAG, "Wi-Fi: cannot join \"%s\" (reason %d), attempt %d; "
                          "check components/app/board_esp32/wifi_credentials.h",
                     WIFI_SSID, e->reason, wifi_attempts);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        snprintf(s_local_ip, sizeof s_local_ip, IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "stream: http://" IPSTR "/stream  app: https://" IPSTR "/",
                 IP2STR(&e->ip_info.ip), IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(wifi_ev, 1);
    }
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

    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid, WIFI_SSID, sizeof wc.sta.ssid);
    strlcpy((char *)wc.sta.password, WIFI_PASS, sizeof wc.sta.password);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
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
    if (wifi_ev) xEventGroupWaitBits(wifi_ev, 1, pdFALSE, pdTRUE, portMAX_DELAY);
}

bool app_board_local_ip(char *out, size_t out_size)
{
    if (!out || !out_size || !s_local_ip[0]) return false;
    snprintf(out, out_size, "%s", s_local_ip);
    return true;
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

#define SNAPSHOT_UPDATE_FILE MOUNT_POINT "/xtape.db"
#define SNAPSHOT_UPDATE_PART 0x40

bool app_board_card_present(void)
{
    struct stat st;
    return s_card != NULL && stat(MOUNT_POINT "/music", &st) == 0;
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
    return stat(SNAPSHOT_UPDATE_FILE, &st) == 0 && S_ISREG(st.st_mode);
}

/* ------------------------------------------------------------- snapshot */

/* --- the image, mapped ------------------------------------------------- */

const uint8_t *app_board_snapshot_image(size_t *size, const char **why, int *state)
{
    static const uint8_t *mapped;
    static size_t         mapped_size;
    static char           err[96];

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
    if (memcmp(hdr, SNAPSHOT_MAGIC, 8) != 0) {
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
    if (state) *state = SNAPSHOT_OK;
    if (size) *size = mapped_size;
    return mapped;
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
    if (stat(SNAPSHOT_UPDATE_FILE, &st) != 0) {
        out = APP_BOARD_UPDATE_NONE;
        goto done;
    }
    if (!S_ISREG(st.st_mode)) {
        ESP_LOGE(TAG, "snapshot: %s is not a file; left alone", SNAPSHOT_UPDATE_FILE);
        goto done;
    }

    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, SNAPSHOT_UPDATE_PART, NULL);
    if (!part) {
        ESP_LOGE(TAG, "snapshot: no snapshot partition in this build");
        goto done;
    }

    f = fopen(SNAPSHOT_UPDATE_FILE, "rb");
    if (!f) {
        ESP_LOGE(TAG, "snapshot: cannot open %s", SNAPSHOT_UPDATE_FILE);
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
        ESP_LOGE(TAG, "snapshot: not applied (%s); %s is left in place", why,
                 SNAPSHOT_UPDATE_FILE);
        goto done;
    }
    /* Only now: the card holds the master copy until the flash has agreed. */
    if (remove(SNAPSHOT_UPDATE_FILE) == 0)
        ESP_LOGI(TAG, "snapshot: flashed and verified; the update file is deleted");
    else
        ESP_LOGW(TAG, "snapshot: flashed and verified, but %s could not be deleted",
                 SNAPSHOT_UPDATE_FILE);
    out = APP_BOARD_UPDATE_APPLIED;

done:
    if (f) fclose(f);
    esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
    s_card = NULL;
    return out;
}

/* --------------------------------------------------------------- the Shell */

/* The page at `/`. The build packs the Shell into this partition with
 * spiffs_create_partition_image() (root CMakeLists.txt) and `idf.py flash` writes it
 * beside the app. The firmware only ever reads it, and the mount never formats: a
 * partition that will not mount is a blank one, not a reason to erase the page.
 *
 * Two devices reach here without a page, and both are states rather than faults: one
 * still carrying the old partition table has no `frontdoor` partition at all, and one
 * flashed without the image has it blank. The front door is handed NULL in both cases,
 * so `/` answers 503 and everything else serves. */
#define SHELL_PARTITION   "frontdoor"
#define SHELL_MOUNT_POINT "/frontdoor"

static const char *s_shell_root;
static bool s_shell_mount_tried;

const char *app_board_shell_mount(void)
{
    if (s_shell_mount_tried) return s_shell_root;
    s_shell_mount_tried = true;

    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, SHELL_PARTITION);
    if (!part) {
        ESP_LOGW(TAG, "shell: no \"%s\" partition in this build; / answers 503",
                 SHELL_PARTITION);
        return NULL;
    }

    esp_vfs_spiffs_conf_t conf = {
        .base_path = SHELL_MOUNT_POINT,
        .partition_label = SHELL_PARTITION,
        /* One streamed file per connection, and the listener carries four: eight is the
         * headroom the card's mount takes, for the same reason. */
        .max_files = 8,
        /* Never format: the image is written by the build and flashed with the app, so
         * a partition that will not mount is one that has not been flashed yet. */
        .format_if_mount_failed = false,
    };
    esp_err_t e = esp_vfs_spiffs_register(&conf);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "shell: %s did not mount (%s); the page is absent and / answers 503",
                 SHELL_MOUNT_POINT, esp_err_to_name(e));
        return NULL;
    }

    s_shell_root = SHELL_MOUNT_POINT;
    size_t total = 0, used = 0;
    if (esp_spiffs_info(SHELL_PARTITION, &total, &used) == ESP_OK)
        ESP_LOGI(TAG, "shell: %s mounted, %u of %u KB used (never written by the firmware)",
                 SHELL_MOUNT_POINT, (unsigned)(used >> 10), (unsigned)(total >> 10));
    else
        ESP_LOGI(TAG, "shell: %s mounted (never written by the firmware)", SHELL_MOUNT_POINT);
    return s_shell_root;
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
