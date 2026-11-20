/*
 * The cassette screen's composition.
 *
 * The screen is one picture: a container under the screen root, the tape body inside
 * it, the parked VU meter in the right hub, and the peek and the chrome as the screen's
 * own children. The parts are private modules:
 *
 *   cassette_tapes    the packs, strands, rollers, run, hubs and the panel's frames
 *   cassette_meter    the VU meter, in the EQ group's VU cell
 *   cassette_cover    Real's Track picture, masked to the frames
 *   cassette_levels   the peek's host
 *   cassette_bars     the two edge bars: their cells, their entrance and their page ink
 *   cassette_buttons  the mode bar of the left bar: the Menu's door, the EQ and the RGB
 *   cassette_home     the right bar's Home group: its four keys
 *   cassette_eq       the right bar's EQ group: Back, the VU meter, Gain, HI/MID/LOW
 *   cassette_rgb      the right bar's RGB group: Back, Effect, Brightness, Red/Green/Blue
 *   knob_editor       the one 3x dial, overlay and edit session those two share
 *   cassette_header   the band that closes the controls screen across the top
 *   cassette_footer   and the one that closes it across the bottom
 *   cassette_window   the frame around the cassette between them
 *   screen_input      the touch policy, as intents
 *   cassette_layout   the design grid (portable, pinned by t16)
 *   cassette_style    the palette
 *   cassette_widgets  the object helpers
 *
 * What the screen should show is components/portable/cassette: this file steps the
 * model with the live stream's status, keeps the pose and wires the parts. It owns no
 * geometry of its own and no ladder state. The pose is a value every part receives on
 * its place() call, so a scale change is one animation over the whole picture.
 *
 * It also owns the page. The screen is four rungs (ADR 0025): the full cassette at Initial,
 * the chrome on the dark Page at Art, the chrome on the white Page with the Track's picture
 * printed on the cassette at Real - and the Peek below them all. The cassette's own page-ink
 * parts turn with the page, the picture cuts in over it, and every way off Real takes the
 * picture with it.
 *
 * Called under the LVGL lock, like everything else on the screen.
 */
#include <stdint.h>
#include <stdio.h>

#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

#include "cassette.h"
#include "cassette_widgets.h"
#include "cassette_bars.h"
#include "controls.h"
#include "groups.h"
#include "cassette_buttons.h"
#include "cassette_card.h"
#include "cassette_bench.h"
#include "cassette_home.h"
#include "cassette_eq.h"
#include "cassette_rgb.h"
#include "cassette_pad.h"
#include "knob_editor.h"
#include "cassette_footer.h"
#include "cassette_header.h"
#include "cassette_window.h"
#include "cassette_layout.h"
#include "cassette_levels.h"
#include "cassette_meter.h"
#include "cassette_style.h"
#include "cassette_tapes.h"
#include "cassette_view.h"
#include "mini_icecast.h"
#include "label_values.h"
#include "library.h"
#include "app_services.h"
#include "screen_input.h"

#include "cassette_cover.h"
#include "cassette_label.h"
#include "cassette_menu.h"

static const char *TAG = "screen";

/* The entry view is the untouched full-scale cassette. The mode bar asks for the
 * control pose: 134/256 puts the tape's 402 px box at 148..550, which clears the drawer's
 * 96 px column on the left and leaves room for the 190 px group block at 594..784 on the right.
 * view shows the block with the drawer, and the pose runs over the drawer's 260 ms slide.
 * The peek has its own 200/256 and -60 px lift; the drawer wins while it is out. */
#define CASSETTE_ROOM_MS       CASSETTE_BARS_SLIDE_MS

/* The white page's cassette is the Card: one tinted rectangle with a pill window, above
 * the tape and under the print (ADR 0039). cassette_card.c owns its geometry and the mask
 * it shares with the print; this file only shows it with the page and inks it per Track. */

static cassette_t *s_model;
static lv_obj_t *s_screen;     /* the root: its backdrop is the page */
static lv_obj_t *s_screen_back_cover; 
static lv_obj_t *s_cassette;   /* the container everything the body draws hangs off */
static cassette_pose_t s_pose = { CASSETTE_SCALE_ENTRY, 0, 0 };
static cassette_pose_t s_pose_from;   /* the pose the change in flight started at */
static cassette_pose_t s_pose_to;     /* and the one it is heading for */
static controls_t *s_controls;        /* the ladder: the level and the Menu */
static groups_t s_groups;             /* which of the right bar's panels is up */
static controls_state_t s_ctl;        /* its latest snapshot */
static bool s_painted_white;          /* the page the screen is painted for */
static bool s_page_painted;           /* the screen's backdrop has been set once */
static lv_color_t s_painted_page;     /* and the colour it was set to */
static cassette_ink_t s_row_ink;      /* the Track's cassette row, for the pose's restore */
static bool s_row_ink_valid;
static uint16_t s_cassette_row;       /* the row the track was given; picks the canvas */
#if CONFIG_APP_UI_RENDER_BENCH
static uint32_t s_bench_changed;      /* what the live bench's next frame will carry */
#endif

/* ------------------------------------------------------------------ layout */

static bool pose_is(cassette_pose_t a, cassette_pose_t b)
{
    return a.scale == b.scale && a.dx == b.dx && a.dy == b.dy;
}

static cassette_pose_t room_pose(controls_room_t room)
{
    if (room == CONTROLS_ROOM_CONTROLS) return cassette_pose_controls();
    if (room == CONTROLS_ROOM_PEEK) return cassette_pose_peek();
    return cassette_pose_entry();
}

/* The model names the room; the view animates to it. The bars, the bands and the window
 * run over the same 260 ms, so the picture arrives as one. */
static void room_apply(controls_room_t room)
{
    cassette_pose_t pose = room_pose(room);
    cassette_view_set_scale(pose.scale, pose.dx, pose.dy, CASSETTE_ROOM_MS);
}

/* The three facts the Controls cannot own: the edit session and the two settled poses. The
 * pose is the one on screen, so a swipe during a slide finds neither settled fact true. The
 * Menu is not one of them: the model owns it, and a close is an intent (menu_close_intent). */
static controls_facts_t facts_now(void)
{
    controls_facts_t facts;
    facts.editing = knob_editor_is_editing();
    facts.at_entry_pose = pose_is(s_pose, cassette_pose_entry());
    facts.at_controls_pose = pose_is(s_pose, cassette_pose_controls());
    return facts;
}

