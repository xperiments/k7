/*
 * The cassette screen's composition.
 *
 * The screen is one picture: a container under the screen root, the tape body inside
 * it, the VU meter in the right hub, and the Level sheets as the container's siblings.
 * The parts are private modules:
 *
 *   cassette_tapes    the packs, strands, rollers, run, hubs and the panel's frames
 *   cassette_meter    the VU meter
 *   cassette_levels   the Level machine, the Label cover and the Artwork sheet
 *   cassette_buttons  the three-button transport drawer at the left edge
 *   cassette_eq       the DJ block at the right edge: GAIN, HI/MED/LOW and the < / > arrows
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
 * Called under the LVGL lock, like everything else on the screen.
 */
#include <stdint.h>

#include "esp_log.h"
#include "lvgl.h"

#include "cassette.h"
#include "cassette_buttons.h"
#include "cassette_eq.h"
#include "cassette_layout.h"
#include "cassette_levels.h"
#include "cassette_meter.h"
#include "cassette_style.h"
#include "cassette_tapes.h"
#include "cassette_view.h"
#include "mini_icecast.h"
#include "screen_input.h"

static const char *TAG = "screen";

/* The peek shrinks the cassette so the bar clears the tape; the bar's slide is
 * 260 ms, and the pose must run over the same time so the two arrive as one picture.
 * The rest pose is the one the left drawer used to ask for: 134/256 puts the tape's
 * 402 px box at 148..550, which clears the 120 px drawer on the left and leaves room
 * for the 192 px DJ block at 592..784 on the right. The VU lands at about x=450. */
#define CASSETTE_SCALE_LABEL   200
#define CASSETTE_SCALE_REST    134
#define CASSETTE_REST_DX       (-51)
#define CASSETTE_REST_DY       (-20)
/* The peek's bar slides in 260 ms; the pose runs with it so the two arrive as one. */
#define CASSETTE_ROOM_MS       260

static cassette_t *s_model;
static lv_obj_t *s_cassette;   /* the container everything the body draws hangs off */
static cassette_pose_t s_pose = { CASSETTE_SCALE_NONE, 0, 0 };
static cassette_pose_t s_pose_from;   /* the pose the change in flight started at */
static cassette_pose_t s_pose_to;     /* and the one it is heading for */
static bool s_peek_wants_room;        /* the peek is up */

/* ------------------------------------------------------------------ layout */

/* The peek asks the cassette for room. The rest pose already leaves the left drawer and
 * the right DJ block clear, so the drawer no longer changes it; only the peek's 200/256
 * and -60 px lift do. A Level change asks for the answer instantly; a swipe runs it over
 * the slide it accompanies. */
static void room_apply(bool instant)
{
    int scale = CASSETTE_SCALE_REST;
    int dx = CASSETTE_REST_DX;
    int dy = CASSETTE_REST_DY;
    if (s_peek_wants_room) {
        scale = CASSETTE_SCALE_LABEL;
        dx = 0;
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
    /* Edit mode is modal: the overlay owns the touch and the two-finger menu arrives from
     * the adapter directly, so the view is the other half of the gate. */
    if (cassette_eq_is_editing()) return;

    switch (intent) {
        case SCREEN_INTENT_BUTTONS_SHOW:
            cassette_buttons_set_shown(true);
            break;
        case SCREEN_INTENT_BUTTONS_HIDE:
            cassette_buttons_set_shown(false);
            break;
        case SCREEN_INTENT_LEVEL_DOWN:
            cassette_levels_swipe_down();
            /* The Artwork sheet raises itself when it settles; chrome stays above it. */
            cassette_buttons_raise();
            cassette_eq_raise();
            break;
        case SCREEN_INTENT_LEVEL_UP:
            cassette_levels_swipe_up();
            cassette_buttons_raise();
            cassette_eq_raise();
            break;
        case SCREEN_INTENT_MENU:
            /* The menu is not designed yet; screen_input has already logged it. */
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

    /* The tapes first, then the meter: the meter paints inside the right hub. The rest
     * pose is the one the DJ block leaves room for, so the first paint is already it. */
    s_pose = cassette_pose(CASSETTE_SCALE_REST, CASSETTE_REST_DX, CASSETTE_REST_DY);
    cassette_tapes_create(s_cassette);
    cassette_meter_create(s_cassette);
    cassette_place();

    /* The sheets are the screen's children, above the cassette's container. */
    cassette_levels_create(scr, levels_room_cb, NULL);
    /* The peek exists now, so give it the resting pose's hub anchors. */
    hubs_apply();

    /* The transport drawer is chrome, above every sheet: created after the Levels, and
     * raised again after a vertical swipe. It starts off screen and hidden. */
    cassette_buttons_create(scr);

    /* The DJ block is chrome too, on the other edge; created last so it starts above the
     * sheets, and raised with the drawer after a vertical swipe. */
    cassette_eq_create(scr);

    /* The model: the tapes' frame count is the angular resolution; the geometry
     * supplies the speeds. */
    s_model = cassette_new(CASSETTE_TAPES_FRAME_COUNT, CASSETTE_DEFAULT_BAR_MS);
    if (!s_model) ESP_LOGW(TAG, "no model for the cassette: the screen will not update");
    else cassette_reset(s_model);

    /* Draw the empty screen now; the first status update replaces it. */
    if (s_model) apply_frame(cassette_step(s_model, NULL, lv_tick_get()));

    /* The picture is built. Make the screen the one thing a finger can hit, then let
     * screen_input decide what a touch means: a right swipe slides the transport in, a
     * left swipe slides it out, a vertical one moves a Level, and a two-finger one is the
     * menu. A tap is not an action; next/previous belong to the block's arrows. */
    lv_obj_set_clickable(scr, true);
    screen_input_attach(scr, screen_intent_cb, NULL);
}

/* ------------------------------------------------------------------ update */

void cassette_view_update(const mini_icecast_status_t *st)
{
    if (!s_model) return;
    const cassette_frame_t *f = cassette_step(s_model, st, lv_tick_get());
    apply_frame(f);

    /* The Levels keep the status for the Artwork request and make it themselves; this
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
void cassette_view_set_artwork(const lv_image_dsc_t *dsc)
{
    cassette_levels_set_artwork(dsc);
}

void cassette_view_set_case(const lv_image_dsc_t *dsc)
{
    cassette_levels_set_case(dsc);
}

void cassette_view_set_tags(const char *group, const char *song)
{
    cassette_levels_set_tags(group, song);
}
#endif
