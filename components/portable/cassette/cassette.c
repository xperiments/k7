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
 * take-up turn at different speeds. A stopped track holds them; a track change
 * plays a rewind (CASSETTE_REWIND_MS) that starts on the frame already on screen
 * and ends on the one normal play shows when the window closes, so the reels never
 * jump. That window is the only place the module departs from the tape geometry:
 * the reverse spin is a perceptual rate, not v/r, because a real rewind would be
 * hundreds of revolutions in less time than the art can show.
 */
#include "cassette.h"

#include "timeline.h"

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

    /* The reel change. `shown_q` and `shown_phase` are what the screen has now, so a
     * new window can start from them instead of from the model's idea of the position;
     * that is also what makes a change during a window restart without a jump. */
    bool     rewinding;
    uint32_t rewind_t0_ms;      /* the elapsed the window opened at */
    uint32_t rewind_q0;         /* the position the packs were showing, 0..1000 */
    float    rewind_offset[2];  /* revolutions still to unwind, whole + fraction */
    uint32_t shown_q;           /* the position the packs are showing, 0..1000 */
    float    shown_phase[2];    /* the hub phase on screen */

    uint32_t shown_permille;
    uint32_t shown_elapsed_ms;
    uint32_t shown_duration_ms;
    uint32_t last_bar_ms;
    bool     have_shown;
    bool     have_time;

    char     shown_title[CASSETTE_TITLE_MAX];
    bool     have_title;

    /* The Card's table ink. It is the Track's path, hashed, because the shell is part of
     * the Track's identity: a Track with no Cover still changes the cassette, and the
     * choice survives a pause and a rebuilt Playlist. */
    uint32_t shown_ink_seed;
    bool     have_ink;
};

/* FNV-1a, 32 bits. Any hash would do; this one is four lines, has no state and spreads a
 * directory of Track names across a handful of buckets evenly enough to look arbitrary. It
 * runs on the Track's **file name**, so a Track keeps its cassette if its folder is renamed
 * and two folders holding the same name share one. */
static uint32_t name_seed(const char *name)
{
    uint32_t h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)name; p && *p; p++) {
        h ^= *p;
        h *= 16777619u;
    }
    return h;
}

/* ----------------------------------------------------------------- rewind */

/* How far the rewind has got (0..1) at a linear time p (0..1): a constant rate for
 * all but the last EASE_MS, then a rate that falls to zero, so the reels wind down
 * into the start of the song instead of stopping dead. The shape is normalised to
 * reach exactly 1, which is what lets the window land on the normal state. */
static float rewind_ease(float p)
{
    float tail = (float)CASSETTE_REWIND_EASE_MS / (float)CASSETTE_REWIND_MS;
    float hold = 1.0f - tail;
    /* The rate is 1 for `hold` of the window and 1-u^2 for the tail; their integrals
     * are `hold` and tail*2/3, so `total` is what the shape has to be divided by. */
    float total = hold + tail * 2.0f / 3.0f;

    if (p <= 0.0f) return 0.0f;
    if (p >= 1.0f) return 1.0f;
    if (p <= hold) return p / total;
    float u = (p - hold) / tail;
    return (hold + tail * (u - u * u * u / 3.0f)) / total;
}

/* The tape's ink through the rewind window, from the window's progress: full at its
 * start, gone over its first second, gone for the body, and back over its last second
 * as the new side starts. The permille is linear in time, so each ramp is a plain
 * interpolation. 1 is the playing red, 0 the tape's own colour. */
static float rewind_tape_ink(uint16_t rewind_permille)
{
    uint32_t elapsed = (uint32_t)rewind_permille * CASSETTE_REWIND_MS / 1000u;
    uint32_t fade_in_at = CASSETTE_REWIND_MS - CASSETTE_REWIND_FADE_MS;

    if (elapsed < CASSETTE_REWIND_FADE_MS)
        return 1.0f - (float)elapsed / (float)CASSETTE_REWIND_FADE_MS;
    if (elapsed > fade_in_at)
        return (float)(elapsed - fade_in_at) / (float)CASSETTE_REWIND_FADE_MS;
    return 0.0f;
}

