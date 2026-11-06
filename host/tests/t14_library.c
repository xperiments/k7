/*
 * T14: the Library tree module, host tier.
 *
 * The module is exercised through its public interface: open a Music directory,
 * walk the tree, look paths up, read the JSON and the binary blob. The blob is
 * parsed by an independent reader in this file, so the test does not trust the
 * module's own accessors for the wire format.
 */
#include "test_util.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "library.h"

#define DISK_NODE_SIZE 32u
#define DISK_HEADER_SIZE 64u

typedef struct {
    uint64_t size;
    uint32_t name_off;
    uint32_t parent;
    uint32_t child_edge;
    uint16_t child_count;
    uint16_t name_len;
    uint8_t  kind;
    uint8_t  format;
    uint16_t flags;
    uint32_t track_index;
} disk_node_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t flags;
    uint32_t node_count;
    uint32_t edge_count;
    uint32_t track_count;
    uint32_t string_bytes;
    uint32_t crc32;
    uint32_t generation;
    uint32_t root_name_off;
    uint64_t library_bytes;
    uint32_t reserved32[2];
    uint64_t reserved64;
} disk_header_t;

_Static_assert(sizeof(disk_node_t) == DISK_NODE_SIZE, "test node is 32 bytes");
_Static_assert(sizeof(disk_header_t) == DISK_HEADER_SIZE, "test header is 64 bytes");

/* ------------------------------------------------------------- fixtures */

static char *temp_root(void)
{
    char tmpl[] = "/tmp/xtlibXXXXXX";
    char *p = mkdtemp(tmpl);
    if (!p) {
        printf("FATAL: mkdtemp failed\n");
        exit(1);
    }
    return strdup(p);
}

static void remove_tree(const char *root)
{
    char cmd[LIB_PATH_MAX + 16];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    if (system(cmd) != 0) { /* best effort: the test's own /tmp tree */ }
}

static void mkdir_at(const char *root, const char *rel)
{
    char path[LIB_PATH_MAX];
    int n = snprintf(path, sizeof path, "%s/%s", root, rel);
    if (n < 0 || (size_t)n >= sizeof path) {
        printf("FATAL: path too long: %s/%s\n", root, rel);
        exit(1);
    }
    if (mkdir(path, 0755) != 0) {
        printf("FATAL: mkdir %s\n", path);
        exit(1);
    }
}

static void file_at(const char *root, const char *rel, size_t size)
{
    char path[LIB_PATH_MAX];
    int n = snprintf(path, sizeof path, "%s/%s", root, rel);
    if (n < 0 || (size_t)n >= sizeof path) {
        printf("FATAL: path too long: %s/%s\n", root, rel);
        exit(1);
    }
    int f = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (f < 0) {
        printf("FATAL: cannot create %s\n", path);
        exit(1);
    }
    for (size_t i = 0; i < size; i++) {
        char c = 'x';
        if (write(f, &c, 1) != 1) {
            printf("FATAL: cannot write %s\n", path);
            exit(1);
        }
    }
    close(f);
}

/* --------------------------------------------------------------- helpers */


static uint32_t crc32_ref(const uint8_t *p, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return ~crc;
}

static const disk_node_t *disk_nodes(const disk_header_t *h)
{
    return (const disk_node_t *)((const uint8_t *)h + DISK_HEADER_SIZE);
}

static const uint32_t *disk_edges(const disk_header_t *h)
{
    return (const uint32_t *)(disk_nodes(h) + h->node_count);
}

static const uint32_t *disk_tracks(const disk_header_t *h)
{
    return disk_edges(h) + h->edge_count;
}

static const char *disk_strings(const disk_header_t *h)
{
    return (const char *)(disk_tracks(h) + h->track_count);
}

