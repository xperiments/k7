#include "http_server.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "http_parse.h"
#include "lwip/sockets.h"

#define TAG "http"

/* Stack budget for one connection task, which is the deepest path in the
 * firmware: serve_connection's request buffer (1 KB) + the route's own locals
 * (the index's directory and error buffers) + http_send_headers' header block +
 * the index's 1 KB streaming writer + lwIP's send() and ESP_LOG's vsnprintf.
 * Measured on the device: 6 KB overflowed while serving the index (detected as
 * "A stack overflow in task http_conn"), 10 KB leaves a comfortable margin, and
 * the margin is reported with the rest of the telemetry. */
#define CONN_TASK_STACK 10240
#define ACCEPT_TASK_STACK 6144

static struct {
    http_server_cfg_t cfg;
    uint32_t          conn_stack_min; /* bytes of headroom left, worst connection seen */
    http_route_t      routes[HTTP_MAX_ROUTES];
    int               nroutes;
    /* Connection workers take accepted sockets off this ring. There is one worker
     * per slot, created once, so no task is created or destroyed per request. */
    int               queue[HTTP_MAX_CONNS];
    unsigned          q_head, q_tail;
    SemaphoreHandle_t jobs;       /* connections waiting for a worker */
    SemaphoreHandle_t free_slots; /* workers not currently serving */
} S;

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
        }
        if (n + 1 >= out_size) return (size_t)-1; /* refuse rather than truncate */
        out[n++] = (char)c;
    }
    out[n] = 0;
    return n;
}

/* ------------------------------------------------------------------ routing */

esp_err_t http_server_register_route(const char *prefix, http_route_fn fn, void *ctx)
{
    if (!prefix || !fn || prefix[0] != '/') return ESP_ERR_INVALID_ARG;
    if (S.nroutes >= HTTP_MAX_ROUTES) return ESP_ERR_NO_MEM;
    http_route_t *r = &S.routes[S.nroutes++];
    r->prefix = prefix;
    r->plen = strlen(prefix);
    r->fn = fn;
    r->ctx = ctx;
    return ESP_OK;
}

/* --------------------------------------------------------------- connection */

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

void http_send_timeout_ms(int fd, uint32_t ms)
{
    struct timeval snd = {.tv_sec = ms / 1000, .tv_usec = (ms % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &snd, sizeof snd);
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
    int status = http_parse_request(buf, &req);
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

    const http_route_t *r = http_route_match(S.routes, S.nroutes, req.path);
    if (!r) {
        ESP_LOGI(TAG, "GET %s -> 404", req.path);
        http_send_error(fd, 404, "no such path");
        close(fd);
        return;
    }
    if (r->fn(fd, &req, r->ctx) != HTTP_UPGRADED) close(fd);
}

/* Start a long-lived task, with its stack in external memory where possible.
 *
 * Two things are deliberate here. The stack is external-preferred because internal
 * RAM is the scarce resource in this firmware - the encoder's 95 KB of state lives
 * there on purpose - and an internal-only 10 KB stack failed under pressure
 * (measured: "no memory for a connection task", with the client seeing nothing at
 * all). And every task started here is *long-lived*: created once at startup and
 * never deleted, which is why nothing in this component calls vTaskDelete().
 *
 * The reason for that is worth recording. xTaskCreatePinnedToCoreWithCaps()
 * allocates its stack with heap_caps_malloc(), so it must be deleted with
 * vTaskDeleteWithCaps() rather than vTaskDelete() - the latter leaks the whole
 * stack, which for a connection task was 10 KB per request, a leak that only showed
 * up as a device degrading over repeated use. But the matching call has its own
 * cost: on ESP-IDF 6.1, deleting a capability-allocated task *from itself* spawns a
 * temporary cleanup task, and aborts the device if that allocation fails. Neither a
 * leak nor an abort-on-pressure is acceptable for something that happens once per
 * HTTP request, so the workers are permanent and there is nothing to delete. */
static BaseType_t create_task(TaskFunction_t fn, const char *name, uint32_t stack,
                              void *arg, UBaseType_t prio, int core)
{
    uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) == 0) {
        ESP_LOGW(TAG, "no external memory: %s gets an internal stack", name);
        caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    }
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(fn, name, stack, arg, prio, NULL, core, caps);
    if (ok != pdPASS && caps != (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)) {
        ESP_LOGW(TAG, "no external memory for the %s task stack; using internal RAM", name);
        ok = xTaskCreatePinnedToCoreWithCaps(fn, name, stack, arg, prio, NULL, core,
                                             MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return ok;
}

/* One worker per slot, alive for the life of the process. */
static void conn_worker(void *arg)
{
    (void)arg;
    for (;;) {
        if (xSemaphoreTake(S.jobs, portMAX_DELAY) != pdTRUE) continue;
        int fd = S.queue[S.q_head];
        S.q_head = (S.q_head + 1) % HTTP_MAX_CONNS;
        serve_connection(fd);
        /* How close this worker came to its stack limit, as a running minimum, so
         * the periodic telemetry reports the worst path taken. */
        uint32_t left = (uint32_t)uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t);
        if (!S.conn_stack_min || left < S.conn_stack_min) S.conn_stack_min = left;
        xSemaphoreGive(S.free_slots);
    }
}

uint32_t http_server_conn_stack_headroom_bytes(void) { return S.conn_stack_min; }

static void accept_task(void *arg)
{
    (void)arg;
    int ls = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (ls < 0) {
        ESP_LOGE(TAG, "socket(): %s", strerror(errno));
        vTaskDeleteWithCaps(NULL); /* created with caps, so deleted with caps */
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
        vTaskDeleteWithCaps(NULL);
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
        /* A free slot is taken before the socket is queued, so the ring cannot
         * overflow: the number of queued-but-unserved connections is bounded by
         * the number of workers. */
        if (xSemaphoreTake(S.free_slots, 0) != pdTRUE) {
            ESP_LOGW(TAG, "connection declined: %u requests already in flight",
                     (unsigned)S.cfg.max_conns);
            http_send_error(fd, 503, "too many concurrent requests");
            close(fd);
            continue;
        }
        S.queue[S.q_tail] = fd;
        S.q_tail = (S.q_tail + 1) % HTTP_MAX_CONNS;
        xSemaphoreGive(S.jobs);
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

    S.jobs = xSemaphoreCreateCounting(S.cfg.max_conns, 0);
    S.free_slots = xSemaphoreCreateCounting(S.cfg.max_conns, S.cfg.max_conns);
    if (!S.jobs || !S.free_slots) return ESP_ERR_NO_MEM;

    /* The workers are created before the listening socket, so a request can never arrive
     * before there is something to serve it. If the pool cannot be created, this
     * fails here, at startup, instead of dropping requests later. */
    for (unsigned i = 0; i < S.cfg.max_conns; i++) {
        if (create_task(conn_worker, "http_conn", CONN_TASK_STACK, NULL, 5, 1) != pdPASS) {
            ESP_LOGE(TAG, "no memory for connection worker %u of %u", i + 1, (unsigned)S.cfg.max_conns);
            return ESP_ERR_NO_MEM;
        }
    }
    if (create_task(accept_task, "http_accept", ACCEPT_TASK_STACK, NULL, 5, 1) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}
