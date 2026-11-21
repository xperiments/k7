/*
 * snapshot_http - see include/snapshot_http.h.
 */
#include "snapshot_http.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "app_board.h"
#include "app_services.h"
#include "esp_log.h"
#include "flash_job.h"
#include "http_json.h"
#include "http_server.h"
#include "json.h"
#include "snapshot.h"

static const char *TAG = "snapshot";

/* The flash write, on the writer task: an HTTP worker's stack is PSRAM, and a write disables
 * the cache the stack lives behind (flash_job.h). */
typedef struct {
    const uint8_t    *image;
    size_t            len;
    char             *why;
    size_t            why_size;
    app_board_flash_t result;
} snapshot_flash_t;

static bool flash_snapshot(void *ctx)
{
    snapshot_flash_t *job = (snapshot_flash_t *)ctx;
    job->result = app_board_snapshot_flash(job->image, job->len, job->why, job->why_size);
    return true;
}

static http_result_t snapshot_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (strcmp(req->path, "/api/snapshot") != 0) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    if (strcmp(req->content_type, "application/octet-stream") != 0) {
        http_send_error(fd, 415, "the snapshot must be application/octet-stream");
        return HTTP_HANDLED;
    }

    /* The image is validated here, before the seam is asked to write it: the host's
     * partition is a file and the device's is flash, and neither should be the place where
     * "is this a snapshot" is answered. Opening it is the validation. */
    char why[128] = {0};
    snapshot_t *snap = NULL;
    snapshot_result_t valid = snapshot_open_why(req->body, req->body_len, &snap, why, sizeof why);
    if (snap) snapshot_close(snap);
    snapshot_flash_t job = {req->body, req->body_len, why, sizeof why, APP_BOARD_FLASH_INVALID};
    app_board_flash_t r;
    if (valid != SNAPSHOT_OK)
        r = APP_BOARD_FLASH_INVALID;
    else if (!flash_job_run(flash_snapshot, &job))
        r = APP_BOARD_FLASH_FAILED;
    else
        r = job.result;
    int status;
    switch (r) {
    case APP_BOARD_FLASH_OK: status = 200; break;
    case APP_BOARD_FLASH_INVALID: status = 400; break;
    case APP_BOARD_FLASH_NO_PARTITION: status = 503; break;
    default: status = 500; break;
    }
    char body[320];
    json_writer_t w = json_writer_make(body, sizeof body);
    if (r == APP_BOARD_FLASH_OK) {
        json_printf(&w, "{\"applied\":true,\"bytes\":%u}", (unsigned)req->body_len);
    } else {
        /* `why` is the validator's sentence, and it is data: it goes through the writer's
         * escaping rather than between two quotes with %s. */
        json_puts(&w, "{\"applied\":false,\"error\":\"");
        json_escaped(&w, why);
        json_puts(&w, "\"}");
    }
    http_json_send_writer(fd, status, &w, "Cache-Control: no-store\r\n");

    /* The reader holds a PSRAM copy of the old image, and the write that just happened ran
     * with the PSRAM cache off. A restart is the cheap, certain way to serve the new one. */
    if (r == APP_BOARD_FLASH_OK) app_board_reboot_soon(1500);
    return HTTP_HANDLED;
}

/* The image the device is answering from, served as it stands: the same `k7.db` the App
 * wrote when it measured the card, whether it arrived over POST or on the card's first
 * boot. A browser that has never met the card reads the names out of it (front-door.md
 * S3), which is what makes naming a card in the player possible without the card.
 *
 * The bytes come from the PSRAM copy the reader reads in place, never from the flash
 * partition: mapping flash disables the cache, and an HTTP worker's stack lives in PSRAM,
 * so a map taken here aborts the device (esp_cache_freeze_caches_disable_interrupts). */
static http_result_t snapshot_get_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (strcmp(req->path, "/api/snapshot") != 0) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    size_t size = 0;
    const uint8_t *image = app_services_snapshot_image(&size);
    if (!image || size == 0) {
        http_send_error(fd, 404, "no image on this device");
        return HTTP_HANDLED;
    }
    const char *headers =
        "Content-Disposition: attachment; filename=\"k7.db\"\r\n"
        "Cache-Control: no-store\r\n";
    http_send_timeout_ms(fd, 30000);
    if (http_send_headers(fd, 200, "application/octet-stream", size, headers) != ESP_OK)
        return HTTP_HANDLED;
    bool sent = http_send_all(fd, image, size);
    ESP_LOGI(TAG, "GET /api/snapshot -> %u B%s", (unsigned)size, sent ? "" : " (client stopped)");
    return HTTP_HANDLED;
}

esp_err_t snapshot_http_start(void)
{
    esp_err_t e = http_server_register_route_method("/api/snapshot", "POST", snapshot_route, NULL);
    if (e != ESP_OK) return e;
    e = http_server_register_route_method("/api/snapshot", "GET", snapshot_get_route, NULL);
    if (e != ESP_OK) return e;
    return http_server_route_body_limit("/api/snapshot", "POST", HTTP_UPLOAD_MAX);
}