/* Builds a node's relative path from the blob, independently of the module. */
static bool disk_path(const disk_header_t *h, uint32_t node, char *out, size_t cap)
{
    const disk_node_t *ns = disk_nodes(h);
    const char *strings = disk_strings(h);
    uint32_t chain[LIB_PATH_MAX / 2];
    uint32_t n = 0;
    for (uint32_t cur = node; cur != 0; cur = ns[cur].parent) {
        if (n >= sizeof chain / sizeof chain[0]) return false;
        chain[n++] = cur;
    }
    size_t len = 0;
    for (uint32_t i = n; i > 0; i--) {
        const disk_node_t *dn = &ns[chain[i - 1]];
        if (i != n) {
            if (len + 1 >= cap) return false;
            out[len++] = '/';
        }
        if (len + dn->name_len >= cap) return false;
        memcpy(out + len, strings + dn->name_off, dn->name_len);
        len += dn->name_len;
    }
    out[len] = 0;
    return true;
}

static bool has_name(const library_t *lib, const char *rel, lib_kind_t kind)
{
    lib_node_t n = library_find(lib, rel);
    return n != LIB_NODE_NONE && library_kind(lib, n) == kind;
}

/* ----------------------------------------------------------------- tests */

/* APFS refuses to create a name that is not valid UTF-8; the scan's rule is
 * this validator, so it is exercised directly rather than through a file. */
static void test_utf8(void)
{
    CHECK(library_utf8_valid("Caf\xc3\xa9.mp3", strlen("Caf\xc3\xa9.mp3")), "valid UTF-8");
    CHECK(!library_utf8_valid("\xc0\xaf.mp3", 7), "overlong two-byte form is refused");
    CHECK(!library_utf8_valid("\xed\xa0\x80.mp3", 7), "surrogate is refused");
    CHECK(!library_utf8_valid("\xe2\x82.mp3", 6), "truncated sequence is refused");
    CHECK(!library_utf8_valid("\xff.mp3", 4), "invalid lead byte is refused");
    CHECK(!library_utf8_valid("\xf5\x80\x80\x80.mp3", 8), "code point above U+10FFFF is refused");
}

static void test_empty_root(void)
{
    char *root = temp_root();
    library_cfg_t cfg = {.music_dir = root};
    library_t *lib = NULL;
    CHECK_EQ(library_open(&cfg, &lib), LIB_OK, "empty root opens");
    CHECK(lib != NULL, "empty root produces a generation");
    CHECK_EQ(library_state(lib), LIB_STATE_EMPTY, "empty root state");
    CHECK_EQ(library_node_count(lib), 1, "empty root has only the root node");
    CHECK_EQ(library_track_count(lib), 0, "empty root has no tracks");
    CHECK_EQ(library_kind(lib, library_root(lib)), LIB_DIR, "root is a directory");
    CHECK_EQ(library_child_count(lib, library_root(lib)), 0, "root has no children");

    size_t len = 0;
    const void *blob = library_blob(lib, &len);
    CHECK(blob != NULL && len >= DISK_HEADER_SIZE, "empty root still has a blob");
    const disk_header_t *h = blob;
    CHECK_EQ(h->magic, 0x424C5458u, "blob magic");
    CHECK_EQ(h->node_count, 1, "blob root node");
    CHECK_EQ(h->track_count, 0, "blob no tracks");
    library_close(lib);
    remove_tree(root);
    free(root);
}

static void test_no_dir(void)
{
    library_cfg_t cfg = {.music_dir = "/definitely/not/a/xtape/library"};
    library_t *lib = NULL;
    CHECK_EQ(library_open(&cfg, &lib), LIB_OK, "missing Music directory still opens");
    CHECK(lib != NULL, "missing Music directory produces a generation");
    CHECK_EQ(library_state(lib), LIB_STATE_NO_DIR, "missing Music directory state");
    CHECK_EQ(library_track_count(lib), 0, "missing Music directory has no tracks");
    CHECK(library_error(lib)[0] != 0, "missing Music directory has a diagnosis");
    library_close(lib);
}

static void test_no_card(void)
{
    char *root = temp_root();
    library_cfg_t cfg = {.music_dir = root};
    library_t *lib = NULL;
    CHECK_EQ(library_open_no_card(&cfg, "host says no", &lib), LIB_OK, "no-card generation opens");
    CHECK(lib != NULL, "no-card generation exists");
    CHECK_EQ(library_state(lib), LIB_STATE_NO_CARD, "no-card state");
    CHECK_EQ(library_track_count(lib), 0, "no-card has no tracks");
    CHECK_EQ(library_node_count(lib), 1, "no-card has only the root node");
    CHECK(strstr(library_error(lib), "host says no") != NULL, "no-card carries the reason");
    library_close(lib);
    remove_tree(root);
    free(root);
}

