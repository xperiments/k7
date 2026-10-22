/*
 * artwork.c - the format sniff, the size caps and the fit.
 */
#include "artwork_private.h"

#include <string.h>

/* The sniff reads this much of the picture's head: a JPEG's SOF is normally before
 * 2 KB (after APP0/APP1), and a PNG's IHDR is at byte 16. */
#define HEAD_BYTES 2048

static bool ieq_n(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

bool art_mime_ok(const char *mime, size_t len, bool *png)
{
    if (len == 10 && ieq_n(mime, "image/jpeg", len)) { *png = false; return true; }
    if (len == 9 && ieq_n(mime, "image/jpg", len)) { *png = false; return true; }
    if (len == 9 && ieq_n(mime, "image/png", len)) { *png = true; return true; }
    return false;
}

bool art_image_info(const uint8_t *p, size_t n, art_image_info_t *out)
{
    if (!p || !out) return false;

    static const uint8_t png_sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if (n >= 24 && memcmp(p, png_sig, sizeof png_sig) == 0) {
        if (memcmp(p + 12, "IHDR", 4) != 0) return false;
        out->png = true;
        out->width = art_be32(p + 16);
        out->height = art_be32(p + 20);
        return true;
    }

    if (n >= 4 && p[0] == 0xFF && p[1] == 0xD8) {
        /* Walk the marker segments to the SOF, which carries the size. */
        size_t i = 2;
        while (i + 4 <= n) {
            if (p[i] != 0xFF) { i++; continue; }
            uint8_t m = p[i + 1];
            if (m == 0xFF) { i++; continue; }
            if (m == 0xD8 || m == 0xD9 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) {
                i += 2;
                continue;
            }
            uint32_t seg = ((uint32_t)p[i + 2] << 8) | p[i + 3];
            if (seg < 2) break;
            bool sof = (m >= 0xC0 && m <= 0xCF) && m != 0xC4 && m != 0xC8 && m != 0xCC;
            if (sof) {
                if (i + 9 > n) break;
                out->png = false;
                out->height = ((uint32_t)p[i + 5] << 8) | p[i + 6];
                out->width = ((uint32_t)p[i + 7] << 8) | p[i + 8];
                return true;
            }
            if (i + 2 + seg > n) break;
            i += 2 + seg;
        }
        /* It is a JPEG even if the SOF was outside the sniffed head. */
        out->png = false;
        out->width = 0;
        out->height = 0;
        return true;
    }
    return false;
}

artwork_result_t art_accept(artwork_read_fn read, void *ctx, uint64_t offset,
                            uint32_t size, bool declared_png, artwork_ref_t *out)
{
    if (size == 0 || size > ARTWORK_MAX_BYTES) return ARTWORK_UNSUPPORTED;

    uint8_t head[HEAD_BYTES];
    size_t want = size < sizeof head ? size : sizeof head;
    if (!read(ctx, offset, head, want)) return ARTWORK_IO;

    art_image_info_t info;
    if (!art_image_info(head, want, &info)) return ARTWORK_UNSUPPORTED;
    if (declared_png != info.png) return ARTWORK_UNSUPPORTED; /* header and MIME disagree */

    if (info.png) {
        if (info.width == 0 || info.height == 0) return ARTWORK_UNSUPPORTED;
        if (info.width > ARTWORK_MAX_EDGE || info.height > ARTWORK_MAX_EDGE)
            return ARTWORK_UNSUPPORTED;
    }

    out->offset = offset;
    out->size = size;
    out->width = info.width;
    out->height = info.height;
    out->png = info.png;
    return ARTWORK_OK;
}

artwork_result_t artwork_locate(mi_fmt_t format, artwork_read_fn read, void *ctx,
                                uint64_t file_size, artwork_ref_t *out)
{
    if (!read || !out) return ARTWORK_BAD_FILE;
    memset(out, 0, sizeof *out);
    switch (format) {
        case MI_FMT_MP3:  return art_id3_locate(read, ctx, file_size, out);
        case MI_FMT_FLAC: return art_flac_locate(read, ctx, file_size, out);
        default:          return ARTWORK_NONE;
    }
}

/* --------------------------------------------------------------------- scaling */

static inline uint32_t lerp8(uint32_t a, uint32_t b, uint32_t t)
{
    return (a * (65536u - t) + b * t) >> 16;
}

static inline uint32_t expand5(uint32_t v) { return (v << 3) | (v >> 2); }
static inline uint32_t expand6(uint32_t v) { return (v << 2) | (v >> 4); }

/* The centred crop that covers a destination, shared with the streaming fit below so
 * the two paths cannot drift. */
static void cover_crop(uint32_t src_w, uint32_t src_h, uint32_t dst_w, uint32_t dst_h,
                       uint32_t *cx, uint32_t *cy, uint32_t *cw, uint32_t *ch);

bool artwork_scale_cover(const uint16_t *src, uint32_t src_w, uint32_t src_h,
                         uint16_t *dst, uint32_t dst_w, uint32_t dst_h)
{
    if (!src || !dst || src_w == 0 || src_h == 0 || dst_w == 0 || dst_h == 0) return false;

    /* The source rectangle that covers the destination, centred. */
    uint32_t cw, ch, cx, cy;
    cover_crop(src_w, src_h, dst_w, dst_h, &cx, &cy, &cw, &ch);

    /* Same size as the crop: a row copy, no interpolation. */
    if (cw == dst_w && ch == dst_h) {
        for (uint32_t y = 0; y < ch; y++) {
            memcpy(dst + (size_t)y * dst_w, src + (size_t)(cy + y) * src_w + cx,
                   (size_t)cw * 2);
        }
        return true;
    }

    for (uint32_t y = 0; y < dst_h; y++) {
        /* Continuous source position of the destination pixel's centre, 16.16. */
        uint64_t s = (uint64_t)(2u * y + 1u) * ch * 65536u / (2u * dst_h);
        int64_t sy = (int64_t)s - 32768;
        uint32_t maxy = (ch > 1 ? ch - 1 : 0);
        if (sy < 0) sy = 0;
        if ((uint64_t)sy > (uint64_t)maxy << 16) sy = (int64_t)maxy << 16;
        uint32_t iy = (uint32_t)((uint64_t)sy >> 16);
        uint32_t fy = (uint32_t)sy & 0xFFFF;
        uint32_t iy1 = (iy + 1 < ch) ? iy + 1 : iy;

        for (uint32_t x = 0; x < dst_w; x++) {
            uint64_t t = (uint64_t)(2u * x + 1u) * cw * 65536u / (2u * dst_w);
            int64_t sx = (int64_t)t - 32768;
            uint32_t maxx = (cw > 1 ? cw - 1 : 0);
            if (sx < 0) sx = 0;
            if ((uint64_t)sx > (uint64_t)maxx << 16) sx = (int64_t)maxx << 16;
            uint32_t ix = (uint32_t)((uint64_t)sx >> 16);
            uint32_t fx = (uint32_t)sx & 0xFFFF;
            uint32_t ix1 = (ix + 1 < cw) ? ix + 1 : ix;

            const uint16_t *r0 = src + (size_t)(cy + iy) * src_w + cx;
            const uint16_t *r1 = src + (size_t)(cy + iy1) * src_w + cx;
            uint16_t p00 = r0[ix], p10 = r0[ix1], p01 = r1[ix], p11 = r1[ix1];

            uint32_t r = lerp8(lerp8(expand5(p00 >> 11), expand5(p10 >> 11), fx),
                               lerp8(expand5(p01 >> 11), expand5(p11 >> 11), fx), fy);
            uint32_t g = lerp8(lerp8(expand6((p00 >> 5) & 0x3F), expand6((p10 >> 5) & 0x3F), fx),
                               lerp8(expand6((p01 >> 5) & 0x3F), expand6((p11 >> 5) & 0x3F), fx), fy);
            uint32_t b = lerp8(lerp8(expand5(p00 & 0x1F), expand5(p10 & 0x1F), fx),
                               lerp8(expand5(p01 & 0x1F), expand5(p11 & 0x1F), fx), fy);

            dst[(size_t)y * dst_w + x] =
                (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
    return true;
}

/* ------------------------------------------------------------------- streaming */

static inline uint16_t rgb565(uint32_t r, uint32_t g, uint32_t b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* The shared crop rule: the source rectangle that covers the destination, centred. */
static void cover_crop(uint32_t src_w, uint32_t src_h, uint32_t dst_w, uint32_t dst_h,
                       uint32_t *cx, uint32_t *cy, uint32_t *cw, uint32_t *ch)
{
    if ((uint64_t)src_w * dst_h > (uint64_t)src_h * dst_w) {
        *cw = (uint32_t)((uint64_t)src_h * dst_w / dst_h); /* crop the sides */
        if (*cw == 0) *cw = 1;
        if (*cw > src_w) *cw = src_w;
        *ch = src_h;
        *cx = (src_w - *cw) / 2;
        *cy = 0;
    } else {
        *ch = (uint32_t)((uint64_t)src_w * dst_h / dst_w); /* crop top and bottom */
        if (*ch == 0) *ch = 1;
        if (*ch > src_h) *ch = src_h;
        *cw = src_w;
        *cx = 0;
        *cy = (src_h - *ch) / 2;
    }
}

bool artwork_cover_streams(uint32_t src_w, uint32_t src_h,
                           uint32_t dst_w, uint32_t dst_h)
{
    if (src_w == 0 || src_h == 0 || dst_w == 0 || dst_h == 0 ||
        dst_w > ARTWORK_COVER_MAX || dst_h > ARTWORK_COVER_MAX) {
        return false;
    }
    uint32_t cx, cy, cw, ch;
    cover_crop(src_w, src_h, dst_w, dst_h, &cx, &cy, &cw, &ch);
    return cw >= dst_w && ch >= dst_h;
}

bool artwork_cover_begin(artwork_cover_t *c, uint32_t src_w, uint32_t src_h,
                         uint16_t *dst, uint32_t dst_w, uint32_t dst_h,
                         uint8_t *band, uint32_t band_rows)
{
    if (!c || !dst ||
        src_w == 0 || src_h == 0 || dst_w == 0 || dst_h == 0 ||
        dst_w > ARTWORK_COVER_MAX || dst_h > ARTWORK_COVER_MAX) {
        return false;
    }

    memset(c, 0, sizeof *c);
    c->dst = dst;
    c->dst_w = dst_w;
    c->dst_h = dst_h;
    c->src_w = src_w;
    c->src_h = src_h;
    c->band = band;
    c->band_rows = band_rows;
    cover_crop(src_w, src_h, dst_w, dst_h, &c->cx, &c->cy, &c->cw, &c->ch);
    c->streaming = c->cw >= dst_w && c->ch >= dst_h;
    if (c->streaming && (!band || band_rows == 0)) {
        c->failed = true;
        return false;
    }
    return c->streaming;
}

/* Write the destination rows up to `upto`, averaging every source pixel that mapped
 * into each cell. In streaming mode every cell is hit: cw and ch are at least the
 * destination's, so no column or row is empty. */
static void cover_flush(artwork_cover_t *c, int upto)
{
    for (; c->cur_dy <= upto && c->cur_dy < (int)c->dst_h; c->cur_dy++) {
        uint16_t *row = c->dst + (size_t)c->cur_dy * c->dst_w;
        for (uint32_t x = 0; x < c->dst_w; x++) {
            uint32_t n = c->cnt[x];
            if (n) {
                row[x] = rgb565(c->acc[0][x] / n, c->acc[1][x] / n, c->acc[2][x] / n);
            } else {
                row[x] = 0;
            }
            c->acc[0][x] = c->acc[1][x] = c->acc[2][x] = 0;
            c->cnt[x] = 0;
        }
    }
}

/* Walk the source rows buffered in this band and accumulate them. TJpgDec emits a
 * whole band - every column of its 8 or 16 rows - before the next, so a destination
 * row that spans more than one source row can only be flushed once the band is full. */
static bool cover_process_band(artwork_cover_t *c)
{
    if (c->band_bottom == 0) return true;
    if (c->band_bottom - c->band_top > c->band_rows) {
        c->failed = true;
        return false;
    }

    for (uint32_t sy = c->band_top; sy < c->band_bottom; sy++) {
        if (sy < c->cy || sy >= c->cy + c->ch) continue;

        int dy = (int)(((uint64_t)(sy - c->cy) * c->dst_h) / c->ch);
        if (dy >= (int)c->dst_h) dy = (int)c->dst_h - 1;
        if (dy < c->cur_dy) {
            c->failed = true;
            return false;
        }
        if (dy > c->cur_dy) cover_flush(c, dy - 1);

        const uint8_t *row = c->band + (size_t)(sy - c->band_top) * c->cw * 3;
        for (uint32_t x = 0; x < c->cw; x++) {
            int dx = (int)(((uint64_t)x * c->dst_w) / c->cw);
            if (dx >= (int)c->dst_w) dx = (int)c->dst_w - 1;
            const uint8_t *p = row + (size_t)x * 3;
            c->acc[0][dx] += p[2]; /* RGB888 is B, G, R */
            c->acc[1][dx] += p[1];
            c->acc[2][dx] += p[0];
            c->cnt[dx]++;
        }
    }

    c->band_top = 0;
    c->band_bottom = 0;
    return true;
}

bool artwork_cover_rect(artwork_cover_t *c, const uint8_t *rgb,
                        uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (!c || !c->streaming || !rgb || w == 0 || h == 0) return false;
    if (h > c->band_rows) {
        c->failed = true;
        return false;
    }

    /* A rectangle whose rows start at or past the band's end begins the next band:
     * the previous one is complete and can be averaged. */
    if (c->band_bottom != 0 && y >= c->band_bottom) {
        if (!cover_process_band(c)) return false;
    }

    if (c->band_bottom == 0) {
        c->band_top = y;
        c->band_bottom = y + h;
    } else {
        if (y < c->band_top) {
            c->failed = true;
            return false;
        }
        if (y + h > c->band_bottom) c->band_bottom = y + h;
        if (c->band_bottom - c->band_top > c->band_rows) {
            c->failed = true;
            return false;
        }
    }

    /* Copy the part of the rectangle inside the crop into the band, at crop-relative
     * columns and band-relative rows. A rectangle outside the crop is ignored, but it
     * still belongs to the band and extends its rows. */
    uint32_t x0 = x > c->cx ? x : c->cx;
    uint32_t x1 = x + w < c->cx + c->cw ? x + w : c->cx + c->cw;
    uint32_t y0 = y > c->cy ? y : c->cy;
    uint32_t y1 = y + h < c->cy + c->ch ? y + h : c->cy + c->ch;
    if (x0 >= x1 || y0 >= y1) return true;

    for (uint32_t sy = y0; sy < y1; sy++) {
        uint8_t *dstrow = c->band + (size_t)(sy - c->band_top) * c->cw * 3 +
                          (size_t)(x0 - c->cx) * 3;
        const uint8_t *srcrow = rgb + (size_t)(sy - y) * w * 3 +
                                (size_t)(x0 - x) * 3;
        memcpy(dstrow, srcrow, (size_t)(x1 - x0) * 3);
    }
    return true;
}

bool artwork_cover_end(artwork_cover_t *c)
{
    if (!c || !c->streaming || c->failed) return false;
    if (!cover_process_band(c)) return false;
    cover_flush(c, (int)c->dst_h - 1);
    return !c->failed;
}
