/*
 * T18: the Menu's model, host tier.
 *
 * The model is exercised through menu.h only: a real library_t built from a temp
 * directory (the fixture style of t14_library.c), a hand-written
 * mini_icecast_status_t and a menu_facts_t the test owns. No LVGL, no private
 * access, no mocks of the model's interiors.
 *
 * docs/specs/menu.md and ADR 0022 are the source of truth; the comments below
 * pin the formatting details the spec leaves open.
 */
#include "test_util.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "library.h"
#include "menu.h"

#define EM_DASH "\xe2\x80\x94"

/* ------------------------------------------------------------- fixtures */

static char *temp_root(void)
{
    char tmpl[] = "/tmp/xtmenuXXXXXX";
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

/* A sparse file: its size (what the library sums) costs no disk. */
static void sparse_at(const char *root, const char *rel, off_t size)
{
    char path[LIB_PATH_MAX];
    int n = snprintf(path, sizeof path, "%s/%s", root, rel);
    if (n < 0 || (size_t)n >= sizeof path) {
        printf("FATAL: path too long: %s/%s\n", root, rel);
        exit(1);
    }
    int f = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (f < 0 || ftruncate(f, size) != 0) {
        printf("FATAL: cannot size %s\n", path);
        exit(1);
    }
    close(f);
}

/* The main tree, built with the same layout the assertions expect:
 *
 *   Café.mp3                                     50 B  (track, root child 0)
 *   Jazz/Blue Train/01 - Blue Train.mp3          10 B
 *   Jazz/Blue Train/02 - Moment's Notice.flac    20 B
 *   Jazz/Miles/So What.mp3                       30 B
 *   loose song.mp3                               60 B
 *   Rock/Don't Stop (Live).wav                   40 B
 *
 * Children sort case-insensitively (the library's rule), so the root is
 * Café.mp3, Jazz, loose song.mp3, Rock and the broadcast order (depth first) is
 * Café, 01 - Blue Train, 02 - Moment's Notice, So What, loose song, Don't Stop.
 * Six tracks, four directories, 210 bytes.
 */
static char *make_tree(void)
{
    char *root = temp_root();
    mkdir_at(root, "Jazz");
    mkdir_at(root, "Jazz/Blue Train");
    mkdir_at(root, "Jazz/Miles");
    mkdir_at(root, "Rock");
    file_at(root, "Caf\xc3\xa9.mp3", 50);
    file_at(root, "Jazz/Blue Train/01 - Blue Train.mp3", 10);
    file_at(root, "Jazz/Blue Train/02 - Moment's Notice.flac", 20);
    file_at(root, "Jazz/Miles/So What.mp3", 30);
    file_at(root, "loose song.mp3", 60);
    file_at(root, "Rock/Don't Stop (Live).wav", 40);
    return root;
}

static library_t *lib_from_cfg(library_cfg_t *cfg)
{
    library_t *lib = NULL;
    CHECK_EQ(library_open(cfg, &lib), LIB_OK, "fixture library opens");
    if (!lib) {
        printf("FATAL: fixture library is NULL\n");
        exit(1);
    }
    return lib;
}

static library_t *lib_from(const char *root)
{
    library_cfg_t cfg = {.music_dir = root,
                         .generation = library_next_generation()};
    return lib_from_cfg(&cfg);
}

static library_t *lib_from_capped(const char *root, uint32_t max_tracks)
{
    library_cfg_t cfg = {.music_dir = root, .max_tracks = max_tracks,
                         .generation = library_next_generation()};
    return lib_from_cfg(&cfg);
}

static library_t *lib_no_card(const char *root)
{
    library_cfg_t cfg = {.music_dir = root,
                         .generation = library_next_generation()};
    library_t *lib = NULL;
    CHECK_EQ(library_open_no_card(&cfg, "test: no card", &lib), LIB_OK,
             "no-card fixture opens");
    if (!lib) {
        printf("FATAL: no-card fixture is NULL\n");
        exit(1);
    }
    return lib;
}

/* The "many" fixture: 1,204 tracks (one of them a 12.4 GiB sparse file) and 38
 * empty directories. It is built once for the tests that need a list longer
 * than a page. */
#define MANY_TRACKS 1204u
#define MANY_DIRS   38u
#define MANY_BIG_BYTES ((uint64_t)13314398618) /* 12.4 GiB, to printf's tenth */

static char *g_many_root;
static library_t *g_many_lib;

static void many_setup(void)
{
    g_many_root = temp_root();
    char rel[32];
    for (uint32_t i = 0; i < MANY_DIRS; i++) {
        snprintf(rel, sizeof rel, "d%02u", i);
        mkdir_at(g_many_root, rel);
    }
    sparse_at(g_many_root, "big.mp3", (off_t)MANY_BIG_BYTES);
    for (uint32_t i = 0; i + 1 < MANY_TRACKS; i++) {
        snprintf(rel, sizeof rel, "t%04u.mp3", i);
        file_at(g_many_root, rel, 1);
    }
    g_many_lib = lib_from(g_many_root);
    CHECK_EQ(library_track_count(g_many_lib), MANY_TRACKS, "the many fixture has 1,204 tracks");
    CHECK_EQ(library_dir_count(g_many_lib), MANY_DIRS, "the many fixture has 38 dirs");
    CHECK_EQ(library_state(g_many_lib), LIB_STATE_OK, "the many fixture is ok");
}

static void many_teardown(void)
{
    library_close(g_many_lib);
    remove_tree(g_many_root);
    free(g_many_root);
    g_many_lib = NULL;
    g_many_root = NULL;
}

/* ---------------------------------------------------------- facts/status */

static menu_facts_t facts_plain(void)
{
    menu_facts_t f = {0};
    f.backlight = 0;
    f.version = "1.2.3";
    f.board = "xtape-esp32s3";
    f.partition = "1.68 MB of 4 MB";
    return f;
}

static mini_icecast_status_t status_idle(void)
{
    mini_icecast_status_t s;
    memset(&s, 0, sizeof s);
    s.format = MI_FMT_NONE;
    return s;
}

static mini_icecast_status_t status_playing(const char *path, mi_fmt_t fmt, uint32_t hz,
                                            uint32_t bitrate)
{
    mini_icecast_status_t s = status_idle();
    snprintf(s.path, sizeof s.path, "%s", path);
    s.format = fmt;
    s.source_hz = hz;
    s.out_hz = hz;
    s.bitrate = bitrate;
    return s;
}

static playlist_file_t list_make(uint64_t id, const char *name, uint32_t rows)
{
    playlist_file_t p;
    memset(&p, 0, sizeof p);
    p.id = id;
    snprintf(p.name, sizeof p.name, "%s", name);
    p.rows = rows;
    return p;
}

static menu_facts_t facts_with_playlists(const playlist_file_t *lists, int count,
                                         uint64_t selected)
{
    menu_facts_t f = facts_plain();
    f.playlists = lists;
    f.playlist_count = count;
    f.playlist_selected = selected;
    return f;
}

/* The long Playlists fixture: 32 lists, so a window shift and a ratchet have room. The
 * names are already in order; the model's own sort is exercised by test_playlists_rows. */
#define LONG_LISTS 32
static playlist_file_t g_long_lists[LONG_LISTS];
static menu_facts_t g_long_facts;

static void long_lists_setup(void)
{
    for (int i = 0; i < LONG_LISTS; i++) {
        char name[32];
        snprintf(name, sizeof name, "List %02d", i);
        g_long_lists[i] = list_make((uint64_t)i + 1, name, 10);
    }
    g_long_facts = facts_with_playlists(g_long_lists, LONG_LISTS, 0);
}

static menu_t *menu_open_with(const library_t *lib, const menu_facts_t *facts)
{
    menu_t *m = menu_new();
    if (!m) {
        printf("FATAL: menu_new failed\n");
        exit(1);
    }
    CHECK(!menu_is_open(m), "a new model is closed");
    menu_open(m, lib, facts);
    CHECK(menu_is_open(m), "menu_open opens the menu");
    return m;
}

/* ------------------------------------------------------------ assertions */

static void want_str(const char *got, const char *want, const char *what)
{
    CHECK(strcmp(got, want) == 0, "%s: got \"%s\", want \"%s\"", what, got, want);
}

static void want_label(const menu_row_t *r, const char *want, const char *what)
{
    want_str(r->label, want, what);
}

static void want_value(const menu_row_t *r, const char *want, const char *what)
{
    want_str(r->value, want, what);
}

static void want_kind(const menu_row_t *r, menu_row_kind_t want, const char *what)
{
    CHECK(r->kind == want, "%s: kind %d, want %d", what, (int)r->kind, (int)want);
}

static void want_icon(const menu_row_t *r, menu_icon_t want, const char *what)
{
    CHECK(r->icon == want, "%s: icon %d, want %d", what, (int)r->icon, (int)want);
}

static bool read_rows(menu_t *m, menu_row_t rows[MENU_ROWS])
{
    memset(rows, 0, sizeof(menu_row_t) * MENU_ROWS);
    bool ok = menu_rows(m, rows);
    CHECK(ok, "menu_rows fills while open");
    return ok;
}

/* ---------------------------------------------------------------- tests */

static void test_clean_name(void)
{
    char buf[MENU_LABEL_MAX];
    want_str(menu_clean_name("01 - Blue Train.mp3", buf, sizeof buf), "01 - Blue Train",
             "extension dropped");
    want_str(menu_clean_name("Song", buf, sizeof buf), "Song", "no extension is left alone");
    want_str(menu_clean_name("Don't Stop.mp3", buf, sizeof buf), "Don t Stop",
             "an apostrophe becomes a space");
    want_str(menu_clean_name("Moment\xe2\x80\x99s Notice.flac", buf, sizeof buf),
             "Moment s Notice", "a typographic apostrophe becomes a space");
    want_str(menu_clean_name("Song (Live).mp3", buf, sizeof buf), "Song",
             "a trailing aside is dropped");
    want_str(menu_clean_name("Song (Live) Now.mp3", buf, sizeof buf), "Song Now",
             "an aside in the middle is dropped and the gap collapsed");
    want_str(menu_clean_name("A (B (C) D) E.wav", buf, sizeof buf), "A E",
             "nested asides are dropped");
    want_str(menu_clean_name("Song (unclosed.mp3", buf, sizeof buf), "Song",
             "an unclosed aside is dropped to the end");
    want_str(menu_clean_name("  padded  .mp3", buf, sizeof buf), "padded",
             "leading and trailing space is trimmed");
    want_str(menu_clean_name("mix..final.mp3", buf, sizeof buf), "mix..final",
             "only the last extension is dropped");
    want_str(menu_clean_name("Caf\xc3\xa9.mp3", buf, sizeof buf), "Caf\xc3\xa9",
             "a UTF-8 name is kept");
    want_str(menu_clean_name("", buf, sizeof buf), "", "an empty name is empty");
    want_str(menu_clean_name(NULL, buf, sizeof buf), "", "a NULL name is empty");
    want_str(menu_clean_name("abcdefghijklmnop.mp3", buf, 8), "abcdefg",
             "the result is truncated into the buffer");
    want_str(menu_clean_name("Caf\xc3\xa9Long.mp3", buf, 5), "Caf",
             "truncation does not split a UTF-8 sequence");
    CHECK(menu_clean_name("x.mp3", buf, sizeof buf) == buf, "the caller's buffer is returned");
}

static void test_root_rows(void)
{
    char *root = make_tree();
    library_t *lib = lib_from(root);
    menu_facts_t facts = facts_plain();
    menu_t *m = menu_open_with(lib, &facts);

    want_str(menu_title(m), "Menu", "root title");
    CHECK_EQ(menu_count(m), 4, "the root has four rows");
    CHECK_EQ(menu_band(m), 0, "the band starts on the first row");
    CHECK_EQ(menu_position(m), 1, "the position is 1-based");

    menu_row_t r[MENU_ROWS];
    read_rows(m, r);

    want_label(&r[0], "Browse", "root row 0 label");
    want_icon(&r[0], MENU_ICON_BROWSE, "root row 0 icon");
    want_kind(&r[0], MENU_ROW_SUBMENU, "root row 0 kind");
    CHECK(r[0].chevron, "Browse wears a chevron");
    CHECK(r[0].enabled, "Browse is live with an ok library");
    CHECK(r[0].value[0] == 0, "a submenu row has no value");

    want_label(&r[1], "Playlists", "root row 1 label");
    want_icon(&r[1], MENU_ICON_PLAYLIST, "root row 1 icon");
    want_kind(&r[1], MENU_ROW_SUBMENU, "root row 1 kind");
    CHECK(r[1].chevron, "Playlists wears a chevron");
    CHECK(r[1].enabled, "Playlists is live with an ok library");
    CHECK(r[1].value[0] == 0, "a submenu row has no value");

    want_label(&r[2], "Settings", "root row 2 label");
    want_icon(&r[2], MENU_ICON_SETTINGS, "root row 2 icon");
    want_kind(&r[2], MENU_ROW_SUBMENU, "root row 2 kind");
    CHECK(r[2].chevron, "Settings wears a chevron");
    CHECK(r[2].enabled, "Settings is always live");

    want_label(&r[3], "Shuffle", "root row 3 label");
    want_icon(&r[3], MENU_ICON_SHUFFLE, "root row 3 icon");
    want_kind(&r[3], MENU_ROW_ACTION, "root row 3 kind");
    CHECK(!r[3].chevron, "Shuffle has no chevron");
    CHECK(r[3].enabled, "Shuffle is always live");
    want_value(&r[3], "Off", "Shuffle reads the mode");

    CHECK(r[4].label[0] == 0, "the root's fifth slot is blank");
    CHECK(!r[4].enabled, "a blank row is not enabled");
    CHECK(!r[4].chevron, "a blank row has no chevron");

    menu_step(m, 3);
    CHECK_EQ(menu_band(m), 3, "the band is on Shuffle");
    CHECK_EQ(menu_activate(m), MENU_ACTION_SHUFFLE, "Shuffle returns its toggle action");
    CHECK_EQ(menu_band(m), 3, "toggling shuffle does not move the band");

    facts.shuffle_on = true;
    menu_update(m, lib, &facts, NULL);
    read_rows(m, r);
    want_value(&r[3], "On", "the shuffle value follows the facts");

    menu_close(m);
    CHECK(!menu_is_open(m), "menu_close closes the menu");
    CHECK(!menu_rows(m, r), "menu_rows is false while closed");
    menu_free(m);
    library_close(lib);
    remove_tree(root);
    free(root);
}

static void test_wifi_list(void)
{
    const library_t *lib = NULL;
    menu_facts_t facts = facts_plain();
    facts.network = "xperiments-2.4G";
    facts.setup_password = "newpass99";
    facts.hotspot_on = true;
    facts.hotspot_address = "192.168.4.1";
    menu_t *m = menu_open_with(lib, &facts);

    menu_step(m, 2);                  /* Settings */
    menu_activate(m);                 /* push Settings */
    menu_step(m, 2);                  /* the Wi-Fi row */
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "opening Wi-Fi is not an action");
    want_str(menu_title(m), "Wi-Fi", "Wi-Fi title");
    CHECK_EQ(menu_count(m), 6, "the Wi-Fi list has six rows");

    menu_row_t r[MENU_ROWS];
    read_rows(m, r);
    want_label(&r[0], "Setup", "Wi-Fi row 0 label");
    want_kind(&r[0], MENU_ROW_ACTION, "Wi-Fi row 0 kind");
    want_value(&r[0], "STOP", "a running hotspot offers STOP");
    want_label(&r[1], "Share", "Wi-Fi row 1 label");
    want_kind(&r[1], MENU_ROW_ACTION, "Wi-Fi row 1 kind");
    want_value(&r[1], "START", "a closed share window offers START");
    want_label(&r[2], "Network", "Wi-Fi row 2 label");
    want_kind(&r[2], MENU_ROW_VALUE, "Wi-Fi row 2 kind");
    want_value(&r[2], "xperiments-2.4G", "the chosen network is named");
    want_label(&r[3], "Password", "Wi-Fi row 3 label");
    want_value(&r[3], "newpass99", "the setup password is shown in clear");
    want_label(&r[4], "Reset password", "Wi-Fi row 4 label");
    want_kind(&r[4], MENU_ROW_ACTION, "Wi-Fi row 4 kind");
    want_value(&r[4], "RESET", "the reset row says RESET");

    CHECK_EQ(menu_activate(m), MENU_ACTION_WIFI_TOGGLE, "the Setup row toggles the hotspot");
    menu_step(m, 1);
    CHECK_EQ(menu_activate(m), MENU_ACTION_WIFI_SHARE, "the Share row opens the window");
    menu_step(m, 3);
    CHECK_EQ(menu_activate(m), MENU_ACTION_WIFI_RESET, "the Reset row resets the password");

    /* The Address row is the sixth; the five-row window scrolls to it. */
    menu_step(m, 1);
    read_rows(m, r);
    want_label(&r[4], "Address", "Wi-Fi row 5 label");
    want_value(&r[4], "192.168.4.1", "the address is shown while the hotspot is up");

    /* The other states of the Setup row. */
    facts.hotspot_on = false;
    facts.hotspot_address = NULL;
    menu_update(m, lib, &facts, NULL);
    menu_step(m, -5);
    read_rows(m, r);
    want_value(&r[0], "START", "a stopped hotspot offers START");
    menu_step(m, 5);
    read_rows(m, r);
    want_value(&r[4], "\xe2\x80\x94", "and the address is an em dash");

    menu_step(m, -5);
    facts.hotspot_failed = true;
    menu_update(m, lib, &facts, NULL);
    read_rows(m, r);
    want_value(&r[0], "UNAVAILABLE", "a hotspot that could not start says so");

    /* The share window's own states, on its own row. */
    facts.share_on = true;
    facts.share_left_s = 43;
    facts.share_failed = false;
    menu_update(m, lib, &facts, NULL);
    read_rows(m, r);
    want_value(&r[1], "43s", "an open window counts down");
    facts.share_failed = true;
    menu_update(m, lib, &facts, NULL);
    read_rows(m, r);
    want_value(&r[1], "UNAVAILABLE", "a window that could not start says so");

    /* A device with no network names none. */
    facts.network = NULL;
    menu_update(m, lib, &facts, NULL);
    read_rows(m, r);
    want_value(&r[2], "none", "no chosen network reads as none");

    /* Back returns to the row that opened the list. */
    /* menu_back() answers only "did this close the Menu"; going up a level is false. */
    menu_back(m);
    want_str(menu_title(m), "Settings", "and lands in Settings");
    CHECK_EQ(menu_band(m), 2, "on the Wi-Fi row");

    menu_free(m);
}

