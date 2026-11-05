#include "flash_job.h"

#include <stddef.h>

#include "esp_log.h"

static const char *TAG = "flashjob";

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* The Snapshot job reads the update file and writes the partition on this task; 6 KB
 * leaves that call chain comfortable room. */
#define FLASH_JOB_STACK_BYTES 6144
/* Long enough that a normal job never reaches it, short enough to notice a wedged one. */
#define FLASH_JOB_SLOW_MS 3000u

static SemaphoreHandle_t s_go;
static SemaphoreHandle_t s_done;
static SemaphoreHandle_t s_lock;
static TaskHandle_t      s_task;
static bool (*volatile s_fn)(void *ctx);
static void *volatile   s_ctx;
static volatile bool    s_result;

static void writer(void *arg)
{
    (void)arg;
    for (;;) {
        if (xSemaphoreTake(s_go, portMAX_DELAY) != pdTRUE) continue;
        s_result = s_fn ? s_fn(s_ctx) : false;
        s_fn = NULL;
        s_ctx = NULL;
        xSemaphoreGive(s_done);
    }
}

bool flash_job_start(void)
{
    if (s_task) return true;
    s_go = xSemaphoreCreateBinary();
    s_done = xSemaphoreCreateBinary();
    s_lock = xSemaphoreCreateMutex();
    if (!s_go || !s_done || !s_lock) return false;
    /* No capability caps: the default stack is internal DRAM, which is the point. */
    if (xTaskCreatePinnedToCore(writer, "flashjob", FLASH_JOB_STACK_BYTES, NULL, 4, &s_task, 1) !=
        pdPASS) {
        s_task = NULL;
        return false;
    }
    return true;
}

bool flash_job_run(bool (*fn)(void *ctx), void *ctx)
{
    if (!fn) return false;
    if (!s_task) return fn(ctx); /* boot-time callers, on an internal stack already */
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return false;
    s_fn = fn;
    s_ctx = ctx;
    s_result = false;
    xSemaphoreGive(s_go);
    /* No timeout: the closure is usually the caller's stack frame, and returning to it
     * while the writer still reads it is a use-after-return. A slow job is logged and
     * waited for; the alternative is a corrupted page or a torn Snapshot. */
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(FLASH_JOB_SLOW_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "a flash job is taking longer than %u ms", (unsigned)FLASH_JOB_SLOW_MS);
        xSemaphoreTake(s_done, portMAX_DELAY);
    }
    bool result = s_result;
    xSemaphoreGive(s_lock);
    return result;
}
