/*
 * T40: playlist_file, host tier.
 *
 * A list is a text file the app and the device both write (docs/specs/playlist-format.md), so
 * the test owns the bytes it reads: every valid file here is written by hand, not by the
 * module, and every refusal is one damaged field away from a good file. What is exercised is
 * the contract the HTTP routes rely on -- the directory index, one list's rows, an atomic
 * write, a remove, and the fingerprint -- plus the rules a card from a computer can break.
 */
#include "test_util.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "playlist_file.h"

/* ------------------------------------------------------------------ helpers */

static void write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (!f || fputs(text, f) == EOF || fclose(f) != 0) {
        printf("FATAL: cannot write %s\n", path);
        exit(1);
    }
}

static bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

/** What one read handed over, so the rows and their order can be checked. */
typedef struct {
    char paths[8][128];
    int count;
    int stop_after;
} rows_t;

static bool collect(void *ctx, const char *path)
{
    rows_t *rows = ctx;
    if (rows->count < 8) snprintf(rows->paths[rows->count], sizeof rows->paths[0], "%s", path);
    rows->count++;
    return !(rows->stop_after > 0 && rows->count >= rows->stop_after);
}

static const char GOOD[] = "K7LIST 1\n"
                           "id 00000000000000a1\n"
                           "name Late night 8A\n"
                           "created 1700000000\n"
                           "rows 2\n"
                           "A/B/one.mp3\n"
                           "A/B/two.mp3\n";

/* --------------------------------------------------------------------- test */

