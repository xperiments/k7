/*
 * front_door - see include/front_door.h.
 *
 * One route and no state: `/api/status` answers the card state the App's views hang from.
 * The device serves no page, so `/` is a 404 like every other name that is not one of the
 * device's own: /library, /stream, the /api routes and /mcp.
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

/* A GET that names no route is a 404, with no page to fall back to. The table already
 * answers OPTIONS under /api/ for the hosted App's preflight, so without this catch-all an
 * unknown GET there would be a 405: the path exists for one method, but not as a resource.
 * `/` and `/bridge` land here too. */
static http_result_t not_found_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "GET %s -> 404", req->path);
    http_send_error(fd, 404, "no such path");
    return HTTP_HANDLED;
}

esp_err_t front_door_start(const front_door_cfg_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    F = *cfg;

    esp_err_t e = http_server_register_route("/api/status", status_route, NULL);
    if (e != ESP_OK) return e;
    e = http_server_register_route("/", not_found_route, NULL);
    if (e != ESP_OK) return e;
    ESP_LOGI(TAG, "/api/status is up; the device serves no page");
    return ESP_OK;
}
