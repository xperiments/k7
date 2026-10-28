#pragma once
/*
 * rgb - the addressable strip's colour, brightness and effect, and the per-pixel frame an effect
 * draws at a moment in time. No LVGL, no ESP-IDF, no allocation, no clock of its own.
 *
 * The RGB group's five knobs publish into this state: three channels 0..100, a brightness
 * 0..100, and one effect. The state is plain data the panel writes and the strip's owner reads.
 * Brightness is the **last** stage - applied to the colour an effect produced, never pre-scaling
 * the channels - so turning the strip down keeps the hues and dims the picture, the same order
 * tone_eq's trim uses.
 *
 * rgb_frame() is the whole of the animation: it fills `n` pixels for `now_ms` and an audio
 * `level` (0..1, the broadcast's RMS), and it is pure - the same inputs give the same bytes. The
 * caller owns the clock and the buffer; nothing here sleeps, allocates or knows how many pixels
 * the strip really has, so the host tier pins every effect with t25_rgb.
 *
 * The effect list is a value, one name each, in the owner's order: off, a solid colour, a
 * breathing colour, a rainbow, a chase, and a level meter driven by the audio.
 */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One pixel, as the strip wants it: three 8-bit channels. */
typedef struct {
    uint8_t r, g, b;
} rgb_pixel_t;

typedef enum {
    RGB_EFFECT_OFF = 0,   /* black, whatever the colour says */
    RGB_EFFECT_SOLID,     /* the colour, steady */
    RGB_EFFECT_BREATHE,   /* the colour, faded up and down */
    RGB_EFFECT_RAINBOW,   /* a hue per pixel, turning */
    RGB_EFFECT_CHASE,     /* a short lit head running along the strip */
    RGB_EFFECT_LEVEL,     /* the colour, scaled by the audio level */
    RGB_EFFECT_COUNT
} rgb_effect_t;

typedef struct {
    int red;          /* 0..100 */
    int green;        /* 0..100 */
    int blue;         /* 0..100 */
    int brightness;   /* 0..100, applied last */
    rgb_effect_t effect;
} rgb_state_t;

/* Full white at full brightness, OFF: the state a fresh panel starts in. */
void rgb_init(rgb_state_t *s);

/* How many effects there are, and the effect at an index, clamped. The effect selector's steps
 * are this list, in this order. */
int rgb_effect_count(void);
rgb_effect_t rgb_effect_at(int index);

/* The effect's name (OFF, SOLID, BREATHE, RAINBOW, CHASE, LEVEL), or NULL for an unknown
 * value. Static strings; the caller does not free them. */
const char *rgb_effect_name(rgb_effect_t effect);

/* Set one channel, the brightness or the effect, clamping each to its range. True iff the state
 * moved, so a caller publishes only on a real change. */
bool rgb_set_channel(rgb_state_t *s, int index, int value);   /* 0 = red, 1 = green, 2 = blue */
bool rgb_set_brightness(rgb_state_t *s, int value);
bool rgb_set_effect(rgb_state_t *s, rgb_effect_t effect);

/* Draw the state's effect into `out` for `now_ms` and the audio `level`, and return how many
 * pixels were written. `level` is 0..1 and is used by RGB_EFFECT_LEVEL alone; a NULL buffer or a
 * non-positive count writes nothing and returns 0. */
int rgb_frame(const rgb_state_t *s, uint32_t now_ms, float level, rgb_pixel_t *out, int n);

#ifdef __cplusplus
}
#endif
