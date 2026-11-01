/*
 * front_door - see include/front_door.h.
 *
 * Two routes and no state: the page's files come from a directory, and the one dynamic
 * answer is the card state. The filesystem calls are stdio, which on the device is the
 * mounted SPIFFS partition and on the host is a directory - the same code both ways.
 */
#include "front_door.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "http_server.h"
#include "library.h"
#include "flash_job.h"
#include "mini_icecast.h"
#include "playlist_select.h"

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
    if (strcmp(ext, "pem") == 0) return "application/x-pem-file";
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

/* `/` is the page; everything else is a file with a real name. The app is one screen
 * with no client-side routes, so an unknown path is a 404 rather than index.html. */
static bool shell_file_for(const char *request_path, char *out, size_t cap)
{
    const char *target = strcmp(request_path, "/") == 0 ? "/index.html" : request_path;
    return snprintf(out, cap, "%s%s", F.shell_root ? F.shell_root : "", target) < (int)cap;
}

/* The Shell is packed pre-compressed: the build writes `assets/index-abc.js.gz` and drops
 * the original, so the device streams those bytes with `Content-Encoding: gzip` and the
 * browser inflates them. Nothing on the board decompresses anything - the saving is flash,
 * and the transfer gets smaller too.
 *
 * A file the pack kept plain - `index.html`, the icons, anything that did not shrink - is
 * served as it is. A client that did not offer gzip cannot be handed a compressed-only
 * file, and the page means nothing to it anyway: it gets 406 rather than a misleading 404.
 *
 * `request_path` is what the cache rules are written against; `file_path` is the logical
 * name, and the content type comes from it - never from the trailing `.gz`. */
