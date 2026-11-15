/*
 * playlist_file - see include/playlist_file.h and docs/specs/playlist-format.md.
 *
 * stdio and dirent only: the firmware runs this against the mounted card, the host tests
 * against a temporary directory, and it is the same bytes either way.
 */
#include "playlist_file.h"

#include <dirent.h>
#include <esp_log.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "playlist_file";

/* FNV-1a 64, the hash the id and a Track's path already use. */
#define FNV_OFFSET 0xcbf29ce484222325ull
#define FNV_PRIME  0x100000001b3ull

/* One line of a file: a path may be PLAYLIST_PATH_MAX bytes, and a line carries it plus a
 * terminator and a stray CR. Read into the heap, not the caller's stack: the index reads 64
 * files from the connection task, whose budget is the tightest in the firmware. */
#define LINE_MAX_BYTES (PLAYLIST_PATH_MAX + 8u)

static uint64_t fnv(uint64_t h, const void *data, size_t len)
{
    const unsigned char *p = data;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= FNV_PRIME;
    }
    return h;
}

static uint64_t fnv_str(uint64_t h, const char *s)
{
    return fnv(h, s, strlen(s));
}

/* FNV over `text`, then over a newline: the framing that keeps "ab" + "c" from hashing like
 * "a" + "bc". */
static uint64_t fnv_line(uint64_t h, const char *text)
{
    h = fnv_str(h, text);
    return fnv(h, "\n", 1);
}

uint64_t playlist_fingerprint(const char *name, uint64_t created, const char *const *rows,
                              uint32_t count)
{
    char number[24];
    snprintf(number, sizeof number, "%llu", (unsigned long long)created);
    uint64_t h = fnv_line(FNV_OFFSET, name ? name : "");
    h = fnv_line(h, number);
    for (uint32_t i = 0; i < count; i++) h = fnv_line(h, rows[i] ? rows[i] : "");
    return h;
}

void playlist_dir(const char *root, char *out, size_t cap)
{
    if (!out || !cap) return;
    if (!root || !root[0]) {
        out[0] = 0;
        return;
    }
    int n = snprintf(out, cap, "%s/%s", root, PLAYLIST_DIR_NAME);
    if (n < 0 || (size_t)n >= cap) out[0] = 0;
}

bool playlist_path(const char *root, uint64_t id, char *out, size_t cap)
{
    if (!out || !cap || !id) return false;
    char dir[256];
    playlist_dir(root, dir, sizeof dir);
    if (!dir[0]) return false;
    int n = snprintf(out, cap, "%s/%016llx%s", dir, (unsigned long long)id, PLAYLIST_EXT);
    return n > 0 && (size_t)n < cap;
}

/* ---------------------------------------------------------------- the file */

/* The header a reader must see, in order. */
static const char *const HEADER_KEYS[5] = {"K7LIST", "id", "name", "created", "rows"};

/** One line, without its line ending. False at EOF, or when the line did not fit. */
static bool read_line(FILE *f, char *out, size_t cap, bool *too_long)
{
    if (!fgets(out, (int)cap, f)) return false;
    size_t len = strlen(out);
    *too_long = false;
    if (len > 0 && out[len - 1] != '\n') {
        /* Either the last line of a file without its final LF, or a line longer than the
         * buffer. Tell them apart by whether the stream is at its end. */
        int c = fgetc(f);
        if (c != EOF) {
            *too_long = true;
            ungetc(c, f);
        }
    }
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) out[--len] = 0;
    return true;
}

/** Split `line` into a key and a value: the first space separates them. */
static bool split_key(const char *line, const char **key, size_t *key_len, const char **value)
{
    const char *space = strchr(line, ' ');
    if (!space) return false;
    *key = line;
    *key_len = (size_t)(space - line);
    *value = space + 1;
    return true;
}

static bool key_is(const char *key, size_t key_len, const char *want)
{
    return strlen(want) == key_len && memcmp(key, want, key_len) == 0;
}

static bool all_digits(const char *s)
{
    if (!s || !*s) return false;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return false;
    }
    return true;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/** A list's name, as the format allows it: 1..63 bytes, no edge space, no control bytes. */
static bool name_ok(const char *name)
{
    size_t len = strlen(name);
    if (len == 0 || len > PLAYLIST_NAME_MAX) return false;
    if (name[0] == ' ' || name[len - 1] == ' ') return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 0x20 || c == 0x7F) return false;
    }
    return true;
}

