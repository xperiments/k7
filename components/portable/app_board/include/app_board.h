#pragma once
/*
 * app_board - what the services need from the board, and nothing else.
 *
 * The seam exists because it already has two real adapters: the ESP32-S3 board
 * (main/board_esp32.c: Wi-Fi credentials, the SPI card mount, the hardware
 * census) and the host build (host/board_host.c: no network, the fixture
 * directory always mounted). The composition module calls this and knows nothing
 * about GPIOs, esp_wifi or FAT.
 */
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *music_dir;   /* the expected directory when the card is mounted, else NULL */
    const char *mount_error; /* why, when music_dir is NULL */
} app_board_card_t;

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

#ifdef __cplusplus
}
#endif
