/*
 * cassette_menu.c - the Menu's view; see cassette_menu.h.
 *
 * The page is the controls' own window: the model's five rows are drawn inside its 6 px
 * padding and the window's fill takes the page's colour for the mode's lifetime. The Pad is
 * a white plate over the DJ block's footprint, and it is the only activator: a vertical drag
 * ratchets the model's band, a tap that never became a drag activates it, and a leftward
 * swipe is the drawer's Back.
 *
 * The model (components/portable/menu) owns the tree, the band and the ratchet; this file
 * draws five row slots, executes the model's actions and owns the mode's lifecycle: hide
 * the cassette container (tape, meter, glow, cover), swap the right bar's panel to the Pad,
 * turn the drawer into Back/Enter/Close, and put all of it back on close. No per-Track
 * object is created: five slots are rebound whenever the model's version changes.
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
#include "cassette_bars.h"
#include "cassette_buttons.h"
#include "cassette.h"
#include "cassette_eq.h"
#include "cassette_footer.h"
#include "cassette_header.h"
#include "cassette_icons.h"
#include "cassette_layout.h"
#include "cassette_style.h"
#include "cassette_widgets.h"
#include "cassette_window.h"
#include "library.h"
#include "menu.h"
#include "pad.h"
#include "mini_icecast.h"

LV_FONT_DECLARE(jura_20);
LV_FONT_DECLARE(jura_28);

static const char *TAG = "menu";

/* The page and its slots come from the layout: the window's inner 482x280, a 6 px inset
 * framing five slots of 54/53/54/53/54 px (docs/specs/menu.md). The row contents below stay
 * the Menu's own. */
#define MENU_PAD CASSETTE_PAGE_INSET
#define MENU_ROW_LABEL_W 400
#define MENU_ROW_LABEL_W_VALUE 340
#define MENU_ROW_VALUE_W 150
#define MENU_ROW_LABEL_X 56
#define MENU_ROW_RIGHT_INSET 16
#define MENU_ROW_DOT_X 364
#define MENU_ROW_DOT_D 6

/* The Pad is the right bar's one-cell panel: the layout's part is a white plate (k_col_white)
 * over the block's place, with a dark inner (k_col_bg) inset 8 px and one dotted vertical
 * guide inside it. Only the inner and the guide are the Pad's own numbers. */
#define PAD_INSET 8

/* The inner plate, and its guide: one dotted vertical, PAD_GUIDE_INSET left of the centre
 * and inset the same distance from its top and bottom. */
#define PAD_GUIDE_INSET 66

/* lv_line keeps the caller's points, so they outlive the call; pad_build() fills them from
 * the layout's Pad each time the panel is installed. */
static lv_point_precise_t s_pad_guides[1][2];



static lv_obj_t *s_page;             /* the five rows, inside the window */
static lv_obj_t *s_pad_cell;         /* the bar's carcase the Pad's plate is built into */
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
static cassette_menu_cb_t s_restore_cb;   /* the view's: put the active group's panel back */
static void *s_restore_ctx;
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
/* The right column's capitals, built per row per redraw. */
static char s_value_upper[MENU_VALUE_MAX];

/* The pad's press classification, as a value; the ratchet itself stays the menu model's. */
static pad_t s_pad_state;

/* ------------------------------------------------------------------ helpers */

static lv_color_t page_ink(void) { return s_white ? k_col_bg : k_col_primary; }
static lv_color_t band_fill(void) { return s_white ? k_col_bg : k_col_primary; }
static lv_color_t band_ink(void) { return s_white ? k_col_button_white : k_col_bg; }

/* The right column speaks in capitals: the model's values are prose ("Auto", "12 MB") and
 * the page is where its capitals belong. ASCII only, like every value the model writes. */
