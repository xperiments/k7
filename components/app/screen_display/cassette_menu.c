/*
 * cassette_menu.c - the Menu's view; see cassette_menu.h.
 *
 * The page is the controls' own window: the model's five rows are drawn inside it and the
 * window's fill takes the page's colour for the mode's lifetime. The Pad is a plate over
 * the DJ block's footprint, drawn as the block's six cells, and it is the only activator:
 * a vertical drag ratchets the model's band, a tap that never became a drag activates it.
 *
 * The model (components/portable/menu) owns the tree, the band and the ratchet; this file
 * draws five row slots, executes the model's actions and owns the mode's lifecycle: hide
 * the cassette container (tape, meter, glow, cover), cover the block, turn the drawer into
 * Back/Enter/Close, and put all of it back on close. No per-Track object is created: five
 * slots are rebound whenever the model's version changes.
 */
#include "cassette_menu.h"

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "lvgl.h"

#include "app_board.h"
#include "app_services.h"
#include "backlight.h"
#include "cassette_buttons.h"
#include "cassette.h"
#include "cassette_eq.h"
#include "cassette_footer.h"
#include "cassette_header.h"
#include "cassette_icons.h"
#include "cassette_style.h"
#include "cassette_widgets.h"
#include "cassette_window.h"
#include "library.h"
#include "menu.h"
#include "mini_icecast.h"

LV_FONT_DECLARE(jura_28);

static const char *TAG = "menu";

/* The page's inner rect is the window's minus its 2 px border; the rows are 56 px and five
 * of them fill its 280 px exactly (docs/specs/menu.md). */
#define MENU_ROW_H 56
#define MENU_ROW_LABEL_W 300
#define MENU_ROW_LABEL_W_VALUE 240
#define MENU_ROW_VALUE_W 150
#define MENU_ROW_LABEL_X 56
#define MENU_ROW_RIGHT_INSET 16
#define MENU_ROW_DOT_X 364
#define MENU_ROW_DOT_D 6

/* The Pad sits inside the DJ block's place: a white plate (k_col_white) with a dark inner
 * (k_col_bg) inset 8 px, and three dotted guides inside the inner. Its left edge meets the
 * window's right edge at x=596. */
#define PAD_X 596
#define PAD_Y 76
#define PAD_W 186
#define PAD_H 284
#define PAD_INSET 8

/* The inner plate, and its guides: two dotted verticals 20 px either side of its centre,
 * inset 20 px from its top and bottom. */
#define PAD_INNER_W (PAD_W - 2 * PAD_INSET)
#define PAD_INNER_H (PAD_H - 2 * PAD_INSET)
#define PAD_GUIDE_INSET 20
#define PAD_GUIDE_X0 (PAD_INNER_W / 2 - PAD_GUIDE_INSET)
#define PAD_GUIDE_X1 (PAD_INNER_W / 2 + PAD_GUIDE_INSET)
#define PAD_GUIDE_TOP PAD_GUIDE_INSET
#define PAD_GUIDE_BOT (PAD_INNER_H - PAD_GUIDE_INSET)

/* lv_line keeps the caller's points, so they are static and constant. */
static const lv_point_precise_t k_pad_guides[2][2] = {
    { { PAD_GUIDE_X0, PAD_GUIDE_TOP }, { PAD_GUIDE_X0, PAD_GUIDE_BOT } },
    { { PAD_GUIDE_X1, PAD_GUIDE_TOP }, { PAD_GUIDE_X1, PAD_GUIDE_BOT } },
};

/* A tap is a press that never travelled this far; the pad's ratchet owns everything else. */
#define PAD_TAP_SLOP 8

static lv_obj_t *s_page;             /* the five rows, inside the window */
static lv_obj_t *s_pad;              /* the plate over the DJ block */
static lv_obj_t *s_row[MENU_ROWS];
static lv_obj_t *s_band[MENU_ROWS];
static lv_obj_t *s_icon[MENU_ROWS];
static lv_obj_t *s_label[MENU_ROWS];
static lv_obj_t *s_right[MENU_ROWS];
static lv_obj_t *s_dot[MENU_ROWS];

