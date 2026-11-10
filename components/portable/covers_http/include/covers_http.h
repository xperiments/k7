#pragma once
/*
 * covers_http - GET /api/covers, the card's preprocessed cover archive.
 *
 * It is a cache the App wrote, not something the device made: the bytes are served verbatim
 * and the device never opens one. Reading it touches the card, not flash.
 */
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register the route. The archive's path comes from card_path() when the request arrives. */
esp_err_t covers_http_start(void);

#ifdef __cplusplus
}
#endif
