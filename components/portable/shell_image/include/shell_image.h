#pragma once
/*
 * shell_image - the Shell's bytes, as the device maps them.
 *
 * The front door serves `web/dist-shell`. On the board those bytes are the `frontdoor`
 * flash partition, read out of it once at boot into PSRAM before the panel exists: any
 * flash access takes the bus the RGB panel's scan-out draws from, which is what tore the
 * picture on every page load (ADR 0055). The page is then served from RAM.
 *
 * The partition is therefore not a filesystem. It is this image: a header, a table of
 * contents, the names, and each file's bytes exactly as they sit on disk - the text files
 * already gzipped by the build, the rest plain. Nothing here decompresses anything:
 * `gzip` is a fact about the stored bytes, and the front door turns it into a
 * `Content-Encoding` header.
 *
 * The writer is `tools/pack_shell.py`; this module is the reader, and it validates
 * everything it is handed before it answers. It is portable - no ESP-IDF, no LVGL - so
 * the host tier runs the same parser against the same bytes the board maps.
 *
 * Layout, little-endian, every offset absolute from the start of the image:
 *
 *     struct header {                 // 24 bytes, at 0
 *         char     magic[4];          // "XTSP"
 *         uint32_t version;           // SHELL_IMAGE_VERSION
 *         uint32_t count;
 *         uint32_t names_off;         // the NUL-terminated names, no leading '/'
 *         uint32_t data_off;          // the first blob
 *         uint32_t size;              // the image's own length, and the only length a
 *                                     // lookup may trust: the partition it sits in is
 *                                     // bigger, so "the bytes I was handed" is not a bound
 *     };
 *     struct entry {                  // 16 bytes each, count of them at 24
 *         uint32_t name_off;
 *         uint16_t name_len;
 *         uint16_t flags;             // bit 0: the stored bytes are gzip
 *         uint32_t data_off;
 *         uint32_t data_len;
 *     };
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHELL_IMAGE_VERSION 1

typedef struct {
    const uint8_t *bytes;
    /* The image's own length (its header's `size`), never the size of the buffer it was
     * found in. */
    size_t         size;
    uint32_t       count;
    /* Where the name table starts. Every name must live at or after it, which is what
     * makes "the table is the entries, the names come next" a checkable fact. */
    uint32_t       names_off;
} shell_image_t;

typedef struct {
    const uint8_t *data;
    size_t         len;
    /* The bytes are gzip: serve them with Content-Encoding: gzip and do not touch them. */
    bool           gzip;
} shell_blob_t;

typedef enum {
    SHELL_IMAGE_FOUND = 0,
    /* No such file in the image. */
    SHELL_IMAGE_MISSING,
    /* The file is here, but only as gzip, and this client cannot take it. */
    SHELL_IMAGE_PACKED_ONLY,
} shell_image_result_t;

/*
 * Check the header and the whole table, and adopt `bytes` as an image. Returns false -
 * and leaves `out` zeroed - for anything malformed: a wrong magic, a version this firmware
 * does not know, a stated length the buffer cannot hold or that runs past a blob, a count
 * that cannot fit, a name or a blob that runs off the end. An image is checked once at boot,
 * so a lookup never has to doubt its offsets.
 */
bool shell_image_open(shell_image_t *out, const void *bytes, size_t size);

/*
 * Look up a request path ("/assets/x.js", "/", or "index.html"). A leading '/' is
 * ignored, and the path is matched exactly - no prefixes, no traversal.
 *
 * `SHELL_IMAGE_FOUND` fills `out` with the bytes to send and whether they are gzip.
 * `SHELL_IMAGE_PACKED_ONLY` is a found file this client cannot accept: the front door
 * answers 406 for it rather than a misleading 404. `SHELL_IMAGE_MISSING` is a 404.
 */
shell_image_result_t shell_image_find(const shell_image_t *img, const char *path,
                                      bool accepts_gzip, shell_blob_t *out);

#ifdef __cplusplus
}
#endif