static controls_intent_t controls_intent(screen_intent_t intent)
{
    switch (intent) {
        case SCREEN_INTENT_DOWN: return CONTROLS_INTENT_DOWN;
        case SCREEN_INTENT_UP: return CONTROLS_INTENT_UP;
        case SCREEN_INTENT_FULL_ON: return CONTROLS_INTENT_FULL_ON;
        case SCREEN_INTENT_FULL_OFF: return CONTROLS_INTENT_FULL_OFF;
        default: return CONTROLS_INTENT_NONE;
    }
}

/* --------------------------------------------------------------- the page */

/* Art and Real are the controls' two faces: the same two bars and bands, on the dark
 * Page at Art and on the white one at Real, where the Track's picture is printed on the
 * cassette as well. The page is the model's wish (the level is Real); this file only paints
 * it, and the cover follows the same bit.
 *
 * The white page is the screen's whole backdrop. The line art keeps its inks on white, so the
 * picture reads as ink on paper. The flip is one frame - a colour animation would repaint the
 * whole 800x480 panel on every step - and it runs as the rung is taken, under the cover's own
 * one frame. The flag keeps a repeated request from invalidating the whole panel for a colour
 * that is already there. */

static bool label_canvas_rgb(lv_color_t *out);

/* The screen's backdrop is the page - or, at the two-finger pose, the label's own canvas.
 * Painted only when the colour changes: the pose's entry and a design change are the only
 * things that can move it, and a repeated paint would invalidate the whole panel. */
static void page_paint(void)
{
    if (!s_screen) return;
    lv_color_t colour = s_painted_white ? k_col_white : k_col_bg;
    lv_color_t canvas;
    if (label_canvas_rgb(&canvas)) colour = canvas;
    if (s_page_painted && lv_color_eq(colour, s_painted_page)) return;
    s_painted_page = colour;
    s_page_painted = true;
    lv_obj_set_style_bg_color(s_screen, colour, 0);
}

/* The canvas for this Track: the design's list cycled by the cassette row. Only the
 * two-finger pose shows it. */
static bool label_canvas_rgb(lv_color_t *out)
{
    uint32_t rgb;
    if (!s_ctl.full || !cassette_label_canvas(s_cassette_row, &rgb)) return false;
    *out = lv_color_hex(rgb);
    return true;
}

/* The cassette's body is the label's canvas while the two-finger pose is up: the Card's
 * plastic, its front plate, the window's rim and the print's fallback are one material, so
 * they move together. The print itself stays the design's paper. With no canvas, or the pose
 * down, the Track's row is back. */
static void card_inks_apply(void)
{
    if (!s_row_ink_valid) return;
    lv_color_t plastic = s_row_ink.plastic;
    lv_color_t canvas;
    if (label_canvas_rgb(&canvas)) plastic = canvas;
    cassette_card_set_ink(plastic);
    cassette_cover_set_inks(plastic, s_row_ink.ink, s_row_ink.label);
}

static void page_set(bool white)
{
    if (white == s_painted_white) return;
    s_painted_white = white;
    /* The Card is the page's body: it arrives with the flip, in one frame, and with it the
     * packs' clamp, so the reel tick never invalidates anything the Card would re-render. */
    cassette_card_set_shown(white);
    cassette_tapes_set_window(white ? cassette_card_window() : (cassette_rect_t){ 0, 0, 0, 0 });
    cassette_header_set_page(white);
    cassette_footer_set_page(white);
    cassette_bars_set_page(white);
    /* The tape's window chips are not repainted: they are windows on the background, and the
     * background behind them is k_col_bg on both pages - the dark screen at rest, the back
     * cover on the white page. The Cover is the model's wish, applied after this by COVER. */
}

/* The packed Label sheet, embedded at build time (components/app/screen_display/CMakeLists.txt,
 * ADR 0071): every design assets/labels/ holds, read in place by the label module. */
extern const uint8_t label_sheet_bin_start[] asm("_binary_label_sheet_bin_start");
extern const uint8_t label_sheet_bin_end[] asm("_binary_label_sheet_bin_end");

/* What the label was last printed for, so a Track change is the only thing that re-resolves
 * its row: the seed picks the design and the path picks the values, and both are compared. */
static uint32_t s_label_seed = UINT32_MAX;
static char     s_label_path[LIB_PATH_MAX];

/* The last frame the model produced, so a pose change can draw the tape once against the new
 * window: a tick redraws a hub only when the model moved it (cassette_view.c, apply_frame). */
static const cassette_frame_t *s_last_frame;

static bool chrome_shown_now(void);
static void groups_install(void);
static void groups_latch(void);
static void groups_apply(unsigned changed);

/* Apply only what the model says changed, in its order: PAGE, PEEK, SHOWN, MENU, ROOM. The
 * page is painted before the picture on the same bit - Real is the white Page and the Cover
 * as one rung, so the flip comes first and the picture cuts in over it; the peek is put away
 * before the chrome arrives; the menu opens with the chrome already in place; the room
 * starts the last animation. */
