/*
 * The host build's board adapter.
 *
 * There is no Wi-Fi and no SPI card here: the music directory is a path and it is
 * always "mounted". The adapter exists so the services composition the firmware
 * runs is the composition the host tests and integration suite run - the board is
 * the only thing that differs.
 */
#include "app_board.h"

esp_err_t app_board_net_start(uint16_t http_port) { (void)http_port; return ESP_OK; }

app_board_card_t app_board_card_mount(const char *expected_music_dir)
{
    app_board_card_t card = {.music_dir = expected_music_dir, .mount_error = NULL};
    return card;
}

void app_board_wait_online(void) { }

void app_board_log_hardware(void) { }
