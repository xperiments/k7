#pragma once
/*
 * knob - the dial: its value, its dB curve or unit fraction, its angle, its labelled
 * positions and their magnet, its drag, and the screen's one edit session. No LVGL, no
 * ESP-IDF, no allocation, no clock.
 *
 * A knob's value is an integer 0..100. The sweep says what that range means: a bipolar dB
 * knob has its detent at 50, which is exactly 0 dB; a unipolar level knob runs 0 (off) to
 * 100 (full); a stepped knob sits on one of `steps` slots along the same arc. The dB ranges
 * are tone_eq's, not copies: TONE_EQ_GAIN_MIN_DB..MAX_DB for the trim and
 * TONE_EQ_BAND_MIN_DB..MAX_DB for the three EQ bands. Both curves are linear on each side
 * of the detent, so a knob's cut and boost are different slopes and the gain's are equal.
 * A knob's neutral is what a reset returns it to: 50 for a dB knob, whatever the control's
 * own home is for a level or stepped one.
 *
 * The arc is one arc for every sweep: 270 degrees, the min end at 7:30 and the max at 4:30,
 * with a dead 90 degree gap at the bottom. There is no seam - a value is a place on the arc,
 * and the two ends are two places, not one. The value is drawn clockwise-positive: at twelve
 * it is 0; the cut side is on the left, so -26 dB sits at 7:30 and +6 dB at 4:30 (an
 * amendment to ADR 0018's "the right side cuts", ADR 0027).
 *
 * The view draws straight from these functions, so their units and signs are the
 * interface:
 *   - value: an int 0..100, quantised to a slot on a stepped knob.
 *   - dB: float, from tone_eq.h's macros; 0.0f exactly at the detent. dB knobs only.
 *   - unit: float 0..1, exactly 0.0f and 1.0f at the ends. What the RGB channels publish.
 *   - angle: tenths of a degree, the unit lv_obj_set_style_transform_rotation() takes,
 *     clockwise positive. Every sweep: 0 is twelve o'clock, -1350 is 7:30, +1350 is 4:30,
 *     and one value step is 27 tenths of a degree (2.7 degrees).
 *   - drag: the value a vertical drag has reached from the value it started on, at so many
 *     pixels per step, clamped, quantised on a stepped knob and then magnetised.
 *   - labels: the knob's `name` and up to KNOB_MARKS_MAX labelled positions. knob_label()
 *     reads the position the knob is on, so a selector's pillow says which effect is
 *     chosen and a range knob's says HI until you reach an end. A mark with `plaque` asks
 *     the view to draw it as a pressable label at its angle (two ends on a range knob, a
 *     row of names on a selector).
 *   - the magnet: knob_snap() pulls a value onto the knob's own positions - its neutral and
 *     its marks (and every slot of a stepped knob). The pull is KNOB_SNAP_STICK wide, and
 *     KNOB_SNAP_RELEASE wide for the position the knob is already sitting on, so a detent
 *     holds on and takes a firm push to leave.
 *
 * knob_edit_t is the screen's single editor: which small knob has a pending open, which
 * one is open, and the ordering of a tap, a double tap, the delayed open and a close. It
 * is clock-free: the caller owns the 260 ms timer, the overlay, the blink and the gates
 * (shown and sliding) and feeds the events in. The id is the caller's - the model never
 * maps it to a cell, a pixel or an EQ band. Every effect is a set of instructions, because
 * a double tap both disarms the timer and resets the knob.
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

/* A labelled position on the dial: the value, the panel's own static string, and whether
 * the view draws it as a plaque at that angle. `plaque` is the view's business, asked for by
 * the panel: a range knob's two ends are worth naming on the dial, a selector's row of names
 * usually is not (its pillow names the choice). */
typedef struct {
    int value;            /* KNOB_MIN..KNOB_MAX */
    const char *text;
    bool plaque;
} knob_mark_t;

/* Enough for a selector's slots with room to spare; the largest in the build is the RGB
 * group's six effects. A knob is a value in .bss, so this is a fixed array, not a list. */
#define KNOB_MARKS_MAX 8

/* The magnet: how close a value comes before it is pulled onto a position, and how far away
 * it has to get before the position it is already on lets go. */
#define KNOB_SNAP_STICK   3
#define KNOB_SNAP_RELEASE 5

/* The two dB curves the DJ block feeds. The ranges live in tone_eq.h. A dB knob is always
 * bipolar: the detent is its neutral and its twelve o'clock. */