static void controls_apply(controls_changed_t changed)
{
    if (!changed || !s_controls) return;
    controls_snapshot(s_controls, &s_ctl);

    /* The white page is Real's, or the two-finger pose's; the Cover's picture is only the
     * Controls pose's. At the two-finger pose the print is the Label template, so asking the
     * artwork loader for the Track's k7label there would read the card for a picture the
     * screen never shows. */
    const bool white_page = s_ctl.level == CONTROLS_LEVEL_REAL || s_ctl.full;
    const bool cover = s_ctl.level == CONTROLS_LEVEL_REAL && !s_ctl.full;

    if (changed & CONTROLS_CHANGED_FULL) {
        /* The geometry first: page_set() clamps the packs to the Card's window, and the
         * window is the layout's. Both layouts land in one frame - no pose animation, because
         * both are the entry pose. */
        cassette_card_set_full(s_ctl.full);
        cassette_cover_set_full(s_ctl.full);
        cassette_tapes_set_preview(s_ctl.full);
        /* The design is the two-finger pose's: the print is the Cover's at the Controls pose,
         * and one frame after the layout changes the other one is up. */
        cassette_label_set_pose(s_ctl.full);
        /* FULL can change without PAGE: the level stays Real while the print switches from
         * the picture to the template, so the cover request has to follow it here. */
        cassette_levels_set_cover_visible(cover);
        /* The window just changed under the tape, and the next tick that draws a hub may be
         * tens of milliseconds away: draw the frame the model last produced against it now. */
        if (s_last_frame) cassette_tapes_set(s_last_frame);
    }

    if (changed & CONTROLS_CHANGED_PAGE) {
        page_set(white_page);
        cassette_cover_set_shown(white_page);
        /* The picture is the Controls pose's: ask for it now, not at boot, and not while
         * the two-finger pose's template is the print. */
        cassette_levels_set_cover_visible(cover);
    }

    /* The page colour is the page's, or the label's while the two-finger pose is up. Paint
     * it after both branches, so a PAGE and a FULL change land on one colour. The cassette's
     * body follows the same rule; it is what the viewer actually sees. */
    if (changed & (CONTROLS_CHANGED_FULL | CONTROLS_CHANGED_PAGE)) {
        page_paint();
        card_inks_apply();
    }

    if (changed & CONTROLS_CHANGED_PEEK) {
        cassette_levels_set_peek(s_ctl.level == CONTROLS_LEVEL_PEEK);
    }

    if (changed & CONTROLS_CHANGED_SHOWN) {
        bool shown = chrome_shown_now();
        cassette_header_set_shown(shown);
        cassette_footer_set_shown(shown);
        cassette_window_set_shown(shown);
        cassette_bars_set_shown(shown);
        /* The bar's cells are raised when they slide in, and its first one reaches 2 px past the
         * badge's bottom edge: the case's mark goes back on top. */
        cassette_header_raise_badge();
        /* The groups live with the chrome: hiding the bars drops the active group, so the
         * controls open on Home every time. */
        groups_apply(groups_handle(&s_groups, GROUPS_INTENT_NONE, shown));
    }

    if (changed & CONTROLS_CHANGED_MENU) {
        if (s_ctl.menu) {
            cassette_menu_open(s_ctl.level == CONTROLS_LEVEL_REAL);
        } else {
            cassette_menu_close();
            /* The Menu had taken the right bar for the Pad; the group that was up comes back on
             * the same bit that closed the page, values and edit layer intact. */
            groups_install();
        }
        groups_latch();
    }

    if (changed & CONTROLS_CHANGED_ROOM) room_apply(s_ctl.room);
}

/* Re-place every part at the current pose. The parts keep the pose themselves, so the
 * draw callbacks (the pack rings, the lattice) follow without a second pass. The peek's
 * two tethers both end on the left hub, so its screen points move with it: the group is
 * above the hub and meets the top rim, the song is below it and meets the bottom one. */
static void cassette_place(void)
{
    cassette_tapes_place(s_pose);
    /* The print does not follow the pose any more: its box is the Card's, fixed in screen
     * pixels, and it only ever shows at the Controls pose. It places itself when it shows. */
    /* The Peek's tethers end on the left hub's rim, and where that is, is arithmetic on the
     * pose - so the peek is told the pose, not where the tape happened to put the rim. */
    cassette_levels_set_pose(s_pose);
}

static void pose_anim_cb(void *var, int32_t step)
{
    (void)var;
    s_pose = cassette_pose_lerp(s_pose_from, s_pose_to, step);
    cassette_place();

}

void cassette_view_set_scale(int scale, int offset_x, int offset_y, uint32_t ms)
{
    if (!s_cassette) return;

    /* The layout clamps the scale. A change in flight is replaced, not stacked, and the
     * new one starts from the pose that is on screen now - the same way the backdrop
     * fade reverses. */
    cassette_pose_t target = cassette_pose(scale, offset_x, offset_y);
    bool was_animating = lv_anim_delete(s_cassette, pose_anim_cb);
    if (!was_animating && target.scale == s_pose.scale &&
        target.dx == s_pose.dx && target.dy == s_pose.dy) return;

    if (ms == 0) {
        s_pose = target;
        cassette_place();
        return;
    }

    s_pose_from = s_pose;
    s_pose_to = target;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_cassette);
    lv_anim_set_exec_cb(&a, pose_anim_cb);
    lv_anim_set_values(&a, 0, 1024);
    lv_anim_set_duration(&a, ms);
    /* The peek's bar and the mode bar drive this, so the ease is theirs: the
     * picture and the thing that asked for room arrive as one. */
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

/* ------------------------------------------------------------------ dim */

/* opa_layered, not opa: the style's opacity is applied to the object alone, while the
 * layered one renders the container and its children into a layer and applies the opacity
 * to the whole. The tapes and the meter are children, so this is what makes the cassette
 * disappear as one. Edit mode is a cut: no animation, one frame. */
