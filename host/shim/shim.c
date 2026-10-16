/*
 * The host build's operating-system stand-in.
 *
 * This file is the only place that knows how to be a Mac. It supplies the
 * eleven functions and five macros of ESP-IDF surface that the production
 * components use (see docs/specs/sd-stream-server.md section 10.4), so the same
 * .c files the firmware compiles can also be compiled, run and probed here.
 */
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* Writing to a socket whose peer has gone raises SIGPIPE, which would kill a
 * long-running server. The firmware has no such signal; the host shim makes the
 * host behave the same way. */
__attribute__((constructor)) static void host_shim_init(void)
{
    signal(SIGPIPE, SIG_IGN);
}

/* ------------------------------------------------------------------- errors */

const char *esp_err_to_name(esp_err_t code)
{
    switch (code) {
    case ESP_OK: return "ESP_OK";
    case ESP_FAIL: return "ESP_FAIL";
    case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_ARG: return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_INVALID_SIZE: return "ESP_ERR_INVALID_SIZE";
    case ESP_ERR_NOT_FOUND: return "ESP_ERR_NOT_FOUND";
    case ESP_ERR_NOT_SUPPORTED: return "ESP_ERR_NOT_SUPPORTED";
    default: return "ESP_ERR_UNKNOWN";
    }
}

/* --------------------------------------------------------------------- logs */

static host_log_level_t log_threshold(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char *v = getenv("HOST_LOG_LEVEL");
        cached = HOST_LOG_INFO;
        if (v && (!strcasecmp(v, "debug"))) cached = HOST_LOG_DEBUG;
        else if (v && !strcasecmp(v, "warn")) cached = HOST_LOG_WARN;
        else if (v && !strcasecmp(v, "error")) cached = HOST_LOG_ERROR;
    }
    return (host_log_level_t)cached;
}

void host_log(host_log_level_t level, const char *tag, const char *fmt, ...)
{
    static const char letters[] = "DIWE";
    if (level < log_threshold()) return;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    fprintf(stderr, "%c (%s) %3ld.%03ld ", letters[level], tag, (long)ts.tv_sec,
            ts.tv_nsec / 1000000);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

/* -------------------------------------------------------------------- timer */

int64_t esp_timer_get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/* --------------------------------------------------------------------- heap */

/* Synthetic, and deliberately so: the host has no PSRAM and no internal/external
 * split. Fixed figures keep the telemetry line the same shape as the device's
 * without pretending to measure anything. */
#define HOST_INTERNAL_FREE (320 * 1024)
#define HOST_SPIRAM_FREE (8 * 1024 * 1024)

void *heap_caps_malloc(size_t size, uint32_t caps)
{
    (void)caps; /* every capability maps to the same allocator on the host */
    return malloc(size);
}

void *heap_caps_calloc(size_t n, size_t size, uint32_t caps)
{
    (void)caps;
    return calloc(n, size);
}

void heap_caps_free(void *p) { free(p); }

size_t heap_caps_get_free_size(uint32_t caps)
{
    return (caps & MALLOC_CAP_SPIRAM) ? HOST_SPIRAM_FREE : HOST_INTERNAL_FREE;
}

/* Deliberately the same figure as get_free_size(): the host's heap is not a model of
 * the target's, and a fabricated "largest block" would be worse than an honest
 * non-answer. Nothing on this tier acts on it. */
size_t heap_caps_get_largest_free_block(uint32_t caps) { return heap_caps_get_free_size(caps); }

/* --------------------------------------------------------------------- tasks */

struct host_task {
    TaskFunction_t fn;
    void *arg;
    char name[24];
};

static void *task_trampoline(void *p)
{
    struct host_task *t = p;
    t->fn(t->arg);
    return NULL;
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack_bytes,
                                   void *arg, UBaseType_t priority, TaskHandle_t *out, int core_id)
{
    (void)priority;
    (void)core_id; /* the host has no core-affinity problem to solve */
    struct host_task *t = calloc(1, sizeof *t);
    if (!t) return pdFAIL;
    t->fn = fn;
    t->arg = arg;
    snprintf(t->name, sizeof t->name, "%s", name ? name : "task");

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    /* The firmware's stacks are sized for Xtensa with the decoder's scratch in
     * mind; the host's libc needs at least a little more headroom. */
    size_t stack = stack_bytes < 64 * 1024 ? 64 * 1024 : stack_bytes;
    pthread_attr_setstacksize(&attr, stack);
    pthread_t th;
    int rc = pthread_create(&th, &attr, task_trampoline, t);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        free(t);
        return pdFAIL;
    }
    pthread_detach(th);
    if (out) *out = (TaskHandle_t)t;
    return pdPASS;
}