static void test_settings_rows(void)
{
    char *root = make_tree();
    library_t *lib = lib_from(root);
    menu_facts_t facts = facts_plain();
    menu_t *m = menu_open_with(lib, &facts);

    menu_step(m, 2);
    CHECK_EQ(menu_band(m), 2, "the band is on Settings");
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "opening Settings is not an action");
    want_str(menu_title(m), "Settings", "Settings title");
    CHECK_EQ(menu_count(m), 5, "Settings has five rows");
    CHECK_EQ(menu_band(m), 0, "a pushed list starts at its first row");
    CHECK_EQ(menu_position(m), 1, "Settings position 1");

    menu_row_t r[MENU_ROWS];
    read_rows(m, r);

    want_label(&r[0], "Backlight", "Settings row 0 label");
    want_icon(&r[0], MENU_ICON_BACKLIGHT, "Settings row 0 icon");
    want_kind(&r[0], MENU_ROW_ACTION, "Settings row 0 kind");
    CHECK(!r[0].chevron, "Backlight has no chevron");
    want_value(&r[0], "Auto", "the default backlight is Auto");

    want_label(&r[1], "Rescan", "Settings row 1 label");
    want_icon(&r[1], MENU_ICON_RESCAN, "Settings row 1 icon");
    want_kind(&r[1], MENU_ROW_ACTION, "Settings row 1 kind");
    CHECK(!r[1].chevron, "Rescan has no chevron");
    CHECK(r[1].enabled, "Rescan is live when it is not running");

    want_label(&r[2], "Wi-Fi", "Settings row 2 label");
    want_icon(&r[2], MENU_ICON_WIFI, "Settings row 2 icon");
    want_kind(&r[2], MENU_ROW_SUBMENU, "Settings row 2 kind");
    CHECK(r[2].chevron, "Wi-Fi wears a chevron");

    want_label(&r[3], "About", "Settings row 3 label");
    want_icon(&r[3], MENU_ICON_ABOUT, "Settings row 3 icon");
    want_kind(&r[3], MENU_ROW_SUBMENU, "Settings row 3 kind");
    CHECK(r[3].chevron, "About wears a chevron");

    /* The player's transport (espnow-stream.md E9, ADR 0104): the row says which way the
     * audio goes, and the model reports a change rather than carrying a value. */
    want_label(&r[4], "Player", "Settings row 4 label");
    want_icon(&r[4], MENU_ICON_STATION, "Settings row 4 icon");
    want_kind(&r[4], MENU_ROW_ACTION, "Settings row 4 kind");
    CHECK(!r[4].chevron, "Player has no chevron");
    want_value(&r[4], "HTTP", "no stored choice shows HTTP (the facts say so)");


    /* Backlight cycles Auto -> 100 -> 60 -> 30 -> Auto, in place. */
    CHECK_EQ(menu_activate(m), MENU_ACTION_BACKLIGHT, "Backlight returns its action");
    CHECK_EQ(menu_action_backlight(m), 100, "Auto goes to 100%");
    facts.backlight = 100;
    menu_update(m, lib, &facts, NULL);
    read_rows(m, r);
    want_value(&r[0], "100%", "the backlight value is 100%");

    CHECK_EQ(menu_activate(m), MENU_ACTION_BACKLIGHT, "Backlight acts again");
    CHECK_EQ(menu_action_backlight(m), 60, "100% goes to 60%");
    facts.backlight = 60;
    menu_update(m, lib, &facts, NULL);
    read_rows(m, r);
    want_value(&r[0], "60%", "the backlight value is 60%");

    CHECK_EQ(menu_activate(m), MENU_ACTION_BACKLIGHT, "Backlight acts again");
    CHECK_EQ(menu_action_backlight(m), 30, "60% goes to 30%");
    facts.backlight = 30;
    menu_update(m, lib, &facts, NULL);
    read_rows(m, r);
    want_value(&r[0], "30%", "the backlight value is 30%");

    CHECK_EQ(menu_activate(m), MENU_ACTION_BACKLIGHT, "Backlight acts again");
    CHECK_EQ(menu_action_backlight(m), 0, "30% goes back to Auto");
    facts.backlight = 0;
    menu_update(m, lib, &facts, NULL);

    /* Rescan: inert and relabelled while it runs. */
    menu_step(m, 1);
    CHECK_EQ(menu_band(m), 1, "the band is on Rescan");
    CHECK_EQ(menu_activate(m), MENU_ACTION_RESCAN, "Rescan is returned when idle");

    facts.rescanning = true;
    menu_update(m, lib, &facts, NULL);
    read_rows(m, r);
    CHECK(!r[1].enabled, "Rescan is inert while running");
    want_label(&r[1], "Rescanning...", "Rescan's label while running");
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "a running rescan return no action");

    facts.rescanning = false;
    menu_update(m, lib, &facts, NULL);
    read_rows(m, r);
    CHECK(r[1].enabled, "Rescan is live again");
    want_label(&r[1], "Rescan", "Rescan's label when idle");

    /* The Player row flips the stored choice, and the row follows it. */
    menu_step(m, 3); /* Rescan -> Wi-Fi -> About -> Player */
    CHECK_EQ(menu_band(m), 4, "the band is on Player");
    CHECK_EQ(menu_activate(m), MENU_ACTION_PLAYER, "Player returns its action");
    facts.player_espnow = true;
    menu_update(m, lib, &facts, NULL);
    read_rows(m, r);
    want_value(&r[4], "ESP-NOW", "the row follows the setting");

    menu_free(m);
    library_close(lib);

    /* A truncated generation says so. */
    library_t *cut = lib_from_capped(root, 2);
    m = menu_open_with(cut, &facts);
    menu_step(m, 2);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "opening Settings is not an action");
    menu_step(m, 3);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "opening About is not an action");
    menu_step(m, 10);
    read_rows(m, r);
    want_value(&r[1], "2 tracks", "a capped library reports two tracks");
    want_value(&r[4], "Yes", "a capped library reports truncation");
    menu_free(m);
    library_close(cut);

    /* A no-card generation still has the library facts, with zeroes. */
    library_t *nc = lib_no_card("/tmp/xtape-no-card");
    m = menu_open_with(nc, &facts);
    menu_step(m, 2);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "opening Settings is not an action");
    menu_step(m, 3);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "opening About is not an action");
    menu_step(m, 10);
    read_rows(m, r);
    want_value(&r[0], "no_card", "a no-card library's state name");
    want_value(&r[1], "0 tracks", "a no-card library has no tracks");
    want_value(&r[2], "0 dirs", "a no-card library has no dirs");
    want_value(&r[3], "0 B", "a no-card library has no bytes");
    want_value(&r[4], "No", "a no-card library is not truncated");
    menu_free(m);
    library_close(nc);

    remove_tree(root);
    free(root);
}

