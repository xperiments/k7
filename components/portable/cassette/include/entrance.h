#pragma once
/*
 * entrance - one piece of chrome's arrival, as a value.
 *
 * Seven pieces of the controls screen move the same way: the two **Bars**, the header band,
 * the footer band, the timeline bar, the window, and the Peek's own bar. Each is away at rest -
 * off screen, or transparent - animates to its resting place over the chrome's 260 ms,
 * reverses from wherever it is when the controls change their mind, and hides when it has
 * left. The bar's module documented that rule once and five other files wrote it again, each
 * with its own "already shown" flag and its own idea of what a reversal starts from.
 *
 * The rule is arithmetic: one value, two ends, and the state it was last asked for. It is
 * here, with cassette_button_rule, because it is the part that drifted, the host tier can
 * compile it, and t28 pins every edge. Nothing here draws, animates, allocates or logs: the
 * 260 ms, the ease, the objects and the hiding are the display module's
 * (chrome_entrance.c), and it holds one of these.
 *
 * A call naming the state the piece is already heading for does nothing - even mid-slide -
 * because a request that changes nothing must not restart the motion it is already making.
 */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* `value` is in whatever units the owner moves the piece in: screen y for a band, x for a bar,
 * opacity for the window. Only the two ends and the order of them are this module's. */
typedef struct {
    int32_t value;    /* where the piece is now */
    int32_t away;     /* the value that puts it off screen, or out of sight */
    int32_t here;     /* and the one it rests at when it is up */
    bool    target;   /* the state it was last asked for: false away, true up */
} entrance_t;

/* A piece that starts away, as every one of them does. */
void entrance_init(entrance_t *e, int32_t away, int32_t here);

/* Ask for a state. False means there is nothing to do - the piece is already heading there -
 * and the caller must not start an animation. Otherwise `from` is where the piece is *now*, so
 * a reversal begins at the pixels on screen, and `to` is the end it is heading for. Either
 * pointer may be NULL when the caller only wants the answer. */
bool entrance_ask(entrance_t *e, bool shown, int32_t *from, int32_t *to);

/* One step of the animation: record the value the animator just applied. */
void entrance_step(entrance_t *e, int32_t value);

/* The animation finished. True when the piece ended away and the caller must hide it; a piece
 * that arrived is already visible and needs nothing. Called from the animator's completion
 * callback, which is the only place that knows the motion has stopped. */
bool entrance_landed(const entrance_t *e);

#ifdef __cplusplus
}
#endif
