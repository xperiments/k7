/*
 * cassette - the screen's state, derived from the broadcast's status.
 *
 * The screen task advances it from the status poll, and the creation path advances
 * it once to draw the empty screen. On the panel path both callers hold the LVGL
 * port's lock; on the headless path there is only one task. The module therefore
 * holds no lock of its own.
 *
 * The reels follow the track's tape position, not a wall clock: each reel
 * integrates the tape speed at its own pack radius (v/r), so the supply and
 * take-up turn at different speeds. A stopped track holds them; a new track
 * starts them at zero.
 */
#include "cassette.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The angular speed is v/r; this turns radius and speed into revolutions/s. */
#define CASSETTE_TWO_PI 6.28318530718f

struct cassette {
    cassette_frame_t frame;
    mini_icecast_status_t status;
    bool     have_status;

    uint16_t frame_count;
    uint32_t bar_period_ms;

    uint8_t  hub_frame[2]; /* the frames the reels are holding */
    float    phase[2];     /* revolutions through the art, 0..1 */
    uint32_t last_elapsed;
    bool     have_last;
    bool     was_playing;
    float    peak_l, peak_r; /* the VU peak-hold, 0..1 */

    uint32_t shown_permille;
    uint32_t last_bar_ms;
    bool     have_shown;

    char     shown_title[CASSETTE_TITLE_MAX];
    bool     have_title;
};

/* The pack radius for a fill (0..1000). The tape length, and therefore the pack
 * area, is proportional to the fill: the radius grows as the square root. */
static float pack_radius(uint32_t fill)
{
    float hub = CASSETTE_HUB_RADIUS_MM;
    float full = CASSETTE_FULL_RADIUS_MM;
    float f = (float)fill / 1000.0f;
    return sqrtf(hub * hub + (full * full - hub * hub) * f);
}

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

cassette_t *cassette_new(uint16_t frame_count, uint32_t bar_period_ms)
{
    if (frame_count == 0) return NULL;
    cassette_t *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->frame_count = frame_count;
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
    memset(c->phase, 0, sizeof c->phase);
    c->last_elapsed = 0;
    c->have_last = false;
    c->was_playing = false;
    c->peak_l = 0.0f;
    c->peak_r = 0.0f;
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

    /* The VU's peak-hold: the status carries RMS, not a sample peak, so hold the
     * highest recent RMS with a slow fall. 0.97 per 40 ms step is about 0.47/s. */
    c->peak_l = fmaxf(c->frame.level_l, c->peak_l * 0.97f);
    c->peak_r = fmaxf(c->frame.level_r, c->peak_r * 0.97f);
    c->frame.vu_l = c->peak_l;
    c->frame.vu_r = c->peak_r;

    /* The position, stepped at the bar's period rather than at the poll rate: a
     * one-second step is ample for a bar and stops a sub-pixel change from
     * invalidating it ten times a second. The reels use the current position
     * below, so their speed follows the pack radius. */
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
    /* The empty side never goes below the minimum load: the tape edge and the VU
     * must stay outside the hub ring. */
    uint32_t load = 1000 - CASSETTE_MIN_FILL;
    c->frame.pack_fill[0] = (uint16_t)(CASSETTE_MIN_FILL + load * (1000 - c->shown_permille) / 1000);
    c->frame.pack_fill[1] = (uint16_t)(CASSETTE_MIN_FILL + load * c->shown_permille / 1000);

    /* A track with no duration still needs a side for the reel fill; the bar
     * stays at zero because it has nothing to measure against. */
    uint32_t position = permille;
    if (c->have_status && !c->status.duration_ms)
        position = (uint32_t)((uint64_t)c->status.elapsed_ms * 1000ULL / CASSETTE_NOMINAL_SIDE_MS);
    if (position > 1000) position = 1000;

    /* The reels: each reel integrates the tape speed at its own pack radius, so
     * the supply and take-up turn at different speeds. The first step of a track
     * starts at zero; a track change (elapsed going backwards) or a stop starts
     * them again. The frame is the phase's position in the art. */
    if (c->frame.playing) {
        uint32_t elapsed = c->status.elapsed_ms;
        bool restart = !c->was_playing || (c->have_last && elapsed < c->last_elapsed);
        if (restart) {
            c->phase[0] = c->phase[1] = 0.0f;
        } else if (c->have_last) {
            float dt = (float)(elapsed - c->last_elapsed) / 1000.0f;
            uint32_t load = 1000 - CASSETTE_MIN_FILL;
            uint32_t fill[2] = {
                CASSETTE_MIN_FILL + load * (1000 - position) / 1000,
                CASSETTE_MIN_FILL + load * position / 1000,
            };
            for (int i = 0; i < 2; i++) {
                float r = pack_radius(fill[i]);
                float rev_s = CASSETTE_TAPE_SPEED_MM_S / (CASSETTE_TWO_PI * r);
                c->phase[i] = fmodf(c->phase[i] + rev_s * dt, 1.0f);
            }
        }
        for (int i = 0; i < 2; i++)
            c->hub_frame[i] = (uint8_t)((uint32_t)(c->phase[i] * (float)c->frame_count)
                                        % c->frame_count);
    }
    c->was_playing = c->frame.playing;
    c->last_elapsed = c->have_status ? c->status.elapsed_ms : 0;
    c->have_last = c->have_status;
    for (int i = 0; i < 2; i++) c->frame.hub_frame[i] = c->hub_frame[i];

    return &c->frame;
}