static lv_obj_t *s_cassette;         /* hidden for the mode's lifetime */
static menu_t *s_model;
static const library_t *s_lib;       /* the lease while open */
static bool s_open;
static bool s_white;                 /* the page the Menu was opened on */
static uint32_t s_drawn;             /* the model version on screen */
static mini_icecast_status_t s_status;
static menu_row_t s_rows[MENU_ROWS];
static menu_facts_t s_facts;

static char s_version[32];
static char s_board[40];
static char s_partition[24];

static char s_tag_group[128];
static char s_tag_song[128];
static char s_footer_group[128];
static char s_footer_song[128];
static char s_footer_pos[32];

/* The pad's press tracking, for the tap/ratchet split. */
static bool s_pad_pressed;
static int s_pad_last_y;
static uint32_t s_pad_last_t;
static int s_pad_moved;

/* ------------------------------------------------------------------ helpers */

static lv_color_t page_ink(void) { return s_white ? k_col_bg : k_col_primary; }
static lv_color_t band_fill(void) { return s_white ? k_col_bg : k_col_primary; }
static lv_color_t band_ink(void) { return s_white ? k_col_button_white : k_col_bg; }

/* The icon font's glyph for a row's icon, its codepoint, and whether the font carries it:
 * folder, gear, sun, info and SD are not drawn yet, and a missing glyph is hidden rather
 * than shown as .notdef. */
typedef struct {
    const char *glyph;
    uint32_t    cp;
} icon_glyph_t;

static icon_glyph_t icon_of(menu_icon_t icon)
{
    switch (icon) {
        case MENU_ICON_TRACKS:
        case MENU_ICON_TRACK:
        case MENU_ICON_STATION:
            return (icon_glyph_t){ CASSETTE_ICON_RADIO, CASSETTE_ICON_RADIO_CP };
        case MENU_ICON_BROWSE:
            return (icon_glyph_t){ CASSETTE_ICON_FOLDER, CASSETTE_ICON_FOLDER_CP };
        case MENU_ICON_DIRECTORY:
            return (icon_glyph_t){ CASSETTE_ICON_TAPE, CASSETTE_ICON_TAPE_CP };
        case MENU_ICON_SHUFFLE:
            return (icon_glyph_t){ CASSETTE_ICON_SHUFFLE, CASSETTE_ICON_SHUFFLE_CP };
        case MENU_ICON_SETTINGS:
            return (icon_glyph_t){ CASSETTE_ICON_GEAR, CASSETTE_ICON_GEAR_CP };
        case MENU_ICON_BACKLIGHT:
            return (icon_glyph_t){ CASSETTE_ICON_SUN, CASSETTE_ICON_SUN_CP };
        case MENU_ICON_LIBRARY:
            return (icon_glyph_t){ CASSETTE_ICON_SD, CASSETTE_ICON_SD_CP };
        case MENU_ICON_RESCAN:
            return (icon_glyph_t){ CASSETTE_ICON_LOOP, CASSETTE_ICON_LOOP_CP };
        case MENU_ICON_ABOUT:
            return (icon_glyph_t){ CASSETTE_ICON_INFO, CASSETTE_ICON_INFO_CP };
        case MENU_ICON_NONE:
        default:
            return (icon_glyph_t){ NULL, 0 };
    }
}

static bool icon_available(uint32_t cp)
{
    lv_font_glyph_dsc_t d;
    if (cp == 0) return false;
    return lv_font_get_glyph_dsc(&xtape_24, &d, cp, 0);
}

static menu_facts_t build_facts(void)
{
    menu_facts_t f;
    memset(&f, 0, sizeof f);
    f.shuffle_on = mini_icecast_shuffle_enabled();
    f.rescanning = app_services_rescan_running();
    f.backlight = backlight_hold();
    f.version = s_version;
    f.board = s_board;
    f.partition = s_partition;
    return f;
}

/* ------------------------------------------------------------------ drawing */

