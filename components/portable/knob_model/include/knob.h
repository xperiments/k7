#pragma once
/*
 * knob - the dial: its value, its dB curve or unit fraction, its angle, the ring's inverse,
 * the crown's lit span, and the screen's one edit session. No LVGL, no ESP-IDF, no
 * allocation, no clock.
 *
 * A knob's value is an integer 0..100. The sweep says what that range means: a bipolar dB
 * knob has its detent at 50, which is exactly 0 dB; a unipolar level knob runs 0 (off) to
 * 100 (full); a stepped knob sits on one of `steps` slots along the same 270 degree arc.
 * The dB ranges are tone_eq's, not copies: TONE_EQ_GAIN_MIN_DB..MAX_DB for the trim and
 * TONE_EQ_BAND_MIN_DB..MAX_DB for the three EQ bands. Both curves are linear on each side
 * of the detent, so a knob's cut and boost are different slopes and the gain's are equal.
 * A knob's neutral is what a double tap resets it to: 50 for a dB knob, whatever the
 * control's own home is for a level or stepped one.
 *
 * The view draws straight from these functions, so their units and signs are the
 * interface:
 *   - value: an int 0..100, quantised to a slot on a stepped knob.
 *   - dB: float, from tone_eq.h's macros; 0.0f exactly at the detent. dB knobs only.
 *   - unit: float 0..1, exactly 0.0f and 1.0f at the ends. What the RGB channels publish.
 *   - angle: tenths of a degree, the unit lv_obj_set_style_transform_rotation() takes,
 *     clockwise positive. Bipolar: 0 is twelve o'clock, value 0 is +1800 and value 100 is
 *     -1800, so the detent-to-end sweep is 180 degrees either way, the bottom a seam.
 *     Unipolar and stepped: -1350 is 7:30 and +1350 is 4:30, a 270 degree arc with the gap
 *     at the bottom.
 *   - click point: dx and dy in pixels from the knob's centre, dy pointing down as the
 *     panel's coordinates do. Twelve o'clock is (0, -r), the distance is irrelevant: any
 *     non-zero r gives the same value. On the bipolar sweep (0, 0) is the detent and
 *     out-of-range angles clamp to 0 or 100; on the unipolar one the bottom gap takes
 *     whichever end is nearer, and a stepped knob rounds to the nearest slot.
 *   - lit dots: index 0 is twelve o'clock and the indices run clockwise. A bipolar crown
 *     lights from twelve towards the line - a cut the span dots clockwise (1..span), a
 *     boost the span dots counter-clockwise (dot_count-span..dot_count-1), with dot 0 the
 *     0 dB tick, span = lround(|value - 50| / 50 * (dot_count / 2)). A unipolar crown
 *     lights from the min end (7:30) clockwise to the line, and the dots in the bottom gap
 *     are never lit; both ends are the tick ink. A stepped crown lights the same way with
 *     the line on its slot's detent.
 *
 * knob_edit_t is the screen's single editor: which small knob has a pending open, which
 * one is open, and the ordering of a tap, a double tap, the delayed open and a close. It
 * is clock-free: the caller owns the 260 ms timer, the overlay, the blink and the gates
 * (shown and sliding) and feeds the events in. The id is the caller's - the model
 * never maps it to a cell, a pixel or an EQ band. Every effect is a set of instructions,
 * because a double tap both disarms the timer and resets the knob.
 */
#include <stdbool.h>

#include "tone_eq.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    KNOB_MIN = 0,
    KNOB_DETENT = 50,
    KNOB_MAX = 100,
};

/* The two dB curves the DJ block feeds. The ranges live in tone_eq.h. A dB knob is always
 * bipolar: the detent is its neutral and its twelve o'clock. */
typedef enum {
    KNOB_RANGE_GAIN = 0,   /* TONE_EQ_GAIN_MIN_DB..MAX_DB, symmetric around 0 dB */
    KNOB_RANGE_BAND,       /* TONE_EQ_BAND_MIN_DB..MAX_DB, the cut side wider */
} knob_range_t;

/* The dial's sweep: what the ring means and where a value sits on it. The bipolar sweep is
 * the mixer's - a cut/boost around a twelve o'clock detent, 180 degrees per side, with the
 * bottom a seam. The unipolar sweep is a level control's - min at 7:30, max at 4:30, 270
 * degrees, the 90 degree gap at the bottom - and the stepped sweep is that arc cut into
 * `steps` equal detents. */
typedef enum {
    KNOB_SWEEP_BIPOLAR = 0,
    KNOB_SWEEP_UNIPOLAR,
    KNOB_SWEEP_STEPPED,
} knob_sweep_t;

typedef struct {
    int value;            /* KNOB_MIN..KNOB_MAX; a stepped knob sits on one of its slots */
    knob_range_t range;   /* the dB curve knob_db() feeds; bipolar knobs only */
    knob_sweep_t sweep;
    int steps;            /* STEPPED: 2..KNOB_MAX+1 slots; 0 for the continuous sweeps */
    int neutral;          /* the reset target, 0..100: the detent for a dB knob */
    const char *name;     /* the control's identity; the caller owns it and may show it */
} knob_t;

/* A bipolar dB knob: the detent at twelve, the neutral at KNOB_DETENT. The DJ block's four,
 * and the gain. The name starts NULL. */
void knob_init(knob_t *k, knob_range_t range);

/* A unipolar level knob: 0..100 over the 270 degree sweep, neutral at full. The RGB
 * channels and the strip's brightness. The name starts NULL. */