static http_result_t serve_file(int fd, const char *request_path, const char *file_path,
                                int missing_status, bool accepts_gzip)
{
    char packed[560];
    bool has_packed = snprintf(packed, sizeof packed, "%s.gz", file_path) < (int)sizeof packed;

    FILE *f = NULL;
    bool compressed = false;
    if (has_packed && accepts_gzip) {
        f = fopen(packed, "rb");
        compressed = f != NULL;
    }
    if (!f) f = fopen(file_path, "rb");
    if (!f) {
        if (has_packed) {
            /* Only the compressed copy exists, and this client did not ask for it. */
            FILE *packed_only = fopen(packed, "rb");
            if (packed_only) {
                fclose(packed_only);
                ESP_LOGW(TAG, "GET %s -> 406 (packed gzip only)", request_path);
                http_send_error(fd, 406, "this file is packed gzip-compressed only");
                return HTTP_HANDLED;
            }
        }
        ESP_LOGW(TAG, "GET %s -> %d (no %s)", request_path, missing_status, file_path);
        http_send_error(fd, missing_status,
                        missing_status == 503 ? "the page is missing from this firmware"
                                              : "no such path");
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

    /* One Cache-Control, never two: the rule already says how the response is cached, and
     * `Vary` keeps a shared cache from handing a gzip body to a client that cannot read
     * it. 160 bytes covers the longest rule plus both extra lines. */
    char headers[160];
    snprintf(headers, sizeof headers, "%s%s%s", cache_control_for(request_path),
             compressed ? "Content-Encoding: gzip\r\n" : "", "Vary: Accept-Encoding\r\n");

    esp_err_t e = http_send_headers(fd, 200, content_type_for(file_path), (size_t)size,
                                    headers);
    if (e != ESP_OK) {
        fclose(f);
        return HTTP_HANDLED;
    }

    /* Streamed, not buffered: the largest file is a few hundred kilobytes and the device
     * has better uses for that RAM. The chunk is on the heap rather than the stack: this
     * runs on the flash writer, whose stack is internal and deliberately small, and 2 KB
     * of it is what overflowed it. */
    char *chunk = malloc(2048);
    if (!chunk) {
        fclose(f);
        http_send_error(fd, 500, "no memory for the page");
        return HTTP_HANDLED;
    }
    size_t got;
    while ((got = fread(chunk, 1, 2048, f)) > 0) {
        if (!http_send_all(fd, chunk, got)) break;
    }
    free(chunk);
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
    if (strcmp(req->path, "/api/status") != 0) {
        http_send_error(fd, 404, "no such path");
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

    bool card = F.card_present ? F.card_present() : false;

    /* The order the broadcast is walking: the library tree, or the Playlist the device
     * was told to walk. A fact like the card state -- the page reads it, never sets it. */
    uint64_t playlist = playlist_select_current();
    char playlist_id[17] = {0};
    char playlist_name[200] = {0};
    if (playlist) {
        snprintf(playlist_id, sizeof playlist_id, "%016llx", (unsigned long long)playlist);
        char raw_name[96] = {0};
        playlist_select_name(raw_name, sizeof raw_name);
        json_escape(playlist_name, sizeof playlist_name, raw_name);
    }

    char playlist_json[320];
    if (playlist)
        snprintf(playlist_json, sizeof playlist_json, "{\"id\":\"%s\",\"name\":\"%s\"}",
                 playlist_id, playlist_name);
    else
        snprintf(playlist_json, sizeof playlist_json, "null");

    char body[1536];
    int n = snprintf(body, sizeof body,
                     "{\"card\":%s,\"tracks\":%u,\"generation\":%u,\"library\":\"%s\","
                     "\"order\":\"%s\",\"playlist\":%s,"
                     "\"playing\":{\"title\":\"%s\",\"path\":\"%s\",\"elapsed_ms\":%u,"
                     "\"duration_ms\":%u}}",
                     card ? "true" : "false", tracks, generation, state,
                     playlist ? "list" : "tree", playlist_json, title, path,
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

/* Serving the Shell reads SPIFFS, and a SPIFFS read is a flash read: the HTTP worker's
 * stack is in PSRAM, so the read happens on the writer task (flash_job.h). */
typedef struct {
    int         fd;
    const char *request_path;
    const char *file;
    int         missing_status;
    bool        accepts_gzip;
} serve_job_t;

static bool serve_job(void *ctx)
{
    serve_job_t *job = (serve_job_t *)ctx;
    (void)serve_file(job->fd, job->request_path, job->file, job->missing_status,
                     job->accepts_gzip);
    return true;
}

static http_result_t page_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (!F.shell_root || !F.shell_root[0]) {
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
    /* The API is the device's, not the page's: an unregistered /api path is a 404,
     * never the HTML fallback. */
    if (strncmp(req->path, "/api/", 5) == 0) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }

    bool is_page = strcmp(req->path, "/") == 0;
    char file[520];
    if (!shell_file_for(req->path, file, sizeof file)) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    /* A page that was packed but is not on the partition is a device fault, so `/` says
     * so; any other missing file is a 404 like every other. The read happens on the
     * writer, where the stack is internal. 30 s: a large gzipped bundle to a slow client
     * must not be cut in half by the writer's own default. */
    serve_job_t job = {fd, req->path, file, is_page ? 503 : 404, req->accepts_gzip};
    if (!flash_job_run(serve_job, &job, 30000u)) {
        /* The writer did not take it: answer instead of leaving the client hanging. */
        http_send_error(fd, 503, "the page reader is busy");
    }
    return HTTP_HANDLED;
}

esp_err_t front_door_start(const front_door_cfg_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    F = *cfg;

    /* One listener, one page. */
    esp_err_t e = http_server_register_route("/api/status", status_route, NULL);
    if (e != ESP_OK) return e;
    e = http_server_register_route("/", page_route, NULL);
    if (e != ESP_OK) return e;

    if (F.shell_root && F.shell_root[0])
        ESP_LOGI(TAG, "page: %s (and /api/status)", F.shell_root);
    else
        ESP_LOGI(TAG, "page: none in this firmware; / answers 503 and /library stays");
    return ESP_OK;
}
