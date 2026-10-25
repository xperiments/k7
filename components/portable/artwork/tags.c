/*
 * tags.c - the group and song names a Track's container carries.
 *
 * MP3: ID3v2.3/2.4 text frames. TPE1 is the artist (the group the screen shows), TPE2
 * the album artist as its fallback, and TIT2 the song. The four text encodings the
 * format defines are decoded to UTF-8 here: ISO-8859-1 (0), UTF-16 with a BOM (1),
 * UTF-16BE (2) and UTF-8 (3). Only the first string of a multi-value frame is kept.
 *
 * FLAC: the Vorbis comment block (type 4). ARTIST, with ALBUMARTIST as the fallback,
 * and TITLE. Keys are ASCII and case-insensitive; values are already UTF-8. The block
 * is capped at VORBIS_READ_MAX bytes, so a base64 METADATA_BLOCK_PICTURE field follows
 * whatever fits rather than being read whole.
 *
 * ID3v2.2 and unsynchronised tags are refused, as in the picture reader: a file old or
 * unusual enough to use them is better served by the filename fallback than by a third
 * parser. A tag with neither frame is valid and returns OK with empty fields.
 */
#include "artwork_private.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define ID3_HEAD_BYTES 10
#define ID3_FRAME_BYTES 10
#define ID3_TEXT_MAX 512      /* more than enough for ARTWORK_TAG_MAX bytes of UTF-8 */
#define MAX_FRAMES 256

#define VORBIS_READ_MAX 4096
#define MAX_COMMENTS 256

/* ------------------------------------------------------------------ UTF-8 out */

/* Append one code point, if it fits with the terminating NUL. */
static void utf8_put(char *out, size_t cap, size_t *o, uint32_t cp)
{
    if (cp < 0x80u) {
        if (*o + 1 >= cap) return;
        out[(*o)++] = (char)cp;
    } else if (cp < 0x800u) {
        if (*o + 2 >= cap) return;
        out[(*o)++] = (char)(0xC0u | (cp >> 6));
        out[(*o)++] = (char)(0x80u | (cp & 0x3Fu));
    } else if (cp < 0x10000u) {
        if (*o + 3 >= cap) return;
        out[(*o)++] = (char)(0xE0u | (cp >> 12));
        out[(*o)++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[(*o)++] = (char)(0x80u | (cp & 0x3Fu));
    } else {
        if (*o + 4 >= cap) return;
        out[(*o)++] = (char)(0xF0u | (cp >> 18));
        out[(*o)++] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
        out[(*o)++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[(*o)++] = (char)(0x80u | (cp & 0x3Fu));
    }
}

/* Trim what taggers leave behind at either end: spaces, tabs and the CR/LF some
 * Windows writers put in a frame. */
static void text_trim(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                 s[n - 1] == '\r' || s[n - 1] == '\n')) {
        s[--n] = 0;
    }
    size_t lead = 0;
    while (s[lead] == ' ' || s[lead] == '\t' || s[lead] == '\r' || s[lead] == '\n') lead++;
    if (lead) memmove(s, s + lead, n - lead + 1);
}

static uint32_t decode_utf16_unit(const uint8_t *p, bool little)
{
    return little ? (uint32_t)p[0] | ((uint32_t)p[1] << 8)
                  : ((uint32_t)p[0] << 8) | (uint32_t)p[1];
}

/* Decode a frame's text payload into `out`. `enc` is the ID3 encoding byte. Returns
 * true when the encoding is one this module knows. */
