/*
 * cassette_levels.c - the peek and Real's cover, hosted on the Cassette screen.
 *
 * The peek is the bottom rung of the four (ADR 0025): the up swipe at Initial raises it
 * (the view does that only from the entry 100% pose) and the down swipe puts it away. The
 * cover is the top rung's picture. No full-screen sheet exists. The peek itself is
 * cassette_peek; this module only creates it, feeds it the frame and the tags, and owns the
 * Track request dedupe.
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
static bool s_peek_visible;
static bool s_cover_visible;

/* The text tags and the cover follow every Track: the peek's labels and the white page's
 * cover are both one request away. A new Track drops the previous picture at once - the
 * loader decodes into the one buffer, so it must not be drawn while it is written - and
 * then asks for the new tags and picture together. */
/* The cover is Real's, not the cassette's: ask only while the white page is up. The
 * label fill shows at once and the picture cuts in over it when the decode finishes;
 * an empty frame for the whole read was worse than the label. */
static void request_cover(void)
{
    if (!s_have_status || !s_status.path[0] || s_status.format == MI_FMT_NONE) return;
    if (artwork_loader_cover_answered(s_status.path, s_status.format)) return;
    cassette_view_set_cover(NULL);
    artwork_loader_request(s_status.path, s_status.format);
}

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
    /* The new Track's names are not the old Track's: clear them so the peek falls back to
     * the path/filename until the loader answers. */
    cassette_view_set_tags("", "");
    /* Each level asks for what it shows, and only when it is up. The loader remembers
     * its answer, so closing and reopening a level does not read the card again. */
    if (s_peek_visible) artwork_loader_request_tags(s_status.path, s_status.format);
    if (s_cover_visible) request_cover();
}
#endif

void cassette_levels_set_peek(bool shown)
{
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    if (shown == cassette_peek_is_shown()) return;
    s_peek_visible = shown;
    if (shown) cassette_peek_show(); else cassette_peek_hide();
    if (shown && s_have_status && s_status.path[0] && s_status.format != MI_FMT_NONE &&
        !artwork_loader_tags_answered(s_status.path, s_status.format))
        artwork_loader_request_tags(s_status.path, s_status.format);
    ESP_LOGI(TAG, "peek %s", shown ? "shown" : "hidden");
#else
    (void)shown;
#endif
}

void cassette_levels_set_cover_visible(bool shown)
{
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    if (shown == s_cover_visible) return;
    s_cover_visible = shown;
    if (shown) request_cover();
#else
    (void)shown;
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

void cassette_levels_create(lv_obj_t *screen)
{
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    cassette_peek_create(screen);
#else
    (void)screen;
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