static void test_playlists_rows(void)
{
    char *root = make_tree();
    library_t *lib = lib_from(root);

    /* The index arrives in id order; the model draws it in name order. */
    playlist_file_t lists[4] = {
        list_make(0x10, "Zed", 12),
        list_make(0x20, "alpha", 1),
        list_make(0x30, "Beta", 0),
        list_make(0x40, "late night 8A", 3),
    };
    menu_facts_t facts = facts_with_playlists(lists, 4, 0x10);
    menu_t *m = menu_open_with(lib, &facts);

    menu_step(m, 1);
    CHECK_EQ(menu_band(m), 1, "the band is on Playlists");
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "opening Playlists is not an action");
    want_str(menu_title(m), "Playlists", "Playlists title");
    CHECK_EQ(menu_count(m), 5, "Library order plus four lists");
    CHECK_EQ(menu_band(m), 0, "the Playlists list starts at Library order");
    CHECK_EQ(menu_position(m), 1, "Playlists position 1");

    menu_row_t r[MENU_ROWS];
    read_rows(m, r);

    want_label(&r[0], "Library order", "row 0 is the library's own order");
    want_icon(&r[0], MENU_ICON_LIBRARY, "Library order wears books");
    want_kind(&r[0], MENU_ROW_PLAYLIST, "Library order chooses a walk");
    CHECK(!r[0].chevron, "Library order has no chevron");
    CHECK(r[0].enabled, "Library order is live");
    CHECK(!r[0].playing, "the selected list is not the library's order");

    want_label(&r[1], "Beta", "the lists are in name order");
    want_value(&r[1], "0 tracks", "a zero-row list states its count");
    CHECK(!r[1].enabled, "a zero-row list is dimmed");
    want_label(&r[2], "Zed", "byte order puts the capitals first");
    want_value(&r[2], "12 tracks", "the declared count is the right column");
    CHECK(r[2].playing, "the walking list carries the dot");
    CHECK(r[2].enabled, "a list with rows is live");
    want_icon(&r[2], MENU_ICON_PLAYLIST, "a list wears radio-waves");
    want_label(&r[3], "alpha", "lowercase names follow the capitals");
    want_value(&r[3], "1 track", "one row is singular");
    want_label(&r[4], "late night 8A", "the name is shown verbatim");

    /* Library order walks the tree order; a list walks its id. */
    CHECK_EQ(menu_band(m), 0, "the band is on Library order");
    CHECK_EQ(menu_activate(m), MENU_ACTION_WALK, "Library order returns WALK");
    CHECK_EQ(menu_action_playlist(m), 0, "Library order is id 0");

    menu_step(m, 2);
    CHECK_EQ(menu_band(m), 2, "the band is on Zed");
    CHECK_EQ(menu_activate(m), MENU_ACTION_WALK, "a list returns WALK");
    CHECK_EQ(menu_action_playlist(m), 0x10, "the walk names the list's id");

    /* A zero-row list is inert. */
    menu_step(m, -1);
    CHECK_EQ(menu_band(m), 1, "the band is on Beta");
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "a zero-row list does nothing");

    /* The dot moves with the selection, without the index changing. */
    facts.playlist_selected = 0;
    menu_update(m, lib, &facts, NULL);
    read_rows(m, r);
    CHECK(r[0].playing, "Library order carries the dot when no list walks");
    CHECK(!r[2].playing, "the old list loses it");
    menu_step(m, 1);
    facts.playlist_selected = 0x40;
    menu_update(m, lib, &facts, NULL);
    read_rows(m, r);
    CHECK(r[4].playing, "the newly selected list carries the dot");

    menu_free(m);

    /* No lists, too many lists, and a card pulled while the list is open. */
    menu_facts_t empty = facts_with_playlists(NULL, 0, 0);
    m = menu_open_with(lib, &empty);
    menu_step(m, 1);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "opening Playlists is not an action");
    CHECK_EQ(menu_count(m), 2, "Library order and the no-lists note");
    read_rows(m, r);
    want_label(&r[0], "Library order", "Library order is still there");
    want_label(&r[1], "No playlists", "the empty note");
    want_kind(&r[1], MENU_ROW_NOTE, "the empty note is a note");
    CHECK(!r[1].enabled, "the empty note is inert");
    menu_free(m);

    menu_facts_t over = facts_with_playlists(lists, -1, 0);
    m = menu_open_with(lib, &over);
    menu_step(m, 1);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "opening Playlists is not an action");
    CHECK_EQ(menu_count(m), 2, "Library order and the over-cap note");
    read_rows(m, r);
    want_label(&r[0], "Library order", "Library order is still there");
    want_label(&r[1], "Too many lists", "the over-cap note");
    menu_free(m);

    menu_facts_t good = facts_with_playlists(lists, 4, 0);
    m = menu_open_with(lib, &good);
    menu_step(m, 1);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "opening Playlists is not an action");
    menu_update(m, NULL, &good, NULL);
    CHECK_EQ(menu_count(m), 1, "a pulled card leaves one note");
    read_rows(m, r);
    want_label(&r[0], "No card", "the card-pulled note");
    menu_free(m);

    library_close(lib);
    remove_tree(root);
    free(root);
}

