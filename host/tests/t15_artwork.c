/*
 * T15: the artwork locator and the cover fit, host tier.
 *
 * The parsers read through a callback, so the whole test is a byte array in and a
 * result out: no card, no LVGL, no decoder. The containers are built here by hand,
 * including the two shapes that real taggers get wrong (a v2.4 frame with a plain
 * integer size, and a description in UTF-16), so the rules are pinned by construction
 * rather than by whatever a fixture happens to contain.
 *
 * What this cannot test is the decode itself - TJpgDec runs on the board - and that
 * is stated in the screen README rather than pretended here.
 */
#include "test_util.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "artwork.h"

/* ------------------------------------------------------------------ builders */

typedef struct {
    uint8_t *p;
    size_t   n;
    size_t   cap;
} buf_t;

static void b_u8(buf_t *b, uint8_t v) { b->p[b->n++] = v; }

static void b_bytes(buf_t *b, const void *p, size_t n)
{
    memcpy(b->p + b->n, p, n);
    b->n += n;
}

static void b_be32(buf_t *b, uint32_t v)
{
    b_u8(b, (uint8_t)(v >> 24));
    b_u8(b, (uint8_t)(v >> 16));
    b_u8(b, (uint8_t)(v >> 8));
    b_u8(b, (uint8_t)v);
}

static void b_be24(buf_t *b, uint32_t v)
{
    b_u8(b, (uint8_t)(v >> 16));
    b_u8(b, (uint8_t)(v >> 8));
    b_u8(b, (uint8_t)v);
}

static void b_le32(buf_t *b, uint32_t v)
{
    b_u8(b, (uint8_t)v);
    b_u8(b, (uint8_t)(v >> 8));
    b_u8(b, (uint8_t)(v >> 16));
    b_u8(b, (uint8_t)(v >> 24));
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static void put_syncsafe(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)((v >> 21) & 0x7F);
    p[1] = (uint8_t)((v >> 14) & 0x7F);
    p[2] = (uint8_t)((v >> 7) & 0x7F);
    p[3] = (uint8_t)(v & 0x7F);
}

/* A JPEG head with a SOF0, which is where the sniffer reads the size. */
static size_t jpeg_head(uint8_t *p, uint16_t w, uint16_t h)
{
    size_t n = 0;
    p[n++] = 0xFF; p[n++] = 0xD8;
    p[n++] = 0xFF; p[n++] = 0xE0; p[n++] = 0x00; p[n++] = 0x10;
    memcpy(p + n, "JFIF\0", 5); n += 5;
    p[n++] = 1; p[n++] = 1; p[n++] = 0; p[n++] = 0;
    p[n++] = 1; p[n++] = 1; p[n++] = 0; p[n++] = 0;
    p[n++] = 0; p[n++] = 0; p[n++] = 0; p[n++] = 0; /* thumbnail w, h and pad to 16 */
    p[n++] = 0xFF; p[n++] = 0xC0; p[n++] = 0x00; p[n++] = 0x11; p[n++] = 0x08;
    p[n++] = (uint8_t)(h >> 8); p[n++] = (uint8_t)h;
    p[n++] = (uint8_t)(w >> 8); p[n++] = (uint8_t)w;
    p[n++] = 3;
    for (int c = 0; c < 3; c++) { p[n++] = (uint8_t)(c + 1); p[n++] = 0x22; p[n++] = 0; }
    p[n++] = 0xFF; p[n++] = 0xD9;
    return n;
}

/* A PNG head with an IHDR, enough for the signature and the size. */
static size_t png_head(uint8_t *p, uint32_t w, uint32_t h)
{
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    memcpy(p, sig, 8);
    put_be32(p + 8, 13);
    memcpy(p + 12, "IHDR", 4);
    put_be32(p + 16, w);
    put_be32(p + 20, h);
    p[24] = 8; p[25] = 6; p[26] = 0; p[27] = 0; p[28] = 0;
    memset(p + 29, 0, 4); /* the CRC is not checked */
    return 33;
}

/* Append an APIC frame to a tag body. `plain_size` writes a v2.4 frame with a plain
 * integer size, which some writers do. Returns the image's offset in the body. */
static size_t add_apic(buf_t *body, int major, uint8_t type, const char *mime,
                       const uint8_t *img, size_t img_len, bool plain_size,
                       uint8_t enc)
{
    b_bytes(body, "APIC", 4);
    size_t size_at = body->n;
    b_be32(body, 0);
    b_u8(body, 0);
    b_u8(body, 0);

    size_t data = body->n;
    b_u8(body, enc);
    b_bytes(body, mime, strlen(mime) + 1);
    b_u8(body, type);
    if (enc == 1) { b_u8(body, 'B'); b_u8(body, 0); } /* a two-byte description */
    b_u8(body, 0);
    if (enc == 1) b_u8(body, 0);
    b_bytes(body, img, img_len);

    uint32_t fsize = (uint32_t)(body->n - data);
    if (major == 3 || plain_size) put_be32(body->p + size_at, fsize);
    else put_syncsafe(body->p + size_at, fsize);
    return data;
}

/* The same APIC with a description, in a one-byte encoding: the shape the App's own
 * writer produces (`k7label`). */
static size_t add_apic_described(buf_t *body, int major, uint8_t type, const char *mime,
                                 const char *desc, const uint8_t *img, size_t img_len)
{
    b_bytes(body, "APIC", 4);
    size_t size_at = body->n;
    b_be32(body, 0);
    b_u8(body, 0);
    b_u8(body, 0);

    size_t data = body->n;
    b_u8(body, 0); /* ISO-8859-1 */
    b_bytes(body, mime, strlen(mime) + 1);
    b_u8(body, type);
    b_bytes(body, desc, strlen(desc) + 1);
    b_bytes(body, img, img_len);

    put_be32(body->p + size_at, (uint32_t)(body->n - data));
    return data;
}

