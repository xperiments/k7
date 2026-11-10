#pragma once
/*
 * snapshot_http - POST /api/snapshot, the one way an image reaches the device.
 *
 * The body is validated before the seam is asked to write it: the host's partition is a file
 * and the device's is flash, and neither should be the place where "is this a Snapshot" is
 * answered. Opening it is the validation. The write runs on the flash writer's task, because
 * a write disables the cache the connection worker's PSRAM stack lives behind.
 *
 * The content type is the guard: `application/octet-stream` is not CORS-safelisted, so a page
 * on another origin cannot send it without a preflight this device never answers.
 */
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register the route and its body limit. `why`/`why_size` are filled with the validator's
 * sentence when an image is refused. */
esp_err_t snapshot_http_start(void);

#ifdef __cplusplus
}
#endif
