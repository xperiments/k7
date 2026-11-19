#pragma once
/*
 * app_services - the device's services, composed once.
 *
 * The order below is load-bearing and used to exist only as the sequence of calls
 * in the appliance and, differently, in host/icecast_host.c:
 *
 *   1. resolve the station format  (arithmetic only: no heap, no hardware)
 *   2. board network               (Wi-Fi's DMA buffers win the internal-RAM race)
 *   3. card mount                  (a missing card is reported, not fatal)
 *   4. library                     (the boot-built tree, arena and JSON)
 *   5. file surface                (the index: a listing, not a file browser)
 *   6. library endpoint            (GET /api/library from the prebuilt JSON)
 *   7. broadcast                   (the encoder takes its internal RAM last)
 *   8. HTTP listener               (starts serving what is already registered)
 *
 * The appliance and the host entry point call this, so the host build exercises the
 * device's composition instead of a second one, and the ordering has one home.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "snapshot.h"   /* the handle app_services_snapshot() borrows */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *music_dir;    /* the directory the broadcast will play */
    const char *station_name;
    uint16_t    port;
    uint32_t    out_hz;       /* station rate */
    uint32_t    bitrate;      /* station bit rate, kbps */
    uint8_t     max_clients;  /* broadcast listeners */
    uint8_t     max_conns;    /* HTTP connections */
    uint32_t    metaint;      /* 0 = the broadcast's default (16000) */
    uint32_t    lead_ms;      /* 0 = the broadcast's default (1500) */
    uint32_t    burst_ms;     /* 0 = the broadcast's default (500) */
    uint32_t    library_max_bytes;  /* 0 = the library's default (4 MiB) */
    uint32_t    library_max_tracks; /* 0 = the library's default (20000) */
    const char *server_name;  /* HTTP Server: header; NULL = the server's default */
    /* The development App's origin, or NULL. A development firmware build sets it and the
     * device then accepts either this origin or the deployed one in its CORS reply and its
     * MCP guard. NULL (the shipping build and the host) trusts the deployed origin alone. */
    const char *app_origin_dev;
    /* The remembered Playlist selection (id, 0 for none). NULL means this build
     * remembers none: the choice then lives until reboot. Wired to NVS by the
     * appliance, left NULL by the host. */
    bool (*selection_load)(uint64_t *id);
    bool (*selection_save)(uint64_t id);
} app_services_cfg_t;

typedef struct {
    bool station_ok;
    bool card_present;
    bool files_up;
    bool broadcast_up;
    bool http_up;
    char station_error[192]; /* empty when the station resolved */
    char card_error[160];    /* empty when the card mounted */
    char failure[256];       /* the first failure, for the boot log */
} app_services_report_t;

/* Resolve the station configuration without allocating, touching the board or
 * starting anything. Returns ESP_OK, or ESP_ERR_INVALID_ARG with `err` filled. */
esp_err_t app_services_check(const app_services_cfg_t *cfg, char *err, size_t err_size);

/* Start the board and the services in the order documented above. Returns the
 * first failure; on any error `report->failure` says what could not start. A
 * missing card is not a failure: the file surface still serves the index. */
esp_err_t app_services_start(const app_services_cfg_t *cfg, app_services_report_t *report);

/* Build and publish a new Library generation off the side. Returns ESP_OK when
 * the scan task started, ESP_ERR_INVALID_STATE when one is already running, and
 * ESP_ERR_NO_MEM when the task could not be created. The broadcast finishes its
 * current Track on the old generation. */
esp_err_t app_services_rescan(void);

/* Whether a rescan task is running. The Menu's Rescan row shows "Rescanning..." from it;
 * starting a scan to find out would be the bug. */
bool app_services_rescan_running(void);

/* The player's transport (espnow-stream.md E9, ADR 0105): true is ESP-NOW, false is HTTP.
 * The setter applies it now and remembers it; the init reads it back at boot and defaults to
 * ESP-NOW. The micro switch on the player and the K7's Settings row both call the setter.
 *
 * The station's format follows the transport, so the setter also restarts the station at the
 * new bit rate (ADR 0106) - the one place the 128/256 rule lives is
 * app_services_station_bitrate(). */
bool app_services_player_espnow(void);
void app_services_player_espnow_set(bool on);
void app_services_player_transport_init(void);

/* The station's bit rate for a transport: 128 kbps when the radio carries the player's
 * audio, 256 when the web's icecast does (espnow-stream.md E9). */
uint32_t app_services_station_bitrate(bool player_espnow);

/* Stop the station and start it again at `kbps`, leaving the network, the card, the HTTP
 * server and the panel alone (ADR 0106). A listener is disconnected, which is what a station
 * restart means, and the Track that was playing is asked for again on the new station. The
 * boot has no need for this: it starts once, at the format the transport implies. */
esp_err_t app_services_station_restart(uint32_t kbps);

/* Tell the composition whether the retry task has a mounted card. The rescan
 * uses the no-card generation when there is none, so the state stays
 * diagnostic instead of turning into a filesystem error. */
void app_services_card_state(bool present, const char *mount_error);

/* Whether the card answers right now. The screen draws its SD mark from this, and every
 * rescan reads it, so one flag answers "is there a library" for both. */
bool app_services_card_present(void);

/* The mounted card's root, or "" when there is none. The Menu's view reads
 * `k7.playlists/` and walks a list through the portable modules, which take a root; only
 * this component knows how the card is mounted (ADR 0096). */
void app_services_card_root(char *out, size_t cap);

/* The opened Snapshot, or NULL when this device has none (no partition, no image, or an
 * image that did not validate). Borrowed, not owned: an update replaces the image, so a
 * caller copies what it needs within the step that asked. The screen asks once per Track
 * change for the row a Slot prints from (docs/specs/label-templates.md §5). */
const snapshot_t *app_services_snapshot(void);

#ifdef __cplusplus
}
#endif
