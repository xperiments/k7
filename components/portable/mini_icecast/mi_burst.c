/*
 * mi_burst - record framing, eviction and wraparound for the connect history.
 */
#include "mi_burst.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"

struct mi_burst {
    uint8_t *ring;
    size_t   size;   /* capacity in bytes */
    size_t   head;   /* next write position */
    size_t   tail;   /* oldest record */
    size_t   used;
    bool     external;
};

#define RECORD_PREFIX 2

static void peek(const mi_burst_t *b, size_t pos, void *out, size_t n)
{
    size_t first = b->size - pos;
    if (first > n) first = n;
    memcpy(out, b->ring + pos, first);
    memcpy((uint8_t *)out + first, b->ring, n - first);
}

static void write_at(mi_burst_t *b, size_t pos, const void *src, size_t n)
{
    size_t first = b->size - pos;
    if (first > n) first = n;
    memcpy(b->ring + pos, src, first);
    memcpy(b->ring, (const uint8_t *)src + first, n - first);
}

/* Drop the oldest record. A corrupt prefix means the ring's own bookkeeping is
 * gone; empty it rather than walking nonsense. Returns false when there was
 * nothing to drop. */
static bool drop_oldest(mi_burst_t *b)
{
    if (b->used < RECORD_PREFIX) return false;
    uint8_t lb[RECORD_PREFIX];
    peek(b, b->tail, lb, sizeof lb);
    size_t n = (size_t)(lb[0] | (lb[1] << 8));
    if (n == 0 || n + RECORD_PREFIX > b->used) {
        b->head = b->tail = b->used = 0;
        return false;
    }
    b->tail = (b->tail + RECORD_PREFIX + n) % b->size;
    b->used -= RECORD_PREFIX + n;
    return true;
}

mi_burst_t *mi_burst_new(size_t capacity, bool prefer_external)
{
    /* A record needs its two-byte prefix plus at least one byte. Refusing this at
     * construction is what keeps put() from looping: the invariant it relies on
     * is created here, not assumed by the caller. */
    if (capacity < RECORD_PREFIX + 1) return NULL;

    mi_burst_t *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    if (prefer_external) {
        b->ring = heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        b->external = b->ring != NULL;
    }
    if (!b->ring) b->ring = malloc(capacity);
    if (!b->ring) {
        free(b);
        return NULL;
    }
    b->size = capacity;
    return b;
}

void mi_burst_free(mi_burst_t *b)
{
    if (!b) return;
    free(b->ring);
    free(b);
}

size_t mi_burst_capacity(const mi_burst_t *b) { return b ? b->size : 0; }
size_t mi_burst_used(const mi_burst_t *b) { return b ? b->used : 0; }
bool mi_burst_uses_external_memory(const mi_burst_t *b) { return b && b->external; }

bool mi_burst_put(mi_burst_t *b, const void *frame, size_t bytes)
{
    if (!b || !frame || bytes == 0) return false;
    /* The newest record always survives, so a record that cannot fit at all is a
     * programming error, not a reason to discard the whole history. */
    if (bytes + RECORD_PREFIX > b->size) return false;

    while (b->used + bytes + RECORD_PREFIX > b->size) {
        if (!drop_oldest(b)) {
            /* Only reachable with a corrupt ring; start clean. */
            b->head = b->tail = b->used = 0;
            break;
        }
    }

    uint8_t lb[RECORD_PREFIX] = {(uint8_t)(bytes & 0xFF), (uint8_t)(bytes >> 8)};
    write_at(b, b->head, lb, sizeof lb);
    b->head = (b->head + sizeof lb) % b->size;
    b->used += sizeof lb;

    write_at(b, b->head, frame, bytes);
    b->head = (b->head + bytes) % b->size;
    b->used += bytes;
    return true;
}

void mi_burst_begin(const mi_burst_t *b, mi_burst_cursor_t *cur)
{
    if (!cur) return;
    cur->pos = b ? b->tail : 0;
    cur->left = b ? b->used : 0;
}

size_t mi_burst_next(const mi_burst_t *b, mi_burst_cursor_t *cur, void *out, size_t capacity)
{
    if (!b || !cur || !out) return 0;
    if (cur->left < RECORD_PREFIX) return 0;

    uint8_t lb[RECORD_PREFIX];
    peek(b, cur->pos, lb, sizeof lb);
    size_t n = (size_t)(lb[0] | (lb[1] << 8));
    /* A record longer than what is left, or than the caller can take, means the
     * ring is corrupt or the caller ignored the capacity contract: stop. */
    if (n == 0 || n + RECORD_PREFIX > cur->left || n > capacity) return 0;

    peek(b, (cur->pos + RECORD_PREFIX) % b->size, out, n);
    cur->pos = (cur->pos + RECORD_PREFIX + n) % b->size;
    cur->left -= RECORD_PREFIX + n;
    return n;
}
