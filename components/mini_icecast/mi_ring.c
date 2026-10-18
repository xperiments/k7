/*
 * mi_ring - a mutex-guarded byte ring for the producer/consumer hand-off.
 *
 * Not lock-free on purpose: the producer takes the mutex inside the broadcast
 * lock, and the consumer takes it only to copy a chunk out, so the critical
 * sections are a memcpy and never a send. That is simple enough to reason about
 * and cheap enough that the audio task never notices it.
 */
#include "mi_ring.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

struct mi_ring {
    uint8_t          *buf;
    size_t            size;
    size_t            head; /* next write */
    size_t            tail; /* next read */
    size_t            used;
    bool              external;
    SemaphoreHandle_t lock;
};

mi_ring_t *mi_ring_new(size_t capacity, bool prefer_external)
{
    if (capacity == 0) return NULL;
    mi_ring_t *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    if (prefer_external) {
        r->buf = heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        r->external = r->buf != NULL;
    }
    if (!r->buf) r->buf = malloc(capacity);
    if (!r->buf) {
        free(r);
        return NULL;
    }
    r->lock = xSemaphoreCreateMutex();
    if (!r->lock) {
        free(r->buf);
        free(r);
        return NULL;
    }
    r->size = capacity;
    return r;
}

void mi_ring_free(mi_ring_t *r)
{
    if (!r) return;
    if (r->lock) vSemaphoreDelete(r->lock);
    free(r->buf);
    free(r);
}

void mi_ring_reset(mi_ring_t *r)
{
    if (!r) return;
    xSemaphoreTake(r->lock, portMAX_DELAY);
    r->head = r->tail = r->used = 0;
    xSemaphoreGive(r->lock);
}

size_t mi_ring_capacity(const mi_ring_t *r) { return r ? r->size : 0; }

size_t mi_ring_used(mi_ring_t *r)
{
    if (!r) return 0;
    xSemaphoreTake(r->lock, portMAX_DELAY);
    size_t used = r->used;
    xSemaphoreGive(r->lock);
    return used;
}

size_t mi_ring_put(mi_ring_t *r, const void *data, size_t n)
{
    if (!r || !data || n == 0) return 0;
    xSemaphoreTake(r->lock, portMAX_DELAY);
    size_t room = r->size - r->used;
    size_t k = n < room ? n : room;
    if (k) {
        size_t first = r->size - r->head;
        if (first > k) first = k;
        memcpy(r->buf + r->head, data, first);
        memcpy(r->buf, (const uint8_t *)data + first, k - first);
        r->head = (r->head + k) % r->size;
        r->used += k;
    }
    xSemaphoreGive(r->lock);
    return k;
}

size_t mi_ring_get(mi_ring_t *r, void *out, size_t capacity)
{
    if (!r || !out || capacity == 0) return 0;
    xSemaphoreTake(r->lock, portMAX_DELAY);
    size_t k = r->used < capacity ? r->used : capacity;
    if (k) {
        size_t first = r->size - r->tail;
        if (first > k) first = k;
        memcpy(out, r->buf + r->tail, first);
        memcpy((uint8_t *)out + first, r->buf, k - first);
        r->tail = (r->tail + k) % r->size;
        r->used -= k;
    }
    xSemaphoreGive(r->lock);
    return k;
}
