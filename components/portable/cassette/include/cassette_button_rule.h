#pragma once
/*
 * cassette_button_rule - the three parts of a Cassette button that are arithmetic, not LVGL.
 *
 * The five buttons - the transport drawer's P, < and >, and the DJ block's M and R - are one
 * widget (components/app/screen_display/cassette_button.c). Three parts of it are values:
 * when a click is a tap, how wide the ring is while pressed, and the shape of the click
 * pulse. They are the parts the two copies drifted on, so they live here, where the host
 * tier compiles them and t21 pins them. Nothing here draws, allocates, logs or reads a
 * global: a value in, a value out, and the caller holds the state.
 *
 * The numbers are the owner's, taken from the two implementations the button module
 * replaces: the drawer's button_clicked_cb, button_state_page and button_pulse_cb
 * (cassette_buttons.c) and the block's cell_clicked_cb, text_button_page and cell_pulse_cb
 * (cassette_eq.c).
 */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The tap guard: a click is the button's only if the touch behind it was not a swipe.
 *
 *   moved      the touch moved past LVGL's press limit before the release
 *              (lv_indev_get_press_moved()).
 *   gesturing  the touch has a gesture direction (lv_indev_get_gesture_dir() is not
 *              LV_DIR_NONE): the release of a swipe that started on the button.
 *
 * The two are one conjunction and neither outranks the other: a clean press is a tap, and
 * either of them alone is not, in whatever order they are read. A touch that moved is a
 * swipe LVGL did not classify as one.
 *
 * The guard runs before the action and before the pulse, so a rejected click does nothing at
 * all. Both hosts obey this one rule. It used to carry a third term for the two-finger VU
 * gesture, which is gone (ADR 0025).
 */
bool cassette_button_is_tap(bool moved, bool gesturing);

/* The ring's two widths, the released one and the pressed one. A button carries the table
 * for its disc (on the dark page) and for its carcase (on either page). The disc's table is
 * {2, 3} for all five and the drawer's carcase is {2, 3} too; M and R share the DJ block's
 * carcase, which does not thicken, so their carcase is {2, 2} and a press reads there as the
 * disc's ring alone. On the white page the disc is the button's cap and its rim is
 * CASSETTE_CAP_RIM_W (cassette_widgets.h) whatever the state, so the table reaches the disc
 * only on the dark page. */
typedef struct {
    uint8_t released;
    uint8_t pressed;
} cassette_button_ring_t;

/* The drawer's table, and the pair every button starts from. */
enum {
    CASSETTE_BUTTON_RING_W         = 2,
    CASSETTE_BUTTON_RING_W_PRESSED = 3,
};

/* The width the ring draws in the current press state. This follows the finger, so a press
 * that moved is still pressed here; moving is the guard's business, not the ring's. */
int cassette_button_ring_w(cassette_button_ring_t ring, bool pressed);

/* The click pulse's scale at one step of a 0..1024 LVGL animation. */
enum {
    CASSETTE_BUTTON_PULSE_STEPS = 1024,  /* one linear run: 0 is the start, 1024 the end */
    CASSETTE_BUTTON_PULSE_SCALE = 256,   /* LV_SCALE_NONE: the button at rest */
    CASSETTE_BUTTON_PULSE_PEAK  = 232,   /* the deepest point, at the half */
};

/* A triangle, 256 -> 232 -> 256: it starts and ends at the rest scale, so a finished pulse
 * needs no restoring call, and it is symmetric - step and 1024 - step give the same scale -
 * so a reversal looks like a pulse. The curve never leaves 232..256.
 *
 * The duration is the caller's animation (160 ms) and is not a parameter: this is the shape,
 * the caller is the clock. The function is total and asserts nothing, so a step outside
 * 0..1024 is clamped to the nearer end. */
int cassette_button_pulse_scale(int32_t step);

#ifdef __cplusplus
}
#endif
