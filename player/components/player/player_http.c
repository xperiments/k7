#include "player_http.h"

#include <string.h>

void player_http_init(player_http_t *h)
{
    if (!h) return;
    memset(h, 0, sizeof *h);
}

int player_http_feed(player_http_t *h, const uint8_t *data, size_t len, size_t *body_off)
{
    if (!h || !data) return -1;
    size_t before = h->len;

    size_t space = sizeof h->buf - h->len;
    size_t take = len < space ? len : space;
    memcpy(h->buf + h->len, data, take);
    h->len += take;

    /* The head ends at the first blank line. */
    size_t end = 0;
    for (size_t i = 3; i < h->len; i++) {
        if (h->buf[i - 3] == '\r' && h->buf[i - 2] == '\n' && h->buf[i - 1] == '\r' &&
            h->buf[i] == '\n') {
            end = i + 1;
            break;
        }
    }
    if (end == 0) {
        if (h->len == sizeof h->buf) return -1;
        return 0;
    }

    /* "HTTP/1.0 200 OK" (the K7's own line) or any HTTP/x.y status. */
    h->buf[end] = '\0';
    if (strncmp((const char *)h->buf, "HTTP/", 5) != 0) return -1;
    const char *sp = strchr((const char *)h->buf, ' ');
    if (!sp || sp[1] < '0' || sp[1] > '9') return -1;
    int code = 0;
    for (int i = 1; i <= 3 && sp[i] >= '0' && sp[i] <= '9'; i++) code = code * 10 + (sp[i] - '0');
    if (code < 100 || code > 599) return -1;

    h->status = code;
    h->complete = true;
    if (body_off) *body_off = end > before ? end - before : 0;
    return 1;
}
