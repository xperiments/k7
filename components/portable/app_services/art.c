/*
 * art - see include/art.h and docs/specs/art-packs.md §4/§5.
 *
 * Two pieces of state, each with its own lock:
 *
 *   s_state_lock  the pool and the status the App reads. It is held for a pointer swap,
 *                 never across a flash access or a socket.
 *   s_io_lock     the mapped pack and the requests that carry its bytes. An upload, a
 *                 reset and GET /api/art/pack are serialized here; the panel never takes
 *                 it, so a slow App download cannot stall a Track change.
 *
 * An upload runs in this order: validate the body (nothing touched), ask the screen to
 * drop the user sheet through its own lock (art_quiesce), release the mapped pack, write
 * the partition on the flash writer, map it again, validate what came back, and install
 * the pool's user half. The label picks it up under the screen's lock on its next build;
 * the bytes behind the old sheet are only unmapped after the screen has forgotten it,
 * because the selected template's Slot names point into them.
 */
#include "art.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "flash_job.h"
#include "http_json.h"
#include "http_server.h"
#include "json.h"
#include "label_pack.h"
#include "label_pool.h"
#include "label_store.h"

static const char *TAG = "art";

/* A pack and its bytes are bounded by the partition; the status is bounded by the pack's
 * three strings and its reason. Escaping a 32-byte name that is all quotes costs six bytes
 * per byte, so this leaves the names and the sentence room. */
#define ART_STATUS_JSON_MAX 1024

static pthread_mutex_t s_state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t s_io_lock = PTHREAD_MUTEX_INITIALIZER;

static label_pool_t s_pool;
static uint8_t s_font_count;
static bool s_started;

static art_quiesce_fn s_quiesce;
static void *s_quiesce_ctx;

/* The mapped pack and the pack inside it: the store may return the whole `art` partition,
 * and only the first `total_size` bytes belong to the pack. */
static label_store_image_t *s_image;
static const uint8_t *s_pack_bytes;
static size_t s_pack_size;

/* The status. An absent pack is installed=false, valid=true; an invalid one is
 * installed=true, valid=false and `s_why` says why. */
static bool s_installed;
static bool s_valid = true;
static uint16_t s_design_count;
static uint32_t s_sheet_crc;
static char s_name[LABEL_PACK_NAME_MAX + 1];
static char s_author[LABEL_PACK_AUTHOR_MAX + 1];
static char s_why[160];

/* Bumped whenever the pool changes. The label compares it, so a sheet swap that lands on
 * the same template index still rebuilds the objects. */
static uint32_t s_generation = 1;

static void art_release_image(void)
{
    if (s_image) label_store_image_put(s_image);
    s_image = NULL;
    s_pack_bytes = NULL;
    s_pack_size = 0;
}

static void set_text(char *dst, size_t cap, const char *src)
{
    snprintf(dst, cap, "%s", src ? src : "");
}

/* --------------------------------------------------------------- the store */

/* Read the partition and make it the active pack, or fall back to the factory. Called
 * with s_io_lock held, and only when no label is using the sheet (boot, or after a
 * quiesce), because the old mapping is released here. Returns whether a valid pack is
 * active; an invalid or absent one is a state, not an error. */
