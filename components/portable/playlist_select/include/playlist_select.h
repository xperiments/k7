#pragma once
/*
 * playlist_select - which Playlist the broadcast walks, and the order that means.
 *
 * A Playlist is stored in the Snapshot (ADR 0051); the broadcast knows nothing about
 * playlists, only about an order of Library track indices. This module is the seam:
 * it resolves a list's rows to those indices, hands the array to the broadcast, keeps
 * the choice, and lets the page clear it.
 *
 * The choice is remembered through an injected store, because NVS is the device's and
 * this module compiles on the host too: the appliance wires NVS, the host tests wire
 * memory. A stored id the current image no longer holds is dropped, not guessed.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "library.h"
#include "snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLAYLIST_SELECT_OK = 0,
    PLAYLIST_SELECT_NO_SNAPSHOT, /* this device has no usable Snapshot */
    PLAYLIST_SELECT_NOT_FOUND,   /* no such list id in the image */
    PLAYLIST_SELECT_EMPTY,       /* the list has no row the Library can resolve */
    PLAYLIST_SELECT_NO_MEM,      /* the order could not be built */
} playlist_select_result_t;

/* The persistence seam. Both are optional; with none the selection lives until reboot. */
typedef struct {
    void *ctx;
    /* Read the remembered id into `id`; false when there is none. */
    bool (*load)(void *ctx, uint64_t *id);
    /* Remember `id`, or 0 for "none". */
    bool (*save)(void *ctx, uint64_t id);
} playlist_select_store_t;

/* Register the store and become the listener for a dropped order: when a play command
 * names a Track outside the list being walked, the broadcast drops the order to the
 * Library's, and this module clears the choice so status never claims a list that is
 * not playing. Call once, before the first sync. */
void playlist_select_init(const playlist_select_store_t *store);

/* Re-read the remembered selection against this Snapshot and Library and re-apply it.
 * Call after the Library is published and after a rescan publishes a new generation;
 * a generation change invalidates the indices the broadcast was given. */
void playlist_select_sync(const snapshot_t *snap, const library_t *lib);

/* Choose a list, or 0 for none. Resolves it, hands the broadcast the order, and stores
 * the choice. */
playlist_select_result_t playlist_select_set(const snapshot_t *snap, const library_t *lib,
                                             uint64_t id);

/* The chosen list, 0 for none. */
uint64_t    playlist_select_current(void);

/* Its name, copied; "" when none or when the buffer is too small. A copy rather than a
 * pointer because the name is written under this module's lock and read from another
 * task's stack. */
void        playlist_select_name(char *out, size_t size);

/* Whether an order is actually being walked. The choice and the walk are different facts:
 * a card that is out leaves the choice standing (so it comes back with the card) while
 * nothing walks. */
bool        playlist_select_walking(void);

/* Rows a resolution could not find on the card, for the last sync or set. */
uint32_t    playlist_select_missing(void);

#ifdef __cplusplus
}
#endif