/** A row, as the format allows it: a relative path below the card's music directory. */
static bool path_ok(const char *path)
{
    size_t len = strlen(path);
    if (len == 0 || len > PLAYLIST_PATH_MAX) return false;
    if (path[0] == '/') return false;
    if (strstr(path, "..")) return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)path[i];
        if (c < 0x20 || c == 0x7F) return false;
    }
    return true;
}

/*
 * Read one file: the header, then the rows. `meta` and `row` are optional. `body` runs once
 * per row, after the header, with the row's path.
 */
static playlist_file_result_t read_file(const char *path, uint64_t wanted, playlist_file_t *meta,
                                        playlist_row_fn row, void *ctx)
{
    FILE *f = fopen(path, "rb");
    if (!f) return errno == ENOENT ? PLAYLIST_FILE_NOT_FOUND : PLAYLIST_FILE_NO_CARD;

    char *line = malloc(LINE_MAX_BYTES);
    if (!line) return PLAYLIST_FILE_FULL;
    bool too_long = false;
    playlist_file_result_t result = PLAYLIST_FILE_BAD;
    char name[PLAYLIST_NAME_MAX + 1] = {0};
    uint64_t id = 0;
    uint64_t created = 0;
    uint64_t total = 0;
    uint32_t declared = 0;
    uint64_t fingerprint = FNV_OFFSET;
    char number[24];

    if (!read_line(f, line, LINE_MAX_BYTES, &too_long) || too_long) goto out;
    {
        const char *key, *value;
        size_t key_len;
        if (!split_key(line, &key, &key_len, &value)) goto out;
        if (!key_is(key, key_len, HEADER_KEYS[0]) || strcmp(value, "1") != 0) goto out;
    }

    for (int field = 1; field < 5; field++) {
        if (!read_line(f, line, LINE_MAX_BYTES, &too_long) || too_long) goto out;
        const char *key, *value;
        size_t key_len;
        if (!split_key(line, &key, &key_len, &value)) goto out;
        if (!key_is(key, key_len, HEADER_KEYS[field])) goto out;
        switch (field) {
        case 1: { /* id */
            if (strlen(value) != 16) goto out;
            uint64_t parsed = 0;
            for (int i = 0; i < 16; i++) {
                int d = hex_digit(value[i]);
                if (d < 0) goto out;
                parsed = (parsed << 4) | (uint64_t)d;
            }
            if (!parsed || (wanted && parsed != wanted)) goto out;
            id = parsed;
            break;
        }
        case 2: /* name */
            if (!name_ok(value)) goto out;
            snprintf(name, sizeof name, "%s", value);
            break;
        case 3: /* created */
            if (!all_digits(value)) goto out;
            created = strtoull(value, NULL, 10);
            break;
        case 4: /* rows */
            if (!all_digits(value)) goto out;
            {
                unsigned long long parsed = strtoull(value, NULL, 10);
                if (parsed > PLAYLIST_MAX_ROWS) goto out;
                declared = (uint32_t)parsed;
            }
            break;
        }
    }

    fingerprint = fnv_line(fingerprint, name);
    snprintf(number, sizeof number, "%llu", (unsigned long long)created);
    fingerprint = fnv_line(fingerprint, number);

    for (uint32_t i = 0; i < declared; i++) {
        if (!read_line(f, line, LINE_MAX_BYTES, &too_long) || too_long) goto out;
        if (!path_ok(line)) goto out;
        fingerprint = fnv_line(fingerprint, line);
        if (row && !row(ctx, line)) {
            /* The caller stopped: what it collected is its own, but the file is well
             * formed so far and the answer is not a refusal. */
            result = PLAYLIST_FILE_OK;
            goto out;
        }
        total++;
    }
    /* No line may follow the declared rows. */
    if (read_line(f, line, LINE_MAX_BYTES, &too_long) && (too_long || line[0])) goto out;

    if (meta) {
        meta->id = id;
        snprintf(meta->name, sizeof meta->name, "%s", name);
        meta->created = created;
        meta->rows = (uint32_t)total;
        meta->fingerprint = fingerprint;
    }
    result = PLAYLIST_FILE_OK;

out:
    fclose(f);
    free(line);
    return result;
}

