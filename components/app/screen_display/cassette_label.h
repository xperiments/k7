#pragma once
/*
 * cassette_label - the two-finger screen's printed label, built from the K7Art pool.
 *
 * The pool is the factory sheet, embedded from assets/k7art/ at build time (ADR 0071),
 * plus the user's pack when the `art` partition holds a valid one. A Track wears
 * `ink_seed % (factory + user)` of them, the same seed that picks its palette row, so the
 * shell and the design always pair (docs/specs/art-packs.md §5). This module owns the
 * objects and nothing else: the design's geometry, colours and type are the sheet's, and
 * the print's box is the Card's.
 *
 * The pool itself is the art endpoint's (`app_services_art_*`), because the same pool is
 * what an upload replaces. This file hands it the embedded factory sheet at boot, asks it
 * for the sheet and template on each Track change, and rebuilds when either the sheet or
 * the generation moved. An upload asks the screen's lock through the quiesce hook and
 * this module forgets the old sheet, so its bytes may be unmapped.
 *
 * The objects are children of the print, so they take the same treatment the Cover's
 * picture does: the Card's mask cuts the window and the print's box clips everything else.
 * They are built once per design and re-texted per Track; nothing here is per frame.
 *
 * Private to screen_display.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"

#include "lvgl.h"

#include "label_values.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Hand the embedded factory sheet and the compiled font table to the art pool, before
 * app_services_start() can serve `GET /api/art`. Called from screen_display_boot(); a
 * factory sheet that does not open is a build bug and leaves the print's flat fill. */
bool cassette_label_factory_register(void);

/* Open the hidden container under `print`. The pool is already registered; a board with
 * no cover has no print and gets nothing. A pack installed later is picked up by the next
 * update. */
void cassette_label_create(lv_obj_t *print);

/* Show or hide with the two-finger pose. A cut, like the print's own. */
void cassette_label_set_pose(bool full);

/* The design for this `ink_seed` and the values for this Track. A different design - or a
 * different sheet behind the same index - rebuilds the objects; the same one only
 * rewrites the texts. `values` may be NULL, which prints the design with blank fields. */
void cassette_label_update(uint32_t ink_seed, const label_values_t *values);

/* Under the screen's lock, after app_services_art_quiesced(): forget the sheet and its
 * objects, whose Slot names point into bytes the upload is about to unmap. The next
 * update rebuilds from the factory, or from the new pack. */
void cassette_label_pool_dropped(void);

/* How many designs the active sheet carries; 0 when none did open. */
uint16_t cassette_label_templates(void);

/* The current design's canvas background for a cassette row, RGB888: the view paints the
 * cassette's body and the page with it while the two-finger pose is up. The design's list is
 * cycled by the row (`row % count`); false when the design carries none, which leaves the
 * Track's row as it is. Before the first design is built it is false too. */
bool cassette_label_canvas(uint16_t row, uint32_t *rgb);

#if CONFIG_APP_UI_RENDER_BENCH
/* The render bench's fixture and its Track change. Blank slots draw almost nothing, so the
 * bench prints a sample title into every field once the two-finger pose is up; the text
 * step then dirties every text object, which is what set_texts() does for a new Track. */
void cassette_label_bench_fill(void);
void cassette_label_bench_texts(void);
#endif

#ifdef __cplusplus
}
#endif
