/*
 * mi_shuffle - the order's arithmetic. See mi_shuffle.h for the contract; this file
 * holds no state, allocates nothing and knows nothing about the broadcast, the
 * Library or the card.
 */
#include "mi_shuffle.h"

#include <stddef.h> /* NULL */

/* xorshift32 (Marsaglia): three shifts and XORs over 32 bits of state. The sequence
 * is part of the host test's contract, so a different generator would need t19
 * re-pinned. */
static uint32_t sh_next(mi_shuffle_t *s)
{
    uint32_t x = s->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s->rng = x;
    return x;
}

/* xorshift32 has no zero state. The fallback is the golden-ratio constant the rest
 * of the firmware already uses for mixing, so an unseeded struct still builds. */
#define SH_SEED_ZERO 0x9e3779b9u

void mi_shuffle_seed(mi_shuffle_t *s, uint32_t seed)
{
    if (!s) return;
    s->rng = seed ? seed : SH_SEED_ZERO;
}

bool mi_shuffle_build(mi_shuffle_t *s, uint32_t *order, uint32_t count, uint32_t current)
{
    if (!s) return false;
    s->order = order;
    s->count = count;
    s->cursor = 0;
    s->ready = order != NULL && count != 0;
    if (!s->ready) return false;
    if (!s->rng) s->rng = SH_SEED_ZERO; /* never seeded: still a valid sequence */

    for (uint32_t i = 0; i < count; i++) order[i] = i;
    /* Fisher-Yates, high to low: every permutation is reachable, and the work is
     * one draw and at most one swap per element (20,000 at the Playlist's ceiling). */
    for (uint32_t i = count - 1; i > 0; i--) {
        uint32_t j = sh_next(s) % (i + 1);
        uint32_t t = order[i];
        order[i] = order[j];
        order[j] = t;
    }

    /* The Track playing keeps its place in the order: the next advance takes the
     * element after its position, and a pick moves this cursor with seek(). */
    if (!mi_shuffle_seek(s, current)) s->cursor = 0;
    return true;
}

uint32_t mi_shuffle_next(mi_shuffle_t *s, bool *wrapped)
{
    if (wrapped) *wrapped = false;
    if (!s || !s->ready) return 0;
    uint32_t pos = s->cursor + 1;
    if (pos >= s->count) {
        pos = 0;
        if (wrapped) *wrapped = true;
    }
    s->cursor = pos;
    return s->order[pos];
}

uint32_t mi_shuffle_prev(mi_shuffle_t *s)
{
    if (!s || !s->ready) return 0;
    s->cursor = s->cursor ? s->cursor - 1 : s->count - 1;
    return s->order[s->cursor];
}

bool mi_shuffle_seek(mi_shuffle_t *s, uint32_t index)
{
    if (!s || !s->ready) return false;
    for (uint32_t i = 0; i < s->count; i++) {
        if (s->order[i] == index) {
            s->cursor = i;
            return true;
        }
    }
    return false;
}

uint32_t mi_shuffle_current(const mi_shuffle_t *s)
{
    return (s && s->ready) ? s->order[s->cursor] : 0;
}

bool mi_shuffle_ready(const mi_shuffle_t *s) { return s && s->ready; }