void cassette_view_dim(bool dim)
{
    if (!s_cassette) return;
    lv_obj_set_style_opa_layered(s_cassette, dim ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
}

/* ------------------------------------------------------------------ frames */

/* Apply a model frame: each part writes only what changed. */
static void apply_frame(const cassette_frame_t *f)
{
    if (!f) return;
    /* The cassette follows the Track: its path picks a row of the table, and the row is the
     * plastic and the label's ink together (ADR 0040). Nothing is read off the picture. */
    if (f->ink_changed) {
#if CONFIG_APP_UI_RENDER_BENCH
        s_bench_changed |= CASSETTE_BENCH_CH_INK;
#endif
        const uint32_t n = (uint32_t)k_col_cassette_count;
        const uint32_t row = n ? (f->ink_seed % n) : 0;
        const cassette_ink_t *ink = &k_col_cassette[row];
        ESP_LOGI(TAG, "cassette %u of %u: %s", (unsigned)row, (unsigned)n,
                 f->title[0] ? f->title : "(no track)");
        s_cassette_row = (uint16_t)row;
        s_row_ink = *ink;
        s_row_ink_valid = true;
        /* The print is the same row's: the label fill, the ink it would print in, and the
         * window's rim in the row's plastic. The label's canvas, when it has one, takes the
         * body over while the two-finger pose is up. */
        card_inks_apply();
    }
    /* The hubs are white while the Cover is what the page shows: the spools read as plain
     * plastic and the artwork has the page's colour to itself. Asked every frame, because the
     * picture lands from the loader between frames; the setter ignores a repeated value. */
    cassette_tapes_set_hubs_white(cassette_cover_is_shown() && cassette_cover_has_picture());

    /* Every frame is offered, at both poses. The tapes early-out per hub: a tick whose hub
     * frame did not change draws nothing, and the model advances a hub about ten times a second
     * (20 frames a revolution, 1.5-3 s a revolution), so only ~10 of 25 passes a second
     * actually redraw. A slower tick here bought 9-15 ms against 11-21 and made the rotation
     * jump, which read as a different speed from the Controls pose's. */
    cassette_tapes_set(f);
    s_last_frame = f;

    /* The window's slice is a second copy of the tape, and LVGL cannot infer that a change
     * under the Card is a change there: the tapes hand back the boxes this frame moved, and
     * the cover invalidates exactly those. Invalidating the whole window instead rebuilt the
     * capsule's two ARGB corner layers and redrew ten rings twice on every 40 ms tick, which
     * starved the stream (2026-11-05). A spoke tick is a box a few dozen pixels across. */
    lv_area_t dirty[CASSETTE_TAPES_SLICE_DIRTY_MAX];
    int n = cassette_tapes_take_slice_dirty(dirty, CASSETTE_TAPES_SLICE_DIRTY_MAX);
    for (int i = 0; i < n; i++) cassette_cover_window_invalidate_area(&dirty[i]);

    /* The meter is only alive while the EQ group is up; its module ignores a frame otherwise. */
    cassette_meter_set(f);
    cassette_levels_apply(f);
}

/* ------------------------------------------------------------------ input */

/* ---------------------------------------------------------------- the groups */

static bool chrome_shown_now(void)
{
    return s_ctl.level == CONTROLS_LEVEL_ART || s_ctl.level == CONTROLS_LEVEL_REAL;
}

/* Put the active group's panel on the right bar. The Menu owns the bar while it is up - the
 * model says so - and the MENU bit that closes it brings the group back. */
static void groups_install(void)
{
    if (s_ctl.menu) return;
    const cassette_panel_t *panel = cassette_home_panel();
    if (groups_active(&s_groups) == GROUPS_ID_EQ) panel = cassette_eq_panel();
    else if (groups_active(&s_groups) == GROUPS_ID_RGB) panel = cassette_rgb_panel();
    cassette_bars_set_panel(CASSETTE_BARS_RIGHT, panel);
    cassette_header_raise_badge();
}

/* The latch on the left bar: the cassette lit while the Menu is open, the sliders or the led
 * lit while its group is the active one. A plain footlight on the mode that is on. Both
 * readings are the models': the Controls' menu and the groups' active group. */
static void groups_latch(void)
{
    cassette_buttons_set_on(CASSETTE_MODE_TAPE, s_ctl.menu);
    cassette_buttons_set_on(CASSETTE_MODE_EQ, groups_active(&s_groups) == GROUPS_ID_EQ);
    cassette_buttons_set_on(CASSETTE_MODE_RGB, groups_active(&s_groups) == GROUPS_ID_RGB);
}

/* Apply the model's mask: a moved group reinstalls the panel and is always relatched. */
static void groups_apply(unsigned changed)
{
    if (changed & GROUPS_CHANGED_ACTIVE) groups_install();
    groups_latch();
}

/* The left bar is the one piece of chrome a dial leaves a finger, so a tap there is how the
 * mode is put away: it ends the edit and runs nothing else (ADR 0034). The click is consumed -
 * a tap that dismisses the dial must not also open the Menu or drop the group on its way - and
 * the tap that follows is the mode's own. */
static bool left_bar_dismiss(void)
{
    if (!knob_editor_is_editing()) return false;
    knob_editor_close();
    return true;
}

/* The cassette's door: the Controls model owns the Menu's guards - the settled controls pose,
 * a knob not editing - and its open state, and its MENU_TAP toggles, so the same button closes
 * the Menu it opened. */
static void tape_cb(void *ctx)
{
    (void)ctx;
    if (left_bar_dismiss()) return;
    if (!s_controls) return;
    controls_facts_t facts = facts_now();
    controls_apply(controls_handle(s_controls, CONTROLS_INTENT_MENU_TAP, &facts));
}

static void eq_group_cb(void *ctx)
{
    (void)ctx;
    if (left_bar_dismiss()) return;
    groups_apply(groups_handle(&s_groups, GROUPS_INTENT_TAP_EQ, chrome_shown_now()));
}

static void rgb_group_cb(void *ctx)
{
    (void)ctx;
    if (left_bar_dismiss()) return;
    groups_apply(groups_handle(&s_groups, GROUPS_INTENT_TAP_RGB, chrome_shown_now()));
}

/* The Menu's close, through the model. Its own input asks for it - a row's Close, the Pad's
 * Back at the root, and the diagnostic scene driver walking home - and the request is an
 * intent like any other: one step, the MENU bit, and apply() closes the page and brings the
 * right bar's group and the latch back. Nothing here needs to know whether the Menu was open;
 * a close with no Menu is a step that changes nothing. */
static void menu_close_intent(void *ctx)
{
    (void)ctx;
    if (!s_controls) return;
    controls_facts_t facts = facts_now();
    controls_apply(controls_handle(s_controls, CONTROLS_INTENT_MENU_CLOSE, &facts));
}

/* A knob's editor borrows the right bar while the dial is up: the Pad takes the active
 * group's place, and the group comes back when the editor closes (ADR 0028). The editor never
 * sees a panel value - the bar and the groups belong to this file. */
static void knob_bar_claim(void *ctx)
{
    (void)ctx;
    cassette_bars_set_panel(CASSETTE_BARS_RIGHT, cassette_pad_panel());
}

static void knob_bar_release(void *ctx)
{
    (void)ctx;
    groups_install();
}

/* What a touch means is screen_input's; the Controls model says what it does, and this
 * applies only what changed. The edit and Menu gates are inside the model. */
static void screen_intent_cb(screen_intent_t intent, void *ctx)
{
    (void)ctx;
    if (!s_controls) return;
    controls_facts_t facts = facts_now();
    controls_apply(controls_handle(s_controls, controls_intent(intent), &facts));
}

/* The walk home, one step a call. The two modalities come first - a knob's edit owns the
 * touch, and the Menu is a page over the chrome - then the level: the Peek goes straight to
 * Initial, and Art and Real take the rung below, so Real needs two calls. A step is applied
 * through the same callbacks a finger's intent uses, which keeps the guards, the pages and
 * the room in one place. */
void cassette_view_home(void)
{
    if (!s_controls) return;

    controls_facts_t facts = facts_now();
    if (facts.editing) {
        knob_editor_close();
        return;
    }
    if (s_ctl.menu) {
        menu_close_intent(NULL);                       /* closes the Menu and nothing else */
        return;
    }
    if (s_ctl.level == CONTROLS_LEVEL_PEEK) {
        screen_intent_cb(SCREEN_INTENT_DOWN, NULL);    /* Peek -> Initial */
        return;
    }
    if (s_ctl.level != CONTROLS_LEVEL_INITIAL)
        screen_intent_cb(SCREEN_INTENT_UP, NULL);      /* Real -> Art -> Initial */
}

/* --------------------------------------------------------------- the dump scene */

/* A diagnostic build's scene driver: it walks the ladder and picks a group (or the Menu) with
 * the intents a finger would send, so the captured picture is one of the new chrome instead of
 * the entry view. It steps only from poses that have settled, which is what the ladder's own
 * gates ask for.
 *
 * The scene is a **target**, not a fixed step. CONFIG_APP_UI_DUMP_SCENE is where the device
 * starts, and with CONFIG_APP_UI_SCREEN_HTTP a POST to /api/scene re-aims it while the device
 * runs, so one build photographs every scene. A scene set while the chrome is up walks home
 * to the entry view first: the ladder only goes forward from a settled pose, and reaching
 * Initial by luck would photograph whatever the previous scene left.
 *
 * CONFIG_APP_UI_SERIAL_DUMP || CONFIG_APP_UI_SCREEN_HTTP; zero in every normal build. */
#if CONFIG_APP_UI_SERIAL_DUMP || CONFIG_APP_UI_SCREEN_HTTP
#if CONFIG_APP_UI_DUMP_SCENE > 0
#define SCENE_BOOT CONFIG_APP_UI_DUMP_SCENE
#else
#define SCENE_BOOT 0
#endif

/* The step a scene's script stops at once the picture is the one it asked for. */
#define SCENE_DONE 99

static int s_scene = SCENE_BOOT;   /* where the picture should end up */
static volatile int s_scene_step;          /* progress through that scene's script */
static volatile bool s_scene_returning;    /* walking back to the entry view first */

void cassette_view_scene_set(int scene)
{
    if (scene < 0 || scene > CASSETTE_SCENE_MAX) return;
    s_scene = scene;
    s_scene_step = 0;
    s_scene_returning = true;
    ESP_LOGI(TAG, "scene -> %d", scene);
}

bool cassette_view_scene_ready(void)
{
    /* Read from the HTTP task, written by the screen task. Two aligned words, one writer:
     * the same arrangement the model's flags use. */
    return !s_scene_returning && s_scene_step >= SCENE_DONE;
}

/* One intent a tick, and the mode decides which one: the Menu closes on its close intent, a
 * knob's dial on its own close, and the level is a reading, so it says which rung to take next.
 * The walk itself is the screen's one `cassette_view_home()`, the same call the cardless screen
 * makes; this only answers whether it has arrived. */
static bool scene_return_to_initial(void)
{
    cassette_view_home();
    return !facts_now().editing && !s_ctl.menu && s_ctl.level == CONTROLS_LEVEL_INITIAL;
}

static void scene_tick(void)
{
    if (!s_controls) return;

    if (s_scene_returning) {
        if (!scene_return_to_initial()) return;
        s_scene_returning = false;
        s_scene_step = 0;
    }

    switch (s_scene_step) {
    case 0:
        if (!pose_is(s_pose, cassette_pose_entry())) break;
        if (s_scene == 0) {
            s_scene_step = SCENE_DONE;                  /* the entry view: nothing to add */
            break;
        }
        if (s_scene == 8) {
            /* The Peek: the one rung that is not the chrome, raised from the entry pose. It
             * is the only way to photograph the two tethers, whose lower ends are the hub's
             * rim. Its own step waits for the pose, so `ready` means the bar is up. */
            s_scene_step = 4;
            screen_intent_cb(SCREEN_INTENT_UP, NULL);    /* Initial -> Peek */
            break;
        }
        if (s_scene == 10) {
            /* The two-finger pose: Initial plus the Card, the label and the tape's preview
             * inks. There is no other way to photograph its window without a finger. */
            s_scene_step = 5;
            screen_intent_cb(SCREEN_INTENT_FULL_ON, NULL);
            break;
        }
        s_scene_step = 1;
        screen_intent_cb(SCREEN_INTENT_DOWN, NULL);      /* Initial -> Art */
        break;

    case 1:
        if (!pose_is(s_pose, cassette_pose_controls())) break;
        s_scene_step = 2;
        if (s_scene == 1) break;                        /* Art, the Home keys */
        if (s_scene == 2) eq_group_cb(NULL);
        else if (s_scene == 3) rgb_group_cb(NULL);
        else if (s_scene == 4) tape_cb(NULL);
        else if (s_scene == 5) eq_group_cb(NULL);
        else if (s_scene == 6) eq_group_cb(NULL);
        else if (s_scene == 7) rgb_group_cb(NULL);
        else if (s_scene == 9) rgb_group_cb(NULL);
        break;

    case 2:
        /* Real/EQ and Real/RGB: one more rung, once the group is installed. */
        if (s_scene == 5 || s_scene == 9) {
            if (s_ctl.level != CONTROLS_LEVEL_ART) break;
            s_scene_step = 3;
            screen_intent_cb(SCREEN_INTENT_DOWN, NULL);  /* Art -> Real */
            break;
        }

        /* The dial scenes. The 3x dial is a tap away and a capture has no finger, so the
         * scene arms the knob's delayed open itself - but only once the bar has settled,
         * because the open is refused while it is sliding and a refused arm never lands.
         * That is a real 20 ms race: the pose settles a tick or two before the slide ends,
         * and the tap used to fire on the earlier of the two. Step 3 waits for the open. */
        if (s_scene == 6 || s_scene == 7) {
            if (knob_editor_is_editing()) {
                s_scene_step = SCENE_DONE;
                break;
            }
            if (cassette_bars_is_sliding()) break;
            knob_editor_tap(s_scene == 6 ? 0 : 4);       /* the EQ's gain; the RGB effect */
            s_scene_step = 3;
            break;
        }

        /* The chrome scenes: the picture is what was asked for once the bars have stopped
         * sliding and, for the Menu, once it is open. The `ready` answer waits on this. */
        if (cassette_bars_is_sliding()) break;
        if (s_scene == 4 && !cassette_menu_is_open()) break;
        s_scene_step = SCENE_DONE;
        break;

    case 3:
        if (s_scene == 5 || s_scene == 9) {
            if (s_ctl.level != CONTROLS_LEVEL_REAL) break;
            s_scene_step = SCENE_DONE;
            break;
        }
        if (knob_editor_is_editing()) s_scene_step = SCENE_DONE;
        break;

    case 4:
        if (!pose_is(s_pose, cassette_pose_peek())) break;   /* the Peek's slide has landed */
        s_scene_step = SCENE_DONE;
        break;

    case 5:
        if (s_ctl.full) s_scene_step = SCENE_DONE;
        break;

    default:
        break;
    }
}
#endif

/* ------------------------------------------------------------------- build */

void cassette_view_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, k_col_bg, 0);
    lv_obj_set_scrollable(scr, false);
    s_screen = scr;
    s_painted_page = k_col_bg;
    s_page_painted = true;

    s_controls = controls_new();
    if (!s_controls) ESP_LOGW(TAG, "no model for the Controls: the swipes do nothing");
    else controls_snapshot(s_controls, &s_ctl);

    cassette_rect_t back = cassette_part_rect(CASSETTE_PART_BACK);
    s_screen_back_cover = cassette_box(scr, back.x, back.y, back.w, back.h, 0, k_col_bg);

    /* The window is the frame the tape is drawn in, so it is the screen's first child: the
     * cassette's picture passes over it - while the pose shrinks the tape into the window,
     * the tape covers the border and reveals it as it goes - and nothing in the window can
     * ever paint over the cassette. */
    cassette_window_create(scr);


    /* The cassette's own container: the body's objects are its children, not the
     * screen's, so the picture is one thing. It is full-bleed at 0,0 and has neither
     * fill nor border, which keeps every position in the parts a screen coordinate and
     * lets the screen's own backdrop show through. It takes no press, and neither does
     * anything under it: every object is decorative at construction. */
    s_cassette = lv_obj_create(scr);
    lv_obj_remove_style_all(s_cassette);
    lv_obj_set_pos(s_cassette, 0, 0);
    lv_obj_set_size(s_cassette, CASSETTE_DESIGN_W, CASSETTE_DESIGN_H);
    lv_obj_set_style_pad_all(s_cassette, 0, 0);
    lv_obj_set_style_bg_opa(s_cassette, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_cassette, 0, 0);
    lv_obj_set_scrollable(s_cassette, false);
    lv_obj_set_clickable(s_cassette, false);

    /* The stack, bottom to top: the tape body, the Card, the print, the porthole, the window's
     * rim. Each is created after the one below it, so nothing is raised at runtime. The peek
     * and the two bars are screen children above the cassette. */
    cassette_tapes_create(s_cassette);

    cassette_card_create(s_cassette);

    /* The cover owns the print, the window's porthole and the rim around it - all above the
     * Card. The porthole paints the tape slice, and the drawer is the tapes': the cover owns
     * the object, the tapes own the paint. */
    cassette_cover_create(s_cassette);
    /* The label prints inside the Cover's print, above the picture: same box, same mask, same
     * clip. It is created after the cover so it draws over the picture the pose hides. */
    cassette_label_create(cassette_cover_print(), label_sheet_bin_start,
                          (size_t)(label_sheet_bin_end - label_sheet_bin_start));
    cassette_cover_set_window_draw(cassette_tapes_draw_slice);
    cassette_place();

    /* The peek is the screen's child, above the cassette's container. It is the only
     * overlay left: the Label cover and Artwork sheets are gone. */
    cassette_levels_create(scr);
    /* The peek exists now, so give it the pose the tethers hang from. */
    cassette_levels_set_pose(s_pose);

    /* The bars own the cells, and nothing is in them until the composition installs a panel -
     * below, once every panel exists. The bars start off screen and hidden. */
    cassette_bars_create(scr);

    /* The mode bar is chrome, above the peek: the Menu's door and the two group selectors. It has
     * no cell until install_panels() hands the left bar its value. */
    cassette_buttons_set_cb(CASSETTE_MODE_TAPE, tape_cb, NULL);
    cassette_buttons_set_cb(CASSETTE_MODE_EQ, eq_group_cb, NULL);
    cassette_buttons_set_cb(CASSETTE_MODE_RGB, rgb_group_cb, NULL);

    /* The one 3x dial and its overlay, shared by the right bar's knob panels (the EQ group
     * today, the RGB group next). It is a screen child above everything the panels build. */
    knob_editor_create(scr);
    knob_editor_set_bar(knob_bar_claim, knob_bar_release, NULL);
    knob_editor_set_dim(cassette_view_dim);

    /* The three groups' panel values. They build no object; their cells are the bar's, built
     * when a value is installed. */
    cassette_eq_create();
    cassette_rgb_create();
    cassette_home_create();

    /* The groups live with the chrome: nothing is selected until the bars are out, and the
     * two latches start dark. */
    groups_init(&s_groups);

    /* Build, then install: every module above has built what it owns, and these three calls are
     * the only places a bar is given content. Left first, then the right bar's default group -
     * the Home keys - and the latches, which read that group. */
    cassette_bars_set_panel(CASSETTE_BARS_LEFT, cassette_buttons_panel());
    groups_install();
    groups_latch();

    /* The Menu's page is drawn above the cassette; its Pad is the right bar's cell, built by
     * the bar when open installs the panel. The bars were created before this; the bands come
     * after, so the frame stays above the rows. M - the right bar's chrome cell - opens it,
     * gated on the controls' settled pose by the model. */
    cassette_menu_create(scr, s_cassette);
    cassette_menu_set_close_cb(menu_close_intent, NULL);

    /* The bands across the top and the bottom of the controls screen: the frame that closes
     * the drawer and the block on their top and bottom lines. They are chrome, hidden with
     * them. The window is not here: it is the screen's first child, under the cassette. */
    cassette_header_create(scr);
    cassette_footer_create(scr);

    /* The model: the tapes' frame count is the angular resolution; the geometry
     * supplies the speeds. */
    s_model = cassette_new(CASSETTE_TAPES_FRAME_COUNT, CASSETTE_DEFAULT_BAR_MS);
    if (!s_model) ESP_LOGW(TAG, "no model for the cassette: the screen will not update");
    else cassette_reset(s_model);

    /* Draw the empty screen now; the first status update replaces it. */
    if (s_model) apply_frame(cassette_step(s_model, NULL, lv_tick_get()));

    /* The picture is built. Make the screen the one thing a finger can hit, then let
     * screen_input decide what a touch means: a down swipe takes the screen one rung towards
     * Real (Initial -> Art -> Real), an up swipe one rung back towards the Peek, and the
     * ends stop. A tap is not an action; next/previous belong to the block's arrows. */
    lv_obj_set_clickable(scr, true);
    screen_input_attach(scr, screen_intent_cb, NULL);
}

