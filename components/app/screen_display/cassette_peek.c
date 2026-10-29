/*
 * cassette_peek.c - the up swipe's overlay: the timeline bar and the two floating tags.
 *
 * The bar is the old title bar's paper strip, 568x52 at (116,416), with the title replaced
 * by the Track's clock: elapsed on the left, a 6 px track and its lit fill in the
 * middle, total on the right. The tags are two 504x44 pills centred on the cassette:
 * x=148..651, which leaves 61 px of cassette on each side and puts the two hubs about
 * 100 px in from the pills' ends. The song is at (148,12) above the left hub, the group
 * at (148,248) below it, the same distance from the hub's centre on either side. Their
 * 28 px text is left-aligned. A label with nothing to say is hidden with its tether
 * rather than shown empty.
 *
 * The tethers follow from those positions. Both start on the left hub's column (x=251),
 * so they stay vertical: the upper label runs down from its bottom to the top of the rim,
 * the lower one up from its top to the bottom of the same rim. The upper end of each
 * tether is under the pill's opaque fill; the lower end is a hub anchor the view hands in
 * on every pose step.
 *
 * The movement is deliberately cheap. One lv_anim drives both pills' y through a sine,
 * and the callback writes a new y only when the rounded pixel actually changes; the
 * tethers are static, their ends under the pills, so a bob does not redraw a line.
 * The pose animation moves the hub anchors instead, and update_lines() follows them:
 * that is a handful of line point writes over the 260 ms slide, not a per-frame redraw
 * of the cassette.
 *
 * Everything here runs under the LVGL lock, like the rest of the screen.
 */
#include "cassette_peek.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

#include "cassette.h"
#include "cassette_layout.h"
#include "cassette_style.h"
#include "cassette_widgets.h"

LV_FONT_DECLARE(jura_28);

static const char *TAG = "screen";

/* ------------------------------------------------------------------ geometry */

/* The bar and its timeline come from cassette_layout: the Menu's footer draws the same
 * instrument at the same rect, and the numbers may not be copied. */
#define PEEK_X        CASSETTE_TIMELINE_X
#define PEEK_W        CASSETTE_TIMELINE_W
#define PEEK_BAR_Y    CASSETTE_TIMELINE_Y
#define PEEK_BAR_H    CASSETTE_TIMELINE_H
#define PEEK_BAR_R    CASSETTE_TIMELINE_R
#define PEEK_BAR_HIDDEN_Y CASSETTE_TIMELINE_HIDDEN_Y
#define PEEK_TIME_W   CASSETTE_TIMELINE_TIME_W
#define PEEK_TIME_PAD CASSETTE_TIMELINE_PAD
#define PEEK_TRACK_X  CASSETTE_TIMELINE_TRACK_X
#define PEEK_TRACK_W  CASSETTE_TIMELINE_TRACK_W
#define PEEK_TRACK_H  CASSETTE_TIMELINE_TRACK_H

/* The two tags, both on the left hub's column: the song above it, the group below it,
 * their centres the same distance from the hub's centre. Sixteen px of inset leaves
 * 472 px of text at Jura 28, so a long title is cut with dots rather than scrolled. */
#define PEEK_TAG_W    504
#define PEEK_TAG_H    44
#define PEEK_TAG_R    22
#define PEEK_TAG_PAD  16
#define PEEK_TAG0_X   148
#define PEEK_TAG0_Y   12
#define PEEK_TAG1_X   148
#define PEEK_TAG1_Y   248

/* The tether's ends: both sit on the left hub's column at the peek pose (x=251), which
 * is 20.4 % of the centred 504 px pill rather than a round quarter; the hub's column
 * wins over the fraction, so the lines stay vertical. The upper label
 * leaves its bottom for the top rim, the lower one its top for the bottom rim. The pill end
 * is PEEK_LINE_INSET px inside the pill, so the bob only uncovers more or less of the
 * line instead of detaching it. */
/* The left hub's column at the peek pose. The pills are centred at x=400 and start at
 * 148; 251 is 103 px in, 20.4 % of the 504 px width. */