static bool art_sync_from_store(void)
{
    label_store_image_t *image = NULL;
    const uint8_t *bytes = NULL;
    size_t size = 0;
    char store_why[128] = {0};
    label_store_state_t state =
        label_store_image_get(&image, &bytes, &size, store_why, sizeof store_why);

    label_pack_t pack;
    memset(&pack, 0, sizeof pack);
    label_sheet_t *sheet = NULL;
    char why[160] = {0};
    bool installed = state != LABEL_STORE_NONE;
    bool valid = false;

    if (state == LABEL_STORE_PRESENT) {
        label_result_t r =
            label_pack_open_why(bytes, size, s_font_count, &pack, &sheet, why, sizeof why);
        if (r == LABEL_OK) {
            valid = true;
        } else if (r == LABEL_NO_MEM) {
            set_text(why, sizeof why, "out of memory for the pack");
        } else if (!why[0]) {
            set_text(why, sizeof why, "the pack is unusable");
        }
    } else if (state == LABEL_STORE_UNREADABLE) {
        set_text(why, sizeof why, store_why[0] ? store_why : "the art partition is unreadable");
    }

    pthread_mutex_lock(&s_state_lock);
    if (s_pool.user) label_pool_clear_user(&s_pool);
    art_release_image();
    if (valid) {
        s_image = image;
        s_pack_bytes = bytes;
        s_pack_size = pack.total_size;
        label_pool_set_user(&s_pool, sheet);
        sheet = NULL; /* the pool owns it now */
        s_installed = true;
        s_valid = true;
        s_design_count = pack.design_count;
        s_sheet_crc = pack.sheet_crc;
        set_text(s_name, sizeof s_name, pack.name);
        set_text(s_author, sizeof s_author, pack.author);
        s_why[0] = 0;
    } else {
        if (image) label_store_image_put(image);
        s_installed = installed;
        s_valid = !installed;
        s_design_count = 0;
        s_sheet_crc = 0;
        s_name[0] = 0;
        s_author[0] = 0;
        set_text(s_why, sizeof s_why, installed ? why : "");
    }
    s_generation++;
    pthread_mutex_unlock(&s_state_lock);

    if (valid)
        ESP_LOGI(TAG, "art: %u designs, %s by %s, sheet crc %08x",
                 (unsigned)pack.design_count, pack.name[0] ? pack.name : "(unnamed)",
                 pack.author[0] ? pack.author : "(unnamed)", (unsigned)pack.sheet_crc);
    else if (installed)
        ESP_LOGW(TAG, "art: %s; the factory sheet alone", why);
    else
        ESP_LOGI(TAG, "art: no user pack; the factory sheet alone");
    return valid;
}

/* The flash writer's jobs. Every flash and MMU operation an upload needs runs here, on
 * flash_job's internal stack: the HTTP worker's stack is PSRAM, and a raw partition read
 * (`esp_partition_read`) while the cache is off would run through the window it disables
 * (ADR 0054). The mapped pack is plain cached memory afterwards; only the mapping and the
 * partition calls have to be on this task. */
typedef struct {
    const uint8_t *bytes;
    size_t         len;
    bool           wrote;
    bool           valid;
    char           why[160];
} art_write_t;

static void status_copy(art_status_t *out);

/* Write, then read back and open what came back. If the bytes that landed do not open,
 * erase them: the factory is the state a failed write is allowed to leave. */
static bool write_job(void *ctx)
{
    art_write_t *job = (art_write_t *)ctx;
    art_release_image();
    job->wrote = label_store_write(job->bytes, job->len, job->why, sizeof job->why);
    job->valid = art_sync_from_store();
    if (job->wrote && !job->valid) {
        art_status_t after;
        status_copy(&after);
        set_text(job->why, sizeof job->why, after.why);
        char erase_why[160] = {0};
        if (!label_store_erase(erase_why, sizeof erase_why))
            ESP_LOGW(TAG, "art: the unreadable pack could not be erased (%s)",
                     erase_why[0] ? erase_why : "no reason");
        (void)art_sync_from_store();
    }
    return true;
}

typedef struct {
    bool erased;
    char why[160];
} art_erase_t;

static bool erase_job(void *ctx)
{
    art_erase_t *job = (art_erase_t *)ctx;
    art_release_image();
    job->erased = label_store_erase(job->why, sizeof job->why);
    (void)art_sync_from_store();
    return true;
}

/* Ask the screen to let the user sheet go. The hook runs under the screen's lock; the
 * host has no screen and clears the pool directly. */
static bool art_quiesce(void)
{
    if (s_quiesce) return s_quiesce(s_quiesce_ctx);
    app_services_art_quiesced();
    return true;
}

/* --------------------------------------------------------------- the status */

static void status_copy(art_status_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof *out);
    pthread_mutex_lock(&s_state_lock);
    out->installed = s_installed;
    out->valid = s_valid;
    set_text(out->name, sizeof out->name, s_name);
    set_text(out->author, sizeof out->author, s_author);
    out->design_count = s_design_count;
    out->sheet_crc = s_sheet_crc;
    out->factory_count = label_pool_factory_count(&s_pool);
    set_text(out->why, sizeof out->why, s_why);
    pthread_mutex_unlock(&s_state_lock);
}