playlist_file_result_t playlist_read(const char *root, uint64_t id, playlist_file_t *meta,
                                     playlist_row_fn row, void *ctx)
{
    char path[320];
    if (!playlist_path(root, id, path, sizeof path)) return PLAYLIST_FILE_NO_CARD;
    return read_file(path, id, meta, row, ctx);
}

playlist_file_result_t playlist_read_meta(const char *root, uint64_t id, playlist_file_t *out)
{
    if (!out) return PLAYLIST_FILE_BAD;
    return playlist_read(root, id, out, NULL, NULL);
}

/* ---------------------------------------------------------------- the index */

/** Whether `a` and `b` are the same letters, ignoring case: FAT32 folds names. */
static bool same_ext(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = (*a >= 'a' && *a <= 'z') ? (char)(*a - 'a' + 'A') : *a;
        char y = (*b >= 'a' && *b <= 'z') ? (char)(*b - 'a' + 'A') : *b;
        if (x != y) return false;
    }
    return *a == *b;
}

static bool hex16_name(const char *name, uint64_t *id)
{
    const size_t ext = sizeof PLAYLIST_EXT - 1;
    size_t len = strlen(name);
    if (len != 16 + ext) return false;
    if (!same_ext(name + 16, PLAYLIST_EXT)) return false;
    uint64_t parsed = 0;
    for (int i = 0; i < 16; i++) {
        int d = hex_digit(name[i]);
        if (d < 0) return false;
        parsed = (parsed << 4) | (uint64_t)d;
    }
    if (!parsed) return false;
    if (id) *id = parsed;
    return true;
}

static int by_id(const void *a, const void *b)
{
    const playlist_file_t *left = a, *right = b;
    if (left->id < right->id) return -1;
    if (left->id > right->id) return 1;
    return 0;
}

int playlist_list(const char *root, playlist_file_t *out, int max)
{
    char dir[256];
    playlist_dir(root, dir, sizeof dir);
    if (!dir[0] || !out || max <= 0) return -1;

    DIR *d = opendir(dir);
    if (!d) return -1;
    int count = 0, found = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        uint64_t id = 0;
        if (!hex16_name(e->d_name, &id)) continue;
        playlist_file_t meta;
        if (playlist_read(root, id, &meta, NULL, NULL) != PLAYLIST_FILE_OK) {
            /* One unreadable file must not hide the rest: it is missing from the index, and
             * the person sees a list that is not there rather than none at all. */
            continue;
        }
        found++;
        if (count < max) out[count++] = meta;
    }
    closedir(d);
    qsort(out, (size_t)count, sizeof *out, by_id);
    /* The true count, even past `max`: the caller is the one that refuses a directory with
     * more lists than the format allows. */
    return found;
}

/* ---------------------------------------------------------------- writing */

/** Write every row, and the header, to `f`. False when the card refused any of it. */
static bool write_file(FILE *f, uint64_t id, const char *name, uint64_t created,
                       const char *const *rows, uint32_t count)
{
    if (fprintf(f, "K7LIST 1\nid %016llx\nname %s\ncreated %llu\nrows %u\n",
                (unsigned long long)id, name, (unsigned long long)created, (unsigned)count) < 0) {
        return false;
    }
    for (uint32_t i = 0; i < count; i++) {
        if (fprintf(f, "%s\n", rows[i]) < 0) return false;
    }
    return fflush(f) == 0;
}

