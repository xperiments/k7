/*
 * http_json - see include/http_json.h.
 */
#include "http_json.h"

#include <string.h>

#include "http_server.h"

void http_json_send(int fd, int status, const char *body, const char *headers)
{
    size_t len = strlen(body);
    if (http_send_headers(fd, status, "application/json", len, headers) == ESP_OK)
        http_send_all(fd, body, len);
}

void http_json_send_writer(int fd, int status, const json_writer_t *w, const char *headers)
{
    if (!json_ok(w)) {
        http_send_error(fd, 500, "the reply did not fit");
        return;
    }
    if (http_send_headers(fd, status, "application/json", w->n, headers) == ESP_OK)
        http_send_all(fd, w->p, w->n);
}
