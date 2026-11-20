/*
 * label_pack - see include/label_pack.h and docs/specs/art-packs.md §1.
 *
 * The check is ordered so a truncated upload or a flipped byte is refused before the
 * sheet is opened, and the sheet's own reader is then asked the one question the pack
 * cannot answer: is this a sheet this firmware reads? Nothing is copied; the sheet is
 * opened over the pack's bytes and read in place.
 */
#include "label_pack.h"

#include <stdio.h>
#include <string.h>

/* The fields the header carries, little-endian, at the offsets art-packs.md §1 writes. */
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

/* A fixed field is UTF-8 and NUL-padded, but the editor's own caps are exactly the field's
 * width, so a full-width value has no NUL inside it. The copy is bounded either way: it
 * takes the bytes up to the first NUL or the field's end, and terminates its own buffer. */
static void copy_field(char *out, size_t cap, const uint8_t *field, size_t field_size)
{
    if (!out || cap == 0) return;
    size_t len = 0;
    while (len < field_size && field[len] != 0) len++;
    if (len > cap - 1) len = cap - 1;
    memcpy(out, field, len);
    out[len] = 0;
}

label_result_t label_pack_open_why(const uint8_t *bytes, size_t size, uint8_t font_count,
                                   label_pack_t *out, label_sheet_t **sheet,
                                   char *why, size_t why_size)
{
    if (why && why_size) why[0] = 0;
    if (!out || !sheet) return LABEL_INVALID;
    *sheet = NULL;
    memset(out, 0, sizeof *out);

    if (!bytes) return fail(why, why_size, "the pack is empty");
    if (size < LABEL_PACK_HEADER_SIZE) return fail(why, why_size, "shorter than its header");
    if (size > LABEL_PACK_MAX) return fail(why, why_size, "larger than the art partition");

    if (memcmp(bytes, LABEL_PACK_MAGIC, 8) != 0)
        return fail(why, why_size, "the magic is not K7ART");
    if (rd16(bytes + 8) != LABEL_PACK_VERSION)
        return fail(why, why_size, "not pack version 1");
    if (rd16(bytes + 10) != LABEL_PACK_HEADER_SIZE)
        return fail(why, why_size, "the header size is not 128");

    const uint32_t total = rd32(bytes + 12);
    if (total > LABEL_PACK_MAX) return fail(why, why_size, "the pack is larger than the art partition");
    if (total < LABEL_PACK_HEADER_SIZE) return fail(why, why_size, "the total size is inside the header");
    if (total > size) return fail(why, why_size, "the pack is shorter than its total size");

    const uint16_t design_count = rd16(bytes + 20);
    const uint16_t sheet_version = rd16(bytes + 22);
    const uint16_t pack_fonts = rd16(bytes + 24);
    const uint32_t sheet_offset = rd32(bytes + 28);
    const uint32_t sheet_size = rd32(bytes + 32);
    const uint32_t jcards_offset = rd32(bytes + 36);
    const uint32_t jcards_size = rd32(bytes + 40);

    if (rd16(bytes + 26) != 0 || rd32(bytes + 124) != 0)
        return fail(why, why_size, "a reserved field is not zero");
    if (design_count == 0 || design_count > LABEL_PACK_DESIGN_MAX)
        return fail(why, why_size, "the pack carries no designs, or more than 32");
    if (sheet_version != LABEL_VERSION)
        return fail(why, why_size, "not sheet version 4");
    if (pack_fonts == 0)
        return fail(why, why_size, "the pack names no font table");
    if (font_count != 0 && pack_fonts != font_count) {
        char msg[96];
        snprintf(msg, sizeof msg,
                 "the pack was built against %u fonts; this firmware has %u",
                 (unsigned)pack_fonts, (unsigned)font_count);
        return fail(why, why_size, msg);
    }

    if (sheet_offset != LABEL_PACK_HEADER_SIZE)
        return fail(why, why_size, "the sheet is not at its offset");
    if (sheet_size == 0) return fail(why, why_size, "the pack carries no sheet");
    if (jcards_offset != LABEL_PACK_HEADER_SIZE + sheet_size)
        return fail(why, why_size, "the J-cards are not at their offset");
    if (jcards_size == 0) return fail(why, why_size, "the pack carries no J-cards");
    if ((uint64_t)jcards_offset + jcards_size != total)
        return fail(why, why_size, "total_size disagrees with the sheet and the J-cards");

    if (label_crc32(bytes + LABEL_PACK_HEADER_SIZE, total - LABEL_PACK_HEADER_SIZE) !=
        rd32(bytes + 16))
        return fail(why, why_size, "the pack's checksum disagrees");

    copy_field(out->pack_id, sizeof out->pack_id, bytes + 44, LABEL_PACK_ID_MAX);
    copy_field(out->name, sizeof out->name, bytes + 60, LABEL_PACK_NAME_MAX);
    copy_field(out->author, sizeof out->author, bytes + 92, LABEL_PACK_AUTHOR_MAX);

    out->total_size = total;
    out->crc32 = rd32(bytes + 16);
    out->design_count = design_count;
    out->sheet_version = sheet_version;
    out->font_count = pack_fonts;
    out->sheet_offset = sheet_offset;
    out->sheet_size = sheet_size;
    out->jcards_offset = jcards_offset;
    out->jcards_size = jcards_size;
    out->sheet = bytes + LABEL_PACK_HEADER_SIZE;
    out->sheet_crc = 0;

    /* The sheet's reader is the authority on its own format. The pack's count has to be
     * the sheet's: the pool picks a template by index, and the App's J-cards are ordered
     * by the same list. Checked here, before the sheet's own checks, so the sentence names
     * the pack's mistake rather than the sheet's. */
    label_sheet_t *opened = NULL;
    char sheet_why[96] = {0};
    label_result_t r = label_open_why(out->sheet, sheet_size, pack_fonts, &opened,
                                      sheet_why, sizeof sheet_why);
    if (r != LABEL_OK) {
        if (r == LABEL_NO_MEM) {
            if (why && why_size) snprintf(why, why_size, "out of memory for the sheet");
            return LABEL_NO_MEM;
        }
        char msg[160];
        snprintf(msg, sizeof msg, "the sheet does not open (%s)",
                 sheet_why[0] ? sheet_why : "invalid sheet");
        return fail(why, why_size, msg);
    }
    if (label_templates(opened) != design_count) {
        label_close(opened);
        return fail(why, why_size, "design_count disagrees with the sheet");
    }
    /* Only now is there a header to read: the pack's sheet_size may be shorter than 32, in
     * which case the reader above refused it and there is no checksum to report. */
    out->sheet_crc = rd32(out->sheet + 16);

    *sheet = opened;
    return LABEL_OK;
}
