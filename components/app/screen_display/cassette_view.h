#pragma once
/*
 * The cassette screen's LVGL view: objects, layout and fonts. Private to
 * screen_display; the arithmetic is components/portable/cassette.
 */
#include "mini_icecast.h"

void cassette_view_create(void);
void cassette_view_update(const mini_icecast_status_t *st);
