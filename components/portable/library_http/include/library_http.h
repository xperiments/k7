#pragma once
/*
 * library_http - the prebuilt Library view over HTTP.
 *
 * One route, GET /api/library. It reads the immutable JSON generation the
 * application loaded at boot and sends it with its real Content-Length; it never
 * walks the card. The generation's state (ok, empty, truncated, no_card,
 * no_dir) is already in the body, so a missing card is a diagnosable 200 rather
 * than a 500.
 */
#include "esp_err.h"
#include "library.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register GET /api/library. The generation must outlive the route; the
 * application owns it. */
esp_err_t library_http_start(const library_t *lib);

#ifdef __cplusplus
}
#endif