/* The art frame for a phase. The reverse spin carries a whole number of revolutions,
 * so the phase can be well past 1; only the fraction selects a frame. */
static uint8_t hub_index(float phase, uint16_t frame_count)
{
    float frac = phase - floorf(phase);
    if (frac < 0.0f) frac += 1.0f;
    return (uint8_t)((uint32_t)(frac * (float)frame_count) % frame_count);
}

/* The pack radius for a fill (0..1000). The tape length, and therefore the pack
 * area, is proportional to the fill: the radius grows as the square root. */
static float pack_radius(uint32_t fill)
{
    float hub = CASSETTE_HUB_RADIUS_MM;
    float full = CASSETTE_FULL_RADIUS_MM;
    float f = (float)fill / 1000.0f;
    return sqrtf(hub * hub + (full * full - hub * hub) * f);
}

/* Clean a display name: apostrophes (ASCII and U+2019) to spaces, parenthetical asides
 * dropped, whitespace collapsed and trimmed, and - for a file name - the extension cut
 * first. One function for the Peek's fallback title and the Menu's row labels, so the two
 * cannot drift apart; the stem flag is the only difference. */
static size_t utf8_sequence(const unsigned char *s, size_t left)
{
    unsigned char c = s[0];
    size_t len = 1;
    if ((c & 0xE0u) == 0xC0u) len = 2;
    else if ((c & 0xF0u) == 0xE0u) len = 3;
    else if ((c & 0xF8u) == 0xF0u) len = 4;
    return len <= left ? len : left;
}

const char *cassette_clean_name(const char *in, bool stem, char *out, size_t n)
{
    if (!out) return out;
    if (n == 0) return out;
    out[0] = 0;
    if (!in) return out;

    size_t total = strlen(in);
    size_t end = total;
    if (stem) {
        for (size_t i = total; i > 0; i--) {
            if (in[i - 1] == '.') {
                end = i - 1;
                break;
            }
        }
    }

    size_t o = 0;
    int depth = 0;        /* inside a parenthetical aside */
    bool pending = false; /* a space is due before the next visible character */
    for (size_t i = 0; i < end;) {
        unsigned char c = (unsigned char)in[i];
        unsigned char next1 = (i + 1 < end) ? (unsigned char)in[i + 1] : 0;
        unsigned char next2 = (i + 2 < end) ? (unsigned char)in[i + 2] : 0;

        if (depth > 0) {
            if (c == '(') depth++;
            else if (c == ')') depth--;
            i++;
            continue;
        }
        if (c == '(') {
            depth = 1;
            i++;
            continue;
        }

        bool space = c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\'';
        size_t seq = 1;
        if (c == 0xE2 && next1 == 0x80 && next2 == 0x99) { /* U+2019 */
            space = true;
            seq = 3;
        } else if (!space) {
            seq = utf8_sequence((const unsigned char *)in + i, end - i);
        }

        if (space) {
            if (o > 0) pending = true;
            i += seq;
            continue;
        }
        if (pending) {
            if (o + 1 >= n) break;
            out[o++] = ' ';
            pending = false;
        }
        if (o + seq >= n) break; /* never split a UTF-8 sequence */
        for (size_t k = 0; k < seq; k++) out[o++] = in[i + k];
        i += seq;
    }
    out[o] = 0;
    return out;
}