#define PEEK_HUB_COL   251
#define PEEK_LINE0_X   PEEK_HUB_COL
#define PEEK_LINE1_X   PEEK_HUB_COL
#define PEEK_LINE_INSET 14
#define PEEK_LINE0_TOP (PEEK_TAG0_Y + PEEK_TAG_H - PEEK_LINE_INSET)
#define PEEK_LINE1_TOP (PEEK_TAG1_Y + PEEK_LINE_INSET)

/* The bob: +/- 6 px over five seconds, one animation for both tags, opposite phases. */
#define PEEK_FLOAT_AMP  6
#define PEEK_FLOAT_MS   5000
#define PEEK_TWO_PI     6.28318530718f

#define PEEK_SLIDE_MS   260

#define PEEK_TEXT_MAX   96

/* ------------------------------------------------------------------ state */

static lv_obj_t *s_bar;
static lv_obj_t *s_elapsed;
static lv_obj_t *s_total;
static lv_obj_t *s_track;
static lv_obj_t *s_fill;
static lv_obj_t *s_idle;
static lv_obj_t *s_tag[2];
static lv_obj_t *s_tag_text[2];
static lv_obj_t *s_line[2];

static bool s_shown;

/* The last frame's values, so a show can paint the current state without waiting for
 * the next 40 ms status poll. */
static bool     s_playing;
static uint32_t s_elapsed_ms;
static uint32_t s_duration_ms;
static uint32_t s_permille;
static uint32_t s_fill_w;

/* The texts: the tags the loader found, the path/filename fallbacks, and what the two
 * labels currently carry (so an unchanged update is no string work at all). */
static char s_tag_group[PEEK_TEXT_MAX];
static char s_tag_song[PEEK_TEXT_MAX];
static char s_fallback_group[PEEK_TEXT_MAX];
static char s_fallback_song[PEEK_TEXT_MAX];
static char s_shown_top[PEEK_TEXT_MAX];    /* the label above the hub */
static char s_shown_bottom[PEEK_TEXT_MAX]; /* the one below it */

/* The pose the cassette is drawn at. Both tethers end on the left hub's rim, and where that
 * rim is comes from cassette_layout's cassette_hub_anchor() - so the peek follows the
 * cassette by being told the pose, not by being handed two points. */
static cassette_pose_t s_pose;

/* lv_line keeps the pointer it is given, so each tether's two points live here rather
 * than in update_lines()'s frame. */
static lv_point_precise_t s_line_pts[2][2];

/* The bob's last integer offset, so a frame that rounds to the same pixel writes
 * nothing. Two ints, not two animations. */
static int s_bob[2];

/* ------------------------------------------------------------------ helpers */

static void update_texts(void);
static void update_lines(void);

static void format_time(char *buf, size_t n, uint32_t ms, bool known)
{
    if (!known) {
        snprintf(buf, n, "--:--");
        return;
    }
    uint32_t s = ms / 1000u;
    snprintf(buf, n, "%u:%02u", (unsigned)(s / 60u), (unsigned)(s % 60u));
}

static void update_times(void)
{
    if (!s_elapsed || !s_total) return;
    char text[16];
    format_time(text, sizeof text, s_elapsed_ms, s_playing);
    lv_label_set_text(s_elapsed, text);
    /* `s_elapsed_ms` is clamped to the duration by the model; the total is unknown for
     * a Track whose length the decoder did not report. */
    format_time(text, sizeof text, s_duration_ms, s_playing && s_duration_ms != 0);
    lv_label_set_text(s_total, text);
}

static void update_fill(void)
{
    if (!s_fill) return;
    uint32_t w = (uint32_t)((uint64_t)PEEK_TRACK_W * s_permille / 1000u);
    if (w == s_fill_w) return;
    s_fill_w = w;
    lv_obj_set_width(s_fill, (int32_t)w);
    lv_obj_set_hidden(s_fill, w == 0);
}

static void update_idle(void)
{
    if (!s_bar) return;
    lv_obj_set_hidden(s_elapsed, !s_playing);
    lv_obj_set_hidden(s_total, !s_playing);
    lv_obj_set_hidden(s_track, !s_playing);
    lv_obj_set_hidden(s_idle, s_playing);
}

