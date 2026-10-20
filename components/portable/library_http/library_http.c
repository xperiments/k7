#include "library_http.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "http_server.h"

#define TAG "library-http"

static const library_t *L_override;

static http_result_t send_library(int fd, const http_request_t *req, bool binary,
                                  const library_t *lib)
{
    const char *path = binary ? "/api/library.bin" : "/api/library";
    if (strcmp(req->path, path) != 0) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }

    size_t len = 0;
    const void *body = NULL;
    const char *content_type = "application/json";
    if (binary) {
        body = library_blob(lib, &len);
        content_type = "application/octet-stream";
    } else {
        body = library_json(lib, &len);
    }
    if (!body) {
        http_send_error(fd, 503, "the library view is not available");
        return HTTP_HANDLED;
    }

    char headers[128];
    int n = snprintf(headers, sizeof headers, "ETag: \"gen-%u\"\r\n",
                     (unsigned)library_generation(lib));
    if (n < 0 || (size_t)n >= sizeof headers) {
        http_send_error(fd, 500, "the library generation is not representable");
        return HTTP_HANDLED;
    }
    if (binary) {
        int m = snprintf(headers + n, sizeof headers - (size_t)n,
                         "Content-Disposition: attachment; filename=\"library.bin\"\r\n");
        if (m < 0 || (size_t)(n + m) >= sizeof headers) {
            http_send_error(fd, 500, "the library response headers are too large");
            return HTTP_HANDLED;
        }
    }
    http_send_timeout_ms(fd, 5000);
    if (http_send_headers(fd, 200, content_type, len, headers) != ESP_OK)
        return HTTP_HANDLED;
    bool sent = http_send_all(fd, body, len);
    ESP_LOGI(TAG, "GET %s -> %u B, gen %u, %s%s", path, (unsigned)len,
             (unsigned)library_generation(lib), library_state_name(library_state(lib)),
             sent ? "" : " (client stopped)");
    return HTTP_HANDLED;
}

static http_result_t library_route(int fd, const http_request_t *req, void *ctx, bool binary)
{
    (void)ctx;
    const char *path = binary ? "/api/library.bin" : "/api/library";
    if (strcmp(req->path, path) != 0) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    bool owned = L_override == NULL;
    const library_t *lib = L_override ? L_override : library_acquire();
    if (!lib) {
        http_send_error(fd, 503, "the library view is not available");
        return HTTP_HANDLED;
    }
    http_result_t r = send_library(fd, req, binary, lib);
    if (owned) library_release(lib);
    return r;
}

static http_result_t json_route(int fd, const http_request_t *req, void *ctx)
{
    return library_route(fd, req, ctx, false);
}

static http_result_t bin_route(int fd, const http_request_t *req, void *ctx)
{
    return library_route(fd, req, ctx, true);
}

esp_err_t library_http_start(const library_t *lib)
{
    L_override = lib;
    esp_err_t err = http_server_register_route("/api/library.bin", bin_route, NULL);
    if (err == ESP_OK) err = http_server_register_route("/api/library", json_route, NULL);
    return err;
}
