/*
 * The addressable strip's colour, brightness and effects, without LVGL.
 *
 * The channel scaling, the order of the two scalings (effect, then brightness), the five
 * effect names in their list order, and every effect's frame as a pure function of the state,
 * the clock and the audio level. The strip is not there yet; the model is what the RGB group's
 * knobs publish into and what a driver will read, so it is pinned here rather than on a board.
 */
#include "test_util.h"

#include <math.h>
#include <string.h>

#include "rgb.h"

#define N 8

static rgb_state_t fresh(void)
{
    rgb_state_t s;
    rgb_init(&s);
    return s;
}

static void fill(const rgb_state_t *s, uint32_t now, float level, rgb_pixel_t *px)
{
    memset(px, 0xAA, sizeof(rgb_pixel_t) * N);
    CHECK_EQ(rgb_frame(s, now, level, px, N), N, "a frame writes every pixel");
}

static void test_init_and_clamps(void)
{
    rgb_state_t s = fresh();

    CHECK_EQ(s.red, 100, "a new state is full white");
    CHECK_EQ(s.green, 100, "on every channel");
    CHECK_EQ(s.blue, 100, "and full brightness");
    CHECK_EQ(s.brightness, 100, "with brightness full");
    CHECK_EQ(s.effect, RGB_EFFECT_OFF, "and the strip off");

    CHECK(!rgb_set_channel(&s, 0, 100), "setting the same value does not move");
    CHECK(rgb_set_channel(&s, 0, 40), "a lower channel moves");
    CHECK_EQ(s.red, 40, "to the value asked for");
    CHECK(rgb_set_channel(&s, 0, 250), "an over-range channel moves");
    CHECK_EQ(s.red, 100, "and clamps to 100");
    CHECK(rgb_set_channel(&s, 1, -5), "an under-range channel moves");
    CHECK_EQ(s.green, 0, "and clamps to 0");
    CHECK(!rgb_set_channel(&s, 3, 50), "there is no fourth channel");
    CHECK(rgb_set_brightness(&s, 30), "brightness moves");
    CHECK_EQ(s.brightness, 30, "to the value asked for");
    CHECK(rgb_set_brightness(&s, 1000), "an over-range brightness moves");
    CHECK_EQ(s.brightness, 100, "and clamps to 100");

    CHECK(rgb_set_effect(&s, RGB_EFFECT_RAINBOW), "the effect moves");
    CHECK_EQ(s.effect, RGB_EFFECT_RAINBOW, "to the one asked for");
    CHECK(!rgb_set_effect(&s, RGB_EFFECT_RAINBOW), "and the same one does not");
    CHECK(rgb_set_effect(&s, (rgb_effect_t)99), "an out-of-range effect moves");
    CHECK_EQ(s.effect, RGB_EFFECT_LEVEL, "and clamps to the last one");
}

static void test_effect_list(void)
{
    CHECK_EQ(rgb_effect_count(), 6, "six effects");
    CHECK(strcmp(rgb_effect_name(RGB_EFFECT_OFF), "OFF") == 0, "OFF's name");
    CHECK(strcmp(rgb_effect_name(RGB_EFFECT_SOLID), "SOLID") == 0, "SOLID's name");
    CHECK(strcmp(rgb_effect_name(RGB_EFFECT_BREATHE), "BREATHE") == 0, "BREATHE's name");
    CHECK(strcmp(rgb_effect_name(RGB_EFFECT_RAINBOW), "RAINBOW") == 0, "RAINBOW's name");
    CHECK(strcmp(rgb_effect_name(RGB_EFFECT_CHASE), "CHASE") == 0, "CHASE's name");
    CHECK(strcmp(rgb_effect_name(RGB_EFFECT_LEVEL), "LEVEL") == 0, "LEVEL's name");
    CHECK(rgb_effect_name((rgb_effect_t)99) == NULL, "an unknown effect has no name");

    CHECK_EQ(rgb_effect_at(0), RGB_EFFECT_OFF, "index 0 is OFF");
    CHECK_EQ(rgb_effect_at(5), RGB_EFFECT_LEVEL, "index 5 is LEVEL");
    CHECK_EQ(rgb_effect_at(99), RGB_EFFECT_LEVEL, "and past the end clamps");
    CHECK_EQ(rgb_effect_at(-3), RGB_EFFECT_OFF, "below zero clamps too");
}

static void test_solid(void)
{
    rgb_pixel_t px[N];
    rgb_state_t s = fresh();

    s.effect = RGB_EFFECT_SOLID;
    rgb_set_channel(&s, 0, 100);
    rgb_set_channel(&s, 1, 0);
    rgb_set_channel(&s, 2, 0);
    fill(&s, 0, 0.0f, px);
    CHECK_EQ(px[0].r, 255, "full red is the byte's top");
    CHECK_EQ(px[0].g, 0, "with no green");
    CHECK_EQ(px[N - 1].r, 255, "on every pixel");

    /* Brightness is last: half of 255 is 128, and the hue does not change. */
    s.effect = RGB_EFFECT_SOLID;
    s.red = 100;
    s.green = 50;
    s.blue = 0;
    rgb_set_brightness(&s, 50);
    fill(&s, 0, 0.0f, px);
    CHECK_EQ(px[0].r, 128, "half brightness halves the byte");
    CHECK(px[0].g == 64, "and the mid channel keeps its proportion, got %u", px[0].g);

    /* Off is off, whatever the colour says. */
    s.effect = RGB_EFFECT_OFF;
    s.brightness = 100;
    fill(&s, 0, 0.0f, px);
    CHECK(px[0].r == 0 && px[0].g == 0 && px[0].b == 0, "OFF is black");
}

