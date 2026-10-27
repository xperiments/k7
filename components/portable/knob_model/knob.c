/*
 * The DJ block's knob model; see knob.h.
 *
 * The arithmetic is the whole point: one piecewise-linear dB law for both ranges, one
 * angle formula and its inverse, and the crown's span. The edit session above it is only
 * ordering - which knob is pending, which is open, and what a tap, a double tap, the
 * delayed open and a close report. Nothing here knows LVGL, the panel, a timer or the
 * broadcast.
 */
#include "knob.h"

#include <math.h>

#define PI_F 3.14159265358979323846f

/* Tenths of a degree per value step: 180 degrees over half the dial. */
#define DEG10_PER_UNIT 36

static int clamp_value(int value)
{
    if (value < KNOB_MIN) return KNOB_MIN;
    if (value > KNOB_MAX) return KNOB_MAX;
    return value;
}

/* ------------------------------------------------------------------------ value */

void knob_init(knob_t *k, knob_range_t range)
{
    if (!k) return;
    k->value = KNOB_DETENT;
    k->range = range;
    k->name = NULL;
}

void knob_set_name(knob_t *k, const char *name)
{
    if (k) k->name = name;
}

const char *knob_name(const knob_t *k)
{
    return k ? k->name : NULL;
}

int knob_value(const knob_t *k)
{
    return k ? k->value : KNOB_DETENT;
}

bool knob_set(knob_t *k, int value)
{
    if (!k) return false;
    int clamped = clamp_value(value);
    if (clamped == k->value) return false;
    k->value = clamped;
    return true;
}

bool knob_reset(knob_t *k)
{
    return knob_set(k, KNOB_DETENT);
}

/* One law for both ranges, linear on each side of the detent. Multiplying before dividing
 * keeps the anchors exact (50 * 12 / 50 is exactly 12.0f; 50 * 0.24f need not be).
 * The band's cut side is the wider one, so -TONE_EQ_BAND_MIN_DB is its span. */
float knob_db(const knob_t *k)
{
    if (!k) return 0.0f;
    int delta = k->value - KNOB_DETENT;
    if (k->range == KNOB_RANGE_GAIN)
        return (float)delta * TONE_EQ_GAIN_MAX_DB / (float)KNOB_DETENT;
    if (delta >= 0)
        return (float)delta * TONE_EQ_BAND_MAX_DB / (float)KNOB_DETENT;
    return (float)delta * -TONE_EQ_BAND_MIN_DB / (float)KNOB_DETENT;
}

/* ----------------------------------------------------------------------- drawing */

/* 0 dB at twelve, positive clockwise (the cut side), in tenths of a degree. */
int knob_angle_ddeg(const knob_t *k)
{
    if (!k) return 0;
    return (KNOB_DETENT - k->value) * DEG10_PER_UNIT;
}

/* The inverse of the angle, with the bottom seam a 180-degree-per-half dial always has.
 * atan2f(dx, -dy) is 0 at twelve and positive clockwise; the division by 36 is the same
 * lroundf the view has always used, so a click still moves by the nearest value. */
int knob_value_at(int dx, int dy)
{
    if (dx == 0 && dy == 0) return KNOB_DETENT;
    float deg = atan2f((float)dx, -(float)dy) * 180.0f / PI_F;
    int value = KNOB_DETENT - (int)lroundf(deg * ((float)KNOB_DETENT / 180.0f));
    return clamp_value(value);
}

/* The crown: dot 0 is the 0 dB tick (never lit), a cut lights indices 1..span clockwise and
 * a boost lights the span dots counter-clockwise from twelve. */
bool knob_dot_lit(const knob_t *k, int index, int dot_count)
{
    if (!k || dot_count <= 0 || index <= 0 || index >= dot_count) return false;
    int delta = k->value - KNOB_DETENT;
    if (delta == 0) return false;

    int half = dot_count / 2;
    int span = (int)lroundf(fabsf((float)delta) / (float)KNOB_DETENT * (float)half);
    if (span > half) span = half;
    if (delta < 0) return index <= span;
    return index >= dot_count - span;
}

/* ------------------------------------------------------------------ edit session */

static knob_edit_effect_t no_effect(void)
{
    knob_edit_effect_t effect = { .id = -1 };
    return effect;
}

void knob_edit_init(knob_edit_t *e)
{
    if (!e) return;
    e->open = -1;
    e->pending = -1;
    e->swallow_click = false;
}

bool knob_edit_is_editing(const knob_edit_t *e)
{
    return e && e->open >= 0;
}

int knob_edit_open_id(const knob_edit_t *e)
{
    return knob_edit_is_editing(e) ? e->open : -1;
}

knob_edit_effect_t knob_edit_tap(knob_edit_t *e, int id)
{
    knob_edit_effect_t effect = no_effect();
    if (!e || id < 0) return effect;

    /* The second CLICKED of a double click: the reset gesture already ran and this click
     * must not start a new delayed open. It is swallowed exactly once. */
    if (e->swallow_click) {
        e->swallow_click = false;
        return effect;
    }
    /* One editor at a time: a tap on another cell while the overlay is up is not its. */
    if (e->open >= 0) return effect;

    e->pending = id;
    effect.arm_timer = true;
    effect.id = id;
    return effect;
}

knob_edit_effect_t knob_edit_double_tap(knob_edit_t *e, int id)
{
    knob_edit_effect_t effect = no_effect();
    if (!e || id < 0) return effect;
    if (e->open >= 0 && e->open != id) return effect;

    /* Always the mixer's reset: disarm the pending open, reset the knob. A double tap on
     * the small knob owes the following click; a double tap while that knob is open
     * closes the editor instead, and the overlay has already consumed the click. */
    e->pending = -1;
    effect.cancel_timer = true;
    effect.reset_knob = true;
    effect.id = id;
    if (e->open == id) {
        e->open = -1;
        e->swallow_click = false;
        effect.close_editor = true;
    } else {
        e->swallow_click = true;
    }
    return effect;
}

knob_edit_effect_t knob_edit_delay_expired(knob_edit_t *e)
{
    knob_edit_effect_t effect = no_effect();
    if (!e) return effect;

    int id = e->pending;
    e->pending = -1;
    if (id < 0 || e->open >= 0) return effect;

    e->open = id;
    effect.open_editor = true;
    effect.id = id;
    return effect;
}

knob_edit_effect_t knob_edit_close(knob_edit_t *e)
{
    knob_edit_effect_t effect = no_effect();
    if (!e) return effect;

    if (e->open >= 0) {
        effect.close_editor = true;
        effect.id = e->open;
        e->open = -1;
        e->pending = -1;
        e->swallow_click = false;
    } else if (e->pending >= 0) {
        /* Nothing is open but a delayed open is armed (the block hid or its panel was
         * swapped out): disarm it, so a stale pending cannot open a knob later. */
        effect.cancel_timer = true;
        effect.id = e->pending;
        e->pending = -1;
        e->swallow_click = false;
    }
    return effect;
}
