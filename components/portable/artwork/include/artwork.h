#pragma once
/*
 * artwork - what a Track's container says about it: its picture, and the text tags
 * that name the group and the song.
 *
 * Two containers carry a picture: MP3 in an ID3v2.3/2.4 APIC frame, FLAC in a native
 * PICTURE metadata block. Both are located here by reading a few headers and returning
 * the picture's offset and length, so the bytes themselves are never held twice: the
 * board adapter reads them into a decode buffer only when it is about to decode.
 *
 * The same tags carry the names the screen puts over the cassette: TPE1 (artist) and
 * TIT2 (title) in an MP3, ARTIST/ALBUMARTIST and TITLE in a FLAC Vorbis comment. They
 * are read here through the same callback, into fixed buffers, so the display never
 * walks a container and the broadcast never reads a file for the screen.
 *
 * The module reads through a callback rather than stdio, so the host build tests the
 * parsers against a byte array and the board passes fseek/fread. It has no LVGL and no
 * ESP-IDF includes.
 *
 * The caps are the ones the decoders impose. The screen only decodes JPEG, but a PNG
 * is still located and reported (the loader refuses it with a log): its dimensions are
 * capped at ARTWORK_MAX_EDGE because a PNG is decoded at full size whenever a later
 * surface does support it. A JPEG's dimensions do not bound memory - TJpgDec walks its
 * MCUs and the loader samples them straight into the window buffer - so only the
 * encoded size is capped, at ARTWORK_MAX_BYTES.
 */
#include <stdbool.h>
#include <stdint.h>

#include "audio_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ARTWORK_MAX_BYTES (1024u * 1024u)
#define ARTWORK_MAX_EDGE  800u

typedef enum {
    ARTWORK_OK = 0,      /* found: `out` carries the picture's offset and size */
    ARTWORK_NONE,        /* the file is well formed but carries no picture */
    ARTWORK_UNSUPPORTED, /* a picture is there, but not a JPEG/PNG within the caps */
    ARTWORK_BAD_FILE,    /* the container is malformed or truncated */
    ARTWORK_IO,          /* the read callback failed */
} artwork_result_t;

/* Reads exactly `n` bytes at `offset`. Returns false on a short read or an error. */
typedef bool (*artwork_read_fn)(void *ctx, uint64_t offset, void *dst, size_t n);

typedef struct {
    uint64_t offset; /* absolute offset of the encoded picture in the file */
    uint32_t size;   /* encoded bytes */
    uint32_t width;  /* from the container when it carries them, else the header */
    uint32_t height;
    bool     png;    /* false = JPEG */
    /* The App's worn label - the picture this project's own writer describes
     * `k7label` - and not the Track's own cover. It is the print itself (402x188), so
     * the screen draws it 1:1 in the Card's window instead of fitting it to the
     * cassette's 603x385 shape. */
    bool     label;
} artwork_ref_t;

/* Locate the picture a Track is printed with, by its Source format. WAV has none.
 * The order is the App's: its own label first, then the front cover, then the first
 * usable picture - a card written here shows the label it was written with, and a
 * card from anywhere else still shows its front cover.
 * `file_size` is used only to reject a header that claims to be larger than the file;
 * pass 0 when it is not known. */
artwork_result_t artwork_locate(mi_fmt_t format, artwork_read_fn read, void *ctx,
                                uint64_t file_size, artwork_ref_t *out);

/* The Track's text tags, as UTF-8. Both buffers are always NUL-terminated and both are
 * empty when the container carries no such tag; a caller that has no better text falls
 * back to the filename and the directory. */
#define ARTWORK_TAG_MAX 96

typedef struct {
    char group[ARTWORK_TAG_MAX]; /* MP3 TPE1/TIT2 -> TPE2; FLAC ARTIST -> ALBUMARTIST */
    char song[ARTWORK_TAG_MAX];  /* MP3 TIT2; FLAC TITLE */
} artwork_tags_t;