static void test_about_rows(void)
{
    char *root = make_tree();
    library_t *lib = lib_from(root);
    menu_facts_t facts = facts_plain();
    menu_t *m = menu_open_with(lib, &facts);

    menu_step(m, 2);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "opening Settings is not an action");
    menu_step(m, 3);
    CHECK_EQ(menu_band(m), 3, "the band is on About");
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "opening About is not an action");
    want_str(menu_title(m), "About", "About title");
    CHECK_EQ(menu_count(m), 11, "About has eleven rows");

    mini_icecast_status_t st = status_playing("", MI_FMT_MP3, 44100, 192);
    menu_update(m, lib, &facts, &st);

    menu_row_t r[MENU_ROWS];
    read_rows(m, r);
    want_label(&r[0], "Firmware", "About row 0 label");
    want_value(&r[0], "1.2.3", "About firmware is the app's version");
    want_icon(&r[0], MENU_ICON_FIRMWARE, "About firmware wears the chip");
    want_label(&r[1], "Board", "About row 1 label");
    want_value(&r[1], "xtape-esp32s3", "About board is the board's name");
    want_icon(&r[1], MENU_ICON_BOARD, "About board wears the tablet");
    want_label(&r[2], "Partition", "About row 2 label");
    want_value(&r[2], "1.68 MB of 4 MB", "About partition figures");
    want_icon(&r[2], MENU_ICON_PARTITION, "About partition wears the disc");
    want_label(&r[3], "Format", "About row 3 label");
    want_value(&r[3], "MP3", "the broadcast's format");
    want_icon(&r[3], MENU_ICON_STATION, "the broadcast's rows wear radio-waves");
    want_label(&r[4], "Rate", "About row 4 label");
    want_value(&r[4], "44.1 kHz", "the broadcast's rate");
    for (int i = 0; i < MENU_ROWS; i++) {
        want_kind(&r[i], MENU_ROW_VALUE, "About rows are read-only values");
        CHECK(!r[i].chevron, "an About row has no chevron");
        CHECK(r[i].enabled, "an About row is live");
    }

    /* The bitrate and then the library block are below the fold. */
    menu_step(m, 5);
    read_rows(m, r);
    want_label(&r[4], "Bitrate", "About row 5 label");
    want_value(&r[4], "192 kbps", "the broadcast's bitrate");

    menu_step(m, 5);
    CHECK_EQ(menu_position(m), 11, "the band is on the last row");
    CHECK_EQ(menu_band(m), 4, "the window scrolled, not the band past the page");
    read_rows(m, r);
    want_label(&r[0], "State", "the library block starts at State");
    want_value(&r[0], "ok", "the library's state");
    want_label(&r[1], "Tracks", "About row 7 label");
    want_value(&r[1], "6 tracks", "the library's track count");
    want_label(&r[2], "Directories", "About row 8 label");
    want_value(&r[2], "4 dirs", "the library's directory count");
    want_label(&r[3], "Size", "About row 9 label");
    want_value(&r[3], "210 B", "the library's size");
    want_label(&r[4], "Truncated", "About row 10 label");
    want_value(&r[4], "No", "the library is not truncated");
    for (int i = 0; i < MENU_ROWS; i++) {
        want_icon(&r[i], MENU_ICON_LIBRARY, "the library's rows wear books");
    }
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "activating a value row does nothing");

    /* Idle: the status carries no format, rate or bit rate. */
    st = status_idle();
    menu_update(m, lib, &facts, &st);
    menu_step(m, -10);
    read_rows(m, r);
    want_value(&r[3], EM_DASH, "an idle format is an em dash");
    want_value(&r[4], EM_DASH, "an idle rate is an em dash");
    menu_step(m, 5);
    read_rows(m, r);
    want_value(&r[4], EM_DASH, "an idle bitrate is an em dash");
    menu_step(m, -5);

    /* NULL facts read as an em dash. */
    facts.version = NULL;
    facts.board = NULL;
    facts.partition = NULL;
    menu_update(m, lib, &facts, NULL);
    read_rows(m, r);
    want_value(&r[0], EM_DASH, "a NULL version is an em dash");
    want_value(&r[1], EM_DASH, "a NULL board is an em dash");
    want_value(&r[2], EM_DASH, "a NULL partition is an em dash");

    /* The facts' strings are copied: a change to the caller's buffer without a
     * menu_update() does not reach the row. */
    char version[16] = "9.9.9";
    facts.version = version;
    facts.board = "xtape-esp32s3";
    facts.partition = "1.68 MB of 4 MB";
    menu_update(m, lib, &facts, NULL);
    snprintf(version, sizeof version, "%s", "changed");
    read_rows(m, r);
    want_value(&r[0], "9.9.9", "the version string is copied at update time");
    want_value(&r[1], "xtape-esp32s3", "the board string is copied at update time");
    want_value(&r[2], "1.68 MB of 4 MB", "the partition string is copied at update time");

    menu_free(m);
    library_close(lib);
    remove_tree(root);
    free(root);
}