static bool decode_text(uint8_t enc, const uint8_t *p, size_t n, char *out, size_t cap)
{
    size_t o = 0;
    out[0] = 0;

    if (enc == 0) { /* ISO-8859-1 */
        for (size_t i = 0; i < n && p[i] != 0; i++) utf8_put(out, cap, &o, p[i]);
    } else if (enc == 3) { /* UTF-8; copy the bytes up to the terminator */
        /* A UTF-8 BOM is legal in the frame and is not part of the name. */
        if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
            p += 3;
            n -= 3;
        }
        for (size_t i = 0; i < n && p[i] != 0; i++) {
            if (o + 1 >= cap) break;
            out[o++] = (char)p[i];
        }
    } else if (enc == 1 || enc == 2) { /* UTF-16 with BOM, or UTF-16BE */
        size_t i = 0;
        bool little = false;
        if (enc == 1) {
            if (n < 2) return true;
            if (p[0] == 0xFF && p[1] == 0xFE) {
                little = true;
                i = 2;
            } else if (p[0] == 0xFE && p[1] == 0xFF) {
                i = 2;
            } else {
                /* A BOM is required by the format; a writer that omitted it is most
                 * usefully read as the same byte order as the platform's taggers. */
                little = true;
            }
        }
        while (i + 1 < n) {
            uint32_t u = decode_utf16_unit(p + i, little);
            i += 2;
            if (u == 0) break;
            if (u >= 0xD800u && u <= 0xDBFFu && i + 1 < n) {
                uint32_t lo = decode_utf16_unit(p + i, little);
                if (lo >= 0xDC00u && lo <= 0xDFFFu) {
                    i += 2;
                    u = 0x10000u + ((u - 0xD800u) << 10) + (lo - 0xDC00u);
                }
            }
            utf8_put(out, cap, &o, u);
        }
    } else {
        return false;
    }

    out[o < cap ? o : cap - 1] = 0;
    text_trim(out);
    return true;
}

/* ------------------------------------------------------------------ ID3 */

/* One text frame, read into `out`. Returns true when a non-empty string was decoded. */
static bool id3_text_frame(artwork_read_fn read, void *ctx, uint64_t frame_off,
                           uint32_t frame_size, char *out, size_t cap)
{
    if (frame_size < 2) return false;
    uint8_t payload[ID3_TEXT_MAX];
    size_t want = frame_size < sizeof payload ? frame_size : sizeof payload;
    if (!read(ctx, frame_off, payload, want)) return false;

    uint8_t enc = payload[0];
    if (!decode_text(enc, payload + 1, want - 1, out, cap)) return false;
    return out[0] != 0;
}

artwork_result_t art_id3_tags(artwork_read_fn read, void *ctx, uint64_t file_size,
                              artwork_tags_t *out)
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
            pos += 4 + ext;
        } else {
            ext = art_syncsafe32(e);
            if (ext == 0xFFFFFFFFu) return ARTWORK_BAD_FILE;
            pos += ext;
        }
        if (pos >= tag_end) return ARTWORK_BAD_FILE;
    }

    /* TPE2 is the album artist: kept aside, and used only when TPE1 is missing, so a
     * frame order cannot decide which one wins. */
    char album_artist[ARTWORK_TAG_MAX] = { 0 };
    bool have_group = false;
    bool have_song = false;

    for (int frame = 0; frame < MAX_FRAMES && pos + ID3_FRAME_BYTES <= tag_end; frame++) {
        uint8_t f[ID3_FRAME_BYTES];
        if (!read(ctx, pos, f, sizeof f)) return ARTWORK_IO;
        if (!f[0] && !f[1] && !f[2] && !f[3]) break; /* padding */

        uint32_t fsize;
        if (major == 3) {
            fsize = art_be32(f + 4);
        } else {
            fsize = art_syncsafe32(f + 4);
            if (fsize == 0xFFFFFFFFu || pos + ID3_FRAME_BYTES + fsize > tag_end)
                fsize = art_be32(f + 4);
        }
        if (fsize == 0 || pos + ID3_FRAME_BYTES + fsize > tag_end) break;

        uint8_t fflags = f[9];
        bool unusable = (major == 3) ? (fflags & 0xE0) != 0
                                     : (fflags & 0x47) != 0;
        if (!unusable) {
            if (!have_group && memcmp(f, "TPE1", 4) == 0) {
                have_group = id3_text_frame(read, ctx, pos + ID3_FRAME_BYTES, fsize,
                                            out->group, sizeof out->group);
            } else if (!album_artist[0] && memcmp(f, "TPE2", 4) == 0) {
                id3_text_frame(read, ctx, pos + ID3_FRAME_BYTES, fsize,
                               album_artist, sizeof album_artist);
            } else if (!have_song && memcmp(f, "TIT2", 4) == 0) {
                have_song = id3_text_frame(read, ctx, pos + ID3_FRAME_BYTES, fsize,
                                           out->song, sizeof out->song);
            }
        }

        if (have_group && have_song) break;
        pos += ID3_FRAME_BYTES + fsize;
    }

    if (!have_group && album_artist[0]) {
        snprintf(out->group, sizeof out->group, "%s", album_artist);
    }
    return ARTWORK_OK;
}