static void test_main_tree(void)
{
    char *root = temp_root();
    mkdir_at(root, "Artist");
    mkdir_at(root, "Artist/Album");
    mkdir_at(root, "Artist/Zen");
    mkdir_at(root, "Empty");
    file_at(root, "Artist/Album/01 - Song.mp3", 11);
    file_at(root, "Artist/Album/02 - Song.flac", 22);
    file_at(root, "loose.wav", 33);
    file_at(root, "Caf\xc3\xa9.mp3", 44);
    file_at(root, ".hidden.mp3", 1);
    file_at(root, "mix..final.mp3", 1);
    file_at(root, "notes.txt", 1);

    char link[LIB_PATH_MAX];
    snprintf(link, sizeof link, "%s/link.mp3", root);
    if (symlink("loose.wav", link) != 0) {
        printf("FATAL: symlink failed\n");
        exit(1);
    }

    char name128[LIB_NAME_MAX + 1];
    memset(name128, 'z', LIB_NAME_MAX - 4);
    memcpy(name128 + LIB_NAME_MAX - 4, ".mp3", 4);
    name128[LIB_NAME_MAX] = 0;
    file_at(root, name128, 1);

    char name129[LIB_NAME_MAX + 2];
    memset(name129, 'y', LIB_NAME_MAX - 3);
    memcpy(name129 + LIB_NAME_MAX - 3, ".mp3", 4);
    name129[LIB_NAME_MAX + 1] = 0;
    file_at(root, name129, 1);

    library_cfg_t cfg = {.music_dir = root};
    library_t *lib = NULL;
    CHECK_EQ(library_open(&cfg, &lib), LIB_OK, "tree opens");
    CHECK(lib != NULL, "tree generation exists");
    CHECK_EQ(library_state(lib), LIB_STATE_OK, "tree state");
    CHECK(!library_truncated(lib), "tree is not truncated");
    CHECK_EQ(library_node_count(lib), 10, "root + 4 dirs + 5 tracks");
    CHECK_EQ(library_track_count(lib), 5, "five playable tracks");
    CHECK_EQ(library_dir_count(lib), 4, "four directories below the root");
    CHECK_EQ(library_total_bytes(lib), 111, "track bytes are summed");
    CHECK_EQ(library_generation(lib), 1, "first generation");

    /* Root children are name-sorted, dirs and tracks together. */
    CHECK_EQ(library_child_count(lib, 0), 5, "root has five children");
    const char *want[] = {"Artist", "Caf\xc3\xa9.mp3", "Empty", "loose.wav", name128};
    for (uint32_t i = 0; i < 5; i++) {
        lib_node_t child = library_child(lib, 0, i);
        uint16_t len = 0;
        const char *name = library_name(lib, child, &len);
        CHECK(len == strlen(want[i]) && memcmp(name, want[i], len) == 0,
              "root child %u is %s (got %.*s)", i, want[i], (int)len, name);
    }

    const char *playlist[] = {
        "Artist/Album/01 - Song.mp3",
        "Artist/Album/02 - Song.flac",
        "Caf\xc3\xa9.mp3",
        "loose.wav",
        name128,
    };
    for (uint32_t i = 0; i < 5; i++) {
        lib_node_t n = library_track_at(lib, i);
        char path[LIB_PATH_MAX];
        CHECK_EQ(library_relpath(lib, n, path, sizeof path), strlen(playlist[i]),
                 "track path length");
        CHECK(strcmp(path, playlist[i]) == 0, "broadcast order %u is %s (got %s)", i, playlist[i], path);
        CHECK(library_track_index(lib, n) == i, "track %u index", i);
    }

    /* Lookup: exact, case-insensitive fallback, directories, and no filesystem. */
    CHECK(has_name(lib, "Artist/Album/01 - Song.mp3", LIB_TRACK), "exact track lookup");
    CHECK(has_name(lib, "artist/album/02 - SONG.FLAC", LIB_TRACK), "case fold lookup");
    CHECK(has_name(lib, "Artist/Album", LIB_DIR), "dir lookup");
    CHECK(has_name(lib, "ARTIST/zen", LIB_DIR), "dir case fold lookup");
    CHECK(!has_name(lib, "Artist/../loose.wav", LIB_TRACK), "parent component is refused");
    CHECK(!has_name(lib, "Artist//Album", LIB_DIR), "empty component is refused");
    CHECK(!has_name(lib, "Artist/Album/", LIB_DIR), "trailing slash is refused");
    CHECK(!has_name(lib, "Artist/Album/notes.txt", LIB_TRACK), "non-audio is not indexed");
    CHECK(!has_name(lib, "link.mp3", LIB_TRACK), "symlink is not indexed");
    CHECK(!has_name(lib, name129, LIB_TRACK), "a 129-byte name is not indexed");
    CHECK(!has_name(lib, "mix..final.mp3", LIB_TRACK), "a name containing .. is not indexed");

    char loose_abs[LIB_PATH_MAX];
    lib_node_t loose_node = library_find(lib, "loose.wav");
    CHECK_EQ(library_abspath(lib, loose_node, loose_abs, sizeof loose_abs),
             strlen(root) + 1 + strlen("loose.wav"), "absolute path length");
    char want_loose[LIB_PATH_MAX];
    snprintf(want_loose, sizeof want_loose, "%s/loose.wav", root);
    CHECK(strcmp(loose_abs, want_loose) == 0, "absolute path is root + relative path");

    /* The tree is immutable: a lookup after the file moves still uses the arena. */
    char loose[LIB_PATH_MAX], moved[LIB_PATH_MAX];
    snprintf(loose, sizeof loose, "%s/loose.wav", root);
    snprintf(moved, sizeof moved, "%s/moved.wav", root);
    CHECK_EQ(rename(loose, moved), 0, "fixture rename");
    CHECK(has_name(lib, "loose.wav", LIB_TRACK), "lookup does not consult the filesystem");

    /* Independent blob reader. */
    size_t blob_len = 0;
    const void *blob = library_blob(lib, &blob_len);
    CHECK(blob != NULL, "blob exists");
    const disk_header_t *h = blob;
    CHECK_EQ(h->magic, 0x424C5458u, "blob magic");
    CHECK_EQ(h->version, 1, "blob version");
    CHECK_EQ(h->header_size, DISK_HEADER_SIZE, "blob header size");
    CHECK_EQ(h->node_count, 10, "blob node count");
    CHECK_EQ(h->edge_count, 9, "blob edge count");
    CHECK_EQ(h->track_count, 5, "blob track count");
    CHECK_EQ(h->library_bytes, 111, "blob library bytes");
    CHECK_EQ(h->crc32, crc32_ref((const uint8_t *)blob + DISK_HEADER_SIZE,
                                blob_len - DISK_HEADER_SIZE),
             "blob CRC");
    CHECK_EQ(library_crc32(lib), h->crc32, "public CRC agrees with the blob");

    const disk_node_t *nodes = disk_nodes(h);
    const uint32_t *tracks = disk_tracks(h);
    CHECK_EQ(nodes[0].parent, UINT32_MAX, "blob root has no parent");
    CHECK_EQ(nodes[0].child_count, 5, "blob root child count");
    CHECK_EQ(nodes[0].child_edge, 0, "blob root edge offset");
    for (uint32_t i = 0; i < h->track_count; i++) {
        char path[LIB_PATH_MAX];
        CHECK(disk_path(h, tracks[i], path, sizeof path), "blob track path builds");
        CHECK(strcmp(path, playlist[i]) == 0, "blob track %u is %s (got %s)", i, playlist[i], path);
        CHECK_EQ(nodes[tracks[i]].track_index, i, "blob track index");
    }
    for (uint32_t i = 0; i + 1 < h->node_count; i++) {
        CHECK(nodes[i].child_edge == UINT32_MAX || nodes[i].child_edge < h->edge_count,
              "node %u edge offset is in range", i);
    }

    library_close(lib);
    remove_tree(root);
    free(root);
}


