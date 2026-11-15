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
#include "playlist_file.h"
#include "playlist_select.h"

/* Where the card's root is, asked per request: a card is inserted and removed while the
 * device runs, and a route must never hold the root it saw at registration. */
static void (*s_card_root)(char *out, size_t cap);

static void card_root(char *out, size_t cap)
{
    if (s_card_root) s_card_root(out, cap);
    else if (cap) out[0] = 0;
}

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

/* The id a request names, from the path after "/api/playlists/". 0 when the path names no
 * list (the index or the select route). */
static uint64_t path_id(const char *path)
{
    static const char PREFIX[] = "/api/playlists/";
    size_t prefix = sizeof PREFIX - 1;
    if (strncmp(path, PREFIX, prefix) != 0) return 0;
    uint64_t id = 0;
    if (!parse_hex64(path + prefix, strlen(path + prefix), &id)) return 0;
    return id;
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

/* A refusal the page can read: ok false and one sentence. */
static void send_error(int fd, int status, const char *error)
{
    char body[256];
    json_writer_t w = json_writer_make(body, sizeof body);
    json_puts(&w, "{\"ok\":false,\"error\":\"");
    json_escaped(&w, error);
    json_puts(&w, "\"}");
    http_json_send_writer(fd, status, &w, "Cache-Control: no-store\r\n");
}

/* GET /api/playlists: the lists the card holds, and which one is walking. A card with no
 * directory answers an empty list rather than an error: "no lists" is true. */
static http_result_t playlists_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (strcmp(req->path, "/api/playlists") != 0) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    char root[128];
    card_root(root, sizeof root);

    /* The index is 64 entries of ~96 bytes; on the connection task's stack that was the
     * overflow the budget comment warns about, so it lives on the heap like the other big
     * route buffers. */
    playlist_file_t *lists = malloc(sizeof *lists * PLAYLIST_MAX_LISTS);
    if (!lists) {
        http_send_error(fd, 500, "out of memory");
        return HTTP_HANDLED;
    }
    int n = playlist_list(root, lists, PLAYLIST_MAX_LISTS);
    if (n > (int)PLAYLIST_MAX_LISTS) {
        /* The format refuses a directory over its cap as a whole. */
        free(lists);
        send_error(fd, 503, "this card holds more playlists than the player reads");
        return HTTP_HANDLED;
    }
    if (n < 0) n = 0;

    size_t cap = 320 + (size_t)n * 384;
    char *body = malloc(cap);
    if (!body) {
        free(lists);
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
    for (int i = 0; i < n; i++) {
        char id[17], fingerprint[17];
        id_hex(id, lists[i].id);
        id_hex(fingerprint, lists[i].fingerprint);
        json_printf(&w, "%s{\"id\":\"%s\",\"name\":\"", i ? "," : "", id);
        /* The name is data: escaped into the document, not into a fixed buffer first. */
        json_escaped(&w, lists[i].name);
        json_printf(&w, "\",\"created\":%llu,\"rows\":%u,\"fingerprint\":\"%s\"}",
                    (unsigned long long)lists[i].created, (unsigned)lists[i].rows, fingerprint);
    }
    json_puts(&w, "]}");
    http_json_send_writer(fd, 200, &w, "Cache-Control: no-cache\r\n");
    free(body);
    free(lists);
    return HTTP_HANDLED;
}

/* The rows of one list, written into a document. Used twice: once to size the body and
 * once to fill it, so a list of long paths cannot ask for a buffer the device cannot give. */
typedef struct {
    json_writer_t *w;
    size_t         bytes;
    bool           counting;
    uint32_t       emitted;
} rows_out_t;

static bool emit_row(void *ctx, const char *path)
{
    rows_out_t *out = ctx;
    if (out->counting) {
        /* The worst case of an escaped path, plus its quotes and comma. */
        out->bytes += strlen(path) * 6 + 8;
        return true;
    }
    json_puts(out->w, out->emitted ? ",\"" : "\"");
    json_escaped(out->w, path);
    json_puts(out->w, "\"");
    out->emitted++;
    return true;
}

/* GET /api/playlists/<id>: one list's name, rows and fingerprint. */
static http_result_t playlist_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    char root[128];
    card_root(root, sizeof root);
    uint64_t id = path_id(req->path);
    if (!id) {
        http_send_error(fd, 404, "no such list");
        return HTTP_HANDLED;
    }

    playlist_file_t list;
    playlist_file_result_t found = playlist_read_meta(root, id, &list);
    if (found == PLAYLIST_FILE_NO_CARD) {
        send_error(fd, 503, "no card mounted");
        return HTTP_HANDLED;
    }
    if (found != PLAYLIST_FILE_OK) {
        http_send_error(fd, 404, "no such list");
        return HTTP_HANDLED;
    }

    rows_out_t sizing = {.counting = true};
    if (playlist_read(root, id, NULL, emit_row, &sizing) != PLAYLIST_FILE_OK) {
        http_send_error(fd, 404, "no such list");
        return HTTP_HANDLED;
    }
    size_t cap = 384 + sizing.bytes;
    char *body = malloc(cap);
    if (!body) {
        http_send_error(fd, 500, "out of memory");
        return HTTP_HANDLED;
    }
    json_writer_t w = json_writer_make(body, cap);
    char hexed[17], fingerprint[17];
    id_hex(hexed, id);
    id_hex(fingerprint, list.fingerprint);
    json_printf(&w, "{\"id\":\"%s\",\"name\":\"", hexed);
    json_escaped(&w, list.name);
    json_printf(&w, "\",\"rows\":%u,\"fingerprint\":\"%s\",\"paths\":[", (unsigned)list.rows,
                fingerprint);
    rows_out_t writing = {.w = &w};
    if (playlist_read(root, id, NULL, emit_row, &writing) != PLAYLIST_FILE_OK) {
        free(body);
        http_send_error(fd, 500, "the card changed while reading the list");
        return HTTP_HANDLED;
    }
    json_puts(&w, "]}");
    http_json_send_writer(fd, 200, &w, "Cache-Control: no-cache\r\n");
    free(body);
    return HTTP_HANDLED;
}

