/*
 * http_loop - the hand-written accept/parse/route loop.
 *
 * This is the server the device has always had, kept because it is the only
 * transport that can hand a bare socket to another component: /stream takes the
 * connection (HTTP_UPGRADED) and the broadcast writes it from its own task for
 * as long as the player listens. httpd closes a connection when its handler
 * returns, and lwIP has no dup() to hand it a second descriptor, so routing the
 * stream through httpd would mean holding an httpd worker and a TLS-sized socket
 * slot for hours. The stream stays here instead, on the plain listener, exactly
 * as it was before TLS existed.
 *
 * On the host this file is the whole server. On the device it is the plain one,
 * started by idf/http_server_idf.c beside the TLS listener; it answers /stream
 * and redirects every other path to HTTPS (front door R2).
 */
#include "http_transport.h"

#include <errno.h>
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
#include "http_server.h"
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
    /* Connection workers take accepted sockets off this ring. There is one worker
     * per slot, created once, so no task is created or destroyed per request. */
    int               queue[HTTP_MAX_CONNS];
    unsigned          q_head, q_tail;
    SemaphoreHandle_t jobs;       /* connections waiting for a worker */
    SemaphoreHandle_t free_slots; /* workers not currently serving */
} L;

/* Discard a rejected POST body that is already on the socket. Closing with
 * unread bytes sends RST and can destroy the error response, so the cap branch
 * reads what the client sent before closing. The cap keeps a hostile declared
 * length from pinning the worker. */
static void discard_body(int fd, size_t len)
{
    if (len > 65536) len = 65536;
    char tmp[256];
    while (len) {
        size_t want = len < sizeof tmp ? len : sizeof tmp;
        int r = recv(fd, tmp, want, 0);
        if (r <= 0) break;
        len -= (size_t)r;
    }
}

/* ------------------------------------------------------------- plain -> TLS */

/* The Host header with any port removed: a redirect built from "xtape.local:80"
 * would send the client back to plain port 80. */
static void host_without_port(const char *host, char *out, size_t out_size)
{
    size_t n = strlen(host);
    const char *port = NULL;
    if (host[0] == '[') {                       /* an IPv6 literal keeps its colons */
        const char *close = strchr(host, ']');
        if (close && close[1] == ':') port = close + 1;
    } else {
        const char *colon = strrchr(host, ':');
        if (colon && colon[1]) port = colon;
    }
    if (port) {
        bool digits = port[1] != 0;
        for (const char *p = port + 1; *p; p++)
            if (*p < '0' || *p > '9') digits = false;
        if (digits) n = (size_t)(port - host);
    }
    if (n >= out_size) n = out_size - 1;
    memcpy(out, host, n);
    out[n] = 0;
}

/* 301 to the same path on HTTPS (front door R2: port 80 carries the stream and
 * nothing else, and no TLS ever).
 *
 * Written here rather than through http_send_headers because a Location can be
 * as long as the request target it came from (511 path + 255 query + a host),
 * and that header block is sized for an ordinary reply. Nothing else about this
 * response is configurable - no type, no body, no cache claim - so there is one
 * rule here and not two. */