playlist_file_result_t playlist_write(const char *root, uint64_t id, const char *name,
                                      uint64_t created, const char *const *rows, uint32_t count)
{
    if (!id || !name_ok(name) || count > PLAYLIST_MAX_ROWS) return PLAYLIST_FILE_BAD;
    for (uint32_t i = 0; i < count; i++) {
        if (!rows || !rows[i] || !path_ok(rows[i])) return PLAYLIST_FILE_BAD;
    }

    char dir[256];
    playlist_dir(root, dir, sizeof dir);
    if (!dir[0]) return PLAYLIST_FILE_NO_CARD;
    if (mkdir(dir, 0777) != 0 && errno != EEXIST) return PLAYLIST_FILE_NO_CARD;

    char final_path[320];
    if (!playlist_path(root, id, final_path, sizeof final_path)) return PLAYLIST_FILE_NO_CARD;
    char tmp_path[sizeof final_path + 8];
    int n = snprintf(tmp_path, sizeof tmp_path, "%s.tmp", final_path);
    if (n < 0 || (size_t)n >= sizeof tmp_path) return PLAYLIST_FILE_NO_CARD;

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        ESP_LOGW(TAG, "cannot open %s for writing: %d", tmp_path, errno);
        return PLAYLIST_FILE_FULL;
    }
    bool written = write_file(f, id, name, created, rows, count);
    if (fclose(f) != 0) written = false;
    if (!written) {
        ESP_LOGW(TAG, "cannot write %s: %d", tmp_path, errno);
        unlink(tmp_path);
        return PLAYLIST_FILE_FULL;
    }
    /* The rename is the commit: before it the old file is untouched, after it the new one is
     * whole. A card that fills mid-write can only lose the temporary.
     *
     * FATFS refuses to rename onto a name that already exists (FR_EXIST), where POSIX replaces
     * it, so a second write of the same list needs the destination gone first. The temporary is
     * whole and validated by now, so the window is one syscall and what lands is the new list. */
    if (rename(tmp_path, final_path) != 0) {
        int first = errno;
        unlink(final_path);
        if (rename(tmp_path, final_path) != 0) {
            ESP_LOGW(TAG, "cannot rename %s onto %s: %d then %d", tmp_path, final_path, first,
                     errno);
            unlink(tmp_path);
            return PLAYLIST_FILE_FULL;
        }
    }
    return PLAYLIST_FILE_OK;
}

playlist_file_result_t playlist_store_body(const char *root, uint64_t id, const char *body,
                                          size_t len, playlist_file_t *meta)
{
    if (!id || !body || !len) return PLAYLIST_FILE_BAD;

    char dir[256];
    playlist_dir(root, dir, sizeof dir);
    if (!dir[0]) return PLAYLIST_FILE_NO_CARD;
    if (mkdir(dir, 0777) != 0 && errno != EEXIST) return PLAYLIST_FILE_NO_CARD;

    char final_path[320];
    if (!playlist_path(root, id, final_path, sizeof final_path)) return PLAYLIST_FILE_NO_CARD;
    char tmp_path[sizeof final_path + 8];
    int n = snprintf(tmp_path, sizeof tmp_path, "%s.tmp", final_path);
    if (n < 0 || (size_t)n >= sizeof tmp_path) return PLAYLIST_FILE_NO_CARD;

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        ESP_LOGW(TAG, "cannot open %s for writing: %d", tmp_path, errno);
        return PLAYLIST_FILE_FULL;
    }
    bool written = fwrite(body, 1, len, f) == len && fflush(f) == 0;
    if (fclose(f) != 0) written = false;
    if (!written) {
        ESP_LOGW(TAG, "cannot write %s: %d", tmp_path, errno);
        unlink(tmp_path);
        return PLAYLIST_FILE_FULL;
    }

    /* Validated where it landed, with the reader the card is read by: a body that is not a
     * list this device would play never reaches the final name. */
    playlist_file_t stored;
    if (read_file(tmp_path, id, &stored, NULL, NULL) != PLAYLIST_FILE_OK) {
        unlink(tmp_path);
        return PLAYLIST_FILE_BAD;
    }
    if (rename(tmp_path, final_path) != 0) {
        /* See `playlist_write`: FATFS wants the destination gone before a rename lands. */
        int first = errno;
        unlink(final_path);
        if (rename(tmp_path, final_path) != 0) {
            ESP_LOGW(TAG, "cannot rename %s onto %s: %d then %d", tmp_path, final_path, first,
                     errno);
            unlink(tmp_path);
            return PLAYLIST_FILE_FULL;
        }
    }
    if (meta) *meta = stored;
    return PLAYLIST_FILE_OK;
}

playlist_file_result_t playlist_remove(const char *root, uint64_t id)
{
    char path[320];
    if (!playlist_path(root, id, path, sizeof path)) return PLAYLIST_FILE_NO_CARD;
    char tmp_path[sizeof path + 8];
    snprintf(tmp_path, sizeof tmp_path, "%s.tmp", path);
    unlink(tmp_path);
    if (unlink(path) != 0) {
        return errno == ENOENT ? PLAYLIST_FILE_NOT_FOUND : PLAYLIST_FILE_NO_CARD;
    }
    return PLAYLIST_FILE_OK;
}
