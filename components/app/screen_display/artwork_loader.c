/*
 * artwork_loader.c - the card, the JPEG decoder, the white page's cover and the peek's
 * tags, on their own task.
 *
 * The picture is located by components/portable/artwork (an ID3v2.3/2.4 APIC frame
 * in an MP3, a native FLAC PICTURE block) and decoded by TJpgDec, which LVGL already
 * vendors. The decoder is called directly rather than through LVGL's image decoder:
 * that one wants a file or the memfs driver, and its band-at-a-time interface does not
 * produce the single fitted buffer the view needs. TJpgDec reads its input a few
 * hundred bytes at a time, so the Track's JPEG is read from the card on demand and
 * never sits in PSRAM whole.
 *
 * The picture is fitted, cover-style, to one 603x385 RGB565 buffer. That was the old
 * Artwork screen's window; the white controls page now scales the same buffer to the
 * cassette's outer frame, so the decode and the fit did not change when the screen did.
 * The view hides the picture when a new Track is requested, so the decode writes the
 * buffer while it is not drawn.
 *
 * Memory is the project's scarce resource: the app's rodata and instructions live in
 * PSRAM (moving them to flash brought the panel's glitches back), which leaves about
 * 1.7 MB free when the screen starts. The cover is therefore one 603x385 buffer
 * (464 310 B). A JPEG whose crop is at least the window's size is sampled from TJpgDec's
 * MCU bands into that buffer, box-filtered to 603x385; a smaller cover is decoded into a
 * crop buffer no larger than the window and upscaled with the portable fit. The band or
 * the crop buffer is allocated per decode and freed; the cover buffer is the only
 * permanent picture buffer.
 *
 * The card is shared with the broadcast, so the read backs off while the stream is
 * behind (the same signal the download path uses) and yields every 16 KB: the VFS holds
 * the volume lock for each fread.
 *
 * Only JPEG is decoded. A PNG picture is located and refused with a log line: it is
 * rare in an MP3, and the cover path does not need LodePNG's full-size buffer.
 *
 * The text tags share the task and the request slot with the picture: one Track costs
 * one wake, and the tags - a few hundred bytes - are read first so the peek's labels
 * settle while the picture is still decoding.
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
#include "mini_icecast.h"

/* Vendored inside LVGL; its private include directory is named by CMakeLists.txt. */
#include "tjpgd.h"

static const char *TAG = "artwork";

/* The fitted picture's size. The white page scales it to the frame it is shown in; a
 * decode at the frame's size would need a different buffer at every pose. */
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

/* The latest request; replaced, never queued. The two flags say what the same Track
 * still needs, so a picture request and a tags request for one Track cost one wake. */
static char s_req_path[LIB_PATH_MAX];
static mi_fmt_t s_req_fmt;
static uint32_t s_req_seq;
static bool s_req_art;
static bool s_req_tags;

/* The cover: one 603x385 buffer, allocated once for the life of the process. The white
 * page hides the picture when a new Track is requested, so the decode writes it while it
 * is not being drawn; a second buffer would cost 464 KB this board does not have. */
static uint16_t *s_art_buf;
static lv_image_dsc_t s_art_dsc;

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
     * bytes at a time, so the picture never has to sit in PSRAM whole. */
    FILE          *file;
    uint64_t       file_left;
    uint32_t       read_bytes;   /* since the last yield */

    uint16_t      *dst;          /* the crop buffer, when the picture is smaller than the cover */
    artwork_cover_t cover;       /* the fit; also carries the crop in the upscale path */
    bool           streaming;    /* true: the fit writes s_art_buf straight from the MCUs */
    bool           failed;

    uint32_t       seq;          /* the request that started this decode */
    uint32_t       rects;        /* MCUs seen, for the supersede check */
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

/* TJpgDec hands back an MCU band at a time, RGB888: every column of 8 or 16 source
 * rows before the next band. The streaming fit buffers the band and averages it; the
 * upscale path copies the crop into a buffer the portable bilinear fit can read. */
