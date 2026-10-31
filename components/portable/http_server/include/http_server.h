#pragma once
/*
 * http_server - the device's HTTP front door.
 *
 * Two listeners, one API. The app is served over TLS on 443
 * (https://xtape.local) and the broadcast over plain HTTP on 80
 * (http://xtape.local/stream); a route names the listener that carries it, and
 * both tiers register, reply and start through this header alone.
 *
 * The transport underneath is a build decision, not a runtime one. The device
 * compiles idf/http_server_idf.c: esp_https_server (IDF's httpd) for the TLS
 * listener, and the hand-written loop in http_loop.c for the plain one, because
 * /stream hands the socket to the broadcast and writes it itself. The host
 * compiles http_backend_single.c, which is that same loop on a single plain
 * listener: TLS routes register there and are served as plain HTTP, which is
 * what the integration checks exercise. No source here is built for both tiers
 * by branching on the target; the build picks the file.
 *
 * This component knows nothing about audio, files or markup. All three arrive
 * as routes registered by other components before the server starts, which
 * keeps the dependency direction one-way: mini_icecast and mini_files depend on
 * http_server, and http_server depends on neither.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A client that cannot take data for this long is dropped. Every route that
 * writes a body applies it, so one stall rule covers the whole surface. */
#define HTTP_CLIENT_STALL_MS 400

/* A connection that has not finished sending its request header in this long is
 * abandoned, freeing its connection slot. */
#define HTTP_HEADER_TIMEOUT_MS 2000

#define HTTP_PATH_MAX 512 /* raw (percent-encoded) request target */
#define HTTP_BODY_MAX 8192 /* a POST body larger than this is 413 */
#define HTTP_QUERY_MAX 256 /* raw (percent-encoded) query string, without the ? */
#define HTTP_HEADER_MAX 1024
#define HTTP_MAX_ROUTES 16 /* the appliance's fourteen, plus the screen's two diagnostics */
#define HTTP_LISTEN_BACKLOG 16
#define HTTP_MAX_CONNS 6

/* The two listeners. A route is served by exactly one of them; the device's
 * plain listener answers /stream and nothing else. A build with a single
 * listener (the host) serves every route on it, whatever the tag says, because
 * it does not filter. */
typedef enum {
    HTTP_LISTENER_TLS = 0,   /* https://xtape.local: the app, the API, MCP */
    HTTP_LISTENER_PLAIN = 1, /* http://xtape.local: the stream, plain, forever */
} http_listener_t;

typedef struct {
    char method[8];           /* request method, e.g. "GET" */
    char path[HTTP_PATH_MAX]; /* request target, percent-encoded, query stripped */
    char query[HTTP_QUERY_MAX]; /* everything after the '?', percent-encoded; "" if none */
    bool want_icy_metadata;   /* the client asked for inline ICY metadata */
    bool has_content_length;
    size_t content_length;
    char content_type[64];    /* lower-case value up to the first ';'; "" if absent */
    char mcp_protocol_version[16]; /* MCP-Protocol-Version, "" when absent */
    char origin[128];         /* Origin header, "" when absent */
    char host[128];           /* Host header, "" when absent */
    /* The POST body, heap-owned by the server for the duration of the route.
     * NUL-terminated for the JSON parsers, so body_len excludes the NUL. */
    const uint8_t *body;
    size_t body_len;
} http_request_t;

/* What a route handler did with the connection.
 *
 * HTTP_HANDLED: the handler wrote a complete response. The server closes the fd.
 * HTTP_UPGRADED: the handler took ownership of the fd (a live stream that will
 *                outlive this request). The server must not close it. Only the
 *                plain listener can honour this - it is the only transport that
 *                owns a bare socket; on TLS the flag is logged and ignored. */
typedef enum {
    HTTP_HANDLED = 0,
    HTTP_UPGRADED = 1,
} http_result_t;

typedef http_result_t (*http_route_fn)(int fd, const http_request_t *req, void *ctx);

