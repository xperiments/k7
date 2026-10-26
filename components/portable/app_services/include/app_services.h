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
 *   5. file surface                (the index and downloads)
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

/* Tell the composition whether the retry task has a mounted card. The rescan
 * uses the no-card generation when there is none, so the state stays
 * diagnostic instead of turning into a filesystem error. */
void app_services_card_state(bool present, const char *mount_error);

#ifdef __cplusplus
}
#endif