static void test_browse_rows(void)
{
    char *root = make_tree();
    library_t *lib = lib_from(root);
    menu_facts_t facts = facts_plain();
    menu_t *m = menu_open_with(lib, &facts);

    CHECK_EQ(menu_band(m), 0, "the band is on Browse");
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "the Browse row pushes the directory");
    want_str(menu_title(m), "Browse", "Browse's root title");
    CHECK_EQ(menu_count(m), 4, "the root directory has four children");
    CHECK_EQ(menu_position(m), 1, "Browse position 1");

    menu_row_t r[MENU_ROWS];
    read_rows(m, r);
    want_label(&r[0], "Caf\xc3\xa9", "Browse row 0 is a Track");
    want_kind(&r[0], MENU_ROW_TRACK, "Browse mixes Tracks in");
    CHECK(!r[0].chevron, "a Track inside Browse has no chevron");

    want_label(&r[1], "Jazz", "Browse row 1 is a directory");
    want_kind(&r[1], MENU_ROW_DIR, "a directory row opens Browse");
    want_icon(&r[1], MENU_ICON_DIRECTORY, "a directory row wears cassette-tape");
    CHECK(r[1].chevron, "a directory row wears a chevron");
    CHECK(r[1].enabled, "a directory row is live");

    want_label(&r[2], "loose song", "the library's name order mixes dirs and Tracks");
    want_kind(&r[2], MENU_ROW_TRACK, "Browse row 2 is a Track");
    want_label(&r[3], "Rock", "Browse row 3 is a directory");
    want_kind(&r[3], MENU_ROW_DIR, "Browse row 3 is a directory");

    /* A directory pushes another Browse list and the title carries the path. */
    menu_step(m, 1);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "a directory pushes Browse");
    want_str(menu_title(m), "Browse /Jazz", "the title carries the path");
    CHECK_EQ(menu_count(m), 2, "Jazz has two children");
    CHECK_EQ(menu_band(m), 0, "the pushed directory starts at its first row");
    read_rows(m, r);
    want_label(&r[0], "Blue Train", "Jazz's first child");
    want_kind(&r[0], MENU_ROW_DIR, "Blue Train is a directory");
    want_label(&r[1], "Miles", "Jazz's second child");
    want_kind(&r[1], MENU_ROW_DIR, "Miles is a directory");

    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "a directory pushes Browse");
    want_str(menu_title(m), "Browse /Jazz /Blue Train", "the title carries both components");
    CHECK_EQ(menu_count(m), 2, "Blue Train has two Tracks");
    read_rows(m, r);
    want_label(&r[0], "01 - Blue Train", "a Track inside Browse is cleaned too");
    want_label(&r[1], "02 - Moment s Notice", "the second Track inside Browse");
    want_kind(&r[1], MENU_ROW_TRACK, "Blue Train's second child is a Track");

    mini_icecast_status_t st =
        status_playing("Jazz/Blue Train/02 - Moment's Notice.flac", MI_FMT_FLAC, 44100, 192);
    menu_update(m, lib, &facts, &st);
    read_rows(m, r);
    CHECK(!r[0].playing, "the first Track is not playing");
    CHECK(r[1].playing, "the second Track carries the marker");

    menu_step(m, 1);
    CHECK_EQ(menu_activate(m), MENU_ACTION_PLAY, "a Track inside Browse returns PLAY");
    want_str(menu_action_path(m), "Jazz/Blue Train/02 - Moment's Notice.flac",
             "the Browse action path is relative to the Library root");

    menu_free(m);
    library_close(lib);
    remove_tree(root);
    free(root);
}