typedef struct {
    uint16_t    port;        /* default 8000; the IDF backend ignores it and uses 443/80 */
    const char *server_name; /* "Server:" header value; default "sd-stream/1.0" */
    uint8_t     max_conns;   /* concurrent request tasks, 1..HTTP_MAX_CONNS, default 4 */
    /* Which listener this instance is. False (the default, and the host build)
     * means "answer every registered route": one listener, no filtering. The
     * device sets it on the plain listener so that listener answers /stream and
     * nothing else. */
    bool            filter_by_listener;
    http_listener_t listener;
    /* Answer a path this listener has no route for with 301 to HTTPS (front door
     * R2: plain port 80 redirects everything that is not the stream). */
    bool        redirect_to_tls;
    const char *tls_host; /* the name to redirect to when the request has no Host */
    uint16_t    tls_port; /* 0 = 443; only named in Location when it is not 443 */
} http_server_cfg_t;

/* Register a route. `prefix` is matched against the start of the raw request
 * path and the longest matching prefix wins; "/" therefore acts as the catch-all
 * and "/api/" takes precedence over it. Must be called before
 * http_server_start(), which is when main() should call it. These register on
 * the TLS listener, the app's: use the _on form to name the other one. */
esp_err_t http_server_register_route(const char *prefix, http_route_fn fn, void *ctx);
/* Register a route for one method. The GET wrapper above is the common case.
 * A path match with the wrong method is a 405, not a 404. */
esp_err_t http_server_register_route_method(const char *prefix, const char *method,
                                            http_route_fn fn, void *ctx);
/* A route that wants to render its own transport errors (MCP does). */
typedef void (*http_error_fn)(int fd, int status, const char *detail, void *ctx);
esp_err_t http_server_register_route_method_ex(const char *prefix, const char *method,
                                               http_route_fn fn, void *ctx,
                                               http_error_fn error_fn);

/* The same three, on a named listener. */
esp_err_t http_server_register_route_on(http_listener_t listener, const char *prefix,
                                        http_route_fn fn, void *ctx);
esp_err_t http_server_register_route_method_on(http_listener_t listener, const char *prefix,
                                               const char *method, http_route_fn fn, void *ctx);
esp_err_t http_server_register_route_method_ex_on(http_listener_t listener, const char *prefix,
                                                  const char *method, http_route_fn fn,
                                                  void *ctx, http_error_fn error_fn);

/* Bind, listen and start accepting. Returns once this build's listener (or
 * listeners) are up. */
esp_err_t http_server_start(const http_server_cfg_t *cfg);

/* ------------------------------------------------------------------ replies */

/* Write a status line and headers. `body_len` of (size_t)-1 omits
 * Content-Length, meaning "the body ends when this connection closes".
 * `extra_headers` may be NULL; it must end with CRLF if given. A Cache-Control in
 * `extra_headers` replaces the default `no-cache` rather than joining it — two of them
 * in one response is a contradiction, and the first one wins. */
esp_err_t http_send_headers(int fd, int status, const char *content_type,
                            size_t body_len, const char *extra_headers);

/* Write a small self-describing error page (400/404/405/503/...). */
esp_err_t http_send_error(int fd, int status, const char *detail);

/* send() until everything is written or the send timeout fires. */
bool http_send_all(int fd, const void *data, size_t n);

/* How long a single send() may block before the client is treated as gone. The
 * per-connection default is HTTP_CLIENT_STALL_MS; a route that transfers more
 * than a few kilobytes over a link that stalls should ask for longer. */
void http_send_timeout_ms(int fd, uint32_t ms);

/* The least stack headroom any connection task has had left, in bytes. Part of
 * the memory-fit telemetry: a route that grows past the budget shows up here
 * before it shows up as a crash. It reports the plain listener's workers; the
 * TLS listener's stack is httpd's own and is not measured here. */
uint32_t http_server_conn_stack_headroom_bytes(void);

/* Decode a percent-encoded string in place into `out`.
 * Returns the decoded length, or (size_t)-1 if the encoding is malformed or the
 * decoded result would not fit - it never truncates silently, because a
 * truncated name could turn a rejected name into an accepted one. */
size_t http_url_decode(const char *in, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
