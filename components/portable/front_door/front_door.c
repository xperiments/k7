/*
 * front_door - see include/front_door.h.
 *
 * Two routes and no state: the page's files come from a directory, and the one dynamic
 * answer is the card state. The filesystem calls are stdio, which on the device is the
 * mounted SPIFFS partition and on the host is a directory - the same code both ways.
 */
#include "front_door.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "http_server.h"
#include "library.h"
#include "mini_icecast.h"

static const char *TAG = "frontdoor";

static front_door_cfg_t F;

/* ------------------------------------------------------------------ files */

static const char *extension(const char *path)
{
    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
    if (!dot || (slash && dot < slash)) return "";
    return dot + 1;
}

static const char *content_type_for(const char *path)
{
    const char *ext = extension(path);
    if (strcmp(ext, "html") == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, "js") == 0 || strcmp(ext, "mjs") == 0) return "text/javascript; charset=utf-8";
    if (strcmp(ext, "css") == 0) return "text/css; charset=utf-8";
    if (strcmp(ext, "json") == 0 || strcmp(ext, "webmanifest") == 0)
        return "application/json; charset=utf-8";
    if (strcmp(ext, "wasm") == 0) return "application/wasm";
    if (strcmp(ext, "svg") == 0) return "image/svg+xml";
    if (strcmp(ext, "png") == 0) return "image/png";
    if (strcmp(ext, "ico") == 0) return "image/x-icon";
    if (strcmp(ext, "txt") == 0) return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

/* The rule the service worker depends on: the page and the worker are revalidated every
 * time, the hashed bundle never is. */
static const char *cache_control_for(const char *path)
{
    if (strcmp(path, "/sw.js") == 0) return "Cache-Control: no-cache\r\n";
    if (strncmp(path, "/assets/", 8) == 0)
        return "Cache-Control: public, max-age=31536000, immutable\r\n";
    return "Cache-Control: no-cache\r\n";
}

/* A request path is not a filesystem path: refuse anything that could walk out of the
 * root, and anything that is not absolute. */
static bool path_is_safe(const char *path)
{
    if (!path || path[0] != '/') return false;
    if (strstr(path, "..")) return false;
    if (strstr(path, "\\")) return false;
    return true;
}

/* `/` and any extensionless path are the page itself; the app routes in JavaScript. */
static const char *resolve(const char *path, char *out, size_t cap)
{
    const char *target = path;
    if (strcmp(path, "/") == 0 || extension(path)[0] == 0) target = "/index.html";
    if (snprintf(out, cap, "%s%s", F.root ? F.root : "", target) >= (int)cap) return NULL;
    return out;
}

/* `path` is what was asked for, which is what the cache rules are written against;
 * `file` is what it resolved to, which is what the content type comes from. */
static http_result_t serve_file(int fd, const char *path, const char *file)
{
    FILE *f = fopen(file, "rb");
    if (!f) {
        ESP_LOGI(TAG, "GET %s -> 404 (no %s)", path, file);
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        http_send_error(fd, 500, "cannot read the page");
        return HTTP_HANDLED;
    }
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        http_send_error(fd, 500, "cannot read the page");
        return HTTP_HANDLED;
    }

    char headers[96];
    snprintf(headers, sizeof headers, "%s", cache_control_for(path));
    esp_err_t e = http_send_headers(fd, 200, content_type_for(file), (size_t)size, headers);
    if (e != ESP_OK) {
        fclose(f);
        return HTTP_HANDLED;
    }

    /* Streamed, not buffered: the largest file is a few hundred kilobytes and the device
     * has better uses for that RAM. */
    char chunk[2048];
    size_t got;
    while ((got = fread(chunk, 1, sizeof chunk, f)) > 0) {
        if (!http_send_all(fd, chunk, got)) break;
    }
    fclose(f);
    return HTTP_HANDLED;
}

static void json_escape(char *out, size_t cap, const char *in)
{
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)in; p && *p && n + 7 < cap; p++) {
        switch (*p) {
        case '"': out[n++] = '\\'; out[n++] = '"'; break;
        case '\\': out[n++] = '\\'; out[n++] = '\\'; break;
        case '\n': out[n++] = '\\'; out[n++] = 'n'; break;
        case '\r': out[n++] = '\\'; out[n++] = 'r'; break;
        case '\t': out[n++] = '\\'; out[n++] = 't'; break;
        default:
            if (*p < 0x20) {
                n += (size_t)snprintf(out + n, cap - n, "\\u%04x", *p);
            } else {
                out[n++] = (char)*p;
            }
        }
    }
    out[n] = 0;
}

static http_result_t status_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (strcmp(req->method, "GET") != 0) {
        http_send_error(fd, 405, "only GET");
        return HTTP_HANDLED;
    }

    const library_t *lib = library_acquire();
    unsigned tracks = lib ? library_track_count(lib) : 0;
    unsigned generation = lib ? library_generation(lib) : 0;
    const char *state = lib ? library_state_name(library_state(lib)) : "none";

    mini_icecast_status_t st;
    mini_icecast_status(&st);

    char title[200];
    char path[200];
    json_escape(title, sizeof title, st.title);
    json_escape(path, sizeof path, st.path);

    bool card = F.card_present ? F.card_present(F.ctx) : false;

    char body[768];
    int n = snprintf(body, sizeof body,
                     "{\"card\":%s,\"tracks\":%u,\"generation\":%u,\"library\":\"%s\","
                     "\"playing\":{\"title\":\"%s\",\"path\":\"%s\",\"elapsed_ms\":%u,"
                     "\"duration_ms\":%u}}",
                     card ? "true" : "false", tracks, generation, state, title, path,
                     (unsigned)st.elapsed_ms, (unsigned)st.duration_ms);
    if (lib) library_release(lib);
    if (n < 0 || n >= (int)sizeof body) {
        http_send_error(fd, 500, "status too large");
        return HTTP_HANDLED;
    }

    if (http_send_headers(fd, 200, "application/json", (size_t)n,
                          "Cache-Control: no-store\r\n") == ESP_OK)
        http_send_all(fd, body, (size_t)n);
    return HTTP_HANDLED;
}

static http_result_t page_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (strcmp(req->method, "GET") != 0) {
        http_send_error(fd, 405, "only GET");
        return HTTP_HANDLED;
    }
    if (!F.root || !F.root[0]) {
        /* A firmware without a page still has an index and an API. Only the page's own
         * paths say what is missing; anything else is still a 404. */
        bool is_page = strcmp(req->path, "/") == 0;
        if (is_page)
            http_send_error(fd, 503, "this firmware carries no page; the index is at /library");
        else
            http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    if (!path_is_safe(req->path)) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    char file[520];
    if (!resolve(req->path, file, sizeof file)) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    return serve_file(fd, req->path, file);
}

esp_err_t front_door_start(const front_door_cfg_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    F = *cfg;

    esp_err_t e = http_server_register_route("/api/status", status_route, NULL);
    if (e != ESP_OK) return e;
    e = http_server_register_route("/", page_route, NULL);
    if (e != ESP_OK) return e;

    if (F.root && F.root[0])
        ESP_LOGI(TAG, "page: %s (and /api/status)", F.root);
    else
        ESP_LOGI(TAG, "page: none in this firmware; / answers 503 and /library stays");
    return ESP_OK;
}
