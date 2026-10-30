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
 *   cassette_empty    the right bar's Empty group: the speaker grille
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

#include "esp_log.h"
#include "lvgl.h"

#include "cassette.h"
#include "cassette_widgets.h"
#include "cassette_bars.h"
#include "controls.h"
#include "groups.h"
#include "cassette_buttons.h"
#include "cassette_card.h"
#include "cassette_empty.h"
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
#include "screen_input.h"

#include "cassette_cover.h"
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

static void page_set(bool white)
{
    if (white == s_painted_white) return;
    s_painted_white = white;
    if (s_screen) lv_obj_set_style_bg_color(s_screen, white ? k_col_white : k_col_bg, 0);
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

    if (changed & CONTROLS_CHANGED_PAGE) {
        const bool real = s_ctl.level == CONTROLS_LEVEL_REAL;
        page_set(real);
        cassette_cover_set_shown(real);
        /* The picture is Real's: ask for it now, not at boot. */
        cassette_levels_set_cover_visible(real);
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
         * controls open on the grille every time. */
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
        const uint32_t n = (uint32_t)k_col_cassette_count;
        const uint32_t row = n ? (f->ink_seed % n) : 0;
        const cassette_ink_t *ink = &k_col_cassette[row];
        ESP_LOGI(TAG, "cassette %u of %u: %s", (unsigned)row, (unsigned)n,
                 f->title[0] ? f->title : "(no track)");
        cassette_card_set_ink(ink->plastic);
        /* The print is the same row's: the label fill, the ink it would print in, and the
         * window's rim in the row's plastic. */
        cassette_cover_set_inks(ink->plastic, ink->ink, ink->label);
    }
    /* The hubs are white while the Cover is what the page shows: the spools read as plain
     * plastic and the artwork has the page's colour to itself. Asked every frame, because the
     * picture lands from the loader between frames; the setter ignores a repeated value. */
    cassette_tapes_set_hubs_white(cassette_cover_is_shown() && cassette_cover_has_picture());
    cassette_tapes_set(f);
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
    const cassette_panel_t *panel = cassette_empty_panel();
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
 * knob's dial on its own close, and the level is a reading, so it says which rung to take next. */
static bool scene_return_to_initial(void)
{
    controls_facts_t facts = facts_now();
    if (facts.editing) {
        knob_editor_close();
        return false;
    }
    if (s_ctl.menu) {
        menu_close_intent(NULL);                       /* closes the Menu and nothing else */
        return false;
    }
    if (s_ctl.level == CONTROLS_LEVEL_PEEK) {
        screen_intent_cb(SCREEN_INTENT_DOWN, NULL);     /* Peek -> Initial */
        return false;
    }
    if (s_ctl.level != CONTROLS_LEVEL_INITIAL) {
        screen_intent_cb(SCREEN_INTENT_UP, NULL);       /* Real -> Art -> Initial */
        return false;
    }
    return true;
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
        s_scene_step = 1;
        screen_intent_cb(SCREEN_INTENT_DOWN, NULL);      /* Initial -> Art */
        break;

    case 1:
        if (!pose_is(s_pose, cassette_pose_controls())) break;
        s_scene_step = 2;
        if (s_scene == 1) break;                        /* Art, the Empty grille */
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

    /* The stack, bottom to top: the tape body, the Card, the print, the window's rim. Each is
     * created after the one below it, so nothing is raised at runtime. The peek and the two
     * bars are screen children above the cassette. */
    cassette_tapes_create(s_cassette);

    cassette_card_create(s_cassette);

    /* The cover owns the print and the rim around its window, and both go above the Card. */
    cassette_cover_create(s_cassette);
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

    /* The two knob panels' values. They build no object; their cells are the bar's, built when
     * their value is installed. */
    cassette_eq_create();
    cassette_rgb_create();

    /* The groups live with the chrome: nothing is selected until the bars are out, and the
     * two latches start dark. */
    groups_init(&s_groups);

    /* Build, then install: every module above has built what it owns, and these three calls are
     * the only places a bar is given content. Left first, then the right bar's default group -
     * the Empty grille - and the latches, which read that group. */
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
    if (!s_model) return;
#if CONFIG_APP_UI_SERIAL_DUMP || CONFIG_APP_UI_SCREEN_HTTP
    scene_tick();
#endif
    const cassette_frame_t *f = cassette_step(s_model, st, lv_tick_get());
    apply_frame(f);

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

/* ------------------------------------------------------------------ hand-off */

void cassette_view_set_cover(const lv_image_dsc_t *dsc)
{
    cassette_cover_set_image(dsc);
}

void cassette_view_set_tags(const char *group, const char *song)
{
    cassette_levels_set_tags(group, song);
}