/* Wrap `body` in an ID3v2 header. */
static size_t wrap_id3(uint8_t *out, size_t cap, int major, uint8_t flags,
                       const uint8_t *body, size_t body_len)
{
    if (body_len + 10 > cap) return 0;
    memcpy(out, "ID3", 3);
    out[3] = (uint8_t)major;
    out[4] = 0;
    out[5] = flags;
    put_syncsafe(out + 6, (uint32_t)body_len);
    memcpy(out + 10, body, body_len);
    return body_len + 10;
}

/* A FLAC file: fLaC, STREAMINFO, then the given blocks. */
static size_t wrap_flac(uint8_t *out, size_t cap, const uint8_t *blocks, size_t blocks_len)
{
    if (blocks_len + 4 + 34 > cap) return 0;
    memcpy(out, "fLaC", 4);
    /* STREAMINFO: last=0, type=0, length 34, all zero. */
    out[4] = 0x00;
    out[5] = 0x00; out[6] = 0x00; out[7] = 0x22;
    memset(out + 8, 0, 34);
    memcpy(out + 42, blocks, blocks_len);
    return 42 + blocks_len;
}

/* Append a FLAC PICTURE block. Returns the picture's absolute offset in `out`. */
static size_t add_flac_picture(buf_t *blocks, bool last, uint8_t ptype, const char *mime,
                               uint32_t w, uint32_t h, const uint8_t *img, size_t img_len)
{
    size_t start = blocks->n;
    uint32_t body = (uint32_t)(4 + 4 + strlen(mime) + 4 + 16 + 4 + img_len);
    b_u8(blocks, (uint8_t)((last ? 0x80 : 0) | 0x06));
    b_be24(blocks, body);
    b_be32(blocks, ptype);
    b_be32(blocks, (uint32_t)strlen(mime));
    b_bytes(blocks, mime, strlen(mime));
    b_be32(blocks, 0); /* description length */
    b_be32(blocks, w);
    b_be32(blocks, h);
    b_be32(blocks, 24); /* depth */
    b_be32(blocks, 0);  /* colours */
    b_be32(blocks, (uint32_t)img_len);
    size_t img_at = blocks->n;
    b_bytes(blocks, img, img_len);
    (void)start;
    return img_at;
}

/* The same PICTURE block with a description. */
static size_t add_flac_picture_described(buf_t *blocks, bool last, uint8_t ptype,
                                         const char *mime, const char *desc,
                                         uint32_t w, uint32_t h,
                                         const uint8_t *img, size_t img_len)
{
    uint32_t body = (uint32_t)(4 + 4 + strlen(mime) + 4 + strlen(desc) + 16 + 4 + img_len);
    b_u8(blocks, (uint8_t)((last ? 0x80 : 0) | 0x06));
    b_be24(blocks, body);
    b_be32(blocks, ptype);
    b_be32(blocks, (uint32_t)strlen(mime));
    b_bytes(blocks, mime, strlen(mime));
    b_be32(blocks, (uint32_t)strlen(desc));
    b_bytes(blocks, desc, strlen(desc));
    b_be32(blocks, w);
    b_be32(blocks, h);
    b_be32(blocks, 24); /* depth */
    b_be32(blocks, 0);  /* colours */
    b_be32(blocks, (uint32_t)img_len);
    size_t img_at = blocks->n;
    b_bytes(blocks, img, img_len);
    return img_at;
}

/* Append an ID3 text frame. `text` is the already-encoded payload after the encoding
 * byte, so a UTF-16 case can hand in its BOM and code units. */
static void add_text_frame(buf_t *body, int major, const char *id, uint8_t enc,
                           const uint8_t *text, size_t text_len, bool plain_size)
{
    b_bytes(body, id, 4);
    size_t size_at = body->n;
    b_be32(body, 0);
    b_u8(body, 0);
    b_u8(body, 0);
    size_t data = body->n;
    b_u8(body, enc);
    b_bytes(body, text, text_len);
    uint32_t fsize = (uint32_t)(body->n - data);
    if (major == 3 || plain_size) put_be32(body->p + size_at, fsize);
    else put_syncsafe(body->p + size_at, fsize);
}

/* Append a FLAC VORBIS_COMMENT block (type 4). */
static void add_flac_comments(buf_t *blocks, bool last, const char *const *comments, int n)
{
    static const char vendor[] = "xtape-test";
    uint32_t body = (uint32_t)(4 + sizeof vendor - 1 + 4);
    for (int i = 0; i < n; i++) body += 4 + (uint32_t)strlen(comments[i]);

    b_u8(blocks, (uint8_t)((last ? 0x80 : 0) | 0x04));
    b_be24(blocks, body);
    /* Vorbis comments are little-endian, unlike FLAC's own header fields. */
    b_le32(blocks, (uint32_t)sizeof vendor - 1);
    b_bytes(blocks, vendor, sizeof vendor - 1);
    b_le32(blocks, (uint32_t)n);
    for (int i = 0; i < n; i++) {
        uint32_t len = (uint32_t)strlen(comments[i]);
        b_le32(blocks, len);
        b_bytes(blocks, comments[i], len);
    }
}

/* ------------------------------------------------------------------ reader */

typedef struct {
    const uint8_t *p;
    size_t         n;
} mem_t;

static bool mem_read(void *ctx, uint64_t off, void *dst, size_t n)
{
    mem_t *m = ctx;
    if (off > m->n || n > m->n - (size_t)off) return false;
    memcpy(dst, m->p + (size_t)off, n);
    return true;
}

/* ------------------------------------------------------------------ tests */