void knob_init_unit(knob_t *k);

/* A stepped knob: `steps` equal detents on the unipolar sweep (clamped to 2..101), neutral
 * at `neutral`. A value set on it quantises to the nearest slot. The effect selector. The
 * name starts NULL. */
void knob_init_steps(knob_t *k, int steps, int neutral);

/* The control's name: a static string the caller owns, NULL for none. It is identity, not
 * a label - the view may show it (the 3x knob's pillow) or not. */
void knob_set_name(knob_t *k, const char *name);
const char *knob_name(const knob_t *k);

int knob_value(const knob_t *k);

/* Set the value, clamped to KNOB_MIN..KNOB_MAX and - on a stepped knob - quantised to the
 * nearest slot. True iff the value moved. */
bool knob_set(knob_t *k, int value);

/* The slot a stepped knob sits on, 0..steps-1; -1 for a continuous knob. */
int knob_step(const knob_t *k);

/* Move a stepped knob to slot `index` (clamped), quantising it up the same way a click does.
 * True iff the value moved. A continuous knob is untouched and reports false. */
bool knob_set_step(knob_t *k, int index);

/* Back to the neutral. True iff the value moved; the view publishes only then. */
bool knob_reset(knob_t *k);

/* The dB this value feeds tone_eq: exactly 0.0f at the detent, exactly the range's min and
 * max at 0 and 100, and linear between each anchor. Meaningful for a dB knob only. */
float knob_db(const knob_t *k);

/* The value as a 0..1 fraction: value / 100, exactly at both ends. What the RGB channels
 * and the brightness publish. */
float knob_unit(const knob_t *k);

/* The stick's angle in tenths of a degree, positive clockwise, for the knob's sweep: bipolar
 * is (KNOB_DETENT - value) * 36 and unipolar is (value - 50) * 27, so the level knob runs
 * -1350..+1350 (7:30..4:30). Pass it to lv_obj_set_style_transform_rotation() unchanged. */
int knob_angle_ddeg(const knob_t *k);

/* The ring's inverse: the value whose angle is closest to the point's, clamped to
 * KNOB_MIN..KNOB_MAX and quantised on a stepped knob. dx/dy are relative to the knob's
 * centre and dy points down. On the bipolar sweep the bottom is a seam: (0, +r) is 0 and
 * (-1, +r) is 100, and (0, 0) is the detent. On the unipolar sweep the bottom gap belongs to
 * whichever end is nearer, so a click below the knob still moves it. */
int knob_value_at(const knob_t *k, int dx, int dy);

/* Whether crown dot `index` (0 = twelve o'clock, clockwise) is lit for a crown of
 * `dot_count` dots. A bipolar crown lights from twelve towards the line; a unipolar one
 * lights from the min end (7:30) towards it. Out-of-range indices are false. */
bool knob_dot_lit(const knob_t *k, int index, int dot_count);

/* Whether dot `index` is one of the sweep's end marks, which the view paints in the tick's
 * ink: twelve o'clock on a bipolar knob, and the min and max ends (7:30 and 4:30) on a
 * unipolar or stepped one. */
bool knob_dot_tick(const knob_t *k, int index, int dot_count);

/* ------------------------------------------------------------- the edit session */

/* One per screen: there is one 800x480 overlay, so one knob is open at a time. -1 means
 * "none" for both `open` and `pending`. */
typedef struct {
    int open;             /* -1, or the id of the knob being edited */
    int pending;          /* -1, or the id whose delayed open is armed */
    bool swallow_click;   /* the click after a reset gesture is owed, exactly once */
} knob_edit_t;

/* What the caller must do. A tap can need two at once (a double tap disarms the timer
 * and resets the knob), so the model reports a set, not an enum. `id` is the knob the
 * effect is about, or -1 when every flag is false. */
typedef struct {
    bool arm_timer;     /* start the caller's delayed open for `id` */
    bool cancel_timer;  /* disarm it */
    bool reset_knob;    /* reset knob `id` to the detent and publish if it moved */
    bool open_editor;   /* show the 3x surface and dim the cassette */
    bool close_editor;  /* hide them */
    int id;
} knob_edit_effect_t;

/* Idle: nothing open, nothing pending, no click owed. */
void knob_edit_init(knob_edit_t *e);

bool knob_edit_is_editing(const knob_edit_t *e);

/* The open knob's id, or -1 when idle. */
int knob_edit_open_id(const knob_edit_t *e);

/* A click on small knob `id`. Arms the delayed open, unless an editor is open (ignored) or
 * a reset gesture still owes a swallowed click (dropped exactly once). */
knob_edit_effect_t knob_edit_tap(knob_edit_t *e, int id);

/* The reset gesture: always disarms the timer and asks for the reset. Closed, it owes the
 * following click; while `id` is open it also closes the editor and owes nothing. A call
 * for a knob that is not the open one does nothing. */
knob_edit_effect_t knob_edit_double_tap(knob_edit_t *e, int id);

/* The caller's 260 ms timer landed: open the pending knob, if one is still wanted. */
knob_edit_effect_t knob_edit_delay_expired(knob_edit_t *e);

/* The centre circle, a page flip, the block hiding, the Menu's cover: end the editor or
 * disarm a pending open. All-false when there is nothing to end. */
knob_edit_effect_t knob_edit_close(knob_edit_t *e);

#ifdef __cplusplus
}
#endif