static void test_breathe(void)
{
    rgb_pixel_t px[N];
    rgb_state_t s = fresh();
    s.effect = RGB_EFFECT_BREATHE;
    s.red = 100;
    s.green = 0;
    s.blue = 0;

    fill(&s, 0, 0.0f, px);
    uint8_t dark = px[0].r;
    fill(&s, 1200, 0.0f, px);
    uint8_t peak = px[0].r;
    fill(&s, 2400, 0.0f, px);
    uint8_t again = px[0].r;

    CHECK(peak > dark, "the breathe peaks above its trough: %u vs %u", peak, dark);
    CHECK_EQ(again, dark, "and comes back to the trough a period later");
    CHECK(dark > 0, "the trough is not black");
}

static void test_rainbow(void)
{
    rgb_pixel_t px[N];
    rgb_state_t s = fresh();
    s.effect = RGB_EFFECT_RAINBOW;

    fill(&s, 0, 0.0f, px);
    int different = 0;
    for (int i = 1; i < N; i++)
        if (px[i].r != px[0].r || px[i].g != px[0].g || px[i].b != px[0].b) different++;
    CHECK(different >= N - 2, "a rainbow is a hue per pixel, %d of %d differ", different, N - 1);

    /* The wheel turns. */
    rgb_pixel_t turned[N];
    fill(&s, 1000, 0.0f, turned);
    CHECK(px[0].r != turned[0].r || px[0].g != turned[0].g || px[0].b != turned[0].b,
          "and turns with time");

    /* Full turns twice per period: the same frame comes back. */
    rgb_pixel_t again[N];
    fill(&s, 4000, 0.0f, again);
    CHECK(again[0].r == px[0].r && again[0].g == px[0].g && again[0].b == px[0].b,
          "and repeats after RAINBOW_MS");
}

static void test_chase(void)
{
    rgb_pixel_t px[N];
    rgb_state_t s = fresh();
    s.effect = RGB_EFFECT_CHASE;
    s.red = 100;
    s.green = 0;
    s.blue = 0;

    fill(&s, 0, 0.0f, px);
    int lit = 0, head = -1;
    for (int i = 0; i < N; i++) {
        if (px[i].r > 200) {
            lit++;
            if (head < 0) head = i;
        }
    }
    CHECK_EQ(lit, 3, "the head is three pixels");
    CHECK_EQ(head, 0, "at the start of the strip");

    /* 60 ms a pixel: after seven steps the head is at pixel 7, wrapping round the strip. */
    fill(&s, 7 * 60, 0.0f, px);
    CHECK(px[7].r > 200 && px[0].r > 200, "and runs one pixel per CHASE_MS");
    CHECK(px[6].r < 20, "leaving the pixel behind it dark");

    /* The tail is dim, not black, so the strip reads as one moving light. */
    fill(&s, 0, 0.0f, px);
    CHECK(px[7].r < 20 && px[7].r > 0, "the far tail is dim, got %u", px[7].r);
}

static void test_level(void)
{
    rgb_pixel_t full[N], half[N], quiet[N];
    rgb_state_t s = fresh();
    s.effect = RGB_EFFECT_LEVEL;
    s.red = 100;
    s.green = 0;
    s.blue = 0;

    fill(&s, 0, 1.0f, full);
    fill(&s, 0, 0.5f, half);
    fill(&s, 0, 0.0f, quiet);

    CHECK_EQ(full[0].r, 255, "a full level is the full colour");
    CHECK_EQ(half[0].r, 128, "half the level is half the byte");
    CHECK_EQ(quiet[0].r, 0, "and silence is black");
    CHECK(full[3].r == full[0].r, "every pixel follows the level");
}

static void test_frame_edges(void)
{
    rgb_pixel_t px[N];
    rgb_state_t s = fresh();
    s.effect = RGB_EFFECT_SOLID;

    CHECK_EQ(rgb_frame(&s, 0, 0.0f, NULL, N), 0, "no buffer writes nothing");
    CHECK_EQ(rgb_frame(&s, 0, 0.0f, px, 0), 0, "no pixels writes nothing");
    CHECK_EQ(rgb_frame(&s, 0, 0.0f, px, -4), 0, "a negative count writes nothing");
    CHECK_EQ(rgb_frame(NULL, 0, 0.0f, px, N), 0, "no state writes nothing");
    CHECK_EQ(rgb_frame(&s, 0, 0.0f, px, N), N, "and a real frame writes every pixel");
}

int main(void)
{
    test_init_and_clamps();
    test_effect_list();
    test_solid();
    test_breathe();
    test_rainbow();
    test_chase();
    test_level();
    test_frame_edges();

    TEST_END();
}
