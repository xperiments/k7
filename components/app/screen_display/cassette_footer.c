/*
 * The band across the bottom of the controls screen; see cassette_footer.h.
 *
 * Two things live here. The band itself is the chrome's bottom frame: built once with its styles
 * at its hidden position, it arrives the way the drawer and the DJ block do - one 260 ms ease-out
 * slide, hidden again when it finishes leaving - so the whole frame of the controls screen lands
 * together. A full-width band has no direction to come in from sideways, so its slide is vertical.
 *
 * The **Peek's timeline** is drawn here whenever the chrome is up: the same bar, at the same rect,
 * with the same insets, because it is the same instrument and a second one would drift (ADR 0032).
 * Its numbers are cassette_layout's, and it slides the way the Peek's bar slides.
 *
 * The bar has three faces and turns them over in a loop: where the broadcast is (the times and the
 * 6 px track), who it is (the artist), and what it is (the song). A face with nothing to say is
 * skipped, and a bar with nothing playing stays on the times and prints --:--.
 */
#include "cassette_footer.h"

#include <stdio.h>

#include "lvgl.h"

#include "cassette.h"        /* cassette_path_group: the artist is the Track's first directory */
#include "cassette_bars.h"   /* the chrome's slide time */
#include "chrome_entrance.h"
#include "menu.h"            /* menu_clean_name: the song is the title, cleaned */
#include "cassette_layout.h"
#include "cassette_style.h"
#include "cassette_widgets.h"
#include "timeline.h"        /* the Timeline bar's arithmetic: shared with the Peek */

LV_FONT_DECLARE(jura_28);

/* The band's rect comes from the layout: 768 px wide at (16,358), 116 tall, 6 px short of
 * the panel's bottom, so its top edge is on the last button's and the last DJ cell's bottom
 * line and its sides are 16 px from the screen, the cassette's own frame inset. */

/* Where the band's slide starts and ends: out of sight below the panel, its top edge on the
 * screen's bottom edge. */
#define FOOTER_HIDDEN_Y 480

/* How long each face is up before the next: the times, the artist, the song. */
#define CYCLE_MS 4000

/* The three faces, in the order they turn. */
enum {
    CYCLE_TIMES = 0,
    CYCLE_ARTIST,
    CYCLE_SONG,
    CYCLE_COUNT
};

#define TEXT_MAX 128

static lv_obj_t *s_footer;
static lv_obj_t *s_bar;        /* the Peek's timeline, at the Peek's rect */
static lv_obj_t *s_elapsed;
static lv_obj_t *s_total;
static lv_obj_t *s_track;
static lv_obj_t *s_fill;
static lv_obj_t *s_text;       /* the artist or the song, centred on the bar */
static lv_timer_t *s_cycle_timer;
static uint32_t s_fill_w;
static int s_cycle;            /* which face is up */
static bool s_playing;
static char s_artist[TEXT_MAX];
static char s_song[TEXT_MAX];

/* Two pieces arrive here: the band and the timeline bar it carries. Both slide up from below
 * the panel, both hide when they have left, and both start away. */
static chrome_entrance_t s_band;
static chrome_entrance_t s_timeline;

static void band_write(int32_t y, void *ctx)
{
    (void)ctx;
    lv_obj_set_y(s_footer, y);
    lv_obj_set_hidden(s_footer, false);
}

static void band_hide(void *ctx)
{
    (void)ctx;
    if (s_footer) lv_obj_set_hidden(s_footer, true);
}

/* ------------------------------------------------------------------ the timeline */

static void timeline_write(int32_t y, void *ctx)
{
    (void)ctx;
    if (!s_bar) return;
    lv_obj_set_y(s_bar, y);
    lv_obj_set_hidden(s_bar, false);
}

static void timeline_hide(void *ctx)
{
    (void)ctx;
    if (s_bar) lv_obj_set_hidden(s_bar, true);
}

/* A face with nothing to say is skipped: the rotation never sits on an empty word. */
static bool face_has_text(int face)
{
    if (face == CYCLE_ARTIST) return s_artist[0] != '\0';
    if (face == CYCLE_SONG) return s_song[0] != '\0';
    return true;
}

/* Put up whatever the faces and the broadcast allow: the times and the track, or one word. */
static void faces_apply(void)
{
    bool times = (s_cycle == CYCLE_TIMES);
    bool fill = times && s_playing && s_fill_w > 0;

    if (s_elapsed) lv_obj_set_hidden(s_elapsed, !times);
    if (s_total) lv_obj_set_hidden(s_total, !times);
    if (s_track) lv_obj_set_hidden(s_track, !times);
    if (s_fill) lv_obj_set_hidden(s_fill, !fill);
    if (s_text) {
        lv_obj_set_hidden(s_text, times);
        if (!times) lv_label_set_text(s_text, s_cycle == CYCLE_ARTIST ? s_artist : s_song);
    }
}

static void cycle_cb(lv_timer_t *t)
{
    (void)t;
    for (int i = 0; i < CYCLE_COUNT; i++) {
        s_cycle = (s_cycle + 1) % CYCLE_COUNT;
        if (face_has_text(s_cycle)) break;
    }
    faces_apply();
}

