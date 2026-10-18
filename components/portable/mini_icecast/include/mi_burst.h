#pragma once
/*
 * mi_burst - the history sent to a client on connect, as whole encoded frames.
 *
 * The burst is a circular byte ring of records, each `[length16 LE][whole encoded
 * frame]`. It owns the three rules that are easy to get wrong and invisible when
 * wrong: a record may straddle the end of the ring, eviction always drops whole
 * records oldest-first, and the capacity must hold at least one record plus its
 * two-byte prefix or the buffer is refused at construction rather than looping
 * forever at the first put.
 *
 * It takes no lock and knows nothing about sockets or clients: the fan-out holds
 * the lock and pushes what comes out to a file descriptor. That is what makes the
 * ring testable without a connection.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mi_burst mi_burst_t;

/* A read position over the records, oldest first. Opaque enough that callers do
 * not depend on the ring's layout. */
typedef struct {
    size_t pos;
    size_t left;
} mi_burst_cursor_t;

/* Allocate a ring of `capacity` bytes, preferring external memory when asked and
 * falling back to internal. Returns NULL when the capacity cannot hold one record
 * (fewer than 3 bytes) or when neither allocator can satisfy it. */
mi_burst_t *mi_burst_new(size_t capacity, bool prefer_external);
void        mi_burst_free(mi_burst_t *b);

size_t mi_burst_capacity(const mi_burst_t *b);
size_t mi_burst_used(const mi_burst_t *b);
bool   mi_burst_uses_external_memory(const mi_burst_t *b);

/* Append one whole frame. Older records are dropped until it fits, so the newest
 * record is always kept. Returns false when the record cannot fit in the ring at
 * all (larger than capacity - 2); the ring is left unchanged in that case. */
bool mi_burst_put(mi_burst_t *b, const void *frame, size_t bytes);

/* Start reading at the oldest surviving record. */
void mi_burst_begin(const mi_burst_t *b, mi_burst_cursor_t *cur);

/* Copy the next record into `out`, whose capacity must be at least the largest
 * record put into the ring. Returns the record's length, or 0 at the end. A
 * corrupt record stops the walk rather than overrunning the buffer. */
size_t mi_burst_next(const mi_burst_t *b, mi_burst_cursor_t *cur, void *out, size_t capacity);

#ifdef __cplusplus
}
#endif
