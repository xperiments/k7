#pragma once
/*
 * mi_station - the station configuration, resolved once.
 *
 * mini_icecast_cfg_t documents that a zero field means "the value this header
 * recommends". That rule used to live only in mini_icecast_start(), so
 * mini_icecast_check_station() validated the raw fields and refused a
 * configuration the start would have served. This module is the one authority:
 * it fills the defaults, does the arithmetic, and returns either a resolved
 * station or a refusal that names the offending value. Every entry point uses
 * it, so a check and a start can never disagree.
 */
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "mini_icecast.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *station_name; /* never NULL once resolved */
    uint16_t    port;
    uint8_t     max_clients;  /* 1..MINI_ICECAST_MAX_CLIENTS */
    uint32_t    out_hz;       /* station rate */
    uint32_t    bitrate;      /* kbps */
    uint32_t    metaint;      /* encoded bytes between metadata blocks */
    uint32_t    lead_ms;
    uint32_t    burst_ms;
    /* mi_enc_check_format()'s tier: 3 = MPEG-1, 2 = MPEG-2, 0 = MPEG-2.5, which
     * is VALID - test `>= 0`, never truthiness. */
    int         mpeg;
} mi_station_t;

/* Resolve a configuration. `out` may be NULL, which is the check-only call.
 * Returns ESP_OK, or ESP_ERR_INVALID_ARG with `err` filled in. It allocates
 * nothing and touches no hardware, which is why it can run before Wi-Fi and the
 * encoder compete for internal RAM. */
esp_err_t mi_station_resolve(const mini_icecast_cfg_t *cfg, mi_station_t *out,
                             char *err, size_t err_size);

#ifdef __cplusplus
}
#endif