static void test_back_paths(void)
{
    char *root = make_tree();
    library_t *lib = lib_from(root);
    menu_facts_t facts = facts_plain();

    /* Down the Browse tree, then back out one list at a time. */
    menu_t *m = menu_open_with(lib, &facts);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "Browse root");
    menu_step(m, 1);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "Jazz");
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "Blue Train");
    want_str(menu_title(m), "Browse /Jazz /Blue Train", "deep title");

    CHECK(!menu_back(m), "Back inside Browse does not close the Menu");
    want_str(menu_title(m), "Browse /Jazz", "Back returns to the parent directory");
    CHECK_EQ(menu_band(m), 0, "the band is on the directory's own row");
    CHECK_EQ(menu_position(m), 1, "Blue Train is Jazz's first child");

    CHECK(!menu_back(m), "Back from Jazz stays in the Menu");
    want_str(menu_title(m), "Browse", "Back returns to Browse's root");
    CHECK_EQ(menu_band(m), 1, "the band is on the Jazz row");
    CHECK_EQ(menu_position(m), 2, "Jazz is the root's second child");

    CHECK(!menu_back(m), "Back from Browse's root stays in the Menu");
    want_str(menu_title(m), "Menu", "Back from Browse lands on the root list");
    CHECK_EQ(menu_band(m), 0, "the band is on the Browse row");
    CHECK_EQ(menu_position(m), 1, "Browse is the root's first row");

    CHECK(menu_back(m), "Back from the root closes the Menu");
    CHECK(!menu_is_open(m), "the Menu is closed");
    CHECK(!menu_back(m), "Back while closed does nothing");
    menu_free(m);

    /* Settings and its lists. */
    m = menu_open_with(lib, &facts);
    menu_step(m, 2);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "Settings");
    menu_step(m, 2);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "Wi-Fi");
    CHECK(!menu_back(m), "Back from Wi-Fi stays in the Menu");
    want_str(menu_title(m), "Settings", "Back returns to Settings");
    CHECK_EQ(menu_band(m), 2, "the band is on the Wi-Fi row");

    menu_step(m, 1);
    CHECK_EQ(menu_band(m), 3, "the band is on About");
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "About");
    CHECK(!menu_back(m), "Back from About stays in the Menu");
    CHECK_EQ(menu_band(m), 3, "the band is on the About row");

    CHECK(!menu_back(m), "Back from Settings stays in the Menu");
    want_str(menu_title(m), "Menu", "Back returns to the root");
    CHECK_EQ(menu_band(m), 2, "the band is on the Settings row");
    CHECK(menu_back(m), "Back from the root closes the Menu");
    menu_free(m);

    /* Playlists returns to its root row. */
    menu_facts_t with_lists = facts_with_playlists(g_long_lists, 4, 0);
    m = menu_open_with(lib, &with_lists);
    menu_step(m, 1);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "Playlists");
    CHECK_EQ(menu_count(m), 5, "Library order plus four lists");
    CHECK(!menu_back(m), "Back from Playlists stays in the Menu");
    want_str(menu_title(m), "Menu", "Back returns to the root");
    CHECK_EQ(menu_band(m), 1, "the band is on the Playlists row");
    menu_free(m);

    library_close(lib);
    remove_tree(root);
    free(root);
}

static void test_remembered_state(void)
{
    char *root = make_tree();
    library_t *lib = lib_from(root);
    menu_facts_t facts = g_long_facts;

    menu_t *m = menu_open_with(lib, &facts);
    want_str(menu_title(m), "Menu", "the root on the first open");
    CHECK_EQ(menu_band(m), 0, "the root's band starts at 0 on the first open");

    menu_step(m, 1);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "Playlists");
    menu_step(m, 5);
    CHECK_EQ(menu_band(m), 4, "the band is on the last visible slot");
    CHECK_EQ(menu_position(m), 6, "the position is the sixth row");
    menu_close(m);
    CHECK(!menu_is_open(m), "close keeps the model");
    menu_open(m, lib, &facts);
    want_str(menu_title(m), "Playlists", "the list is remembered");
    CHECK_EQ(menu_count(m), LONG_LISTS + 1, "the list is rebuilt");
    CHECK_EQ(menu_band(m), 4, "the screen row is remembered");
    CHECK_EQ(menu_position(m), 6, "the band is remembered");
    menu_row_t r[MENU_ROWS];
    read_rows(m, r);
    want_label(&r[0], "List 00", "the window is remembered too");

    /* The Settings band survives a close as well. */
    CHECK(!menu_back(m), "back to the root");
    menu_step(m, 1);
    CHECK_EQ(menu_band(m), 2, "the band is on Settings");
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "Settings");
    menu_step(m, 3);
    CHECK_EQ(menu_band(m), 3, "the band is on About");
    menu_close(m);
    menu_open(m, lib, &facts);
    want_str(menu_title(m), "Settings", "the Settings list is remembered");
    CHECK_EQ(menu_band(m), 3, "the Settings band is remembered");
    CHECK_EQ(menu_position(m), 4, "the Settings position is remembered");
    menu_free(m);

    library_close(lib);
    remove_tree(root);
    free(root);
}

/* Open a good library, descend into Browse, close, and reopen on a library that
 * cannot be read: the remembered list holds one inert note. */
static void note_case(const library_t *bad, const char *want)
{
    char *root = make_tree();
    library_t *good = lib_from(root);
    menu_facts_t facts = facts_plain();
    menu_t *m = menu_open_with(good, &facts);

    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "the good library opens Browse");
    CHECK_EQ(menu_count(m), 4, "the good Browse list has rows");
    menu_close(m);

    menu_open(m, bad, &facts);
    want_str(menu_title(m), "Browse", "the list is remembered");
    CHECK_EQ(menu_count(m), 1, "an unreadable list holds one note");
    CHECK_EQ(menu_position(m), 1, "the note's position is 1");

    menu_row_t r[MENU_ROWS];
    read_rows(m, r);
    want_label(&r[0], want, "the note repeats the library's state");
    want_kind(&r[0], MENU_ROW_NOTE, "the empty-state row is a note");
    CHECK(!r[0].enabled, "the note is inert");
    CHECK(!r[0].chevron, "the note has no chevron");
    CHECK(!r[0].playing, "the note is not a Track");
    CHECK(r[1].label[0] == 0, "a note list leaves the rest of the page blank");
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "activating the note does nothing");

    menu_free(m);
    library_close(good);
    remove_tree(root);
    free(root);
}