static void send_status(int fd, int status)
{
    art_status_t st;
    status_copy(&st);
    char body[ART_STATUS_JSON_MAX];
    json_writer_t w = json_writer_make(body, sizeof body);
    json_printf(&w, "{\"installed\":%s,\"valid\":%s,\"name\":\"",
                st.installed ? "true" : "false", st.valid ? "true" : "false");
    json_escaped(&w, st.name);
    json_puts(&w, "\",\"author\":\"");
    json_escaped(&w, st.author);
    json_printf(&w, "\",\"design_count\":%u,\"sheet_crc\":%u,\"factory_count\":%u,\"why\":\"",
                (unsigned)st.design_count, (unsigned)st.sheet_crc,
                (unsigned)st.factory_count);
    json_escaped(&w, st.why);
    json_puts(&w, "\"}");
    http_json_send_writer(fd, status, &w, "Cache-Control: no-store\r\n");
}

static void send_error(int fd, int status, const char *why)
{
    char body[320];
    json_writer_t w = json_writer_make(body, sizeof body);
    json_puts(&w, "{\"ok\":false,\"error\":\"");
    json_escaped(&w, why ? why : "");
    json_puts(&w, "\"}");
    http_json_send_writer(fd, status, &w, "Cache-Control: no-store\r\n");
}

/* ------------------------------------------------------------------ routes */

static http_result_t art_get_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (strcmp(req->path, "/api/art") != 0) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    send_status(fd, 200);
    return HTTP_HANDLED;
}

static http_result_t art_pack_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (strcmp(req->path, "/api/art/pack") != 0) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }

    pthread_mutex_lock(&s_io_lock);
    if (!s_installed || !s_valid || !s_pack_bytes || !s_pack_size) {
        pthread_mutex_unlock(&s_io_lock);
        http_send_error(fd, 404, "there is no art pack");
        return HTTP_HANDLED;
    }
    const uint8_t *bytes = s_pack_bytes;
    const size_t len = s_pack_size;
    http_send_timeout_ms(fd, 2000);
    if (http_send_headers(fd, 200, "application/octet-stream", len,
                          "Cache-Control: no-store\r\n") == ESP_OK)
        http_send_all(fd, bytes, len);
    pthread_mutex_unlock(&s_io_lock);
    return HTTP_HANDLED;
}

/* Both content types are the App's; both are outside the safelisted set, which is the
 * CSRF guard the Snapshot uses too. */
static bool pack_content_type(const char *type)
{
    return type && (strcmp(type, "application/octet-stream") == 0 ||
                    strcmp(type, "application/x-k7artpack") == 0);
}

static http_result_t art_post_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (strcmp(req->path, "/api/art") != 0) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    if (!pack_content_type(req->content_type)) {
        send_error(fd, 415,
                   "the pack must be application/octet-stream or application/x-k7artpack");
        return HTTP_HANDLED;
    }
    if (req->body_len > APP_SERVICES_ART_PACK_MAX) {
        http_send_error(fd, 413, "the pack is larger than the art partition");
        return HTTP_HANDLED;
    }

    /* Validate all of it before anything is written (art-packs.md §7). The sheet is
     * opened over the request body; the handle is closed before the body is freed, and
     * the one that goes into the pool is opened again over the partition's bytes. */
    label_pack_t pack;
    label_sheet_t *sheet = NULL;
    char why[192] = {0};
    label_result_t r = label_pack_open_why(req->body, req->body_len, s_font_count, &pack,
                                           &sheet, why, sizeof why);
    if (sheet) label_close(sheet);
    if (r == LABEL_NO_MEM) {
        send_error(fd, 500, "not enough memory for the pack");
        return HTTP_HANDLED;
    }
    if (r != LABEL_OK) {
        send_error(fd, 422, why[0] ? why : "the pack is not one this firmware reads");
        return HTTP_HANDLED;
    }
    /* The upload is the file. The partition read is allowed to see a tail past `total_size`
     * (the partition is 1 MB); an upload that carries one is not the file the CRC covers. */
    if (pack.total_size != req->body_len) {
        send_error(fd, 422, "the pack has bytes past its total size");
        return HTTP_HANDLED;
    }

    if (!art_quiesce()) {
        send_error(fd, 503, "the screen is busy; try again");
        return HTTP_HANDLED;
    }

    pthread_mutex_lock(&s_io_lock);
    art_write_t write = {req->body, req->body_len, false, false, {0}};
    (void)flash_job_run(write_job, &write);
    pthread_mutex_unlock(&s_io_lock);

    if (!write.wrote) {
        send_error(fd, 500, write.why[0] ? write.why : "the art partition refused the write");
        return HTTP_HANDLED;
    }
    if (!write.valid) {
        send_error(fd, 500,
                   write.why[0] ? write.why : "the written pack did not verify");
        return HTTP_HANDLED;
    }
    send_status(fd, 200);
    return HTTP_HANDLED;
}

