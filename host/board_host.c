/*
 * The host build's board adapter.
 *
 * There is no Wi-Fi and no SPI card here: the music directory is a path and it is
 * "mounted" unless XTAPE_HOST_NO_CARD is set, the host-only switch the acceptance
 * tests use for the cardless state. The adapter exists so the services composition
 * the firmware runs is the composition the host tests and integration suite run -
 * the board is the only thing that differs.
 */
#include "app_board.h"

#include <stdio.h>
#include <stdlib.h>

esp_err_t app_board_net_start(uint16_t http_port) { (void)http_port; return ESP_OK; }

app_board_card_t app_board_card_mount(const char *expected_music_dir)
{
    /* A host-only switch for the cardless acceptance case; the firmware has no
     * equivalent because its SD slot is the hardware. */
    if (getenv("XTAPE_HOST_NO_CARD")) {
        app_board_card_t none = {.music_dir = NULL, .mount_error = "host: no card requested"};
        return none;
    }
    app_board_card_t card = {.music_dir = expected_music_dir, .mount_error = NULL};
    return card;
}

void app_board_wait_online(void) { }

bool app_board_local_ip(char *out, size_t out_size)
{
    if (!out || !out_size) return false;
    snprintf(out, out_size, "127.0.0.1");
    return true;
}

esp_err_t app_board_mdns_start(const char *hostname, uint16_t port, const char *path)
{
    (void)hostname;
    (void)port;
    (void)path;
    return ESP_OK; /* there is no mDNS in the host build */
}

void app_board_log_hardware(void) { }