static uint8_t g_body[2 * 1024 * 1024];
static uint8_t g_file[2 * 1024 * 1024];

static void test_id3_v23_front(void)
{
    uint8_t jpg[128];
    size_t jpg_len = jpeg_head(jpg, 500, 500);

    buf_t body = { g_body, 0, sizeof g_body };
    size_t body_off = add_apic(&body, 3, 3, "image/jpeg", jpg, jpg_len, false, 0);
    size_t total = wrap_id3(g_file, sizeof g_file, 3, 0, body.p, body.n);
    CHECK(total > 0, "the tag was built");

    mem_t m = { g_file, total };
    artwork_ref_t ref;
    artwork_result_t r = artwork_locate(MI_FMT_MP3, mem_read, &m, total, &ref);
    CHECK_EQ(r, ARTWORK_OK, "v2.3 front cover");
    CHECK_EQ(ref.offset, 10 + body_off + (1 + (long)strlen("image/jpeg") + 1 + 1 + 1),
             "image offset");
    CHECK_EQ(ref.size, jpg_len, "image size");
    CHECK_EQ(ref.png, 0, "JPEG, not PNG");
    CHECK_EQ(ref.width, 500, "width from the SOF");
    CHECK_EQ(ref.height, 500, "height from the SOF");
    CHECK_EQ(ref.label, 0, "a front cover is not the App's label");
}

static void test_id3_v24_png(void)
{
    uint8_t png[64];
    size_t png_len = png_head(png, 600, 400);

    buf_t body = { g_body, 0, sizeof g_body };
    size_t body_off = add_apic(&body, 4, 3, "image/png", png, png_len, false, 0);
    size_t total = wrap_id3(g_file, sizeof g_file, 4, 0, body.p, body.n);

    mem_t m = { g_file, total };
    artwork_ref_t ref;
    artwork_result_t r = artwork_locate(MI_FMT_MP3, mem_read, &m, total, &ref);
    CHECK_EQ(r, ARTWORK_OK, "v2.4 PNG front cover");
    CHECK_EQ(ref.offset, 10 + body_off + (1 + (long)strlen("image/png") + 1 + 1 + 1),
             "PNG image offset");
    CHECK_EQ(ref.png, 1, "PNG");
    CHECK_EQ(ref.width, 600, "PNG width");
    CHECK_EQ(ref.height, 400, "PNG height");
}

static void test_id3_prefers_front(void)
{
    uint8_t jpg_back[128], jpg_front[128], png[64];
    size_t back_len = jpeg_head(jpg_back, 300, 300);
    size_t front_len = jpeg_head(jpg_front, 700, 700);
    size_t png_len = png_head(png, 100, 100);

    buf_t body = { g_body, 0, sizeof g_body };
    add_apic(&body, 3, 4, "image/jpeg", jpg_back, back_len, false, 0);  /* back cover */
    add_apic(&body, 3, 3, "image/jpeg", jpg_front, front_len, false, 0); /* front */
    add_apic(&body, 3, 3, "image/png", png, png_len, false, 0);        /* later front */
    size_t total = wrap_id3(g_file, sizeof g_file, 3, 0, body.p, body.n);

    mem_t m = { g_file, total };
    artwork_ref_t ref;
    CHECK_EQ(artwork_locate(MI_FMT_MP3, mem_read, &m, total, &ref), ARTWORK_OK,
             "a front cover is found");
    CHECK_EQ(ref.width, 700, "the first front cover wins, not the later one nor the back");
}

static void test_id3_label_wins(void)
{
    uint8_t cover[128], label[128];
    size_t cover_len = jpeg_head(cover, 700, 700);
    size_t label_len = jpeg_head(label, 402, 188);

    buf_t body = { g_body, 0, sizeof g_body };
    add_apic(&body, 3, 3, "image/jpeg", cover, cover_len, false, 0); /* front cover */
    add_apic_described(&body, 3, 0, "image/jpeg", "k7label", label, label_len);
    size_t total = wrap_id3(g_file, sizeof g_file, 3, 0, body.p, body.n);

    mem_t m = { g_file, total };
    artwork_ref_t ref;
    CHECK_EQ(artwork_locate(MI_FMT_MP3, mem_read, &m, total, &ref), ARTWORK_OK,
             "a label beside a front cover is found");
    CHECK_EQ(ref.label, 1, "the label is the picture the screen prints");
    CHECK_EQ(ref.width, 402, "at the print's width");
    CHECK_EQ(ref.height, 188, "and height");
}

static void test_id3_no_cover(void)
{
    buf_t body = { g_body, 0, sizeof g_body };
    b_bytes(&body, "TIT2", 4);
    b_be32(&body, 6);
    b_u8(&body, 0); b_u8(&body, 0);   /* frame flags */
    b_u8(&body, 0);                    /* encoding */
    b_bytes(&body, "Title", 5);
    size_t total = wrap_id3(g_file, sizeof g_file, 3, 0, body.p, body.n);

    mem_t m = { g_file, total };
    artwork_ref_t ref;
    CHECK_EQ(artwork_locate(MI_FMT_MP3, mem_read, &m, total, &ref), ARTWORK_NONE,
             "a tag with no APIC has no artwork");
}

static void test_id3_v24_plain_size(void)
{
    uint8_t jpg[128];
    size_t jpg_len = jpeg_head(jpg, 512, 512);
    buf_t body = { g_body, 0, sizeof g_body };
    add_apic(&body, 4, 3, "image/jpeg", jpg, jpg_len, true, 0); /* plain size in a v2.4 frame */
    size_t total = wrap_id3(g_file, sizeof g_file, 4, 0, body.p, body.n);

    mem_t m = { g_file, total };
    artwork_ref_t ref;
    CHECK_EQ(artwork_locate(MI_FMT_MP3, mem_read, &m, total, &ref), ARTWORK_OK,
             "a v2.4 frame with a plain size still parses");
    CHECK_EQ(ref.width, 512, "its size is the plain one");
}

