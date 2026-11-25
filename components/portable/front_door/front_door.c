/*
 * front_door - see include/front_door.h.
 *
 * `/api/status` answers the card state the App's views hang from, and the device's address
 * is a doorbell: it serves no page of its own (ADR 0067), so a browser that opens
 * `http://k7.local` is sent to the App. Everything else that is not one of the device's own
 * names - /library, /stream, the /api routes and /mcp - is a 404.
 */
#include "front_door.h"
#include "json.h"

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
#define CONFIG_FRONT_DOOR_LAUNCH_URL "https://k7.xperiments.dev/app#/player"
#endif

static const char *TAG = "front_door";

static front_door_cfg_t F;

/* Which deck this is: copied at start, so the route reads values rather than calling the
 * board while a connection waits. `id` is the station MAC as twelve hex characters. */
static char F_id[13];
static char F_fw[32];
static char F_board[24];

/* One allocation instead of the route's frames: the broadcast's own status struct carries
 * 1.4 KB of strings, and that plus this route is what overflowed a connection worker. The
 * body is built by a bounded writer, so the pieces no longer need buffers of their own. */
typedef struct {
    mini_icecast_status_t st;
    char body[1664];
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
    bool card = F.card_present ? F.card_present() : false;

    json_writer_t w = json_writer_make(b->body, sizeof b->body);
    json_puts(&w, "{\"card\":");
    json_puts(&w, card ? "true" : "false");
    json_puts(&w, ",\"mark\":");
    /* The card's own name, when it carries one. Read once by the callback, not on this
     * route: a status poll must not become a card read. */
    char mark[64];
    if (F.card_mark && F.card_mark(mark, sizeof mark)) {
        json_puts(&w, "\"");
        json_escaped(&w, mark);
        json_puts(&w, "\"");
    } else {
        json_puts(&w, "null");
    }
    /* Which deck, which firmware, which board. The App ignores them; a home automation
     * keys its entity on `id`, which names the deck where `mark` names the card. */
    if (F_id[0]) {
        json_puts(&w, ",\"id\":\"");
        json_escaped(&w, F_id);
        json_puts(&w, "\"");
    }
    if (F_fw[0]) {
        json_puts(&w, ",\"fw\":\"");
        json_escaped(&w, F_fw);
        json_puts(&w, "\"");
    }
    if (F_board[0]) {
        json_puts(&w, ",\"board\":\"");
        json_escaped(&w, F_board);
        json_puts(&w, "\"");
    }
    /* The choice and the walk are different facts: a card that is out keeps the choice (it
     * comes back with the card) while nothing is walking. */
    bool walking = playlist_select_walking();
    uint64_t playlist = walking ? playlist_select_current() : 0;
    bool rescanning = F.rescan_running ? F.rescan_running() : false;
    json_printf(&w, ",\"tracks\":%u,\"generation\":%u,\"library\":\"%s\",\"rescanning\":%s,"
                "\"order\":\"%s\",\"playlist\":",
                tracks, generation, state, rescanning ? "true" : "false",
                playlist ? "list" : "tree");
    if (playlist) {
        char id[17] = {0};
        char raw[96];
        snprintf(id, sizeof id, "%016llx", (unsigned long long)playlist);
        playlist_select_name(raw, sizeof raw);
        json_printf(&w, "{\"id\":\"%s\",\"name\":\"", id);
        json_escaped(&w, raw);
        json_puts(&w, "\"}");
    } else {
        json_puts(&w, "null");
    }
    json_puts(&w, ",\"playing\":{\"title\":\"");
    json_escaped(&w, b->st.title);
    json_puts(&w, "\",\"path\":\"");
    json_escaped(&w, b->st.path);
    json_printf(&w, "\",\"elapsed_ms\":%u,\"duration_ms\":%u},"
                "\"listeners\":{\"used\":%u,\"slots\":%u}}",
                (unsigned)b->st.elapsed_ms, (unsigned)b->st.duration_ms,
                (unsigned)b->st.listeners, (unsigned)b->st.listener_slots);
    if (lib) library_release(lib);
    if (!json_ok(&w)) {
        free(b);
        http_send_error(fd, 500, "status too large");
        return HTTP_HANDLED;
    }

    if (http_send_headers(fd, 200, "application/json", w.n,
                          "Cache-Control: no-store\r\n") == ESP_OK)
        http_send_all(fd, b->body, w.n);
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
    snprintf(F_id, sizeof F_id, "%s", cfg->id ? cfg->id : "");
    snprintf(F_fw, sizeof F_fw, "%s", cfg->fw ? cfg->fw : "");
    snprintf(F_board, sizeof F_board, "%s", cfg->board ? cfg->board : "");

    esp_err_t e = http_server_register_route("/api/status", status_route, NULL);
    if (e != ESP_OK) return e;
    e = http_server_register_route("/", root_route, NULL);
    if (e != ESP_OK) return e;
    ESP_LOGI(TAG, "/api/status is up; the root sends a browser to %s",
             CONFIG_FRONT_DOOR_LAUNCH_URL);
    return ESP_OK;
}
