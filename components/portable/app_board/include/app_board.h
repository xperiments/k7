#pragma once
/*
 * app_board - what the services need from the board, and nothing else.
 *
 * The seam exists because it already has two real adapters: the ESP32-S3 board
 * (components/app/board_esp32/board_esp32.c: the Wi-Fi record and the setup network,
 * the SPI card mount, the hardware census) and the host build (host/board_host.c: no
 * radio, a record kept in memory or a file, the fixture directory always mounted). The
 * composition and the appliance call this and know nothing about GPIOs, esp_wifi or NVS.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "wifi_record.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *music_dir;   /* the expected directory when the card is mounted, else NULL */
    const char *mount_error; /* why, when music_dir is NULL */
} app_board_card_t;

/* The board's short name, for the Menu's About list. A value, not an adverb: the
 * adapters return "8048S050C" and "host" and never touch hardware here. */
const char *app_board_name(void);

/* Bring the network stack up and start associating, without waiting. This must
 * happen before the services create sockets, and before the encoder takes its
 * internal RAM: esp_wifi_init() wants contiguous DMA-capable buffers. The port is
 * used only for the "where to listen" line the board logs when an address
 * arrives, so a deployer does not have to guess the URL. */
esp_err_t app_board_net_start(uint16_t http_port);

/* Mount the card if this board has one. `expected_music_dir` is the path the
 * services will use; a failure is reported in the struct, never fatal. */
app_board_card_t app_board_card_mount(const char *expected_music_dir);

/* Block until the device has an address. No-op where there is no network. */
void app_board_wait_online(void);

/* Log the board's chip, flash, PSRAM and startup memory census, once. */
void app_board_log_hardware(void);

/* The current station IPv4 address, without the port. False until Wi-Fi has an
 * address (the host adapter always has 127.0.0.1). */
bool app_board_local_ip(char *out, size_t out_size);

/* The network this device joins, as stored. False means **unprovisioned**: no record,
 * or one that no longer decodes, and `out` is cleared so the caller never sees a
 * half-record. The device stores this itself (NVS on the board, memory or a file on the
 * host); nothing compiles a network in. ADR 0076, wifi-provisioning.md §5. */
bool app_board_wifi_record_load(wifi_record_t *out);

/* Store the network. The record is validated by its own codec before anything is
 * written, so an invalid one is ESP_ERR_INVALID_ARG and the stored record is untouched.
 * A *valid* record with an empty SSID erases the stored one - "forget this network" -
 * rather than storing a record that means unprovisioned, because there is only one way
 * for the device to be unprovisioned. */
esp_err_t app_board_wifi_record_save(const wifi_record_t *rec);

/* The setup network's password, in its own key rather than in the network's record: a
 * device can carry one before it has a network at all, and "unprovisioned" has to keep
 * meaning exactly one thing (wifi-provisioning.md P25). Absent or empty means the default,
 * WIFI_RECORD_DEFAULT_SETUP_PASSWORD; `_save("")` restores it. */
bool app_board_setup_password_load(char *out, size_t cap);
esp_err_t app_board_setup_password_save(const char *password);

/* --------------------------------------------------------------- the radio
 *
 * What the setup machine needs and nothing more: whether the station is associated,
 * what the radio has done since the last join, how to ask for the setup network, how to
 * try a network, and what the radio can see. The policy - when the setup network is up,
 * how many attempts, the grace window - is portable (components/portable/setup) and
 * host-tested; this is only the machinery it drives. */
/* The setup network's identity, named once: the adapter starts an access point with
 * these, and the page prints them. (The address is the pass point's default lease; it
 * is written down because a person types it.) */
#define SETUP_NETWORK_SSID "K7.local"
#define SETUP_NETWORK_IP   "192.168.4.1"

/* How long the device tries its stored network at boot before the setup network appears
 * (wifi-provisioning.md P9). Not a tuning knob: it is the point where "the router is slow"
 * stops being the explanation. */
#define SETUP_BOOT_WINDOW_MS 20000

typedef struct {
    bool associated;   /* the station has an address */
    int  disconnects;  /* disconnect events since app_board_wifi_join(), 0 before one */
    int  last_reason;  /* the most recent one's reason code, 0 when there has been none */
    char ip[16];       /* the station's address, "" when it has none */
} app_board_wifi_t;

/* A reading of the radio, never a wait. */
void app_board_wifi_state(app_board_wifi_t *out);

/* Try `ssid` with `pass` (empty for an open network) and return once the attempt is
 * under way: association arrives through app_board_wifi_state(). The disconnect counter
 * and the last reason start again from zero, so an attempt's failures can be counted. */
esp_err_t app_board_wifi_join(const char *ssid, const char *pass);