/* ------------------------------------------------------------------ update */

void cassette_view_update(const mini_icecast_status_t *st)
{
    /* The render bench owns the screen while it runs: a station update would invalidate
     * the title, the footer and the meter between its samples. */
    if (cassette_bench_frozen()) return;
    if (!s_model) return;
#if CONFIG_APP_UI_SERIAL_DUMP || CONFIG_APP_UI_SCREEN_HTTP
    scene_tick();
#endif
    const cassette_frame_t *f = cassette_step(s_model, st, lv_tick_get());
    apply_frame(f);

    /* The design and its values follow the Track: the seed picks the design and the path picks
     * the Snapshot row, and a Track change is the only thing that moves either. */
    if (f) {
        const char *path = st ? st->path : "";
        if (f->ink_seed != s_label_seed || strcmp(path, s_label_path) != 0) {
            s_label_seed = f->ink_seed;
            snprintf(s_label_path, sizeof s_label_path, "%s", path);

            const library_t *lib = library_acquire();
            const snapshot_t *snap = app_services_snapshot();
            label_values_t values;
            label_values_fill(&values, lib, snap, path);
            /* The slots are joined on the path and the file's size, so an empty design is worth
             * one line: it says which half of the join missed (label-templates.md 5). */
            if (path[0] && !label_values_get(&values, "song_title")[0] &&
                !label_values_get(&values, "song_artist")[0]) {
                lib_node_t n = lib ? library_find(lib, path) : LIB_NODE_NONE;
                ESP_LOGW(TAG, "no label values for %s: library %s, size %u, snapshot %s",
                         path, n == LIB_NODE_NONE ? "misses it" : "has it",
                         n == LIB_NODE_NONE ? 0u : (unsigned)library_size(lib, n),
                         snap ? "open" : "none");
            }
            if (lib) library_release(lib);
            cassette_label_update(f->ink_seed, &values);
            /* A new design can carry a different canvas: the page and the cassette's body
             * follow it while the two-finger pose is up. */
            if (s_ctl.full) {
                page_paint();
                card_inks_apply();
            }
#if CONFIG_APP_UI_RENDER_BENCH
            s_bench_changed |= CASSETTE_BENCH_CH_LABEL;
#endif
        }
    }

    /* The peek's host keeps the status for the tag request and makes it itself; this
     * is a string compare on the frames that do not change it. */
    cassette_levels_update(st);

    /* The bands' timeline is the Peek's bar and it is up whenever the chrome is, so it follows
     * the broadcast on both pages and inside the Menu alike. */
    cassette_footer_set_playing(st && st->format != MI_FMT_NONE,
                                st ? st->elapsed_ms : 0u, st ? st->duration_ms : 0u,
                                st ? st->title : NULL, st ? st->path : NULL);
    /* The Menu's rows and its playing marker follow the same status. The Menu's own state is
     * the model's, but this is a drawing call: the overlay draws only while it is up. */
    if (cassette_menu_is_open()) cassette_menu_update(st);

    /* No polling step here: the model changes only when an intent arrives, and every intent is
     * applied by the callback that sent it. The 40 ms CONTROLS_INTENT_NONE that used to run here
     * existed to adopt the Menu's self-close, and there is no self-close now. */

#if CONFIG_APP_UI_SERIAL_DUMP
    /* A still image cannot show that the hubs turn, so a diagnostic build says it. */
    static uint32_t last_log;
    uint32_t now = lv_tick_get();
    if (f && now - last_log > 1000) {
        last_log = now;
        /* The reel change lasts 8 s, so a one-second log catches most of its frames -
         * enough to tune the spin and the ease against real frame costs. */
        ESP_LOGI(TAG, "hubs %u/%u and %u/%u, fills %u/%u, %u permille%s%u",
                 (unsigned)f->hub_frame[0], CASSETTE_TAPES_FRAME_COUNT,
                 (unsigned)f->hub_frame[1], CASSETTE_TAPES_FRAME_COUNT,
                 (unsigned)f->pack_fill[0], (unsigned)f->pack_fill[1],
                 (unsigned)f->permille,
                 f->rewinding ? ", rewinding " : "",
                 f->rewinding ? (unsigned)f->rewind_permille : 0u);
    }
#endif
}