static void test_truncation(void)
{
    char *root = temp_root();
    file_at(root, "a.mp3", 1);
    file_at(root, "b.mp3", 1);
    file_at(root, "c.mp3", 1);
    file_at(root, "d.mp3", 1);
    file_at(root, "e.mp3", 1);

    library_cfg_t cfg = {.music_dir = root, .max_tracks = 3};
    library_t *lib = NULL;
    CHECK_EQ(library_open(&cfg, &lib), LIB_OK, "capped tree opens");
    CHECK_EQ(library_track_count(lib), 3, "track cap stops the scan at three");
    CHECK_EQ(library_state(lib), LIB_STATE_OK, "a capped tree is still ok");
    CHECK(library_truncated(lib), "track cap sets truncated");
    size_t len = 0;
    const disk_header_t *h = library_blob(lib, &len);
    CHECK((h->flags & 0x2u) != 0, "blob truncated flag");
    library_close(lib);

    /* A byte cap smaller than even the root's estimate stops before any child. */
    cfg.max_tracks = 0;
    cfg.max_bytes = 64;
    lib = NULL;
    CHECK_EQ(library_open(&cfg, &lib), LIB_OK, "byte-capped tree opens");
    CHECK_EQ(library_track_count(lib), 0, "byte cap stops before any track");
    CHECK_EQ(library_state(lib), LIB_STATE_EMPTY, "a byte-capped empty tree is empty");
    CHECK(library_truncated(lib), "byte cap sets truncated");
    library_close(lib);
    remove_tree(root);
    free(root);
}

