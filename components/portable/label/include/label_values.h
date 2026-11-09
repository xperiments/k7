#pragma once
/*
 * label_values - what a design's slots print, resolved from the Track's Snapshot row.
 *
 * The vocabulary is the Snapshot's, not the editor's (docs/specs/label-templates.md §5): a
 * Track with no row leaves every slot empty, and an unmeasured value is empty rather than
 * zero. Nothing here touches the card, and nothing here is LVGL: the resolver is handed the
 * models the rest of the device already holds.
 */
#include <stdbool.h>
#include <stdint.h>

#include "label.h"
#include "library.h"
#include "snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The one table, in the editor's own ids. The packer refuses a name that is not here. */
typedef enum {
    LABEL_SLOT_SONG_TITLE = 0,
    LABEL_SLOT_SONG_ARTIST,
    LABEL_SLOT_SONG_ALBUM,
    LABEL_SLOT_GENRE,
    LABEL_SLOT_BPM,
    LABEL_SLOT_MUSICAL_KEY,
    LABEL_SLOT_ENERGY,
    LABEL_SLOT_DANCEABILITY,
    LABEL_SLOT_VALENCE,
    LABEL_SLOT_AROUSAL,
    LABEL_SLOT_MOOD,
    LABEL_SLOT_COUNT,
} label_slot_id_t;

typedef struct {
    char text[LABEL_SLOT_COUNT][LABEL_VALUE_MAX + 1];
} label_values_t;

/* The Snapshot row for a Track's path and size, or false when the image has none.
 *
 * It tries the path as the card gives it, then - once, and only when the first misses - with
 * the Latin-1 accented letters decomposed. macOS hands a card's names out decomposed (NFD)
 * while FATFS reads the same card composed (NFC), so an image built on a Mac hashes different
 * bytes for the same Track; the size must still agree, so it is the same file either way.
 * (docs/specs/label-templates.md §5) */
bool label_row_for(const snapshot_t *snap, const char *relpath, uint32_t size,
                   snapshot_row_t *row);

/* Every slot empty. */
void label_values_clear(label_values_t *v);

/* Fill `v` from the playing Track's row, if this Library still has the path and the
 * Snapshot still matches its size. Both pointers are the caller's: the resolver acquires
 * nothing and holds nothing. A NULL, unknown or unmatched Track clears `v`. */
void label_values_fill(label_values_t *v, const library_t *lib, const snapshot_t *snap,
                       const char *relpath);

/* The formatting half of fill(): a row already in hand becomes the eleven strings. Split out
 * so the rules - whole-number BPM, the Camelot code, one mood word, 0xFF prints nothing - are
 * testable without a Library or a card. `fill` is this plus the path lookup. */
void label_values_apply(label_values_t *v, const snapshot_t *snap, const snapshot_row_t *row);

/* The value a slot prints, or "" for a name the vocabulary does not know. */
const char *label_values_get(const label_values_t *v, const char *slot_name);

/* The id a slot index prints, for logs and tests: "song_title", ... */
const char *label_values_name(label_slot_id_t id);

#ifdef __cplusplus
}
#endif
