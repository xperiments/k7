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

#ifdef __cplusplus
}
#endif