static http_result_t art_delete_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (strcmp(req->path, "/api/art") != 0) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    if (!art_quiesce()) {
        send_error(fd, 503, "the screen is busy; try again");
        return HTTP_HANDLED;
    }

    pthread_mutex_lock(&s_io_lock);
    art_erase_t erase = {false, {0}};
    (void)flash_job_run(erase_job, &erase);
    pthread_mutex_unlock(&s_io_lock);

    if (!erase.erased) {
        send_error(fd, 500, erase.why[0] ? erase.why : "the art partition refused the erase");
        return HTTP_HANDLED;
    }
    send_status(fd, 200);
    return HTTP_HANDLED;
}

/* ------------------------------------------------------------------- API */

bool app_services_art_set_factory(const uint8_t *sheet_bytes, size_t size, uint8_t font_count)
{
    if (!sheet_bytes || size == 0 || font_count == 0) return false;

    label_sheet_t *sheet = NULL;
    char why[128] = {0};
    if (label_open_why(sheet_bytes, size, font_count, &sheet, why, sizeof why) != LABEL_OK) {
        ESP_LOGE(TAG, "the factory sheet does not open (%s); the print keeps its flat fill",
                 why[0] ? why : "invalid sheet");
        return false;
    }

    pthread_mutex_lock(&s_state_lock);
    s_font_count = font_count;
    label_pool_set_factory(&s_pool, sheet);
    s_generation++;
    const uint16_t count = label_pool_factory_count(&s_pool);
    pthread_mutex_unlock(&s_state_lock);

    ESP_LOGI(TAG, "factory sheet: %u designs, %u compiled fonts", (unsigned)count,
             (unsigned)font_count);
    return true;
}

void app_services_art_set_quiesce(art_quiesce_fn fn, void *ctx)
{
    s_quiesce = fn;
    s_quiesce_ctx = ctx;
}

void app_services_art_quiesced(void)
{
    pthread_mutex_lock(&s_state_lock);
    label_pool_clear_user(&s_pool);
    s_generation++;
    pthread_mutex_unlock(&s_state_lock);
}

bool app_services_art_pick(uint32_t ink_seed, const label_sheet_t **sheet,
                           uint16_t *template, uint32_t *generation)
{
    bool ok;
    pthread_mutex_lock(&s_state_lock);
    ok = label_pool_pick(&s_pool, ink_seed, sheet, template);
    if (generation) *generation = s_generation;
    pthread_mutex_unlock(&s_state_lock);
    return ok;
}

uint16_t app_services_art_count(void)
{
    pthread_mutex_lock(&s_state_lock);
    const uint16_t count = label_pool_count(&s_pool);
    pthread_mutex_unlock(&s_state_lock);
    return count;
}

void app_services_art_status(art_status_t *out) { status_copy(out); }

esp_err_t app_services_art_start(void)
{
    if (s_started) return ESP_OK;
    s_started = true;

    /* The partition is read before the listener starts: the boot is the one moment the
     * pack can be opened without a screen holding a sheet. */
    pthread_mutex_lock(&s_io_lock);
    (void)art_sync_from_store();
    pthread_mutex_unlock(&s_io_lock);

    esp_err_t e = http_server_register_route_method("/api/art", "GET", art_get_route, NULL);
    if (e != ESP_OK) return e;
    e = http_server_register_route_method("/api/art/pack", "GET", art_pack_route, NULL);
    if (e != ESP_OK) return e;
    e = http_server_register_route_method("/api/art", "POST", art_post_route, NULL);
    if (e != ESP_OK) return e;
    e = http_server_register_route_method("/api/art", "DELETE", art_delete_route, NULL);
    if (e != ESP_OK) return e;
    return http_server_route_body_limit("/api/art", "POST", APP_SERVICES_ART_PACK_MAX);
}
