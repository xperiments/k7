/*
 * label - see include/label.h and docs/specs/label-templates.md.
 *
 * The sheet is read in place: once label_open() has validated the bytes, every accessor reads
 * a field it already range-checked, and nothing is copied or allocated except the handle.
 * That is the same shape the Snapshot has, and for the same reason - the bytes are already
 * in flash, and a copy would cost RAM the screen needs.
 *
 * The checks are a refusal, never a clamp: a sheet that disagrees with the format is a build
 * bug, and the caller can only fall back to the flat palette fill and say so once.
 */
#include "label.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct label_sheet {
    const uint8_t *blob;
    size_t         size;
    uint16_t       templates;
    uint16_t       prims;
    uint16_t       slots;
    uint16_t       pool_size;
    const uint8_t *tmpl;   /* the template array: templates x 48 */
    const uint8_t *prim;   /* the prim array: prims x 24 */
    const uint8_t *slot;   /* the slot array: slots x 8 */
    const uint8_t *pool;   /* the string pool: pool_size bytes, offsets relative to it */
};

/* ------------------------------------------------------------------ bytes */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static label_result_t fail(char *why, size_t why_size, const char *msg)
{
    if (why && why_size) snprintf(why, why_size, "%s", msg);
    return LABEL_INVALID;
}

uint32_t label_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/* A pool offset is valid when it points at a NUL-terminated string inside the pool. Offsets
 * are relative to the pool's start, which is why the pool is the last thing in the sheet. */
static bool string_in(const uint8_t *pool, uint32_t pool_size, uint32_t off)
{
    if (off >= pool_size) return false;
    uint32_t i = off;
    while (i < pool_size && pool[i]) i++;
    return i < pool_size;
}

/* ------------------------------------------------------------------ open */

static const uint8_t *template_at(const label_sheet_t *s, uint16_t t)
{
    return s->tmpl + (size_t)t * LABEL_TEMPLATE_SIZE;
}

static const uint8_t *prim_at(const label_sheet_t *s, uint16_t i)
{
    return s->prim + (size_t)i * LABEL_PRIM_SIZE;
}

static const uint8_t *slot_at(const label_sheet_t *s, uint16_t i)
{
    return s->slot + (size_t)i * LABEL_SLOT_SIZE;
}

/* One prim: a kind, a colour, a box, a rotation, and either a string or a slot name. */
static bool prim_ok(const label_sheet_t *s, uint16_t i)
{
    const uint8_t *p = prim_at(s, i);
    const uint8_t kind = p[0];
    const uint8_t fill = p[1];
    const uint8_t font = p[2];
    const uint8_t align = p[3];
    const int16_t x = (int16_t)rd16(p + 4);
    const int16_t y = (int16_t)rd16(p + 6);
    const uint16_t w = rd16(p + 8);
    const uint16_t h = rd16(p + 10);
    const uint16_t rot = rd16(p + 14);
    const uint32_t text = rd32(p + 16);

    if (kind > LABEL_TEXT_SLOT || fill > LABEL_FILL_LITERAL) return false;
    if (font > LABEL_FONT_CAPTION || align > LABEL_ALIGN_RIGHT) return false;
    if (rot > 3599u) return false;

    /* The print clips, so a prim outside it would simply lose part of itself: the packer
     * refuses that and the reader does not accept it either. */
    if (x < 0 || y < 0 || (int32_t)x + (int32_t)w > (int32_t)LABEL_SPACE_W ||
        y >= (int32_t)LABEL_SPACE_H)
        return false;

    if (kind == LABEL_RECT) {
        if (h == 0 || (int32_t)y + (int32_t)h > (int32_t)LABEL_SPACE_H) return false;
        if (text != 0xFFFFFFFFu) return false;
    } else {
        if (fill != LABEL_FILL_INK) return false;   /* v1 has one text colour */
        if (!string_in(s->pool, s->pool_size, text)) return false;
    }
    return true;
}

static bool slot_ok(const label_sheet_t *s, uint16_t i)
{
    const uint8_t *p = slot_at(s, i);
    if (p[4] == 0 || p[4] > LABEL_VALUE_MAX) return false;
    return string_in(s->pool, s->pool_size, rd32(p));
}

