#pragma once
/*
 * mcp - the read-only MCP endpoint over HTTP.
 *
 * One route, POST /mcp, speaking the legacy 2025-11-25 lifecycle: stateless,
 * JSON-only, no SSE and no session header. The tool surface is the Library tree
 * and the player status; control tools are a separate, compile-time-gated
 * ticket. Nothing here touches the card: every tool reads the immutable
 * generation the application loaded at boot.
 */
#include "esp_err.h"
#include "library.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Called per request so the Origin guard sees the address the device has now,
 * not the one it had at boot. The callback writes a bare IPv4 address. */
typedef bool (*mcp_local_ip_fn)(char *out, size_t out_size);
typedef esp_err_t (*mcp_rescan_fn)(void);

typedef struct {
    const library_t *library; /* may be NULL; library tools then report an error */
    uint32_t scan_ms;         /* the boot scan+build time, for library_stats */
    mcp_local_ip_fn local_ip; /* may be NULL; the guard then only accepts xtape.local */
    mcp_rescan_fn rescan;     /* control builds only; may be NULL */
    uint16_t port;            /* the HTTP port; 0 means 8000 */
} mcp_cfg_t;

/* Register GET/POST /mcp. Call before http_server_start(). */
esp_err_t mcp_start(const mcp_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
