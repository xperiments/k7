/*
 * http_parse - request-line parsing and route matching.
 *
 * Nothing here allocates, logs, reads or writes a socket. That is the point: the
 * connection worker keeps the I/O, and every rule about what a request means is
 * reachable from a unit test.
 */
#include "http_parse.h"

#include <stdint.h>
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

/* Whether one header's value contains `token`, case-insensitively and stopping at the end
 * of that header's line: `find_header` returns a pointer into the whole block, so an
 * unbounded search would match a token in a later header. A `;q=0` list is not honoured -
 * every client that matters offers gzip either way or offers nothing. */
static bool header_line_has(const char *value, const char *token)
{
    size_t len = strcspn(value, "\r\n");
    size_t tlen = strlen(token);
    for (size_t i = 0; i + tlen <= len; i++) {
        if (strncasecmp(value + i, token, tlen) == 0) return true;
    }
    return false;
}

int http_split_target(const char *target, char *path, size_t path_size,
                      char *query, size_t query_size)
{
    if (!target || !path || !query || path_size == 0 || query_size == 0) return 400;
    path[0] = 0;
    query[0] = 0;

    /* Absolute-form request target: legal in HTTP/1.0 (RFC 1945) and HTTP/1.1
     * (RFC 7230 section 5.3.2), and what a proxy or a hand-typed
     * "GET http://host:8000/stream" sends. The device's TLS backend needs the
     * same rule: httpd hands over the target as received. */
    if (target[0] != '/') {
        const char *scheme = strstr(target, "://");
        if (!scheme) return 400;
        const char *slash = strchr(scheme + 3, '/');
        if (!slash) return 400;
        target = slash;
    }
    /* The query is kept, not dropped: a route that takes parameters reads
     * it from req->query, still percent-encoded like the path, and the caller
     * decodes what it needs. An over-long query is a 414 for the same reason an
     * over-long path is. */
    const char *q = strchr(target, '?');
    size_t plen = q ? (size_t)(q - target) : strlen(target);
    if (q) {
        size_t qlen = strlen(q + 1);
        if (qlen >= query_size) return 414;
        memcpy(query, q + 1, qlen + 1);
    }
    if (plen >= path_size) return 414;
    memcpy(path, target, plen);
    path[plen] = 0;
    return 0;
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

    int bad = http_split_target(target, req->path, sizeof req->path,
                                req->query, sizeof req->query);
    if (bad) return bad;

    const char *icy = find_header(line_end + 2, "icy-metadata");
    req->want_icy_metadata = icy != NULL && icy[0] == '1';

    /* The front door packs its text files compressed only, so a client that cannot take
     * gzip is told 406 rather than handed bytes it will not decode. */
    const char *ae = find_header(line_end + 2, "accept-encoding");
    req->accepts_gzip = ae != NULL && header_line_has(ae, "gzip");

    const char *cl = find_header(line_end + 2, "content-length");
    if (cl) {
        size_t value = 0;
        const char *p = cl;
        if (*p < '0' || *p > '9') return 400;
        while (*p >= '0' && *p <= '9') {
            if (value > (SIZE_MAX - 9) / 10) return 400;
            value = value * 10 + (size_t)(*p - '0');
            p++;
        }
        if (*p != '\r' && *p != '\n') return 400;
        req->has_content_length = true;
        req->content_length = value;
    }

    const char *mpv = find_header(line_end + 2, "mcp-protocol-version");
    if (mpv) {
        size_t n = 0;
        while (mpv[n] && mpv[n] != '\r' && mpv[n] != '\n' && n + 1 < sizeof req->mcp_protocol_version) {
            req->mcp_protocol_version[n] = mpv[n];
            n++;
        }
        while (n && (req->mcp_protocol_version[n - 1] == ' ' || req->mcp_protocol_version[n - 1] == '\t'))
            n--;
        req->mcp_protocol_version[n] = 0;
    }

    const char *hostv = find_header(line_end + 2, "host");
    if (hostv) {
        size_t n = 0;
        while (hostv[n] && hostv[n] != '\r' && hostv[n] != '\n' && n + 1 < sizeof req->host) {
            req->host[n] = hostv[n];
            n++;
        }
        while (n && (req->host[n - 1] == ' ' || req->host[n - 1] == '\t')) n--;
        req->host[n] = 0;
    }
    const char *originv = find_header(line_end + 2, "origin");
    if (originv) {
        size_t n = 0;
        while (originv[n] && originv[n] != '\r' && originv[n] != '\n' && n + 1 < sizeof req->origin) {
            req->origin[n] = originv[n];
            n++;
        }
        while (n && (req->origin[n - 1] == ' ' || req->origin[n - 1] == '\t')) n--;
        req->origin[n] = 0;
    }

    const char *ct = find_header(line_end + 2, "content-type");
    if (ct) {
        size_t n = 0;
        while (ct[n] && ct[n] != '\r' && ct[n] != '\n' && ct[n] != ';' &&
               n + 1 < sizeof req->content_type) {
            char c = ct[n];
            if (c >= 'A' && c <= 'Z') c = (char)(c + ('a' - 'A'));
            req->content_type[n++] = c;
        }
        while (n && (req->content_type[n - 1] == ' ' || req->content_type[n - 1] == '\t')) n--;
        req->content_type[n] = 0;
    }
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

/* Does this listener answer this route? A build with one listener answers
 * everything it is given, whatever the route's tag says. */
static bool route_serves(const http_route_t *r, bool filter_by_listener, http_listener_t listener)
{
    return !filter_by_listener || r->listener == listener;
}

const http_route_t *http_route_lookup(const http_route_t *routes, int count,
                                      bool filter_by_listener, http_listener_t listener,
                                      const char *path, const char *method,
                                      http_route_find_t *how)
{
    if (how) *how = HTTP_ROUTE_NOT_FOUND;
    if (!routes || !path) return NULL;
    const char *want = method && method[0] ? method : "GET";
    const http_route_t *best = NULL;
    bool path_exists = false;
    for (int i = 0; i < count; i++) {
        const http_route_t *r = &routes[i];
        if (!r->prefix || r->plen == 0) continue;
        if (!route_serves(r, filter_by_listener, listener)) continue;
        if (strncmp(path, r->prefix, r->plen) != 0) continue;
        /* "/" is the index catch-all, but the index only serves the root; a
         * method mismatch on /nope is a 404, not a 405. */
        if (r->plen > 1 || strcmp(path, "/") == 0) path_exists = true;
        const char *have = r->method && r->method[0] ? r->method : "GET";
        if (strcasecmp(have, want) != 0) continue;
        if (!best || r->plen > best->plen) best = r;
    }
    if (best) {
        if (how) *how = HTTP_ROUTE_MATCHED;
        return best;
    }
    if (how) *how = path_exists ? HTTP_ROUTE_WRONG_METHOD : HTTP_ROUTE_NOT_FOUND;
    return NULL;
}

const http_route_t *http_route_match_method(const http_route_t *routes, int count,
                                            const char *path, const char *method)
{
    return http_route_lookup(routes, count, false, HTTP_LISTENER_TLS, path, method, NULL);
}
