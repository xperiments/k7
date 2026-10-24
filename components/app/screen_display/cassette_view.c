/*
 * The cassette screen's composition.
 *
 * The screen is one picture: a container under the screen root, the tape body inside
 * it, the VU meter over the tape, and the Level sheets as the container's siblings.
 * The parts are private modules:
 *
 *   cassette_tapes    the packs, strands, rollers, run, hubs and the panel's frames
 *   cassette_meter    the VU meter
 *   cassette_levels   the Level machine, the Label, the Label cover and Artwork
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
#include "cassette_layout.h"
#include "cassette_levels.h"
#include "cassette_meter.h"
#include "cassette_style.h"
#include "cassette_tapes.h"
#include "cassette_view.h"
#include "mini_icecast.h"
#include "screen_input.h"

static const char *TAG = "screen";

/* The Label's peek shrinks the cassette so the bar clears the tape; the bar's slide is
 * 260 ms, and the pose must run over the same time so the two arrive as one picture. */
#define CASSETTE_SCALE_LABEL 200
#define CASSETTE_ROOM_MS     260

static cassette_t *s_model;
static lv_obj_t *s_cassette;   /* the container everything the body draws hangs off */
static cassette_pose_t s_pose = { CASSETTE_SCALE_NONE, 0, 0 };
static cassette_pose_t s_pose_from;   /* the pose the change in flight started at */
static cassette_pose_t s_pose_to;     /* and the one it is heading for */

/* ------------------------------------------------------------------ layout */

/* The Label asks the cassette for room through the Levels' room callback: the view is
 * the only side that knows the pose, so it turns the request into a scale, an offset
 * and a duration. A Level change asks for it instantly; a swipe asks for it on the
 * bar's slide. */
static void levels_room_cb(bool shown, bool instant, void *ctx)
{
    (void)ctx;
    cassette_view_set_scale(shown ? CASSETTE_SCALE_LABEL : CASSETTE_SCALE_NONE,
                            0, shown ? -60 : 0, instant ? 0 : CASSETTE_ROOM_MS);
}

/* Re-place every part at the current pose. The parts keep the pose themselves, so the
 * draw callbacks (the pack rings, the lattice) follow without a second pass. */
static void cassette_place(void)
{
    cassette_tapes_place(s_pose);
    cassette_meter_place(s_pose);
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
    /* The Label's peek drives this today, so the ease is the bar's: the bar and the
     * cassette arrive as one picture. */
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
    switch (intent) {
        case SCREEN_INTENT_NEXT:
            mini_icecast_skip();
            break;
        case SCREEN_INTENT_PREV:
            mini_icecast_prev();
            break;
        case SCREEN_INTENT_LEVEL_DOWN:
            cassette_levels_swipe_down();
            break;
        case SCREEN_INTENT_LEVEL_UP:
            cassette_levels_swipe_up();
            break;
        case SCREEN_INTENT_TAP:
            mini_icecast_skip();
            ESP_LOGI(TAG, "tap: next track");
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

    /* The tapes first, then the meter: the meter paints over the right pack's rings,
     * as it did when it sat between the strands and the hubs. */
    cassette_tapes_create(s_cassette);
    cassette_meter_create(s_cassette);
    cassette_place();

    /* The sheets are the screen's children, above the cassette's container. */
    cassette_levels_create(scr, levels_room_cb, NULL);

    /* The model: the tapes' frame count is the angular resolution; the geometry
     * supplies the speeds. */
    s_model = cassette_new(CASSETTE_TAPES_FRAME_COUNT, CASSETTE_DEFAULT_BAR_MS);
    if (!s_model) ESP_LOGW(TAG, "no model for the cassette: the screen will not update");
    else cassette_reset(s_model);

    /* Draw the empty screen now; the first status update replaces it. */
    if (s_model) apply_frame(cassette_step(s_model, NULL, lv_tick_get()));

    /* The picture is built. Make the screen the one thing a finger can hit, then let
     * screen_input decide what a touch means: a tap or a left swipe is next, a right
     * swipe previous, a vertical one moves a Level, a two-finger one is the menu. */
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
#endif
