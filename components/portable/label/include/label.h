#pragma once
/*
 * label - the Label sheet: every Label template in the build, packed and read in place.
 *
 * The sheet is generated at build time by tools/label_pack.py from the designs in
 * assets/labels/, embedded in the image, and handed here as a pointer and a length. Nothing
 * is copied and nothing is parsed at runtime: label_open() validates the header, the counts,
 * the CRC and every offset once, and the accessors read the bytes that were already checked.
 *
 * The byte layout is docs/specs/label-templates.md; this header is the only interface the
 * rest of the firmware sees (ADR 0071). The sheet is data the build produced, not hostile
 * input, but it is validated all the same: a build bug must show up as a refusal and a log
 * line, not as a panel full of noise.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The only magic this reader accepts: `K7LBL`, NUL-padded to eight bytes. */
#define LABEL_MAGIC        "K7LBL\0\0\0"
#define LABEL_VERSION      2u   /* the template record carries eight bands */
#define LABEL_HEADER_SIZE  32u
#define LABEL_TEMPLATE_SIZE 64u
#define LABEL_PRIM_SIZE    24u
#define LABEL_SLOT_SIZE    8u

/* The design's space: the print at the entry grid. */
#define LABEL_SPACE_W      748u
#define LABEL_SPACE_H      359u

#define LABEL_PRIM_MAX     48u
#define LABEL_SLOT_MAX     16u
#define LABEL_TEMPLATE_MAX 32u

/* The band palette a design carries. A design may declare four to eight of them; the
 * sheet always has eight slots, and an undeclared one reads as 0. */
#define LABEL_BAND_COUNT   8u

/* The longest a resolved slot value can be; a design's slot `max` is bounded by this too,
 * so the view never has to truncate a value it was handed. */
#define LABEL_VALUE_MAX    96u

typedef enum {
    LABEL_OK = 0,
    LABEL_INVALID,  /* the bytes are not a sheet this firmware reads */
    LABEL_NO_MEM,
} label_result_t;

/* A validated sheet. Opaque: the fields are read through the accessors below. */
typedef struct label_sheet label_sheet_t;

/* What a prim is. */
typedef enum {
    LABEL_RECT = 0,
    LABEL_TEXT_STATIC = 1,  /* the text is in the sheet */
    LABEL_TEXT_SLOT = 2,    /* the text is a slot's name; the value is set per Track */
} label_kind_t;

/* What a prim paints with. The colours are the design's, not the palette's. */
typedef enum {
    LABEL_FILL_PAPER = 0,
    LABEL_FILL_INK,
    LABEL_FILL_RULE,
    LABEL_FILL_BAND0,
    LABEL_FILL_BAND1,
    LABEL_FILL_BAND2,
    LABEL_FILL_BAND3,
    LABEL_FILL_BAND4,
    LABEL_FILL_BAND5,
    LABEL_FILL_BAND6,
    LABEL_FILL_BAND7,
    LABEL_FILL_LITERAL,     /* the one literal colour a design may use; see `literal` */
} label_fill_t;

/* A text's face, by role. The index into the app's k_label_font[] comes from
 * label_font(); this is which of the design's three roles the prim names. */
typedef enum {
    LABEL_FONT_TITLE = 0,
    LABEL_FONT_NUMERAL,
    LABEL_FONT_CAPTION,
} label_font_role_t;

typedef enum {
    LABEL_ALIGN_LEFT = 0,
    LABEL_ALIGN_CENTER,
    LABEL_ALIGN_RIGHT,
} label_align_t;

/* One prim, as read. Positions are in the design's space (748x359), tenths of a degree for
 * rotation, and `text` is only meaningful for the two text kinds. */
typedef struct {
    uint8_t  kind;       /* label_kind_t */
    uint8_t  fill;       /* label_fill_t */
    uint8_t  font;       /* label_font_role_t; a rect's is TITLE and unused */
    uint8_t  align;      /* label_align_t; a rect's is LEFT and unused */
    int16_t  x, y;
    uint16_t w, h;       /* h is 0 for a text */
    uint16_t r;          /* radius; 0 for a text */
    int16_t  rot10;      /* tenths of a degree, 0..3599 */
    uint32_t literal;    /* RGB888, only when fill == LABEL_FILL_LITERAL */
} label_prim_t;

/* One slot of a template: its name and the most characters the design allows. */
typedef struct {
    const char *name;    /* NUL-terminated inside the sheet */
    uint8_t     max;
} label_slot_t;

/* A design's role colours, RGB888 in the low 24 bits: paper, ink, rule and the eight
 * bands. A band the design did not declare is 0. */
typedef struct {
    uint32_t paper, ink, rule, band[LABEL_BAND_COUNT];
} label_inks_t;

/* ---------------------------------------------------------------- the sheet */

/* Validate `size` bytes at `blob` and take a reference to them. The bytes must outlive the
 * sheet (on the device they are flash). `font_count` is the number of entries in the app's
 * k_label_font[] - LABEL_FONT_COUNT from the generated header - which is what a prim's font
 * index is checked against. `why`, when non-NULL, receives one sentence naming the first
 * failure (or "" on success). */
label_result_t label_open_why(const uint8_t *blob, size_t size, uint8_t font_count,
                              label_sheet_t **out, char *why, size_t why_size);
label_result_t label_open(const uint8_t *blob, size_t size, uint8_t font_count,
                          label_sheet_t **out);
void label_close(label_sheet_t *s);

/* How many designs the sheet carries; never 0 for a sheet that opened. */
uint16_t label_templates(const label_sheet_t *s);

/* The design a Track wears: `ink_seed % template_count`, the same seed that picks the
 * palette row (cassette-card.md, "The ink"). */
uint16_t label_pick(const label_sheet_t *s, uint32_t ink_seed);

/* The design's `name`, for the log. */
const char *label_template_name(const label_sheet_t *s, uint16_t t);

/* A template's prims, in paint order: first at the back. */
uint16_t label_prim_count(const label_sheet_t *s, uint16_t t);
bool     label_prim_at(const label_sheet_t *s, uint16_t t, uint16_t i, label_prim_t *out);

/* The text of a text prim, or NULL for a rect or an index out of range. A LABEL_TEXT_SLOT
 * prim's text is the slot's name, not a value. */
const char *label_prim_text(const label_sheet_t *s, uint16_t t, uint16_t i);

uint16_t label_slot_count(const label_sheet_t *s, uint16_t t);
bool     label_slot_at(const label_sheet_t *s, uint16_t t, uint16_t i, label_slot_t *out);

/* The design's colours. */
label_inks_t label_inks(const label_sheet_t *s, uint16_t t);

/* The compiled font a role names, as an index into k_label_font[]. */
uint8_t label_font(const label_sheet_t *s, uint16_t t, uint8_t role);

/* The sheet's checksum: CRC-32/IEEE, the same one the packer writes and the header carries.
 * Exposed because it is the format's integrity check, not an implementation detail. */
uint32_t label_crc32(const uint8_t *data, size_t size);

/* Copy `src` into `dst`, cut at `max_chars` characters (not bytes), never splitting a UTF-8
 * sequence, NUL-terminated within `cap`. `dst` is always a string, even for a NULL `src`. */
void label_cut(const char *src, unsigned max_chars, char *dst, size_t cap);

#ifdef __cplusplus
}
#endif