static void test_no_library(void)
{
    library_t *nc = lib_no_card("/tmp/xtape-no-card");
    note_case(nc, "No card");
    note_case(NULL, "No card");

    char *empty_root = temp_root();
    library_t *empty = lib_from(empty_root);
    CHECK_EQ(library_state(empty), LIB_STATE_EMPTY, "the empty fixture is empty");
    note_case(empty, "Library empty");
    library_close(empty);
    remove_tree(empty_root);
    free(empty_root);

    char *empty_root2 = temp_root();
    library_t *empty2 = lib_from(empty_root2);
    menu_facts_t empty_facts = facts_plain();
    menu_t *me = menu_open_with(empty2, &empty_facts);
    menu_row_t er[MENU_ROWS];
    read_rows(me, er);
    CHECK(!er[0].enabled, "Browse is dimmed for an empty library");
    CHECK(!er[1].enabled, "Playlists is dimmed for an empty library");
    CHECK_EQ(menu_activate(me), MENU_ACTION_NONE, "Browse is inert for an empty library");
    want_str(menu_title(me), "Menu", "the model stays on the root");
    menu_free(me);
    library_close(empty2);
    remove_tree(empty_root2);
    free(empty_root2);

    library_cfg_t bad_cfg = {.music_dir = "/definitely/not/a/xtape/music",
                             .generation = library_next_generation()};
    library_t *nodir = lib_from_cfg(&bad_cfg);
    CHECK_EQ(library_state(nodir), LIB_STATE_NO_DIR, "the bad fixture has no directory");
    note_case(nodir, "Library error");
    library_close(nodir);

    /* No card: the root still opens; Browse and Playlists are dimmed and inert, and
     * the lists that explain and fix it stay reachable. */
    menu_facts_t facts = facts_plain();
    menu_t *m = menu_open_with(nc, &facts);
    menu_row_t r[MENU_ROWS];
    read_rows(m, r);
    CHECK(!r[0].enabled, "Browse is dimmed with no card");
    CHECK(!r[1].enabled, "Playlists is dimmed with no card");
    CHECK(r[2].enabled, "Settings still works with no card");
    CHECK(r[3].enabled, "Shuffle still works with no card");

    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "a dimmed Browse row is inert");
    want_str(menu_title(m), "Menu", "the model stays on the root");
    CHECK_EQ(menu_band(m), 0, "the band does not move");
    menu_step(m, 1);
    CHECK_EQ(menu_band(m), 1, "the band is on Playlists");
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "a dimmed Playlists row is inert");
    CHECK_EQ(menu_band(m), 1, "the band does not move from Playlists");

    menu_step(m, 1);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "Settings opens with no card");
    menu_step(m, 3);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "About opens with no card");
    menu_step(m, 10);
    read_rows(m, r);
    want_value(&r[0], "no_card", "About explains the missing card");
    menu_free(m);

    library_close(nc);
}

static void test_about_big_values(void)
{
    menu_facts_t facts = facts_plain();
    menu_t *m = menu_open_with(g_many_lib, &facts);

    menu_step(m, 2);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "opening Settings is not an action");
    menu_step(m, 3);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "opening About is not an action");
    menu_step(m, 10);

    menu_row_t r[MENU_ROWS];
    read_rows(m, r);
    want_value(&r[0], "ok", "the many fixture is an ok library");
    want_value(&r[1], "1,204 tracks", "thousands are grouped");
    want_value(&r[2], "38 dirs", "the directory count");
    want_value(&r[3], "12.4 GB", "the size is decimal GB over binary bytes");
    want_value(&r[4], "No", "the many fixture is not truncated");

    menu_free(m);
}

static void test_band_window(void)
{
    menu_facts_t facts = g_long_facts;
    menu_t *m = menu_open_with(g_many_lib, &facts);

    menu_step(m, 1);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "the Playlists row pushes the lists");
    CHECK_EQ(menu_count(m), LONG_LISTS + 1, "Library order plus the fixture's lists");
    CHECK_EQ(menu_band(m), 0, "the band starts on the first visible row");
    CHECK_EQ(menu_position(m), 1, "the position is 1");

    menu_row_t r[MENU_ROWS];
    read_rows(m, r);
    want_label(&r[0], "Library order", "the first row is Library order");
    want_label(&r[1], "List 00", "the first list");
    want_label(&r[4], "List 03", "the fifth visible row");

    menu_step(m, 4);
    CHECK_EQ(menu_band(m), 4, "the band moves inside the page");
    CHECK_EQ(menu_position(m), 5, "the band is on row 5");

    menu_step(m, 1);
    CHECK_EQ(menu_band(m), 4, "at the edge the band stays on the edge row");
    CHECK_EQ(menu_position(m), 6, "the window shifted instead");
    read_rows(m, r);
    want_label(&r[0], "List 00", "the window starts one row later");

    menu_step(m, -1);
    CHECK_EQ(menu_band(m), 3, "up inside the page moves the band");
    CHECK_EQ(menu_position(m), 5, "the position follows the band");
    read_rows(m, r);
    want_label(&r[0], "List 00", "the window does not shift until the top edge");

    menu_step(m, -4);
    CHECK_EQ(menu_band(m), 0, "the top edge is the first row");
    CHECK_EQ(menu_position(m), 1, "the first position");
    read_rows(m, r);
    want_label(&r[0], "Library order", "the window is back at the top");

    menu_step(m, 5000);
    CHECK_EQ(menu_band(m), 4, "a long step lands on the page's last slot");
    CHECK_EQ(menu_position(m), LONG_LISTS + 1, "a long step lands on the last row");
    read_rows(m, r);
    want_label(&r[0], "List 27", "the last window starts six rows from the end");
    want_label(&r[4], "List 31", "the last visible list");

    menu_step(m, 1);
    CHECK_EQ(menu_position(m), LONG_LISTS + 1, "a step past the end does nothing");
    CHECK_EQ(menu_band(m), 4, "the band stays on the last visible row");

    menu_step(m, -5000);
    CHECK_EQ(menu_position(m), 1, "a long up step clamps at the top");
    CHECK_EQ(menu_band(m), 0, "the band is on the first row");

    /* A short list leaves blank rows and never shifts its window. */
    CHECK(!menu_back(m), "Back returns to the root");
    CHECK_EQ(menu_count(m), 4, "the root has four rows");
    menu_step(m, 10);
    CHECK_EQ(menu_band(m), 3, "the band clamps at the root's last row");
    CHECK_EQ(menu_position(m), 4, "the root's last position");
    read_rows(m, r);
    want_label(&r[3], "Shuffle", "the last root row is Shuffle");
    CHECK(r[4].label[0] == 0, "a short list leaves its fifth slot blank");
    menu_step(m, 1);
    CHECK_EQ(menu_band(m), 3, "the short list's band cannot leave the list");

    menu_free(m);
}

/* The ratchet's pinned constants: 54 px/row at rest, 2x at 1200 px/s, 4x at
 * 2400 px/s, and a 150 ms hold before the stage drops back. */
static void test_ratchet(void)
{
    menu_facts_t facts = g_long_facts;
    menu_t *m = menu_open_with(g_many_lib, &facts);
    menu_step(m, 1);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "the Playlists row pushes the lists");

    /* One row per 54 px at rest, and only whole rows. menu_position() is the
     * absolute 1-based row, so it is what a ratchet assertion wants. */
    menu_drag(m, 53, 200);
    CHECK_EQ(menu_position(m), 1, "53 px is not yet a row");
    menu_drag(m, 1, 200);
    CHECK_EQ(menu_position(m), 2, "the 54th pixel is a row");
    menu_drag(m, 108, 1000);
    CHECK_EQ(menu_position(m), 4, "a slow drag of 108 px is two rows");

    /* A direction change drops the partial row. */
    menu_drag(m, 40, 1000);
    CHECK_EQ(menu_position(m), 4, "40 px of travel is partial");
    menu_drag(m, -20, 1000);
    CHECK_EQ(menu_position(m), 4, "the direction change resets the partial travel");
    menu_drag(m, -34, 1000);
    CHECK_EQ(menu_position(m), 3, "-54 px from the reset is one row up");

    /* dt = 0 must not divide by zero, and still ratchets. */
    menu_drag(m, 54, 0);
    CHECK_EQ(menu_position(m), 4, "dt 0 still steps a whole row");

    /* 2x while fast. */
    menu_drag(m, 108, 50); /* 2160 px/s */
    CHECK_EQ(menu_position(m), 8, "2x is 27 px per row");

    /* 4x while faster. */
    menu_drag(m, 104, 20); /* 5200 px/s */
    CHECK_EQ(menu_position(m), 16, "4x is 13 px per row");

    /* The stage is held for 150 ms after the speed drops. */
    menu_drag(m, 13, 100); /* 130 px/s, still 4x, 13 px = one row */
    CHECK_EQ(menu_position(m), 17, "the 4x stage survives 100 ms of slow travel");
    menu_drag(m, 13, 100); /* 200 ms of slow travel: the stage drops to 1x */
    CHECK_EQ(menu_position(m), 17, "back at 1x, 13 px is not a row");
    menu_drag(m, 41, 1000); /* 13 + 41 = 54 px at 1x */
    CHECK_EQ(menu_position(m), 18, "the rest of the row completes at 1x");

    menu_free(m);
}