static int jpeg_out(JDEC *jd, void *bitmap, JRECT *r)
{
    jpeg_ctx_t *j = jd->device;
    const uint32_t w = (uint32_t)(r->right - r->left + 1);

    /* A newer Track abandons the decode; checking every 32 MCUs keeps the mutex out
     * of the per-MCU path and still makes a skip responsive. */
    if (((++j->rects) & 31u) == 0 && j->seq != current_seq()) {
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
    j->file = f;
    j->file_left = size;
    j->seq = seq;

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
 * the request was made, so the decode wrote it while the panel was not drawing it. The
 * sequence is checked again under the lock: a newer Track may have arrived between the
 * decode's last check and this hand-off, and the picture must not land against it. */
static void apply_cover(uint32_t seq, bool have)
{
    if (!s_art_buf) return;
    if (!lvgl_port_lock(1000)) return;
    if (seq == current_seq()) cassette_view_set_cover(have ? &s_art_dsc : NULL);
    lvgl_port_unlock();
}

/* The text tags, handed in under the same lock as the picture. Empty fields are a real
 * answer (the Track has no tags) and let the peek fall back to its path/filename. */
static void apply_tags(const artwork_tags_t *tags)
{
    if (!lvgl_port_lock(1000)) return;
    cassette_view_set_tags(tags->group, tags->song);
    lvgl_port_unlock();
}

/* Read the Track's group/song tags. Small: one open, a header and the two frames (or
 * one Vorbis comment block), then the card is released. A newer Track's request makes
 * the result stale, so it is dropped rather than shown against the wrong song. */
static void render_tags(const char *track_path, mi_fmt_t fmt, uint32_t seq)
{
    artwork_tags_t tags;
    memset(&tags, 0, sizeof tags);
    bool ok = false;
    uint64_t open_ms = 0, read_ms = 0;

    if (track_path[0] && fmt != MI_FMT_NONE) {
        card_wait();

        char path[sizeof s_music_dir + LIB_PATH_MAX + 2];
        snprintf(path, sizeof path, "%s/%s", s_music_dir, track_path);

        int64_t t_open = esp_timer_get_time();
        FILE *f = fopen(path, "rb");
        open_ms = (uint64_t)((esp_timer_get_time() - t_open) / 1000);
        if (f) {
            uint64_t size = file_size(f);
            int64_t t_read = esp_timer_get_time();
            artwork_result_t r = artwork_tags(fmt, file_read_at, f, size, &tags);
            read_ms = (uint64_t)((esp_timer_get_time() - t_read) / 1000);
            fclose(f);
            ok = r == ARTWORK_OK;
            if (!ok && r != ARTWORK_NONE) {
                ESP_LOGI(TAG, "%s: tags: %s", track_path,
                         r == ARTWORK_UNSUPPORTED ? "an unsupported tag"
                         : r == ARTWORK_IO ? "read failed"
                         : "the container is malformed");
            }
        } else {
            ESP_LOGW(TAG, "%s: cannot open the Track; no tags", track_path);
        }
    }

    if (seq != current_seq()) {
        ESP_LOGI(TAG, "%s: tags superseded", track_path[0] ? track_path : "(none)");
        return;
    }

    apply_tags(&tags);
    ESP_LOGI(TAG, "%s: tags %s (group '%s', song '%s'; open %llu ms, read %llu ms)",
             track_path[0] ? track_path : "(none)", ok ? "read" : "empty",
             tags.group, tags.song,
             (unsigned long long)open_ms, (unsigned long long)read_ms);
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
     * a picture for a Track that is no longer playing. The hand-off repeats the check
     * under the LVGL lock, so the log's verdict is this one and the view's is the last. */
    bool current = seq == current_seq();
    apply_cover(seq, ok);

    ESP_LOGI(TAG, "%s: %s (wait %llu ms, open %llu ms, read+decode %llu ms, %s)",
             track_path[0] ? track_path : "(none)",
             ok ? "603x385 cover" : "no cover",
             (unsigned long long)wait_ms, (unsigned long long)open_ms,
             (unsigned long long)decode_ms,
             current ? "current" : "superseded");
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
        bool do_art = false, do_tags = false;
        if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
            snprintf(path, sizeof path, "%s", s_req_path);
            fmt = s_req_fmt;
            seq = s_req_seq;
            do_art = s_req_art;
            do_tags = s_req_tags;
            s_req_art = false;
            s_req_tags = false;
            xSemaphoreGive(s_lock);
        }
        /* The tags first: they are a few hundred bytes and the labels become right while
         * the picture is still decoding. A stale request does neither. */
        if (do_tags) render_tags(path, fmt, seq);
        if (do_art && seq == current_seq()) render_art(path, fmt, seq);
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
                      "the white page will show no cover",
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

/* One request channel: a path, its format, and what is still wanted for it. A new
 * Track replaces both flags and increments the sequence a decode checks; a second
 * request for the same Track only adds a flag, so a tags request cannot stale an
 * in-flight picture of that same Track. */
static void request(const char *track_path, mi_fmt_t format, bool art, bool tags)
{
    if (!s_lock) return;
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return;
    const char *path = track_path ? track_path : "";
    if (strcmp(path, s_req_path) != 0 || format != s_req_fmt) {
        snprintf(s_req_path, sizeof s_req_path, "%s", path);
        s_req_fmt = format;
        s_req_seq++;
        s_req_art = false;
        s_req_tags = false;
    }
    if (art) s_req_art = true;
    if (tags) s_req_tags = true;
    xSemaphoreGive(s_lock);
    if (s_wake) xSemaphoreGive(s_wake);
}

void artwork_loader_request(const char *track_path, mi_fmt_t format)
{
    request(track_path, format, true, false);
}

void artwork_loader_request_tags(const char *track_path, mi_fmt_t format)
{
    request(track_path, format, false, true);
}