static void test_id3_utf16_description(void)
{
    uint8_t jpg[128];
    size_t jpg_len = jpeg_head(jpg, 640, 480);
    buf_t body = { g_body, 0, sizeof g_body };
    size_t body_off = add_apic(&body, 3, 3, "image/jpeg", jpg, jpg_len, false, 1); /* UTF-16 */
    size_t total = wrap_id3(g_file, sizeof g_file, 3, 0, body.p, body.n);

    mem_t m = { g_file, total };
    artwork_ref_t ref;
    CHECK_EQ(artwork_locate(MI_FMT_MP3, mem_read, &m, total, &ref), ARTWORK_OK,
             "a UTF-16 description is skipped");
    /* 1 encoding + MIME and its NUL + 1 type + a 2-byte BOM + a 2-byte terminator */
    CHECK_EQ(ref.offset, 10 + body_off + 1 + (long)strlen("image/jpeg") + 1 + 1 + 2 + 2,
             "offset past the UTF-16 description");
}

static void test_id3_caps(void)
{
    uint8_t jpg[128];
    size_t jpg_len = jpeg_head(jpg, 1000, 1000); /* PNG-only cap, so a JPEG this size is fine */
    uint8_t png[64];
    size_t png_len = png_head(png, 900, 400);    /* over the PNG edge cap */

    buf_t body = { g_body, 0, sizeof g_body };
    add_apic(&body, 3, 3, "image/png", png, png_len, false, 0);
    size_t total = wrap_id3(g_file, sizeof g_file, 3, 0, body.p, body.n);
    mem_t m = { g_file, total };
    artwork_ref_t ref;
    CHECK_EQ(artwork_locate(MI_FMT_MP3, mem_read, &m, total, &ref), ARTWORK_UNSUPPORTED,
             "a PNG over 800 px is refused");

    /* A JPEG of 1000x1000 is accepted: TJpgDec scales it. */
    body.n = 0;
    add_apic(&body, 3, 3, "image/jpeg", jpg, jpg_len, false, 0);
    total = wrap_id3(g_file, sizeof g_file, 3, 0, body.p, body.n);
    m.p = g_file; m.n = total;
    CHECK_EQ(artwork_locate(MI_FMT_MP3, mem_read, &m, total, &ref), ARTWORK_OK,
             "a JPEG over 800 px is accepted");

    /* A non-picture APIC is UNSUPPORTED, not NONE. */
    body.n = 0;
    add_apic(&body, 3, 3, "image/webp", jpg, jpg_len, false, 0);
    total = wrap_id3(g_file, sizeof g_file, 3, 0, body.p, body.n);
    m.p = g_file; m.n = total;
    CHECK_EQ(artwork_locate(MI_FMT_MP3, mem_read, &m, total, &ref), ARTWORK_UNSUPPORTED,
             "an APIC that is neither JPEG nor PNG is unsupported");
}

static void test_id3_bad(void)
{
    mem_t m = { (const uint8_t *)"not an mp3", 10 };
    artwork_ref_t ref;
    CHECK_EQ(artwork_locate(MI_FMT_MP3, mem_read, &m, 10, &ref), ARTWORK_NONE,
             "a file with no ID3 tag has no artwork");

    uint8_t jpg[64];
    size_t jpg_len = jpeg_head(jpg, 100, 100);
    buf_t body = { g_body, 0, sizeof g_body };
    add_apic(&body, 3, 3, "image/jpeg", jpg, jpg_len, false, 0);
    size_t total = wrap_id3(g_file, sizeof g_file, 3, 0, body.p, body.n);
    /* The header says the tag is larger than the file. */
    m.p = g_file; m.n = total;
    CHECK_EQ(artwork_locate(MI_FMT_MP3, mem_read, &m, total - 5, &ref), ARTWORK_BAD_FILE,
             "a tag larger than the file is refused");
}

static void test_flac_front(void)
{
    uint8_t png[64];
    size_t png_len = png_head(png, 500, 500);
    buf_t blocks = { g_body, 0, sizeof g_body };
    add_flac_picture(&blocks, false, 4, "image/png", 500, 500, png, png_len); /* back */
    size_t front_at = add_flac_picture(&blocks, true, 3, "image/png", 500, 500, png, png_len);
    size_t total = wrap_flac(g_file, sizeof g_file, blocks.p, blocks.n);

    mem_t m = { g_file, total };
    artwork_ref_t ref;
    CHECK_EQ(artwork_locate(MI_FMT_FLAC, mem_read, &m, total, &ref), ARTWORK_OK,
             "a FLAC PICTURE is found");
    CHECK_EQ(ref.offset, 42 + front_at, "the front cover's offset");
    CHECK_EQ(ref.png, 1, "PNG");
    CHECK_EQ(ref.width, 500, "width");
}

static void test_flac_label_wins(void)
{
    uint8_t cover[128], label[128];
    size_t cover_len = jpeg_head(cover, 700, 700);
    size_t label_len = jpeg_head(label, 402, 188);

    buf_t blocks = { g_body, 0, sizeof g_body };
    add_flac_picture(&blocks, false, 3, "image/jpeg", 700, 700, cover, cover_len);
    add_flac_picture_described(&blocks, true, 0, "image/jpeg", "k7label",
                               402, 188, label, label_len);
    size_t total = wrap_flac(g_file, sizeof g_file, blocks.p, blocks.n);

    mem_t m = { g_file, total };
    artwork_ref_t ref;
    CHECK_EQ(artwork_locate(MI_FMT_FLAC, mem_read, &m, total, &ref), ARTWORK_OK,
             "a label beside a front cover is found");
    CHECK_EQ(ref.label, 1, "the label is the picture the screen prints");
    CHECK_EQ(ref.width, 402, "at the print's width");
}

