/*
 * The cassette screen's Levels, their sheets, and the peek they sit above.
 *
 * The screen has three Levels - Bare, Label cover and Artwork - ordered by how much of
 * it the sheet covers, and exactly one sheet is shown at a time. A one-finger down
 * swipe moves one level towards Artwork, an up swipe one towards Bare, and the ends do
 * not wrap. The level changes in one frame, before LVGL draws: a transition that
 * redraws a full-screen sheet per frame was measured to stutter on this panel.
 *
 * The Label cover comes from assets/labels/k7logo.png: an 800x480 image, the panel's
 * own size, exported as RGB565A8 and drawn full-bleed at 0,0. Its black triangle and
 * circle carry alpha, so the reels show through them; once the cover is up the screen
 * background fades from k_col_bg to the cover's own k_col_cover_bg. While the cover is
 * up the reels are behind it, and so is the VU meter: both are drawn with the cassette,
 * not over the cover.
 *
 * The Artwork level is a screen of its own: the cassette Case backdrop
 * (assets/labels/cover.jpg) full-bleed, the Track's picture filling the case's window,
 * and the title on the band below. Neither picture is decoded here; the board adapter
 * (artwork_loader.c) locates, decodes and fits them and hands the finished descriptors
 * in under the LVGL lock, so this module never touches the card or a decoder.
 *
 * The peek is not a Level: the up swipe at Bare has no Level to move to, so it raises
 * the overlay in cassette_peek.c - the timeline bar and the two floating tags - instead.
 * The cassette gives the overlay its room by shrinking and lifting itself, and the
 * cassette is the view's, so this module passes the view's `room` callback through to
 * the peek rather than touching the pose: the view owns the exact scale, offset and
 * duration (it is the one that includes cassette_layout.h). Everything else the Level
 * needs is here.
 *
 * What this module does not build is the cassette itself: the container, the packs, the
 * rollers, the tape paths, the hubs and the meter stay in the view, and so does the
 * model. The seam is the screen the sheets are children of, and the call order in
 * cassette_view_create() keeps them above the cassette in the child order.
 *
 * A board without the touch adapter has no sheet beyond Bare - the vertical gestures
 * that reach them are the panel's - so the sheet machinery below is guarded and its
 * functions build nothing and do nothing there.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

#include "cassette.h"
#include "cassette_levels.h"
#include "cassette_style.h"
#include "cassette_widgets.h"
#if CONFIG_APP_BOARD_SUNTON_8048S050C
#include "artwork_loader.h"
#include "cassette_peek.h"
#include "label_cover.h"
#endif

LV_FONT_DECLARE(jura_36);

static const char *TAG = "screen";

/* The three positions of the cassette screen, ordered by how much of it the sheet
 * covers: Bare, then the K7 sticker, then Artwork. A one-finger down swipe moves one
 * level towards Artwork, an up swipe one towards Bare, and the ends do not wrap.
 * Exactly one sheet is shown at a time. A board without the touch adapter has no
 * sheet beyond Bare, so its LEVEL_MAX is LEVEL_BARE and a swipe that way does
 * nothing: the level is a position, not a command. */
enum {
    LEVEL_BARE = 0,
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    LEVEL_COVER,
    LEVEL_ART,
#endif
};
#if CONFIG_APP_BOARD_SUNTON_8048S050C
#define LEVEL_MAX LEVEL_ART
#else
#define LEVEL_MAX LEVEL_BARE
#endif

/* What arriving at a Level does to the screen's backdrop. */
enum {
    BACKDROP_CASSETTE = 0,   /* the cassette's own colour */
    BACKDROP_COVER,          /* fade to the Label cover's backdrop */
    BACKDROP_ART,            /* the Artwork sheet covers the screen at once */
};

/* One row per Level: the sheet, where it rests and hides, whether reaching it asks for
 * the Track's picture, and what the backdrop does on arrival. A board without the touch
 * adapter has no rows beyond Bare. Adding a Level is a row here, an enum value and its
 * creation; the machine itself does not name it. */