/* Read the group and song tags, by Source format. WAV has none.
 *
 * ARTWORK_OK means the container was read and `out` is valid, even if both fields are
 * empty. ARTWORK_NONE means the file carries no tag this module reads. The other
 * results are the container's: a malformed or truncated tag, an unsupported ID3
 * version/unsync bit, or a failed read. `file_size` is used the same way as in
 * artwork_locate(). */
artwork_result_t artwork_tags(mi_fmt_t format, artwork_read_fn read, void *ctx,
                              uint64_t file_size, artwork_tags_t *out);

/* Scale-to-cover and centre-crop a source RGB565 image into a destination of exactly
 * dst_w x dst_h pixels. The source's aspect ratio is kept: the overflowing axis is
 * cropped, never squeezed. Returns false for a zero size or a null buffer. */
bool artwork_scale_cover(const uint16_t *src, uint32_t src_w, uint32_t src_h,
                         uint16_t *dst, uint32_t dst_w, uint32_t dst_h);

/* The streaming cover fit: the same "cover" rule, applied to a JPEG as TJpgDec emits
 * it, so no full-size RGB buffer is ever allocated. The decoder hands back rectangles
 * of RGB888 in MCU order: a whole horizontal band of MCUs - every column of 8 or 16
 * source rows - before the next band. The fit copies each band into a caller-owned
 * row buffer, then walks it row by row, box-averaging each destination cell. The
 * destination may be smaller than the source (the common case); when it is larger,
 * begin() returns false and the caller decodes the crop into a buffer and uses
 * artwork_scale_cover() for the smooth upscale.
 *
 * The caller owns the workspace: the band buffer must hold `cw * band_rows` pixels of
 * RGB888, and band_rows must be at least the decoder's MCU height (16 for TJpgDec's
 * 2x2 MCU). The accumulators take about 10 KB at ARTWORK_COVER_MAX, so the loader keeps
 * the struct in BSS and the band in PSRAM. */
#define ARTWORK_COVER_MAX      640
#define ARTWORK_COVER_BAND_ROWS 16

typedef struct {
    uint16_t *dst;
    uint32_t  dst_w, dst_h;
    uint32_t  src_w, src_h;
    uint32_t  cx, cy, cw, ch; /* the cover crop, in source pixels */
    uint8_t  *band;           /* cw x band_rows RGB888, caller-owned */
    uint32_t  band_rows;
    uint32_t  band_top;       /* first source row of the band being filled */
    uint32_t  band_bottom;    /* one past its last row; 0 = no band */
    uint32_t  acc[3][ARTWORK_COVER_MAX];
    uint32_t  cnt[ARTWORK_COVER_MAX];
    int       cur_dy;
    bool      streaming;
    bool      failed;
} artwork_cover_t;

/* True when the source's crop covers the destination, so the fit can stream and the
 * caller must supply the band. False means the source is smaller and the caller decodes
 * the crop into a buffer and upscales it. */
bool artwork_cover_streams(uint32_t src_w, uint32_t src_h,
                           uint32_t dst_w, uint32_t dst_h);

/* Set up a fit. Returns true when the source can stream into `dst`; false when it is
 * smaller than the destination (the crop is still set, for the caller's upscale) or
 * when a size is zero/over the cap. A streaming fit needs `band` and `band_rows`; an
 * upscale may pass NULL/0, because it never touches the band. */
bool artwork_cover_begin(artwork_cover_t *c, uint32_t src_w, uint32_t src_h,
                         uint16_t *dst, uint32_t dst_w, uint32_t dst_h,
                         uint8_t *band, uint32_t band_rows);

/* One decoded rectangle of RGB888 at (x, y), w x h. Rectangles must arrive in MCU
 * order; a rectangle that goes backwards returns false and marks the fit failed. */
bool artwork_cover_rect(artwork_cover_t *c, const uint8_t *rgb,
                        uint32_t x, uint32_t y, uint32_t w, uint32_t h);

/* Flush the last band and destination rows. Returns false if the fit failed. */
bool artwork_cover_end(artwork_cover_t *c);

#ifdef __cplusplus
}
#endif
