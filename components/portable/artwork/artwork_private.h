#pragma once
/*
 * artwork's private helpers: big-endian reads, the read callback wrapper, and the
 * detail a picture's header carries. Shared by artwork.c, id3.c and flac.c only.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "artwork.h"

/* Big-endian integers, as both containers store them. */
static inline uint32_t art_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline uint32_t art_be24(const uint8_t *p)
{
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
}

/* ID3's 28-bit syncsafe integer: four bytes with the top bit clear. 0xFFFFFFFF when
 * the value is malformed (any top bit set). */
static inline uint32_t art_syncsafe32(const uint8_t *p)
{
    if ((p[0] | p[1] | p[2] | p[3]) & 0x80) return 0xFFFFFFFFu;
    return ((uint32_t)p[0] << 21) | ((uint32_t)p[1] << 14) |
           ((uint32_t)p[2] << 7) | (uint32_t)p[3];
}

/* The picture's format and, for PNG, the size its decoder needs. `png` is set from the
 * file's magic, not from a declared MIME. */
typedef struct {
    bool     png;
    uint32_t width;  /* 0 when the header does not say */
    uint32_t height;
} art_image_info_t;

/* Sniff `n` bytes at the start of the picture. Returns false when it is neither a JPEG
 * nor a PNG, or when `n` is too short to tell. */
bool art_image_info(const uint8_t *p, size_t n, art_image_info_t *out);

/* True when `mime` names one of the two formats this module decodes. `mime` is not
 * NUL-terminated; `len` bounds it. */
bool art_mime_ok(const char *mime, size_t len, bool *png);

/* True when a picture's description is this App's label marker, `k7label`. Case is
 * folded: a tagger that upper-cases a description is still naming the same picture. The
 * description is the one-byte text both containers carry; a UTF-16 description is not
 * this App's, so it never matches. */
bool art_desc_is_label(const char *desc, size_t len);

/* The common checks once a candidate's offset and length are known: the MIME, the
 * magic, the encoded-size cap and, for PNG, the dimension cap. Reads up to the first
 * 24 bytes of the picture for the sniff. */
artwork_result_t art_accept(artwork_read_fn read, void *ctx, uint64_t offset,
                            uint32_t size, bool declared_png, artwork_ref_t *out);

/* The two container readers. */
artwork_result_t art_id3_locate(artwork_read_fn read, void *ctx, uint64_t file_size,
                                artwork_ref_t *out);
artwork_result_t art_flac_locate(artwork_read_fn read, void *ctx, uint64_t file_size,
                                 artwork_ref_t *out);

/* The two text-tag readers, in the same containers. */
artwork_result_t art_id3_tags(artwork_read_fn read, void *ctx, uint64_t file_size,
                              artwork_tags_t *out);
artwork_result_t art_flac_tags(artwork_read_fn read, void *ctx, uint64_t file_size,
                               artwork_tags_t *out);