int main(void)
{
    char tmpl[] = "/tmp/xtpplaylistXXXXXX";
    char *root = mkdtemp(tmpl);
    if (!root) {
        printf("FATAL: mkdtemp failed\n");
        return 1;
    }

    char dir[512], path[512];
    playlist_dir(root, dir, sizeof dir);
    snprintf(path, sizeof path, "%s/%016llx.k7list", dir, 0xa1ull);

    /* a card with no directory is "no card", not an empty index */
    playlist_file_t entries[PLAYLIST_MAX_LISTS];
    CHECK_EQ(playlist_list(root, entries, PLAYLIST_MAX_LISTS), -1, "no directory is no card");
    playlist_file_t meta;
    CHECK_EQ(playlist_read_meta(root, 0xa1, &meta), PLAYLIST_FILE_NOT_FOUND, "and no list");

    /* a valid file, written by the test */
    mkdir(dir, 0777);
    write_text(path, GOOD);

    CHECK_EQ(playlist_list(root, entries, PLAYLIST_MAX_LISTS), 1, "the directory holds one list");
    CHECK_EQ(entries[0].id, 0xa1, "its id comes from the name");
    CHECK(strcmp(entries[0].name, "Late night 8A") == 0, "its name comes from the file");
    CHECK_EQ(entries[0].created, 1700000000ull, "its creation time is read");
    CHECK_EQ(entries[0].rows, 2, "its row count is read");

    rows_t rows = {0};
    CHECK_EQ(playlist_read(root, 0xa1, &meta, collect, &rows), PLAYLIST_FILE_OK, "it reads");
    CHECK_EQ(rows.count, 2, "two rows are handed over");
    CHECK(strcmp(rows.paths[0], "A/B/one.mp3") == 0, "the first row, in order");
    CHECK(strcmp(rows.paths[1], "A/B/two.mp3") == 0, "the second row, in order");

    /* a caller that stops early gets the rows it asked for and an OK, not a refusal */
    rows_t stopped = {.stop_after = 1};
    CHECK_EQ(playlist_read(root, 0xa1, NULL, collect, &stopped), PLAYLIST_FILE_OK,
             "a stopped read is not a refusal");
    CHECK_EQ(stopped.count, 1, "and it stopped where it was told");

    /* an id that disagrees with the file's name is refused */
    CHECK_EQ(playlist_read_meta(root, 0xa2, &meta), PLAYLIST_FILE_NOT_FOUND,
             "the file id must equal the name's");

    /* ---------------------------------------------------------------- refusals */

    struct {
        const char *what;
        const char *body;
    } bad[] = {
        {"a wrong version", "K7LIST 2\nid 00000000000000a1\nname x\ncreated 1\nrows 0\n"},
        {"a missing header line", "K7LIST 1\nname x\ncreated 1\nrows 0\n"},
        {"keys out of order", "K7LIST 1\nname x\nid 00000000000000a1\ncreated 1\nrows 0\n"},
        {"an unknown key", "K7LIST 1\nid 00000000000000a1\nname x\nextra 1\ncreated 1\nrows 0\n"},
        {"an id of zero", "K7LIST 1\nid 0000000000000000\nname x\ncreated 1\nrows 0\n"},
        {"a short id", "K7LIST 1\nid a1\nname x\ncreated 1\nrows 0\n"},
        {"an empty name", "K7LIST 1\nid 00000000000000a1\nname \ncreated 1\nrows 0\n"},
        {"a name with an edge space", "K7LIST 1\nid 00000000000000a1\nname  x\ncreated 1\nrows 0\n"},
        {"a non-numeric count", "K7LIST 1\nid 00000000000000a1\nname x\ncreated 1\nrows two\n"},
        {"a count that disagrees", "K7LIST 1\nid 00000000000000a1\nname x\ncreated 1\nrows 3\nA\n"},
        {"a line after the rows", "K7LIST 1\nid 00000000000000a1\nname x\ncreated 1\nrows 1\nA\nB\n"},
        {"an absolute path", "K7LIST 1\nid 00000000000000a1\nname x\ncreated 1\nrows 1\n/A\n"},
        {"a path with ..", "K7LIST 1\nid 00000000000000a1\nname x\ncreated 1\nrows 1\nA/../B\n"},
        {"an empty path", "K7LIST 1\nid 00000000000000a1\nname x\ncreated 1\nrows 1\n\n"},
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        write_text(path, bad[i].body);
        CHECK_EQ(playlist_read_meta(root, 0xa1, &meta), PLAYLIST_FILE_BAD, bad[i].what);
        /* one broken file must not hide the rest of the directory */
        CHECK_EQ(playlist_list(root, entries, PLAYLIST_MAX_LISTS), 0, "and the index skips it");
    }

    /* ------------------------------------------------------------- atomic write */

    unlink(path);
    const char *good_rows[] = {"A/B/one.mp3", "A/B/two.mp3", "A/B/one.mp3"};
    CHECK_EQ(playlist_write(root, 0xa1, "Late night 8A", 1700000000ull, good_rows, 3),
             PLAYLIST_FILE_OK, "a write succeeds");
    CHECK(file_exists(path), "and lands on the final name");
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    CHECK(!file_exists(tmp), "with no temporary left behind");

    rows_t written = {0};
    CHECK_EQ(playlist_read(root, 0xa1, &meta, collect, &written), PLAYLIST_FILE_OK,
             "the written file reads back");
    CHECK_EQ(written.count, 3, "with its rows");
    CHECK(strcmp(written.paths[0], "A/B/one.mp3") == 0, "in order");
    CHECK(strcmp(written.paths[2], "A/B/one.mp3") == 0, "repeats included");
    CHECK_EQ(meta.rows, 3, "the count agrees");

    /* the fingerprint is the content's, so a rewrite says the same thing */
    const char *same[] = {"A/B/one.mp3", "A/B/two.mp3", "A/B/one.mp3"};
    uint64_t before = playlist_fingerprint("Late night 8A", 1700000000ull, same, 3);
    CHECK_EQ(meta.fingerprint, before, "the index fingerprint is the content's");
    CHECK(playlist_fingerprint("Late night 8A", 1700000000ull, same, 2) != before,
          "a shorter list hashes differently");
    CHECK(playlist_fingerprint("Late night 8B", 1700000000ull, same, 3) != before,
          "and so does a renamed one");
    /* line endings a text editor may add do not change the content's fingerprint */
    write_text(path,
               "K7LIST 1\r\nid 00000000000000a1\r\nname Late night 8A\r\ncreated 1700000000\r\n"
               "rows 2\r\nA/B/one.mp3\r\nA/B/two.mp3\r\n");
    CHECK_EQ(playlist_read(root, 0xa1, &meta, NULL, NULL), PLAYLIST_FILE_OK, "CRLF is accepted");
    CHECK_EQ(meta.fingerprint, playlist_fingerprint("Late night 8A", 1700000000ull, good_rows, 2),
             "and hashes the same");

    /* Writing the same list again replaces it. POSIX rename replaces; FATFS refuses an
     * existing destination, which is why `playlist_write` unlinks it and renames again — the
     * card keeps the newest list rather than refusing every write after the first. */
    const char *longer[] = {"A/B/one.mp3", "A/B/two.mp3", "A/B/three.mp3"};
    CHECK_EQ(playlist_write(root, 0xa1, "Late night 8A", 1700000000ull, longer, 3),
             PLAYLIST_FILE_OK, "a second write of the same list succeeds");
    rows_t again = {0};
    CHECK_EQ(playlist_read(root, 0xa1, &meta, collect, &again), PLAYLIST_FILE_OK,
             "and reads back");
    CHECK_EQ(again.count, 3, "with the newer rows");

    /* a bad name or path is refused before anything touches the card */
    CHECK_EQ(playlist_write(root, 0xa2, "", 1, good_rows, 1), PLAYLIST_FILE_BAD, "an empty name");
    CHECK_EQ(playlist_write(root, 0xa2, "x", 1, good_rows, 1), PLAYLIST_FILE_OK, "a short name");
    CHECK_EQ(playlist_write(root, 0, "x", 1, good_rows, 1), PLAYLIST_FILE_BAD, "an id of zero");
    const char *absolute[] = {"/A"};
    CHECK_EQ(playlist_write(root, 0xa3, "x", 1, absolute, 1), PLAYLIST_FILE_BAD, "an absolute row");

    /* --------------------------------------------------------------- remove */

    CHECK_EQ(playlist_remove(root, 0xa2), PLAYLIST_FILE_OK, "a remove succeeds");
    CHECK_EQ(playlist_remove(root, 0xa2), PLAYLIST_FILE_NOT_FOUND, "and twice is not found");

    /* the directory's cap is the caller's to refuse, so the count is the true one */
    for (uint64_t id = 0xc0; id < 0xc0 + PLAYLIST_MAX_LISTS + 2; id++) {
        char one[64];
        snprintf(one, sizeof one, "%016llx", (unsigned long long)id);
        CHECK_EQ(playlist_write(root, id, one, 1, NULL, 0), PLAYLIST_FILE_OK, "an empty list");
    }
    int found = playlist_list(root, entries, PLAYLIST_MAX_LISTS);
    CHECK(found > (int)PLAYLIST_MAX_LISTS, "the index reports a directory over its cap");

    TEST_END();
}
