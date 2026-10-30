#pragma once
/*
 * What this build's screen has, as a value.
 *
 * The screen display is one component with two boards behind it, and the difference used to be
 * a preprocessor symbol threaded through the composition: `#if CONFIG_APP_BOARD_SUNTON_...`
 * decided whether the white page's Cover, the Peek and the Menu existed, whether the Card was
 * mounted, and whether the cassette body was drawn on the RGB panel's two border frames. The
 * modules that own those things were compiled on one board only, so the composition could not
 * ask - it had to be rewritten per board.
 *
 * This is the asking. The modules compile on both boards; the composition reads a capability
 * and the parts that do not exist simply do not happen. The board symbol survives in exactly
 * two kinds of place: this file, which answers the question, and the adapters that are
 * physically different (the RGB panel, the GT911, the backlight).
 *
 * Two capabilities, because two things genuinely differ:
 *
 *   cover          the card-side artwork loader, the white page's print on the Card, and the
 *                  116 KB mask the Card builds once. A board with no touch has no white page
 *                  to print on, and no card reader behind it.
 *   panel_borders  the cassette body is drawn on two filled border rectangles, keyed
 *                  `k_col_frame` (FF00FF), which the panel's RGB path uses to mask its own
 *                  overscan - and which, on a diagnostic framebuffer, are magenta lines round
 *                  a cassette.
 *
 * A third board adds a value, not a conditional: fill the struct for it and the composition
 * already asks the right questions.
 */
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool cover;
    bool panel_borders;
} screen_caps_t;

/* The constants for this build. Never NULL, never changes at runtime. */
const screen_caps_t *screen_caps(void);

#ifdef __cplusplus
}
#endif
