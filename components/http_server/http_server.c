#include "http_server.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#define TAG "http"

#define CONN_TASK_STACK 6144
#define ACCEPT_TASK_STACK 4096

typedef struct {
    const char  *prefix;
    size_t       plen;
    http_route_fn fn;
    void        *ctx;
} route_t;

typedef struct {
    int fd;
} conn_arg_t;

static struct {
    http_server_cfg_t cfg;
    route_t           routes[HTTP_MAX_ROUTES];
    int               nroutes;
    SemaphoreHandle_t slots; /* caps concurrently-parsing connection tasks */
} S;

/* ------------------------------------------------------------- text helpers */

/* Value of a request header, with leading whitespace skipped, or NULL. */
static const char *find_header(const char *req, const char *name)
{
    size_t nlen = strlen(name);
    const char *p = strstr(req, "\r\n");
    if (!p) return NULL;
    p += 2;
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

static const char *reason_phrase(int status)
{
    switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 414: return "URI Too Long";
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

/* ------------------------------------------------------------------ replies */

bool http_send_all(int fd, const void *data, size_t n)
{
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
    char h[512];
    hdr_t w = {h, sizeof h, 0, true};
    hdr_add(&w, "HTTP/1.0 %d %s\r\n", status, reason_phrase(status));
    hdr_add(&w, "Server: %s\r\n", S.cfg.server_name ? S.cfg.server_name : "sd-stream/1.0");
    if (content_type) hdr_add(&w, "Content-Type: %s\r\n", content_type);
    if (body_len != (size_t)-1) hdr_add(&w, "Content-Length: %u\r\n", (unsigned)body_len);
    hdr_add(&w, "Cache-Control: no-cache\r\nConnection: close\r\n");
    if (extra_headers) hdr_add(&w, "%s", extra_headers);
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
                     status, reason_phrase(status), status, reason_phrase(status),
                     detail ? detail : "");
    if (n < 0) return ESP_FAIL;
    if ((size_t)n >= sizeof body) n = (int)sizeof body - 1;
    http_send_headers(fd, status, "text/html; charset=utf-8", (size_t)n, NULL);
    return http_send_all(fd, body, (size_t)n) ? ESP_OK : ESP_FAIL;
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
        } else if (c == '+') {
            c = ' '; /* form encoding; harmless for the paths we serve */
        }
        if (n + 1 >= out_size) return (size_t)-1; /* refuse rather than truncate */
        out[n++] = (char)c;
    }
    out[n] = 0;
    return n;
}

/* ------------------------------------------------------------------ routing */

static route_t *match_route(const char *path)
{
    route_t *best = NULL;
    for (int i = 0; i < S.nroutes; i++) {
        route_t *r = &S.routes[i];
        if (strncmp(path, r->prefix, r->plen) != 0) continue;
        if (!best || r->plen > best->plen) best = r;
    }
    return best;
}

esp_err_t http_server_register_route(const char *prefix, http_route_fn fn, void *ctx)
{
    if (!prefix || !fn || prefix[0] != '/') return ESP_ERR_INVALID_ARG;
    if (S.nroutes >= HTTP_MAX_ROUTES) return ESP_ERR_NO_MEM;
    route_t *r = &S.routes[S.nroutes++];
    r->prefix = prefix;
    r->plen = strlen(prefix);
    r->fn = fn;
    r->ctx = ctx;
    return ESP_OK;
}

/* --------------------------------------------------------------- connection */

/* Returns 0 on success, or an HTTP status to answer with. */
static int parse_request(char *buf, http_request_t *req)
{
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

    /* Absolute-form request target (some clients use it for proxies). */
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

static void set_socket_options(int fd)
{
    struct timeval rcv = {.tv_sec = HTTP_HEADER_TIMEOUT_MS / 1000,
                          .tv_usec = (HTTP_HEADER_TIMEOUT_MS % 1000) * 1000};
    struct timeval snd = {.tv_sec = HTTP_CLIENT_STALL_MS / 1000,
                          .tv_usec = (HTTP_CLIENT_STALL_MS % 1000) * 1000};
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcv, sizeof rcv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &snd, sizeof snd);
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
}

