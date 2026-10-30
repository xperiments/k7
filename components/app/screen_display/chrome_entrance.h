#pragma once
/*
 * chrome_entrance - one piece of the controls screen's chrome, arriving or leaving.
 *
 * The seven pieces that move - the two **Bars**, the header and footer bands, the timeline
 * bar, the window and the Peek's own bar - share the rule (entrance.h) and one motion: a
 * 260 ms ease-out from wherever the piece is, with a hide once it has left. What differs is
 * what moves, so each piece hands this module two small callbacks:
 *
 *   write(value)  place the piece at a value: a band's y, the window's opacity, a bar's roots
 *                 at their rest x minus the offset. Called once before the motion starts and
 *                 on every step, so it is where a piece that is coming back makes itself
 *                 visible - writing twice with the same value must be harmless.
 *   hide()        take it out of the draw tree, once the motion out has finished.
 *
 * Nothing here knows what a band is. The piece owns its objects, its rest position and its
 * away value; this owns the animation and the bookkeeping that used to be copied into each of
 * the seven, along with the five ways of getting a reversal wrong.
 */
#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#include "entrance.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    entrance_t rule;                              /* where it is and where it was asked to go */
    void (*write)(int32_t value, void *ctx);      /* place the piece; may unhide it */
    void (*hide)(void *ctx);                      /* it has left: hide its objects */
    void *ctx;
} chrome_entrance_t;

/* `away` and `here` are in the piece's own units, as entrance_init() takes them. `write` is
 * required; `hide` may be NULL for a piece with nothing to take out of the tree. */
void chrome_entrance_init(chrome_entrance_t *c, int32_t away, int32_t here,
                          void (*write)(int32_t value, void *ctx),
                          void (*hide)(void *ctx), void *ctx);

/* Slide it in (true) or out (false) over the chrome's 260 ms. A call naming the state it is
 * already heading for does nothing, even mid-slide; a reversal starts from where the piece is
 * now, so a second swipe halfway through the first turns the motion around instead of
 * restarting it. */
void chrome_entrance_set(chrome_entrance_t *c, bool shown);

#ifdef __cplusplus
}
#endif
