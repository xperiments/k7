/*
 * artwork_loader.c - the card, the JPEG decoder and the Artwork screen's cover, on
 * their own task.
 *
 * The picture is located by components/portable/artwork (an ID3v2.3/2.4 APIC frame
 * in an MP3, a native FLAC PICTURE block) and decoded by TJpgDec, which LVGL already
 * vendors. The decoder is called directly rather than through LVGL's image decoder:
 * that one wants a file or the memfs driver, and its band-at-a-time interface does not
 * produce the single fitted buffer the view needs. TJpgDec reads its input a few
 * hundred bytes at a time, so the Track's JPEG is read from the card on demand and
 * never sits in PSRAM whole.
 *
 * The screen is the cassette case, assets/labels/cover.jpg: an opaque 800x480 JPEG
 * embedded in the image and decoded once at start into a PSRAM backdrop. The Track
 * artwork is fitted to the case's window, 603x385 at (101,23), and drawn over it.
 *
 * Memory is the project's scarce resource: the app's rodata and instructions live in
 * PSRAM (moving them to flash brought the panel's glitches back), which leaves about
 * 1.7 MB free when the screen starts. The cover is therefore one 603x385 RGB565 buffer
 * (464 310 B); the view hides the picture when a new Track is requested, so the decode
 * writes it while it is not drawn. A JPEG whose crop is at least the window's size is
 * sampled from TJpgDec's MCU bands into that buffer, box-filtered to 603x385; a smaller
 * cover is decoded into a crop buffer no larger than the window and upscaled with the
 * portable fit. The band or the crop buffer is allocated per decode and freed; the Case
 * is the only other permanent buffer, 768 000 B.
 *
 * The card is shared with the broadcast, so the read backs off while the stream is
 * behind (the same signal the download path uses) and yields every 16 KB: the VFS holds
 * the volume lock for each fread.
 *
 * Only JPEG is decoded. A PNG picture is located and refused with a log line: it is
 * rare in an MP3, and the cover path does not need LodePNG's full-size buffer.
 */
#include "artwork_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "artwork.h"
#include "cassette_view.h"
#include "cover_jpg.h"
#include "mini_icecast.h"

/* Vendored inside LVGL; its private include directory is named by CMakeLists.txt. */
#include "tjpgd.h"

static const char *TAG = "artwork";

/* The Case's window: the rectangle the Track artwork fills, (101,23) to (704,408). */
#define COVER_W 603
#define COVER_H 385
#define COVER_BYTES ((size_t)COVER_W * COVER_H * 2)

#define LOADER_TASK_STACK (16 * 1024)
#define LOADER_TASK_PRIO  2
#define LOADER_CORE       0

/* TJpgDec's own working pool: small and touched for every MCU, so internal RAM. */
#define JPEG_POOL_BYTES 4096
/* The decoder reads the source at full size (this LVGL build has JD_USE_SCALE off),
 * so an absurd header is refused rather than decoded: 4096 px a side is 16 M pixels,
 * already far beyond any cover this screen can show. */
#define JPEG_MAX_EDGE 4096
/* One fread per chunk, then yield. See the header comment. */
#define READ_CHUNK (16 * 1024)

static SemaphoreHandle_t s_wake;
static SemaphoreHandle_t s_lock;
static bool s_started;

static char s_music_dir[80];

/* The latest request; replaced, never queued. */
static char s_req_path[LIB_PATH_MAX];
static mi_fmt_t s_req_fmt;
static uint32_t s_req_seq;

/* The cover: one 603x385 buffer, allocated once for the life of the process. The view
 * hides the picture when a new Track is requested, so the decode writes it while it is
 * not being drawn; a second buffer would cost 464 KB this board does not have. */
static uint16_t *s_art_buf;
static lv_image_dsc_t s_art_dsc;

/* The Case backdrop: the embedded cover.jpg, decoded once at start. */
static uint16_t *s_case;
static lv_image_dsc_t s_case_dsc;

/* ------------------------------------------------------------------ card access */

static uint32_t current_seq(void); /* the request counter, below */

static uint64_t file_size(FILE *f)
{
    long cur = ftell(f);
    if (cur < 0) return 0;
    if (fseek(f, 0, SEEK_END) != 0) return 0;
    long end = ftell(f);
    if (fseek(f, cur, SEEK_SET) != 0) return 0;
    return end > 0 ? (uint64_t)end : 0;
}

/* A header read: small, so one fread is fine. */
static bool file_read_at(void *ctx, uint64_t off, void *dst, size_t n)
{
    FILE *f = ctx;
    if (off > 0x7FFFFFFF) return false;
    if (fseek(f, (long)off, SEEK_SET) != 0) return false;
    return fread(dst, 1, n, f) == n;
}

