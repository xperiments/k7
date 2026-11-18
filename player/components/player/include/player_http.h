#pragma once
/*
 * player_http - the head of the K7's /stream response, parsed as it arrives.
 *
 * The K7 answers `HTTP/1.0 200 OK` with `Connection: close` and no length (mini_icecast.c:390),
 * so the body is whatever follows the blank line and ends when the socket does. This finds the
 * blank line and the status, across as many reads as the socket needs, and tells the caller
 * where the body starts in the bytes it just gave it.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLAYER_HTTP_HEAD_MAX 1024

typedef struct {
    uint8_t buf[PLAYER_HTTP_HEAD_MAX];
    size_t  len;
    int     status; /* the code from the status line, 0 before it is complete */
    bool    complete;
} player_http_t;

void player_http_init(player_http_t *h);

/* Feed the bytes that arrived. Returns 1 when the head is complete (*body_off is where the
 * body starts within `data`), 0 when more are needed, -1 when this is not an HTTP head or it
 * outgrew the buffer. */
int player_http_feed(player_http_t *h, const uint8_t *data, size_t len, size_t *body_off);

#ifdef __cplusplus
}
#endif
