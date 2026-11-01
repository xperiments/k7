/*
 * playlist_select - see include/playlist_select.h and ADR 0051.
 *
 * The order is handed to the broadcast as Library track indices, because that is what
 * the Library's lookup answers. Resolution happens here, on demand, against the
 * generation that is published now: a row names a path, and the path is looked up the
 * same way /api/play does it. A path that is gone is counted, not removed -- the list
 * on the device is exactly what the last image said.
 */
#include "playlist_select.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mini_icecast.h"

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

/* Resolve a list's rows into `out`. Returns how many rows the Library could resolve;
 * `missing` counts the rest. */
static uint32_t resolve(const snapshot_t *snap, const library_t *lib, const snapshot_list_t *list,
                        uint32_t *out, uint32_t *missing)
{
    uint32_t n = 0, gone = 0;
    for (uint32_t i = 0; i < list->row_count; i++) {
        const char *path = NULL;
        if (!snapshot_list_row(snap, list, i, &path) || !path || !path[0]) {
            gone++;
            continue;
        }
        lib_node_t node = library_find(lib, path);
        if (node == LIB_NODE_NONE || library_kind(lib, node) != LIB_TRACK) {
            gone++;
            continue;
        }
        uint32_t index = library_track_index(lib, node);
        if (index == (uint32_t)LIB_NODE_NONE) { /* not a track */
            gone++;
            continue;
        }
        out[n++] = index;
    }
    *missing = gone;
    return n;
}

/* Resolve and apply, or refuse without touching what is walking. The order is
 * published to the broadcast only once the list has resolved, so a refusal cannot
 * silence a list that was working. Called with the lock held. */
static playlist_select_result_t apply(const snapshot_t *snap, const library_t *lib,
                                      uint64_t id, bool persist)
{
    if (!id) {
        mini_icecast_set_order(NULL, 0, 0);
        remember(0, "", 0);
        if (persist) store_save(0);
        return PLAYLIST_SELECT_OK;
    }
    if (!snap || !lib) return PLAYLIST_SELECT_NO_SNAPSHOT;

    snapshot_list_t list;
    if (!snapshot_list_find(snap, id, &list)) return PLAYLIST_SELECT_NOT_FOUND;
    if (list.row_count == 0) return PLAYLIST_SELECT_EMPTY;

    uint32_t *indices = malloc((size_t)list.row_count * sizeof *indices);
    if (!indices) return PLAYLIST_SELECT_EMPTY;

    uint32_t missing = 0;
    uint32_t generation = library_generation(lib);
    uint32_t n = resolve(snap, lib, &list, indices, &missing);
    if (n == 0) {
        free(indices);
        return PLAYLIST_SELECT_EMPTY;
    }
    mini_icecast_set_order(indices, n, generation);
    free(indices);
    remember(id, snapshot_list_name(snap, &list), missing);
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

void playlist_select_sync(const snapshot_t *snap, const library_t *lib)
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
    playlist_select_result_t r = apply(snap, lib, id, false);
    if (id && r != PLAYLIST_SELECT_OK) {
        /* The image no longer holds it (or the Library cannot resolve a row): back to
         * the Library's order, and forget the choice rather than retry it every boot. */
        mini_icecast_set_order(NULL, 0, 0);
        remember(0, "", 0);
        store_save(0);
    }
    pthread_mutex_unlock(&s_lock);
}

playlist_select_result_t playlist_select_set(const snapshot_t *snap, const library_t *lib,
                                             uint64_t id)
{
    pthread_mutex_lock(&s_lock);
    playlist_select_result_t r = apply(snap, lib, id, true);
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

uint32_t playlist_select_missing(void)
{
    pthread_mutex_lock(&s_lock);
    uint32_t n = s_missing;
    pthread_mutex_unlock(&s_lock);
    return n;
}
