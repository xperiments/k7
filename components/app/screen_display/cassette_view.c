/*
 * The cassette screen's composition.
 *
 * The screen is one picture: a container under the screen root, the tape body inside
 * it, the VU meter in the right hub, and the Level sheets as the container's siblings.
 * The parts are private modules:
 *
 *   cassette_tapes    the packs, strands, rollers, run, hubs and the panel's frames
 *   cassette_meter    the VU meter
 *   cassette_cover    the white page's Track picture, masked to the frames
 *   cassette_levels   the peek's host
 *   cassette_buttons  the three-button transport drawer at the left edge
 *   cassette_eq       the DJ block at the right edge: GAIN, HI/MED/LOW and the < / > arrows
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
 * geometry of its own and no Level state. The pose is a value every part receives on
 * its place() call, so a scale change is one animation over the whole picture.
 *
 * It also owns the page. The cassette sits on the dark screen at rest; the down swipe
 * shows the controls on a white page, and the horizontal swipes keep showing the same
 * controls on the dark one. The page follows the gesture that opened the controls, and
 * the cassette's own page-ink parts turn with it. Once the page is white, the next down
 * swipe turns the Track's picture on the cassette's body; the up swipe turns it off again,
 * and every way off the page takes it with them.
 *
 * Called under the LVGL lock, like everything else on the screen.
 */
#include <stdint.h>

#include "esp_log.h"
#include "lvgl.h"

#include "cassette.h"
#include "cassette_widgets.h"
#include "cassette_buttons.h"
#include "cassette_eq.h"
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

#if CONFIG_APP_BOARD_SUNTON_8048S050C
#include "cassette_cover.h"
#endif

static const char *TAG = "screen";

/* The entry view is the untouched full-scale cassette. The transport drawer asks for the
 * control pose: 134/256 puts the tape's 402 px box at 148..550, which clears the 120 px
 * drawer on the left and leaves room for the 192 px DJ block at 592..784 on the right. The
 * view shows the block with the drawer, and the pose runs over the drawer's 260 ms slide.
 * The peek has its own 200/256 and -60 px lift; the drawer wins while it is out. */
#define CASSETTE_SCALE_LABEL   200
#define CASSETTE_SCALE_BUTTONS 134
#define CASSETTE_BUTTONS_DX    (-51)
#define CASSETTE_BUTTONS_DY    (-20)
#define CASSETTE_ROOM_MS       CASSETTE_BUTTONS_SLIDE_MS

/* The white page's edge mark, all of it tunable by hand: a transparent rectangle centred on
 * the tape, with only EDGE_SIDES of its borders lit. The default lights the top and the left
 * - the edges opposite the dome's bottom-right shadow - and leaves the right and the bottom
 * transparent. EDGE_COL is any palette name or a raw colour. */
#define EDGE_W         426
#define EDGE_H         272
#define EDGE_R         10          /* corner radius: the bands' and buttons' 8 px */
#define EDGE_BORDER_W  1
#define EDGE_COL       k_col_glass
#define EDGE_SIDES     (LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT)

/* The mark's inner glow: a linear gradient at 135 degrees, EDGE_GLOW_NEAR at the top-left
 * corner fading to EDGE_GLOW_FAR at the bottom-right, reaching it at EDGE_GLOW_FAR_FRAC
 * (0..255 of the way along the diagonal: 255 is the far corner; a smaller number stops the
 * fade early and leaves the rest empty). EDGE_GLOW_HEX is the glow's colour. */
#define EDGE_GLOW_HEX       0xFFFFFF
#define EDGE_GLOW_NEAR      90
#define EDGE_GLOW_FAR       LV_OPA_TRANSP
#define EDGE_GLOW_FAR_FRAC  255

/* The glow does not come in with the chrome: it waits for the drawer's, the block's, the
 * bands' and the window's 260 ms entrance to land, then fades in over EDGE_GLOW_FADE_MS. A
 * fade of 0 would make it a cut, one frame after the delay. */