/* A label with nothing to say is hidden with its tether, on its own: an empty pill floating
 * over the tape would say nothing, and there is no em dash standing in for it. The bar stays
 * up with its own idle message. */
static void tags_visible(void)
{
    bool top = s_shown && s_shown_top[0];
    bool bottom = s_shown && s_shown_bottom[0];
    lv_obj_set_hidden(s_tag[0], !top);
    lv_obj_set_hidden(s_line[0], !top);
    lv_obj_set_hidden(s_tag[1], !bottom);
    lv_obj_set_hidden(s_line[1], !bottom);
}

/* The two labels from the tags, or the fallbacks while the tags are read (or absent). The
 * song is the label above the hub and the group the one below it. */
static void update_texts(void)
{
    if (!s_tag_text[0] || !s_tag_text[1]) return;

    const char *group = s_tag_group[0] ? s_tag_group : s_fallback_group;
    const char *song = s_tag_song[0] ? s_tag_song : s_fallback_song;
    const char *top = song;
    const char *bottom = group;

    if (strcmp(top, s_shown_top) != 0) {
        snprintf(s_shown_top, sizeof s_shown_top, "%s", top);
        lv_label_set_text(s_tag_text[0], s_shown_top);
    }
    if (strcmp(bottom, s_shown_bottom) != 0) {
        snprintf(s_shown_bottom, sizeof s_shown_bottom, "%s", bottom);
        lv_label_set_text(s_tag_text[1], s_shown_bottom);
    }
    tags_visible();
}

/* One tether. The line object is placed on the segment's own bounding box so its
 * invalidation is the line, not the screen from 0,0; the points are relative to that
 * box and the absolute ends are unchanged. The -1 keeps the box at least one pixel
 * wide, so a vertical tether still has an area to invalidate. */
static void line_set(int i, int ax, int ay, int bx, int by)
{
    int ox = (ax < bx ? ax : bx) - 1;
    int oy = (ay < by ? ay : by) - 1;
    lv_obj_set_pos(s_line[i], ox, oy);
    s_line_pts[i][0].x = ax - ox;
    s_line_pts[i][0].y = ay - oy;
    s_line_pts[i][1].x = bx - ox;
    s_line_pts[i][1].y = by - oy;
    lv_line_set_points(s_line[i], s_line_pts[i], 2);
}

/* The tether's upper end stays inside the pill; its lower end is the left hub's rim at the
 * current pose - the top rim for the song above, the bottom one for the group below. */
static void update_lines(void)
{
    if (!s_line[0] || !s_line[1]) return;

    int x = 0;
    int y = 0;
    cassette_hub_anchor(s_pose, 0, true, &x, &y);
    line_set(0, PEEK_LINE0_X, PEEK_LINE0_TOP, x, y);
    cassette_hub_anchor(s_pose, 0, false, &x, &y);
    line_set(1, PEEK_LINE1_X, PEEK_LINE1_TOP, x, y);
}

/* ------------------------------------------------------------------ the slide */

static void bar_slide_cb(void *var, int32_t value)
{
    lv_obj_set_y((lv_obj_t *)var, value);
}

static void bar_hidden_cb(lv_anim_t *a)
{
    (void)a;
    if (!s_shown) lv_obj_set_hidden(s_bar, true);
}

static void bar_slide_to(int to)
{
    int from = lv_obj_get_y(s_bar);
    lv_anim_delete(s_bar, bar_slide_cb);
    if (from == to) {
        lv_obj_set_y(s_bar, to);
        if (!s_shown) lv_obj_set_hidden(s_bar, true);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_bar);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)bar_slide_cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, PEEK_SLIDE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    if (!s_shown) lv_anim_set_completed_cb(&a, bar_hidden_cb);
    lv_anim_start(&a);
}

/* ------------------------------------------------------------------ the bob */

/* One animation, two tags, opposite phases. Only a changed whole pixel is written, so
 * a five-second sine with 25 Hz ticks is around eight writes per tag per second - each
 * one a 504x44 invalidation, and no tether redraw. */