typedef struct {
    lv_obj_t *sheet;
    int rest_y;
    int hidden_y;
    bool wants_art;
    uint8_t backdrop;
} level_sheet_t;

static level_sheet_t s_sheets[LEVEL_MAX + 1];

/* The big cover: the supplied full cover, full-bleed at 0,0, the Label cover level.
 * Cover art is 800x480, the panel's own size, so the cover has no margin. This one
 * keeps its alpha (RGB565A8): the black triangle and circle are transparent, so the
 * reels show through them.
 * The title is a child of the cover; it sits on the empty band at the top of the art. The cover hides just above the screen,
 * so a level change places it at 0,0 in one frame. It is the panel board's, because
 * the vertical gestures that reach its level are. */
#if CONFIG_APP_BOARD_SUNTON_8048S050C
#define COVER_X 0
#define COVER_Y 0
#define COVER_HIDDEN_Y (-LABEL_COVER_H - 8)
#define COVER_BG_FADE_MS 500
#define COVER_TITLE_X 40
#define COVER_TITLE_Y 8
#define COVER_TITLE_W 720

/* The Artwork level: the Case backdrop, assets/labels/cover.jpg, full-bleed at 0,0,
 * with the Track's picture in its window - (101,23) to (704,408), 603x385 - and the
 * title on the band below. The Case is opaque, so the cassette is covered rather than
 * redrawn. The title sits in the free band under the case,
 * clear of the window. */
#define ART_X 101
#define ART_Y 23
#define ART_W 603
#define ART_H 385
#define ART_TITLE_Y 424
#define ART_TITLE_W 720
#define ART_HIDDEN_Y (-480 - 8)
#endif

/* The current Level, and the last status: the status is what the Artwork request is
 * made from, so it is kept here rather than in the view. */
static int s_level;
static mini_icecast_status_t s_status;
static bool s_have_status;

#if CONFIG_APP_BOARD_SUNTON_8048S050C
static lv_obj_t *s_screen;         /* the screen the sheets are on; the backdrop is its */
static lv_obj_t *s_cover_title;    /* the big cover's title, on the empty top band */
static lv_obj_t *s_cover;          /* the full label cover (Label cover level) */
static lv_obj_t *s_art_sheet;      /* the Artwork level: its own full screen */
static lv_obj_t *s_case_img;       /* the Case backdrop; the loader owns its pixels */
static lv_obj_t *s_art_img;        /* the Track picture in the Case's window */
static lv_obj_t *s_art_title;      /* the Artwork level's title */
/* The two labels that carry the Now playing title on the sheets: the cover's band and
 * the Artwork band. The peek's tags are cassette_peek's. */
static lv_obj_t *s_titles[2];
/* The module remembers the last artwork request, so a 25 Hz update is a string compare
 * rather than a loader call. */
static char      s_art_path[LIB_PATH_MAX];
static mi_fmt_t  s_art_fmt;
static bool      s_art_requested;
/* The same, for the text tags: they are requested for every Track, not only while the
 * Artwork Level is up, because the group and song tags are on Bare's peek. */
static char      s_tags_path[LIB_PATH_MAX];
static mi_fmt_t  s_tags_fmt;
static bool      s_tags_requested;
#endif

/* ------------------------------------------------------------------ the level machine */

/* No level change animates. The full-screen sheets are 800x480, and a transition that
 * redraws one of them per frame was measured to stutter on this panel; the level moves
 * in one frame, before LVGL draws. */
