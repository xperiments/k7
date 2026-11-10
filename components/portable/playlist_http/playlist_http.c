/*
 * playlist_http - see include/playlist_http.h.
 */
#include "playlist_http.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http_json.h"
#include "http_server.h"
#include "json.h"
#include "library.h"
#include "playlist_select.h"

static const snapshot_t *(*current)(void);

static void id_hex(char out[17], uint64_t id)
{
    snprintf(out, 17, "%016llx", (unsigned long long)id);
}

static bool parse_hex64(const char *s, size_t n, uint64_t *out)
{
    if (!s || n == 0 || n > 16) return false;
    uint64_t v = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = (v << 4) | (uint64_t)d;
    }
    *out = v;
    return true;
}

/* `{"id": "<hex>"}` or `{"id": null}`. The object is read by the shared reader, so a body
 * with an "id" somewhere else cannot be read as this one. */
static bool body_list_id(const char *body, size_t len, uint64_t *id)
{
    (void)len;
    json_tok_t tok[16];
    int nt = 0;
    if (!json_parse(body, tok, 16, &nt) || nt == 0) return false;
    if (tok[0].type != JSON_TOK_OBJECT) return false;
    int at = json_tok_member(body, tok, nt, 0, "id");
    if (at < 0) return false;
    if (tok[at].type == JSON_TOK_PRIMITIVE) {
        /* `null` is "no list": the choice goes back to the tree order. Read from the token's
         * own bytes, because a primitive is not a string and unescaping refuses it. */
        int n = tok[at].end - tok[at].start;
        if (n != 4 || memcmp(body + tok[at].start, "null", 4) != 0) return false;
        *id = 0;
        return true;
    }
    char hex[17];
    if (!json_tok_unescape(body, &tok[at], hex, sizeof hex)) return false;
    return parse_hex64(hex, strlen(hex), id);
}

/* GET /api/playlists: what the image holds and which one is walking. A device with no
 * Snapshot answers an empty list rather than an error: "no lists" is true. */
static http_result_t playlists_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (strcmp(req->path, "/api/playlists") != 0) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    const snapshot_t *snap = current();
    uint32_t n = snap ? snapshot_lists(snap) : 0;
    size_t cap = 320 + (size_t)n * 320;
    char *body = malloc(cap);
    if (!body) {
        http_send_error(fd, 500, "out of memory");
        return HTTP_HANDLED;
    }
    json_writer_t w = json_writer_make(body, cap);
    uint64_t selected = playlist_select_current();
    json_puts(&w, "{\"selected\":");
    if (selected) {
        char sel[17] = {0};
        id_hex(sel, selected);
        json_printf(&w, "\"%s\"", sel);
    } else {
        json_puts(&w, "null");
    }
    json_printf(&w, ",\"walking\":%s,\"missing\":%u,\"lists\":[",
                playlist_select_walking() ? "true" : "false",
                (unsigned)playlist_select_missing());
    for (uint32_t i = 0; i < n; i++) {
        snapshot_list_t l;
        if (!snapshot_list_at(snap, i, &l)) continue;
        char id[17];
        id_hex(id, l.id);
        json_printf(&w, "%s{\"id\":\"%s\",\"name\":\"", i ? "," : "", id);
        /* The name is data: escaped into the document, not into a fixed buffer first. */
        json_escaped(&w, snapshot_list_name(snap, &l));
        json_printf(&w, "\",\"created\":%llu,\"rows\":%u}", (unsigned long long)l.created,
                    (unsigned)l.row_count);
    }
    json_puts(&w, "]}");
    http_json_send_writer(fd, 200, &w, "Cache-Control: no-cache\r\n");
    free(body);
    return HTTP_HANDLED;
}

