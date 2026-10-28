#pragma once
/*
 * knob_editor - the one 3x dial, the screen's one edit session, and the bank it is editing.
 *
 * The right bar's panels own their cells: the small 44 px circles, their crowns, their lines
 * and what their values publish. This module owns the other half of the idiom - the modal
 * surface a tapped knob opens: the full-screen transparent overlay that takes every touch, the
 * 256 px dial over the cassette, its crown, its line and its name pillow, the 260 ms delay
 * before it opens, the blink, and the cassette's cut-out. Two panels use it (the EQ group and
 * the RGB group) and there is still exactly one overlay, because exactly one knob is open.
 *
 * A **bank** is what the panel hands over: the knobs, how a screen point maps back to one of
 * their cells, each knob's pillow text, what to repaint when a value moves, what to publish,
 * the small disc to blink, and whether the editor may open at all. The editor never reads a
 * palette, a band or an LED: a value moves, the panel is told, the panel publishes.
 *
 * The ordering is knob_edit_t's (knob_model): which knob is pending, which is open, the reset
 * gesture and its owed click, the close. This module feeds those events in and applies the
 * effects it gets back, so it owns only the timer, the overlay, the blink and the dim.
 *
 * A tap on a small knob arms the delayed open; the second tap of a double tap lands on the 3x
 * dial and resets there, so the reset gesture survives the short delay. A click on the centre
 * circle closes; a click on the ring - the 256 px area around it - is the target point and the
 * knob jumps to that angle; a click outside does nothing. There is no drag. The pillow under
 * the dial carries the bank's label for the knob (GAIN, or an effect selector's effect name).
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
    /* The knob whose cell is at a screen point, or -1. The overlay covers the cells, so a
     * double tap on a small knob maps back through this. */
    int (*id_at)(int x, int y, void *ctx);
    /* The pillow's text for a knob, or NULL to use knob_name(). */
    const char *(*label)(int id, void *ctx);
    /* Repaint the small knob's crown and line after its value moved. */
    void (*repaint)(int id, void *ctx);
    /* The value moved or was reset: send it where it goes (a band, the strip). */
    void (*publish)(int id, void *ctx);
    /* The small disc to blink while the dial is open. */
    lv_obj_t *(*cell_disc)(int id, void *ctx);
    /* Whether the panel is up and settled, re-checked when the delayed open lands. */
    bool (*open_allowed)(void *ctx);
    void *ctx;
} knob_editor_bank_t;

/* Build the dial, hidden, on `parent` (the screen). Call once, after the bars exist. */
void knob_editor_create(lv_obj_t *parent);

/* Install the bank the panels' cells belong to, or NULL to leave the editor with none. A new
 * bank ends any edit of the old one first; the values are the panel's and outlive the swap. */
void knob_editor_set_bank(const knob_editor_bank_t *bank);

/* True while a knob is in edit mode. The overlay owns the touch then: the view drops every
 * screen intent. */
bool knob_editor_is_editing(void);

/* The panel is going away (its bar slid out, or its cells were swapped): end the edit or
 * disarm the pending open, so neither the overlay nor the dial can outlive the cells. */
void knob_editor_close(void);

/* A small knob's CLICKED and DOUBLE_CLICKED: the tap that arms the delayed open, and the
 * reset gesture. The caller applies the tap guard and the shown/settled gate first. */
void knob_editor_tap(int id);
void knob_editor_double_tap(int id);

/* Paint a crown from the model: the dim dots, the lit run, and the sweep's end marks in the
 * tick ink. The panels' small cells and the editor's own dial share this one painter, so a
 * crown reads the same at 1x and 3x. */
void knob_editor_crown_ink(lv_obj_t *const dots[], int dot_count, const knob_t *k);

#ifdef __cplusplus
}
#endif
