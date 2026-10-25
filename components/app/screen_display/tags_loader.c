/*
 * tags_loader.c - the Track's group/song tags for the peek, on their own task.
 *
 * The tag half of the old artwork loader. The cover/JPEG/Case pipeline is parked in
 * artwork_loader.c and is not in the build; the Label cover and the Artwork screen are
 * gone. The parser is components/portable/artwork's tags.c, which reads an ID3v2.3/2.4
 * text frame or a FLAC Vorbis comment through a read callback, so the bytes never have to
 * sit in memory whole.
 *
 * A request is a path, its format and a sequence, replaced rather than queued. The task
 * opens the Track, reads the few hundred bytes the tags need, drops the result if a newer
 * Track arrived meanwhile, and hands it to the view under the LVGL lock.
 */
#include "tags_loader.h"

#include <stdio.h>
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

static const char *TAG = "tags";

#define TAGS_TASK_STACK (8 * 1024)
#define TAGS_TASK_PRIO  2
#define TAGS_CORE       0

static SemaphoreHandle_t s_wake;
static SemaphoreHandle_t s_lock;
static bool s_started;

static char s_music_dir[80];

/* The latest request; replaced, never queued. */
static char s_req_path[LIB_PATH_MAX];
static mi_fmt_t s_req_fmt;
static uint32_t s_req_seq;

/* ------------------------------------------------------------------ card access */

static uint32_t current_seq(void)
{
    uint32_t seq = 0;
    if (s_lock && xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        seq = s_req_seq;
        xSemaphoreGive(s_lock);
    }
    return seq;
}

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

/* ------------------------------------------------------------------ the tags */

/* Hand the tags to the view under the LVGL lock. Empty fields are a real answer (the
 * Track has no tags) and let the peek fall back to its path/filename. */
static void apply_tags(const artwork_tags_t *tags)
{
    if (!lvgl_port_lock(1000)) return;
    cassette_view_set_tags(tags->group, tags->song);
    lvgl_port_unlock();
}

/* Read the Track's group/song tags. One open, a header and the two frames (or one
 * Vorbis comment block), then the card is released. A newer Track's request makes the
 * result stale, so it is dropped rather than shown against the wrong song. */
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

/* ------------------------------------------------------------------ the task */

static void tags_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "tags task on core %d", (int)xPortGetCoreID());
    for (;;) {
        xSemaphoreTake(s_wake, portMAX_DELAY);

        char path[LIB_PATH_MAX];
        mi_fmt_t fmt = MI_FMT_NONE;
        uint32_t seq = 0;
        bool do_tags = false;
        if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
            snprintf(path, sizeof path, "%s", s_req_path);
            fmt = s_req_fmt;
            seq = s_req_seq;
            do_tags = true;
            xSemaphoreGive(s_lock);
        }
        if (do_tags) render_tags(path, fmt, seq);
    }
}

esp_err_t tags_loader_start(const char *music_dir)
{
    if (!music_dir || !music_dir[0]) return ESP_ERR_INVALID_ARG;
    if (s_started) return ESP_OK;
    snprintf(s_music_dir, sizeof s_music_dir, "%s", music_dir);

    s_lock = xSemaphoreCreateMutex();
    s_wake = xSemaphoreCreateBinary();
    if (!s_lock || !s_wake) {
        ESP_LOGE(TAG, "no memory for the tags request channel");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(tags_task, "tags",
                                                    TAGS_TASK_STACK, NULL,
                                                    TAGS_TASK_PRIO, NULL, TAGS_CORE,
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        ok = xTaskCreatePinnedToCoreWithCaps(tags_task, "tags", TAGS_TASK_STACK,
                                             NULL, TAGS_TASK_PRIO, NULL, TAGS_CORE,
                                             MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "no memory for the tags task");
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    return ESP_OK;
}

void tags_loader_request(const char *track_path, mi_fmt_t format)
{
    if (!s_lock) return;
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return;
    const char *path = track_path ? track_path : "";
    if (strcmp(path, s_req_path) != 0 || format != s_req_fmt) {
        snprintf(s_req_path, sizeof s_req_path, "%s", path);
        s_req_fmt = format;
        s_req_seq++;
    }
    xSemaphoreGive(s_lock);
    if (s_wake) xSemaphoreGive(s_wake);
}