static void test_empty_browse(void)
{
    char *root = temp_root();
    mkdir_at(root, "Empty");
    file_at(root, "a.mp3", 1);
    library_t *lib = lib_from(root);
    menu_facts_t facts = facts_plain();
    menu_t *m = menu_open_with(lib, &facts);

    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "Browse root");
    CHECK_EQ(menu_count(m), 2, "the root has a Track and an empty directory");

    menu_row_t r[MENU_ROWS];
    read_rows(m, r);
    want_label(&r[0], "a", "the Track sorts before the directory");
    want_label(&r[1], "Empty", "the empty directory");

    menu_step(m, 1);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "an empty directory opens an empty list");
    want_str(menu_title(m), "Browse /Empty", "the empty list keeps its path");
    CHECK_EQ(menu_count(m), 0, "an empty list has no rows");
    CHECK_EQ(menu_position(m), 0, "an empty list has position 0");
    CHECK_EQ(menu_band(m), 0, "the band sits on screen row 0");
    read_rows(m, r);
    for (int i = 0; i < MENU_ROWS; i++) CHECK(r[i].label[0] == 0, "the empty page is blank");
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "an empty list activates nothing");
    menu_step(m, 1);
    CHECK_EQ(menu_count(m), 0, "stepping an empty list changes nothing");

    menu_free(m);
    library_close(lib);
    remove_tree(root);
    free(root);
}

static void test_update_generation(void)
{
    char *root = make_tree();
    library_t *lib1 = lib_from(root);
    menu_facts_t facts = facts_plain();
    menu_t *m = menu_open_with(lib1, &facts);

    /* Deep in Browse. */
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "Browse root");
    menu_step(m, 1);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "Jazz");
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "Blue Train");
    want_str(menu_title(m), "Browse /Jazz /Blue Train", "the deep title");

    /* The same tree as a new generation: the path re-resolves and nothing that is
     * drawn changes, so the version does not move. */
    uint32_t before = menu_version(m);
    library_t *lib2 = lib_from(root);
    CHECK(library_generation(lib2) != library_generation(lib1), "the generations differ");
    menu_update(m, lib2, &facts, NULL);
    want_str(menu_title(m), "Browse /Jazz /Blue Train", "the path survives a rescan");
    CHECK_EQ(menu_count(m), 2, "the directory is re-resolved from the new tree");
    CHECK_EQ(menu_version(m), before, "a rescan that draws the same rows does not bump");

    /* A generation where the path has vanished pops to Browse's root. */
    char *other = temp_root();
    file_at(other, "a.mp3", 1);
    library_t *lib3 = lib_from(other);
    menu_update(m, lib3, &facts, NULL);
    want_str(menu_title(m), "Browse", "a vanished path pops to Browse's root");
    CHECK_EQ(menu_count(m), 1, "the new tree's root has one child");
    CHECK_EQ(menu_band(m), 0, "the band clamps");
    CHECK_EQ(menu_position(m), 1, "the position is 1");
    menu_row_t r[MENU_ROWS];
    read_rows(m, r);
    want_label(&r[0], "a", "the rows come from the new generation");

    /* The Playlists list is rebuilt from the facts and its band clamps. */
    menu_update(m, lib3, &g_long_facts, NULL);
    CHECK(!menu_back(m), "Back to the root");
    menu_step(m, 1);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "Playlists opens on the long index");
    CHECK_EQ(menu_count(m), LONG_LISTS + 1, "Library order plus the fixture's lists");
    menu_step(m, 20);
    CHECK_EQ(menu_position(m), 21, "deep in the lists");
    menu_facts_t small = facts_with_playlists(NULL, 0, 0);
    menu_update(m, lib3, &small, NULL);
    CHECK_EQ(menu_count(m), 2, "Library order and the no-lists note");
    CHECK_EQ(menu_band(m), 1, "the band clamps to the last row");
    CHECK_EQ(menu_position(m), 2, "the position clamps");
    menu_row_t p[MENU_ROWS];
    read_rows(m, p);
    want_label(&p[1], "No playlists", "the new index's note");

    /* A remembered Browse path is re-resolved by string when the Menu reopens. */
    menu_free(m);
    m = menu_open_with(lib1, &facts);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "Browse root");
    menu_step(m, 1);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "Jazz");
    menu_close(m);
    menu_open(m, lib3, &facts);
    want_str(menu_title(m), "Browse", "a remembered path that vanished pops to the root");
    CHECK_EQ(menu_count(m), 1, "the new tree's Browse root");
    menu_free(m);

    library_close(lib1);
    library_close(lib2);
    library_close(lib3);
    remove_tree(root);
    remove_tree(other);
    free(root);
    free(other);
}

static void test_version(void)
{
    char *root = make_tree();
    library_t *lib = lib_from(root);
    menu_facts_t facts = g_long_facts;
    menu_t *m = menu_open_with(lib, &facts);

    uint32_t v0 = menu_version(m);
    menu_update(m, lib, &facts, NULL);
    CHECK_EQ(menu_version(m), v0, "an identical update does not bump the version");

    mini_icecast_status_t st = status_playing("Caf\xc3\xa9.mp3", MI_FMT_MP3, 44100, 192);
    menu_update(m, lib, &facts, &st);
    CHECK_EQ(menu_version(m), v0, "the root does not draw the broadcast's status");

    menu_step(m, 1);
    CHECK_EQ(menu_activate(m), MENU_ACTION_NONE, "Playlists opens");
    uint32_t v1 = menu_version(m);
    CHECK(v1 != v0, "pushing a list bumps the version");

    menu_update(m, lib, &facts, &st);
    CHECK_EQ(menu_version(m), v1, "the same rows and title do not bump");

    /* A partial row moves nothing on screen. */
    menu_drag(m, 53, 1000);
    CHECK_EQ(menu_version(m), v1, "a partial row does not bump");

    menu_drag(m, 1, 1000);
    uint32_t v2 = menu_version(m);
    CHECK(v2 != v1, "a band move bumps the version");

    /* The dot is part of the rows. */
    facts.playlist_selected = 1;
    menu_update(m, lib, &facts, &st);
    CHECK(menu_version(m) != v2, "the walking dot moving bumps the version");
    uint32_t v3 = menu_version(m);

    menu_step(m, 100);
    uint32_t v4 = menu_version(m);
    CHECK(v4 != v3, "a window shift bumps the version");
    menu_step(m, 1);
    CHECK_EQ(menu_version(m), v4, "a clamped step does not bump");

    CHECK(!menu_back(m), "Back to the root");
    uint32_t v5 = menu_version(m);
    CHECK(v5 != v4, "the title and rows changing bumps the version");
    CHECK(menu_back(m), "Back from the root closes the Menu");
    CHECK_EQ(menu_version(m), v5, "closing does not change what was drawn");

    menu_free(m);
    library_close(lib);
    remove_tree(root);
    free(root);
}

int main(void)
{
    long_lists_setup();

    test_clean_name();
    test_root_rows();
    test_settings_rows();
    test_wifi_list();
    test_about_rows();
    test_playlists_rows();
    test_browse_rows();
    test_back_paths();
    test_remembered_state();
    test_no_library();

    many_setup();
    test_about_big_values();
    test_band_window();
    test_ratchet();
    test_empty_browse();
    test_update_generation();
    test_version();
    many_teardown();

    TEST_END();
}
