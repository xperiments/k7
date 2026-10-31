#pragma once
/*
 * app_board - what the services need from the board, and nothing else.
 *
 * The seam exists because it already has two real adapters: the ESP32-S3 board
 * (components/app/board_esp32/board_esp32.c: Wi-Fi credentials, the SPI card mount,
 * the hardware census) and the host build (host/board_host.c: no network, the
 * fixture directory always mounted). The composition and the appliance call this and
 * know nothing about GPIOs, esp_wifi or FAT.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

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

/* Advertise `hostname.local` and `_mcp._tcp` with the endpoint TXT records. The
 * appliance calls this after the HTTP listener is up and Wi-Fi has an address;
 * the host adapter is a no-op. */
esp_err_t app_board_mdns_start(const char *hostname, uint16_t port, const char *path);

/* The Snapshot image this board can offer, and why not when it cannot: the mapped
 * `snapshot` partition on the device, a file named by XTAPE_SNAPSHOT on the host.
 * The pointer stays valid for the life of the process; the caller validates it. */
const uint8_t *app_board_snapshot_image(size_t *size, const char **why,
                                        int *state); /* state: snapshot_result_t */

/* Whether the card answers right now. A stat of the mount point: the card is pulled by
 * hand, and this is the cheapest question the FAT layer can answer. */
bool app_board_card_present(void);

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
 * the running snapshot intact. The caller reboots afterwards: the reader holds a mapping,
 * and on this board a flash write disables the PSRAM the app is running from. */
app_board_flash_t app_board_snapshot_flash(const uint8_t *image, size_t len, char *why,
                                           size_t why_size);

/* Restart the board after `ms`, so a reply can be written first. A no-op on the host,
 * where "restart" is the test's next process. */
void app_board_reboot_soon(uint32_t ms);

/* Apply CARD/xtape.db to the snapshot partition and delete it, before anything that
 * needs PSRAM is running (a flash write disables the cache, and on this board the
 * app's code lives in PSRAM). Mounts the card if it can, and leaves it as it found
 * it. A failure never reboots and never removes the file. */
app_board_update_t app_board_snapshot_update(void);

#ifdef __cplusplus
}
#endif
