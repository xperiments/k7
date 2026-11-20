#pragma once
/*
 * label_pack - a user's K7Art pack, as the device reads it: the 128-byte header, the
 * label sheet at its offset and the J-cards behind it (docs/specs/art-packs.md §1).
 *
 * The device never parses a K7Art and never looks inside the J-cards; it validates the
 * header, the payload's CRC, the bounds and the sheet's own open, and reads the sheet in
 * place. `label_pack_open_why()` is the whole check: on LABEL_OK the pack is one this
 * firmware can draw, and `*sheet` is a `label_sheet_t` over the pack's own bytes (the
 * caller closes it with label_close()).
 *
 * The pack is named here, beside the sheet it carries: both are the label's format, and
 * the pack is what a user writes to the `art` partition ([label_store.h]).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "label.h"

#ifdef __cplusplus
extern "C" {
#endif

/* `K7ART`, NUL-padded to eight - deliberately not the sheet's `K7LBL`. */
#define LABEL_PACK_MAGIC        "K7ART\0\0\0"
#define LABEL_PACK_VERSION      1u
#define LABEL_PACK_HEADER_SIZE  128u
/* The `art` partition's size, and the most a pack may claim (art-packs.md §1). */
#define LABEL_PACK_MAX          (1024u * 1024u)
#define LABEL_PACK_ID_MAX       16u
#define LABEL_PACK_NAME_MAX     32u
#define LABEL_PACK_AUTHOR_MAX   32u
#define LABEL_PACK_DESIGN_MAX   32u /* the sheet's template cap */

/* A pack the header checks accepted. The strings are NUL-terminated copies bounded by the
 * header's fields; `sheet` points into the pack's bytes and is only meaningful after the
 * sheet opened. */
typedef struct {
    uint32_t total_size;
    uint32_t crc32;          /* over [LABEL_PACK_HEADER_SIZE, total_size) */
    uint16_t design_count;
    uint16_t sheet_version;
    uint16_t font_count;     /* the LABEL_FONT_COUNT the sheet was built against */
    uint32_t sheet_offset;   /* always LABEL_PACK_HEADER_SIZE */
    uint32_t sheet_size;
    uint32_t jcards_offset;  /* always LABEL_PACK_HEADER_SIZE + sheet_size */
    uint32_t jcards_size;
    uint32_t sheet_crc;      /* the sheet's own CRC-32, as its header carries it */
    const uint8_t *sheet;    /* the sheet's bytes inside the pack */
    char pack_id[LABEL_PACK_ID_MAX + 1];
    char name[LABEL_PACK_NAME_MAX + 1];
    char author[LABEL_PACK_AUTHOR_MAX + 1];
} label_pack_t;

/* Validate a pack and open the sheet it carries. `size` is the bytes the device holds:
 * for a file it is the file's length, and for the `art` partition it is the partition's
 * size - a pack's `total_size` may be smaller, and the bytes after it are not part of it.
 * `font_count` is the firmware's LABEL_FONT_COUNT, which the pack's own `font_count` must
 * equal; 0 means the caller has no table yet (the host tier), and the sheet is then
 * checked against the pack's declared count.
 *
 * Refusals are one sentence naming the first failure; the caller may show it as it is
 * (the HTTP upload answers 422 with it). */
label_result_t label_pack_open_why(const uint8_t *bytes, size_t size, uint8_t font_count,
                                   label_pack_t *out, label_sheet_t **sheet,
                                   char *why, size_t why_size);

#ifdef __cplusplus
}
#endif