/* ------------------------------------------------------------------ bench */

#if CONFIG_APP_UI_RENDER_BENCH
/* The view's half of the render bench (cassette_bench.h): the poses, the two real
 * animation steps, and the boxes the composition knows. Nothing here runs in a normal
 * build. */

static volatile bool s_bench_frozen;
static cassette_frame_t s_bench_frame;
static bool s_bench_frame_ok;

void cassette_bench_freeze(bool on)
{
    s_bench_frozen = on;
}

bool cassette_bench_frozen(void)
{
    return s_bench_frozen;
}

void cassette_bench_pose(int pose)
{
    screen_intent_cb(pose ? SCREEN_INTENT_FULL_ON : SCREEN_INTENT_FULL_OFF, NULL);
#if !CONFIG_APP_UI_RENDER_BENCH_LIVE
    /* The label is blank when the card is out, and a blank field draws almost nothing: the
     * sweep's fixture puts a title in every slot so the label is the one a Track would print.
     * The live bench leaves the real Track's label alone. */
    if (pose) cassette_label_bench_fill();
#endif
    if (!pose) {
        cassette_tapes_bench_annulus(true);
        cassette_tapes_bench_pack_body(true);
    }
}

void cassette_bench_annulus(bool on)
{
    cassette_tapes_bench_annulus(on);
}