/* PUT /api/playlists/<id>: the body is the list's file, byte for byte. One request, one
 * file, and an answer only after it is on the card (ADR 0094). */
static http_result_t put_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    char root[128];
    card_root(root, sizeof root);
    uint64_t id = path_id(req->path);
    if (!id) return (http_send_error(fd, 400, "a list id is 16 hex characters"), HTTP_HANDLED);
    if (!req->body || req->body_len == 0) {
        return (http_send_error(fd, 400, "a body is the list's file"), HTTP_HANDLED);
    }

    playlist_file_t stored;
    playlist_file_result_t r =
        playlist_store_body(root, id, (const char *)req->body, req->body_len, &stored);
    switch (r) {
    case PLAYLIST_FILE_OK: break;
    case PLAYLIST_FILE_NO_CARD:
        return (send_error(fd, 503, "no card mounted"), HTTP_HANDLED);
    case PLAYLIST_FILE_FULL:
        return (send_error(fd, 507, "the card would not take the file"), HTTP_HANDLED);
    default:
        return (send_error(fd, 400, "that body is not a playlist this player reads"),
                HTTP_HANDLED);
    }

    char body[256];
    json_writer_t w = json_writer_make(body, sizeof body);
    char hexed[17], fingerprint[17];
    id_hex(hexed, stored.id);
    id_hex(fingerprint, stored.fingerprint);
    json_printf(&w, "{\"ok\":true,\"id\":\"%s\",\"rows\":%u,\"fingerprint\":\"%s\"}", hexed,
                (unsigned)stored.rows, fingerprint);
    http_json_send_writer(fd, 200, &w, "Cache-Control: no-store\r\n");
    return HTTP_HANDLED;
}

/* DELETE /api/playlists/<id>: remove the file. Deleting the list being walked ends the walk,
 * exactly as a selection naming a list that is gone does. */
static http_result_t delete_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    char root[128];
    card_root(root, sizeof root);
    uint64_t id = path_id(req->path);
    if (!id) return (http_send_error(fd, 400, "a list id is 16 hex characters"), HTTP_HANDLED);

    playlist_file_result_t r = playlist_remove(root, id);
    if (r == PLAYLIST_FILE_NO_CARD) {
        return (send_error(fd, 503, "no card mounted"), HTTP_HANDLED);
    }
    if (r == PLAYLIST_FILE_NOT_FOUND) {
        return (send_error(fd, 404, "no such list"), HTTP_HANDLED);
    }
    if (r != PLAYLIST_FILE_OK) {
        return (send_error(fd, 500, "the card would not remove the file"), HTTP_HANDLED);
    }
    if (playlist_select_current() == id) {
        const library_t *lib = library_acquire();
        playlist_select_set(root, lib, 0);
        if (lib) library_release(lib);
    }

    char body[192];
    json_writer_t w = json_writer_make(body, sizeof body);
    char hexed[17];
    id_hex(hexed, id);
    json_printf(&w, "{\"ok\":true,\"id\":\"%s\",\"removed\":true}", hexed);
    http_json_send_writer(fd, 200, &w, "Cache-Control: no-store\r\n");
    return HTTP_HANDLED;
}

/* POST /api/playlists/select: walk this one, or none. The library is acquired for the
 * resolution only: a Playlist is resolved against the generation that is current now. */
static http_result_t select_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    char root[128];
    card_root(root, sizeof root);
    uint64_t id = 0;
    if (!body_list_id((const char *)req->body, req->body_len, &id)) {
        http_json_send(fd, 400,
                       "{\"ok\":false,\"error\":\"a body of {\\\"id\\\":\\\"<hex>\\\"} or "
                       "{\\\"id\\\":null}\"}",
                       "Cache-Control: no-store\r\n");
        return HTTP_HANDLED;
    }
    const library_t *lib = library_acquire();
    playlist_select_result_t r = playlist_select_set(root, lib, id);
    if (lib) library_release(lib);

    int status;
    const char *error = NULL;
    switch (r) {
    case PLAYLIST_SELECT_OK: status = 200; break;
    case PLAYLIST_SELECT_NO_CARD: status = 503; error = "no card mounted"; break;
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

esp_err_t playlist_http_start(void (*card_root_fn)(char *out, size_t cap))
{
    if (!card_root_fn) return ESP_ERR_INVALID_ARG;
    s_card_root = card_root_fn;
    esp_err_t e = http_server_register_route("/api/playlists", playlists_route, NULL);
    if (e != ESP_OK) return e;
    e = http_server_register_route("/api/playlists/", playlist_route, NULL);
    if (e != ESP_OK) return e;
    e = http_server_register_route_method("/api/playlists/", "PUT", put_route, NULL);
    if (e != ESP_OK) return e;
    e = http_server_route_body_limit("/api/playlists/", "PUT", PLAYLIST_HTTP_BODY_MAX);
    if (e != ESP_OK) return e;
    e = http_server_register_route_method("/api/playlists/", "DELETE", delete_route, NULL);
    if (e != ESP_OK) return e;
    return http_server_register_route_method("/api/playlists/select", "POST", select_route, NULL);
}
