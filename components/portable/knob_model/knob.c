/*
 * The DJ block's knob model; see knob.h.
 *
 * The arithmetic is the whole point: one piecewise-linear dB law for both ranges, one
 * angle law per sweep and its inverse, and the crown's span. The edit session above it is only
 * ordering - which knob is pending, which is open, and what a tap, a double tap, the
 * delayed open and a close report. Nothing here knows LVGL, the panel, a timer or the
 * broadcast.
 */
#include "knob.h"

#include <math.h>

#define PI_F 3.14159265358979323846f

/* ------------------------------------------------------------------------ value */

/* Tenths of a degree per value step: 180 degrees over half the dial for the mixer's bipolar
 * sweep, 270 degrees over the whole dial for a level control's unipolar one. */
#define DEG10_PER_UNIT 36
#define DEG10_PER_UNIT_LEVEL 27
/* The unipolar sweep's half-span: min at 7:30 is -135 degrees, max at 4:30 is +135. */
#define LEVEL_HALF_DEG 135.0f

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

/* The fraction of the sweep the value covers, 0..1, whatever the sweep. */
static float sweep_fraction(const knob_t *k)
{
    int count = step_count(k);
    if (count >= 2) return (float)knob_step(k) / (float)(count - 1);
    return (float)k->value / (float)KNOB_MAX;
}

/* Bipolar: 0 dB at twelve, positive clockwise (the cut side), 180 degrees per half.
 * Unipolar and stepped: the level arc, -135 degrees at the min end, +135 at the max. */
int knob_angle_ddeg(const knob_t *k)
{
    if (!k) return 0;
    if (k->sweep == KNOB_SWEEP_BIPOLAR) return (KNOB_DETENT - k->value) * DEG10_PER_UNIT;
    return (k->value - KNOB_DETENT) * DEG10_PER_UNIT_LEVEL;
}

/* The inverse of the angle. The bipolar sweep keeps the bottom seam a 180-degree-per-half
 * dial always has: atan2f(dx, -dy) is 0 at twelve and positive clockwise, and the division
 * by 36 is the same lroundf the view has always used. The unipolar sweep's bottom gap has no
 * value of its own, so a click in it takes whichever end is nearer; a stepped knob then
 * rounds to its nearest slot. */
int knob_value_at(const knob_t *k, int dx, int dy)
{
    if (!k) return KNOB_DETENT;
    /* The dead centre is the knob's own value: a click there is not a move. */
    if (dx == 0 && dy == 0) return k->sweep == KNOB_SWEEP_BIPOLAR ? KNOB_DETENT : k->value;

    float deg = atan2f((float)dx, -(float)dy) * 180.0f / PI_F;

    if (k->sweep == KNOB_SWEEP_BIPOLAR) {
        int value = KNOB_DETENT - (int)lroundf(deg * ((float)KNOB_DETENT / 180.0f));
        return clamp_value(value);
    }

    if (deg < -LEVEL_HALF_DEG) deg = -LEVEL_HALF_DEG;
    if (deg > LEVEL_HALF_DEG) deg = LEVEL_HALF_DEG;
    int value = KNOB_DETENT + (int)lroundf(deg * ((float)KNOB_DETENT / LEVEL_HALF_DEG));
    return quantise(k, clamp_value(value));
}

/* The crown. Bipolar: dot 0 is the 0 dB tick, a cut lights indices 1..span clockwise and a
 * boost the span dots counter-clockwise from twelve. Unipolar and stepped: the arc starts at
 * 7:30 (index 21 of 24) and runs clockwise, every dot up to the line is lit, and the dots in
 * the bottom gap stay dark. */
bool knob_dot_lit(const knob_t *k, int index, int dot_count)
{
    if (!k || dot_count <= 0 || index < 0 || index >= dot_count) return false;

    if (k->sweep == KNOB_SWEEP_BIPOLAR) {
        if (index == 0) return false;
        int delta = k->value - KNOB_DETENT;
        if (delta == 0) return false;

        int half = dot_count / 2;
        int span = (int)lroundf(fabsf((float)delta) / (float)KNOB_DETENT * (float)half);
        if (span > half) span = half;
        if (delta < 0) return index <= span;
        return index >= dot_count - span;
    }

    /* Where the dot sits along the 270 degree arc, in degrees clockwise from the min end
     * (7:30 is 225 degrees clockwise from twelve). */
    float dot_deg = (float)index * 360.0f / (float)dot_count;
    float along = dot_deg - 225.0f;
    if (along < 0.0f) along += 360.0f;
    if (along > 270.0f) return false;

    float progress = sweep_fraction(k) * 270.0f;
    return along <= progress;
}

/* The ends of the sweep, which the view paints in the tick's ink: twelve o'clock for a
 * bipolar knob, and both ends of the arc (7:30 and 4:30) for a level one. */
bool knob_dot_tick(const knob_t *k, int index, int dot_count)
{
    if (!k || dot_count <= 0 || index < 0 || index >= dot_count) return false;
    if (k->sweep == KNOB_SWEEP_BIPOLAR) return index == 0;

    int min_index = (225 * dot_count + 180) / 360 % dot_count;
    int max_index = (135 * dot_count + 180) / 360 % dot_count;
    return index == min_index || index == max_index;
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