typedef enum {
    KNOB_RANGE_GAIN = 0,   /* TONE_EQ_GAIN_MIN_DB..MAX_DB, symmetric around 0 dB */
    KNOB_RANGE_BAND,       /* TONE_EQ_BAND_MIN_DB..MAX_DB, the cut side wider */
} knob_range_t;

/* The dial's sweep: what the ring means. All three share the one 270 degree arc; the sweep
 * decides the law over it - a cut/boost around a twelve o'clock detent, a level from min to
 * max, or `steps` equal detents - and, for a stepped knob, that only its slots are values. */
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
    int neutral;          /* the reset target and a magnet, 0..100 */
    const char *name;     /* the control's identity; the caller owns it and may show it */
    knob_mark_t mark[KNOB_MARKS_MAX];   /* the labelled positions, copied in */
    int mark_count;
} knob_t;

/* A bipolar dB knob: the detent at twelve, the neutral at KNOB_DETENT. The DJ block's four,
 * and the gain. The name starts NULL and there are no marked positions. */
void knob_init(knob_t *k, knob_range_t range);

/* A unipolar level knob: 0..100 over the 270 degree arc, neutral at full. The RGB
 * channels and the strip's brightness. The name starts NULL. */
void knob_init_unit(knob_t *k);

/* A stepped knob: `steps` equal detents on the 270 degree arc (clamped to 2..101), neutral
 * at `neutral`. A value set on it quantises to the nearest slot. The effect selector. The
 * name starts NULL. */
void knob_init_steps(knob_t *k, int steps, int neutral);

/* The control's name: a static string the caller owns, NULL for none. It is identity, not
 * a label - the view shows it in the dial's pillow. */
void knob_set_name(knob_t *k, const char *name);
const char *knob_name(const knob_t *k);

/* The labelled positions, copied into the knob so a table built on the stack is safe;
 * `count` is clamped to 0..KNOB_MARKS_MAX. NULL or a count of 0 clears them. */
void knob_set_marks(knob_t *k, const knob_mark_t *marks, int count);
int knob_mark_count(const knob_t *k);
const knob_mark_t *knob_mark_at(const knob_t *k, int index);

/* What the dial's pillow says: the text of the marked position the knob is sitting on, else
 * the knob's name, else NULL. A stepped knob's marked slots make this the selected value. */
const char *knob_label(const knob_t *k);

int knob_value(const knob_t *k);

/* Set the value, clamped to KNOB_MIN..KNOB_MAX and - on a stepped knob - quantised to the
 * nearest slot. True iff the value moved. No magnet: this is the view's jump and the
 * panel's own restore, not a drag. */
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

/* Where any value sits on the arc, in tenths of a degree, positive clockwise: (value - 50)
 * * 27, so 0 is -1350 (7:30), 50 is 0 (twelve) and 100 is +1350 (4:30). The view reads the
 * three printed marks and the value marker through this one formula. */
int knob_angle_for_value(int value);

/* The stick's angle for the knob's value; a convenience for knob_angle_for_value(). Pass it
 * to lv_obj_set_style_transform_rotation() unchanged. */
int knob_angle_ddeg(const knob_t *k);

/* Pull `value` onto the knob's own positions: its neutral and its marks, plus every slot of
 * a stepped knob. The nearest position within KNOB_SNAP_STICK wins, except the position the
 * knob is already on, whose band is KNOB_SNAP_RELEASE - that pair is the detent's hysteresis,
 * and it is why a drag can pass through twelve but a knob left alone stays exactly on it.
 * Returns `value` untouched when no position is near. */
int knob_snap(const knob_t *k, int value);

/* The value a vertical drag has reached. `start` is the value the press began on, `dy` the
 * total travel since, positive up (a drag up boosts), `px_per_value` the travel of one step
 * (2 px at 1x, 4 at 3x). The result is clamped, quantised on a stepped knob and magnetised
 * with knob_snap(), so a drag cannot land next to a detent. */
int knob_drag_value(const knob_t *k, int start, int dy, int px_per_value);

/* The next **stop** after the knob's value, wrapping round to the lowest when the value is on
 * the highest. A stop is a position the knob means to be visited at: its neutral, its marked
 * positions and - on a stepped knob - every slot. So a tap on a gain knob visits -12, then
 * 0 dB, then +12, then -12 again, and a tap on a selector walks its slots. A value that is
 * not a stop goes to the next one above it; a knob with a single stop (one neutral and no
 * marks) returns it, which is a jump home. */
int knob_cycle(const knob_t *k);

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
    bool reset_knob;    /* reset knob `id` to its neutral and publish if it moved */
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
 * a reset gesture still owes a swallowed click (dropped exactly once). A click is never how
 * a value moves: the drag is (ADR 0027). */
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