static void value_upper(const char *src, char *dst, size_t n)
{
    size_t i = 0;
    for (; src[i] && i + 1 < n; i++) {
        char c = src[i];
        dst[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    dst[i] = '\0';
}

/* The icon font's glyph for a row's icon, its codepoint, and whether the font carries it:
 * a missing glyph is hidden rather than shown as .notdef. The Shuffle row takes the toggle
 * that matches the mode's state. */
typedef struct {
    const char *glyph;
    uint32_t    cp;
} icon_glyph_t;

static icon_glyph_t icon_of(menu_icon_t icon, bool shuffle_on)
{
    switch (icon) {
        case MENU_ICON_TRACKS:
        case MENU_ICON_STATION:
            return (icon_glyph_t){ CASSETTE_ICON_RADIO, CASSETTE_ICON_RADIO_CP };
        case MENU_ICON_TRACK:
            return (icon_glyph_t){ CASSETTE_ICON_TRACK, CASSETTE_ICON_TRACK_CP };
        case MENU_ICON_BROWSE:
            return (icon_glyph_t){ CASSETTE_ICON_BOOKS, CASSETTE_ICON_BOOKS_CP };
        case MENU_ICON_DIRECTORY:
            return (icon_glyph_t){ CASSETTE_ICON_TAPE, CASSETTE_ICON_TAPE_CP };
        case MENU_ICON_SHUFFLE:
            return shuffle_on
                       ? (icon_glyph_t){ CASSETTE_ICON_TOGGLE_ON, CASSETTE_ICON_TOGGLE_ON_CP }
                       : (icon_glyph_t){ CASSETTE_ICON_TOGGLE_OFF, CASSETTE_ICON_TOGGLE_OFF_CP };
        case MENU_ICON_SETTINGS:
            return (icon_glyph_t){ CASSETTE_ICON_SETTINGS, CASSETTE_ICON_SETTINGS_CP };
        case MENU_ICON_BACKLIGHT:
            return (icon_glyph_t){ CASSETTE_ICON_BRIGHTNESS, CASSETTE_ICON_BRIGHTNESS_CP };
        case MENU_ICON_LIBRARY:
            return (icon_glyph_t){ CASSETTE_ICON_BOOKS, CASSETTE_ICON_BOOKS_CP };
        case MENU_ICON_RESCAN:
            return (icon_glyph_t){ CASSETTE_ICON_LOOP, CASSETTE_ICON_LOOP_CP };
        case MENU_ICON_ABOUT:
            return (icon_glyph_t){ CASSETTE_ICON_K7LOGO, CASSETTE_ICON_K7LOGO_CP };
        case MENU_ICON_FIRMWARE:
            return (icon_glyph_t){ CASSETTE_ICON_MICRO_CHIP, CASSETTE_ICON_MICRO_CHIP_CP };
        case MENU_ICON_BOARD:
            return (icon_glyph_t){ CASSETTE_ICON_TABLET, CASSETTE_ICON_TABLET_CP };
        case MENU_ICON_PARTITION:
            return (icon_glyph_t){ CASSETTE_ICON_DISC, CASSETTE_ICON_DISC_CP };
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

        icon_glyph_t g = icon_of(s_rows[i].icon, s_facts.shuffle_on);
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

        /* The right column: the model's value in the page's capitals and the smaller Jura,
         * or the chevron as the icon font's angle-right - an icon keeps the row's 24 px. */
        if (s_rows[i].value[0]) {
            value_upper(s_rows[i].value, s_value_upper, sizeof s_value_upper);
            lv_label_set_text(s_right[i], s_value_upper);
            lv_obj_set_style_text_font(s_right[i], &jura_20, 0);
        } else if (s_rows[i].chevron) {
            lv_label_set_text(s_right[i], CASSETTE_ICON_RIGHT);
            lv_obj_set_style_text_font(s_right[i], &xtape_24, 0);
        }
        if (s_rows[i].value[0] || s_rows[i].chevron) {
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

/* The Pad: one plate. A vertical drag ratchets the model's band; a press that never
 * travelled PAD_TAP_SLOP activates the band; a leftward swipe as long as the screen's own
 * gesture limit is the drawer's Back, so the Menu can be left from either thumb. The press
 * takes one axis, decided once, so the same touch cannot both ratchet and go Back. The bar
 * gives the plate no gesture bubbling, so no swipe here can close the Menu as an up swipe on
 * the screen behind it. */
static void pad_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (!s_open || !s_model) return;

    /* A gesture on the Pad is the Pad's: the bar's right cells keep their gestures local
     * (the Pad is not the screen's), so the leftward one is read here - it is the Menu's
     * Back/Close, at LVGL's 50 px threshold, whether or not the release also reaches the
     * pad's 50 px rule. */
    if (code == LV_EVENT_GESTURE) {
        lv_indev_t *indev = lv_indev_active();
        if (indev && lv_indev_get_gesture_dir(indev) == LV_DIR_LEFT) {
            pad_reset(&s_pad_state);
            menu_back_cb(NULL);
        }
        return;
    }

    pad_in_t in;
    switch (code) {
        case LV_EVENT_PRESSED: in = PAD_IN_PRESS; break;
        case LV_EVENT_PRESSING: in = PAD_IN_MOVE; break;
        case LV_EVENT_RELEASED: in = PAD_IN_RELEASE; break;
        case LV_EVENT_PRESS_LOST: in = PAD_IN_LOST; break;
        default: return;
    }

    lv_point_t p = { 0, 0 };
    if (in != PAD_IN_LOST) {
        lv_indev_t *indev = lv_indev_active();
        if (!indev) return;
        lv_indev_get_point(indev, &p);
    }

    pad_ev_t ev = pad_feed(&s_pad_state, in, p.x, p.y, lv_tick_get());
    switch (ev.kind) {
        case PAD_EV_STEP:
            menu_drag(s_model, ev.dy, ev.dt);
            refresh();
            break;
        case PAD_EV_TAP:
            activate();
            break;
        case PAD_EV_BACK:
            menu_back_cb(NULL);
            break;
        default:
            break;
    }
}

/* ------------------------------------------------------------------ the panel */

/* The Pad is one plate, not a grid: the bar builds one carcase at the layout's Pad rect
 * (596,76, 186x284) and build() turns it into the plate. It covers the DJ block's M cell as
 * well as its five; M stays the bar's chrome and is inert while the Menu is open. */
static cassette_rect_t pad_cell_rect(int col, int row)
{
    (void)col;
    (void)row;
    return cassette_part_rect(CASSETTE_PART_PAD);
}

/* The plate: white on both pages, no border of its own, the shared 8 px corner. The top and
 * bottom rows are where the header's bottom line and the footer's top line cross the Pad; the
 * bar is above the bands now, so those two rows are drawn as rules of their own. */
static void pad_ink(lv_obj_t *cell)
{
    if (!cell) return;
    lv_obj_set_style_bg_color(cell, k_col_white, 0);
    lv_obj_set_style_border_width(cell, 0, 0);
    lv_obj_set_style_radius(cell, CASSETTE_CORNER_R, 0);
}

/* One carcase, the bar's, turned into the Pad. The bar made it clickable and gave it the
 * panel's hit rule; the pickable surface is the carcase itself, with the inner and the guide
 * decorative so a press on them falls through to it. */
static void pad_build(lv_obj_t *cell, int col, int row, void *ctx)
{
    (void)col;
    (void)row;
    (void)ctx;
    if (!cell) return;

    s_pad_cell = cell;
    pad_ink(cell);

    cassette_rect_t pad = cassette_part_rect(CASSETTE_PART_PAD);

    /* The two frame rows the old screen-child Pad let the bands draw over it. */
    for (int i = 0; i < 2; i++) {
        lv_obj_t *rule = lv_obj_create(cell);
        lv_obj_remove_style_all(rule);
        lv_obj_set_pos(rule, 0, i == 0 ? 0 : pad.h - 2);
        lv_obj_set_size(rule, pad.w, 2);
        lv_obj_set_style_bg_color(rule, k_col_button_rect, 0);
        lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(rule, 0, 0);
        lv_obj_set_style_radius(rule, 0, 0);
        lv_obj_set_clickable(rule, false);
    }

    int inner_w = pad.w - 2 * PAD_INSET;
    int inner_h = pad.h - 2 * PAD_INSET;
    int guide_x = inner_w / 2 - PAD_GUIDE_INSET;
    s_pad_guides[0][0].x = guide_x;
    s_pad_guides[0][0].y = PAD_GUIDE_INSET;
    s_pad_guides[0][1].x = guide_x;
    s_pad_guides[0][1].y = inner_h - PAD_GUIDE_INSET;

    /* The inner plate: the dark ground the guide sits on. */
    lv_obj_t *inner = lv_obj_create(cell);
    lv_obj_remove_style_all(inner);
    lv_obj_set_pos(inner, PAD_INSET, PAD_INSET);
    lv_obj_set_size(inner, inner_w, inner_h);
    lv_obj_set_style_bg_color(inner, k_col_bg, 0);
    lv_obj_set_style_bg_opa(inner, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(inner, 0, 0);
    lv_obj_set_style_radius(inner, PAD_INSET, 0);
    lv_obj_set_style_pad_all(inner, 0, 0);
    lv_obj_set_scrollable(inner, false);
    lv_obj_set_clickable(inner, false);

    /* The guide: dotted, light on the dark inner. */
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
    lv_line_set_points(line, s_pad_guides[0], 2);

    lv_obj_add_event_cb(cell, pad_event_cb, LV_EVENT_ALL, NULL);
}

/* The bar calls this last in set_panel() and again whenever the bar's visibility changes:
 * after the carcase's default page ink, so the plate is put back white on both pages. A
 * swap also resets the press in flight before the old carcase goes. */
static void pad_shown(bool visible, void *ctx)
{
    (void)visible;
    (void)ctx;
    pad_reset(&s_pad_state);
    pad_ink(s_pad_cell);
}

/* The Pad is white on both pages, so a page flip while the Menu is up must not let the
 * bar's default dark fill through. The model drops every intent while the Menu is open
 * today, but the hook costs nothing and makes the panel self-contained. */
static void pad_page(bool white, void *ctx)
{
    (void)white;
    (void)ctx;
    pad_ink(s_pad_cell);
}

/* The Pad as the right bar's panel: one carcase at the layout's plate rect, white on both
 * pages (no page ink of its own), with the pad's press machine inside it. */
static const cassette_panel_t k_pad_panel = {
    .columns = 1,
    .rows = 1,
    .cell_rect = pad_cell_rect,
    .gesture_bubble = false,
    .build = pad_build,
    .set_page = pad_page,
    .shown = pad_shown,
    .ctx = NULL,
};

/* ------------------------------------------------------------------ lifecycle */

void cassette_menu_create(lv_obj_t *screen, lv_obj_t *cassette)
{
    if (!screen || s_model) return;

    s_cassette = cassette;
    pad_reset(&s_pad_state);

    cassette_rect_t page = cassette_page_rect();
    int rw = page.w - 2 * MENU_PAD;

    s_page = lv_obj_create(screen);
    lv_obj_remove_style_all(s_page);
    lv_obj_set_pos(s_page, page.x, page.y);
    lv_obj_set_size(s_page, page.w, page.h);
    lv_obj_set_style_bg_opa(s_page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_page, 0, 0);
    lv_obj_set_style_pad_all(s_page, MENU_PAD, 0);
    lv_obj_set_scrollable(s_page, false);
    lv_obj_set_clickable(s_page, false);

    for (int i = 0; i < MENU_ROWS; i++) {
        int y = cassette_page_slot_y(i);
        int h = cassette_page_slot_h(i);

        s_row[i] = lv_obj_create(s_page);
        lv_obj_remove_style_all(s_row[i]);
        lv_obj_set_pos(s_row[i], 0, y);
        lv_obj_set_size(s_row[i], rw, h);
        lv_obj_set_style_bg_opa(s_row[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_row[i], 0, 0);
        lv_obj_set_scrollable(s_row[i], false);
        lv_obj_set_clickable(s_row[i], false);

        s_band[i] = cassette_border_box(s_row[i], 2, 2, rw - 4, h - 4, CASSETTE_CORNER_R,
                                        k_col_bg, LV_OPA_COVER, k_col_bg, 0);
        lv_obj_set_hidden(s_band[i], true);

        s_icon[i] = cassette_text(s_row[i], &xtape_24, k_col_bg, LV_ALIGN_LEFT_MID, 16, 0);

        s_label[i] = cassette_text(s_row[i], &jura_28, k_col_bg, LV_ALIGN_LEFT_MID,
                                   MENU_ROW_LABEL_X, 0);
        lv_obj_set_width(s_label[i], MENU_ROW_LABEL_W);
        lv_label_set_long_mode(s_label[i], LV_LABEL_LONG_DOT);

        s_right[i] = cassette_text(s_row[i], &jura_28, k_col_bg, LV_ALIGN_RIGHT_MID,
                                   -MENU_ROW_RIGHT_INSET, 0);
        lv_obj_set_style_text_align(s_right[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_width(s_right[i], MENU_ROW_VALUE_W);
        lv_label_set_long_mode(s_right[i], LV_LABEL_LONG_DOT);

        s_dot[i] = cassette_box(s_row[i], MENU_ROW_DOT_X, (h - MENU_ROW_DOT_D) / 2,
                                MENU_ROW_DOT_D, MENU_ROW_DOT_D, MENU_ROW_DOT_D / 2, k_col_bg);
        lv_obj_set_hidden(s_dot[i], true);
    }
    lv_obj_set_hidden(s_page, true);

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

void cassette_menu_set_restore_cb(cassette_menu_cb_t cb, void *ctx)
{
    s_restore_cb = cb;
    s_restore_ctx = ctx;
}

void cassette_menu_open(bool white)
{
    if (!s_model || s_open) return;

    s_open = true;
    s_white = white;
    pad_reset(&s_pad_state);
    s_drawn = 0;
    mini_icecast_status(&s_status);
    s_lib = library_acquire();

    s_facts = build_facts();
    menu_open(s_model, s_lib, &s_facts);
    /* The status reaches the model before the first draw, so the Station values and the
     * playing Track's marker are right in the frame the Menu opens in. */
    menu_update(s_model, s_lib, &s_facts, &s_status);

    cassette_window_set_menu_page(true, white);
    /* The Pad takes the active group's place for the mode: the bar deletes the group's cells
     * and builds the plate. The group's values and its edit layer stay with it. */
    cassette_bars_set_panel(CASSETTE_BARS_RIGHT, &k_pad_panel);
    if (s_cassette) lv_obj_set_hidden(s_cassette, true);

    lv_obj_set_hidden(s_page, false);
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

    cassette_window_set_menu_page(false, s_white);
    cassette_header_set_title(NULL);
    cassette_footer_set_playing(NULL, NULL, NULL);
    /* Back to the group that was up: this module has no opinion about which one that is, so the
     * view's restore hook puts the right bar's panel back (the Empty grille, or a group with its
     * values and edit layer intact). It runs before the cassette returns, so the picture is
     * whole in the same frame. */
    if (s_restore_cb) s_restore_cb(s_restore_ctx);
    if (s_cassette) lv_obj_set_hidden(s_cassette, false);

    menu_close(s_model);
    if (s_lib) {
        library_release(s_lib);
        s_lib = NULL;
    }
    pad_reset(&s_pad_state);
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
