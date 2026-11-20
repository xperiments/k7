#pragma once
/*
 * cassette_label - the two-finger screen's printed label, built from the embedded Label sheet.
 *
 * The Label sheet is every design the build carries, packed from assets/labels/ (ADR 0071).
 * A Track wears `ink_seed % template_count` of them, the same seed that picks its palette row,
 * so the shell and the design always pair. This module owns the objects and nothing else: the
 * design's geometry, colours and type are the sheet's, and the print's box is the Card's.
 *
 * The objects are children of the print, so they take the same treatment the Cover's picture
 * does: the Card's mask cuts the window and the print's box clips everything else. They are
 * built once per design and re-texted per Track; nothing here is per frame.
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

/* Open the sheet and build the hidden container under `print`. A sheet that does not validate
 * leaves the label absent and logs once: that is a build bug, and the flat palette fill is
 * still a cassette label. Nothing is built on a board with no cover capability. */
void cassette_label_create(lv_obj_t *print, const uint8_t *sheet, size_t sheet_size);

/* Show or hide with the two-finger pose. A cut, like the print's own. */
void cassette_label_set_pose(bool full);

/* The design for this `ink_seed` and the values for this Track. A different design rebuilds
 * the objects; the same one only rewrites the texts. `values` may be NULL, which prints the
 * design with blank fields. */
void cassette_label_update(uint32_t ink_seed, const label_values_t *values);

/* How many designs the embedded sheet carries; 0 when it did not open. */
uint16_t cassette_label_templates(void);

/* The current design's active canvas background, RGB888: the page the view paints while the
 * two-finger pose shows the tape. False when the design carries none, which leaves the page
 * as it is. Before the first design is built it is false too. */
bool cassette_label_canvas(uint32_t *rgb);

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