#define EDGE_GLOW_DELAY_MS  260
#define EDGE_GLOW_FADE_MS   200

/* The tape's ink box in design coordinates is 16..784 x 16..460, so its centre is here. The
 * mark is centred on it at the controls pose. */
#define TAPE_CENTRE_X  400
#define TAPE_CENTRE_Y  238

static cassette_t *s_model;
static lv_obj_t *s_screen;     /* the root: its backdrop is the page */
static lv_obj_t *s_screen_back_cover; 
static lv_obj_t *s_page_edge;  /* the white page's two-sided edge mark */
/* The edge mark's gradient. LVGL's style keeps a pointer to the descriptor, so it outlives
 * the object here and is filled once, before the object is built. */
static lv_grad_dsc_t s_edge_grad;
static lv_obj_t *s_cassette;   /* the container everything the body draws hangs off */
static cassette_pose_t s_pose = { CASSETTE_SCALE_NONE, 0, 0 };
static cassette_pose_t s_pose_from;   /* the pose the change in flight started at */
static cassette_pose_t s_pose_to;     /* and the one it is heading for */
static bool s_peek_wants_room;        /* the peek is up */
static bool s_buttons_want_room;      /* the transport drawer (and the DJ block) is out */

/* ------------------------------------------------------------------ layout */

/* The drawer and the peek both ask the cassette for room. The view keeps one pose, so it
 * arbitrates: the drawer is the smaller of the two (134/256) and wins while it is out, and
 * at that size the tape already clears the peek's bar, so the peek's -60 px lift is not
 * added on top. At rest the cassette is at its design size, the entry view. A Level change
 * asks for the answer instantly; a swipe runs it over the slide it accompanies. */
static void room_apply(bool instant)
{
    int scale = CASSETTE_SCALE_NONE;
    int dx = 0;
    int dy = 0;
    if (s_buttons_want_room) {
        scale = CASSETTE_SCALE_BUTTONS;
        dx = CASSETTE_BUTTONS_DX;
        dy = CASSETTE_BUTTONS_DY;
    } else if (s_peek_wants_room) {
        scale = CASSETTE_SCALE_LABEL;
        dy = -60;
    }
    cassette_view_set_scale(scale, dx, dy, instant ? 0 : CASSETTE_ROOM_MS);
}

static void levels_room_cb(bool shown, bool instant, void *ctx)
{
    (void)ctx;
    s_peek_wants_room = shown;
    room_apply(instant);
}

/* --------------------------------------------------------------- the page */

/* The controls view has two faces. The vertical ladder shows it on a white page - the down
 * swipe's page - and the horizontal swipes show the same drawer and block on the dark
 * screen they have always had. So the page follows the gesture that opened the controls,
 * not the controls themselves: only the down swipe turns it white.
 *
 * The white page is the screen's whole backdrop. The line art keeps its inks on white, and
 * the cassette's black holes turn white with it, so the picture reads as ink on paper. The
 * flip is one frame - a colour animation would repaint the whole 800x480 panel on every
 * step - and it runs as the drawer's slide starts, so the page and the chrome move together.
 * The flag keeps a repeated request from invalidating the whole panel for a colour that is
 * already there.
 *
 * The page also owns the cover: once it is white, the down swipe turns cassette_cover on
 * and the up swipe off, and every flip back to dark hides it. The cover is the Track's
 * picture, so it comes and goes with the Track loader, not with the page's paint. */
static bool s_page_white;

/* The glow's fades. The object's opacity multiplies its gradient's stop opacities and its
 * border, so one animation covers the whole mark. */
static void edge_fade_cb(void *var, int32_t opa)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)opa, 0);
}

/* The white page: wait for the entrance, then fade the mark in from nothing. Starting from
 * transparent keeps a re-entry from flashing a mark that is already at full. */