static bool template_ok(const label_sheet_t *s, uint16_t t, uint8_t font_count)
{
    const uint8_t *p = template_at(s, t);
    const uint32_t name = rd32(p);
    const uint16_t prim_start = rd16(p + 4);
    const uint16_t prim_count = rd16(p + 6);
    const uint16_t slot_start = rd16(p + 8);
    const uint16_t slot_count = rd16(p + 10);

    if (!string_in(s->pool, s->pool_size, name)) return false;
    if (prim_count == 0 || prim_count > LABEL_PRIM_MAX) return false;
    if ((uint32_t)prim_start + prim_count > s->prims) return false;
    if (slot_count > LABEL_SLOT_MAX) return false;
    if ((uint32_t)slot_start + slot_count > s->slots) return false;
    for (int role = 0; role < 3; role++)
        if (p[12 + role] >= font_count) return false;
    return true;
}

label_result_t label_open_why(const uint8_t *blob, size_t size, uint8_t font_count,
                              label_sheet_t **out, char *why, size_t why_size)
{
    if (why && why_size) why[0] = 0;
    if (!blob || !out) return LABEL_INVALID;
    *out = NULL;
    if (size > UINT32_MAX) return fail(why, why_size, "sheet is larger than any image");
    if (size < LABEL_HEADER_SIZE) return fail(why, why_size, "shorter than its header");

    if (memcmp(blob, LABEL_MAGIC, 8) != 0) return fail(why, why_size, "magic is not K7LBL");
    if (rd16(blob + 8) != LABEL_VERSION) return fail(why, why_size, "not sheet version 1");
    if (rd16(blob + 10) != LABEL_HEADER_SIZE) return fail(why, why_size, "header size is not 32");
    if (rd16(blob + 20) != LABEL_SPACE_W || rd16(blob + 22) != LABEL_SPACE_H)
        return fail(why, why_size, "space is not 748x359");

    const uint16_t templates = rd16(blob + 24);
    const uint16_t prims = rd16(blob + 26);
    const uint16_t slots = rd16(blob + 28);
    const uint16_t pool_size = rd16(blob + 30);

    if (templates == 0 || templates > LABEL_TEMPLATE_MAX)
        return fail(why, why_size, "no designs, or more than a sheet holds");
    if (prims > LABEL_TEMPLATE_MAX * LABEL_PRIM_MAX)
        return fail(why, why_size, "more prims than any sheet can hold");
    if (slots > LABEL_TEMPLATE_MAX * LABEL_SLOT_MAX)
        return fail(why, why_size, "more slots than any sheet can hold");

    const uint64_t want = (uint64_t)LABEL_HEADER_SIZE +
                          (uint64_t)templates * LABEL_TEMPLATE_SIZE +
                          (uint64_t)prims * LABEL_PRIM_SIZE +
                          (uint64_t)slots * LABEL_SLOT_SIZE + pool_size;
    if (rd32(blob + 12) != want || want != (uint64_t)size)
        return fail(why, why_size, "total_size disagrees with the parts and the bytes");

    if (label_crc32(blob + LABEL_HEADER_SIZE, size - LABEL_HEADER_SIZE) != rd32(blob + 16))
        return fail(why, why_size, "checksum mismatch");

    label_sheet_t *s = (label_sheet_t *)calloc(1, sizeof *s);
    if (!s) return LABEL_NO_MEM;
    s->blob = blob;
    s->size = size;
    s->templates = templates;
    s->prims = prims;
    s->slots = slots;
    s->pool_size = pool_size;
    s->tmpl = blob + LABEL_HEADER_SIZE;
    s->prim = s->tmpl + (size_t)templates * LABEL_TEMPLATE_SIZE;
    s->slot = s->prim + (size_t)prims * LABEL_PRIM_SIZE;
    s->pool = s->slot + (size_t)slots * LABEL_SLOT_SIZE;

    for (uint16_t t = 0; t < templates; t++) {
        if (!template_ok(s, t, font_count)) {
            free(s);
            return fail(why, why_size, "a template is out of range");
        }
    }
    for (uint16_t i = 0; i < prims; i++) {
        if (!prim_ok(s, i)) {
            free(s);
            return fail(why, why_size, "a prim is out of range");
        }
    }
    for (uint16_t i = 0; i < slots; i++) {
        if (!slot_ok(s, i)) {
            free(s);
            return fail(why, why_size, "a slot is out of range");
        }
    }

    *out = s;
    return LABEL_OK;
}

label_result_t label_open(const uint8_t *blob, size_t size, uint8_t font_count,
                          label_sheet_t **out)
{
    return label_open_why(blob, size, font_count, out, NULL, 0);
}

