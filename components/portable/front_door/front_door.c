/*
 * front_door - see include/front_door.h.
 *
 * Two routes and no state: the page's files come out of one packed image, and the one
 * dynamic answer is the card state. The image is resident - PSRAM on the board, a buffer on
 * the host - so nothing here reads flash at all, and nothing is buffered: a file is a pointer
 * and a length inside bytes that are already in memory.
 */
#include "front_door.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "http_server.h"
#include "library.h"
#include "mini_icecast.h"
#include "playlist_select.h"
#include "shell_image.h"

static const char *TAG = "frontdoor";

static front_door_cfg_t F;

/* The image, validated once in front_door_start(). `s_shell.count` is 0 until then, and
 * every path out of the page says "no page" rather than half-serving one. */
static shell_image_t s_shell;

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
static const char *logical_for(const char *request_path)
{
    return strcmp(request_path, "/") == 0 ? "/index.html" : request_path;
}

/* The Shell is packed pre-compressed: the build stores `assets/index-abc.js` as gzip and
 * drops the original, so the device sends those bytes with `Content-Encoding: gzip` and
 * the browser inflates them. Nothing on the board decompresses anything - the saving is
 * flash, and the transfer gets smaller too.
 *
 * A file the pack kept plain - `index.html`, the icons, anything that did not shrink - is
 * served as it is. A client that did not offer gzip cannot be handed a compressed-only
 * file, and the page means nothing to it anyway: it gets 406 rather than a misleading 404.
 *
 * `request_path` is what the cache rules are written against; the logical name is what the
 * image is keyed by, and the content type comes from it - never from a trailing `.gz`. */
static http_result_t serve_file(int fd, const char *request_path, int missing_status,
                                bool accepts_gzip)
{
    const char *logical = logical_for(request_path);
    shell_blob_t blob;
    shell_image_result_t found = shell_image_find(&s_shell, logical, accepts_gzip, &blob);

    if (found == SHELL_IMAGE_PACKED_ONLY) {
        ESP_LOGW(TAG, "GET %s -> 406 (packed gzip only)", request_path);
        http_send_error(fd, 406, "this file is packed gzip-compressed only");
        return HTTP_HANDLED;
    }
    if (found != SHELL_IMAGE_FOUND) {
        ESP_LOGW(TAG, "GET %s -> %d (not in the shell image)", request_path, missing_status);
        http_send_error(fd, missing_status,
                        missing_status == 503 ? "the page is missing from this firmware"
                                              : "no such path");
        return HTTP_HANDLED;
    }

    /* One Cache-Control, never two: the rule already says how the response is cached, and
     * `Vary` keeps a shared cache from handing a gzip body to a client that cannot read
     * it. 160 bytes covers the longest rule plus both extra lines. */
    char headers[160];
    snprintf(headers, sizeof headers, "%s%s%s", cache_control_for(request_path),
             blob.gzip ? "Content-Encoding: gzip\r\n" : "", "Vary: Accept-Encoding\r\n");

    esp_err_t e = http_send_headers(fd, 200, content_type_for(logical), blob.len, headers);
    if (e != ESP_OK) return HTTP_HANDLED;

    /* Nothing to read and nothing to buffer: the bytes are already in memory - mapped
     * flash on the board - so the response is one send loop over the file. This is the
     * whole point of the image: the old path did a 2 KB flash read per chunk, and every
     * one of them blanked the PSRAM the panel draws from. */
    http_send_all(fd, blob.data, blob.len);
    return HTTP_HANDLED;
}