static void float_cb(void *var, int32_t value)
{
    (void)var;
    float p = (float)value / 1024.0f;
    float phase = p * PEEK_TWO_PI;
    int off[2] = {
        (int)lroundf(sinf(phase) * (float)PEEK_FLOAT_AMP),
        (int)lroundf(sinf(phase + 3.14159265358979323846f) * (float)PEEK_FLOAT_AMP),
    };
    const int rest[2] = { PEEK_TAG0_Y, PEEK_TAG1_Y };
    for (int i = 0; i < 2; i++) {
        if (off[i] == s_bob[i]) continue;
        s_bob[i] = off[i];
        lv_obj_set_y(s_tag[i], rest[i] + off[i]);
    }
}

static void bob_start(void)
{
    if (!s_bar) return;
    lv_anim_delete(s_bar, float_cb);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_bar);
    lv_anim_set_exec_cb(&a, float_cb);
    lv_anim_set_values(&a, 0, 1024);
    lv_anim_set_duration(&a, PEEK_FLOAT_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
}

static void bob_stop(void)
{
    if (!s_bar) return;
    lv_anim_delete(s_bar, float_cb);
    for (int i = 0; i < 2; i++) {
        s_bob[i] = 0;
        if (s_tag[i]) lv_obj_set_y(s_tag[i], i ? PEEK_TAG1_Y : PEEK_TAG0_Y);
    }
}

/* ------------------------------------------------------------------ public */

void cassette_peek_set_path(const char *track_path)
{
    cassette_path_group(track_path, s_fallback_group, sizeof s_fallback_group);
    /* A new Track's tags are not its predecessor's. */
    s_tag_group[0] = 0;
    s_tag_song[0] = 0;
    update_texts();
}

void cassette_peek_set_tags(const char *group, const char *song)
{
    snprintf(s_tag_group, sizeof s_tag_group, "%s", group ? group : "");
    snprintf(s_tag_song, sizeof s_tag_song, "%s", song ? song : "");
    update_texts();
}

void cassette_peek_set_pose(cassette_pose_t pose)
{
    s_pose = pose;
    update_lines();
}

void cassette_peek_apply(const cassette_frame_t *frame)
{
    if (!frame || !s_bar) return;

    bool playing = frame->playing;
    if (playing != s_playing) {
        s_playing = playing;
        update_idle();
    }

    if (frame->title_changed) {
        /* The frame's title is longer than a tag buffer; the precision bounds the copy
         * to what the 260 px label can show anyway. */
        snprintf(s_fallback_song, sizeof s_fallback_song, "%.*s",
                 (int)sizeof s_fallback_song - 1, frame->title);
        update_texts();
    }
    if (frame->time_changed) {
        s_elapsed_ms = frame->elapsed_ms;
        s_duration_ms = frame->duration_ms;
        update_times();
    }
    if (frame->permille_changed) {
        s_permille = frame->permille;
        update_fill();
    }
}

void cassette_peek_show(void)
{
    if (!s_bar || s_shown) return;
    s_shown = true;

    /* Paint the current state now rather than on the next status poll. The tags appear
     * with the first frame; only the bar slides. */
    update_texts();
    update_idle();
    update_times();
    update_fill();

    lv_obj_set_hidden(s_bar, false);
    lv_obj_move_foreground(s_line[0]);
    lv_obj_move_foreground(s_line[1]);
    lv_obj_move_foreground(s_tag[0]);
    lv_obj_move_foreground(s_tag[1]);
    lv_obj_move_foreground(s_bar);
    bar_slide_to(PEEK_BAR_Y);
    bob_start();
    ESP_LOGI(TAG, "peek shown");
}

void cassette_peek_hide(void)
{
    if (!s_bar || !s_shown) return;
    s_shown = false;
    bob_stop();

    for (int i = 0; i < 2; i++) {
        lv_obj_set_hidden(s_tag[i], true);
        lv_obj_set_hidden(s_line[i], true);
    }
    bar_slide_to(PEEK_BAR_HIDDEN_Y);
    ESP_LOGI(TAG, "peek hidden");
}

bool cassette_peek_is_shown(void)
{
    return s_shown;
}

/* ------------------------------------------------------------------ build */