static void test_flac_no_picture(void)
{
    buf_t blocks = { g_body, 0, sizeof g_body };
    /* A VORBIS_COMMENT block only, marked last. */
    b_u8(&blocks, 0x80 | 0x04);
    b_be24(&blocks, 4);
    b_be32(&blocks, 0);
    size_t total = wrap_flac(g_file, sizeof g_file, blocks.p, blocks.n);
    mem_t m = { g_file, total };
    artwork_ref_t ref;
    CHECK_EQ(artwork_locate(MI_FMT_FLAC, mem_read, &m, total, &ref), ARTWORK_NONE,
             "no PICTURE block, no artwork");
}

static void test_flac_bad_magic(void)
{
    mem_t m = { (const uint8_t *)"OggS", 4 };
    artwork_ref_t ref;
    CHECK_EQ(artwork_locate(MI_FMT_FLAC, mem_read, &m, 4, &ref), ARTWORK_BAD_FILE,
             "a non-FLAC file is refused");
    CHECK_EQ(artwork_locate(MI_FMT_WAV, mem_read, &m, 4, &ref), ARTWORK_NONE,
             "a WAV has no artwork");
}

/* ------------------------------------------------------------------ tags */

static void test_id3_tags_latin1(void)
{
    buf_t body = { g_body, 0, sizeof g_body };
    add_text_frame(&body, 3, "TPE1", 0, (const uint8_t *)"The Beatles", 11, false);
    add_text_frame(&body, 3, "TIT2", 0, (const uint8_t *)"Come Together", 13, false);
    size_t total = wrap_id3(g_file, sizeof g_file, 3, 0, body.p, body.n);

    mem_t m = { g_file, total };
    artwork_tags_t tags;
    CHECK_EQ(artwork_tags(MI_FMT_MP3, mem_read, &m, total, &tags), ARTWORK_OK,
             "the v2.3 text tags are read");
    CHECK(strcmp(tags.group, "The Beatles") == 0, "the group is TPE1: '%s'", tags.group);
    CHECK(strcmp(tags.song, "Come Together") == 0, "the song is TIT2: '%s'", tags.song);
}

static void test_id3_tags_utf8(void)
{
    static const uint8_t group[] = "\xEF\xBB\xBF" "Caf\xC3\xA9 Tacvba";
    static const uint8_t song[] = "Eres";
    buf_t body = { g_body, 0, sizeof g_body };
    add_text_frame(&body, 4, "TPE1", 3, group, sizeof group - 1, false);
    add_text_frame(&body, 4, "TIT2", 3, song, sizeof song - 1, false);
    size_t total = wrap_id3(g_file, sizeof g_file, 4, 0, body.p, body.n);

    mem_t m = { g_file, total };
    artwork_tags_t tags;
    CHECK_EQ(artwork_tags(MI_FMT_MP3, mem_read, &m, total, &tags), ARTWORK_OK,
             "a v2.4 UTF-8 tag is read");
    CHECK(strcmp(tags.group, "Café Tacvba") == 0, "UTF-8 passes through, BOM dropped: '%s'", tags.group);
    CHECK(strcmp(tags.song, "Eres") == 0, "and the song too");
}

static void test_id3_tags_utf16(void)
{
    /* "Björk" as UTF-16LE with a BOM: B j ö r k, each a code unit. */
    static const uint8_t group[] = {
        0xFF, 0xFE,
        'B', 0, 'j', 0, 0xF6, 0, 'r', 0, 'k', 0,
    };
    buf_t body = { g_body, 0, sizeof g_body };
    add_text_frame(&body, 3, "TPE1", 1, group, sizeof group, false);
    size_t total = wrap_id3(g_file, sizeof g_file, 3, 0, body.p, body.n);

    mem_t m = { g_file, total };
    artwork_tags_t tags;
    CHECK_EQ(artwork_tags(MI_FMT_MP3, mem_read, &m, total, &tags), ARTWORK_OK,
             "a UTF-16 tag is read");
    CHECK(strcmp(tags.group, "Björk") == 0, "UTF-16 becomes UTF-8: '%s'", tags.group);
    CHECK(tags.song[0] == 0, "a missing TIT2 leaves the song empty");
}

static void test_id3_tags_fallbacks(void)
{
    /* TPE2 before TPE1: the album artist must not win over the artist. */
    buf_t body = { g_body, 0, sizeof g_body };
    add_text_frame(&body, 3, "TPE2", 0, (const uint8_t *)"Various Artists", 15, false);
    add_text_frame(&body, 3, "TPE1", 0, (const uint8_t *)"The Band", 8, false);
    size_t total = wrap_id3(g_file, sizeof g_file, 3, 0, body.p, body.n);

    mem_t m = { g_file, total };
    artwork_tags_t tags;
    CHECK_EQ(artwork_tags(MI_FMT_MP3, mem_read, &m, total, &tags), ARTWORK_OK,
             "the tag is read");
    CHECK(strcmp(tags.group, "The Band") == 0, "TPE1 beats TPE2: '%s'", tags.group);

    /* Without TPE1 the album artist is better than nothing. */
    body.n = 0;
    add_text_frame(&body, 3, "TPE2", 0, (const uint8_t *)"Various Artists", 15, false);
    total = wrap_id3(g_file, sizeof g_file, 3, 0, body.p, body.n);
    m.p = g_file; m.n = total;
    CHECK_EQ(artwork_tags(MI_FMT_MP3, mem_read, &m, total, &tags), ARTWORK_OK,
             "the fallback tag is read");
    CHECK(strcmp(tags.group, "Various Artists") == 0, "TPE2 fills an empty group: '%s'",
          tags.group);
    CHECK(tags.song[0] == 0, "and the song is still empty");
}

