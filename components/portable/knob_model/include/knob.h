#pragma once
/*
 * knob - the DJ block's dial: its value, its dB curve, its angle, the ring's inverse, the
 * crown's lit span, and the screen's one edit session. No LVGL, no ESP-IDF, no allocation,
 * no clock.
 *
 * A knob's value is an integer 0..100 with the detent at 50, which is exactly 0 dB. The
 * dB ranges are tone_eq's, not copies: TONE_EQ_GAIN_MIN_DB..MAX_DB for the trim and
 * TONE_EQ_BAND_MIN_DB..MAX_DB for the three EQ bands. Both curves are linear on each side
 * of the detent, so a knob's cut and boost are different slopes and the gain's are equal.
 *
 * The view draws straight from these functions, so their units and signs are the
 * interface:
 *   - value: an int 0..100. 50 is the detent and the reset target.
 *   - dB: float, from tone_eq.h's macros; 0.0f exactly at the detent.
 *   - angle: tenths of a degree, the unit lv_obj_set_style_transform_rotation() takes.
 *     0 is twelve o'clock; positive is clockwise, which is the cut side; value 0 is
 *     +1800 and value 100 is -1800, so the detent-to-end sweep is 180 degrees either way.
 *   - click point: dx and dy in pixels from the knob's centre, dy pointing down as the
 *     panel's coordinates do. Twelve o'clock is (0, -r), clockwise is the cut side, and
 *     the distance is irrelevant: any non-zero r gives the same value. (0, 0) reads as
 *     the detent. Out-of-range angles clamp to 0 or 100.
 *   - lit dots: index 0 is twelve o'clock and the indices run clockwise. Dot 0 is never
 *     "lit": the view paints it as the 0 dB tick. A cut lights the span dots clockwise
 *     from twelve (indices 1..span); a boost lights the span dots counter-clockwise from
 *     twelve (indices dot_count-span .. dot_count-1). The span is
 *     lround(|value - 50| / 50 * (dot_count / 2)).
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

/* The two dB curves the DJ block feeds. The ranges live in tone_eq.h. */
typedef enum {
    KNOB_RANGE_GAIN = 0,   /* TONE_EQ_GAIN_MIN_DB..MAX_DB, symmetric around 0 dB */
    KNOB_RANGE_BAND,       /* TONE_EQ_BAND_MIN_DB..MAX_DB, the cut side wider */
} knob_range_t;

typedef struct {
    int value;            /* KNOB_MIN..KNOB_MAX; read with knob_value() */
    knob_range_t range;   /* the dB curve this value feeds */
    const char *name;     /* the control's identity; the caller owns it and may show it */
} knob_t;

/* Set the detent value (0 dB) and the curve; the name starts NULL. */
void knob_init(knob_t *k, knob_range_t range);

/* The control's name: a static string the caller owns, NULL for none. It is identity, not
 * a label - the view may show it (the 3x knob's pillow) or not. */
void knob_set_name(knob_t *k, const char *name);
const char *knob_name(const knob_t *k);

int knob_value(const knob_t *k);

/* Set the value, clamped to KNOB_MIN..KNOB_MAX. True iff the value moved. */
bool knob_set(knob_t *k, int value);

/* Back to the detent (0 dB). True iff the value moved; the view publishes only then. */
bool knob_reset(knob_t *k);

/* The dB this value feeds tone_eq: exactly 0.0f at the detent, exactly the range's min and
 * max at 0 and 100, and linear between each anchor. */
float knob_db(const knob_t *k);

/* The stick's angle in tenths of a degree: (KNOB_DETENT - value) * 36, so 36 per unit,
 * positive clockwise. Pass it to lv_obj_set_style_transform_rotation() unchanged. */
int knob_angle_ddeg(const knob_t *k);

/* The ring's inverse: the value whose angle is closest to the point's, clamped to
 * KNOB_MIN..KNOB_MAX. dx/dy are relative to the knob's centre and dy points down. The
 * dial is 180 degrees per half, so the bottom is a seam: (0, +r) is 0 and (-1, +r) is 100.
 * (0, 0) is the detent. */
int knob_value_at(int dx, int dy);

/* Whether crown dot `index` (0 = twelve o'clock, clockwise) is lit for a crown of
 * `dot_count` dots. Dot 0 is never lit and out-of-range indices are false; the lit run is
 * documented at the top of this file. */
bool knob_dot_lit(const knob_t *k, int index, int dot_count);

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
