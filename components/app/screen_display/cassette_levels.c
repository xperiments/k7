/*
 * cassette_levels.c - the peek and the white page's cover, hosted on the Cassette screen.
 *
 * The Label cover and Artwork screens and the Level machine are removed. Bare is the
 * only screen: the up swipe raises the peek (the view does that only from the entry 100%
 * pose), the down swipe puts it away or turns the white page's cover on, and no
 * full-screen sheet exists. The peek itself is cassette_peek; this module only creates
 * it, feeds it the frame and the tags, and owns the Track request dedupe.
 *
 * A new Track asks artwork_loader for its picture and its tags in one request slot, and
 * hides the previous picture before the loader reuses its buffer. The cover is the
 * view's to show - the white page's down swipe turns it on, the up turns it off - but the
 * request belongs here, with the status that names the Track.
 */
#include "cassette_levels.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "cassette.h"
#include "cassette_peek.h"
#include "cassette_view.h"
#include "artwork_loader.h"
#include "mini_icecast.h"

#if CONFIG_APP_BOARD_SUNTON_8048S050C
static const char *TAG = "screen";

/* The status is what the Track requests are made from, so it is kept here rather than in
 * the view; the path/format dedupe makes a 25 Hz update a string compare. */
static mini_icecast_status_t s_status;
static bool s_have_status;
static char s_track_path[LIB_PATH_MAX];
static mi_fmt_t s_track_fmt;
static bool s_track_requested;

/* The text tags and the cover follow every Track: the peek's labels and the white page's
 * cover are both one request away. A new Track drops the previous picture at once - the
 * loader decodes into the one buffer, so it must not be drawn while it is written - and
 * then asks for the new tags and picture together. */
static void track_request(void)
{
    if (!s_have_status) return;
    if (s_track_requested && s_track_fmt == s_status.format &&
        strcmp(s_track_path, s_status.path) == 0) return;
    s_track_requested = true;
    s_track_fmt = s_status.format;
    snprintf(s_track_path, sizeof s_track_path, "%s", s_status.path);
    cassette_peek_set_path(s_status.path);
    if (!s_status.path[0] || s_status.format == MI_FMT_NONE) {
        cassette_view_set_cover(NULL);
        return;
    }
    ESP_LOGI(TAG, "track request: %s (fmt %d)", s_status.path, (int)s_status.format);
    /* The requests bump the sequence before the hide: the loader's hand-off re-checks it,
     * so a picture for the old Track cannot land after this hide. */
    artwork_loader_request_tags(s_status.path, s_status.format);
    artwork_loader_request(s_status.path, s_status.format);
    cassette_view_set_cover(NULL);
}
#endif

void cassette_levels_toggle_peek(void)
{
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    bool show = !cassette_peek_is_shown();
    if (show) cassette_peek_show(); else cassette_peek_hide();
    ESP_LOGI(TAG, "swipe up: peek %s", show ? "shown" : "hidden");
#endif
}

void cassette_levels_apply(const cassette_frame_t *frame)
{
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    cassette_peek_apply(frame);
#else
    (void)frame;
#endif
}

void cassette_levels_update(const mini_icecast_status_t *status)
{
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    if (status) {
        s_status = *status;
        s_have_status = true;
    }
    track_request();
#else
    (void)status;
#endif
}

void cassette_levels_create(lv_obj_t *screen, cassette_levels_room_cb_t room, void *ctx)
{
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    cassette_peek_create(screen, room, ctx);
#else
    (void)screen;
    (void)room;
    (void)ctx;
#endif
}

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
