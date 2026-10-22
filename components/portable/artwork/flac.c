/*
 * flac.c - the front cover in a native FLAC file's PICTURE metadata block.
 *
 * FLAC metadata blocks are a four-byte header (last flag, type, 24-bit length) and a
 * payload. PICTURE is type 6, and the payload is a sequence of big-endian lengths and
 * strings followed by the picture. Only that head is read: the block is then skipped by
 * its declared length, so a 4 MB picture costs no more than a 400-byte one.
 *
 * The Vorbis comment's base64 METADATA_BLOCK_PICTURE field is deliberately not read:
 * native FLAC files with a cover carry a PICTURE block, which is where every tagger
 * writes it, and the base64 field is an Ogg convention.
 */
#include "artwork_private.h"

#include <string.h>

#define FLAC_HEAD_MAX 1024
#define MAX_BLOCKS 128

artwork_result_t art_flac_locate(artwork_read_fn read, void *ctx, uint64_t file_size,
                                 artwork_ref_t *out)
{
    (void)file_size;
    uint8_t h[4];
    if (!read(ctx, 0, h, sizeof h)) return ARTWORK_IO;
    if (memcmp(h, "fLaC", 4) != 0) return ARTWORK_BAD_FILE;

    artwork_ref_t first;
    bool have_first = false;
    bool saw_picture = false;
    uint64_t pos = 4;

    for (int block = 0; block < MAX_BLOCKS; block++) {
        if (!read(ctx, pos, h, sizeof h)) return ARTWORK_IO;
        bool last = (h[0] & 0x80) != 0;
        uint8_t type = h[0] & 0x7F;
        uint32_t len = art_be24(h + 1);
        uint64_t data = pos + 4;

        if (type == 6) saw_picture = true;
        if (type == 6 && len >= 32) {
            uint8_t head[FLAC_HEAD_MAX];
            size_t want = len < sizeof head ? len : sizeof head;
            if (!read(ctx, data, head, want)) return ARTWORK_IO;

            uint32_t picture_type = art_be32(head);
            uint32_t mime_len = art_be32(head + 4);
            size_t o = 8;
            bool png = false;
            bool fits = (uint64_t)o + mime_len + 4 <= want &&
                        art_mime_ok((const char *)head + o, mime_len, &png);
            if (fits) {
                o += mime_len;
                uint32_t desc_len = art_be32(head + o);
                o += 4;
                /* width, height, depth, colours, then the picture's length and data */
                if ((uint64_t)o + desc_len + 20 <= want) {
                    o += desc_len;
                    uint32_t data_len = art_be32(head + o + 16);
                    uint64_t data_off = data + o + 20;
                    artwork_ref_t tmp;
                    if (art_accept(read, ctx, data_off, data_len, png, &tmp) == ARTWORK_OK) {
                        if (picture_type == 3) { /* front cover */
                            *out = tmp;
                            return ARTWORK_OK;
                        }
                        if (!have_first) {
                            first = tmp;
                            have_first = true;
                        }
                    }
                }
            }
        }

        if (last) break;
        pos = data + len;
    }

    if (have_first) {
        *out = first;
        return ARTWORK_OK;
    }
    return saw_picture ? ARTWORK_UNSUPPORTED : ARTWORK_NONE;
}
