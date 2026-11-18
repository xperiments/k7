#include "player_ring.h"

#include <string.h>

/* A frame in the ring is its two-byte length and then its bytes, so the ring is a plain byte
 * FIFO and the frame boundaries travel with the data. */
#define PREFIX 2

static void bytes_in(player_ring_t *r, const uint8_t *src, size_t n)
{
    size_t first = PLAYER_RING_BYTES - r->head;
    if (first > n) first = n;
    memcpy(r->data + r->head, src, first);
    if (n > first) memcpy(r->data, src + first, n - first);
    r->head = (unsigned)((r->head + n) % PLAYER_RING_BYTES);
}

static void bytes_out(player_ring_t *r, uint8_t *dst, size_t n)
{
    size_t first = PLAYER_RING_BYTES - r->tail;
    if (first > n) first = n;
    memcpy(dst, r->data + r->tail, first);
    if (n > first) memcpy(dst + first, r->data, n - first);
    r->tail = (unsigned)((r->tail + n) % PLAYER_RING_BYTES);
}

/* Look at bytes without taking them, wrapped. */
static void bytes_at(const player_ring_t *r, size_t off, uint8_t *dst, size_t n)
{
    size_t pos = (r->tail + off) % PLAYER_RING_BYTES;
    size_t first = PLAYER_RING_BYTES - pos;
    if (first > n) first = n;
    memcpy(dst, r->data + pos, first);
    if (n > first) memcpy(dst + first, r->data, n - first);
}

void player_ring_init(player_ring_t *r)
{
    if (!r) return;
    memset(r, 0, sizeof *r);
}

bool player_ring_push(player_ring_t *r, const uint8_t *frame, size_t len)
{
    if (!r || !frame || len == 0 || len > PLAYER_RING_SLOT) return false;

    size_t need = len + PREFIX;
    if (need > PLAYER_RING_BYTES) return false;

    if (r->used + need > PLAYER_RING_BYTES) {
        /* The outage outlasted the budget: the backlog goes whole and the newest audio stays.
         * One discontinuity, and the count says so. */
        r->flushes++;
        r->dropped_bytes += r->used;
        r->head = r->tail = r->used = 0;
    }

    uint8_t prefix[PREFIX] = {(uint8_t)(len & 0xFFu), (uint8_t)((len >> 8) & 0xFFu)};
    bytes_in(r, prefix, PREFIX);
    bytes_in(r, frame, len);
    r->used += (unsigned)need;
    return true;
}

bool player_ring_pop(player_ring_t *r, uint8_t *out, size_t cap, size_t *len)
{
    if (!r || !out || cap == 0 || r->used < PREFIX) return false;

    uint8_t prefix[PREFIX];
    bytes_at(r, 0, prefix, PREFIX);
    size_t n = (size_t)prefix[0] | ((size_t)prefix[1] << 8);
    /* A length that is empty, longer than the caller's buffer, or longer than the ring is a
     * damaged ring, not a frame: refuse rather than hand out half of anything. */
    if (n == 0 || n > cap || n + PREFIX > r->used) return false;

    uint8_t scratch[PREFIX];
    bytes_out(r, scratch, PREFIX);
    bytes_out(r, out, n);
    r->used -= (unsigned)(n + PREFIX);
    if (len) *len = n;
    return true;
}

unsigned player_ring_used(const player_ring_t *r) { return r ? r->used : 0; }
unsigned player_ring_capacity(void) { return PLAYER_RING_BYTES; }
unsigned player_ring_flushes(const player_ring_t *r) { return r ? r->flushes : 0; }
unsigned player_ring_dropped_bytes(const player_ring_t *r) { return r ? r->dropped_bytes : 0; }