void cassette_footer_create(lv_obj_t *screen)
{
    if (!screen || s_footer) return;

    cassette_rect_t band = cassette_part_rect(CASSETTE_PART_FOOTER);

    s_footer = cassette_border_box(screen, band.x, FOOTER_HIDDEN_Y, band.w, band.h,
                                   CASSETTE_CORNER_R, k_col_bg, LV_OPA_COVER,
                                   k_col_button_rect, CASSETTE_BORDER_W);

    /* The Peek's bar, whole: its rect, its corner, its paper fill and its primary border. Built
     * after the band so it lands on the band's bottom edge, exactly where the Peek puts it. */
    s_bar = cassette_border_box(screen, CASSETTE_TIMELINE_X, CASSETTE_TIMELINE_HIDDEN_Y,
                                CASSETTE_TIMELINE_W, CASSETTE_TIMELINE_H,
                                CASSETTE_TIMELINE_R, k_col_paper, LV_OPA_COVER,
                                k_col_primary, CASSETTE_BORDER_W);

    s_elapsed = cassette_text(s_bar, &jura_28, k_col_primary, LV_ALIGN_LEFT_MID,
                              CASSETTE_TIMELINE_PAD, 0);
    lv_obj_set_width(s_elapsed, CASSETTE_TIMELINE_TIME_W);
    lv_label_set_long_mode(s_elapsed, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_elapsed, LV_TEXT_ALIGN_LEFT, 0);

    s_total = cassette_text(s_bar, &jura_28, k_col_primary, LV_ALIGN_RIGHT_MID,
                            -CASSETTE_TIMELINE_PAD, 0);
    lv_obj_set_width(s_total, CASSETTE_TIMELINE_TIME_W);
    lv_obj_align(s_total, LV_ALIGN_RIGHT_MID, -CASSETTE_TIMELINE_PAD, 0);
    lv_label_set_long_mode(s_total, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_total, LV_TEXT_ALIGN_RIGHT, 0);

    s_track = cassette_box(s_bar, CASSETTE_TIMELINE_TRACK_X,
                           (CASSETTE_TIMELINE_H - CASSETTE_TIMELINE_TRACK_H) / 2,
                           CASSETTE_TIMELINE_TRACK_W, CASSETTE_TIMELINE_TRACK_H,
                           CASSETTE_TIMELINE_TRACK_H / 2, k_col_meter_dim);
    s_fill = cassette_box(s_track, 0, 0, 0, CASSETTE_TIMELINE_TRACK_H,
                          CASSETTE_TIMELINE_TRACK_H / 2, k_col_meter_lit);
    lv_obj_set_hidden(s_fill, true);

    /* The bar's other two faces: the artist and the song, one label the rotation re-writes,
     * inset by the same 20 px the times keep. */
    s_text = cassette_text(s_bar, &jura_28, k_col_primary, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_width(s_text, CASSETTE_TIMELINE_W - 2 * CASSETTE_TIMELINE_PAD - 4);
    lv_label_set_long_mode(s_text, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_text, LV_TEXT_ALIGN_CENTER, 0);

    s_cycle = CYCLE_TIMES;
    s_cycle_timer = lv_timer_create(cycle_cb, CYCLE_MS, NULL);

    faces_apply();
    lv_obj_set_hidden(s_bar, true);
    lv_obj_set_hidden(s_footer, true);

    chrome_entrance_init(&s_band, FOOTER_HIDDEN_Y, band.y, band_write, band_hide, NULL);
    chrome_entrance_init(&s_timeline, CASSETTE_TIMELINE_HIDDEN_Y, CASSETTE_TIMELINE_Y,
                         timeline_write, timeline_hide, NULL);
}

void cassette_footer_set_shown(bool shown)
{
    if (!s_footer) return;
    chrome_entrance_set(&s_band, shown);
    /* The timeline is the Menu's, so it comes and goes with the band - on the same clock. */
    chrome_entrance_set(&s_timeline, shown);
}

void cassette_footer_set_page(bool white)
{
    if (!s_footer) return;
    lv_obj_set_style_bg_color(s_footer, white ? k_col_white : k_col_bg, 0);

    /* The timeline's frame is page ink, its contents are not. On paper the Peek's primary ring
     * reads as a second, louder border than the chrome's own dark lines, so it drops to the
     * page's grey; the playing ink inside - the times, the title, the track - stays primary,
     * which is the one colour the white page keeps (ADR 0037). */
    if (s_bar)
        lv_obj_set_style_border_color(s_bar, white ? k_col_glass : k_col_primary, 0);
}

void cassette_footer_set_playing(bool playing, uint32_t elapsed_ms, uint32_t duration_ms,
                                 const char *title, const char *path)
{
    if (!s_bar) return;

    s_playing = playing;

    /* Who: the Track's first directory, and its title with the file's furniture taken off - the
     * same pair the Menu's rows are built from. */
    cassette_path_group(path, s_artist, sizeof s_artist);
    menu_clean_name(title, s_song, sizeof s_song);

    char text[16];
    timeline_clock(text, sizeof text, elapsed_ms, s_playing);
    lv_label_set_text(s_elapsed, text);
    /* The total is unknown for a Track whose length the decoder did not report. */
    timeline_clock(text, sizeof text, duration_ms, s_playing && duration_ms != 0);
    lv_label_set_text(s_total, text);

    /* The band follows the broadcast's own clock, not the cassette model's frame: these two
     * numbers are what it has, and timeline_permille() is what rounds them. */
    uint32_t permille = s_playing ? timeline_permille(elapsed_ms, duration_ms) : 0;
    uint32_t w = (uint32_t)timeline_fill_w(CASSETTE_TIMELINE_TRACK_W, permille);
    if (w != s_fill_w) {
        s_fill_w = w;
        lv_obj_set_width(s_fill, (int32_t)w);
    }

    /* A face that has just lost its words - the Track changed under it - falls back. */
    if (!face_has_text(s_cycle)) s_cycle = CYCLE_TIMES;
    faces_apply();
}