static void send_https_redirect(int fd, const http_request_t *req)
{
    char host[128];
    host_without_port(req->host[0] ? req->host : (L.cfg.tls_host ? L.cfg.tls_host : ""),
                      host, sizeof host);
    if (!host[0]) { /* no Host and no configured name: nothing honest to send */
        http_send_error(fd, 400, "redirecting to HTTPS needs a Host header");
        return;
    }

    char location[1024];
    int n;
    if (L.cfg.tls_port && L.cfg.tls_port != 443) {
        n = snprintf(location, sizeof location, "https://%s:%u%s%s%s", host,
                     (unsigned)L.cfg.tls_port, req->path,
                     req->query[0] ? "?" : "", req->query);
    } else {
        n = snprintf(location, sizeof location, "https://%s%s%s%s", host, req->path,
                     req->query[0] ? "?" : "", req->query);
    }
    if (n < 0 || (size_t)n >= sizeof location) {
        http_send_error(fd, 414, "the path is too long to redirect");
        return;
    }

    char h[1100];
    int m = snprintf(h, sizeof h,
                     "HTTP/1.0 301 %s\r\nLocation: %s\r\nContent-Length: 0\r\n"
                     "Connection: close\r\n\r\n",
                     http_reason_phrase(301), location);
    if (m < 0 || (size_t)m >= sizeof h) {
        http_send_error(fd, 500, "the redirect response is too large");
        return;
    }
    ESP_LOGI(TAG, "%s %s -> 301 https", req->method, req->path);
    http_send_all(fd, h, (size_t)m);
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
    char *header_end = strstr(buf, "\r\n\r\n");
    if (!header_end) {
        ESP_LOGW(TAG, "request header too large (%u bytes)", (unsigned)n);
        http_send_error(fd, 400, "request header too large or incomplete");
        close(fd);
        return;
    }
    size_t header_len = (size_t)(header_end - buf) + 4;
    size_t body_in_buf = n > header_len ? n - header_len : 0;

    http_request_t req;
    memset(&req, 0, sizeof req);
    int status = http_parse_request(buf, &req);
    if (status) {
        ESP_LOGW(TAG, "malformed request -> %d", status);
        http_send_error(fd, status, "malformed request line");
        close(fd);
        return;
    }

    http_route_find_t how;
    const http_route_t *r = http_find_route(L.cfg.filter_by_listener, L.cfg.listener,
                                            req.path, req.method, &how);
    if (!r) {
        /* Nothing this listener serves. Read off a POST body before answering, so
         * closing does not send RST over an unread body and destroy the answer -
         * whichever answer it is. */
        if (strcasecmp(req.method, "POST") == 0 && req.has_content_length) {
            discard_body(fd, req.content_length > body_in_buf ? req.content_length - body_in_buf : 0);
        }
        if (L.cfg.redirect_to_tls) {
            send_https_redirect(fd, &req);
            close(fd);
            return;
        }
        if (how == HTTP_ROUTE_WRONG_METHOD) {
            ESP_LOGI(TAG, "%s %s -> 405", req.method, req.path);
            http_send_error(fd, 405, "that method is not served for this path");
        } else {
            ESP_LOGI(TAG, "%s %s -> 404", req.method, req.path);
            http_send_error(fd, 404, "no such path");
        }
        close(fd);
        return;
    }

    uint8_t *body = NULL;
    const char *detail = NULL;
    int bad = http_post_rules(&req, r->method, &detail);
    if (bad) {
        ESP_LOGI(TAG, "POST %s -> %d (%u bytes)", req.path, bad, (unsigned)req.content_length);
        http_route_error(fd, r, bad, detail);
        if (bad == 413 || bad == 415)
            discard_body(fd, req.content_length > body_in_buf ? req.content_length - body_in_buf : 0);
        close(fd);
        return;
    }
    if (strcasecmp(r->method && r->method[0] ? r->method : "GET", "POST") == 0) {
        body = http_body_alloc(req.content_length);
        if (!body) {
            http_route_error(fd, r, 500, "no memory for the request body");
            close(fd);
            return;
        }
        size_t copied = body_in_buf < req.content_length ? body_in_buf : req.content_length;
        if (copied) memcpy(body, buf + header_len, copied);
        size_t got = copied;
        while (got < req.content_length) {
            int r2 = recv(fd, body + got, req.content_length - got, 0);
            if (r2 <= 0) {
                ESP_LOGW(TAG, "POST %s -> 400 (short body %u/%u)", req.path, (unsigned)got,
                         (unsigned)req.content_length);
                http_route_error(fd, r, 400, "the request body is incomplete");
                free(body);
                close(fd);
                return;
            }
            got += (size_t)r2;
        }
        body[req.content_length] = 0;
        req.body = body;
        req.body_len = req.content_length;
    }

    if (r->fn(fd, &req, r->ctx) != HTTP_UPGRADED) close(fd);
    free(body);
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
        if (xSemaphoreTake(L.jobs, portMAX_DELAY) != pdTRUE) continue;
        int fd = L.queue[L.q_head];
        L.q_head = (L.q_head + 1) % HTTP_MAX_CONNS;
        serve_connection(fd);
        /* How close this worker came to its stack limit, as a running minimum, so
         * the periodic telemetry reports the worst path taken. */
        uint32_t left = (uint32_t)uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t);
        if (!L.conn_stack_min || left < L.conn_stack_min) L.conn_stack_min = left;
        xSemaphoreGive(L.free_slots);
    }
}

