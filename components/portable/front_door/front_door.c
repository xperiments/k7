/*
 * front_door - see include/front_door.h.
 *
 * `/api/status` answers the card state the App's views hang from, and the device's address
 * is a doorbell: it serves no page of its own (ADR 0067), so a browser that opens
 * `http://k7.local` is sent to the App. Everything else that is not one of the device's own
 * names - /library, /stream, the /api routes and /mcp - is a 404.
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

/* The App is the only UI. The device's root answers 302 here; a development build sets the
 * same string to the development origin (components/portable/front_door/Kconfig). The
 * fallback is what a host build, which has no sdkconfig, compiles. */
#ifndef CONFIG_FRONT_DOOR_LAUNCH_URL
#define CONFIG_FRONT_DOOR_LAUNCH_URL "https://k7.xperiments.dev/#/player"
#endif

static const char *TAG = "front_door";

static front_door_cfg_t F;

/* One allocation instead of seven stack buffers (see the route's comment). */
typedef struct {
    mini_icecast_status_t st;
    char body[1536];
    char title[200];
    char path[200];
    char playlist_name[200];
    char raw_name[96];
    char playlist_json[320];
    char mark_raw[44];
    char mark_escaped[80];
    char mark_json[84];
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

    /* The card's own name, when it carries one. Read once by the callback, not on this
     * route: a status poll must not become a card read. */
    if (F.card_mark && F.card_mark(b->mark_raw, sizeof b->mark_raw)) {
        http_json_escape(b->mark_escaped, sizeof b->mark_escaped, b->mark_raw);
        snprintf(b->mark_json, sizeof b->mark_json, "\"%s\"", b->mark_escaped);
    } else {
        snprintf(b->mark_json, sizeof b->mark_json, "null");
    }

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
                     "{\"card\":%s,\"mark\":%s,\"tracks\":%u,\"generation\":%u,\"library\":\"%s\","
                     "\"order\":\"%s\",\"playlist\":%s,"
                     "\"playing\":{\"title\":\"%s\",\"path\":\"%s\",\"elapsed_ms\":%u,"
                     "\"duration_ms\":%u}}",
                     card ? "true" : "false", b->mark_json, tracks, generation, state,
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

/* The device's address is a doorbell. A browser that types it, or follows an old bookmark
 * to the page the device used to serve, is sent to the App - which is the only UI (ADR
 * 0067). Only exact names ring: everything else stays a 404, which is also what keeps an
 * unknown GET under /api/ a 404 rather than the 405 the OPTIONS route would otherwise
 * cause. */
static http_result_t root_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    static const char *const doorbell[] = {"/", "/index.html", "/bridge", "/bridge/",
                                           "/app", "/app/"};
    bool ring = false;
    for (size_t i = 0; i < sizeof doorbell / sizeof doorbell[0]; ++i) {
        if (strcmp(req->path, doorbell[i]) == 0) {
            ring = true;
            break;
        }
    }

    if (!ring) {
        ESP_LOGI(TAG, "GET %s -> 404", req->path);
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }

    char headers[256];
    snprintf(headers, sizeof headers, "Location: %s\r\nCache-Control: no-store\r\n",
             CONFIG_FRONT_DOOR_LAUNCH_URL);
    ESP_LOGI(TAG, "GET %s -> 302 %s", req->path, CONFIG_FRONT_DOOR_LAUNCH_URL);
    http_send_headers(fd, 302, "text/plain", 0, headers);
    return HTTP_HANDLED;
}

esp_err_t front_door_start(const front_door_cfg_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    F = *cfg;

    esp_err_t e = http_server_register_route("/api/status", status_route, NULL);
    if (e != ESP_OK) return e;
    e = http_server_register_route("/", root_route, NULL);
    if (e != ESP_OK) return e;
    ESP_LOGI(TAG, "/api/status is up; the root sends a browser to %s",
             CONFIG_FRONT_DOOR_LAUNCH_URL);
    return ESP_OK;
}
