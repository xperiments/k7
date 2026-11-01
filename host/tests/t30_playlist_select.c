/*
 * T30: playlist_select, host tier.
 *
 * The selection module is tested against a real Library built from a temporary tree and
 * a real version 2 Snapshot built byte by byte here, with the broadcast stubbed: this
 * test is about resolution, refusal and remembering, not about audio. What the order
 * does once the broadcast has it is the integration suite's business (the hosting server
 * plus /api/playlists/select and the stream's own advance).
 *
 * The stub is the seam, not a fork: playlist_select only ever calls set_order and the
 * dropped callback, so the two functions here are the whole contract it uses.
 */
#include "test_util.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "library.h"
#include "playlist_select.h"
#include "snapshot.h"

/* ------------------------------------------------------------- the broadcast stub */

static uint32_t g_order[64];
static uint32_t g_order_n;
static uint32_t g_order_gen;
static int      g_order_calls;
static void   (*g_dropped)(void *ctx);
static void    *g_dropped_ctx;

void mini_icecast_set_order(const uint32_t *indices, uint32_t count, uint32_t generation)
{
    g_order_calls++;
    g_order_n = count > 64 ? 64 : count;
    for (uint32_t i = 0; i < g_order_n; i++) g_order[i] = indices[i];
    g_order_gen = generation;
}

bool mini_icecast_order_active(void) { return g_order_n > 0; }
bool mini_icecast_order_contains(uint32_t track_index)
{
    for (uint32_t i = 0; i < g_order_n; i++)
        if (g_order[i] == track_index) return true;
    return g_order_n == 0;
}
void mini_icecast_set_order_dropped_cb(void (*cb)(void *ctx), void *ctx)
{
    g_dropped = cb;
    g_dropped_ctx = ctx;
}

/* ------------------------------------------------------------- fixtures */

static char *temp_root(void)
{
    char tmpl[] = "/tmp/xtpselXXXXXX";
    char *p = mkdtemp(tmpl);
    if (!p) {
        printf("FATAL: mkdtemp failed\n");
        exit(1);
    }
    return strdup(p);
}

static void mkdir_at(const char *root, const char *rel)
{
    char path[LIB_PATH_MAX];
    snprintf(path, sizeof path, "%s/%s", root, rel);
    if (mkdir(path, 0755) != 0) {
        printf("FATAL: mkdir %s\n", path);
        exit(1);
    }
}

static void file_at(const char *root, const char *rel, size_t size)
{
    char path[LIB_PATH_MAX];
    snprintf(path, sizeof path, "%s/%s", root, rel);
    int f = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (f < 0) {
        printf("FATAL: cannot create %s\n", path);
        exit(1);
    }
    for (size_t i = 0; i < size; i++)
        if (write(f, "x", 1) != 1) {
            printf("FATAL: cannot write %s\n", path);
            exit(1);
        }
    close(f);
}

/* ------------------------------------------------------------- the image */

