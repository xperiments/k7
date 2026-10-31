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

const char *app_board_name(void) { return "host"; }

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

/* The host has no partition, so the Snapshot is a file: XTAPE_SNAPSHOT names it and
 * the integration test points it at a generated image. Absent is the normal host
 * state, not an error. */
/* The host card is a path that never goes away, and it never carries an update file: the
 * host exercises the snapshot through XTAPE_SNAPSHOT, not through the card. */
bool app_board_card_present(void) { return getenv("XTAPE_HOST_NO_CARD") == NULL; }
void app_board_card_forget(void) { }
bool app_board_snapshot_pending(void) { return false; }

const uint8_t *app_board_snapshot_image(size_t *size, const char **why, int *state)
{
    static uint8_t *image;
    static size_t   image_size;
    static bool     tried;

    if (state) *state = 1; /* SNAPSHOT_NO_PARTITION */
    if (tried) {
        if (size) *size = image_size;
        if (why && !image) *why = "no snapshot partition on the host";
        return image;
    }
    tried = true;
    if (why) *why = "no snapshot partition on the host";

    const char *path = getenv("XTAPE_SNAPSHOT");
    if (!path || !path[0]) return NULL;
    /* From here on a failure is a bad image, not an absent one. */
    if (state) *state = 3; /* SNAPSHOT_INVALID */
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (why) *why = "XTAPE_SNAPSHOT could not be opened";
        return NULL;
    }
    long n = -1;
    if (fseek(f, 0, SEEK_END) == 0) n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        if (why) *why = "XTAPE_SNAPSHOT is not a readable file";
        return NULL;
    }
    image = malloc((size_t)n);
    if (!image || fread(image, 1, (size_t)n, f) != (size_t)n) {
        free(image);
        image = NULL;
        fclose(f);
        if (why) *why = "XTAPE_SNAPSHOT could not be read";
        return NULL;
    }
    fclose(f);
    image_size = (size_t)n;
    if (size) *size = image_size;
    if (why) *why = "";
    if (state) *state = 0; /* SNAPSHOT_OK */
    return image;
}