/* One allocation instead of seven stack buffers (see the route's comment). */
typedef struct {
    mini_icecast_status_t st;
    char body[1536];
    char title[200];
    char path[200];
    char playlist_name[200];
    char raw_name[96];
    char playlist_json[320];
} status_buffers_t;

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

    /* Every buffer here is on the heap, not the stack: this route's frames plus the
     * broadcast status struct's own 1.4 KB of strings is what overflowed a connection
     * worker once the Playlist block joined them. One allocation, freed once. The fields
     * are members of one struct rather than pointers carved out of a char block because
     * the compiler cannot prove disjointness for pointers, and -Wrestrict is an error in
     * this build. */
    status_buffers_t *b = malloc(sizeof *b);
    if (!b) {
        if (lib) library_release(lib);
        http_send_error(fd, 500, "no memory for the status");
        return HTTP_HANDLED;
    }
    mini_icecast_status(&b->st);
    http_json_escape(b->title, sizeof b->title, b->st.title);
    http_json_escape(b->path, sizeof b->path, b->st.path);

    bool card = F.card_present ? F.card_present() : false;

    /* The choice and the walk are different facts: a card that is out keeps the choice
     * (it comes back with the card) while nothing is walking. */
    bool walking = playlist_select_walking();
    uint64_t playlist = walking ? playlist_select_current() : 0;
    if (playlist) {
        char id[17] = {0};
        snprintf(id, sizeof id, "%016llx", (unsigned long long)playlist);
        playlist_select_name(b->raw_name, sizeof b->raw_name);
        http_json_escape(b->playlist_name, sizeof b->playlist_name, b->raw_name);
        snprintf(b->playlist_json, sizeof b->playlist_json,
                 "{\"id\":\"%s\",\"name\":\"%s\"}", id, b->playlist_name);
    } else {
        snprintf(b->playlist_json, sizeof b->playlist_json, "null");
    }

    int n = snprintf(b->body, sizeof b->body,
                     "{\"card\":%s,\"tracks\":%u,\"generation\":%u,\"library\":\"%s\","
                     "\"order\":\"%s\",\"playlist\":%s,"
                     "\"playing\":{\"title\":\"%s\",\"path\":\"%s\",\"elapsed_ms\":%u,"
                     "\"duration_ms\":%u}}",
                     card ? "true" : "false", tracks, generation, state,
                     playlist ? "list" : "tree", b->playlist_json, b->title, b->path,
                     (unsigned)b->st.elapsed_ms, (unsigned)b->st.duration_ms);
    if (lib) library_release(lib);
    if (n < 0 || n >= (int)sizeof b->body) {
        free(b);
        http_send_error(fd, 500, "status too large");
        return HTTP_HANDLED;
    }

    if (http_send_headers(fd, 200, "application/json", (size_t)n,
                          "Cache-Control: no-store\r\n") == ESP_OK)
        http_send_all(fd, b->body, (size_t)n);
    free(b);
    return HTTP_HANDLED;
}

/* Every file is served from the bytes the board handed over, which are resident (PSRAM on
 * the board, a buffer on the host) - so a page load touches no flash at all, and this does
 * not need the one flash-capable task. That is a change from ADR 0054, and it is the point:
 * a flash read shares the bus with the panel's scan-out and tears the picture (ADR 0055). */
static http_result_t page_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (!F.shell_image || s_shell.count == 0) {
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

    /* A page that was packed but is not in the image is a device fault, so `/` says so;
     * any other missing file is a 404 like every other. */
    bool is_page = strcmp(req->path, "/") == 0;
    return serve_file(fd, req->path, is_page ? 503 : 404, req->accepts_gzip);
}

esp_err_t front_door_start(const front_door_cfg_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    F = *cfg;

    /* One check, once: every lookup afterwards trusts the table. An image that does not
     * validate is treated exactly like a firmware that carries no page. */
    if (F.shell_image && shell_image_open(&s_shell, F.shell_image, F.shell_image_size))
        ESP_LOGI(TAG, "page: %u files in the shell image (%u KiB), and /api/status",
                 (unsigned)s_shell.count, (unsigned)(s_shell.size >> 10));
    else {
        memset(&s_shell, 0, sizeof s_shell);
        if (F.shell_image)
            ESP_LOGW(TAG, "page: the shell image did not validate; / answers 503");
    }

    /* One listener, one page. */
    esp_err_t e = http_server_register_route("/api/status", status_route, NULL);
    if (e != ESP_OK) return e;
    e = http_server_register_route("/", page_route, NULL);
    if (e != ESP_OK) return e;

    if (s_shell.count == 0)
        ESP_LOGI(TAG, "page: none in this firmware; / answers 503 and /library stays");
    return ESP_OK;
}