static void test_id3_tags_none(void)
{
    /* A tag with only a frame this module does not read. */
    static const uint8_t body[] = { 'T', 'A', 'L', 'B', 0, 0, 0, 6, 0, 0, 0, 'A', 'l', 'b', 'u', 'm' };
    size_t total = wrap_id3(g_file, sizeof g_file, 3, 0, body, sizeof body);
    mem_t m = { g_file, total };
    artwork_tags_t tags;
    CHECK_EQ(artwork_tags(MI_FMT_MP3, mem_read, &m, total, &tags), ARTWORK_OK,
             "a tag with no text still parses");
    CHECK(tags.group[0] == 0 && tags.song[0] == 0, "and both fields are empty");

    /* A file with no ID3 header at all. */
    mem_t plain = { (const uint8_t *)"not an mp3", 10 };
    CHECK_EQ(artwork_tags(MI_FMT_MP3, mem_read, &plain, 10, &tags), ARTWORK_NONE,
             "no container, no tags");
    CHECK_EQ(artwork_tags(MI_FMT_WAV, mem_read, &plain, 10, &tags), ARTWORK_NONE,
             "a WAV has no tags");
}

static void test_flac_tags(void)
{
    static const char *comments[] = {
        "TITLE=Teardrop",
        "ARTIST=Massive Attack",
        "ALBUMARTIST=Massive Attack",
    };
    buf_t blocks = { g_body, 0, sizeof g_body };
    add_flac_comments(&blocks, true, comments, 3);
    size_t total = wrap_flac(g_file, sizeof g_file, blocks.p, blocks.n);

    mem_t m = { g_file, total };
    artwork_tags_t tags;
    CHECK_EQ(artwork_tags(MI_FMT_FLAC, mem_read, &m, total, &tags), ARTWORK_OK,
             "the Vorbis comments are read");
    CHECK(strcmp(tags.group, "Massive Attack") == 0, "ARTIST is the group: '%s'", tags.group);
    CHECK(strcmp(tags.song, "Teardrop") == 0, "TITLE is the song: '%s'", tags.song);

    /* Keys are case-insensitive, and ALBUMARTIST is the fallback. */
    static const char *lower[] = {
        "title=Angel",
        "albumartist=Massive Attack",
    };
    blocks.n = 0;
    add_flac_comments(&blocks, true, lower, 2);
    total = wrap_flac(g_file, sizeof g_file, blocks.p, blocks.n);
    m.p = g_file; m.n = total;
    CHECK_EQ(artwork_tags(MI_FMT_FLAC, mem_read, &m, total, &tags), ARTWORK_OK,
             "lower-case keys are read");
    CHECK(strcmp(tags.group, "Massive Attack") == 0, "ALBUMARTIST fills the group: '%s'",
          tags.group);
    CHECK(strcmp(tags.song, "Angel") == 0, "the lower-case title is the song: '%s'",
          tags.song);

    /* A FLAC with metadata but no Vorbis comment. */
    blocks.n = 0;
    b_u8(&blocks, 0x80 | 0x06);
    b_be24(&blocks, 0);
    total = wrap_flac(g_file, sizeof g_file, blocks.p, blocks.n);
    m.p = g_file; m.n = total;
    CHECK_EQ(artwork_tags(MI_FMT_FLAC, mem_read, &m, total, &tags), ARTWORK_NONE,
             "no Vorbis comment, no tags");
}

static void test_tags_truncated(void)
{
    /* The ID3 header claims more bytes than the file holds. */
    uint8_t file[16];
    memcpy(file, "ID3\3\0\0", 6);
    put_syncsafe(file + 6, 1000);
    mem_t m = { file, sizeof file };
    artwork_tags_t tags;
    CHECK_EQ(artwork_tags(MI_FMT_MP3, mem_read, &m, sizeof file, &tags),
             ARTWORK_BAD_FILE, "a tag larger than the file is refused");

    /* A tag whose first frame runs past the tag end stops at the padding rule rather
     * than reading outside it. */
    buf_t body = { g_body, 0, sizeof g_body };
    b_bytes(&body, "TIT2", 4);
    b_be32(&body, 900);
    b_u8(&body, 0); b_u8(&body, 0);
    b_bytes(&body, "x", 1);
    size_t total = wrap_id3(g_file, sizeof g_file, 3, 0, body.p, body.n);
    m.p = g_file; m.n = total;
    CHECK_EQ(artwork_tags(MI_FMT_MP3, mem_read, &m, total, &tags), ARTWORK_OK,
             "a frame past the tag end is skipped, not read");
    CHECK(tags.song[0] == 0, "and leaves the song empty");
}

/* --------------------------------------------------------------- the fit */

static void test_scale_same_size(void)
{
    uint16_t src[6] = { 0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000, 0xF81F }; /* 3x2 */
    uint16_t dst[6] = { 0 };
    CHECK(artwork_scale_cover(src, 3, 2, dst, 3, 2), "same size scales");
    CHECK(memcmp(src, dst, sizeof src) == 0, "same size is a copy");
}

static void test_scale_crops_the_wide_side(void)
{
    /* 4x2: the top row red, the bottom blue. Into 2x2 the sides are cropped, so each
     * destination row keeps its own source row. */
    uint16_t src[8];
    for (int i = 0; i < 4; i++) { src[i] = 0xF800; src[4 + i] = 0x001F; }
    uint16_t dst[4] = { 0 };
    CHECK(artwork_scale_cover(src, 4, 2, dst, 2, 2), "the wide source scales");
    CHECK_EQ(dst[0], 0xF800, "top left is red");
    CHECK_EQ(dst[1], 0xF800, "top right is red");
    CHECK_EQ(dst[2], 0x001F, "bottom left is blue");
    CHECK_EQ(dst[3], 0x001F, "bottom right is blue");
}

