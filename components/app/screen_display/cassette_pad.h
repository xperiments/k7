#pragma once
/*
 * cassette_pad - the right bar's Pad: one white plate, and the press machine that reads it.
 *
 * The Pad is one carcase at the layout's Pad rect (596,76, 186x284) over the group block's
 * place, and it is the bar's panel whenever something owns the bar's touch for a mode: the
 * Menu, which ratchets its bands with it, and a knob's editor, which drives the dial with it
 * (ADR 0028). The plate is the same in both, which is the point - one idiom for "this panel
 * is being navigated, not listed".
 *
 * This module owns the plate (its ink, its inner and its dotted guide) and the portable press
 * machine (pad.h: a press becomes STEP, TAP or BACK, and one press takes one axis). Who the
 * Pad is *for* is a **target**: three callbacks the current owner installs, with
 *
 *   step   vertical travel, dy positive down, dt the milliseconds it took
 *   tap    a press that never travelled (PAD_TAP_SLOP)
 *   back   a long leftward swipe
 *
 * and nothing else. A NULL target leaves the Pad inert, which is what a panel that is up but
 * owns nothing should be. The palette, the carcase and the bar's hit rule are the bar's; this
 * module creates no carcase and registers no root.
 *
 * Private to screen_display. Called under the LVGL lock.
 */
#include "lvgl.h"

#include "cassette_bars.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What one press means to whoever owns the Pad. Every hook may be NULL; a NULL target (not a
 * NULL hook) is what makes the Pad inert. `dy` is signed travel in pixels, positive down, and
 * on the first STEP of a press it is the whole travel since the press.
 *
 * `press` and `release` bracket a gesture, so an owner that accumulates travel knows where a
 * press began; they carry no meaning of their own and a target that only ratchets ignores
 * them. */
typedef struct {
    void (*press)(void *ctx);
    void (*step)(int dy, uint32_t dt, void *ctx);
    void (*tap)(void *ctx);
    void (*back)(void *ctx);
    void (*release)(void *ctx);
    void *ctx;
} cassette_pad_target_t;

/* Aim the Pad at `target`, or NULL to leave it inert. A new target drops any press in
 * flight, so the previous owner's half-gesture cannot arrive as the next owner's STEP. */
void cassette_pad_set_target(const cassette_pad_target_t *target);

/* The Pad as a panel value: one carcase, the plate's ink, the press machine inside. */
const cassette_panel_t *cassette_pad_panel(void);

#ifdef __cplusplus
}
#endif
