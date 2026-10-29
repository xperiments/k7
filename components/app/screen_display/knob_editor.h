#pragma once
/*
 * knob_editor - the one 3x dial, the screen's one edit session, and the bank it is editing.
 *
 * The right bar's panels own their cells: the small dials, their values and what those
 * publish. This module owns the other half of the idiom - the mode a tapped knob opens: the
 * 256 px dial over the cassette, its pillow, its plaques, the 260 ms delay before it opens,
 * and the cassette's cut-out. Two panels use it (the EQ group and the RGB group)
 * and there is still exactly one dial, because exactly one knob is open.
 *
 * **The dial is a display; the Pad is the control.** A knob's value moves by dragging the
 * small cell (2 px a step). A tap on that cell opens the dial, and from then on the right bar
 * shows the **Pad** where the group's cells were: its ratchet drives the knob at 4 px a step,
 * its tap cycles the knob's stops (knob_cycle()), and its left swipe closes the dial
 * (ADR 0028). Nothing on the dial itself takes a touch - not its ring, not its plaques, not
 * its centre - so the surfaces that act are the two the screen already teaches: the group's
 * cells, and the Pad.
 *
 * While a drag is in progress the pillow shows the live value; a second after the finger
 * lifts it goes back to knob_label(). Every line on the dial - the marks, the graduations,
 * the value marker - is knob_cell's, so the two sizes cannot drift apart.
 *
 * A **bank** is what the panel hands over: the knobs, what to repaint when a value moves, what
 * to publish, and whether the editor may open at all. The editor never reads a palette, a band or an LED:
 * a value moves, the panel is told, the panel publishes. What the dial says is the knob's own
 * business - knob_label() and the knob's marked positions - so the bank has no label hook.
 *
 * A bank's cells are **absent while the editor owns the bar**: the Pad has replaced them, so
 * a panel's repaint and disc hooks must tolerate being called when its cells are gone.
 *
 * Private to screen_display. create() builds the dial hidden, once, then each panel calls
 * set_bank() when it is built and close() when it goes away; every object is built under the
 * LVGL lock.
 */
#include <stdbool.h>

#include "lvgl.h"

#include "knob.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What the panel hands the editor while its cells are up. Every hook may be NULL except
 * `knobs` and `count`; a bank with no knobs is not installed. */
typedef struct {
    knob_t *knobs;                              /* the panel's values, in its own id order */
    int count;
    /* The knob whose cell is at a screen point, or -1. */
    /* Repaint the small knob's dial after its value moved. Not called while the editor owns
     * the bar: the cells are gone then, and only the 3x dial is on screen. */
    void (*repaint)(int id, void *ctx);
    /* The value moved or was reset: send it where it goes (a band, the strip). */
    void (*publish)(int id, void *ctx);
    /* Whether the panel is up and settled, re-checked when the delayed open lands. */
    bool (*open_allowed)(void *ctx);
    void *ctx;
} knob_editor_bank_t;

/* Build the dial, hidden, on `parent` (the screen). Call once, after the bars exist. */
void knob_editor_create(lv_obj_t *parent);

/* The bar the editor borrows while a knob is open. `claim` installs the Pad where the group's
 * cells were; `release` puts the active group's panel back. Both may be NULL, in which case
 * the editor still opens but cannot offer the Pad (the dial would be read-only). */
void knob_editor_set_bar(void (*claim)(void *ctx), void (*release)(void *ctx), void *ctx);

/* Install the bank the panels' cells belong to, or NULL to leave the editor with none. A new
 * bank ends any edit of the old one first; the values are the panel's and outlive the swap. */
void knob_editor_set_bank(const knob_editor_bank_t *bank);

/* True while a knob is in edit mode. The view drops every screen intent then. */
bool knob_editor_is_editing(void);

/* The panel is going away (its bar slid out, or its cells were swapped): end the edit or
 * disarm the pending open, so neither the Pad nor the dial can outlive the cells. */
void knob_editor_close(void);

/* A small knob's CLICKED and DOUBLE_CLICKED: the tap that arms the delayed open, and the
 * reset gesture. The caller applies the shown/settled gate first. */
void knob_editor_tap(int id);
void knob_editor_double_tap(int id);

/* A vertical drag on small knob `id`: the panel calls these from its cell's PRESSED,
 * PRESSING and RELEASED. The travel is read from the active input device, measured from the
 * point the press began on, so the value follows the finger and not the cell. A 6 px dead
 * zone keeps a tap a tap - and a drag is never a tap, so the click that ends one is dropped;
 * 2 px of travel is one value step. */
void knob_editor_drag_begin(int id);
void knob_editor_drag_update(void);
void knob_editor_drag_end(void);

#ifdef __cplusplus
}
#endif
