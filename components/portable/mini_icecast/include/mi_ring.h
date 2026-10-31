#pragma once
/*
 * mi_ring - the byte queue between the broadcast task and one client's sender.
 *
 * The broadcast produces encoded bytes and must never wait for a socket: a slow
 * or stalled listener used to block the audio task in send(), which is exactly
 * how a slow reader on the same Wi-Fi could starve the stream. This ring is the
 * hand-off. The producer writes whole ICY-framed spans and never blocks; the
 * sender thread reads at the network's pace. When the ring is full the client is
 * too slow and is dropped, rather than slowing the broadcast.
 *
 * A mutex guards the indices. The producer and the consumer each hold it only
 * for the copy, never across a send.
 */
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mi_ring mi_ring_t;

/* `prefer_external` asks for PSRAM first (the device's internal RAM is scarce),
 * with an internal fallback. NULL when the capacity cannot be met. */
mi_ring_t *mi_ring_new(size_t capacity, bool prefer_external);
void       mi_ring_free(mi_ring_t *r);

void   mi_ring_reset(mi_ring_t *r);
size_t mi_ring_capacity(const mi_ring_t *r);
size_t mi_ring_used(mi_ring_t *r);

/* Append as much as fits. Returns the number of bytes accepted; a short return
 * means the consumer is behind (or the ring is full). */
size_t mi_ring_put(mi_ring_t *r, const void *data, size_t n);

/* Copy out at most `capacity` bytes. Returns 0 when the ring is empty. */
size_t mi_ring_get(mi_ring_t *r, void *out, size_t capacity);

#ifdef __cplusplus
}
#endif
