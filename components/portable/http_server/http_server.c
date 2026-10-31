/*
 * http_server - the front door's transport-agnostic half.
 *
 * Here live the things both tiers and both listeners must agree on: the route
 * table, the reply rules (one Cache-Control, the Server header, the error page),
 * the POST body rules, and the public API. The bytes are written by whichever
 * transport the build picked - see http_transport.h - and this file reaches it
 * through five calls, so a handler that writes `http_send_headers(fd, ...)` does
 * not know or care whether `fd` is a bare socket or a TLS request.
 */
#include "http_server.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "http_parse.h"
#include "http_transport.h"
#include "lwip/sockets.h"

#define TAG "http"

static struct {
    http_server_cfg_t cfg;
    http_route_t      routes[HTTP_MAX_ROUTES];
    int               nroutes;
} S;

const char *http_reason_phrase(int status)
{
    switch (status) {
    case 200: return "OK";
    case 202: return "Accepted";
    case 301: return "Moved Permanently";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 411: return "Length Required";
    case 413: return "Content Too Large";
    case 414: return "URI Too Long";
    case 415: return "Unsupported Media Type";
    case 500: return "Internal Server Error";
    case 503: return "Service Unavailable";
    default:  return "Error";
    }
}

/* A bounded string builder: never writes past the end and remembers failure. */
typedef struct {
    char  *buf;
    size_t cap;
    size_t n;
    bool   ok;
} hdr_t;

static void hdr_add(hdr_t *w, const char *fmt, ...)
{
    if (!w->ok || w->n >= w->cap) { w->ok = false; return; }
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(w->buf + w->n, w->cap - w->n, fmt, ap);
    va_end(ap);
    if (r < 0 || (size_t)r >= w->cap - w->n) { w->ok = false; return; }
    w->n += (size_t)r;
}

/* ------------------------------------------------------------- reply rules */

void http_plan_reply(http_reply_t *out, const char *server_name, int status,
                     const char *content_type, size_t body_len, const char *extra_headers)
{
    out->status = status;
    out->reason = http_reason_phrase(status);
    out->server_name = server_name ? server_name : "sd-stream/1.0";
    out->content_type = content_type;
    out->has_length = body_len != (size_t)-1;
    out->length = body_len;
    out->extra_headers = extra_headers;
    /* A Cache-Control in the extra block replaces the default rather than
     * joining it: two of them in one response is a contradiction. */
    out->default_cache_control = !extra_headers || !strstr(extra_headers, "Cache-Control:");
}

/* ------------------------------------------------------------------ replies */

bool http_send_all(int fd, const void *data, size_t n)
{
    if (http_transport_owns(fd)) return http_transport_body(fd, data, n);

    const uint8_t *p = data;
    while (n) {
        int r = send(fd, p, n, 0);
        if (r <= 0) return false; /* error, or SO_SNDTIMEO expired: client is gone */
        p += r;
        n -= (size_t)r;
    }
    return true;
}

esp_err_t http_send_headers(int fd, int status, const char *content_type,
                            size_t body_len, const char *extra_headers)
{
    http_reply_t reply;
    http_plan_reply(&reply, S.cfg.server_name, status, content_type, body_len, extra_headers);
    if (http_transport_owns(fd)) return http_transport_reply(fd, &reply);

    char h[512];
    hdr_t w = {h, sizeof h, 0, true};
    hdr_add(&w, "HTTP/1.0 %d %s\r\n", reply.status, reply.reason);
    hdr_add(&w, "Server: %s\r\n", reply.server_name);
    if (reply.content_type) hdr_add(&w, "Content-Type: %s\r\n", reply.content_type);
    if (reply.has_length) hdr_add(&w, "Content-Length: %u\r\n", (unsigned)reply.length);
    if (reply.default_cache_control) hdr_add(&w, "Cache-Control: no-cache\r\n");
    hdr_add(&w, "Connection: close\r\n");
    if (reply.extra_headers) hdr_add(&w, "%s", reply.extra_headers);
    hdr_add(&w, "\r\n");
    if (!w.ok) return ESP_ERR_INVALID_SIZE;
    return http_send_all(fd, h, w.n) ? ESP_OK : ESP_FAIL;
}

esp_err_t http_send_error(int fd, int status, const char *detail)
{
    char body[600];
    int n = snprintf(body, sizeof body,
                     "<!doctype html><meta charset=\"utf-8\"><title>%d %s</title>"
                     "<h1>%d %s</h1><p>%s</p>\n",
                     status, http_reason_phrase(status), status, http_reason_phrase(status),
                     detail ? detail : "");
    if (n < 0) return ESP_FAIL;
    if ((size_t)n >= sizeof body) n = (int)sizeof body - 1;
    http_send_headers(fd, status, "text/html; charset=utf-8", (size_t)n, NULL);
    return http_send_all(fd, body, (size_t)n) ? ESP_OK : ESP_FAIL;
}

