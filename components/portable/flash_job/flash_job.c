#include "flash_job.h"

#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* Serving a page runs SPIFFS (fopen/read) on this task, and a VFS open alone costs a
 * kilobyte or two; 6 KB leaves the SPIFFS walk comfortable room. */
#define FLASH_JOB_STACK_BYTES 6144
#define FLASH_JOB_DEFAULT_TIMEOUT_MS 8000u

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

bool flash_job_run(bool (*fn)(void *ctx), void *ctx, unsigned timeout_ms)
{
    if (!fn) return false;
    if (!s_task) return fn(ctx); /* boot-time callers, on an internal stack already */
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return false;
    s_fn = fn;
    s_ctx = ctx;
    s_result = false;
    xSemaphoreGive(s_go);
    unsigned wait = timeout_ms ? timeout_ms : FLASH_JOB_DEFAULT_TIMEOUT_MS;
    bool done = xSemaphoreTake(s_done, pdMS_TO_TICKS(wait)) == pdTRUE;
    bool result = done && s_result;
    xSemaphoreGive(s_lock);
    return result;
}
