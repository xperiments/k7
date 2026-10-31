/*
 * http_backend_single - the transport for a build with one plain listener.
 *
 * This is the host build's choice of backend, and the one file that says so. It
 * has no second transport to dispatch to: every `fd` a handler writes to is a
 * bare socket, so the front's own send() path carries the bytes, and the reply
 * and timeout hooks are never reached. That keeps the hand-written loop in
 * http_loop.c the only server the integration checks talk to, byte for byte what
 * they talked to before TLS existed.
 *
 * TLS routes register here exactly as they do on the device; the listener tag is
 * not filtered, so they are served as plain HTTP (front door T3: TLS and
 * keep-alive are board-only behaviour and are not asserted by the host suite).
 */
#include "http_transport.h"

esp_err_t http_transport_start(const http_server_cfg_t *cfg) { return http_loop_start(cfg); }

bool http_transport_owns(int fd)
{
    (void)fd;
    return false; /* one plain listener: every reply is a socket write */
}

esp_err_t http_transport_reply(int fd, const http_reply_t *reply)
{
    (void)fd;
    (void)reply;
    return ESP_ERR_NOT_SUPPORTED; /* unreachable: owns() is always false here */
}

bool http_transport_body(int fd, const void *data, size_t n)
{
    (void)fd;
    (void)data;
    (void)n;
    return false;
}

void http_transport_send_timeout(int fd, uint32_t ms)
{
    (void)fd;
    (void)ms;
}
