/*
 * id3.c - the picture in an MP3's ID3v2.3/2.4 APIC frame: the App's `k7label` first,
 * then the front cover.
 *
 * Only what locating needs is parsed: the tag header, the extended header when it is
 * present, and each frame's ten-byte header. The APIC frame's own header (encoding,
 * MIME, picture type, description) is read, and the picture is then returned by offset
 * and length, so the image bytes are read once, by the decoder, when it wants them.
 *
 * ID3v2.2 (three-byte frame ids, "PIC") is not handled: it is old enough that a file
 * using it is better served by the empty Artwork screen than by a third parser.
 */
#include "artwork_private.h"

#include <string.h>

#define ID3_HEAD_BYTES 10
#define ID3_FRAME_BYTES 10
#define APIC_HEAD_MAX 1024
#define MAX_FRAMES 256

/* The end of a NUL-terminated description, honouring the frame's text encoding.
 *
 * A UTF-16 string is a sequence of two-byte code units, so its terminator is a zero
 * unit at an even offset from the description's start - not any two zero bytes in a
 * row. Scanning every offset would stop on the second byte of a BOM (FF FE 00 00)
 * and cut the picture by a byte, which is exactly how a UTF-16 description corrupts
 * an otherwise valid APIC. */
static bool description_end(const uint8_t *p, size_t n, uint8_t enc, size_t *end)
{
    if (enc == 1 || enc == 2) { /* UTF-16 with BOM, or UTF-16BE: a zero code unit */
        for (size_t i = 0; i + 1 < n; i += 2) {
            if (p[i] == 0 && p[i + 1] == 0) {
                *end = i + 2;
                return true;
            }
        }
        return false;
    }
    for (size_t i = 0; i < n; i++) { /* ISO-8859-1 or UTF-8: one zero byte */
        if (p[i] == 0) {
            *end = i + 1;
            return true;
        }
    }
    return false;
}

/* Parse one APIC frame's head. Returns the picture type on success (0..20), or -1.
 * `label` says whether the description is this App's `k7label`. */
static int parse_apic(artwork_read_fn read, void *ctx, uint64_t frame_off,
                      uint32_t frame_size, artwork_ref_t *out, bool *label)
{
    if (frame_size <= 4) return -1;
    uint8_t head[APIC_HEAD_MAX];
    size_t want = frame_size < sizeof head ? frame_size : sizeof head;
    if (!read(ctx, frame_off, head, want)) return -1;

    size_t i = 0;
    uint8_t enc = head[i++];
    if (enc > 3) return -1;

    size_t mime_end = i;
    while (mime_end < want && head[mime_end] != 0) mime_end++;
    if (mime_end >= want) return -1;

    bool png;
    if (!art_mime_ok((const char *)head + i, mime_end - i, &png)) return -1;

    i = mime_end + 1;
    if (i >= want) return -1;
    uint8_t picture_type = head[i++];

    size_t desc_end;
    if (!description_end(head + i, want - i, enc, &desc_end)) return -1;

    /* The label marker is written in a one-byte encoding (the writer uses ASCII); a
     * UTF-16 description of the same word is somebody else's and is left alone. */
    bool is_label = enc != 1 && enc != 2 && desc_end > 1 &&
                    art_desc_is_label((const char *)head + i, desc_end - 1);

    uint64_t image_off = frame_off + i + desc_end;
    if (frame_size <= i + desc_end) return -1;
    uint32_t image_len = (uint32_t)(frame_size - i - desc_end);

    artwork_ref_t tmp;
    if (art_accept(read, ctx, image_off, image_len, png, &tmp) != ARTWORK_OK) return -1;
    tmp.label = is_label;
    *out = tmp;
    if (label) *label = is_label;
    return picture_type;
}

