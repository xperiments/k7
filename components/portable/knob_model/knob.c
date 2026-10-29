/*
 * The DJ block's knob model; see knob.h.
 *
 * The arithmetic is the whole point: one piecewise-linear dB law for both ranges, one arc
 * for every sweep, the magnet that holds a detent, and the drag that moves the value. The
 * edit session above it is only ordering - which knob is pending, which is open, and what a
 * tap, a double tap, the delayed open and a close report. Nothing here knows LVGL, the
 * panel, a timer or the broadcast.
 */
#include "knob.h"

#include <math.h>

/* Tenths of a degree per value step over the one 270 degree arc: 135 degrees per side of
 * twelve o'clock, 50 steps per side, 2.7 degrees each. */
#define DEG10_PER_UNIT 27

static int clamp_value(int value)
{
    if (value < KNOB_MIN) return KNOB_MIN;
    if (value > KNOB_MAX) return KNOB_MAX;
    return value;
}

static bool is_stepped(const knob_t *k)
{
    return k && k->sweep == KNOB_SWEEP_STEPPED && k->steps >= 2;
}

static int step_count(const knob_t *k)
{
    return is_stepped(k) ? k->steps : 0;
}

/* The value slot `index` sits on: the slots are spread over the same 0..100 range, so 6
 * slots are 0/20/40/60/80/100. */
static int step_value(const knob_t *k, int index)
{
    int count = step_count(k);
    if (count < 2) return 0;
    if (index < 0) index = 0;
    if (index > count - 1) index = count - 1;
    return (int)lroundf((float)index * (float)KNOB_MAX / (float)(count - 1));
}

/* A stepped knob only ever sits on a slot; everything else is already whatever it is. */
static int quantise(const knob_t *k, int value)
{
    int count = step_count(k);
    if (count < 2) return value;
    return step_value(k, (int)lroundf((float)value * (float)(count - 1) / (float)KNOB_MAX));
}

void knob_init(knob_t *k, knob_range_t range)
{
    if (!k) return;
    k->value = KNOB_DETENT;
    k->range = range;
    k->sweep = KNOB_SWEEP_BIPOLAR;
    k->steps = 0;
    k->neutral = KNOB_DETENT;
    k->name = NULL;
    k->mark_count = 0;
}

void knob_init_unit(knob_t *k)
{
    if (!k) return;
    k->value = KNOB_MAX;
    k->range = KNOB_RANGE_GAIN;   /* unused: a level knob feeds knob_unit(), not dB */
    k->sweep = KNOB_SWEEP_UNIPOLAR;
    k->steps = 0;
    k->neutral = KNOB_MAX;
    k->name = NULL;
    k->mark_count = 0;
}

void knob_init_steps(knob_t *k, int steps, int neutral)
{
    if (!k) return;
    if (steps < 2) steps = 2;
    if (steps > KNOB_MAX + 1) steps = KNOB_MAX + 1;
    k->range = KNOB_RANGE_GAIN;   /* unused */
    k->sweep = KNOB_SWEEP_STEPPED;
    k->steps = steps;
    k->neutral = clamp_value(neutral);
    /* The neutral lands on the nearest slot, so a fresh stepped knob is already valid. */
    k->value = quantise(k, k->neutral);
    k->name = NULL;
    k->mark_count = 0;
}

void knob_set_name(knob_t *k, const char *name)
{
    if (k) k->name = name;
}

const char *knob_name(const knob_t *k)
{
    return k ? k->name : NULL;
}

void knob_set_marks(knob_t *k, const knob_mark_t *marks, int count)
{
    if (!k) return;
    if (!marks || count < 0) count = 0;
    if (count > KNOB_MARKS_MAX) count = KNOB_MARKS_MAX;
    for (int i = 0; i < count; i++) k->mark[i] = marks[i];
    k->mark_count = count;
}

int knob_mark_count(const knob_t *k)
{
    return k ? k->mark_count : 0;
}

const knob_mark_t *knob_mark_at(const knob_t *k, int index)
{
    if (!k || index < 0 || index >= k->mark_count) return NULL;
    return &k->mark[index];
}

