/*
 * cassette_levels.c - the peek's host on the Cassette screen.
 *
 * The Label cover and Artwork screens and the Level machine are removed. Bare is the
 * only screen: the up swipe at Bare raises or lowers the peek, the down swipe has no
 * meaning, and no full-screen sheet exists. The peek itself is cassette_peek; this module
 * only creates it, feeds it the frame and the tags, and owns the tag request dedupe.
 *
 * The peek's tags still come from the Track's container through tags_loader. The cover
 * half of the old artwork loader is parked in artwork_loader.c and is not built.
 */
#include "cassette_levels.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "cassette.h"
#include "cassette_peek.h"
#include "mini_icecast.h"
#include "tags_loader.h"

static const char *TAG = "screen";

#if CONFIG_APP_BOARD_SUNTON_8048S050C
/* The status is what the tag request is made from, so it is kept here rather than in the
 * view; the path/format dedupe makes a 25 Hz update a string compare. */
static mini_icecast_status_t s_status;
static bool s_have_status;
static char s_tags_path[LIB_PATH_MAX];
static mi_fmt_t s_tags_fmt;
static bool s_tags_requested;

/* The text tags follow every Track, on Bare's peek. A new Track drops the previous tags
 * at once and the peek shows the path/filename fallback until the new ones land. */
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
    tags_loader_request(s_status.path, s_status.format);
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
    tags_request();
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