void cassette_peek_create(lv_obj_t *screen)
{
    if (!screen) return;
    /* The view hands in the real pose straight after create; the entry view is what the
     * first frame would be drawn at if it did not. */
    s_pose = cassette_pose_entry();

    /* The bar: the old title strip, now the timeline. The four parts of its middle are
     * children of the bar, so it slides as one object. */
    s_bar = cassette_border_box(screen, PEEK_X, PEEK_BAR_Y, PEEK_W, PEEK_BAR_H,
                                PEEK_BAR_R, k_col_paper, LV_OPA_COVER, k_col_primary, 2);
    lv_obj_set_y(s_bar, PEEK_BAR_HIDDEN_Y);
    lv_obj_set_hidden(s_bar, true);

    s_elapsed = cassette_text(s_bar, &jura_28, k_col_primary, LV_ALIGN_LEFT_MID,
                              PEEK_TIME_PAD, 0);
    lv_obj_set_width(s_elapsed, PEEK_TIME_W);
    lv_label_set_long_mode(s_elapsed, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_elapsed, LV_TEXT_ALIGN_LEFT, 0);

    s_total = cassette_text(s_bar, &jura_28, k_col_primary, LV_ALIGN_RIGHT_MID,
                            -PEEK_TIME_PAD, 0);
    lv_obj_set_width(s_total, PEEK_TIME_W);
    /* The width change moves the object's right edge, so put it back on the bar's
     * right inset after the box is the fixed one the text can be right-aligned in. */
    lv_obj_align(s_total, LV_ALIGN_RIGHT_MID, -PEEK_TIME_PAD, 0);
    lv_label_set_long_mode(s_total, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_total, LV_TEXT_ALIGN_RIGHT, 0);

    s_track = cassette_box(s_bar, PEEK_TRACK_X, (PEEK_BAR_H - PEEK_TRACK_H) / 2,
                           PEEK_TRACK_W, PEEK_TRACK_H, PEEK_TRACK_H / 2,
                           k_col_meter_dim);
    s_fill = cassette_box(s_track, 0, 0, 0, PEEK_TRACK_H, PEEK_TRACK_H / 2,
                          k_col_meter_lit);
    lv_obj_set_hidden(s_fill, true);

    s_idle = cassette_text(s_bar, &jura_28, k_col_primary, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(s_idle, "no card, or nothing to play");

    /* The tethers first, so each tag covers the end that is inside it. They are screen
     * children at 0,0: their points are screen coordinates and nothing moves under them
     * except the hub anchors. */
    for (int i = 0; i < 2; i++) {
        s_line[i] = lv_line_create(screen);
        lv_obj_remove_style_all(s_line[i]);
        lv_obj_set_pos(s_line[i], 0, 0);
        lv_obj_set_style_line_width(s_line[i], 2, 0);
        lv_obj_set_style_line_color(s_line[i], k_col_primary, 0);
        lv_obj_set_style_line_opa(s_line[i], LV_OPA_COVER, 0);
        lv_obj_set_style_line_rounded(s_line[i], true, 0);
        lv_obj_set_clickable(s_line[i], false);
        lv_obj_set_hidden(s_line[i], true);
    }

    const int tag_x[2] = { PEEK_TAG0_X, PEEK_TAG1_X };
    const int tag_y[2] = { PEEK_TAG0_Y, PEEK_TAG1_Y };
    for (int i = 0; i < 2; i++) {
        s_tag[i] = cassette_border_box(screen, tag_x[i], tag_y[i],
                                       PEEK_TAG_W, PEEK_TAG_H, PEEK_TAG_R,
                                       k_col_paper, LV_OPA_COVER, k_col_primary, 2);
        s_tag_text[i] = cassette_text(s_tag[i], &jura_28, k_col_primary,
                                      LV_ALIGN_LEFT_MID, PEEK_TAG_PAD, 0);
        lv_obj_set_width(s_tag_text[i], PEEK_TAG_W - 2 * PEEK_TAG_PAD);
        lv_label_set_long_mode(s_tag_text[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(s_tag_text[i], LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_hidden(s_tag[i], true);
    }
    update_idle();
    update_times();
    update_fill();
    update_lines();
}