static void test_scale_crops_the_tall_side(void)
{
    /* 2x4: left column red, right green. Into 2x2 the top and bottom are cropped. */
    uint16_t src[8];
    for (int y = 0; y < 4; y++) { src[y * 2] = 0xF800; src[y * 2 + 1] = 0x07E0; }
    uint16_t dst[4] = { 0 };
    CHECK(artwork_scale_cover(src, 2, 4, dst, 2, 2), "the tall source scales");
    CHECK_EQ(dst[0], 0xF800, "left is red");
    CHECK_EQ(dst[1], 0x07E0, "right is green");
}

static void test_scale_upscales(void)
{
    uint16_t src[4] = { 0xF800, 0x07E0, 0x001F, 0xFFFF };
    uint16_t dst[16] = { 0 };
    CHECK(artwork_scale_cover(src, 2, 2, dst, 4, 4), "the small source scales up");
    CHECK_EQ(dst[0], 0xF800, "the top-left corner stays red");
    CHECK_EQ(dst[15], 0xFFFF, "the bottom-right corner stays white");
}

static void test_scale_rejects_nothing(void)
{
    uint16_t dst[4];
    CHECK(!artwork_scale_cover(NULL, 2, 2, dst, 2, 2), "no source, no scale");
    uint16_t src[4] = { 0 };
    CHECK(!artwork_scale_cover(src, 0, 2, dst, 2, 2), "zero source width is refused");
    CHECK(!artwork_scale_cover(src, 2, 2, dst, 0, 2), "zero destination width is refused");
}

/* --------------------------------------------------------------- the streaming fit */

/* The widest test source is 10 px; a TJpgDec band is at most 16 rows. */
static uint8_t g_band[10 * 16 * 3];

/* TJpgDec emits RGB888 as B, G, R. */
static void put_rect(uint8_t *rgb, uint32_t w, uint32_t h,
                     uint8_t r, uint8_t g, uint8_t b)
{
    for (uint32_t i = 0; i < w * h; i++) {
        rgb[i * 3 + 0] = b;
        rgb[i * 3 + 1] = g;
        rgb[i * 3 + 2] = r;
    }
}

static uint16_t g_window_dst[603 * 385];
static uint8_t g_window_band[800 * 16 * 3];

static void test_cover_stream_quadrants(void)
{
    uint16_t dst[4] = { 0 };
    artwork_cover_t c;
    CHECK(artwork_cover_begin(&c, 4, 4, dst, 2, 2, g_band, 16), "a 4x4 source streams into 2x2");
    CHECK(c.streaming, "it is a streaming fit");
    CHECK_EQ(c.cw, 4, "no crop on a square source");

    uint8_t rect[2 * 2 * 3];
    put_rect(rect, 2, 2, 255, 0, 0);
    CHECK(artwork_cover_rect(&c, rect, 0, 0, 2, 2), "top-left rect");
    put_rect(rect, 2, 2, 0, 255, 0);
    CHECK(artwork_cover_rect(&c, rect, 2, 0, 2, 2), "top-right rect");
    put_rect(rect, 2, 2, 0, 0, 255);
    CHECK(artwork_cover_rect(&c, rect, 0, 2, 2, 2), "bottom-left rect");
    put_rect(rect, 2, 2, 255, 255, 255);
    CHECK(artwork_cover_rect(&c, rect, 2, 2, 2, 2), "bottom-right rect");
    CHECK(artwork_cover_end(&c), "the fit completes");

    CHECK_EQ(dst[0], 0xF800, "top left is red");
    CHECK_EQ(dst[1], 0x07E0, "top right is green");
    CHECK_EQ(dst[2], 0x001F, "bottom left is blue");
    CHECK_EQ(dst[3], 0xFFFF, "bottom right is white");
}

static void test_cover_stream_crops_the_sides(void)
{
    /* 6x4: the outer columns are white, the inner four red. A 2x2 cover crops the
     * outer columns, so every output pixel has to be red. */
    uint8_t src[6 * 4 * 3];
    for (uint32_t y = 0; y < 4; y++) {
        for (uint32_t x = 0; x < 6; x++) {
            uint8_t *p = src + (y * 6 + x) * 3;
            bool outer = (x == 0 || x == 5);
            p[0] = outer ? 255 : 0;   /* B */
            p[1] = outer ? 255 : 0;   /* G */
            p[2] = 255;               /* R */
        }
    }

    uint16_t dst[4] = { 0 };
    artwork_cover_t c;
    CHECK(artwork_cover_begin(&c, 6, 4, dst, 2, 2, g_band, 16), "a 6x4 source streams into 2x2");
    CHECK_EQ(c.cx, 1, "one column is cropped from each side");
    CHECK_EQ(c.cw, 4, "four columns are kept");
    CHECK(artwork_cover_rect(&c, src, 0, 0, 6, 4), "the whole source as one rect");
    CHECK(artwork_cover_end(&c), "the fit completes");
    for (int i = 0; i < 4; i++) CHECK_EQ(dst[i], 0xF800, "the cropped columns never show");
}

static void test_cover_stream_averages(void)
{
    uint16_t dst[1] = { 0 };
    artwork_cover_t c;
    CHECK(artwork_cover_begin(&c, 4, 4, dst, 1, 1, g_band, 16), "a 4x4 source streams into 1x1");
    uint8_t rect[4 * 4 * 3];
    put_rect(rect, 4, 4, 128, 128, 128);
    CHECK(artwork_cover_rect(&c, rect, 0, 0, 4, 4), "one rect");
    CHECK(artwork_cover_end(&c), "the fit completes");
    CHECK_EQ(dst[0], 0x8410, "128 is 16/32/16 in RGB565");
}