static void draw_rows(void)
{
    if (!s_model || !s_open) return;
    if (!menu_rows(s_model, s_rows)) return;

    int band = menu_band(s_model);
    cassette_header_set_title(menu_title(s_model));

    for (int i = 0; i < MENU_ROWS; i++) {
        bool sel = i == band;
        lv_color_t ink = sel ? band_ink() : page_ink();
        lv_opa_t dim = s_rows[i].enabled ? LV_OPA_COVER : LV_OPA_40;

        lv_obj_set_hidden(s_band[i], !sel);
        if (sel) lv_obj_set_style_bg_color(s_band[i], band_fill(), 0);

        icon_glyph_t g = icon_of(s_rows[i].icon);
        if (g.glyph && icon_available(g.cp)) {
            lv_label_set_text_static(s_icon[i], g.glyph);
            lv_obj_set_style_text_color(s_icon[i], ink, 0);
            lv_obj_set_style_opa(s_icon[i], dim, 0);
            lv_obj_set_hidden(s_icon[i], false);
        } else {
            lv_obj_set_hidden(s_icon[i], true);
        }

        lv_obj_set_width(s_label[i], s_rows[i].kind == MENU_ROW_VALUE ? MENU_ROW_LABEL_W_VALUE
                                                                     : MENU_ROW_LABEL_W);
        /* LONG_DOT writes its dots into the label's own copy, so the model's buffer is
         * copied rather than handed to LVGL as static text (LVGL 9 does not allow the
         * dot mode on static text). Five small copies per redraw, not per frame. */
        lv_label_set_text(s_label[i], s_rows[i].label);
        lv_obj_set_style_text_color(s_label[i], ink, 0);
        lv_obj_set_style_opa(s_label[i], dim, 0);

        lv_obj_set_hidden(s_dot[i], !s_rows[i].playing);
        if (s_rows[i].playing) lv_obj_set_style_bg_color(s_dot[i], ink, 0);

        const char *right = s_rows[i].value[0] ? s_rows[i].value
                                               : (s_rows[i].chevron ? ">" : NULL);
        if (right) {
            lv_label_set_text(s_right[i], right);
            lv_obj_set_style_text_color(s_right[i], ink, 0);
            lv_obj_set_style_opa(s_right[i], dim, 0);
            lv_obj_set_hidden(s_right[i], false);
        } else {
            lv_obj_set_hidden(s_right[i], true);
        }
    }
    s_drawn = menu_version(s_model);
}

static void update_footer(void)
{
    if (!s_open) return;

    char song[128] = "";
    char group[128] = "";
    char pos[32] = "";

    if (s_tag_song[0]) {
        snprintf(song, sizeof song, "%s", s_tag_song);
    } else if (s_status.title[0]) {
        menu_clean_name(s_status.title, song, sizeof song);
    }
    if (s_tag_group[0]) {
        snprintf(group, sizeof group, "%s", s_tag_group);
    } else {
        cassette_path_group(s_status.path, group, sizeof group);
    }
    int count = menu_count(s_model);
    if (count > 0) snprintf(pos, sizeof pos, "%d / %d", menu_position(s_model), count);

    if (strcmp(song, s_footer_song) == 0 && strcmp(group, s_footer_group) == 0 &&
        strcmp(pos, s_footer_pos) == 0) {
        return;
    }
    snprintf(s_footer_song, sizeof s_footer_song, "%s", song);
    snprintf(s_footer_group, sizeof s_footer_group, "%s", group);
    snprintf(s_footer_pos, sizeof s_footer_pos, "%s", pos);
    cassette_footer_set_playing(song, group, pos);
}

/* Re-read the facts, hand them and the status to the model, and redraw what changed. A
 * rescan may have published a new generation: take the new lease first, then drop the old
 * one, so the model's re-resolve walks the new tree. */
