/*
 * http_server_idf - the device's transport: esp_https_server on 443, the plain
 * listener on 80.
 *
 * This file is compiled by the firmware's CMake and never by the host, which is
 * how the two tiers differ: the build picks the backend file, and nothing in the
 * shared sources branches on the target. It provides the five calls declared
 * under "a transport provides" in http_transport.h.
 *
 * Why the plain listener is http_loop.c and not a second httpd instance: /stream
 * returns HTTP_UPGRADED and the broadcast writes the socket from its own task for
 * the whole listen. httpd closes a connection as soon as its handler returns (and
 * sends its own reply if the handler does not), and lwIP has no dup() that would
 * let us keep a second descriptor alive, so a stream behind httpd would mean
 * holding an httpd worker and its socket slot for hours and blocking the handler
 * until the player left. The hand-written loop already had exactly the semantics
 * /stream needs, and the hardware players' bytes stay byte-for-byte what they
 * were. The TLS listener, where every route answers and closes, is httpd.
 *
 * One more thing httpd needs: the handlers here are the same ones the host runs,
 * written against http_request_t and a raw fd. So every TLS request is presented
 * to them as an http_request_t, and the `fd` it is handed is the request's own
 * socket (httpd_req_to_sockfd). http_send_headers()/http_send_all() find that fd
 * in the session table below and write through httpd_resp_send()/
 * httpd_resp_send_chunk() instead of to the socket, which is what puts the bytes
 * inside the TLS record layer where they belong.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>

#include "esp_http_server.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "http_parse.h"
#include "http_server.h"
#include "http_transport.h"
#include "lwip/sockets.h"
#include "tls_material.h"

#define TAG "http_tls"

/* Front door R1/R2: 443 carries the app, 80 carries the stream. Defaults live
 * here rather than being threaded through app_services. */
#define TLS_PORT 443
#define PLAIN_PORT 80
/* Where the plain listener sends everything that is not /stream. The mDNS name is
 * the canonical one; a request that carries a Host header is answered with that
 * host instead, so the LAN address keeps working. */
#define TLS_HOSTNAME "xtape.local"

/* Concurrent TLS requests httpd will carry. Each one costs ~40 KB while it is
 * open, which is why IDF's own SSL default is 4 and not 7. */
#define TLS_SOCKETS 4
/* Extra headers a request may add through http_send_headers(); httpd's own
 * max_resp_headers is 8, and the two this file adds itself take the rest. */
#define SESSION_EXTRA_MAX 6

/* One request being served by the TLS listener. The handlers only ever see an
 * int fd, so this table is how a reply finds its httpd_req_t again. */
typedef struct {
    int         fd;    /* 0 when free: a socket fd is never 0 on lwIP */
    httpd_req_t *req;
    char        status[40];       /* "200 OK", for httpd_resp_set_status */
    char        content_type[64]; /* a copy: the caller's may be a route's local */
    char        server[40];       /* a copy, for the same reason */
    char        extra[512];       /* a copy of the caller's header block, split in place */
    bool        headers_set;
    bool        started;          /* something is on the wire */
    bool        complete;         /* the whole body went out with Content-Length */
    bool        has_length;
    size_t      length;
} session_t;

static session_t         s_sessions[TLS_SOCKETS + 2];
static SemaphoreHandle_t s_lock;
static httpd_handle_t    s_tls;
/* Requests in flight on this listener. Almost always 0: /stream's sender calls
 * http_send_all() every audio packet, and it must not take a mutex to learn that
 * its socket is not a TLS request. */
static volatile int      s_active;

static const char *method_name(httpd_method_t method)
{
    switch (method) {
    case HTTP_GET:     return "GET";
    case HTTP_POST:    return "POST";
    case HTTP_PUT:     return "PUT";
    case HTTP_DELETE:  return "DELETE";
    case HTTP_PATCH:   return "PATCH";
    case HTTP_HEAD:    return "HEAD";
    case HTTP_OPTIONS: return "OPTIONS";
    default:           return "";
    }
}