void label_close(label_sheet_t *s) { free(s); }

/* ------------------------------------------------------------------ reads */

uint16_t label_templates(const label_sheet_t *s) { return s ? s->templates : 0; }

uint16_t label_pick(const label_sheet_t *s, uint32_t ink_seed)
{
    if (!s || s->templates == 0) return 0;
    return (uint16_t)(ink_seed % s->templates);
}

const char *label_template_name(const label_sheet_t *s, uint16_t t)
{
    if (!s || t >= s->templates) return "";
    return (const char *)s->pool + rd32(template_at(s, t));
}

uint16_t label_prim_count(const label_sheet_t *s, uint16_t t)
{
    if (!s || t >= s->templates) return 0;
    return rd16(template_at(s, t) + 6);
}

bool label_prim_at(const label_sheet_t *s, uint16_t t, uint16_t i, label_prim_t *out)
{
    if (!s || !out || t >= s->templates) return false;
    if (i >= label_prim_count(s, t)) return false;

    const uint8_t *p = prim_at(s, rd16(template_at(s, t) + 4) + i);
    out->kind = p[0];
    out->fill = p[1];
    out->font = p[2];
    out->align = p[3];
    out->x = (int16_t)rd16(p + 4);
    out->y = (int16_t)rd16(p + 6);
    out->w = rd16(p + 8);
    out->h = rd16(p + 10);
    out->r = rd16(p + 12);
    out->rot10 = (int16_t)rd16(p + 14);
    out->literal = rd32(p + 20);
    return true;
}

const char *label_prim_text(const label_sheet_t *s, uint16_t t, uint16_t i)
{
    if (!s || t >= s->templates || i >= label_prim_count(s, t)) return NULL;
    const uint8_t *p = prim_at(s, rd16(template_at(s, t) + 4) + i);
    if (p[0] == LABEL_RECT) return NULL;
    return (const char *)s->pool + rd32(p + 16);
}

uint16_t label_slot_count(const label_sheet_t *s, uint16_t t)
{
    if (!s || t >= s->templates) return 0;
    return rd16(template_at(s, t) + 10);
}

bool label_slot_at(const label_sheet_t *s, uint16_t t, uint16_t i, label_slot_t *out)
{
    if (!s || !out || t >= s->templates) return false;
    if (i >= label_slot_count(s, t)) return false;

    const uint8_t *p = slot_at(s, rd16(template_at(s, t) + 8) + i);
    out->max = p[4];
    out->name = (const char *)s->pool + rd32(p);
    return true;
}

label_inks_t label_inks(const label_sheet_t *s, uint16_t t)
{
    label_inks_t inks = {0, 0, 0, {0, 0, 0, 0}};
    if (!s || t >= s->templates) return inks;

    const uint8_t *p = template_at(s, t);
    inks.paper = rd32(p + 16);
    inks.ink = rd32(p + 20);
    inks.rule = rd32(p + 24);
    for (int i = 0; i < 4; i++) inks.band[i] = rd32(p + 28 + i * 4);
    return inks;
}

uint8_t label_font(const label_sheet_t *s, uint16_t t, uint8_t role)
{
    if (!s || t >= s->templates || role > LABEL_FONT_CAPTION) return 0;
    return template_at(s, t)[12 + role];
}

/* ------------------------------------------------------------------ the cut */

/* One UTF-8 sequence's length from its lead byte; an invalid byte counts as one, so a
 * malformed string still terminates instead of running off the end. */
static unsigned utf8_len(uint8_t b)
{
    if (b < 0x80) return 1;
    if ((b & 0xE0) == 0xC0) return 2;
    if ((b & 0xF0) == 0xE0) return 3;
    if ((b & 0xF8) == 0xF0) return 4;
    return 1;
}

void label_cut(const char *src, unsigned max_chars, char *dst, size_t cap)
{
    if (!dst || cap == 0) return;
    if (!src) {
        dst[0] = 0;
        return;
    }

    size_t o = 0;
    unsigned chars = 0;
    for (const char *p = src; *p && chars < max_chars; chars++) {
        unsigned len = utf8_len((uint8_t)*p);
        for (unsigned k = 0; k < len; k++) {
            if (!p[k]) break;                 /* the source ends inside a sequence */
            if (o + 1 >= cap) {               /* never split the terminator */
                dst[o] = 0;
                return;
            }
            dst[o++] = p[k];
        }
        p += len;
    }
    dst[o] = 0;
}
