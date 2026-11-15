/*
 * playlist_select - see include/playlist_select.h and ADR 0095.
 *
 * The order is handed to the broadcast as Library track indices, because that is what
 * the Library's lookup answers. Resolution happens here, on demand, against the
 * generation that is published now: a row names a path, and the path is looked up the
 * same way /api/play does it. A path that is gone is counted, not removed -- the list
 * on the device is exactly what the card's file says.
 */
#include "playlist_select.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mini_icecast.h"
#include "playlist_file.h"

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static playlist_select_store_t s_store;
static bool     s_init;
static uint64_t s_id;
static char     s_name[64];
static uint32_t s_missing;

static void remember(uint64_t id, const char *name, uint32_t missing)
{
    s_id = id;
    snprintf(s_name, sizeof s_name, "%s", name ? name : "");
    s_missing = missing;
}

static void store_save(uint64_t id)
{
    if (s_store.save) s_store.save(s_store.ctx, id);
}

/* One list's rows, resolved against the Library as they are read. */
typedef struct {
    const library_t *lib;
    uint32_t        *indices;
    uint32_t         n;
    uint32_t         missing;
} resolve_t;

static bool resolve_row(void *ctx, const char *path)
{
    resolve_t *r = ctx;
    lib_node_t node = library_find(r->lib, path);
    if (node == LIB_NODE_NONE || library_kind(r->lib, node) != LIB_TRACK) {
        r->missing++;
        return true;
    }
    uint32_t index = library_track_index(r->lib, node);
    if (index == (uint32_t)LIB_NODE_NONE) {
        r->missing++;
        return true;
    }
    r->indices[r->n++] = index;
    return true;
}

/* Resolve and apply, or refuse without touching what is walking. The order is
 * published to the broadcast only once the list has resolved, so a refusal cannot
 * silence a list that was working. Called with the lock held. */
static playlist_select_result_t apply(const char *card_root, const library_t *lib,
                                      uint64_t id, bool persist)
{
    if (!id) {
        mini_icecast_set_order(NULL, 0, 0);
        remember(0, "", 0);
        if (persist) store_save(0);
        return PLAYLIST_SELECT_OK;
    }
    if (!card_root || !card_root[0] || !lib) return PLAYLIST_SELECT_NO_CARD;

    playlist_file_t list;
    playlist_file_result_t read = playlist_read_meta(card_root, id, &list);
    if (read == PLAYLIST_FILE_NOT_FOUND) return PLAYLIST_SELECT_NOT_FOUND;
    if (read == PLAYLIST_FILE_NO_CARD) return PLAYLIST_SELECT_NO_CARD;
    if (read != PLAYLIST_FILE_OK) return PLAYLIST_SELECT_NO_CARD;
    if (list.rows == 0) return PLAYLIST_SELECT_EMPTY;

    uint32_t *indices = malloc((size_t)list.rows * sizeof *indices);
    if (!indices) return PLAYLIST_SELECT_NO_MEM;

    resolve_t r = {lib, indices, 0, 0};
    if (playlist_read(card_root, id, NULL, resolve_row, &r) != PLAYLIST_FILE_OK || r.n == 0) {
        /* The file went away or changed shape between the two reads, or nothing in it is on
         * the card. A list that resolved to nothing is EMPTY, not a broken selection. */
        free(indices);
        return PLAYLIST_SELECT_EMPTY;
    }

    mini_icecast_set_order(indices, r.n, library_generation(lib));
    free(indices);
    remember(id, list.name, r.missing);
    if (persist) store_save(id);
    return PLAYLIST_SELECT_OK;
}

/* The broadcast dropped the order because a play command named a Track outside it. */
static void on_order_dropped(void *ctx)
{
    (void)ctx;
    pthread_mutex_lock(&s_lock);
    if (s_id) {
        s_id = 0;
        s_name[0] = 0;
        s_missing = 0;
        store_save(0);
    }
    pthread_mutex_unlock(&s_lock);
}

void playlist_select_init(const playlist_select_store_t *store)
{
    pthread_mutex_lock(&s_lock);
    if (store) s_store = *store;
    else memset(&s_store, 0, sizeof s_store);
    s_init = true;
    /* A fresh init is a fresh boot: whatever a previous lifecycle held is gone. */
    s_id = 0;
    s_name[0] = 0;
    s_missing = 0;
    pthread_mutex_unlock(&s_lock);
    mini_icecast_set_order_dropped_cb(on_order_dropped, NULL);
}

void playlist_select_sync(const char *card_root, const library_t *lib)
{
    pthread_mutex_lock(&s_lock);
    if (!s_init) {
        pthread_mutex_unlock(&s_lock);
        return;
    }
    /* The remembered choice is the truth; a device that has never chosen has id 0. */
    uint64_t id = s_id;
    if (!id && s_store.load) {
        uint64_t stored = 0;
        if (s_store.load(s_store.ctx, &stored) && stored) id = stored;
    }
    playlist_select_result_t r = apply(card_root, lib, id, false);
    if (id && r == PLAYLIST_SELECT_NOT_FOUND) {
        /* The card's file is gone: back to the Library's order and forget the choice rather
         * than retry it every boot. NO_CARD is not this case -- with the card out the choice
         * stands, and the walk resumes when the card is back (U3: a refusal leaves the
         * selection alone). */
        mini_icecast_set_order(NULL, 0, 0);
        remember(0, "", 0);
        store_save(0);
    } else if (id && r == PLAYLIST_SELECT_NO_CARD) {
        /* The card is out: nothing can walk, and the choice stays on it to come back with
         * it. The broadcast is handed no order until then. */
        mini_icecast_set_order(NULL, 0, 0);
    } else if (id && r == PLAYLIST_SELECT_EMPTY) {
        /* Keep the id and its name; remember how many rows are waiting for the card. */
        playlist_file_t list;
        if (playlist_read_meta(card_root, id, &list) == PLAYLIST_FILE_OK) {
            /* Nothing resolved, so every row is waiting for the card. */
            remember(id, list.name, list.rows);
        }
    }
    pthread_mutex_unlock(&s_lock);
}

playlist_select_result_t playlist_select_set(const char *card_root, const library_t *lib,
                                             uint64_t id)
{
    pthread_mutex_lock(&s_lock);
    playlist_select_result_t r = apply(card_root, lib, id, true);
    pthread_mutex_unlock(&s_lock);
    return r;
}

uint64_t playlist_select_current(void)
{
    pthread_mutex_lock(&s_lock);
    uint64_t id = s_id;
    pthread_mutex_unlock(&s_lock);
    return id;
}

void playlist_select_name(char *out, size_t size)
{
    if (!out || !size) return;
    pthread_mutex_lock(&s_lock);
    snprintf(out, size, "%s", s_name);
    pthread_mutex_unlock(&s_lock);
}

bool playlist_select_walking(void)
{
    return mini_icecast_order_active();
}

uint32_t playlist_select_missing(void)
{
    pthread_mutex_lock(&s_lock);
    uint32_t n = s_missing;
    pthread_mutex_unlock(&s_lock);
    return n;
}
