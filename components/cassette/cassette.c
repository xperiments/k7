/*
 * cassette - the screen's state, advanced by the clock and the broadcast's status.
 *
 * Two callers advance it: the status poll and the reel timer. Both are serialised
 * by the LVGL port's lock on the panel path, and both run in the same task on the
 * headless path, so this module holds no lock of its own - the same arrangement
 * the statics it replaces relied on, made explicit.
 */
#include "cassette.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A gap longer than this is a stalled task, not elapsed audio: advancing the
 * whole reel over it would make the tape jump. 250 ms is about two and a half
 * art frames at the exported loop's rate. */
#define REEL_MAX_DT_MS 250

struct cassette {
    cassette_frame_t frame;
    mini_icecast_status_t status;
    bool     have_status;

    uint16_t frame_count;
    uint32_t frame_ms;
    uint32_t bar_period_ms;

    uint32_t reel_acc[2]; /* time carried towards the next art frame */
    uint8_t  hub_frame[2];
    uint32_t last_ms;
    bool     have_last;

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
    memset(c->reel_acc, 0, sizeof c->reel_acc);
    memset(c->hub_frame, 0, sizeof c->hub_frame);
    c->last_ms = 0;
    c->have_last = false;
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

    /* The reels: one art frame per frame_ms, with a long gap clamped so the tape
     * does not leap forward after a stalled task. Time is recorded even when the
     * reels are stopped, so resuming does not spend a backlog. */
    uint32_t dt = 0;
    if (c->have_last) dt = now_ms - c->last_ms;
    c->last_ms = now_ms;
    c->have_last = true;
    if (dt > REEL_MAX_DT_MS) dt = REEL_MAX_DT_MS;
    if (c->frame.playing && dt) {
        for (int i = 0; i < 2; i++) {
            c->reel_acc[i] += dt;
            while (c->reel_acc[i] >= c->frame_ms) {
                c->reel_acc[i] -= c->frame_ms;
                c->hub_frame[i] = (uint8_t)((c->hub_frame[i] + 1) % c->frame_count);
            }
            c->frame.hub_frame[i] = c->hub_frame[i];
        }
    } else {
        for (int i = 0; i < 2; i++) c->frame.hub_frame[i] = c->hub_frame[i];
    }

    return &c->frame;
}