static void edge_reveal(void)
{
    if (!s_page_edge) return;
    lv_anim_delete(s_page_edge, edge_fade_cb);
    lv_obj_set_style_opa(s_page_edge, LV_OPA_TRANSP, 0);
    lv_obj_set_hidden(s_page_edge, false);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_page_edge);
    lv_anim_set_exec_cb(&a, edge_fade_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_delay(&a, EDGE_GLOW_DELAY_MS);
    lv_anim_set_duration(&a, EDGE_GLOW_FADE_MS);
    /* Ease in: it starts softly out of nothing rather than popping at the first step. */
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_start(&a);
}

/* The dark page, or a close before the delay ran out: cancel and hide, no fade out - the
 * chrome is leaving and the page has already flipped. */
static void edge_hide(void)
{
    if (!s_page_edge) return;
    lv_anim_delete(s_page_edge, edge_fade_cb);
    lv_obj_set_hidden(s_page_edge, true);
}

static void page_set(bool white)
{
    if (white == s_page_white) return;
    s_page_white = white;
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    /* The cover belongs to the white page: every flip takes it away, and the down swipe
     * that the white page is for is the only way to ask for it again. */
    cassette_cover_set_shown(false);
#endif
    if (s_screen) lv_obj_set_style_bg_color(s_screen, white ? k_col_white : k_col_bg, 0);
    if (white) edge_reveal();
    else edge_hide();
    cassette_header_set_page(white);
    cassette_footer_set_page(white);
    cassette_buttons_set_page(white);
    cassette_eq_set_page(white);
    /* The tape's window chips are not repainted: they are windows on the background, and the
     * background behind them is k_col_bg on both pages - the dark screen at rest, the back
     * cover on the white page. */
}

/* The one place that opens and closes the controls, whichever intent asked. `white` is the
 * page the caller wants; only the down swipe asks for it, and a close always returns to
 * the dark screen. Opening the controls also ends the peek: the peek and the controls are
 * two overlays for one screen, and the white page carries only the controls. */
static void controls_set_shown(bool shown, bool white)
{
    if (shown && s_peek_wants_room) cassette_levels_toggle_peek();
    page_set(shown && white);
    cassette_header_set_shown(shown);
    cassette_footer_set_shown(shown);
    cassette_window_set_shown(shown);
    cassette_buttons_set_shown(shown);
    cassette_eq_set_shown(shown);
}

/* The drawer reports its slide as it starts, so the tape, the panel and the DJ block move
 * together. The page is not written here: it belongs to the intent that asked for the
 * controls. */
static void buttons_room_cb(bool shown, void *ctx)
{
    (void)ctx;
    s_buttons_want_room = shown;
    room_apply(false);
}

/* Re-place every part at the current pose. The parts keep the pose themselves, so the
 * draw callbacks (the pack rings, the lattice) follow without a second pass. The peek's
 * two tethers both end on the left hub, so its screen points move with it: the group is
 * above the hub and meets the top rim, the song is below it and meets the bottom one. */
static void hubs_apply(void)
{
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    cassette_tapes_hub_anchor(0, true, &x0, &y0);
    cassette_tapes_hub_anchor(0, false, &x1, &y1);
    cassette_levels_set_hubs(x0, y0, x1, y1);
#endif
}