uint32_t http_server_conn_stack_headroom_bytes(void) { return L.conn_stack_min; }

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
        .sin_port = htons(L.cfg.port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(ls, (struct sockaddr *)&a, sizeof a) || listen(ls, HTTP_LISTEN_BACKLOG)) {
        ESP_LOGE(TAG, "bind/listen failed on port %u: %s", (unsigned)L.cfg.port, strerror(errno));
        close(ls);
        vTaskDeleteWithCaps(NULL);
        return;
    }
    ESP_LOGI(TAG, "listening on :%u (backlog %d, max %u at once)%s",
             (unsigned)L.cfg.port, HTTP_LISTEN_BACKLOG, (unsigned)L.cfg.max_conns,
             L.cfg.redirect_to_tls ? ", everything but /stream redirects to HTTPS" : "");

    for (;;) {
        int fd = accept(ls, NULL, NULL);
        if (fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(50)); /* accept() can fail transiently */
            continue;
        }
        /* A free slot is taken before the socket is queued, so the ring cannot
         * overflow: the number of queued-but-unserved connections is bounded by
         * the number of workers. */
        if (xSemaphoreTake(L.free_slots, 0) != pdTRUE) {
            ESP_LOGW(TAG, "connection declined: %u requests already in flight",
                     (unsigned)L.cfg.max_conns);
            http_send_error(fd, 503, "too many concurrent requests");
            close(fd);
            continue;
        }
        L.queue[L.q_tail] = fd;
        L.q_tail = (L.q_tail + 1) % HTTP_MAX_CONNS;
        xSemaphoreGive(L.jobs);
    }
}

esp_err_t http_loop_start(const http_server_cfg_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    L.cfg = *cfg;
    if (!L.cfg.port) L.cfg.port = 8000;
    if (!L.cfg.server_name) L.cfg.server_name = "sd-stream/1.0";
    if (!L.cfg.max_conns) L.cfg.max_conns = 4;
    if (L.cfg.max_conns > HTTP_MAX_CONNS) L.cfg.max_conns = HTTP_MAX_CONNS;

    L.jobs = xSemaphoreCreateCounting(L.cfg.max_conns, 0);
    L.free_slots = xSemaphoreCreateCounting(L.cfg.max_conns, L.cfg.max_conns);
    if (!L.jobs || !L.free_slots) return ESP_ERR_NO_MEM;

    /* The workers are created before the listening socket, so a request can never arrive
     * before there is something to serve it. If the pool cannot be created, this
     * fails here, at startup, instead of dropping requests later. */
    for (unsigned i = 0; i < L.cfg.max_conns; i++) {
        if (create_task(conn_worker, "http_conn", CONN_TASK_STACK, NULL, 5, 1) != pdPASS) {
            ESP_LOGE(TAG, "no memory for connection worker %u of %u", i + 1, (unsigned)L.cfg.max_conns);
            return ESP_ERR_NO_MEM;
        }
    }
    if (create_task(accept_task, "http_accept", ACCEPT_TASK_STACK, NULL, 5, 1) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}
