#include "library_http.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "http_server.h"

#define TAG "library-http"

static const library_t *L_override;

static http_result_t bin_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (strcmp(req->path, "/api/library.bin") != 0) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    bool owned = L_override == NULL;
    const library_t *lib = L_override ? L_override : library_acquire();
    if (!lib) {
        http_send_error(fd, 503, "the library view is not available");
        return HTTP_HANDLED;
    }

    size_t len = 0;
    const void *body = library_blob(lib, &len);
    if (!body) {
        if (owned) library_release(lib);
        http_send_error(fd, 503, "the library view is not available");
        return HTTP_HANDLED;
    }

    char headers[160];
    int n = snprintf(headers, sizeof headers,
                     "ETag: \"gen-%u\"\r\n"
                     "Content-Disposition: attachment; filename=\"library.bin\"\r\n",
                     (unsigned)library_generation(lib));
    if (n < 0 || (size_t)n >= sizeof headers) {
        if (owned) library_release(lib);
        http_send_error(fd, 500, "the library generation is not representable");
        return HTTP_HANDLED;
    }
    http_send_timeout_ms(fd, 5000);
    int sent_headers = http_send_headers(fd, 200, "application/octet-stream", len, headers);
    bool sent = sent_headers == ESP_OK && http_send_all(fd, body, len);
    ESP_LOGI(TAG, "GET %s -> %u B, gen %u, %s%s", req->path, (unsigned)len,
             (unsigned)library_generation(lib), library_state_name(library_state(lib)),
             sent ? "" : " (client stopped)");
    if (owned) library_release(lib);
    return HTTP_HANDLED;
}

esp_err_t library_http_start(const library_t *lib)
{
    L_override = lib;
    return http_server_register_route_on(HTTP_LISTENER_TLS, "/api/library.bin", bin_route, NULL);
}