static void cassette_place(void)
{
    cassette_tapes_place(s_pose);
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    /* The frames are placed now, so the cover's mask follows this step of the pose. */
    cassette_cover_place();
#endif
    cassette_meter_place(s_pose);
    hubs_apply();
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
    /* The peek's bar and the transport drawer drive this, so the ease is theirs: the
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
    cassette_tapes_set(f);
    cassette_meter_set(f);
    cassette_levels_apply(f);
}

/* ------------------------------------------------------------------ input */

/* What a touch means is screen_input's; what the screen does about it is here. */
static void screen_intent_cb(screen_intent_t intent, void *ctx)
{
    (void)ctx;
    /* Edit mode is modal: the overlay owns the touch and the two-finger VU arrives from
     * the adapter directly, so the view is the other half of the gate. */
    if (cassette_eq_is_editing()) return;

    switch (intent) {
        case SCREEN_INTENT_BUTTONS_SHOW:
        case SCREEN_INTENT_BUTTONS_HIDE:
            /* Either horizontal swipe toggles the drawer and the DJ block on the dark
             * screen: from the 100% entry either brings them in, and while they are out
             * either takes them out. The white page is not theirs to ask for - only the
             * down swipe turns it on. */
            controls_set_shown(!cassette_buttons_is_shown(), false);
            break;
        case SCREEN_INTENT_DOWN:
            /* Down is the ladder's way in: it shows the controls with the white page. An
             * open peek is put away first, and the same swipe does only that - the peek
             * and the controls never share the screen. If the controls are already out
             * (from a horizontal swipe) the down swipe still asks for its page; once the
             * page is white, the same swipe turns the Track's cover on. The label's mask is
             * built at the controls pose, so the pose must have landed: a swipe during the
             * shrink does nothing and the next one shows it. */
            if (s_peek_wants_room) cassette_levels_toggle_peek();
            else if (!s_buttons_want_room) controls_set_shown(true, true);
            else if (s_page_white) {
#if CONFIG_APP_BOARD_SUNTON_8048S050C
                if (s_pose.scale == CASSETTE_SCALE_BUTTONS)
                    cassette_cover_set_shown(true);
#endif
            } else page_set(true);
            break;
        case SCREEN_INTENT_UP:
            /* Up is the way back out, one step at a time: with the cover up it takes the
             * cover away and leaves the controls; with the controls out it takes them away.
             * With none out it raises the peek, and only from the 100% pose (256/256 with
             * neither the drawer nor the peek asking for room); while the peek is already
             * up it does nothing. */
            if (s_buttons_want_room) {
#if CONFIG_APP_BOARD_SUNTON_8048S050C
                if (cassette_cover_is_shown()) {
                    cassette_cover_set_shown(false);
                } else {
                    controls_set_shown(false, false);
                }
#else
                controls_set_shown(false, false);
#endif
            } else if (!s_peek_wants_room && s_pose.scale == CASSETTE_SCALE_NONE) {
                cassette_levels_toggle_peek();
            }
            break;
        case SCREEN_INTENT_VU_SHOW:
            cassette_meter_show();
            break;
        case SCREEN_INTENT_VU_HIDE:
            cassette_meter_hide();
            break;
        default:
            break;
    }
}

/* ------------------------------------------------------------------- build */

void cassette_view_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, k_col_bg, 0);
    lv_obj_set_scrollable(scr, false);
    s_screen = scr;

    s_screen_back_cover = cassette_box(scr, 16, 16, 768, 448, 0, k_col_bg);

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
    lv_obj_set_size(s_cassette, 800, 480);
    lv_obj_set_style_pad_all(s_cassette, 0, 0);
    lv_obj_set_style_bg_opa(s_cassette, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_cassette, 0, 0);
    lv_obj_set_scrollable(s_cassette, false);
    lv_obj_set_clickable(s_cassette, false);

    /* The cover goes down first, under everything. Then the white page's edge mark, inside
     * the cassette as well, and then the tape body: the order is label < glow < cinta. The
     * print never hides a tape, the glow washes the label but not the tape, and the peek,
     * the drawer and the DJ block are screen children above the cassette. */
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    cassette_cover_create(s_cassette);
#endif

    /* The white page's edge mark: an L of lit borders on an otherwise transparent rectangle,
     * centred on the tape. It is a child of the cassette, created after the cover and before
     * the body, so the glow lies over the label and under the tapes. It is hidden until the
     * page turns white. */
    cassette_pose_t controls = cassette_pose(CASSETTE_SCALE_BUTTONS, CASSETTE_BUTTONS_DX,
                                             CASSETTE_BUTTONS_DY);
    s_page_edge = cassette_border_box(s_cassette,
                                      cassette_x(controls, TAPE_CENTRE_X) - (EDGE_W / 2),
                                      cassette_y(controls, TAPE_CENTRE_Y) - EDGE_H / 2,
                                      EDGE_W, EDGE_H, EDGE_R,
                                      k_col_bg, LV_OPA_TRANSP, EDGE_COL, EDGE_BORDER_W);
    lv_obj_set_style_border_side(s_page_edge, EDGE_SIDES, 0);

    /* The inner glow: a static descriptor of two stops on a top-left -> bottom-right line.
     * The stops carry the opacity, so the far corner is really transparent; the object's own
     * bg_opa has to be COVER or LVGL skips the background (and the gradient) entirely. */
    static const lv_color_t glow_cols[2] = { COL_HEX(EDGE_GLOW_HEX), COL_HEX(EDGE_GLOW_HEX) };
    static const lv_opa_t glow_opa[2] = { EDGE_GLOW_NEAR, EDGE_GLOW_FAR };
    static const uint8_t glow_frac[2] = { 0, EDGE_GLOW_FAR_FRAC };
    lv_grad_init_stops(&s_edge_grad, glow_cols, glow_opa, glow_frac, 2);
    lv_grad_linear_init(&s_edge_grad, LV_GRAD_LEFT, LV_GRAD_TOP,
                        LV_GRAD_RIGHT, LV_GRAD_BOTTOM, LV_GRAD_EXTEND_PAD);
    lv_obj_set_style_bg_opa(s_page_edge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad(s_page_edge, &s_edge_grad, 0);

    lv_obj_set_hidden(s_page_edge, true);

    /* The tapes, then the meter: the frames and the whole body are created after the glow,
     * so every tape draws over it, and the meter paints inside the right hub. */
    cassette_tapes_create(s_cassette);
    cassette_meter_create(s_cassette);
    cassette_place();

    /* The peek is the screen's child, above the cassette's container. It is the only
     * overlay left: the Label cover and Artwork sheets are gone. */
    cassette_levels_create(scr, levels_room_cb, NULL);
    /* The peek exists now, so give it the resting pose's hub anchors. */
    hubs_apply();

    /* The transport drawer is chrome, above the peek. It starts off screen and hidden. */
    cassette_buttons_create(scr, buttons_room_cb, NULL);

    /* The DJ block is chrome too, on the other edge; created last so it starts above the
     * peek, hidden with the drawer. */
    cassette_eq_create(scr);

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
     * screen_input decide what a touch means: either horizontal swipe toggles the
     * transport drawer and the DJ block, a down swipe shows them, an up swipe takes them
     * away again, an up swipe with none out raises the peek, a down swipe with the peek up
     * puts it away, and a two-finger one toggles the VU meter. A tap is not an action;
     * next/previous belong to the block's arrows. */
    lv_obj_set_clickable(scr, true);
    screen_input_attach(scr, screen_intent_cb, NULL);
}

/* ------------------------------------------------------------------ update */

void cassette_view_update(const mini_icecast_status_t *st)
{
    if (!s_model) return;
    const cassette_frame_t *f = cassette_step(s_model, st, lv_tick_get());
    apply_frame(f);

    /* The peek's host keeps the status for the tag request and makes it itself; this
     * is a string compare on the frames that do not change it. */
    cassette_levels_update(st);

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

#if CONFIG_APP_BOARD_SUNTON_8048S050C
void cassette_view_set_cover(const lv_image_dsc_t *dsc)
{
    cassette_cover_set_image(dsc);
}

void cassette_view_set_tags(const char *group, const char *song)
{
    cassette_levels_set_tags(group, song);
}
#endif
