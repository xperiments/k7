#pragma once
/*
 * setup_http - the one page the device serves, and the routes behind it.
 *
 * `GET /setup` is the page a person reaches on the setup network at 192.168.4.1 (or
 * http://k7.local/setup where mDNS works), and on the LAN while the device is healthy.
 * It is the device's only page: `/` still redirects to the App (ADR 0067), and this one
 * exists because provisioning is the recovery path and must not depend on a cached PWA,
 * a particular browser, or the local-network permission (ADR 0076).
 *
 * The page is a constant in this component: no build step, no asset, no framework, about
 * six kilobytes. Its JavaScript talks to the five routes registered here, all of which
 * answer JSON except the page itself.
 *
 * It is deliberately not styled with the App's design system: it is measured in kilobytes
 * of firmware, and it must render on whatever browser is in the room.
 */
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register `/setup` and the `/api/setup*` routes. Call before http_server_start(). */
esp_err_t setup_http_start(void);

#ifdef __cplusplus
}
#endif