artwork_result_t art_id3_locate(artwork_read_fn read, void *ctx, uint64_t file_size,
                                artwork_ref_t *out)
{
    uint8_t h[ID3_HEAD_BYTES];
    if (!read(ctx, 0, h, sizeof h)) return ARTWORK_IO;
    if (memcmp(h, "ID3", 3) != 0) return ARTWORK_NONE;

    uint8_t major = h[3];
    if (major != 3 && major != 4) return ARTWORK_UNSUPPORTED;
    uint8_t flags = h[5];
    if (flags & 0x80) return ARTWORK_UNSUPPORTED; /* unsynchronised tag */

    uint32_t tag_size = art_syncsafe32(h + 6);
    if (tag_size == 0xFFFFFFFFu) return ARTWORK_BAD_FILE;
    uint64_t tag_end = (uint64_t)ID3_HEAD_BYTES + tag_size;
    if (file_size && tag_end > file_size) return ARTWORK_BAD_FILE;

    uint64_t pos = ID3_HEAD_BYTES;
    if (flags & 0x40) { /* extended header */
        uint8_t e[4];
        if (!read(ctx, pos, e, sizeof e)) return ARTWORK_IO;
        uint32_t ext;
        if (major == 3) {
            ext = art_be32(e);
            pos += 4 + ext; /* v2.3 size excludes its own four bytes */
        } else {
            ext = art_syncsafe32(e);
            if (ext == 0xFFFFFFFFu) return ARTWORK_BAD_FILE;
            pos += ext; /* v2.4 size includes its own four bytes */
        }
        if (pos >= tag_end) return ARTWORK_BAD_FILE;
    }

    artwork_ref_t first, front;
    bool have_first = false;
    bool have_front = false;
    bool saw_apic = false;

    for (int frame = 0; frame < MAX_FRAMES && pos + ID3_FRAME_BYTES <= tag_end; frame++) {
        uint8_t f[ID3_FRAME_BYTES];
        if (!read(ctx, pos, f, sizeof f)) return ARTWORK_IO;
        if (!f[0] && !f[1] && !f[2] && !f[3]) break; /* padding */

        uint32_t fsize;
        if (major == 3) {
            fsize = art_be32(f + 4);
        } else {
            fsize = art_syncsafe32(f + 4);
            /* A few writers put a plain integer in a v2.4 frame. If the syncsafe
             * reading runs past the tag, read it the other way. */
            if (fsize == 0xFFFFFFFFu || pos + ID3_FRAME_BYTES + fsize > tag_end)
                fsize = art_be32(f + 4);
        }
        if (fsize == 0 || pos + ID3_FRAME_BYTES + fsize > tag_end) break;

        uint8_t fflags = f[9];
        /* Compression, encryption, grouping, per-frame unsync and a data-length
         * indicator all change where the frame's payload starts. Rather than each be
         * decoded for an APIC (all are rare), such a frame is skipped. */
        bool unusable = (major == 3) ? (fflags & 0xE0) != 0
                                     : (fflags & 0x47) != 0;
        if (!unusable && memcmp(f, "APIC", 4) == 0) {
            saw_apic = true;
            artwork_ref_t tmp;
            bool label = false;
            int type = parse_apic(read, ctx, pos + ID3_FRAME_BYTES, fsize, &tmp, &label);
            if (label) { /* the App's own label is what the Card prints */
                *out = tmp;
                return ARTWORK_OK;
            }
            /* The cover is remembered, not returned: the App's label may be written
             * after it, and the label is the picture the Card prints. */
            if (type == 3 && !have_front) {
                front = tmp;
                have_front = true;
            }
            if (type >= 0 && !have_first) {
                first = tmp;
                have_first = true;
            }
        }
        pos += ID3_FRAME_BYTES + fsize;
    }

    if (have_front) {
        *out = front;
        return ARTWORK_OK;
    }
    if (have_first) {
        *out = first;
        return ARTWORK_OK;
    }
    /* An APIC frame that exists but is not a JPEG/PNG within the caps is a different
     * case from no picture at all, and worth saying so in the log. */
    return saw_apic ? ARTWORK_UNSUPPORTED : ARTWORK_NONE;
}
