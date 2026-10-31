#pragma once
/*
 * http_transport - the seam between the front door and the wire.
 *
 * http_server.c holds what the replies mean - the route table, the body rules, the status
 * codes, the page's bytes - while the transport owns the socket: the accept loop, reading a
 * request, and writing a reply. The split is not "no socket code in http_server.c"; it is
 * that every rule lives once and both transports ask it.:
 * the route table, the reply rules (one Cache-Control, the Server header, the
 * POST body rules), and the public API. This header is where that front meets a
 * transport, and the build picks the transport by picking a file:
 *
 *   http_backend_single.c   the host: one plain listener, the hand-written loop.
 *   idf/http_server_idf.c   the device: esp_https_server on 443 for the app, and
 *                           the same hand-written loop on 80 for /stream.
 *
 * Exactly one of those two defines the five functions under "a transport
 * provides". Nothing here branches on the target, and neither tier's handlers
 * know which file answered the request: they call http_send_headers() with the
 * `fd` the server handed them, and the front decides where it goes.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "http_parse.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A response's header block, decided once so the two transports cannot disagree
 * about it. `extra_headers` is the caller's raw block and is only valid until
 * http_send_headers() returns; a transport that must keep it copies it. */
typedef struct {
    int         status;
    const char *reason;         /* the phrase for status, e.g. "Not Found" */
    const char *server_name;    /* never NULL */
    const char *content_type;   /* NULL: no Content-Type */
    bool        has_length;     /* false: the body ends when the connection closes */
    size_t      length;
    const char *extra_headers;  /* CRLF-terminated, or NULL */
    bool        default_cache_control; /* add "Cache-Control: no-cache" */
} http_reply_t;

/* ------------------------------------------------------- a transport provides */

/* Start this build's listener or listeners. Called once, with the defaults of
 * http_server_cfg_t already filled in. ESP_OK means at least the plain listener
 * is serving. */
esp_err_t http_transport_start(const http_server_cfg_t *cfg);

/* True when `fd` is a request this transport is carrying and must write (the
 * device's TLS listener). False for a bare socket the front writes itself with
 * send(), which is the host, and the device's plain listener. */
bool http_transport_owns(int fd);

/* Open a response for a request this transport owns: remember the status, the
 * type and the headers. Nothing is on the wire until the first body write. */
esp_err_t http_transport_reply(int fd, const http_reply_t *reply);

/* One body write. False if the client is gone or the response cannot take it. */
bool http_transport_body(int fd, const void *data, size_t n);

/* Extend the stall timeout for this connection. */
void http_transport_send_timeout(int fd, uint32_t ms);

/* ---------------------------------------------------------- the front offers */

/* The reason phrase for a status, "Error" for one this server never mints. */
const char *http_reason_phrase(int status);

/* Fill in the reply rules for these arguments: the status phrase, whether a
 * Content-Length is sent, and whether the default Cache-Control applies. */
void http_plan_reply(http_reply_t *out, const char *server_name, int status,
                     const char *content_type, size_t body_len, const char *extra_headers);

/* http_split_target() - path and query out of a raw request target - is declared
 * in http_parse.h, which this header includes: it is a parsing rule, and both
 * tiers must apply the same one. */

/* The route this path and method resolve to on this listener, or NULL with `how`
 * saying whether the path is unknown (404) or known with another method (405). */
const http_route_t *http_find_route(bool filter_by_listener, http_listener_t listener,
                                    const char *path, const char *method,
                                    http_route_find_t *how);

/* The POST body rules, in one place: 0 when the request may be read by its
 * route, or the status to answer with (411 no Content-Length, 413 too large, 415
 * not JSON) and a detail line for the page. */
int http_post_rules(const http_request_t *req, const char *route_method, const char **detail);

/* Render a failed request through the route's own error function when it has one
 * (MCP does), and through http_send_error() otherwise. */
void http_route_error(int fd, const http_route_t *route, int status, const char *detail);

/* A POST body buffer of `n` bytes plus the NUL the JSON parsers want, from
 * external memory when there is any. NULL when there is no memory. */
uint8_t *http_body_alloc(size_t n);

/* ------------------------------------------------------------------ the loop */

/* The hand-written accept/parse/route loop. The host's whole server; the
 * device's plain listener, where /stream takes the socket and writes it itself. */
esp_err_t http_loop_start(const http_server_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
