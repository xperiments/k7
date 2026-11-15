/*
 * T30: playlist_select, host tier.
 *
 * The selection module is tested against a real Library built from a temporary tree and real
 * playlist files on a temporary card, with the broadcast stubbed: this test is about
 * resolution, refusal and remembering, not about audio. What the order does once the
 * broadcast has it is the integration suite's business (the hosting server plus
 * /api/playlists/select and the stream's own advance).
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
#include "playlist_file.h"
#include "playlist_select.h"

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

/** One list on the card, written the way the app writes it. */
static void write_list(const char *card_root, uint64_t id, const char *name,
                       const char *const *paths, uint32_t npaths)
{
    char dir[LIB_PATH_MAX];
    snprintf(dir, sizeof dir, "%s/%s", card_root, PLAYLIST_DIR_NAME);
    mkdir(dir, 0755);
    char path[LIB_PATH_MAX + 40];
    snprintf(path, sizeof path, "%s/%016llx%s", dir, (unsigned long long)id, PLAYLIST_EXT);
    FILE *f = fopen(path, "wb");
    if (!f) {
        printf("FATAL: cannot write %s\n", path);
        exit(1);
    }
    fprintf(f, "K7LIST 1\nid %016llx\nname %s\ncreated 1767225600\nrows %u\n",
            (unsigned long long)id, name, (unsigned)npaths);
    for (uint32_t i = 0; i < npaths; i++) fprintf(f, "%s\n", paths[i]);
    if (fclose(f) != 0) {
        printf("FATAL: cannot close %s\n", path);
        exit(1);
    }
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
    char *card = temp_root();
    char music[LIB_PATH_MAX];
    snprintf(music, sizeof music, "%s/music", card);
    mkdir_at(card, "music");
    mkdir_at(music, "A");
    mkdir_at(music, "B");
    file_at(music, "A/one.mp3", 10);
    file_at(music, "B/two.mp3", 20);
    file_at(music, "B/three.mp3", 30);

    library_cfg_t cfg = {.music_dir = music};
    library_t *lib = NULL;
    CHECK_EQ(library_open(&cfg, &lib), LIB_OK, "the fixture tree opens");
    CHECK_EQ(library_track_count(lib), 3, "three tracks");

    const char *const ALL[] = {"A/one.mp3", "B/two.mp3", "B/three.mp3"};
    const char *const SOME[] = {"A/one.mp3", "B/gone.mp3"};
    write_list(card, 0xAAAA000000000001ull, "Late night", ALL, 3);

    g_store_has = false;
    g_saves = 0;
    playlist_select_store_t store = {NULL, store_load, store_save};
    playlist_select_init(&store);

    /* nothing chosen: the Library plays, and no order is sent */
    g_order_calls = 0;
    playlist_select_sync(card, lib);
    CHECK_EQ(playlist_select_current(), 0, "no selection");
    CHECK_EQ(g_order_n, 0, "no order");
    char name[64] = {0};
    playlist_select_name(name, sizeof name);
    CHECK(strcmp(name, "") == 0, "no name");

    /* choose it: the whole list, in order */
    CHECK_EQ(playlist_select_set(card, lib, 0xAAAA000000000001ull), PLAYLIST_SELECT_OK,
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
    for (uint32_t i = 0; i < 3; i++) {
        lib_node_t node = library_find(lib, ALL[i]);
        CHECK_EQ(g_order[i], library_track_index(lib, node), "row resolves in order");
    }

    /* an unknown id is a refusal, and the old order stays */
    CHECK_EQ(playlist_select_set(card, lib, 0xAAAA0000000000FFull), PLAYLIST_SELECT_NOT_FOUND,
             "an unknown id is refused");
    CHECK_EQ(playlist_select_current(), 0xAAAA000000000001ull, "the refusal changed nothing");
    CHECK_EQ(g_order_n, 3, "the order is still the selected one");

    /* a row whose file is gone counts, and the rest still plays */
    write_list(card, 0xBBBB000000000002ull, "Half gone", SOME, 2);
    CHECK_EQ(playlist_select_set(card, lib, 0xBBBB000000000002ull), PLAYLIST_SELECT_OK,
             "a list with a missing row still selects");
    CHECK_EQ(g_order_n, 1, "one resolved row");
    CHECK_EQ(playlist_select_missing(), 1, "one missing row is counted");

    /* an empty list is refused, and nothing is walking */
    write_list(card, 0xCCCC000000000003ull, "Empty", NULL, 0);
    CHECK_EQ(playlist_select_set(card, lib, 0xCCCC000000000003ull), PLAYLIST_SELECT_EMPTY,
             "an empty list is refused");
    CHECK_EQ(playlist_select_current(), 0xBBBB000000000002ull,
             "a refused selection leaves the working one alone");
    CHECK_EQ(g_order_n, 1, "and its order is untouched");

    /* none: back to the Library */
    CHECK_EQ(playlist_select_set(card, lib, 0), PLAYLIST_SELECT_OK, "clearing");
    CHECK_EQ(playlist_select_current(), 0, "cleared");
    CHECK_EQ(g_order_n, 0, "no order after clearing");
    CHECK(!g_store_has, "and the stored id is gone");

    /* remembered across a reboot: a fresh init reads the stored id and re-applies it */
    g_saves = 0;
    CHECK_EQ(playlist_select_set(card, lib, 0xAAAA000000000001ull), PLAYLIST_SELECT_OK,
             "select again");
    CHECK(g_store_has, "stored");
    playlist_select_init(&store); /* the reboot */
    CHECK_EQ(playlist_select_current(), 0, "a fresh module has no state");
    g_order_n = 0;
    playlist_select_sync(card, lib); /* the boot sync */
    CHECK_EQ(playlist_select_current(), 0xAAAA000000000001ull, "the stored choice came back");
    CHECK_EQ(g_order_n, 3, "and so did its order");

    /* a boot sync with no card keeps the choice and stops the walk: the card carries it, so
     * it comes back with the card (spec U3: a refusal leaves the selection alone) */
    playlist_select_sync(NULL, lib);
    CHECK_EQ(playlist_select_current(), 0xAAAA000000000001ull, "no card keeps the choice");
    CHECK_EQ(g_order_n, 0, "and stops the walk");
    CHECK(g_store_has, "the choice is still stored");

    /* a Library that cannot resolve a row is EMPTY, not a refusal: the walk stops and the
     * choice stands, so it resumes when the card is back */
    char *empty_root = temp_root();
    library_cfg_t empty_cfg = {.music_dir = empty_root};
    library_t *empty_lib = NULL;
    CHECK_EQ(library_open(&empty_cfg, &empty_lib), LIB_OK, "the empty root opens");
    playlist_select_sync(card, lib);
    CHECK_EQ(g_order_n, 3, "walking");
    g_order_n = 0;
    playlist_select_sync(card, empty_lib);
    CHECK_EQ(g_order_n, 0, "with no resolvable row the walk stops");
    CHECK_EQ(playlist_select_current(), 0xAAAA000000000001ull,
             "but the choice stands, so it comes back with the card");
    CHECK(g_store_has, "and it is still stored");
    playlist_select_sync(card, lib);
    CHECK_EQ(g_order_n, 3, "the card comes back and so does the walk");
    CHECK_EQ(playlist_select_current(), 0xAAAA000000000001ull, "same choice");
    library_close(empty_lib);
    char rm[LIB_PATH_MAX + 16];
    snprintf(rm, sizeof rm, "rm -rf '%s'", empty_root);
    if (system(rm) != 0) { /* the test's own /tmp tree */ }
    free(empty_root);

    /* a list whose file went away is forgotten rather than retried every boot */
    CHECK_EQ(playlist_select_set(card, lib, 0xBBBB000000000002ull), PLAYLIST_SELECT_OK,
             "walk the half-gone list");
    char gone_path[LIB_PATH_MAX + 40];
    snprintf(gone_path, sizeof gone_path, "%s/%s/%016llx%s", card, PLAYLIST_DIR_NAME,
             0xBBBB000000000002ull, PLAYLIST_EXT);
    unlink(gone_path);
    playlist_select_sync(card, lib);
    CHECK_EQ(playlist_select_current(), 0, "a list that is gone clears the choice");
    CHECK_EQ(g_order_n, 0, "and the order");
    CHECK(!g_store_has, "and the stored id");

    /* the broadcast dropped the order for a Track outside it: the choice is cleared */
    CHECK_EQ(playlist_select_set(card, lib, 0xAAAA000000000001ull), PLAYLIST_SELECT_OK,
             "select again for the drop");
    CHECK(g_dropped != NULL, "the dropped callback is registered");
    if (g_dropped) g_dropped(g_dropped_ctx);
    CHECK_EQ(playlist_select_current(), 0, "the drop cleared the selection");
    CHECK(!g_store_has, "and the stored id");

    playlist_select_set(card, lib, 0);
    library_close(lib);

    char cmd[LIB_PATH_MAX + 16];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", card);
    if (system(cmd) != 0) { /* the test's own /tmp tree */ }
    free(card);
    TEST_END();
}