/* Reads, parses and routes one request. Closes the fd unless a route took it. */
static void serve_connection(int fd)
{
    set_socket_options(fd);

    char buf[HTTP_HEADER_MAX];
    size_t n = 0;
    while (n < sizeof buf - 1) {
        int r = recv(fd, buf + n, sizeof buf - 1 - n, 0);
        if (r <= 0) break;
        n += (size_t)r;
        buf[n] = 0;
        if (strstr(buf, "\r\n\r\n")) break;
    }
    if (n == 0) { /* connected and said nothing: nothing to answer */
        close(fd);
        return;
    }
    if (!strstr(buf, "\r\n\r\n")) {
        ESP_LOGW(TAG, "request header too large (%u bytes)", (unsigned)n);
        http_send_error(fd, 400, "request header too large or incomplete");
        close(fd);
        return;
    }

    http_request_t req;
    memset(&req, 0, sizeof req);
    int status = parse_request(buf, &req);
    if (status) {
        ESP_LOGW(TAG, "malformed request -> %d", status);
        http_send_error(fd, status, "malformed request line");
        close(fd);
        return;
    }
    if (strcmp(req.method, "GET") != 0) {
        ESP_LOGI(TAG, "%s %s -> 405", req.method, req.path);
        http_send_error(fd, 405, "only GET is served");
        close(fd);
        return;
    }

    route_t *r = match_route(req.path);
    if (!r) {
        ESP_LOGI(TAG, "GET %s -> 404", req.path);
        http_send_error(fd, 404, "no such path");
        close(fd);
        return;
    }
    if (r->fn(fd, &req, r->ctx) != HTTP_UPGRADED) close(fd);
}

static void conn_task(void *arg)
{
    conn_arg_t *a = arg;
    int fd = a->fd;
    free(a);
    serve_connection(fd);
    xSemaphoreGive(S.slots);
    vTaskDelete(NULL);
}

static void accept_task(void *arg)
{
    (void)arg;
    int ls = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (ls < 0) {
        ESP_LOGE(TAG, "socket(): %s", strerror(errno));
        vTaskDelete(NULL);
        return;
    }
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a = {
        .sin_family = AF_INET,
        .sin_port = htons(S.cfg.port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(ls, (struct sockaddr *)&a, sizeof a) || listen(ls, HTTP_LISTEN_BACKLOG)) {
        ESP_LOGE(TAG, "bind/listen failed on port %u: %s", (unsigned)S.cfg.port, strerror(errno));
        close(ls);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "listening on :%u (backlog %d, max %u at once)",
             (unsigned)S.cfg.port, HTTP_LISTEN_BACKLOG, (unsigned)S.cfg.max_conns);

    for (;;) {
        int fd = accept(ls, NULL, NULL);
        if (fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(50)); /* accept() can fail transiently */
            continue;
        }
        if (xSemaphoreTake(S.slots, 0) != pdTRUE) {
            ESP_LOGW(TAG, "connection declined: %u requests already in flight",
                     (unsigned)S.cfg.max_conns);
            http_send_error(fd, 503, "too many concurrent requests");
            close(fd);
            continue;
        }
        conn_arg_t *ca = malloc(sizeof *ca);
        if (!ca) {
            close(fd);
            xSemaphoreGive(S.slots);
            continue;
        }
        ca->fd = fd;
        if (xTaskCreatePinnedToCore(conn_task, "http_conn", CONN_TASK_STACK, ca, 5, NULL, 1) != pdPASS) {
            ESP_LOGE(TAG, "no memory for a connection task");
            free(ca);
            close(fd);
            xSemaphoreGive(S.slots);
        }
    }
}

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

    S.slots = xSemaphoreCreateCounting(S.cfg.max_conns, S.cfg.max_conns);
    if (!S.slots) return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(accept_task, "http_accept", ACCEPT_TASK_STACK, NULL, 5, NULL, 1) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}
