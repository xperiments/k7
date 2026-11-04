/*
 * mi_station - the default table and the arithmetic, in one place.
 *
 * This is the only code in the component that knows what a zero field means.
 * mini_icecast_check_station() and mini_icecast_start() both call it, so the
 * configuration contract has one interpretation rather than one per entry point.
 */
#include "mi_station.h"

#include <stdio.h>

#include "mi_encode.h"

#define DEFAULT_OUT_HZ   44100
#define DEFAULT_LEAD_MS  1500
#define DEFAULT_BURST_MS 500
#define DEFAULT_CLIENTS  2 /* the documented default; 0 means "leave it" */

esp_err_t mi_station_resolve(const mini_icecast_cfg_t *cfg, mi_station_t *out,
                             char *err, size_t err_size)
{
    if (err && err_size) err[0] = 0;
    if (!cfg) {
        if (err && err_size) snprintf(err, err_size, "no configuration given");
        return ESP_ERR_INVALID_ARG;
    }

    mi_station_t s = {
        .station_name = cfg->station_name ? cfg->station_name : "ESP32 Radio",
        .port         = cfg->port ? cfg->port : 8000,
        .max_clients  = cfg->max_clients ? cfg->max_clients : DEFAULT_CLIENTS,
        .out_hz       = cfg->out_hz ? cfg->out_hz : DEFAULT_OUT_HZ,
        .bitrate      = cfg->bitrate ? cfg->bitrate : MINI_ICECAST_BITRATE_DEFAULT,
        .metaint      = cfg->metaint ? cfg->metaint : MINI_ICECAST_METAINT_DEFAULT,
        .lead_ms      = cfg->lead_ms ? cfg->lead_ms : DEFAULT_LEAD_MS,
        .burst_ms     = cfg->burst_ms ? cfg->burst_ms : DEFAULT_BURST_MS,
        .on_client_connect = cfg->on_client_connect,
    };

    /* Refuse rather than coerce: a client limit outside the table would be a
     * silent behaviour change (defect D3), and the caller can fix one value. */
    if (s.max_clients > MINI_ICECAST_MAX_CLIENTS) {
        if (err && err_size)
            snprintf(err, err_size, "max_clients %u is outside the documented 1..%d range",
                     (unsigned)s.max_clients, MINI_ICECAST_MAX_CLIENTS);
        return ESP_ERR_INVALID_ARG;
    }

    /* Validation is arithmetic: no allocation, no hardware. */
    s.mpeg = mi_enc_check_format(s.out_hz, s.bitrate, err, err_size);
    if (s.mpeg < 0) return ESP_ERR_INVALID_ARG;

    if (out) *out = s;
    return ESP_OK;
}
