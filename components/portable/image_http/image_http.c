/*
 * covers_http - see include/covers_http.h.
 */
#include "image_http.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "card_path.h"
#include "esp_log.h"
#include "http_server.h"

static const char *TAG = "image";

/* A 3,500-track image is a few hundred kilobytes; a file past this is not one. */
#define IMAGE_MAX_BYTES (8u * 1024u * 1024u)

static http_result_t image_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (strcmp(req->path, "/api/k7db") != 0) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }

    char path[256];
    card_path_file(path, sizeof path, "k7.db");
    FILE *f = fopen(path, "rb");
    if (!f) {
        http_send_error(fd, 404, "no image on the card");
        return HTTP_HANDLED;
    }

    long size = -1;
    if (fseek(f, 0, SEEK_END) == 0) size = ftell(f);
    if (size <= 0 || (unsigned long)size > IMAGE_MAX_BYTES || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        http_send_error(fd, size == 0 ? 404 : 413, "the image is not usable");
        return HTTP_HANDLED;
    }

    const char *headers =
        "Content-Disposition: attachment; filename=\"k7.db\"\r\n"
        "Cache-Control: no-store\r\n";
    http_send_timeout_ms(fd, 30000);
    if (http_send_headers(fd, 200, "application/octet-stream", (size_t)size, headers) != ESP_OK) {
        fclose(f);
        return HTTP_HANDLED;
    }

    uint8_t buf[4096];
    bool sent = true;
    size_t got;
    while ((got = fread(buf, 1, sizeof buf, f)) > 0) {
        if (!http_send_all(fd, buf, got)) {
            sent = false;
            break;
        }
    }
    if (ferror(f)) sent = false;
    fclose(f);
    ESP_LOGI(TAG, "GET /api/k7db -> %ld B%s", size, sent ? "" : " (client stopped)");
    return HTTP_HANDLED;
}

esp_err_t image_http_start(void)
{
    return http_server_register_route("/api/k7db", image_route, NULL);
}