static void refresh(void)
{
    if (!s_open || !s_model) return;

    uint32_t current = library_current_generation();
    if (current != 0 && (!s_lib || library_generation(s_lib) != current)) {
        const library_t *fresh = library_acquire();
        if (fresh) {
            if (s_lib) library_release(s_lib);
            s_lib = fresh;
        }
    }

    s_facts = build_facts();
    menu_update(s_model, s_lib, &s_facts, &s_status);
    if (menu_version(s_model) != s_drawn) draw_rows();
    update_footer();
}

/* ------------------------------------------------------------------ actions */

static void activate(void)
{
    if (!s_open) return;
    menu_action_t action = menu_activate(s_model);
    switch (action) {
        case MENU_ACTION_PLAY: {
            const char *path = menu_action_path(s_model);
            if (path && path[0] && !mini_icecast_play(path)) {
                ESP_LOGW(TAG, "the broadcast refused %s", path);
            }
            break;
        }
        case MENU_ACTION_RESCAN:
            if (app_services_rescan() != ESP_OK) {
                ESP_LOGW(TAG, "rescan did not start");
            }
            break;
        case MENU_ACTION_BACKLIGHT:
            backlight_set_hold(menu_action_backlight(s_model));
            break;
        case MENU_ACTION_SHUFFLE:
            mini_icecast_shuffle_set(!mini_icecast_shuffle_enabled());
            break;
        case MENU_ACTION_CLOSE:
            cassette_menu_close();
            return;
        default:
            break;
    }
    refresh();
}

static void menu_back_cb(void *ctx)
{
    (void)ctx;
    if (!s_open) return;
    if (menu_back(s_model)) cassette_menu_close();
    else refresh();
}

static void menu_enter_cb(void *ctx)
{
    (void)ctx;
    activate();
}

static void menu_close_cb(void *ctx)
{
    (void)ctx;
    cassette_menu_close();
}

/* The Pad: one plate. A vertical drag ratchets the model's band; a press that never
 * travelled PAD_TAP_SLOP activates the band. The plate does not let its gestures bubble,
 * so a drag here can never close the Menu as an up swipe. */
static void pad_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (!s_open || !s_model) return;

    lv_indev_t *indev = lv_indev_active();
    lv_point_t p;

    switch (code) {
        case LV_EVENT_PRESSED:
            if (!indev) return;
            lv_indev_get_point(indev, &p);
            s_pad_pressed = true;
            s_pad_last_y = p.y;
            s_pad_last_t = lv_tick_get();
            s_pad_moved = 0;
            break;
        case LV_EVENT_PRESSING: {
            if (!indev || !s_pad_pressed) return;
            lv_indev_get_point(indev, &p);
            int dy = p.y - s_pad_last_y;
            if (dy == 0) return;
            uint32_t now = lv_tick_get();
            uint32_t dt = now - s_pad_last_t;
            s_pad_last_y = p.y;
            s_pad_last_t = now;
            s_pad_moved += dy < 0 ? -dy : dy;
            menu_drag(s_model, dy, dt);
            refresh();
            break;
        }
        case LV_EVENT_RELEASED:
            if (s_pad_pressed && s_pad_moved < PAD_TAP_SLOP) activate();
            s_pad_pressed = false;
            break;
        case LV_EVENT_PRESS_LOST:
            s_pad_pressed = false;
            break;
        default:
            break;
    }
}

/* ------------------------------------------------------------------ lifecycle */

