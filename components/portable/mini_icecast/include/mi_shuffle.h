#pragma once
/*
 * mi_shuffle - the shuffle order: one permutation of the broadcast order, walked both ways.
 *
 * Shuffle is a mode of the broadcast, not an action
 * (docs/adr/0023-shuffle-is-a-broadcast-mode.md): the whole broadcast order is permuted once,
 * `next` walks that permutation forward, `prev` walks it back, and an explicit pick
 * moves the cursor in with `seek` instead of rebuilding the order. The cursor always
 * names the Track playing now, so the next advance takes the element after it, and
 * the previous command never has to consult the order at all - it walks the
 * broadcast's own history.
 *
 * This unit is pure arithmetic: no esp_*, no logging, no allocation, no clock. The
 * caller owns the order array (count * 4 bytes, 80,000 B at 20,000 Tracks) and seeds
 * the generator, so the host tests pin the sequence and the board's randomness comes
 * from the app (`mini_icecast_shuffle_seed((uint32_t)esp_random())` at boot).
 *
 * The generator is xorshift32: 32 bits of state, three shifts and XORs. It is not
 * cryptographic and does not need to be - the order is heard, not attacked. The
 * Fisher-Yates draw uses `%`, so the permutation is uniform only up to a modulo bias;
 * at 20,000 Tracks that is far below anything an ear could notice.
 */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t *order;  /* the caller's array: order[i] is the track index at position i */
    uint32_t  count;  /* Tracks in the order; 0 when there is none */
    uint32_t  cursor; /* position in `order` of the Track playing now */
    uint32_t  rng;    /* xorshift32 state; 0 means "not seeded yet" */
    bool      ready;  /* order != NULL && count != 0 */
} mi_shuffle_t;

/* Seed the generator. Call it once at boot, before the first build; a build that
 * follows draws from this state. Zero is replaced by a fixed non-zero constant,
 * because xorshift32 has no zero state (it would emit zeros forever). The same seed
 * always produces the same permutation, which is what the host test pins. */
void mi_shuffle_seed(mi_shuffle_t *s, uint32_t seed);

/* Build a fresh order over [0, count) into the caller's `order`, by Fisher-Yates.
 * `current` is the track index playing now: it is placed at the cursor (a seek
 * after the shuffle), so `next` continues after it and the playing Track is not
 * interrupted. A `current` outside the broadcast order (a generation change) lands the
 * cursor on the first element.
 *
 * Returns true when there is an order to walk (`order != NULL` and `count > 0`);
 * false leaves the struct empty, so next/prev/seek are safe no-ops. The RNG state
 * carries over, so rebuilding without a new seed draws a new order. */
bool mi_shuffle_build(mi_shuffle_t *s, uint32_t *order, uint32_t count, uint32_t current);

/* Advance one place and return the track index now playing. `*wrapped` (may be
 * NULL) is set when the cursor moved to position 0 - the first element of a new lap -
 * which the broadcast uses to reset its history exactly like the linear broadcast order's
 * `(last + 1) % count == 0`. With one Track every advance wraps, matching the linear
 * lap. Returns 0 and reports no wrap when there is no order. */
uint32_t mi_shuffle_next(mi_shuffle_t *s, bool *wrapped);

/* Step back one place, wrapping from the first position to the last, and return the
 * track index now playing. The broadcast leaves the cursor where it is on `prev`
 * (it walks its own history instead), but walking it back here is how the order's
 * reverse direction is pinned. */
uint32_t mi_shuffle_prev(mi_shuffle_t *s);

/* Move the cursor to `index`'s place in the order (a linear search: the order is a
 * permutation, so a pick moves the cursor without rebuilding it). Returns false and
 * leaves the cursor alone when the index is not in the order or there is no order. */
bool mi_shuffle_seek(mi_shuffle_t *s, uint32_t index);

/* The track index at the cursor, or 0 when there is no order. */
uint32_t mi_shuffle_current(const mi_shuffle_t *s);

/* Whether an order is being walked. One Track is a real order (walked in place);
 * an empty Library is not. */
bool mi_shuffle_ready(const mi_shuffle_t *s);

#ifdef __cplusplus
}
#endif
