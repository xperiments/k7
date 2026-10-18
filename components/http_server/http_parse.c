/*
 * http_parse - request-line parsing and route matching.
 *
 * Nothing here allocates, logs, reads or writes a socket. That is the point: the
 * connection worker keeps the I/O, and every rule about what a request means is
 * reachable from a unit test.
 */
#include "http_parse.h"

#include <string.h>
#include <strings.h>

/* Value of a request header, with leading whitespace skipped, or NULL.
 *
 * `req` is the header block that starts *after* the request line. It used to be
 * passed the same pointer but this function skipped its first line, so a client
 * whose first header was `Icy-MetaData` was not given metadata - the clients
 * that worked sent `Host` first. The test table now pins the first-header case. */
static const char *find_header(const char *req, const char *name)
{
    size_t nlen = strlen(name);
    const char *p = req;
    for (;;) {
        const char *eol = strstr(p, "\r\n");
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        if (len == 0) return NULL; /* end of headers */
        if (len > nlen && strncasecmp(p, name, nlen) == 0 && p[nlen] == ':') {
            const char *v = p + nlen + 1;
            while (v < p + len && (*v == ' ' || *v == '\t')) v++;
            return v;
        }
        if (!eol) return NULL;
        p = eol + 2;
    }
}

int http_parse_request(char *buf, http_request_t *req)
{
    if (!buf || !req) return 400;

    char *line_end = strstr(buf, "\r\n");
    if (!line_end) return 400;
    *line_end = 0;

    char *sp1 = strchr(buf, ' ');
    if (!sp1) return 400;
    *sp1 = 0;
    char *target = sp1 + 1;
    char *sp2 = strchr(target, ' ');
    if (sp2) *sp2 = 0;

    if (strlen(buf) == 0 || strlen(buf) >= sizeof req->method) return 400;
    strcpy(req->method, buf);

    /* Absolute-form request target: legal in HTTP/1.0 (RFC 1945) and HTTP/1.1
     * (RFC 7230 section 5.3.2), and what a proxy or a hand-typed
     * "GET http://host:8000/stream" sends. */
    if (target[0] != '/') {
        char *scheme = strstr(target, "://");
        if (!scheme) return 400;
        char *slash = strchr(scheme + 3, '/');
        if (!slash) return 400;
        target = slash;
    }
    char *query = strchr(target, '?');
    if (query) *query = 0;
    if (strlen(target) >= sizeof req->path) return 414;
    strcpy(req->path, target);

    const char *icy = find_header(line_end + 2, "icy-metadata");
    req->want_icy_metadata = icy != NULL && icy[0] == '1';
    return 0;
}

const http_route_t *http_route_match(const http_route_t *routes, int count, const char *path)
{
    if (!routes || !path) return NULL;
    const http_route_t *best = NULL;
    for (int i = 0; i < count; i++) {
        const http_route_t *r = &routes[i];
        if (!r->prefix || r->plen == 0) continue;
        if (strncmp(path, r->prefix, r->plen) != 0) continue;
        if (!best || r->plen > best->plen) best = r;
    }
    return best;
}