void cassette_menu_create(lv_obj_t *screen, lv_obj_t *cassette)
{
    if (!screen || s_model) return;

    s_cassette = cassette;

    int wx = 0, wy = 0, ww = 0, wh = 0;
    cassette_window_rect(&wx, &wy, &ww, &wh);
    int px = wx + 2, py = wy + 2, pw = ww - 4, ph = wh - 4;

    s_page = lv_obj_create(screen);
    lv_obj_remove_style_all(s_page);
    lv_obj_set_pos(s_page, px, py);
    lv_obj_set_size(s_page, pw, ph);
    lv_obj_set_style_bg_opa(s_page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_page, 0, 0);
    lv_obj_set_style_pad_all(s_page, 0, 0);
    lv_obj_set_scrollable(s_page, false);
    lv_obj_set_clickable(s_page, false);

    for (int i = 0; i < MENU_ROWS; i++) {
        s_row[i] = lv_obj_create(s_page);
        lv_obj_remove_style_all(s_row[i]);
        lv_obj_set_pos(s_row[i], 0, i * MENU_ROW_H);
        lv_obj_set_size(s_row[i], pw, MENU_ROW_H);
        lv_obj_set_style_bg_opa(s_row[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_row[i], 0, 0);
        lv_obj_set_scrollable(s_row[i], false);
        lv_obj_set_clickable(s_row[i], false);

        s_band[i] = cassette_border_box(s_row[i], 2, 2, pw - 4, MENU_ROW_H - 4, 8,
                                        k_col_bg, LV_OPA_COVER, k_col_bg, 0);
        lv_obj_set_hidden(s_band[i], true);

        s_icon[i] = cassette_text(s_row[i], &xtape_24, k_col_bg, LV_ALIGN_LEFT_MID, 16, 0);

        s_label[i] = cassette_text(s_row[i], &jura_28, k_col_bg, LV_ALIGN_LEFT_MID,
                                   MENU_ROW_LABEL_X, 0);
        lv_obj_set_width(s_label[i], MENU_ROW_LABEL_W);
        lv_label_set_long_mode(s_label[i], LV_LABEL_LONG_DOT);

        s_right[i] = cassette_text(s_row[i], &jura_28, k_col_bg, LV_ALIGN_RIGHT_MID,
                                   -MENU_ROW_RIGHT_INSET, 0);
        lv_obj_set_width(s_right[i], MENU_ROW_VALUE_W);
        lv_label_set_long_mode(s_right[i], LV_LABEL_LONG_DOT);

        s_dot[i] = cassette_box(s_row[i], MENU_ROW_DOT_X, (MENU_ROW_H - MENU_ROW_DOT_D) / 2,
                                MENU_ROW_DOT_D, MENU_ROW_DOT_D, MENU_ROW_DOT_D / 2, k_col_bg);
        lv_obj_set_hidden(s_dot[i], true);
    }
    lv_obj_set_hidden(s_page, true);

    /* The Pad: the owner's white plate with a dark inner, over the block's place. The six
     * carcases it replaces are not drawn in the mode. */
    s_pad = lv_obj_create(screen);
    lv_obj_remove_style_all(s_pad);
    lv_obj_set_pos(s_pad, PAD_X, PAD_Y);
    lv_obj_set_size(s_pad, PAD_W, PAD_H);
    lv_obj_set_style_bg_color(s_pad, k_col_white, 0);
    lv_obj_set_style_bg_opa(s_pad, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_pad, 0, 0);
    lv_obj_set_style_radius(s_pad, 8, 0);
    lv_obj_set_style_pad_all(s_pad, 0, 0);
    lv_obj_set_scrollable(s_pad, false);
    lv_obj_set_clickable(s_pad, true);
    lv_obj_set_gesture_bubble(s_pad, false);
    lv_obj_add_event_cb(s_pad, pad_event_cb, LV_EVENT_ALL, NULL);

    /* The inner plate: the dark ground the guides sit on. It is decorative, so a press on
     * it (or on a guide) falls through to the Pad and ratchets the list. */
    lv_obj_t *inner = lv_obj_create(s_pad);
    lv_obj_remove_style_all(inner);
    lv_obj_set_pos(inner, PAD_INSET, PAD_INSET);
    lv_obj_set_size(inner, PAD_INNER_W, PAD_INNER_H);
    lv_obj_set_style_bg_color(inner, k_col_bg, 0);
    lv_obj_set_style_bg_opa(inner, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(inner, 0, 0);
    lv_obj_set_style_radius(inner, PAD_INSET, 0);
    lv_obj_set_style_pad_all(inner, 0, 0);
    lv_obj_set_scrollable(inner, false);
    lv_obj_set_clickable(inner, false);

    /* The guides: dotted, light on the dark inner. */
    for (int i = 0; i < 2; i++) {
        lv_obj_t *line = lv_line_create(inner);
        lv_obj_remove_style_all(line);
        lv_obj_set_pos(line, 0, 0);
        lv_obj_set_style_line_width(line, 2, 0);
        lv_obj_set_style_line_color(line, k_col_white, 0);
        lv_obj_set_style_line_opa(line, LV_OPA_COVER, 0);
        lv_obj_set_style_line_rounded(line, true, 0);
        lv_obj_set_style_line_dash_width(line, 2, 0);
        lv_obj_set_style_line_dash_gap(line, 4, 0);
        lv_obj_set_clickable(line, false);
        lv_line_set_points(line, k_pad_guides[i], 2);
    }
    lv_obj_set_hidden(s_pad, true);

    s_model = menu_new();
    if (!s_model) {
        ESP_LOGE(TAG, "no memory for the Menu's model: M does nothing");
        return;
    }

    const esp_app_desc_t *desc = esp_app_get_description();
    snprintf(s_version, sizeof s_version, "%s", desc && desc->version[0] ? desc->version
                                                                        : "unknown");
    snprintf(s_board, sizeof s_board, "%s", app_board_name());
    const esp_partition_t *part = esp_ota_get_running_partition();
    if (part) {
        snprintf(s_partition, sizeof s_partition, "%u MB",
                 (unsigned)(part->size / (1024u * 1024u)));
    } else {
        snprintf(s_partition, sizeof s_partition, "%s", "unknown");
    }
    memset(&s_facts, 0, sizeof s_facts);
    memset(&s_status, 0, sizeof s_status);
}

void cassette_menu_open(bool white)
{
    if (!s_model || s_open) return;

    s_open = true;
    s_white = white;
    s_drawn = 0;
    mini_icecast_status(&s_status);
    s_lib = library_acquire();

    s_facts = build_facts();
    menu_open(s_model, s_lib, &s_facts);
    /* The status reaches the model before the first draw, so the Station values and the
     * playing Track's marker are right in the frame the Menu opens in. */
    menu_update(s_model, s_lib, &s_facts, &s_status);

    cassette_window_set_menu_page(true, white);
    cassette_buttons_set_menu_mode(true, menu_back_cb, menu_enter_cb, menu_close_cb, NULL);
    cassette_eq_set_covered(true);
    if (s_cassette) lv_obj_set_hidden(s_cassette, true);

    lv_obj_set_hidden(s_page, false);
    lv_obj_set_hidden(s_pad, false);
    memset(s_footer_song, 0, sizeof s_footer_song);
    memset(s_footer_group, 0, sizeof s_footer_group);
    memset(s_footer_pos, 0, sizeof s_footer_pos);
    draw_rows();
    update_footer();
}

void cassette_menu_close(void)
{
    if (!s_open) return;
    s_open = false;

    lv_obj_set_hidden(s_page, true);
    lv_obj_set_hidden(s_pad, true);

    cassette_window_set_menu_page(false, s_white);
    cassette_header_set_title(NULL);
    cassette_footer_set_playing(NULL, NULL, NULL);
    cassette_buttons_set_menu_mode(false, NULL, NULL, NULL, NULL);
    cassette_eq_set_covered(false);
    if (s_cassette) lv_obj_set_hidden(s_cassette, false);

    menu_close(s_model);
    if (s_lib) {
        library_release(s_lib);
        s_lib = NULL;
    }
    s_pad_pressed = false;
    s_drawn = 0;
}

bool cassette_menu_is_open(void)
{
    return s_open;
}

void cassette_menu_update(const mini_icecast_status_t *status)
{
    if (!s_open) return;
    if (status) s_status = *status;
    refresh();
}

void cassette_menu_set_tags(const char *group, const char *song)
{
    snprintf(s_tag_group, sizeof s_tag_group, "%s", group ? group : "");
    snprintf(s_tag_song, sizeof s_tag_song, "%s", song ? song : "");
    if (s_open) update_footer();
}
