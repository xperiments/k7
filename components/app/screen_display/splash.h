#pragma once
/*
 * The boot mark: the K7 logo on the black page, held until the card has been scanned.
 *
 * This is the first thing the panel shows, and the reason it exists is a hardware fact:
 * the panel's two framebuffers live in PSRAM, which powers up reading as 0xFFFF - white.
 * The backlight used to be lit before LVGL had painted anything, so boot opened on a white
 * flash and went black when the cassette arrived. The screen now paints this mark into both
 * framebuffers before the light comes on, and the appliance takes it down only after
 * `app_services_start()` has mounted and scanned the card.
 *
 * The mark is a glyph, not a bitmap: the icon font's K7 logo (U+F00F), 238x128 px of ink, in
 * the palette's primary on the palette's black. It is the same mark the controls header wears
 * (cassette_header.c), drawn from the same typeface at a size the header's 60x32 A8 blob
 * cannot give.
 *
 * Everything here is called under the LVGL lock, on the active screen, and only once.
 */
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Paint the mark: the screen's own page in k_col_bg, opaque, with the K7 glyph centred on
 * it in k_col_primary. Idempotent enough to be called once; a second call adds nothing. */
void splash_create(void);

/* Take the mark down. The caller has already built what replaces it - the composition
 * creates the cassette first and calls this second - so the screen cuts from the mark to
 * the finished picture with no empty frame in between. Harmless when nothing was created. */
void splash_destroy(void);

#ifdef __cplusplus
}
#endif