/* The panel's strings are identity, not layout: the model only says which one is current. */
const char *knob_label(const knob_t *k)
{
    if (!k) return NULL;
    for (int i = 0; i < k->mark_count; i++) {
        if (k->mark[i].value == k->value) return k->mark[i].text;
    }
    return k->name;
}

int knob_value(const knob_t *k)
{
    return k ? k->value : KNOB_DETENT;
}

bool knob_set(knob_t *k, int value)
{
    if (!k) return false;
    int clamped = quantise(k, clamp_value(value));
    if (clamped == k->value) return false;
    k->value = clamped;
    return true;
}

int knob_step(const knob_t *k)
{
    int count = step_count(k);
    if (count < 2) return -1;
    return (int)lroundf((float)k->value * (float)(count - 1) / (float)KNOB_MAX);
}

bool knob_set_step(knob_t *k, int index)
{
    int count = step_count(k);
    if (count < 2) return false;
    return knob_set(k, step_value(k, index));
}

bool knob_reset(knob_t *k)
{
    return k ? knob_set(k, k->neutral) : false;
}

/* One law for both dB ranges, linear on each side of the detent. Multiplying before dividing
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

float knob_unit(const knob_t *k)
{
    if (!k) return 0.0f;
    return (float)k->value / (float)KNOB_MAX;
}

/* ----------------------------------------------------------------------- drawing */

int knob_angle_for_value(int value)
{
    return (clamp_value(value) - KNOB_DETENT) * DEG10_PER_UNIT;
}

int knob_angle_ddeg(const knob_t *k)
{
    return knob_angle_for_value(knob_value(k));
}

/* ------------------------------------------------------------------- the magnet */

/* One candidate: the nearer of the value itself and `anchor`, if it is inside the band its
 * own stickiness gives it. `best`/`best_d` are the running winner. */
static void mark_candidate(const knob_t *k, int anchor, int value, int *best, int *best_d)
{
    int band = (k->value == anchor) ? KNOB_SNAP_RELEASE : KNOB_SNAP_STICK;
    int d = (anchor > value) ? anchor - value : value - anchor;
    if (d > band || d >= *best_d) return;
    *best = anchor;
    *best_d = d;
}

int knob_snap(const knob_t *k, int value)
{
    if (!k) return value;
    int best = value;
    /* Wider than any distance on the dial, so the first candidate inside its band always
     * wins and every later one has to be nearer. */
    int best_d = KNOB_MAX + 1;

    mark_candidate(k, k->neutral, value, &best, &best_d);
    for (int i = 0; i < k->mark_count; i++)
        mark_candidate(k, k->mark[i].value, value, &best, &best_d);

    /* A stepped knob's every slot is a place it can sit, so every slot is a detent. */
    int count = step_count(k);
    for (int i = 0; i < count; i++)
        mark_candidate(k, step_value(k, i), value, &best, &best_d);

    return best;
}

int knob_drag_value(const knob_t *k, int start, int dy, int px_per_value)
{
    if (!k) return start;
    if (px_per_value < 1) px_per_value = 1;
    int value = quantise(k, clamp_value(start + dy / px_per_value));
    return knob_snap(k, value);
}

/* One candidate stop: the lowest of all of them, and the lowest one above `value`. Duplicates
 * (a mark on the neutral, a slot on a mark) cost nothing: they only fail to be smaller. */
static void consider_stop(int stop, int value, int *next, int *lowest)
{
    if (*lowest < 0 || stop < *lowest) *lowest = stop;
    if (stop > value && (*next < 0 || stop < *next)) *next = stop;
}

int knob_cycle(const knob_t *k)
{
    if (!k) return KNOB_DETENT;

    int next = -1;
    int lowest = -1;
    consider_stop(k->neutral, k->value, &next, &lowest);
    for (int i = 0; i < k->mark_count; i++)
        consider_stop(k->mark[i].value, k->value, &next, &lowest);

    int count = step_count(k);
    for (int i = 0; i < count; i++)
        consider_stop(step_value(k, i), k->value, &next, &lowest);

    if (next >= 0) return next;
    return lowest >= 0 ? lowest : k->value;
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
