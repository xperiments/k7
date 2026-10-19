/*
 * cassette - the screen's state, derived from the broadcast's status.
 *
 * The screen task advances it from the status poll, and the creation path advances
 * it once to draw the empty screen. On the panel path both callers hold the LVGL
 * port's lock; on the headless path there is only one task. The module therefore
 * holds no lock of its own.
 *
 * The reels follow the track's audio position (`status.elapsed_ms`), not a wall
 * clock and not the cadence of the calls: a long gap reports where the tape is,
 * and a stopped track holds the last frame.
 */
#include "cassette.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct cassette {
    cassette_frame_t frame;
    mini_icecast_status_t status;
    bool     have_status;

    uint16_t frame_count;
    uint32_t frame_ms;
    uint32_t bar_period_ms;

    uint8_t  hub_frame[2]; /* the frame the reels are holding */

    uint32_t shown_permille;
    uint32_t last_bar_ms;
    bool     have_shown;

    char     shown_title[CASSETTE_TITLE_MAX];
    bool     have_title;
};

/* Metadata titles often carry an aside in parentheses - "(live, 1984, Le
 * Spectrum)" - which is longer than the label and says less than the song. Drop
 * the parenthesised groups, and any stray bracket, from the copy that goes on the
 * label. */
static void title_clean(const char *in, char *out, size_t n)
{
    size_t o = 0;
    int depth = 0;

    for (const char *p = in; *p && o + 1 < n; p++) {
        if (*p == '(') { depth++; continue; }
        if (*p == ')') { if (depth) depth--; continue; }
        if (depth) continue;
        out[o++] = *p;
    }
    while (o && (out[o - 1] == ' ' || out[o - 1] == '\t')) o--;
    out[o] = 0;
    /* Dropping a leading aside leaves the separator space behind - "(live) Song"
     * must read "Song", not " Song". The old inline copy trimmed only the end. */
    size_t start = 0;
    while (out[start] == ' ' || out[start] == '\t') start++;
    if (start) memmove(out, out + start, o - start + 1);
}

cassette_t *cassette_new(uint32_t loop_ms, uint16_t frame_count, uint32_t bar_period_ms)
{
    if (loop_ms == 0 || frame_count == 0) return NULL;
    cassette_t *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->frame_count = frame_count;
    c->frame_ms = loop_ms / frame_count;
    if (c->frame_ms == 0) c->frame_ms = 1;
    c->bar_period_ms = bar_period_ms;
    return c;
}

void cassette_free(cassette_t *c) { free(c); }

void cassette_reset(cassette_t *c)
{
    if (!c) return;
    memset(&c->frame, 0, sizeof c->frame);
    memset(&c->status, 0, sizeof c->status);
    c->status.format = MI_FMT_NONE;
    c->have_status = false;
    memset(c->hub_frame, 0, sizeof c->hub_frame);
    c->shown_permille = 0;
    c->last_bar_ms = 0;
    c->have_shown = false;
    c->shown_title[0] = 0;
    c->have_title = false;
    /* The adapter drew something; the next frame must say what to replace it
     * with, even when it is the same empty screen. */
    c->frame.title_changed = true;
    c->frame.permille_changed = true;
}

const cassette_frame_t *cassette_step(cassette_t *c, const mini_icecast_status_t *st, uint32_t now_ms)
{
    if (!c) return NULL;
    if (st) {
        c->status = *st;
        c->have_status = true;
    }

    /* The label: cleaned, and reported changed only when the text differs. */
    char clean[CASSETTE_TITLE_MAX];
    title_clean(c->have_status ? c->status.title : "", clean, sizeof clean);
    c->frame.title_changed = !c->have_title || strcmp(clean, c->shown_title) != 0;
    snprintf(c->frame.title, sizeof c->frame.title, "%s", clean);
    if (c->frame.title_changed) {
        snprintf(c->shown_title, sizeof c->shown_title, "%s", clean);
        c->have_title = true;
    }

    c->frame.playing = c->have_status && c->status.file[0] && c->status.format != MI_FMT_NONE;
    c->frame.level_l = c->have_status ? c->status.level_l : 0.0f;
    c->frame.level_r = c->have_status ? c->status.level_r : 0.0f;

    /* The position, stepped at the bar's period rather than at the poll rate: a
     * one-second step is ample for a bar and stops a sub-pixel change from
     * invalidating it ten times a second. */
    uint32_t permille = 0;
    if (c->have_status && c->status.duration_ms)
        permille = (uint32_t)((uint64_t)c->status.elapsed_ms * 1000ULL / c->status.duration_ms);
    if (permille > 1000) permille = 1000;

    bool due = !c->have_shown || now_ms - c->last_bar_ms >= c->bar_period_ms;
    if (due) {
        c->frame.permille_changed = !c->have_shown || permille != c->shown_permille;
        c->shown_permille = permille;
        c->last_bar_ms = now_ms;
        c->have_shown = true;
    } else {
        c->frame.permille_changed = false;
    }
    c->frame.permille = c->shown_permille;
    c->frame.pack_fill[0] = (uint16_t)(1000 - c->shown_permille);
    c->frame.pack_fill[1] = (uint16_t)c->shown_permille;

    /* The reels: the track's audio position picks the art frame, so the frame is
     * reproducible and independent of how often this is called. A stopped track
     * holds the last frame; a new track starts the loop again at elapsed 0. */
    if (c->frame.playing) {
        uint32_t frame = c->status.elapsed_ms / c->frame_ms;
        for (int i = 0; i < 2; i++)
            c->hub_frame[i] = (uint8_t)(frame % c->frame_count);
    }
    for (int i = 0; i < 2; i++) c->frame.hub_frame[i] = c->hub_frame[i];

    return &c->frame;
}