BaseType_t xTaskCreatePinnedToCoreWithCaps(TaskFunction_t fn, const char *name, uint32_t stack_bytes,
                                           void *arg, UBaseType_t priority, TaskHandle_t *out,
                                           int core_id, uint32_t caps)
{
    (void)caps; /* one memory class here: see the header */
    return xTaskCreatePinnedToCore(fn, name, stack_bytes, arg, priority, out, core_id);
}

void vTaskDeleteWithCaps(TaskHandle_t task) { vTaskDelete(task); }

void vTaskDelay(TickType_t ticks)
{
    struct timespec ts = {.tv_sec = ticks / 1000, .tv_nsec = (long)(ticks % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

void vTaskDelete(TaskHandle_t task)
{
    (void)task; /* every host task is detached, and only ever deletes itself */
    pthread_exit(NULL);
}

TickType_t xTaskGetTickCount(void)
{
    return (TickType_t)(esp_timer_get_time() / 1000);
}

/* Synthetic, like the heap figures above: a pthread stack's high-water mark is not
 * available through a portable API, and the host gives every task at least 64 KB.
 * Reporting a plausible figure keeps the telemetry line the same shape as the
 * device's without pretending to have measured anything. */
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task)
{
    (void)task;
    return 8192;
}

/* ----------------------------------------------------------------- semaphore */

typedef struct {
    pthread_mutex_t m;
    pthread_cond_t c;
    unsigned count;
    unsigned max;
} host_sem_t;

static host_sem_t *sem_new(unsigned max, unsigned initial)
{
    host_sem_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    pthread_mutex_init(&s->m, NULL);
    pthread_cond_init(&s->c, NULL);
    s->count = initial;
    s->max = max;
    return s;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return sem_new(1, 1); }

SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t max_count, UBaseType_t initial_count)
{
    return sem_new(max_count, initial_count);
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t ticks_to_wait)
{
    host_sem_t *s = handle;
    if (!s) return pdFALSE;
    pthread_mutex_lock(&s->m);
    while (s->count == 0) {
        if (ticks_to_wait == 0) {
            pthread_mutex_unlock(&s->m);
            return pdFALSE;
        }
        if (ticks_to_wait == portMAX_DELAY) {
            pthread_cond_wait(&s->c, &s->m);
        } else {
            struct timespec deadline;
            clock_gettime(CLOCK_REALTIME, &deadline);
            deadline.tv_sec += ticks_to_wait / 1000;
            deadline.tv_nsec += (long)(ticks_to_wait % 1000) * 1000000L;
            if (deadline.tv_nsec >= 1000000000L) {
                deadline.tv_sec++;
                deadline.tv_nsec -= 1000000000L;
            }
            if (pthread_cond_timedwait(&s->c, &s->m, &deadline) == ETIMEDOUT && s->count == 0) {
                pthread_mutex_unlock(&s->m);
                return pdFALSE;
            }
        }
    }
    s->count--;
    pthread_mutex_unlock(&s->m);
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t handle)
{
    host_sem_t *s = handle;
    if (!s) return pdFALSE;
    pthread_mutex_lock(&s->m);
    if (s->count >= s->max) {
        pthread_mutex_unlock(&s->m);
        return pdFALSE;
    }
    s->count++;
    pthread_cond_signal(&s->c);
    pthread_mutex_unlock(&s->m);
    return pdTRUE;
}