/* GET /api/playlists/<id>: one list's rows, as the Library's own paths. */
static http_result_t playlist_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    const snapshot_t *snap = current();
    uint64_t id = 0;
    const char *suffix = req->path + strlen("/api/playlists/");
    if (!snap || !parse_hex64(suffix, strlen(suffix), &id)) {
        http_send_error(fd, 404, "no such list");
        return HTTP_HANDLED;
    }
    snapshot_list_t l;
    if (!snapshot_list_find(snap, id, &l)) {
        http_send_error(fd, 404, "no such list");
        return HTTP_HANDLED;
    }
    /* Exact enough: the paths are read once to size the body, so a list of long paths cannot
     * ask for a buffer the device cannot give. */
    size_t cap = 320;
    for (uint32_t i = 0; i < l.row_count; i++) {
        const char *path = NULL;
        if (snapshot_list_row(snap, &l, i, &path) && path) cap += strlen(path) * 2 + 8;
    }
    char *body = malloc(cap);
    if (!body) {
        http_send_error(fd, 500, "out of memory");
        return HTTP_HANDLED;
    }
    json_writer_t w = json_writer_make(body, cap);
    char id_hexed[17];
    id_hex(id_hexed, id);
    json_printf(&w, "{\"id\":\"%s\",\"name\":\"", id_hexed);
    json_escaped(&w, snapshot_list_name(snap, &l));
    json_puts(&w, "\",\"rows\":[");
    for (uint32_t i = 0; i < l.row_count; i++) {
        const char *path = NULL;
        if (!snapshot_list_row(snap, &l, i, &path)) continue;
        /* The path is escaped straight into the document: no second buffer, and no
         * LIB_PATH_MAX cut in the middle of an escape. */
        json_puts(&w, i ? ",\"" : "\"");
        json_escaped(&w, path);
        json_puts(&w, "\"");
    }
    json_puts(&w, "]}");
    http_json_send_writer(fd, 200, &w, "Cache-Control: no-cache\r\n");
    free(body);
    return HTTP_HANDLED;
}

/* POST /api/playlists/select: walk this one, or none. The library is acquired for the
 * resolution only: a Playlist is resolved against the generation that is current now. */
static http_result_t select_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    uint64_t id = 0;
    if (!body_list_id((const char *)req->body, req->body_len, &id)) {
        http_json_send(fd, 400,
                       "{\"ok\":false,\"error\":\"a body of {\\\"id\\\":\\\"<hex>\\\"} or "
                       "{\\\"id\\\":null}\"}",
                       "Cache-Control: no-store\r\n");
        return HTTP_HANDLED;
    }
    const snapshot_t *snap = current();
    if (id && !snap) {
        http_json_send(fd, 503, "{\"ok\":false,\"error\":\"no Snapshot on this device\"}",
                       "Cache-Control: no-store\r\n");
        return HTTP_HANDLED;
    }
    const library_t *lib = library_acquire();
    playlist_select_result_t r = playlist_select_set(snap, lib, id);
    if (lib) library_release(lib);

    int status;
    const char *error = NULL;
    switch (r) {
    case PLAYLIST_SELECT_OK: status = 200; break;
    case PLAYLIST_SELECT_NO_SNAPSHOT: status = 503; error = "no Snapshot on this device"; break;
    case PLAYLIST_SELECT_NOT_FOUND: status = 404; error = "no such list"; break;
    case PLAYLIST_SELECT_EMPTY:
        status = 409;
        error = "that list has no Track this Library can find";
        break;
    default: status = 500; error = "cannot select"; break;
    }
    char body[256];
    json_writer_t w = json_writer_make(body, sizeof body);
    if (error) {
        json_puts(&w, "{\"ok\":false,\"error\":\"");
        json_escaped(&w, error);
        json_puts(&w, "\"}");
    } else if (id) {
        char hexed[17];
        id_hex(hexed, id);
        json_printf(&w, "{\"ok\":true,\"order\":\"list\",\"selected\":\"%s\"}", hexed);
    } else {
        json_puts(&w, "{\"ok\":true,\"order\":\"tree\",\"selected\":null}");
    }
    http_json_send_writer(fd, status, &w, "Cache-Control: no-store\r\n");
    return HTTP_HANDLED;
}

esp_err_t playlist_http_start(const snapshot_t *(*snapshot)(void))
{
    if (!snapshot) return ESP_ERR_INVALID_ARG;
    current = snapshot;
    esp_err_t e = http_server_register_route("/api/playlists", playlists_route, NULL);
    if (e != ESP_OK) return e;
    e = http_server_register_route("/api/playlists/", playlist_route, NULL);
    if (e != ESP_OK) return e;
    return http_server_register_route_method("/api/playlists/select", "POST", select_route, NULL);
}
