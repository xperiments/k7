/*
 * shell_image - the reader; the format and its reasons are in shell_image.h.
 *
 * Every field is decoded a byte at a time rather than by casting the image to a struct:
 * the image is written by a build-time tool on a different machine, so its layout is a
 * wire format and not a C struct. It is read-only: the board copies it into PSRAM at boot
 * and the host serves a buffer, and nothing either way writes to it.
 */
#include "shell_image.h"

#include <string.h>

#define HEADER_BYTES 24
#define ENTRY_BYTES  16
#define FLAG_GZIP    0x0001u

/* A count that cannot be real is a truncated or foreign image, not a big one: the table
 * has to end inside the bytes it was handed. */
#define MAX_ENTRIES 4096u

static const uint8_t k_magic[4] = { 'X', 'T', 'S', 'P' };

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (uint16_t)(p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/* One entry, decoded. */
typedef struct {
    uint32_t name_off;
    uint16_t name_len;
    uint32_t data_off;
    uint32_t data_len;
    bool     gzip;
} entry_t;

static bool entry_at(const shell_image_t *img, uint32_t i, entry_t *e)
{
    if (i >= img->count) return false;
    size_t at = HEADER_BYTES + (size_t)i * ENTRY_BYTES;
    if (at + ENTRY_BYTES > img->size) return false;

    const uint8_t *p = img->bytes + at;
    e->name_off = rd32(p + 0);
    e->name_len = rd16(p + 4);
    e->gzip = (rd16(p + 6) & FLAG_GZIP) != 0;
    e->data_off = rd32(p + 8);
    e->data_len = rd32(p + 12);

    /* The name must live in the name table, fit, and be terminated; the blob must fit.
     * All of it is checked here, so a lookup can trust the entry it read. */
    if (e->name_off < img->names_off) return false;
    if ((size_t)e->name_off + e->name_len + 1 > img->size) return false;
    if (img->bytes[(size_t)e->name_off + e->name_len] != '\0') return false;
    if ((size_t)e->data_off + e->data_len > img->size) return false;
    return true;
}

bool shell_image_open(shell_image_t *out, const void *bytes, size_t size)
{
    if (!out) return false;
    memset(out, 0, sizeof *out);
    if (!bytes || size < HEADER_BYTES) return false;

    const uint8_t *p = (const uint8_t *)bytes;
    if (memcmp(p, k_magic, sizeof k_magic) != 0) return false;
    uint32_t version = rd32(p + 4);
    if (version != SHELL_IMAGE_VERSION) return false;

    uint32_t count = rd32(p + 8);
    if (count == 0 || count > MAX_ENTRIES) return false;
    size_t table_end = HEADER_BYTES + (size_t)count * ENTRY_BYTES;
    if (table_end > size) return false;

    /* The names start after the table and the blobs after the names; the image's own
     * arithmetic says so, and checking it is what keeps a name out of the table. */
    /* The image's own length. The partition it is mapped from is bigger than the image, so
     * the buffer's size is an upper bound and not the real one: an image whose last blob
     * runs past its stated end is truncated, and a stated end past the buffer is unreadable. */
    uint32_t declared = rd32(p + 20);
    if (declared > size) return false;
    if (declared < table_end) return false;

    uint32_t names_off = rd32(p + 12);
    if (names_off < table_end || names_off > declared) return false;
    uint32_t data_off = rd32(p + 16);
    if (data_off < names_off || data_off > declared) return false;

    shell_image_t img = { (const uint8_t *)bytes, declared, count, names_off };
    for (uint32_t i = 0; i < count; i++) {
        entry_t e;
        if (!entry_at(&img, i, &e)) return false;
    }

    *out = img;
    return true;
}

shell_image_result_t shell_image_find(const shell_image_t *img, const char *path,
                                      bool accepts_gzip, shell_blob_t *out)
{
    if (!img || !img->bytes || !path) return SHELL_IMAGE_MISSING;
    if (path[0] == '/') path++;
    size_t want = strlen(path);
    if (want == 0) return SHELL_IMAGE_MISSING;

    for (uint32_t i = 0; i < img->count; i++) {
        entry_t e;
        if (!entry_at(img, i, &e)) continue;
        if (e.name_len != want) continue;
        if (memcmp(img->bytes + e.name_off, path, want) != 0) continue;

        if (e.gzip && !accepts_gzip) return SHELL_IMAGE_PACKED_ONLY;
        if (out) {
            out->data = img->bytes + e.data_off;
            out->len = e.data_len;
            out->gzip = e.gzip;
        }
        return SHELL_IMAGE_FOUND;
    }
    return SHELL_IMAGE_MISSING;
}
