/*
 * The screen, over HTTP: /api/screen.bmp and /api/scene.
 *
 * A diagnostic build's second pair of eyes. The panel board runs LVGL in direct mode over
 * the RGB panel's own two PSRAM framebuffers, so the finished picture is ordinary readable
 * memory - the peripheral scans it, it does not own it - and the console is not the only
 * way to look at the interface. Two routes:
 *
 *   GET  /api/screen.bmp    the 800x480 frame as a 24-bit BMP, bottom-up, no padding
 *                           (800 x 3 is already a multiple of four). It opens in any
 *                           viewer, so the check is `curl > scene.bmp && open scene.bmp`
 *                           instead of several minutes of base64 over a 115200 baud line.
 *   POST /api/scene?n=0..7  re-aim the scene driver the console dump uses, while the
 *                           device runs. One build photographs every scene.
 *
 * Both compile only with CONFIG_APP_UI_SCREEN_HTTP (and only on the panel board, whose
 * Kconfig that symbol depends on). CONFIG_APP_UI_SERIAL_DUMP stays the headless board's,
 * where there is no panel to scan.
 *
 * The capture is deliberate about two things the panel path makes easy to get wrong:
 *
 *   - **Two full refreshes.** The port owns two framebuffers and LVGL alternates between
 *     them, so after one refresh only one index holds the current picture. Invalidating
 *     the screen and refreshing twice leaves both complete; the headless boot does the
 *     same before it trusts a buffer.
 *   - **Copy under the lock.** 768 KB is copied into PSRAM while the LVGL lock is held and
 *     the bytes are streamed after it is released. Holding the lock across a Wi-Fi send
 *     would freeze the screen for seconds; reading the panel's buffer without a copy would
 *     let the reels tear the rows mid-transfer.
 *
 * Called from the appliance before http_server_start(); the screen comes up later, so a
 * request that arrives before it answers 503.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "cassette_view.h"
#include "http_server.h"
#include "panel_rgb.h"
#include "screen_display.h"

static const char *TAG = "screen";

#define BMP_HEADER_BYTES 54

/* ------------------------------------------------------------------ the frame */

/* A complete, current copy of the panel's picture, in PSRAM, owned by the caller. NULL
 * when the screen is not up or PSRAM is short. */
static uint16_t *frame_grab(int *w, int *h)
{
    *w = 0;
    *h = 0;

    lv_display_t *disp = lv_display_get_default();
    if (!disp || !lvgl_port_lock(1000)) return NULL;

    for (int i = 0; i < 2; i++) {
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(disp);
    }

    const uint16_t *px = NULL;
    int fw = 0;
    int fh = 0;
    bool ok = panel_rgb_frame(&px, &fw, &fh);
    uint16_t *copy = NULL;
    if (ok && fw > 0 && fh > 0) {
        size_t bytes = (size_t)fw * (size_t)fh * 2u;
        copy = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (copy) {
            memcpy(copy, px, bytes);
            *w = fw;
            *h = fh;
        } else {
            ESP_LOGE(TAG, "no PSRAM for the %u KB screen copy", (unsigned)(bytes / 1024));
        }
    }
    lvgl_port_unlock();
    return copy;
}

/* -------------------------------------------------------------------- the BMP */

static void put_le16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
}

static void put_le32(uint8_t *p, uint32_t v)
{
    put_le16(p, v);
    put_le16(p + 2, v >> 16);
}

/* A 24-bit, bottom-up BITMAPINFOHEADER. Every row is w * 3 bytes and w is 800, so no row
 * needs the padding BMP asks for on other widths. */
static void bmp_header(uint8_t *h, int w, int height)
{
    const uint32_t image = (uint32_t)w * (uint32_t)height * 3u;
    memset(h, 0, BMP_HEADER_BYTES);

    h[0] = 'B';
    h[1] = 'M';
    put_le32(h + 2, BMP_HEADER_BYTES + image);   /* file size */
    put_le32(h + 10, BMP_HEADER_BYTES);          /* offset to the pixels */
    put_le32(h + 14, 40);                        /* header size */
    put_le32(h + 18, (uint32_t)w);
    put_le32(h + 22, (uint32_t)height);          /* positive: bottom-up */
    put_le16(h + 26, 1);                         /* planes */
    put_le16(h + 28, 24);                        /* bits per pixel */
    put_le32(h + 34, image);
    put_le32(h + 38, 2835);                      /* 72 dpi, as pixels per metre */
    put_le32(h + 42, 2835);
}