static uint32_t crc32_ref(const uint8_t *p, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static void put16(uint8_t *p, size_t at, uint16_t v)
{
    p[at] = (uint8_t)v;
    p[at + 1] = (uint8_t)(v >> 8);
}
static void put32(uint8_t *p, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[at + i] = (uint8_t)(v >> (8 * i));
}
static void put64(uint8_t *p, size_t at, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[at + i] = (uint8_t)(v >> (8 * i));
}

/* A version 2 image with no measurement rows and one list. The rows are paths; the
 * pool is the list's name followed by each path. */
static uint8_t *build_v2(size_t *out_n, const char *name, uint64_t id, const char *const *paths,
                         uint32_t npaths)
{
    size_t pool_n = strlen(name) + 1;
    for (uint32_t i = 0; i < npaths; i++) pool_n += strlen(paths[i]) + 1;
    size_t total = 80 + pool_n + 32 + (size_t)npaths * 4;
    uint8_t *img = calloc(1, total);
    if (!img) abort();

    memcpy(img, "XTAPESNP", 8);
    put16(img, 8, 2);
    put16(img, 10, 80);
    put32(img, 12, (uint32_t)total);
    put32(img, 16, 0);            /* no measurement rows */
    put32(img, 20, 32);
    put64(img, 24, 1767225600ull); /* generation */
    put32(img, 32, (uint32_t)pool_n);
    put32(img, 36, 0);            /* crc, patched below */
    put32(img, 40, 300);
    put32(img, 44, 2);
    put64(img, 48, 0x0123456789ABCDEFull);
    put16(img, 64, 1);            /* list_count */
    put16(img, 66, 32);
    put16(img, 68, 4);
    put32(img, 72, npaths);

    size_t at = 80;
    memcpy(img + at, name, strlen(name) + 1);
    at += strlen(name) + 1;
    size_t name_off = 0; /* offsets are into the pool, not into the image */
    uint32_t *path_off = malloc(sizeof(uint32_t) * (npaths ? npaths : 1));
    for (uint32_t i = 0; i < npaths; i++) {
        path_off[i] = (uint32_t)(at - 80);
        memcpy(img + at, paths[i], strlen(paths[i]) + 1);
        at += strlen(paths[i]) + 1;
    }

    /* the one list record */
    put64(img, at, id);
    put32(img, at + 8, (uint32_t)name_off);
    put32(img, at + 12, 0);
    put32(img, at + 16, npaths);
    put64(img, at + 20, 1767225600ull);
    at += 32;
    for (uint32_t i = 0; i < npaths; i++, at += 4) put32(img, at, path_off[i]);

    put32(img, 36, crc32_ref(img + 80, total - 80));
    free(path_off);
    *out_n = total;
    return img;
}

/* ------------------------------------------------------------- store */

static uint64_t g_stored;
static bool     g_store_has;
static int      g_saves;

static bool store_load(void *ctx, uint64_t *id)
{
    (void)ctx;
    if (!g_store_has) return false;
    *id = g_stored;
    return true;
}

static bool store_save(void *ctx, uint64_t id)
{
    (void)ctx;
    g_saves++;
    g_stored = id;
    g_store_has = id != 0;
    return true;
}

/* ------------------------------------------------------------- the tests */

int main(void)
{
    char *root = temp_root();
    mkdir_at(root, "A");
    mkdir_at(root, "B");
    file_at(root, "A/one.mp3", 10);
    file_at(root, "B/two.mp3", 20);
    file_at(root, "B/three.mp3", 30);

    library_cfg_t cfg = {.music_dir = root, .build_json = false};
    library_t *lib = NULL;
    CHECK_EQ(library_open(&cfg, &lib), LIB_OK, "the fixture tree opens");
    CHECK_EQ(library_track_count(lib), 3, "three tracks");

    const char *const ALL[] = {"A/one.mp3", "B/two.mp3", "B/three.mp3"};
    const char *const SOME[] = {"A/one.mp3", "B/gone.mp3"};
    static const char *const NONE[] = {NULL};
    size_t n = 0;
    uint8_t *img = build_v2(&n, "Late night", 0xAAAA000000000001ull, ALL, 3);
    snapshot_t *snap = NULL;
    CHECK_EQ(snapshot_open(img, n, &snap), SNAPSHOT_OK, "the image opens");
    CHECK_EQ(snapshot_lists(snap), 1, "one list");

    g_store_has = false;
    g_saves = 0;
    playlist_select_store_t store = {NULL, store_load, store_save};
    playlist_select_init(&store);

    /* nothing chosen: the Library plays, and no order is sent */
    g_order_calls = 0;
    playlist_select_sync(snap, lib);
    CHECK_EQ(playlist_select_current(), 0, "no selection");
    CHECK_EQ(g_order_n, 0, "no order");
    char name[64] = {0};
    playlist_select_name(name, sizeof name);
    CHECK(strcmp(name, "") == 0, "no name");

    /* choose it: the whole list, in order */
    CHECK_EQ(playlist_select_set(snap, lib, 0xAAAA000000000001ull), PLAYLIST_SELECT_OK,
             "selecting the list");
    CHECK_EQ(playlist_select_current(), 0xAAAA000000000001ull, "the id is remembered");
    playlist_select_name(name, sizeof name);
    CHECK(strcmp(name, "Late night") == 0, "the name is kept");
    CHECK_EQ(g_order_n, 3, "three tracks in the order");
    CHECK_EQ(playlist_select_missing(), 0, "nothing missing");
    CHECK_EQ(g_order_gen, library_generation(lib), "the order carries its generation");
    CHECK_EQ(g_saves, 1, "the choice is persisted once");
    CHECK(g_stored == 0xAAAA000000000001ull, "the stored id");
    /* the order is Library track indices, in list order */
    uint32_t expect = 0;
    for (uint32_t i = 0; i < 3; i++) {
        const char *p = ALL[i];
        lib_node_t node = library_find(lib, p);
        uint32_t got = g_order[i];
        uint32_t want = library_track_index(lib, node);
        CHECK_EQ(got, want, "row resolves in order");
        expect++;
    }
    CHECK_EQ(expect, 3, "three rows checked");

    /* an unknown id is a refusal, and the old order stays */
    CHECK_EQ(playlist_select_set(snap, lib, 0xAAAA0000000000FFull), PLAYLIST_SELECT_NOT_FOUND,
             "an unknown id is refused");
    CHECK_EQ(playlist_select_current(), 0xAAAA000000000001ull, "the refusal changed nothing");
    CHECK_EQ(g_order_n, 3, "the order is still the selected one");

    /* a row whose file is gone counts, and the rest still plays */
    size_t n2 = 0;
    uint8_t *img2 = build_v2(&n2, "Half gone", 0xBBBB000000000002ull, SOME, 2);
    snapshot_t *snap2 = NULL;
    CHECK_EQ(snapshot_open(img2, n2, &snap2), SNAPSHOT_OK, "the second image opens");
    CHECK_EQ(playlist_select_set(snap2, lib, 0xBBBB000000000002ull), PLAYLIST_SELECT_OK,
             "a list with a missing row still selects");
    CHECK_EQ(g_order_n, 1, "one resolved row");
    CHECK_EQ(playlist_select_missing(), 1, "one missing row is counted");

    /* an empty list is refused, and nothing is walking */
    size_t n3 = 0;
    uint8_t *img3 = build_v2(&n3, "Empty", 0xCCCC000000000003ull, NONE, 0);
    snapshot_t *snap3 = NULL;
    CHECK_EQ(snapshot_open(img3, n3, &snap3), SNAPSHOT_OK, "the empty image opens");
    CHECK_EQ(playlist_select_set(snap3, lib, 0xCCCC000000000003ull), PLAYLIST_SELECT_EMPTY,
             "an empty list is refused");
    CHECK_EQ(playlist_select_current(), 0xBBBB000000000002ull,
             "a refused selection leaves the working one alone");
    CHECK_EQ(g_order_n, 1, "and its order is untouched");

    /* none: back to the Library */
    CHECK_EQ(playlist_select_set(snap, lib, 0), PLAYLIST_SELECT_OK, "clearing");
    CHECK_EQ(playlist_select_current(), 0, "cleared");
    CHECK_EQ(g_order_n, 0, "no order after clearing");
    CHECK(!g_store_has, "and the stored id is gone");

    /* remembered across a reboot: a fresh init reads the stored id and re-applies it */
    g_saves = 0;
    CHECK_EQ(playlist_select_set(snap, lib, 0xAAAA000000000001ull), PLAYLIST_SELECT_OK,
             "select again");
    CHECK(g_store_has, "stored");
    playlist_select_init(&store); /* the reboot */
    CHECK_EQ(playlist_select_current(), 0, "a fresh module has no state");
    g_order_n = 0;
    playlist_select_sync(snap, lib); /* the boot sync */
    CHECK_EQ(playlist_select_current(), 0xAAAA000000000001ull, "the stored choice came back");
    CHECK_EQ(g_order_n, 3, "and so did its order");

    /* a boot sync against no Snapshot clears rather than guessing */
    playlist_select_sync(NULL, lib);
    CHECK_EQ(playlist_select_current(), 0, "no Snapshot clears the choice");
    CHECK_EQ(g_order_n, 0, "and the order");
    CHECK(!g_store_has, "and the stored id");

    /* the broadcast dropped the order for a Track outside it: the choice is cleared */
    CHECK_EQ(playlist_select_set(snap, lib, 0xAAAA000000000001ull), PLAYLIST_SELECT_OK,
             "select again for the drop");
    CHECK(g_dropped != NULL, "the dropped callback is registered");
    if (g_dropped) g_dropped(g_dropped_ctx);
    CHECK_EQ(playlist_select_current(), 0, "the drop cleared the selection");
    CHECK(!g_store_has, "and the stored id");

    playlist_select_set(snap, lib, 0);
    snapshot_close(snap);
    snapshot_close(snap2);
    snapshot_close(snap3);
    free(img);
    free(img2);
    free(img3);
    library_close(lib);

    char cmd[LIB_PATH_MAX + 16];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    if (system(cmd) != 0) { /* the test's own /tmp tree */ }
    free(root);
    TEST_END();
}
