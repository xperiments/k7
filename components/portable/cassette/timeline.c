/*
 * The Timeline bar's arithmetic; see timeline.h.
 *
 * Three pure functions and no state. The two hosts keep their own labels, their own fill
 * object and their own memory of the last width they wrote, because those are the objects
 * ADR 0032 kept apart; what is here is the part that must not be written twice.
 */
#include "timeline.h"

#include <stdio.h>

void timeline_clock(char *out, size_t n, uint32_t ms, bool known)
{
    if (!out || n == 0) return;
    if (!known) {
        snprintf(out, n, "--:--");
        return;
    }
    uint32_t s = ms / 1000u;
    snprintf(out, n, "%u:%02u", (unsigned)(s / 60u), (unsigned)(s % 60u));
}

uint32_t timeline_permille(uint32_t elapsed_ms, uint32_t duration_ms)
{
    if (!duration_ms) return 0;
    uint32_t permille = (uint32_t)((uint64_t)elapsed_ms * 1000u / duration_ms);
    return permille > 1000u ? 1000u : permille;
}

int timeline_fill_w(int track_w, uint32_t permille)
{
    if (track_w <= 0) return 0;
    if (permille > 1000u) permille = 1000u;
    return (int)((uint64_t)track_w * permille / 1000u);
}