/* Stop trying: drop the station's configuration and any association attempt. What a
 * forgotten network needs (wifi-provisioning.md P24) - without it the radio keeps retrying
 * the network that was just erased, and a retrying station refuses a scan. */
void app_board_wifi_forget(void);

/* Raise or drop the setup network, `K7.local` at 192.168.4.1 (ADR 0076). Idempotent:
 * asking for the state it is already in is not an error. A failure is the caller's to
 * report and retry - on this board it means the radio could not allocate. */
esp_err_t app_board_setup_ap(bool on);

/* One row per network the radio can see, strongest first. Returns the count, or -1 when
 * the scan could not run (busy, out of memory, the radio down). Blocking, bounded by
 * `timeout_ms`: about two seconds of airtime for the band. */
typedef struct {
    char ssid[WIFI_RECORD_SSID_MAX + 1];
    int8_t rssi;
    bool secure; /* anything but WIFI_AUTH_OPEN */
} app_board_scan_t;
#define APP_BOARD_SCAN_MAX 24
int app_board_wifi_scan(app_board_scan_t *out, int max, uint32_t timeout_ms);

/* Advertise `hostname.local` and `_mcp._tcp` with the endpoint TXT records. The
 * appliance calls this after the HTTP listener is up and Wi-Fi has an address;
 * the host adapter is a no-op. */
esp_err_t app_board_mdns_start(const char *hostname, uint16_t port, const char *path);

/* The Snapshot image this board can offer, and why not when it cannot: the mapped
 * `snapshot` partition on the device, a file named by XTAPE_SNAPSHOT on the host.
 * The pointer stays valid until app_board_snapshot_release(); the caller validates it. */
const uint8_t *app_board_snapshot_image(size_t *size, const char **why,
                                        int *state); /* state: snapshot_result_t */

/* Drop the image app_board_snapshot_image() returned: the device unmaps the partition,
 * the host frees its file buffer. Safe to call when nothing is mapped, and a later
 * app_board_snapshot_image() maps the image again. */
void app_board_snapshot_release(void);

/* One health probe of the mounted card. This is a directory read of the music
 * directory, not a cached mount flag: the FAT layer answers it from the card, so it
 * fails with EIO when the card stops answering, ETIMEDOUT when the FATFS volume lock
 * could not be taken within CONFIG_FATFS_TIMEOUT_MS, ENOENT when the card answered but
 * the directory is not on it, and ENODEV when nothing is mounted. `*err` is that errno (0 when it succeeded); the
 * caller decides which failures mean the card has gone. */
bool app_board_card_probe(int *err);

/* Give up the mount after the card has gone, so a reinsert can mount again. Best-effort:
 * a card that was pulled mid-write may leave the volume unable to unmount, and a reboot
 * is the only repair. */
void app_board_card_forget(void);

/* Whether the card currently holds an Update file. The appliance reboots on this, because
 * applying it needs the boot window, not a running panel. */
bool app_board_snapshot_pending(void);

typedef enum {
    APP_BOARD_UPDATE_NONE = 0, /* no card, or no update file: nothing to do */
    APP_BOARD_UPDATE_APPLIED,  /* flashed, verified, and the file deleted */
    APP_BOARD_UPDATE_FAILED,   /* there was an update and it was not applied */
} app_board_update_t;

typedef enum {
    APP_BOARD_FLASH_OK = 0,      /* validated, written, read back */
    APP_BOARD_FLASH_INVALID,     /* the image is not a snapshot; nothing was written */
    APP_BOARD_FLASH_NO_PARTITION, /* no snapshot partition in this build */
    APP_BOARD_FLASH_FAILED,      /* the partition refused a write */
} app_board_flash_t;

/* Write an image that arrived over HTTP into the snapshot partition. The image is
 * validated in full - header, rows, checksum - before the first erase, so a refusal leaves
 * the running snapshot intact. The caller reboots afterwards: the reader holds a PSRAM
 * copy, and on this board a flash write disables the PSRAM the app is running from. */
app_board_flash_t app_board_snapshot_flash(const uint8_t *image, size_t len, char *why,
                                           size_t why_size);

/* Restart the board after `ms`, so a reply can be written first. A no-op on the host,
 * where "restart" is the test's next process. */
void app_board_reboot_soon(uint32_t ms);

/* Apply CARD/k7.db to the snapshot partition and delete it, before anything that
 * needs PSRAM is running (a flash write disables the cache, and on this board the
 * app's code lives in PSRAM). A card written before the rename still holds
 * CARD/xtape.db, which is read when the current name is absent. Mounts the card if it
 * can, and leaves it as it found it. A failure never reboots and never removes the
 * file. */
app_board_update_t app_board_snapshot_update(void);

#ifdef __cplusplus
}
#endif