static void sheet_hide_now(int level)
{
    lv_obj_t *o = s_sheets[level].sheet;
    if (!o) return;
    lv_anim_delete(o, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_obj_set_y(o, s_sheets[level].hidden_y);
    lv_obj_set_hidden(o, true);
}

/* Put a sheet where it rests, visible; every other sheet is hidden by level_settled(). */
static void sheet_show_at_rest(int level)
{
    lv_obj_t *o = s_sheets[level].sheet;
    if (!o) return;
    lv_anim_delete(o, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_obj_set_y(o, s_sheets[level].rest_y);
    lv_obj_set_hidden(o, false);
    /* The Artwork screen sits above the decorations. The K7 cover stays below the bar:
     * it is the cassette's own label. */
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    if (level == LEVEL_ART) {
        lv_obj_move_foreground(o);
    }
#endif
}

/* Leave exactly the target sheet visible. */
static void level_settled(void)
{
    for (int level = LEVEL_BARE + 1; level <= LEVEL_MAX; level++) {
        if (level != s_level) sheet_hide_now(level);
    }
}

#if CONFIG_APP_BOARD_SUNTON_8048S050C
/* ------------------------------------------------------------------ the backdrop */

/* The cover's alpha shapes look through to the screen. The screen's backdrop is mixed
 * between the cassette's colour and the cover's: 0 is k_col_bg, 255 is k_col_cover_bg, so
 * the Label cover level can fade its own backdrop in behind the picture. */
#define COVER_BG_MIX_COVER 255
static uint8_t s_cover_bg_mix;

static void cover_bg_mix_set(uint8_t mix)
{
    if (mix == s_cover_bg_mix) return;
    s_cover_bg_mix = mix;
    lv_obj_set_style_bg_color(s_screen, lv_color_mix(k_col_cover_bg, k_col_bg, mix), 0);
}

static void cover_bg_fade_cb(void *var, int32_t value)
{
    (void)var;
    cover_bg_mix_set((uint8_t)value);
}

/* Fade between the two backdrops from whatever is on screen now, so a fade can be
 * reversed in flight without a jump. */
static void cover_bg_fade(bool cover, uint32_t ms)
{
    lv_anim_delete(s_screen, cover_bg_fade_cb);
    int32_t target = cover ? COVER_BG_MIX_COVER : 0;
    if ((int32_t)s_cover_bg_mix == target) return;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_screen);
    lv_anim_set_exec_cb(&a, cover_bg_fade_cb);
    lv_anim_set_values(&a, s_cover_bg_mix, target);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

/* No animation: used before a level change settles the backdrop at once. */
static void cover_bg_now(void)
{
    lv_anim_delete(s_screen, cover_bg_fade_cb);
    cover_bg_mix_set(0);
}

/* ------------------------------------------------------------- the Artwork picture */

/* Ask the loader for the current Track's picture. The loader dedupes too, but this
 * remembers the request so a 25 Hz update is a string compare, not a lock. */
static void art_request(void)
{
    if (!s_have_status || !s_art_sheet) return;
    if (s_art_requested && s_art_fmt == s_status.format &&
        strcmp(s_art_path, s_status.path) == 0) return;
    s_art_requested = true;
    s_art_fmt = s_status.format;
    snprintf(s_art_path, sizeof s_art_path, "%s", s_status.path);
    /* A new Track: put the frame back until its picture lands, rather than leaving the
     * previous Track's cover up while it decodes. */
    if (s_art_img) lv_obj_set_hidden(s_art_img, true);
    ESP_LOGI(TAG, "artwork request: %s (fmt %d)", s_status.path, (int)s_status.format);
    artwork_loader_request(s_status.path, s_status.format);
}

/* The text tags are requested for every Track, not only while the Artwork Level is up:
 * the group and song tags live on Bare's peek. Deduped on path and format, so the 25 Hz
 * update is a string compare; a new Track drops the previous tags at once and the peek
 * shows the path/filename fallback until the new ones land. */
static void tags_request(void)
{
    if (!s_have_status) return;
    if (s_tags_requested && s_tags_fmt == s_status.format &&
        strcmp(s_tags_path, s_status.path) == 0) return;
    s_tags_requested = true;
    s_tags_fmt = s_status.format;
    snprintf(s_tags_path, sizeof s_tags_path, "%s", s_status.path);
    cassette_peek_set_path(s_status.path);
    if (!s_status.path[0] || s_status.format == MI_FMT_NONE) return;
    ESP_LOGI(TAG, "tags request: %s (fmt %d)", s_status.path, (int)s_status.format);
    artwork_loader_request_tags(s_status.path, s_status.format);
}
#endif /* CONFIG_APP_BOARD_SUNTON_8048S050C */

/* Move the level by one. The level is a position, not a command, so a swipe at an end
 * does nothing and the ends do not wrap. */
static void set_level(int target)
{
    if (target < LEVEL_BARE) target = LEVEL_BARE;
    if (target > LEVEL_MAX) target = LEVEL_MAX;
    if (target == s_level) return;

    s_level = target;
    const level_sheet_t *sheet = &s_sheets[target];

#if CONFIG_APP_BOARD_SUNTON_8048S050C
    /* The peek belongs to Bare alone. Any Level change puts it away before the target
     * sheet is placed. */
    cassette_peek_hide_now();

    if (sheet->wants_art) art_request();

    switch (sheet->backdrop) {
        case BACKDROP_COVER:
            cover_bg_now();
            cover_bg_fade(true, COVER_BG_FADE_MS);
            break;
        case BACKDROP_ART:
            cover_bg_now();
            break;
        default:
            cover_bg_fade(false, COVER_BG_FADE_MS);
            break;
    }
#endif

    if (sheet->sheet) sheet_show_at_rest(target);
    level_settled();

    ESP_LOGI(TAG, "level %d", s_level);
}

/* A down swipe moves one Level towards Artwork. At Bare the peek is not a Level: while
 * it is up, the down swipe puts it away first; only the next one moves to the K7 cover. */
void cassette_levels_swipe_down(void)
{
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    if (s_level == LEVEL_BARE && cassette_peek_is_shown()) {
        cassette_peek_hide();
        ESP_LOGI(TAG, "swipe down at Bare: peek hidden");
        return;
    }
#endif
    set_level(s_level + 1);
}

/* An up swipe moves one Level towards Bare. At Bare there is no Level above to move to:
 * the up swipe raises the peek, and while it is up the next up swipe puts it away. */
void cassette_levels_swipe_up(void)
{
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    if (s_level == LEVEL_BARE) {
        bool show = !cassette_peek_is_shown();
        if (show) cassette_peek_show(); else cassette_peek_hide();
        ESP_LOGI(TAG, "swipe up at Bare: peek %s", show ? "shown" : "hidden");
        return;
    }
#endif
    set_level(s_level - 1);
}

/* ------------------------------------------------------------------ titles */

void cassette_levels_apply(const cassette_frame_t *frame)
{
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    if (!frame) return;

    /* The cover and the Artwork band keep the cleaned title; the peek has its own
     * group/song tags and its clock, and takes the frame directly. */
    if (frame->title_changed) {
        const char *label = frame->title[0] ? frame->title
                          : frame->playing ? ""
                          : "no card, or nothing to play";
        for (size_t i = 0; i < sizeof s_titles / sizeof s_titles[0]; i++)
            lv_label_set_text(s_titles[i], label);
    }
    cassette_peek_apply(frame);
#else
    /* Bare alone has no title on it: the two bands are the touch board's, so there is
     * nothing to write. */
    (void)frame;
#endif
}

void cassette_levels_update(const mini_icecast_status_t *status)
{
    if (status) {
        s_status = *status;
        s_have_status = true;
    }

#if CONFIG_APP_BOARD_SUNTON_8048S050C
    /* The text tags follow every Track, on Bare's peek too. */
    tags_request();
    /* At the Artwork level a track change means a new picture. The module remembers
     * the request, so this is a string compare on the frames that do not change it. */
    if (s_level == LEVEL_ART) art_request();
#endif
}

/* ------------------------------------------------------------------- build */

void cassette_levels_create(lv_obj_t *screen, cassette_levels_room_cb_t room, void *ctx)
{
    /* Bare is the one Level every board has: the cassette alone, with no sheet. */
    s_sheets[LEVEL_BARE] = (level_sheet_t){ NULL, 0, 0, false, BACKDROP_CASSETTE };

#if CONFIG_APP_BOARD_SUNTON_8048S050C
    s_screen = screen;

    /* The peek: the timeline bar and the two tags the up swipe at Bare raises. It is
     * not a Level and it owns its own objects; the room callback is the view's, passed
     * straight through. */
    cassette_peek_create(screen, room, ctx);

    /* The big cover: the supplied full cover at 0,0, one swipe past Bare. Cover art is
     * 800x480, the panel's size, so it sits full-bleed. Its black shapes are
     * transparent, so the reels show through them; the title is written on the empty
     * band at the top and is its child. */
    s_cover = cassette_image(screen, &k_label_cover, COVER_X, COVER_Y,
                             LABEL_COVER_W, LABEL_COVER_H);
    lv_obj_set_y(s_cover, COVER_HIDDEN_Y);
    lv_obj_set_hidden(s_cover, true);
    s_cover_title = cassette_text(s_cover, &jura_36, k_col_cover_ink,
                                  LV_ALIGN_TOP_LEFT, COVER_TITLE_X, COVER_TITLE_Y);
    lv_label_set_long_mode(s_cover_title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_cover_title, COVER_TITLE_W);

    s_sheets[LEVEL_COVER] = (level_sheet_t){ s_cover, COVER_Y, COVER_HIDDEN_Y, false,
                                             BACKDROP_COVER };

    /* The Artwork level: its own full screen, above everything else. The Case backdrop
     * is full-bleed at 0,0; the Track picture fills its window at 101,23. The loader
     * owns both sets of pixels, and both are hidden until they land. */
    s_art_sheet = cassette_box(screen, 0, 0, 800, 480, 0, k_col_art_bg);
    lv_obj_set_y(s_art_sheet, ART_HIDDEN_Y);
    lv_obj_set_hidden(s_art_sheet, true);

    s_case_img = cassette_image(s_art_sheet, NULL, 0, 0, 800, 480);
    lv_obj_set_hidden(s_case_img, true);

    s_art_img = cassette_image(s_art_sheet, NULL, ART_X, ART_Y, ART_W, ART_H);
    lv_obj_set_hidden(s_art_img, true);

    s_art_title = cassette_text(s_art_sheet, &jura_36, k_col_cover_ink,
                                LV_ALIGN_TOP_MID, 0, ART_TITLE_Y);
    lv_obj_set_width(s_art_title, ART_TITLE_W);
    lv_label_set_long_mode(s_art_title, LV_LABEL_LONG_DOT);

    s_titles[0] = s_cover_title;
    s_titles[1] = s_art_title;

    s_sheets[LEVEL_ART] = (level_sheet_t){ s_art_sheet, 0, ART_HIDDEN_Y, true,
                                           BACKDROP_ART };
#else
    /* A board without the touch adapter has Bare alone, so there is no sheet to build
     * and no peek to give room to. */
    (void)screen;
    (void)room;
    (void)ctx;
#endif
}

/* ------------------------------------------------------------- the loader's pictures */

void cassette_levels_set_artwork(const lv_image_dsc_t *dsc)
{
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    if (!s_art_img) return;
    if (dsc) {
        lv_image_set_src(s_art_img, dsc);
        lv_obj_set_hidden(s_art_img, false);
    } else {
        lv_obj_set_hidden(s_art_img, true);
    }
#else
    (void)dsc;
#endif
}

void cassette_levels_set_case(const lv_image_dsc_t *dsc)
{
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    if (!s_case_img) return;
    if (dsc) {
        lv_image_set_src(s_case_img, dsc);
        lv_obj_set_hidden(s_case_img, false);
    } else {
        lv_obj_set_hidden(s_case_img, true);
    }
#else
    (void)dsc;
#endif
}

/* ------------------------------------------------------------- the peek's inputs */

void cassette_levels_set_tags(const char *group, const char *song)
{
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    cassette_peek_set_tags(group, song);
#else
    (void)group;
    (void)song;
#endif
}

void cassette_levels_set_hubs(int x0, int y0, int x1, int y1)
{
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    cassette_peek_set_hubs(x0, y0, x1, y1);
#else
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
#endif
}
