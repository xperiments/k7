#pragma once
/*
 * http_parse - what a request line and a route table mean, without a socket.
 *
 * The rules here are the ones a client can violate: absolute-form request
 * targets, query strings (kept, for routes that take parameters), over-long paths,
 * case-insensitive `Icy-MetaData`, and
 * longest-prefix routing. They used to be static inside the connection worker,
 * where the only way to observe them was to open a connection. They are pure
 * functions now, so the awkward requests can be tested as a table.
 */
#include <stdbool.h>
#include <stddef.h>

#include "http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Parse one complete request header block (NUL-terminated, headers ending with
 * CRLFCRLF). Returns 0, or the HTTP status to answer with (400, 414).
 *
 * `buf` is modified: the request line is split in place, so the caller must own a
 * writable header block. `req` is overwritten. */
int http_parse_request(char *buf, http_request_t *req);

/* Split a raw request target into path and query. Returns 0, or 400/414. This is
 * the rule both tiers apply: the hand-written parser calls it on its request
 * line, and the device's TLS backend calls it on httpd's `r->uri`, which arrives
 * with the query still attached. */
int http_split_target(const char *target, char *path, size_t path_size,
                      char *query, size_t query_size);

/* A route table entry. The server keeps its own array; tests can build one. */
typedef void (*http_error_fn)(int fd, int status, const char *detail, void *ctx);

typedef struct {
    const char  *prefix;
    size_t       plen;
    const char  *method; /* NULL means "GET" */
    http_route_fn fn;
    void         *ctx;
    http_error_fn error_fn; /* optional transport-error renderer */
    /* Which listener carries this route. TLS - the app - is the default, so a
     * route that does not say is the app's. /stream names the plain one. */
    http_listener_t listener;
} http_route_t;

/* Why a lookup found no route. */
typedef enum {
    HTTP_ROUTE_MATCHED = 0,
    HTTP_ROUTE_NOT_FOUND,
    HTTP_ROUTE_WRONG_METHOD,
} http_route_find_t;

/* The route whose prefix is the longest match for `path`, or NULL. The compare is
 * a plain prefix compare: a path is matched raw (still percent-encoded). */
const http_route_t *http_route_match(const http_route_t *routes, int count, const char *path);
/* The longest prefix whose method also matches; a wrong-method path is deliberately
 * not a match, so the server can tell 405 from 404. */
const http_route_t *http_route_match_method(const http_route_t *routes, int count,
                                            const char *path, const char *method);

/* The same lookup, plus the listener a multi-listener build must respect, and the
 * reason when nothing matched. `filter_by_listener` false serves every route,
 * which is the host's single listener; true serves only the routes tagged
 * `listener`. */
const http_route_t *http_route_lookup(const http_route_t *routes, int count,
                                      bool filter_by_listener, http_listener_t listener,
                                      const char *path, const char *method,
                                      http_route_find_t *how);

#ifdef __cplusplus
}
#endif
