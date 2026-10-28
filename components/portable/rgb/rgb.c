/*
 * The strip's state and its effects; see rgb.h.
 *
 * The arithmetic is the whole point: a 0..100 channel scaled to a byte, a brightness applied to
 * whatever an effect produced, and five effects that are functions of (state, now_ms, level, i).
 * Nothing here allocates, reads a clock or knows the strip: a frame is pure, so t25_rgb pins it.
 */
#include "rgb.h"

#include <math.h>
#include <stddef.h>

#define CHANNEL_MAX  100
#define BYTE_MAX     255

/* The effects' clocks, in milliseconds. Slow enough to read on a strip, fast enough to see. */
#define BREATHE_MS   2400
#define RAINBOW_MS   4000
#define CHASE_MS     60
#define CHASE_WINDOW 3

#define PI_F 3.14159265358979323846f

static int clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* A 0..100 value as a byte, rounded. */
static int to_byte(int value)
{
    return (clamp_int(value, 0, CHANNEL_MAX) * BYTE_MAX + CHANNEL_MAX / 2) / CHANNEL_MAX;
}

/* Brightness is the last stage: it scales what the effect produced, and rounds. */
static uint8_t dim(int value, int brightness)
{
    int v = (clamp_int(value, 0, BYTE_MAX) * clamp_int(brightness, 0, CHANNEL_MAX) +
             CHANNEL_MAX / 2) / CHANNEL_MAX;
    return (uint8_t)clamp_int(v, 0, BYTE_MAX);
}

void rgb_init(rgb_state_t *s)
{
    if (!s) return;
    s->red = CHANNEL_MAX;
    s->green = CHANNEL_MAX;
    s->blue = CHANNEL_MAX;
    s->brightness = CHANNEL_MAX;
    s->effect = RGB_EFFECT_OFF;
}

int rgb_effect_count(void)
{
    return (int)RGB_EFFECT_COUNT;
}

rgb_effect_t rgb_effect_at(int index)
{
    return (rgb_effect_t)clamp_int(index, 0, (int)RGB_EFFECT_COUNT - 1);
}

const char *rgb_effect_name(rgb_effect_t effect)
{
    switch (effect) {
    case RGB_EFFECT_OFF: return "OFF";
    case RGB_EFFECT_SOLID: return "SOLID";
    case RGB_EFFECT_BREATHE: return "BREATHE";
    case RGB_EFFECT_RAINBOW: return "RAINBOW";
    case RGB_EFFECT_CHASE: return "CHASE";
    case RGB_EFFECT_LEVEL: return "LEVEL";
    default: return NULL;
    }
}

bool rgb_set_channel(rgb_state_t *s, int index, int value)
{
    if (!s) return false;
    int *field = NULL;
    if (index == 0) field = &s->red;
    else if (index == 1) field = &s->green;
    else if (index == 2) field = &s->blue;
    if (!field) return false;

    int v = clamp_int(value, 0, CHANNEL_MAX);
    if (*field == v) return false;
    *field = v;
    return true;
}

bool rgb_set_brightness(rgb_state_t *s, int value)
{
    if (!s) return false;
    int v = clamp_int(value, 0, CHANNEL_MAX);
    if (s->brightness == v) return false;
    s->brightness = v;
    return true;
}

bool rgb_set_effect(rgb_state_t *s, rgb_effect_t effect)
{
    if (!s) return false;
    rgb_effect_t e = rgb_effect_at((int)effect);
    if (s->effect == e) return false;
    s->effect = e;
    return true;
}

/* A hue in degrees as a full-value, full-saturation colour. */
static void hue_rgb(float hue, int *r, int *g, int *b)
{
    float h = fmodf(hue, 360.0f);
    if (h < 0.0f) h += 360.0f;
    float hp = h / 60.0f;
    float x = 1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f);

    float rf = 0.0f, gf = 0.0f, bf = 0.0f;
    switch ((int)hp % 6) {
    case 0: rf = 1.0f; gf = x; break;
    case 1: rf = x; gf = 1.0f; break;
    case 2: gf = 1.0f; bf = x; break;
    case 3: gf = x; bf = 1.0f; break;
    case 4: rf = x; bf = 1.0f; break;
    default: rf = 1.0f; bf = x; break;
    }
    *r = (int)lroundf(rf * (float)BYTE_MAX);
    *g = (int)lroundf(gf * (float)BYTE_MAX);
    *b = (int)lroundf(bf * (float)BYTE_MAX);
}

int rgb_frame(const rgb_state_t *s, uint32_t now_ms, float level, rgb_pixel_t *out, int n)
{
    if (!s || !out || n <= 0) return 0;

    const int cr = to_byte(s->red), cg = to_byte(s->green), cb = to_byte(s->blue);
    const int brightness = s->brightness;

    /* The effects that are one colour for the whole strip. */
    int factor_pm = 1000;   /* per mille, applied to the colour */
    bool colour_effect = true;

    switch (s->effect) {
    case RGB_EFFECT_OFF:
        factor_pm = 0;
        break;
    case RGB_EFFECT_SOLID:
        break;
    case RGB_EFFECT_BREATHE: {
        float phase = 2.0f * PI_F * (float)(now_ms % BREATHE_MS) / (float)BREATHE_MS;
        float f = 0.1f + 0.9f * (0.5f - 0.5f * cosf(phase));
        factor_pm = (int)lroundf(f * 1000.0f);
        break;
    }
    case RGB_EFFECT_LEVEL: {
        float l = level;
        if (l < 0.0f) l = 0.0f;
        if (l > 1.0f) l = 1.0f;
        factor_pm = (int)lroundf(l * 1000.0f);
        break;
    }
    case RGB_EFFECT_RAINBOW:
    case RGB_EFFECT_CHASE:
    default:
        colour_effect = false;
        break;
    }

    if (colour_effect) {
        for (int i = 0; i < n; i++) {
            int r = (cr * factor_pm + 500) / 1000;
            int g = (cg * factor_pm + 500) / 1000;
            int b = (cb * factor_pm + 500) / 1000;
            out[i].r = dim(r, brightness);
            out[i].g = dim(g, brightness);
            out[i].b = dim(b, brightness);
        }
        return n;
    }

    if (s->effect == RGB_EFFECT_RAINBOW) {
        /* A hue per pixel, the whole wheel turning over RAINBOW_MS. */
        float turn = 360.0f * (float)(now_ms % RAINBOW_MS) / (float)RAINBOW_MS;
        for (int i = 0; i < n; i++) {
            float hue = turn + 360.0f * (float)i / (float)n;
            int r, g, b;
            hue_rgb(hue, &r, &g, &b);
            out[i].r = dim(r, brightness);
            out[i].g = dim(g, brightness);
            out[i].b = dim(b, brightness);
        }
        return n;
    }

    /* CHASE: a short head running along the strip over a dim tail of the same colour. */
    int pos = (int)((now_ms / CHASE_MS) % (uint32_t)n);
    for (int i = 0; i < n; i++) {
        int behind = (i - pos + n) % n;
        bool head = behind < CHASE_WINDOW;
        int factor = head ? 1000 : 60;   /* the tail is 6% of the colour */
        out[i].r = dim((cr * factor + 500) / 1000, brightness);
        out[i].g = dim((cg * factor + 500) / 1000, brightness);
        out[i].b = dim((cb * factor + 500) / 1000, brightness);
    }
    return n;
}
