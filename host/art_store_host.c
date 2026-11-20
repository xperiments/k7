/*
 * The host's `art` partition: the file XTAPE_ART names.
 *
 * The board maps its data partition and reads the sheet in place; the host has no flash,
 * so it reads the same bytes from a file - whole or not at all on a write, like the
 * snapshot's adapter (host/board_host.c). The file is how the integration tests install a
 * pack and how a second server process proves the boot re-opens one. Absent is the normal
 * host state, not an error.
 *
 * This file is compiled into the host server (host/Makefile) and is the portable label
 * store's only host implementation.
 */
#include "label_store.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "label_pack.h"

struct label_store_image {
    uint8_t *bytes;
    size_t   size;
};

static const char *art_path(void)
{
    const char *path = getenv("XTAPE_ART");
    return path && path[0] ? path : NULL;
}

label_store_state_t label_store_image_get(label_store_image_t **out, const uint8_t **bytes,
                                          size_t *size, char *why, size_t why_size)
{
    if (out) *out = NULL;
    if (bytes) *bytes = NULL;
    if (size) *size = 0;
    if (why && why_size) why[0] = 0;

    const char *path = art_path();
    if (!path) {
        if (why) snprintf(why, why_size, "no art partition on the host");
        return LABEL_STORE_NONE;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        if (errno == ENOENT) {
            if (why) snprintf(why, why_size, "the art partition holds no pack");
            return LABEL_STORE_NONE;
        }
        if (why) snprintf(why, why_size, "XTAPE_ART could not be opened");
        return LABEL_STORE_UNREADABLE;
    }
    long n = -1;
    if (fseek(f, 0, SEEK_END) == 0) n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0 || n > (long)LABEL_PACK_MAX) {
        fclose(f);
        if (why) snprintf(why, why_size, "XTAPE_ART is not a readable pack file");
        return LABEL_STORE_UNREADABLE;
    }
    if (n == 0) {
        fclose(f);
        if (why) snprintf(why, why_size, "the art partition holds no pack");
        return LABEL_STORE_NONE;
    }

    label_store_image_t *image = calloc(1, sizeof *image);
    uint8_t *buf = malloc((size_t)n);
    if (!image || !buf) {
        free(image);
        free(buf);
        fclose(f);
        if (why) snprintf(why, why_size, "no memory for the art image");
        return LABEL_STORE_UNREADABLE;
    }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) {
        free(image);
        free(buf);
        if (why) snprintf(why, why_size, "XTAPE_ART could not be read");
        return LABEL_STORE_UNREADABLE;
    }
    image->bytes = buf;
    image->size = got;
    if (out) *out = image;
    if (bytes) *bytes = image->bytes;
    if (size) *size = image->size;
    return LABEL_STORE_PRESENT;
}

void label_store_image_put(label_store_image_t *image)
{
    if (!image) return;
    free(image->bytes);
    free(image);
}

bool label_store_write(const uint8_t *bytes, size_t len, char *why, size_t why_size)
{
    if (why && why_size) why[0] = 0;
    const char *path = art_path();
    if (!path) {
        if (why) snprintf(why, why_size, "no art partition on the host");
        return false;
    }
    if (!bytes || len < LABEL_PACK_HEADER_SIZE || len > LABEL_PACK_MAX) {
        if (why) snprintf(why, why_size, "the pack is not between %u and %u bytes",
                          (unsigned)LABEL_PACK_HEADER_SIZE, (unsigned)LABEL_PACK_MAX);
        return false;
    }

    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        if (why) snprintf(why, why_size, "cannot write %s", tmp);
        return false;
    }
    bool ok = fwrite(bytes, 1, len, f) == len;
    ok = fclose(f) == 0 && ok;
    if (!ok || rename(tmp, path) != 0) {
        remove(tmp);
        if (why) snprintf(why, why_size, "cannot replace %s", path);
        return false;
    }
    return true;
}

bool label_store_erase(char *why, size_t why_size)
{
    if (why && why_size) why[0] = 0;
    const char *path = art_path();
    if (!path) return true; /* nothing to erase is the factory state */
    if (remove(path) != 0 && errno != ENOENT) {
        if (why) snprintf(why, why_size, "cannot erase %s", path);
        return false;
    }
    return true;
}
