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

/* A route table entry. The server keeps its own array; tests can build one. */
typedef struct {
    const char  *prefix;
    size_t       plen;
    const char  *method; /* NULL means "GET" */
    http_route_fn fn;
    void         *ctx;
} http_route_t;

/* The route whose prefix is the longest match for `path`, or NULL. The compare is
 * a plain prefix compare: a path is matched raw (still percent-encoded). */
const http_route_t *http_route_match(const http_route_t *routes, int count, const char *path);
/* The longest prefix whose method also matches; a wrong-method path is deliberately
 * not a match, so the server can tell 405 from 404. */
const http_route_t *http_route_match_method(const http_route_t *routes, int count,
                                            const char *path, const char *method);

#ifdef __cplusplus
}
#endif