static void test_unreadable_dir(void)
{
    char *root = temp_root();
    if (geteuid() == 0) {
        printf("note: running as root; the unreadable-directory case is skipped\n");
        free(root);
        return;
    }
    mkdir_at(root, "private");
    file_at(root, "visible.mp3", 3);
    file_at(root, "private/secret.mp3", 5);
    char path[LIB_PATH_MAX];
    snprintf(path, sizeof path, "%s/private", root);
    if (chmod(path, 0000) != 0) {
        printf("FATAL: chmod private\n");
        exit(1);
    }

    library_cfg_t cfg = {.music_dir = root};
    library_t *lib = NULL;
    CHECK_EQ(library_open(&cfg, &lib), LIB_OK, "tree with an unreadable dir opens");
    CHECK_EQ(library_track_count(lib), 1, "only the visible track is indexed");
    CHECK(has_name(lib, "visible.mp3", LIB_TRACK), "visible track is found");
    CHECK(has_name(lib, "private", LIB_DIR), "the unreadable dir is still indexed");
    CHECK(!has_name(lib, "private/secret.mp3", LIB_TRACK), "its track is not indexed");
    CHECK(library_error(lib)[0] != 0, "the unreadable dir is reported");
    library_close(lib);

    chmod(path, 0755);
    remove_tree(root);
    free(root);
}