/* What the frame the live bench just measured carried. The tape body wrote its two bits as
 * it mutated; the ink and the label were written by the view. */
uint32_t cassette_bench_take_changes(void)
{
    uint32_t m = s_bench_changed;
    s_bench_changed = 0;
    const uint8_t d = cassette_tapes_bench_take_dirty();
    if (d & 1) m |= CASSETTE_BENCH_CH_HUB;
    if (d & 2) m |= CASSETTE_BENCH_CH_FILL;
    return m;
}

/* The window's slice is a second reading of the tape, and it is above the Card: the
 * bench steps a change below it exactly as apply_frame() does, then hands the boxes the
 * slice has to repaint to the cover. */
static void bench_slice_flush(void)
{
    lv_area_t dirty[CASSETTE_TAPES_SLICE_DIRTY_MAX];
    int n = cassette_tapes_take_slice_dirty(dirty, CASSETTE_TAPES_SLICE_DIRTY_MAX);
    for (int i = 0; i < n; i++) cassette_cover_window_invalidate_area(&dirty[i]);
}

/* The real tape step: both packs change diameter by 40 permille, alternating around the
 * fill the screen held when the bench froze, which the model's own pack_diameter() turns
 * into a ring change. The strands follow the new radius, exactly as playback's do. */
static bool bench_fill_tick(void)
{
    if (!s_bench_frame_ok) {
        if (!s_last_frame) return false;
        s_bench_frame = *s_last_frame;
        s_bench_frame_ok = true;
    }
    for (int i = 0; i < 2; i++) {
        uint16_t fill = s_bench_frame.pack_fill[i];
        s_bench_frame.pack_fill[i] = (uint16_t)(fill > 500 ? fill - 40 : fill + 40);
    }
    cassette_tapes_set(&s_bench_frame);
    return true;
}

