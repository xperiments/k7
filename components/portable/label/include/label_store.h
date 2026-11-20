#pragma once
/*
 * label_store - where the one user K7Art pack lives: the `art` data partition on the
 * board, a file on the host (docs/specs/art-packs.md §5).
 *
 * The pack is bytes, not a filesystem: 1 MB at 0x610000, erased and written whole, read
 * in place. This header is the seam between the portable art endpoint and whichever
 * adapter knows where those bytes are. It is declared beside the label because the sheet
 * inside the pack is what the device reads; the implementation is the board's
 * (`components/app/board_esp32`) and the host's (`host/art_store_host.c`).
 *
 * The image is owned, so one may be read out by `GET /api/art/pack` while the active one
 * stays mapped for the label. Bytes must not be written while an image of them is open;
 * the upload quiesces the label and releases the active image first.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct label_store_image label_store_image_t;

typedef enum {
    LABEL_STORE_NONE = 0,   /* no partition in this build, or the partition is erased */
    LABEL_STORE_PRESENT,    /* bytes are there; whether they are a pack is label_pack's */
    LABEL_STORE_UNREADABLE, /* the bytes are there and could not be read */
} label_store_state_t;

/* Open the pack as it stands. On LABEL_STORE_PRESENT `*out` owns an image, `*bytes` and
 * `*size` describe it, and label_store_image_put() releases it. `why` is a sentence for
 * NONE and UNREADABLE ("" otherwise). Never allocates the pack itself: the board maps
 * flash, the host reads its file. */
label_store_state_t label_store_image_get(label_store_image_t **out, const uint8_t **bytes,
                                          size_t *size, char *why, size_t why_size);

void label_store_image_put(label_store_image_t *image);

/* Erase and write the whole pack, then read it back and compare. Called on the flash
 * writer (flash_job), never from an HTTP worker's PSRAM stack. False with `why` set on a
 * partition that would not take it; the caller then re-reads the store, because a failed
 * write may have left either the old pack or nothing. */
bool label_store_write(const uint8_t *bytes, size_t len, char *why, size_t why_size);

/* Erase the pack, restoring the factory. Succeeds when there is nothing to erase. */
bool label_store_erase(char *why, size_t why_size);

#ifdef __cplusplus
}
#endif