static void test_deep_path(void)
{
    char *root = temp_root();
    char dir[LIB_PATH_MAX];
    snprintf(dir, sizeof dir, "%s", root);
    char name[101];
    memset(name, 'd', sizeof name - 1);
    name[sizeof name - 1] = 0;

    /* The host cannot create a path over its own PATH_MAX, so the ceiling is
     * lowered through the configuration; the rule under test is the same one
     * the default FILENAME_MAX uses. */
    const size_t target = 240; /* a small test ceiling, below the host's PATH_MAX */
    while (strlen(dir) + 1 + strlen(name) < target) {
        size_t used = strlen(dir);
        int n = snprintf(dir + used, sizeof dir - used, "/%s", name);
        if (n < 0 || (size_t)n >= sizeof dir - used) {
            printf("FATAL: deep path build\n");
            exit(1);
        }
        if (mkdir(dir, 0755) != 0) {
            printf("FATAL: deep mkdir %s\n", dir);
            exit(1);
        }
    }
    size_t remaining = target - strlen(dir) - 1;
    if (remaining > 0 && remaining <= 100) {
        char tail[101];
        memset(tail, 'e', remaining);
        tail[remaining] = 0;
        size_t used = strlen(dir);
        int n = snprintf(dir + used, sizeof dir - used, "/%s", tail);
        if (n < 0 || (size_t)n >= sizeof dir - used) {
            printf("FATAL: deep tail build\n");
            exit(1);
        }
        if (mkdir(dir, 0755) != 0) {
            printf("FATAL: deep tail mkdir %s\n", dir);
            exit(1);
        }
    }
    size_t base = strlen(dir);
    char under[LIB_PATH_MAX], over[LIB_PATH_MAX];
    snprintf(under, sizeof under, "%s/u.mp3", dir);
    snprintf(over, sizeof over, "%s/over-long-file-name.mp3", dir);
    CHECK(base + 1 + strlen("u.mp3") <= 256, "the under path fits the test ceiling");
    CHECK(base + 1 + strlen("over-long-file-name.mp3") > 256,
          "the over path exceeds the test ceiling");
    file_at(dir, "u.mp3", 3);
    file_at(dir, "over-long-file-name.mp3", 4);

    library_cfg_t cfg = {.music_dir = root, .max_path = 256};
    library_t *lib = NULL;
    CHECK_EQ(library_open(&cfg, &lib), LIB_OK, "deep tree opens");
    const char *rel = dir + strlen(root) + 1;
    char rel_under[LIB_PATH_MAX], rel_over[LIB_PATH_MAX];
    snprintf(rel_under, sizeof rel_under, "%s/u.mp3", rel);
    snprintf(rel_over, sizeof rel_over, "%s/over-long-file-name.mp3", rel);
    CHECK(has_name(lib, rel_under, LIB_TRACK), "a path under the ceiling is indexed");
    CHECK(!has_name(lib, rel_over, LIB_TRACK), "a path over the ceiling is skipped");
    library_close(lib);
    remove_tree(root);
    free(root);
}


static void test_store(void)
{
    char *root = temp_root();
    file_at(root, "a.wav", 3);
    library_cfg_t cfg = {.music_dir = root,
                         .generation = library_next_generation()};
    library_t *one = NULL;
    CHECK_EQ(library_open(&cfg, &one), LIB_OK, "store generation one opens");
    CHECK(library_publish(one), "generation one publishes");
    CHECK_EQ(library_current_generation(), library_generation(one), "the current generation is one");

    const library_t *held = library_acquire();
    CHECK(held == one, "acquire returns the published generation");
    CHECK_EQ(library_refs(one), 2, "the publisher and the reader each hold a reference");

    library_cfg_t cfg2 = {.music_dir = root,
                          .generation = library_next_generation()};
    library_t *two = NULL;
    CHECK_EQ(library_open(&cfg2, &two), LIB_OK, "store generation two opens");
    CHECK(library_generation(two) > library_generation(one), "the rescan number advances");
    CHECK(library_publish(two), "generation two publishes");
    CHECK_EQ(library_refs(one), 1, "the retired generation survives its slow reader");
    CHECK_EQ(library_current_generation(), library_generation(two), "the current generation is two");

    library_release(held); /* the last reader frees generation one */
    const library_t *again = library_acquire();
    CHECK(again == two, "a new lease sees generation two");
    CHECK_EQ(library_refs(two), 2, "the new lease is counted");
    library_release(again);
    remove_tree(root);
    /* The published generation intentionally outlives this test process; there is
     * no app shutdown hook and a dangling current pointer would be worse. */
}

int main(void)
{
    test_utf8();
    test_empty_root();
    test_no_dir();
    test_no_card();
    test_main_tree();
    test_truncation();
    test_unreadable_dir();
    test_deep_path();
    test_store();

    /* The host filesystem is case-insensitive, so it cannot hold two names that
     * differ only by case; the ambiguous-fallback refusal is implemented in
     * library_find and is exercised by code review, not by this fixture. The
     * host shim ignores allocation capabilities, so PSRAM placement is a board
     * property (ticket 13), not something this tier can assert. */
    TEST_END();
}