size_t cassette_path_group(const char *path, char *out, size_t n)
{
    if (!out || n == 0) return 0;
    out[0] = 0;
    if (!path || !path[0]) return 0;
    const char *slash = strchr(path, '/');
    if (!slash || slash == path) return 0;
    size_t len = (size_t)(slash - path);
    if (len >= n) len = n - 1;
    memcpy(out, path, len);
    out[len] = 0;
    return len;
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
    c->rewinding = false;
    c->rewind_t0_ms = 0;
    c->rewind_q0 = 0;
    memset(c->rewind_offset, 0, sizeof c->rewind_offset);
    c->shown_q = 0;
    memset(c->shown_phase, 0, sizeof c->shown_phase);
    c->shown_permille = 0;
    c->shown_elapsed_ms = 0;
    c->shown_duration_ms = 0;
    c->last_bar_ms = 0;
    c->have_shown = false;
    c->have_time = false;
    c->shown_title[0] = 0;
    c->have_title = false;
    c->shown_ink_seed = 0;
    c->have_ink = false;
    /* The adapter drew something; the next frame must say what to replace it
     * with, even when it is the same empty screen. */
    c->frame.title_changed = true;
    c->frame.permille_changed = true;
    c->frame.time_changed = true;
    c->frame.ink_changed = true;
    c->frame.tape_ink = 1.0f;
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
    cassette_clean_name(c->have_status ? c->status.title : "", false, clean, sizeof clean);
    c->frame.title_changed = !c->have_title || strcmp(clean, c->shown_title) != 0;
    snprintf(c->frame.title, sizeof c->frame.title, "%s", clean);
    if (c->frame.title_changed) {
        snprintf(c->shown_title, sizeof c->shown_title, "%s", clean);
        c->have_title = true;
    }

    /* The Card's table ink: which cassette sits under the print. It follows the Track's file
     * name - the song's identity, not its place in the card - so a Track always gets the same
     * shell, whether or not it carries a Cover. */
    const char *name = c->have_status ? c->status.file : "";
    if (!name[0] && c->have_status) name = c->status.path;
    uint32_t seed = name_seed(name);
    c->frame.ink_seed = seed;
    c->frame.ink_changed = !c->have_ink || seed != c->shown_ink_seed;
    if (c->frame.ink_changed) {
        c->shown_ink_seed = seed;
        c->have_ink = true;
    }

    c->frame.playing = c->have_status && c->status.file[0] && c->status.format != MI_FMT_NONE;
    c->frame.level_l = c->have_status ? c->status.level_l : 0.0f;
    c->frame.level_r = c->have_status ? c->status.level_r : 0.0f;

    /* The VU's peak-hold: the status carries RMS, not a sample peak, so hold the
     * highest recent RMS with a fast fall. 0.90 per 40 ms step is about 0.07/s
     * (-23 dB/s), so the peak follows a real drop instead of hanging on its last
     * dash; a new peak still attacks in the same frame. */
    c->peak_l = fmaxf(c->frame.level_l, c->peak_l * 0.90f);
    c->peak_r = fmaxf(c->frame.level_r, c->peak_r * 0.90f);
    c->frame.vu_l = c->peak_l;
    c->frame.vu_r = c->peak_r;

    /* The position is stepped at the bar's period rather than at the poll rate: a
     * one-second step is ample for a bar and stops a sub-pixel change from invalidating
     * it ten times a second. The reels use the current position below, so their speed
     * follows the pack radius. The times are the status's own elapsed and duration, not
     * a re-derivation from the permille, so a track whose length is unknown still shows
     * its elapsed seconds. They are compared every step - the whole-second test is what
     * keeps the view from rewriting them - so play/stop and a newly known duration show
     * at once rather than at the next bar step. */
    uint32_t elapsed = c->have_status ? c->status.elapsed_ms : 0;
    uint32_t duration = c->have_status ? c->status.duration_ms : 0;
    uint32_t permille = timeline_permille(elapsed, duration);

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

    /* No track, no clock: an idle screen shows 0:00 / --:--. The elapsed is clamped to
     * the duration so a file's encoder padding cannot make the clock pass the end of its
     * own bar. */
    bool playing_changed = c->frame.playing != c->was_playing;
    uint32_t shown_elapsed = c->frame.playing ? elapsed : 0;
    uint32_t shown_duration = c->frame.playing ? duration : 0;
    if (shown_duration && shown_elapsed > shown_duration) shown_elapsed = shown_duration;
    bool time_changed = !c->have_time || playing_changed ||
                        shown_elapsed / 1000 != c->shown_elapsed_ms / 1000 ||
                        shown_duration / 1000 != c->shown_duration_ms / 1000;
    c->frame.time_changed = time_changed;
    c->frame.elapsed_ms = shown_elapsed;
    c->frame.duration_ms = shown_duration;
    c->shown_elapsed_ms = shown_elapsed;
    c->shown_duration_ms = shown_duration;
    c->have_time = true;

    /* A track with no duration still needs a side for the reel fill; the bar
     * stays at zero because it has nothing to measure against. */
    uint32_t position = permille;
    if (c->have_status && !c->status.duration_ms)
        position = (uint32_t)((uint64_t)elapsed * 1000ULL / CASSETTE_NOMINAL_SIDE_MS);
    if (position > 1000) position = 1000;

    /* A track change is the only thing that makes the reels jump, so it is also the
     * only thing that opens the rewind. It is decided before the phase is reset,
     * because the window has to start on the frame that is already on screen; the
     * first status after boot has no side to rewind from and is not a change. */
    bool track_change = c->frame.playing && c->was_playing && c->have_last &&
                        elapsed < c->last_elapsed;
    float from_phase[2] = { c->shown_phase[0], c->shown_phase[1] };
    uint32_t from_q = c->shown_q;

    /* The reels: each reel integrates the tape speed at its own pack radius, so
     * the supply and take-up turn at different speeds. The first step of a track
     * starts at zero; a track change (elapsed going backwards) or a stop starts
     * them again. The frame is the phase's position in the art. */
    if (c->frame.playing) {
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
    }

    /* Open the window on what is on screen and end it on what normal play shows when
     * it closes. The spin carries a whole number of revolutions: only a phase's
     * fraction selects a frame, so `offset` can be ~REVS and still start on the frame
     * that was already drawn. */
    if (track_change) {
        c->rewinding = true;
        c->rewind_t0_ms = elapsed;
        c->rewind_q0 = from_q;
        for (int i = 0; i < 2; i++) {
            float f = from_phase[i] - c->phase[i];
            c->rewind_offset[i] = f + roundf(CASSETTE_REWIND_REVS - f);
        }
    }
    /* The window is half-open: the step that reaches CASSETTE_REWIND_MS is already
     * normal play. A status that stops playing has no side to rewind, and a window
     * frozen by a paused clock would never close. */
    uint32_t since = elapsed > c->rewind_t0_ms ? elapsed - c->rewind_t0_ms : 0;
    if (c->rewinding && (!c->frame.playing || since >= CASSETTE_REWIND_MS))
        c->rewinding = false;

    c->frame.rewinding = c->rewinding;
    c->frame.rewind_permille = 0;

    /* What the screen shows: the normal state, or the window between the state it
     * opened on and the one normal play will show when it closes. Both pairs move
     * together, so the tape and the hubs decelerate as one transport. */
    uint32_t q = c->shown_permille;
    float disp[2] = { c->phase[0], c->phase[1] };
    if (c->rewinding) {
        float p = (float)since / (float)CASSETTE_REWIND_MS;
        if (p > 1.0f) p = 1.0f;
        float left = 1.0f - rewind_ease(p); /* the share of the rewind still to run */
        q = (uint32_t)((float)c->shown_permille +
                       ((float)c->rewind_q0 - (float)c->shown_permille) * left + 0.5f);
        for (int i = 0; i < 2; i++) disp[i] = c->phase[i] + c->rewind_offset[i] * left;
        c->frame.rewind_permille = (uint16_t)(p * 1000.0f + 0.5f);
    }
    /* The ring's ink is a function of the window's progress alone: full outside it,
     * and the fade's envelope inside. */
    c->frame.tape_ink = c->rewinding ? rewind_tape_ink(c->frame.rewind_permille) : 1.0f;
    if (q > 1000) q = 1000;
    c->shown_q = q;
    for (int i = 0; i < 2; i++) c->shown_phase[i] = disp[i];

    /* The empty side never goes below the minimum load: the tape edge and the VU
     * must stay outside the hub ring. */
    uint32_t load = 1000 - CASSETTE_MIN_FILL;
    c->frame.pack_fill[0] = (uint16_t)(CASSETTE_MIN_FILL + load * (1000 - q) / 1000);
    c->frame.pack_fill[1] = (uint16_t)(CASSETTE_MIN_FILL + load * q / 1000);
    for (int i = 0; i < 2; i++) {
        c->hub_frame[i] = hub_index(disp[i], c->frame_count);
        c->frame.hub_frame[i] = c->hub_frame[i];
    }

    c->was_playing = c->frame.playing;
    c->last_elapsed = c->have_status ? c->status.elapsed_ms : 0;
    c->have_last = c->have_status;

    return &c->frame;
}