/* ------------------------------------------------------------------ FLAC */

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool key_is(const uint8_t *p, size_t n, const char *key)
{
    size_t k = strlen(key);
    if (n != k) return false;
    for (size_t i = 0; i < n; i++) {
        if (tolower((unsigned char)p[i]) != tolower((unsigned char)key[i])) return false;
    }
    return true;
}

/* Copy a Vorbis comment value into a field. Values are UTF-8 already; like an ID3
 * frame, only the first NUL-terminated string is kept. */
static void copy_value(const uint8_t *p, size_t n, char *out, size_t cap)
{
    size_t o = 0;
    for (size_t i = 0; i < n && p[i] != 0 && o + 1 < cap; i++) out[o++] = (char)p[i];
    out[o] = 0;
    text_trim(out);
}

/* Parse the Vorbis comment payload, or as much of it as was read. A truncated
 * comment list simply stops; the fields already read stay. */
static void parse_vorbis(const uint8_t *p, size_t n, artwork_tags_t *out)
{
    if (n < 4) return;
    uint32_t vendor = le32(p);
    size_t o = 4;
    if (vendor > n - o) return;
    o += vendor;
    if (n - o < 4) return;

    uint32_t count = le32(p + o);
    o += 4;
    if (count > MAX_COMMENTS) count = MAX_COMMENTS;

    char album_artist[ARTWORK_TAG_MAX] = { 0 };
    bool have_group = false;
    bool have_song = false;

    for (uint32_t i = 0; i < count; i++) {
        if (n - o < 4) break;
        uint32_t len = le32(p + o);
        o += 4;
        if (len > n - o) break; /* the read cap cut this comment */
        const uint8_t *c = p + o;
        o += len;

        const uint8_t *eq = memchr(c, '=', len);
        if (!eq) continue;
        size_t key_len = (size_t)(eq - c);
        const char *value = (const char *)eq + 1;
        size_t value_len = len - key_len - 1;

        if (!have_group && key_is(c, key_len, "ARTIST")) {
            copy_value((const uint8_t *)value, value_len, out->group, sizeof out->group);
            have_group = out->group[0] != 0;
        } else if (!album_artist[0] && key_is(c, key_len, "ALBUMARTIST")) {
            copy_value((const uint8_t *)value, value_len, album_artist,
                       sizeof album_artist);
        } else if (!have_song && key_is(c, key_len, "TITLE")) {
            copy_value((const uint8_t *)value, value_len, out->song, sizeof out->song);
            have_song = out->song[0] != 0;
        }
    }

    if (!have_group && album_artist[0]) {
        snprintf(out->group, sizeof out->group, "%s", album_artist);
    }
}

artwork_result_t art_flac_tags(artwork_read_fn read, void *ctx, uint64_t file_size,
                               artwork_tags_t *out)
{
    (void)file_size;
    uint8_t h[4];
    if (!read(ctx, 0, h, sizeof h)) return ARTWORK_IO;
    if (memcmp(h, "fLaC", 4) != 0) return ARTWORK_BAD_FILE;

    bool saw_comments = false;
    uint64_t pos = 4;

    for (int block = 0; block < 128; block++) {
        if (!read(ctx, pos, h, sizeof h)) return ARTWORK_IO;
        bool last = (h[0] & 0x80) != 0;
        uint8_t type = h[0] & 0x7F;
        uint32_t len = art_be24(h + 1);
        uint64_t data = pos + 4;

        if (type == 4 && len) {
            saw_comments = true;
            uint8_t payload[VORBIS_READ_MAX];
            size_t want = len < sizeof payload ? len : sizeof payload;
            if (!read(ctx, data, payload, want)) return ARTWORK_IO;
            parse_vorbis(payload, want, out);
        }

        if (last) break;
        pos = data + len;
    }

    return saw_comments ? ARTWORK_OK : ARTWORK_NONE;
}

/* ------------------------------------------------------------------ entry */

artwork_result_t artwork_tags(mi_fmt_t format, artwork_read_fn read, void *ctx,
                              uint64_t file_size, artwork_tags_t *out)
{
    if (!out) return ARTWORK_BAD_FILE;
    memset(out, 0, sizeof *out);
    if (!read) return ARTWORK_BAD_FILE;

    switch (format) {
        case MI_FMT_MP3:
            return art_id3_tags(read, ctx, file_size, out);
        case MI_FMT_FLAC:
            return art_flac_tags(read, ctx, file_size, out);
        default:
            return ARTWORK_NONE;
    }
}