/* RGB565 (LVGL's, little-endian) into one BMP row, B first. The 5- and 6-bit fields are
 * widened by repeating their top bits, which is the full-range expansion. */
static void row_bgr(uint8_t *out, const uint16_t *px, int w)
{
    for (int x = 0; x < w; x++) {
        uint16_t v = px[x];
        uint8_t r = (uint8_t)((v >> 11) & 0x1f);
        uint8_t g = (uint8_t)((v >> 5) & 0x3f);
        uint8_t b = (uint8_t)(v & 0x1f);
        out[x * 3 + 0] = (uint8_t)((b << 3) | (b >> 2));
        out[x * 3 + 1] = (uint8_t)((g << 2) | (g >> 4));
        out[x * 3 + 2] = (uint8_t)((r << 3) | (r >> 2));
    }
}

static http_result_t screen_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (strcmp(req->path, "/api/screen.bmp") != 0) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }

    int w = 0;
    int h = 0;
    uint16_t *px = frame_grab(&w, &h);
    if (!px) {
        http_send_error(fd, 503, "the screen is not up yet, or PSRAM is short");
        return HTTP_HANDLED;
    }

    uint8_t header[BMP_HEADER_BYTES];
    bmp_header(header, w, h);

    uint8_t *row = malloc((size_t)w * 3u);
    if (!row) {
        free(px);
        http_send_error(fd, 503, "no memory for a row buffer");
        return HTTP_HANDLED;
    }

    http_send_timeout_ms(fd, 20000);   /* 1.15 MB over Wi-Fi, with room for a slow link */
    bool sent = http_send_headers(fd, 200, "image/bmp", BMP_HEADER_BYTES + (size_t)w * (size_t)h * 3u,
                                  "Content-Disposition: inline; filename=\"screen.bmp\"\r\n"
                                  "Cache-Control: no-store\r\n") == ESP_OK &&
                http_send_all(fd, header, sizeof header);
    /* Bottom-up: the last row of the frame is the first row of the file. */
    for (int y = h - 1; sent && y >= 0; y--) {
        row_bgr(row, px + (size_t)y * (size_t)w, w);
        sent = http_send_all(fd, row, (size_t)w * 3u);
    }

    free(row);
    free(px);
    ESP_LOGI(TAG, "GET /api/screen.bmp -> %dx%d%s", w, h, sent ? "" : " (client stopped)");
    return HTTP_HANDLED;
}

/* ------------------------------------------------------------------ the scene */

/* "n=6", the only query this route takes. Anything else is 400: a scene number that was
 * silently ignored would photograph the wrong picture and look like a bug in the screen. */
static http_result_t scene_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    /* `query` is an array, so it is never NULL - only empty. */
    const char *q = strstr(req->query, "n=");
    char *end = NULL;
    long n = q ? strtol(q + 2, &end, 10) : -1;
    if (!q || !end || end == q + 2 || n < 0 || n > CASSETTE_SCENE_MAX) {
        http_send_error(fd, 400, "n must be 0..8");
        return HTTP_HANDLED;
    }

    if (!lvgl_port_lock(1000)) {
        http_send_error(fd, 503, "the screen did not give its lock");
        return HTTP_HANDLED;
    }
    cassette_view_scene_set((int)n);
    lvgl_port_unlock();

    char body[24];
    int len = snprintf(body, sizeof body, "scene %ld\n", n);
    if (len < 0) len = 0;
    if (http_send_headers(fd, 200, "text/plain; charset=utf-8", (size_t)len, NULL) != ESP_OK ||
        !http_send_all(fd, body, (size_t)len))
        return HTTP_HANDLED;
    return HTTP_HANDLED;
}

/* ------------------------------------------------------------------- the routes */

esp_err_t screen_display_http_start(void)
{
    esp_err_t err = http_server_register_route("/api/screen.bmp", screen_route, NULL);
    if (err != ESP_OK) return err;
    err = http_server_register_route_method("/api/scene", "POST", scene_route, NULL);
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "screen diagnostics on /api/screen.bmp and POST /api/scene?n=0..7");
    return ESP_OK;
}