/* Leave the card alone while the broadcast is behind. */
static void card_wait(void)
{
    for (int i = 0; i < 20 && mini_icecast_lag_ms() > 0; i++) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* --------------------------------------------------------------------- JPEG */

typedef struct {
    /* A Track's JPEG is read from the card on demand: the decoder wants a few hundred
     * bytes at a time, so the picture never has to sit in PSRAM whole. The Case is
     * embedded in the image and read from that buffer. */
    FILE          *file;
    uint64_t       file_left;
    const uint8_t *bytes;
    size_t         n;
    size_t         pos;
    bool           from_file;
    uint32_t       read_bytes;   /* since the last yield */

    uint16_t      *dst;          /* the crop buffer, when the picture is smaller than the cover */
    artwork_cover_t cover;       /* the fit; also carries the crop in the upscale path */
    bool           streaming;    /* true: the fit writes s_art_buf straight from the MCUs */
    bool           failed;

    uint32_t       seq;          /* the request that started this decode */
    uint32_t       rects;        /* MCUs seen, for the supersede check */
    bool           check_seq;    /* false for the Case: a Track request must not abort it */
} jpeg_ctx_t;

/* One decode at a time; the fit's workspace is about 10 KB, so BSS rather than the
 * task's 16 KB stack. */
static jpeg_ctx_t s_jpeg;

static inline uint16_t rgb565(uint32_t r, uint32_t g, uint32_t b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static size_t jpeg_in(JDEC *jd, uint8_t *buff, size_t ndata)
{
    jpeg_ctx_t *j = jd->device;

    if (j->from_file) {
        if (ndata > j->file_left) ndata = (size_t)j->file_left;
        if (ndata == 0) return 0;
        size_t got;
        if (buff) {
            got = fread(buff, 1, ndata, j->file);
            if (got == 0) return 0;
            j->read_bytes += (uint32_t)got;
            /* The VFS serialises one fread at a time; 16 KB of them is a fair slice. */
            if (j->read_bytes >= READ_CHUNK) {
                j->read_bytes = 0;
                if (mini_icecast_lag_ms() > 0) vTaskDelay(pdMS_TO_TICKS(10));
                else vTaskDelay(1);
            }
        } else {
            if (fseek(j->file, (long)ndata, SEEK_CUR) != 0) return 0;
            got = ndata;
        }
        j->file_left -= got;
        return got;
    }

    size_t left = j->n - j->pos;
    if (ndata > left) ndata = left;
    if (buff) memcpy(buff, j->bytes + j->pos, ndata);
    j->pos += ndata;
    return ndata;
}

/* TJpgDec hands back an MCU band at a time, RGB888: every column of 8 or 16 source
 * rows before the next band. The streaming fit buffers the band and averages it; the
 * upscale path copies the crop into a buffer the portable bilinear fit can read. */
static int jpeg_out(JDEC *jd, void *bitmap, JRECT *r)
{
    jpeg_ctx_t *j = jd->device;
    const uint32_t w = (uint32_t)(r->right - r->left + 1);

    /* A newer Track abandons the decode; checking every 32 MCUs keeps the mutex out
     * of the per-MCU path and still makes a skip responsive. The Case decode is not a
     * Track request: a boot touch must not abort it. */
    if (j->check_seq && ((++j->rects) & 31u) == 0 && j->seq != current_seq()) {
        j->failed = true;
        return 0;
    }

    if (j->streaming) {
        if (!artwork_cover_rect(&j->cover, bitmap, r->left, r->top, w,
                                (uint32_t)(r->bottom - r->top + 1))) {
            j->failed = true;
            return 0;
        }
        return 1;
    }

    const uint8_t *src = bitmap;
    const artwork_cover_t *c = &j->cover;
    for (uint32_t y = r->top; y <= r->bottom; y++) {
        if (y < c->cy || y >= c->cy + c->ch) {
            src += w * 3;
            continue;
        }
        uint16_t *dstrow = j->dst + (size_t)(y - c->cy) * c->cw;
        const uint8_t *p = src;
        for (uint32_t x = r->left; x <= r->right; x++, p += 3) {
            if (x < c->cx || x >= c->cx + c->cw) continue;
            dstrow[x - c->cx] = rgb565(p[2], p[1], p[0]);
        }
        src += w * 3;
    }
    return 1;
}

/* Decode a Track's JPEG from the card into `dst`, fitted to 603x385. The picture is
 * read on demand, so neither the encoded bytes nor a full-size RGB copy are held in
 * PSRAM: the only workspace is the band for a streaming fit or the crop buffer for an
 * upscale. */
static bool decode_jpeg_file(FILE *f, uint64_t off, uint32_t size,
                             uint32_t seq, uint16_t *dst)
{
    if (!f || !dst || size == 0 || off > 0x7FFFFFFF) return false;
    if (fseek(f, (long)off, SEEK_SET) != 0) return false;

    jpeg_ctx_t *j = &s_jpeg;
    memset(j, 0, sizeof *j);
    j->from_file = true;
    j->file = f;
    j->file_left = size;
    j->seq = seq;
    j->check_seq = true;

    uint8_t *pool = heap_caps_malloc(JPEG_POOL_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!pool) {
        ESP_LOGW(TAG, "jpeg: no internal RAM for the decoder pool");
        return false;
    }

    JDEC jd;
    JRESULT rc = jd_prepare(&jd, jpeg_in, pool, JPEG_POOL_BYTES, j);
    if (rc != JDR_OK) {
        ESP_LOGW(TAG, "jpeg: jd_prepare failed (%d)", (int)rc);
        heap_caps_free(pool);
        return false;
    }
    if (jd.width == 0 || jd.height == 0 ||
        jd.width > JPEG_MAX_EDGE || jd.height > JPEG_MAX_EDGE) {
        ESP_LOGW(TAG, "jpeg: %ux%u is outside the decoder's bounds",
                 (unsigned)jd.width, (unsigned)jd.height);
        heap_caps_free(pool);
        return false;
    }

    bool ok = false;
    const bool streaming = artwork_cover_streams(jd.width, jd.height, COVER_W, COVER_H);
    if (streaming) {
        /* The band is sized to this picture, not to the largest one the decoder accepts:
         * a 1000 px cover needs 48 KB, and the card heap has little room left. */
        const size_t band_bytes = (size_t)jd.width * ARTWORK_COVER_BAND_ROWS * 3;
        uint8_t *band = heap_caps_malloc(band_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!band) {
            ESP_LOGW(TAG, "jpeg: no PSRAM for the %u B decode band (free %u, largest %u)",
                     (unsigned)band_bytes,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            heap_caps_free(pool);
            return false;
        }
        if (!artwork_cover_begin(&j->cover, jd.width, jd.height, dst, COVER_W, COVER_H,
                                 band, ARTWORK_COVER_BAND_ROWS)) {
            heap_caps_free(band);
            heap_caps_free(pool);
            return false;
        }
        j->streaming = true;
        rc = jd_decomp(&jd, jpeg_out, 0);
        if (rc == JDR_OK && !j->failed && artwork_cover_end(&j->cover)) {
            ok = true;
        } else {
            ESP_LOGW(TAG, "jpeg: jd_decomp failed (%d)", (int)rc);
        }
        heap_caps_free(band);
    } else {
        /* Smaller than the window: decode the crop and upscale it bilinearly. The crop
         * is the window's aspect, so the fit is a straight resample. */
        artwork_cover_begin(&j->cover, jd.width, jd.height, dst, COVER_W, COVER_H, NULL, 0);
        uint16_t *tmp = heap_caps_malloc((size_t)j->cover.cw * j->cover.ch * 2,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!tmp) {
            ESP_LOGW(TAG, "jpeg: no PSRAM for a %ux%u crop",
                     (unsigned)j->cover.cw, (unsigned)j->cover.ch);
            heap_caps_free(pool);
            return false;
        }
        j->streaming = false;
        j->dst = tmp;
        rc = jd_decomp(&jd, jpeg_out, 0);
        if (rc == JDR_OK && !j->failed) {
            ok = artwork_scale_cover(tmp, j->cover.cw, j->cover.ch,
                                     dst, COVER_W, COVER_H);
        } else {
            ESP_LOGW(TAG, "jpeg: jd_decomp failed (%d)", (int)rc);
        }
        heap_caps_free(tmp);
    }

    heap_caps_free(pool);
    return ok;
}

/* Decode the embedded Case backdrop once, into PSRAM, and hand it to the view. The
 * JPEG is the panel's own 800x480, so this is a straight 1:1 decode. */
static bool decode_case(void)
{
    const size_t bytes = (size_t)COVER_JPG_W * COVER_JPG_H * 2;
    s_case = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_case) {
        ESP_LOGE(TAG, "no PSRAM for the %u B case backdrop", (unsigned)bytes);
        return false;
    }

    jpeg_ctx_t *j = &s_jpeg;
    memset(j, 0, sizeof *j);
    j->bytes = cover_jpg_start;
    j->n = (size_t)(cover_jpg_end - cover_jpg_start);
    j->dst = s_case;

    uint8_t *pool = heap_caps_malloc(JPEG_POOL_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!pool) {
        ESP_LOGE(TAG, "no internal RAM for the case decoder pool");
        heap_caps_free(s_case);
        s_case = NULL;
        return false;
    }

    JDEC jd;
    JRESULT rc = jd_prepare(&jd, jpeg_in, pool, JPEG_POOL_BYTES, j);
    if (rc != JDR_OK) {
        ESP_LOGE(TAG, "case backdrop: jd_prepare failed (%d)", (int)rc);
        heap_caps_free(pool);
        heap_caps_free(s_case);
        s_case = NULL;
        return false;
    }
    if (jd.width != COVER_JPG_W || jd.height != COVER_JPG_H) {
        ESP_LOGE(TAG, "case backdrop is %ux%u, expected %ux%u",
                 (unsigned)jd.width, (unsigned)jd.height, COVER_JPG_W, COVER_JPG_H);
        heap_caps_free(pool);
        heap_caps_free(s_case);
        s_case = NULL;
        return false;
    }

    j->cover.cx = 0;
    j->cover.cy = 0;
    j->cover.cw = jd.width;
    j->cover.ch = jd.height;
    j->streaming = false;
    rc = jd_decomp(&jd, jpeg_out, 0);
    heap_caps_free(pool);
    if (rc != JDR_OK || j->failed) {
        ESP_LOGE(TAG, "case backdrop: jd_decomp failed (%d)", (int)rc);
        heap_caps_free(s_case);
        s_case = NULL;
        return false;
    }

    memset(&s_case_dsc, 0, sizeof s_case_dsc);
    s_case_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_case_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_case_dsc.header.w = COVER_JPG_W;
    s_case_dsc.header.h = COVER_JPG_H;
    s_case_dsc.header.stride = COVER_JPG_W * 2;
    s_case_dsc.data_size = (uint32_t)bytes;
    s_case_dsc.data = (const uint8_t *)s_case;

    if (lvgl_port_lock(1000)) {
        cassette_view_set_case(&s_case_dsc);
        lvgl_port_unlock();
    }
    return true;
}

/* ------------------------------------------------------------------ the picture */

static uint32_t current_seq(void)
{
    uint32_t seq = 0;
    if (s_lock && xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        seq = s_req_seq;
        xSemaphoreGive(s_lock);
    }
    return seq;
}

/* Hand the single buffer to the view under the LVGL lock. The view hid the picture when
 * the request was made, so the decode wrote it while the panel was not drawing it. */
static void apply_cover(bool have)
{
    if (!s_art_buf) return;
    if (!lvgl_port_lock(1000)) return;
    cassette_view_set_artwork(have ? &s_art_dsc : NULL);
    lvgl_port_unlock();
}

static void render_art(const char *track_path, mi_fmt_t fmt, uint32_t seq)
{
    bool ok = false;
    uint64_t wait_ms = 0, open_ms = 0, decode_ms = 0;

    if (track_path[0] && fmt != MI_FMT_NONE && s_art_buf) {
        int64_t t_wait = esp_timer_get_time();
        card_wait();
        wait_ms = (uint64_t)((esp_timer_get_time() - t_wait) / 1000);

        char path[sizeof s_music_dir + LIB_PATH_MAX + 2];
        snprintf(path, sizeof path, "%s/%s", s_music_dir, track_path);

        int64_t t_open = esp_timer_get_time();
        FILE *f = fopen(path, "rb");
        open_ms = (uint64_t)((esp_timer_get_time() - t_open) / 1000);

        if (!f) {
            ESP_LOGW(TAG, "%s: cannot open the Track; no cover", track_path);
        } else {
            uint64_t size = file_size(f);
            artwork_ref_t ref;
            artwork_result_t r = artwork_locate(fmt, file_read_at, f, size, &ref);
            if (r == ARTWORK_OK) {
                if (ref.png) {
                    ESP_LOGI(TAG, "%s: %u B PNG picture; only JPEG is decoded",
                             track_path, (unsigned)ref.size);
                } else {
                    int64_t t_decode = esp_timer_get_time();
                    ok = decode_jpeg_file(f, ref.offset, ref.size, seq, s_art_buf);
                    decode_ms = (uint64_t)((esp_timer_get_time() - t_decode) / 1000);
                }
            } else if (r != ARTWORK_NONE) {
                ESP_LOGI(TAG, "%s: %s", track_path,
                         r == ARTWORK_UNSUPPORTED ? "a picture, but not a usable JPEG"
                         : r == ARTWORK_IO ? "read failed"
                         : "the container is malformed");
            }
            fclose(f);
        }
    }

    /* A newer request has replaced this one: throw the result away instead of showing
     * a picture for a Track that is no longer playing. */
    bool current = seq == current_seq();
    if (current) apply_cover(ok);

    ESP_LOGI(TAG, "%s: %s (wait %llu ms, open %llu ms, read+decode %llu ms, %s)",
             track_path[0] ? track_path : "(none)",
             ok ? "603x385 cover" : "no cover",
             (unsigned long long)wait_ms, (unsigned long long)open_ms,
             (unsigned long long)decode_ms,
             current ? "shown" : "superseded");
}

/* ------------------------------------------------------------------ the task */

static void loader_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "artwork task on core %d", (int)xPortGetCoreID());
    for (;;) {
        xSemaphoreTake(s_wake, portMAX_DELAY);

        char path[LIB_PATH_MAX];
        mi_fmt_t fmt = MI_FMT_NONE;
        uint32_t seq = 0;
        if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
            snprintf(path, sizeof path, "%s", s_req_path);
            fmt = s_req_fmt;
            seq = s_req_seq;
            xSemaphoreGive(s_lock);
        }
        render_art(path, fmt, seq);
    }
}