static void test_cover_streams_helper(void)
{
    CHECK(artwork_cover_streams(800, 480, 603, 385), "an 800x480 cover streams");
    CHECK(artwork_cover_streams(603, 385, 603, 385), "a window-sized cover streams");
    CHECK(!artwork_cover_streams(239, 236, 603, 385), "a small cover needs the upscale");
    CHECK(!artwork_cover_streams(602, 602, 603, 385), "one pixel short is an upscale");

    /* The loader decides with the helper, then calls begin with a band only when the
     * answer is true. An upscale must set the crop without a band. */
    artwork_cover_t c;
    CHECK(!artwork_cover_begin(&c, 239, 236, g_window_dst, 603, 385, NULL, 0),
          "an upscale begin needs no band");
    CHECK_EQ(c.cw, 239, "the upscale crop width");
    CHECK_EQ(c.ch, 152, "the upscale crop height");
    CHECK(!c.streaming, "it is an upscale");
}

static void test_cover_stream_refuses_disorder(void)
{
    uint16_t dst[4] = { 0 };
    artwork_cover_t c;
    CHECK(artwork_cover_begin(&c, 4, 4, dst, 2, 2, g_band, 16), "a streaming fit");
    uint8_t rect[2 * 2 * 3];
    put_rect(rect, 2, 2, 255, 0, 0);
    CHECK(artwork_cover_rect(&c, rect, 0, 2, 2, 2), "the bottom row first");
    CHECK(!artwork_cover_rect(&c, rect, 0, 0, 2, 2), "a rect that goes back is refused");
    CHECK(!artwork_cover_end(&c), "and the fit is failed");
}

static void test_cover_stream_ignores_outside_rects(void)
{
    /* 10x8 with a 2x2 cover crops to the centre 8x8 at x=1. A rectangle entirely left
     * of the crop must not advance the fit's row: the first MCU of a wide picture is
     * exactly that, and advancing over it would make the first MCU inside look
     * out of order. */
    uint16_t dst[4] = { 0 };
    artwork_cover_t c;
    CHECK(artwork_cover_begin(&c, 10, 8, dst, 2, 2, g_band, 16), "a 10x8 source streams into 2x2");
    CHECK_EQ(c.cx, 1, "the crop starts at x=1");

    uint8_t rect[10 * 4 * 3];
    put_rect(rect, 10, 4, 255, 0, 0);

    CHECK(artwork_cover_rect(&c, rect, 0, 0, 1, 4), "a rect left of the crop is ignored");
    CHECK_EQ(c.cur_dy, 0, "and does not advance the destination row");
    CHECK(artwork_cover_rect(&c, rect, 1, 0, 8, 4), "the first rect inside the crop");
    CHECK(artwork_cover_rect(&c, rect, 1, 4, 8, 4), "the second half");
    CHECK(artwork_cover_end(&c), "the fit completes");
    for (int i = 0; i < 4; i++) CHECK_EQ(dst[i], 0xF800, "every cell is red");
}

static void test_cover_window_603x385(void)
{
    /* The Case window the screen draws: (101,23) to (704,408), 603x385. An 800x480
     * picture covers it by scaling to its height and cropping 24 px from each side. */
    artwork_cover_t c;
    CHECK(artwork_cover_begin(&c, 800, 480, g_window_dst, 603, 385,
                              g_window_band, 16),
          "an 800x480 cover streams into the case window");
    CHECK_EQ(c.dst_w, 603, "window width");
    CHECK_EQ(c.dst_h, 385, "window height");
    CHECK_EQ(c.cw, 751, "the cover crops the sides");
    CHECK_EQ(c.ch, 480, "the full source height is kept");
    CHECK_EQ(c.cx, 24, "the crop is centred");
    CHECK_EQ(c.cy, 0, "and starts at the top");
}

static void test_cover_stream_refuses_upscale(void)
{
    uint16_t dst[16] = { 0 };
    artwork_cover_t c;
    CHECK(!artwork_cover_begin(&c, 2, 2, dst, 4, 4, g_band, 16),
          "a 2x2 source cannot stream into 4x4");
    CHECK(!c.streaming, "the caller has to upscale");
    CHECK_EQ(c.cw, 2, "the crop is still reported");
    uint8_t rect[2 * 2 * 3] = { 0 };
    CHECK(!artwork_cover_rect(&c, rect, 0, 0, 2, 2), "no streaming rectangle");
    CHECK(!artwork_cover_end(&c), "no streaming end");
}

int main(void)
{
    test_id3_v23_front();
    test_id3_v24_png();
    test_id3_prefers_front();
    test_id3_no_cover();
    test_id3_v24_plain_size();
    test_id3_utf16_description();
    test_id3_label_wins();
    test_id3_caps();
    test_id3_bad();

    test_flac_front();
    test_flac_label_wins();
    test_flac_no_picture();
    test_flac_bad_magic();

    test_id3_tags_latin1();
    test_id3_tags_utf8();
    test_id3_tags_utf16();
    test_id3_tags_fallbacks();
    test_id3_tags_none();
    test_flac_tags();
    test_tags_truncated();

    test_scale_same_size();
    test_scale_crops_the_wide_side();
    test_scale_crops_the_tall_side();
    test_scale_upscales();
    test_scale_rejects_nothing();

    test_cover_stream_quadrants();
    test_cover_stream_crops_the_sides();
    test_cover_stream_averages();
    test_cover_stream_ignores_outside_rects();
    test_cover_window_603x385();
    test_cover_streams_helper();
    test_cover_stream_refuses_disorder();
    test_cover_stream_refuses_upscale();

    TEST_END();
}
