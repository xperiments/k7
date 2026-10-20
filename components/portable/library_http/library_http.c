#include "library_http.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "http_server.h"

#define TAG "library-http"

static const library_t *L;

static http_result_t library_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (strcmp(req->path, "/api/library") != 0) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    if (!L) {
        http_send_error(fd, 503, "the library view is not available");
        return HTTP_HANDLED;
    }
    size_t len = 0;
    const char *json = library_json(L, &len);
    if (!json) {
        http_send_error(fd, 503, "the JSON library view is not available");
        return HTTP_HANDLED;
    }

    char headers[64];
    snprintf(headers, sizeof headers, "ETag: \"gen-%u\"\r\n",
             (unsigned)library_generation(L));
    http_send_timeout_ms(fd, 5000);
    if (http_send_headers(fd, 200, "application/json", len, headers) != ESP_OK)
        return HTTP_HANDLED;
    bool sent = http_send_all(fd, json, len);
    ESP_LOGI(TAG, "GET /api/library -> %u B, gen %u, %s%s",
             (unsigned)len, (unsigned)library_generation(L),
             library_state_name(library_state(L)), sent ? "" : " (client stopped)");
    return HTTP_HANDLED;
}

esp_err_t library_http_start(const library_t *lib)
{
    if (!lib) return ESP_ERR_INVALID_ARG;
    L = lib;
    return http_server_register_route("/api/library", library_route, NULL);
}