static session_t *session_begin(int fd, httpd_req_t *req)
{
    if (!s_lock || fd <= 0) return NULL; /* 0 is "free", and -1 is httpd's failure */
    session_t *found = NULL;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (size_t i = 0; i < sizeof s_sessions / sizeof s_sessions[0]; i++) {
        if (s_sessions[i].fd) continue;
        session_t *s = &s_sessions[i];
        memset(s, 0, sizeof *s);
        s->fd = fd;
        s->req = req;
        s_active++;
        found = s;
        break;
    }
    xSemaphoreGive(s_lock);
    return found;
}

static void session_end(session_t *s)
{
    if (!s || !s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s->fd = 0;
    s->req = NULL;
    if (s_active > 0) s_active--;
    xSemaphoreGive(s_lock);
}

static session_t *session_find(int fd)
{
    if (!s_lock || fd <= 0) return NULL;
    session_t *found = NULL;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (size_t i = 0; i < sizeof s_sessions / sizeof s_sessions[0]; i++) {
        if (s_sessions[i].fd == fd) {
            found = &s_sessions[i];
            break;
        }
    }
    xSemaphoreGive(s_lock);
    return found;
}

/* ------------------------------------------------------- request translation */

/* A header value, or "" when it is absent or would not fit. The hand-written
 * parser truncates instead; a value this long is a client fault either way, and
 * the only two readers (Host, Origin) both refuse what they cannot see whole. */
static void header_value(httpd_req_t *r, const char *name, char *out, size_t out_size)
{
    out[0] = 0;
    if (!out_size) return;
    if (httpd_req_get_hdr_value_len(r, name) == 0) return;
    if (httpd_req_get_hdr_value_str(r, name, out, out_size) != ESP_OK) out[0] = 0;
}

static void header_value_lower(httpd_req_t *r, const char *name, char *out, size_t out_size)
{
    char tmp[96];
    header_value(r, name, tmp, sizeof tmp);
    size_t n = 0;
    for (; tmp[n] && tmp[n] != ';' && n + 1 < out_size; n++) {
        char c = tmp[n];
        out[n] = (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
    }
    while (n && (out[n - 1] == ' ' || out[n - 1] == '\t')) n--;
    out[n] = 0;
}

/* httpd hands over what the client sent, query and all. The path and the query
 * are split by the same rule the hand-written parser applies. */
static int fill_request(httpd_req_t *r, http_request_t *req)
{
    memset(req, 0, sizeof *req);
    snprintf(req->method, sizeof req->method, "%s", method_name((httpd_method_t)r->method));
    if (!req->method[0]) return 405;
    int bad = http_split_target(r->uri, req->path, sizeof req->path,
                                req->query, sizeof req->query);
    if (bad) return bad;

    header_value(r, "Host", req->host, sizeof req->host);
    header_value(r, "Origin", req->origin, sizeof req->origin);
    header_value(r, "MCP-Protocol-Version", req->mcp_protocol_version,
                 sizeof req->mcp_protocol_version);
    header_value_lower(r, "Content-Type", req->content_type, sizeof req->content_type);

    char icy[8];
    header_value(r, "Icy-MetaData", icy, sizeof icy);
    req->want_icy_metadata = icy[0] == '1';

    req->has_content_length = httpd_req_get_hdr_value_len(r, "Content-Length") > 0;
    req->content_length = r->content_len;
    return 0;
}

/* The body, read through httpd and NUL-terminated for the JSON parsers.
 *
 * A body that is refused (413, 415) is not drained here: httpd finishes reading
 * whatever the client declared after the handler returns (httpd_req_delete purges
 * ra->remaining_len), so the error reaches the client and the connection closes
 * cleanly. The hand-written loop has to do that for itself; this transport does
 * not. */
static int read_body(httpd_req_t *r, http_request_t *req, uint8_t **out)
{
    uint8_t *body = http_body_alloc(req->content_length);
    if (!body) return 500;
    size_t got = 0;
    while (got < req->content_length) {
        int n = httpd_req_recv(r, (char *)body + got, req->content_length - got);
        if (n <= 0) {
            free(body);
            return 400;
        }
        got += (size_t)n;
    }
    body[req->content_length] = 0;
    req->body = body;
    req->body_len = req->content_length;
    *out = body;
    return 0;
}

/* ------------------------------------------------------------ the TLS route */

/* End the response: a chunked body gets its terminator, a body that never arrived
 * gets an empty one, and everything else was already complete. */
static esp_err_t finish(session_t *s)
{
    if (s) {
        if (!s->complete) {
            if (!s->started && s->has_length && s->length == 0) {
                httpd_resp_send(s->req, NULL, 0); /* headers, Content-Length: 0 */
            } else {
                httpd_resp_send_chunk(s->req, NULL, 0); /* ends a chunked body */
            }
        }
        session_end(s);
    }
    return ESP_OK;
}

static esp_err_t tls_dispatch(httpd_req_t *r)
{
    int fd = httpd_req_to_sockfd(r);
    session_t *s = session_begin(fd, r);
    if (!s) {
        /* No slot to record the reply in, so this one response is written through
         * httpd directly. It is the same 503 the plain listener answers when its
         * workers are all busy. */
        ESP_LOGE(TAG, "no slot for socket %d: %u TLS requests already in flight", fd,
                 (unsigned)(sizeof s_sessions / sizeof s_sessions[0]));
        char body[256];
        int n = snprintf(body, sizeof body,
                         "<!doctype html><meta charset=\"utf-8\">"
                         "<title>503 Service Unavailable</title>"
                         "<h1>503 Service Unavailable</h1><p>too many concurrent requests</p>\n");
        if (n < 0) n = 0;
        if ((size_t)n >= sizeof body) n = (int)sizeof body - 1;
        httpd_resp_set_status(r, "503 Service Unavailable");
        httpd_resp_set_type(r, "text/html; charset=utf-8");
        httpd_resp_send(r, body, n);
        return ESP_OK;
    }

    http_request_t req;
    int bad = fill_request(r, &req);
    if (bad) {
        ESP_LOGW(TAG, "request refused -> %d", bad);
        http_send_error(fd, bad, bad == 405 ? "that method is not served for this path"
                                           : "malformed request line");
        return finish(s);
    }

    http_route_find_t how;
    const http_route_t *route = http_find_route(false, HTTP_LISTENER_TLS, req.path, req.method, &how);
    if (!route) {
        int status = how == HTTP_ROUTE_WRONG_METHOD ? 405 : 404;
        ESP_LOGI(TAG, "%s %s -> %d", req.method, req.path, status);
        http_send_error(fd, status, status == 405 ? "that method is not served for this path"
                                                  : "no such path");
        return finish(s);
    }

    uint8_t *body = NULL;
    const char *detail = NULL;
    int post = http_post_rules(&req, route->method, &detail);
    if (post) {
        ESP_LOGI(TAG, "POST %s -> %d (%u bytes)", req.path, post, (unsigned)req.content_length);
        http_route_error(fd, route, post, detail);
        return finish(s);
    }
    if (strcasecmp(route->method && route->method[0] ? route->method : "GET", "POST") == 0) {
        int read = read_body(r, &req, &body);
        if (read) {
            ESP_LOGW(TAG, "POST %s -> %d (body)", req.path, read);
            http_route_error(fd, route, read, read == 500 ? "no memory for the request body"
                                                          : "the request body is incomplete");
            return finish(s);
        }
    }

    if (route->fn(fd, &req, route->ctx) == HTTP_UPGRADED) {
        /* Only a bare socket can be handed over, and this transport never owns
         * one. Say so rather than pretending the stream is being served. */
        ESP_LOGE(TAG, "%s %s asked to upgrade a TLS connection; the stream lives on "
                      "the plain listener", req.method, req.path);
    }
    free(body);
    return finish(s);
}

/* ------------------------------------------------------- a transport provides */

bool http_transport_owns(int fd) { return s_active != 0 && session_find(fd) != NULL; }

esp_err_t http_transport_reply(int fd, const http_reply_t *reply)
{
    session_t *s = session_find(fd);
    if (!s || !s->req) return ESP_ERR_NOT_FOUND;
    if (s->started) return ESP_ERR_INVALID_STATE; /* headers are already on the wire */

    snprintf(s->status, sizeof s->status, "%d %s", reply->status, reply->reason);
    snprintf(s->server, sizeof s->server, "%s",
             reply->server_name ? reply->server_name : "sd-stream/1.0");
    httpd_resp_set_status(s->req, s->status);
    if (reply->content_type) {
        snprintf(s->content_type, sizeof s->content_type, "%s", reply->content_type);
        httpd_resp_set_type(s->req, s->content_type);
    }
    httpd_resp_set_hdr(s->req, "Server", s->server);
    if (reply->default_cache_control) httpd_resp_set_hdr(s->req, "Cache-Control", "no-cache");

    /* The caller's block is only valid until http_send_headers() returns, and
     * httpd keeps pointers to whatever it is given until the headers go out, so
     * the block is copied here and split in place. */
    if (reply->extra_headers && reply->extra_headers[0]) {
        if (strlen(reply->extra_headers) >= sizeof s->extra)
            ESP_LOGW(TAG, "the response header block is too large (%u B); it is cut at %u B",
                     (unsigned)strlen(reply->extra_headers), (unsigned)sizeof s->extra - 1);
        snprintf(s->extra, sizeof s->extra, "%s", reply->extra_headers);
        char *line = s->extra;
        int count = 0;
        while (*line && count < SESSION_EXTRA_MAX) {
            char *crlf = strstr(line, "\r\n");
            if (!crlf) break; /* the block must end with CRLF; drop the rest if not */
            *crlf = 0;
            char *colon = strchr(line, ':');
            if (colon) {
                *colon = 0;
                char *value = colon + 1;
                while (*value == ' ' || *value == '\t') value++;
                if (httpd_resp_set_hdr(s->req, line, value) == ESP_OK) count++;
            }
            line = crlf + 2;
        }
        if (count == SESSION_EXTRA_MAX)
            ESP_LOGW(TAG, "more than %d extra headers; the rest are dropped", count);
    }

    s->has_length = reply->has_length;
    s->length = reply->length;
    s->headers_set = true;
    return ESP_OK;
}

bool http_transport_body(int fd, const void *data, size_t n)
{
    session_t *s = session_find(fd);
    if (!s || !s->req) return false;
    if (!n) return true;
    if (s->complete) return false; /* the declared body is already out */
    /* A body written in one piece with the length that was promised goes out with
     * a Content-Length; anything else is chunked, which is what lets a route
     * stream a file it knows the size of in 2 KB buffers. */
    if (!s->started && s->has_length && n == s->length) {
        s->started = s->complete = true;
        return httpd_resp_send(s->req, (const char *)data, (ssize_t)n) == ESP_OK;
    }
    s->started = true;
    return httpd_resp_send_chunk(s->req, (const char *)data, (ssize_t)n) == ESP_OK;
}

void http_transport_send_timeout(int fd, uint32_t ms)
{
    struct timeval snd = {.tv_sec = ms / 1000, .tv_usec = (ms % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &snd, sizeof snd);
}

/* -------------------------------------------------------------------- start */

/* One handler, one route table: every path on this listener is resolved by
 * http_server's table, so the routing rules are the same ones the host runs
 * (longest prefix, 405 versus 404, the POST body rules). Registering a handler
 * per route would fork those rules into httpd's matcher. */
static bool match_every_path(const char *registered, const char *uri, size_t uri_len)
{
    (void)registered;
    (void)uri;
    (void)uri_len;
    return true;
}

static esp_err_t start_tls(void)
{
    size_t cert_len = 0, key_len = 0;
    const uint8_t *cert = tls_material_cert(&cert_len);
    const uint8_t *key = tls_material_key(&key_len);
    if (!cert || !cert_len || !key || !key_len) {
        ESP_LOGE(TAG, "no TLS certificate in this image: the app is not served on %d; "
                      "the stream on %d is up", TLS_PORT, PLAIN_PORT);
        return ESP_ERR_NOT_FOUND;
    }

    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            ESP_LOGE(TAG, "no memory for the TLS session table; serving plain only");
            return ESP_ERR_NO_MEM;
        }
    }

    httpd_ssl_config_t sc = HTTPD_SSL_CONFIG_DEFAULT();
    sc.servercert = cert;
    sc.servercert_len = cert_len;
    sc.prvtkey_pem = key;
    sc.prvtkey_len = key_len;
    sc.transport_mode = HTTPD_SSL_TRANSPORT_SECURE;
    sc.port_secure = TLS_PORT; /* httpd_ssl_start takes the listening port from here */
    sc.httpd.max_open_sockets = TLS_SOCKETS;
    sc.httpd.max_uri_handlers = 1;
    sc.httpd.max_resp_headers = 8;
    sc.httpd.lru_purge_enable = true;
    sc.httpd.keep_alive_enable = false;
    /* One request per connection, and a stalled one dropped: the same two rules
     * the plain listener applies, in httpd's units (seconds there). */
    sc.httpd.recv_wait_timeout = (HTTP_HEADER_TIMEOUT_MS + 999) / 1000;
    sc.httpd.send_wait_timeout = (HTTP_CLIENT_STALL_MS + 999) / 1000;
    /* The TLS path is deeper than the plain one - mbedtls's handshake and record
     * layer sit under the same route handlers - so it gets the larger stack. */
    sc.httpd.stack_size = 12288;
    sc.httpd.core_id = 1;
    sc.httpd.uri_match_fn = match_every_path;

    esp_err_t err = httpd_ssl_start(&s_tls, &sc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TLS listener on %d refused to start: %s; serving plain only",
                 TLS_PORT, esp_err_to_name(err));
        return err;
    }

    httpd_uri_t any = {
        .uri = "/",
        .method = HTTP_ANY,
        .handler = tls_dispatch,
        .user_ctx = NULL,
    };
    err = httpd_register_uri_handler(s_tls, &any);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot register the TLS dispatcher: %s", esp_err_to_name(err));
        httpd_ssl_stop(s_tls);
        s_tls = NULL;
        return err;
    }
    ESP_LOGI(TAG, "serving the app on :%d (TLS, cert %s, %u B)", TLS_PORT,
             TLS_HOSTNAME, (unsigned)cert_len);
    return ESP_OK;
}

esp_err_t http_transport_start(const http_server_cfg_t *cfg)
{
    esp_err_t tls = start_tls();

    /* The plain listener is always started, certificate or not: the broadcast is
     * the one thing on this device that must never depend on TLS (front door
     * §6, "the certificate is missing or unreadable"). */
    http_server_cfg_t plain = *cfg;
    plain.port = PLAIN_PORT;
    plain.filter_by_listener = true;
    plain.listener = HTTP_LISTENER_PLAIN;
    plain.redirect_to_tls = true;
    plain.tls_host = TLS_HOSTNAME;
    plain.tls_port = TLS_PORT;
    esp_err_t stream = http_loop_start(&plain);
    if (stream != ESP_OK) {
        ESP_LOGE(TAG, "the plain listener on %d failed: %s", PLAIN_PORT,
                 esp_err_to_name(stream));
        return stream;
    }
    if (tls != ESP_OK) {
        /* Not an error for the caller: the device is on the network and the
         * broadcast is reachable, which is what must never depend on TLS. The log
         * above says why the app is missing. */
        ESP_LOGW(TAG, "the app is not served over TLS; http://%s/stream (%d) is up",
                 TLS_HOSTNAME, PLAIN_PORT);
    }
    return ESP_OK;
}