static void bench_rect(cassette_bench_rect_t *r, int x, int y, int w, int h)
{
    r->x1 = x;
    r->y1 = y;
    r->x2 = x + w - 1;
    r->y2 = y + h - 1;
}

int cassette_bench_step(cassette_bench_step_t step, cassette_bench_rect_t *rects, int max)
{
    if (!rects || max < 1) return -1;

    /* The two A/B steps are the only ones that leave the preview's wound body out; every
     * other step puts it back, and the setter repaints the packs only when it changes. */
    cassette_tapes_bench_pack_body(step != CASSETTE_BENCH_TAPE_FILL_BORDER &&
                                   step != CASSETTE_BENCH_PACK_BORDER);

    lv_area_t a;
    cassette_rect_t tape;

    switch (step) {
    case CASSETTE_BENCH_HUB_TICK:
        cassette_tapes_bench_hub_tick();
        bench_slice_flush();
        return 0;

    case CASSETTE_BENCH_TAPE_FILL:
    case CASSETTE_BENCH_TAPE_FILL_BORDER:
        if (!bench_fill_tick()) return -1;
        bench_slice_flush();
        return 0;

    case CASSETTE_BENCH_HUB_RIM_LEFT:
    case CASSETTE_BENCH_HUB_RIM_RIGHT: {
        const int i = step == CASSETTE_BENCH_HUB_RIM_RIGHT ? 1 : 0;
        const int d = cassette_len(s_pose, CASSETTE_HUB_RING_D);
        const int cx = cassette_x(s_pose, i ? CASSETTE_HUB_X1 : CASSETTE_HUB_X0);
        const int cy = cassette_y(s_pose, CASSETTE_HUB_Y);
        bench_rect(&rects[0], cx - d / 2 - 2, cy - d / 2 - 2, d + 4, d + 4);
        return 1;
    }

    case CASSETTE_BENCH_PACK_LEFT:
    case CASSETTE_BENCH_PACK_RIGHT:
        if (!cassette_tapes_pack_box(step == CASSETTE_BENCH_PACK_RIGHT, &a)) return -1;
        rects[0] = (cassette_bench_rect_t){ a.x1, a.y1, a.x2, a.y2 };
        return 1;

    case CASSETTE_BENCH_PACK_BORDER:
        if (!s_ctl.full) return -1;
        if (!cassette_tapes_pack_box(0, &a)) return -1;
        rects[0] = (cassette_bench_rect_t){ a.x1, a.y1, a.x2, a.y2 };
        return 1;

    case CASSETTE_BENCH_STRAND_LEFT:
    case CASSETTE_BENCH_STRAND_RIGHT:
        if (!cassette_tapes_strand_box(step == CASSETTE_BENCH_STRAND_RIGHT, &a)) return -1;
        rects[0] = (cassette_bench_rect_t){ a.x1, a.y1, a.x2, a.y2 };
        return 1;

    case CASSETTE_BENCH_LABEL:
        /* The print is the label's container, and it is the two-finger pose's alone: at
         * Initial the whole print is hidden, and a rect there would only measure the tape. */
        if (!s_ctl.full || !cassette_cover_print()) return -1;
        lv_obj_get_coords(cassette_cover_print(), &a);
        rects[0] = (cassette_bench_rect_t){ a.x1, a.y1, a.x2, a.y2 };
        return 1;

    case CASSETTE_BENCH_LABEL_TEXT:
        if (!s_ctl.full) return -1;
        cassette_label_bench_texts();
        return 0;

    case CASSETTE_BENCH_CARD: {
        if (!s_ctl.full) return -1;
        const cassette_card_geometry_t *g = cassette_card_geometry();
        if (!g || g->w <= 0) return -1;
        bench_rect(&rects[0], g->x, g->y, g->w, g->h);
        return 1;
    }

    case CASSETTE_BENCH_WINDOW:
        if (!s_ctl.full) return -1;
        tape = cassette_card_window();
        if (tape.w <= 0 || tape.h <= 0) return -1;
        bench_rect(&rects[0], tape.x, tape.y, tape.w, tape.h);
        return 1;

    case CASSETTE_BENCH_TAPE_BODY:
        tape = cassette_screen_rect(s_pose, cassette_tape_ink());
        if (tape.w <= 0 || tape.h <= 0) return -1;
        bench_rect(&rects[0], tape.x, tape.y, tape.w, tape.h);
        return 1;

    case CASSETTE_BENCH_SCREEN:
        bench_rect(&rects[0], 0, 0, CASSETTE_DESIGN_W, CASSETTE_DESIGN_H);
        return 1;

    default:
        return -1;
    }
}

const char *cassette_bench_step_name(cassette_bench_step_t step)
{
    static const char *const k_name[CASSETTE_BENCH_STEP_COUNT] = {
        "hub-tick", "tape-fill", "tape-fill-border", "hub-rim-left", "hub-rim-right",
        "pack-left", "pack-right", "pack-border", "strand-left", "strand-right", "label",
        "label-text", "card", "window", "tape-body", "screen",
    };
    if (step < 0 || step >= CASSETTE_BENCH_STEP_COUNT) return "?";
    return k_name[step];
}
#endif /* CONFIG_APP_UI_RENDER_BENCH */

/* ------------------------------------------------------------------ hand-off */
