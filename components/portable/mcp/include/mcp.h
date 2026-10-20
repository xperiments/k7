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

typedef struct {
    const library_t *library; /* may be NULL; library tools then report an error */
    uint32_t scan_ms;         /* the boot scan+build time, for library_stats */
} mcp_cfg_t;

/* Register GET/POST /mcp. Call before http_server_start(). */
esp_err_t mcp_start(const mcp_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