void http_send_timeout_ms(int fd, uint32_t ms)
{
    if (http_transport_owns(fd)) {
        http_transport_send_timeout(fd, ms);
        return;
    }
    struct timeval snd = {.tv_sec = ms / 1000, .tv_usec = (ms % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &snd, sizeof snd);
}

size_t http_url_decode(const char *in, char *out, size_t out_size)
{
    size_t n = 0;
    for (const char *p = in; *p; p++) {
        int c = (unsigned char)*p;
        if (c == '%') {
            if (!p[1] || !p[2]) return (size_t)-1;
            int hi = (p[1] >= '0' && p[1] <= '9') ? p[1] - '0'
                   : (p[1] >= 'a' && p[1] <= 'f') ? p[1] - 'a' + 10
                   : (p[1] >= 'A' && p[1] <= 'F') ? p[1] - 'A' + 10 : -1;
            int lo = (p[2] >= '0' && p[2] <= '9') ? p[2] - '0'
                   : (p[2] >= 'a' && p[2] <= 'f') ? p[2] - 'a' + 10
                   : (p[2] >= 'A' && p[2] <= 'F') ? p[2] - 'A' + 10 : -1;
            if (hi < 0 || lo < 0) return (size_t)-1;
            c = (hi << 4) | lo;
            p += 2;
        }
        if (n + 1 >= out_size) return (size_t)-1; /* refuse rather than truncate */
        out[n++] = (char)c;
    }
    out[n] = 0;
    return n;
}

/* ------------------------------------------------------------------ routing */

esp_err_t http_server_register_route_method_ex_on(http_listener_t listener, const char *prefix,
                                                  const char *method, http_route_fn fn,
                                                  void *ctx, http_error_fn error_fn)
{
    if (!prefix || !fn || prefix[0] != '/') return ESP_ERR_INVALID_ARG;
    if (listener != HTTP_LISTENER_TLS && listener != HTTP_LISTENER_PLAIN)
        return ESP_ERR_INVALID_ARG;
    if (!method || !method[0]) method = "GET";
    if (S.nroutes >= HTTP_MAX_ROUTES) return ESP_ERR_NO_MEM;
    http_route_t *r = &S.routes[S.nroutes++];
    r->prefix = prefix;
    r->plen = strlen(prefix);
    r->method = method;
    r->fn = fn;
    r->ctx = ctx;
    r->error_fn = error_fn;
    r->listener = listener;
    return ESP_OK;
}

esp_err_t http_server_register_route_method_on(http_listener_t listener, const char *prefix,
                                               const char *method, http_route_fn fn, void *ctx)
{
    return http_server_register_route_method_ex_on(listener, prefix, method, fn, ctx, NULL);
}

esp_err_t http_server_register_route_on(http_listener_t listener, const char *prefix,
                                        http_route_fn fn, void *ctx)
{
    return http_server_register_route_method_on(listener, prefix, "GET", fn, ctx);
}

esp_err_t http_server_register_route_method_ex(const char *prefix, const char *method,
                                               http_route_fn fn, void *ctx,
                                               http_error_fn error_fn)
{
    return http_server_register_route_method_ex_on(HTTP_LISTENER_TLS, prefix, method, fn, ctx,
                                                   error_fn);
}

esp_err_t http_server_register_route_method(const char *prefix, const char *method,
                                            http_route_fn fn, void *ctx)
{
    return http_server_register_route_method_on(HTTP_LISTENER_TLS, prefix, method, fn, ctx);
}

esp_err_t http_server_register_route(const char *prefix, http_route_fn fn, void *ctx)
{
    return http_server_register_route_method_on(HTTP_LISTENER_TLS, prefix, "GET", fn, ctx);
}

const http_route_t *http_find_route(bool filter_by_listener, http_listener_t listener,
                                    const char *path, const char *method, http_route_find_t *how)
{
    return http_route_lookup(S.routes, S.nroutes, filter_by_listener, listener, path, method, how);
}

/* ------------------------------------------------------------------- bodies */

int http_post_rules(const http_request_t *req, const char *route_method, const char **detail)
{
    const char *m = route_method && route_method[0] ? route_method : "GET";
    if (strcasecmp(m, "POST") != 0) return 0;
    if (!req->has_content_length) {
        *detail = "Content-Length is required";
        return 411;
    }
    if (req->content_length > HTTP_BODY_MAX) {
        *detail = "the request body is too large";
        return 413;
    }
    if (strcmp(req->content_type, "application/json") != 0) {
        *detail = "the request body must be application/json";
        return 415;
    }
    return 0;
}

void http_route_error(int fd, const http_route_t *route, int status, const char *detail)
{
    if (route && route->error_fn) {
        route->error_fn(fd, status, detail, route->ctx);
    } else {
        http_send_error(fd, status, detail);
    }
}

uint8_t *http_body_alloc(size_t n)
{
    uint8_t *body = heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) body = malloc(n + 1);
    return body;
}

/* -------------------------------------------------------------------- start */

esp_err_t http_server_start(const http_server_cfg_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    S.cfg = *cfg;
    if (!S.cfg.port) S.cfg.port = 8000;
    if (!S.cfg.server_name) S.cfg.server_name = "sd-stream/1.0";
    if (!S.cfg.max_conns) S.cfg.max_conns = 4;
    if (S.cfg.max_conns > HTTP_MAX_CONNS) {
        ESP_LOGW(TAG, "max_conns %u exceeds the %d supported; using %d",
                 (unsigned)S.cfg.max_conns, HTTP_MAX_CONNS, HTTP_MAX_CONNS);
        S.cfg.max_conns = HTTP_MAX_CONNS;
    }
    if (S.nroutes == 0) ESP_LOGW(TAG, "no routes registered: every request will be a 404");
    return http_transport_start(&S.cfg);
}