esp_err_t artwork_loader_start(const char *music_dir)
{
    if (!music_dir || !music_dir[0]) return ESP_ERR_INVALID_ARG;
    if (s_started) return ESP_OK;
    snprintf(s_music_dir, sizeof s_music_dir, "%s", music_dir);

    s_lock = xSemaphoreCreateMutex();
    s_wake = xSemaphoreCreateBinary();
    if (!s_lock || !s_wake) {
        ESP_LOGE(TAG, "no memory for the artwork request channel");
        return ESP_ERR_NO_MEM;
    }

    /* The Case backdrop first: one decode, and the Artwork screen is empty without it.
     * The task is not running yet, so this can use the same decoder state. */
    if (!decode_case()) {
        ESP_LOGW(TAG, "the Case backdrop did not decode; the Artwork screen keeps its flat background");
    }

    /* One cover buffer, not two: with the app's rodata in PSRAM (the panel needs it
     * there: moving it to flash brought the glitches back) the heap has about 1.7 MB
     * free at this point, and two 464 KB buffers plus a band did not fit. The view
     * hides the picture when a new Track is requested, so a decode into the one buffer
     * is never drawn. */
    ESP_LOGI(TAG, "artwork memory before the cover: psram free %u B, largest %u B",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    s_art_buf = heap_caps_malloc(COVER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_art_buf) {
        ESP_LOGE(TAG, "no PSRAM for the %u B cover (free %u, largest %u); "
                      "the Artwork screen will stay empty",
                 (unsigned)COVER_BYTES,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        return ESP_ERR_NO_MEM;
    }
    memset(&s_art_dsc, 0, sizeof s_art_dsc);
    s_art_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_art_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_art_dsc.header.w = COVER_W;
    s_art_dsc.header.h = COVER_H;
    s_art_dsc.header.stride = COVER_W * 2;
    s_art_dsc.data_size = (uint32_t)COVER_BYTES;
    s_art_dsc.data = (const uint8_t *)s_art_buf;
    ESP_LOGI(TAG, "artwork memory after the cover: psram free %u B, largest %u B",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    /* The stack goes to PSRAM first: the encoder and Wi-Fi own the internal margin,
     * and the decode is not on either core's critical path. */
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(loader_task, "artwork",
                                                    LOADER_TASK_STACK, NULL,
                                                    LOADER_TASK_PRIO, NULL, LOADER_CORE,
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        ok = xTaskCreatePinnedToCoreWithCaps(loader_task, "artwork", LOADER_TASK_STACK,
                                             NULL, LOADER_TASK_PRIO, NULL, LOADER_CORE,
                                             MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "no memory for the artwork task");
        heap_caps_free(s_art_buf);
        s_art_buf = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    ESP_LOGI(TAG, "cover buffer %u B in PSRAM, 603x385, JPEG only",
             (unsigned)COVER_BYTES);
    return ESP_OK;
}

void artwork_loader_request(const char *track_path, mi_fmt_t format)
{
    if (!s_lock) return;
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return;
    snprintf(s_req_path, sizeof s_req_path, "%s", track_path ? track_path : "");
    s_req_fmt = format;
    s_req_seq++;
    xSemaphoreGive(s_lock);
    if (s_wake) xSemaphoreGive(s_wake);
}
