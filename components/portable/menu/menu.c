/*
 * menu - the Menu's model: the tree, the navigation, the window, the band and the
 * pad's ratchet. Portable: no LVGL, no card, no board (docs/specs/menu.md, ADR 0022).
 *
 * The model reads the Library through the pointer it is handed and never touches the
 * card. Everything the view can see is derived state: the five visible rows and the
 * title are rebuilt from the current list, the facts and the broadcast's status, and
 * menu_version() only moves when one of those would draw differently.
 *
 * The ratchet's constants are tuned on the board (docs/specs/menu.md, "Open items");
 * the values here are the ones the host tests pin.
 */
#include "menu.h"

#include "cassette.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MENU_EM_DASH "\xe2\x80\x94" /* "nothing known", the same glyph About uses */

/* The ratchet: 54 px per row at rest - the rows are 53-54 px tall (docs/specs/menu.md) -
 * then 2x and 4x while the finger is fast. A stage is held for MENU_STAGE_HOLD_MS after the
 * speed drops so a stutter mid-drag does not drop the multiplier. */
#define MENU_RATCHET_PX     54
#define MENU_SPEED_STAGE2   1200u /* px/s */
#define MENU_SPEED_STAGE4   2400u /* px/s */
#define MENU_STAGE_HOLD_MS  150u

#define MENU_FACTS_MAX 64
#define MENU_TITLE_MAX (LIB_PATH_MAX + 16)

typedef enum {
    LIST_ROOT = 0,
    LIST_SETTINGS,
    LIST_TRACKS,
    LIST_BROWSE,
    LIST_STATION,
    LIST_LIBRARY,
    LIST_ABOUT,
} menu_list_t;

typedef struct {
    const char      *label;
    menu_icon_t      icon;
    menu_row_kind_t  kind;
} fixed_row_t;

static const fixed_row_t k_root_rows[] = {
    {"Tracks", MENU_ICON_TRACKS, MENU_ROW_SUBMENU},
    {"Browse", MENU_ICON_BROWSE, MENU_ROW_SUBMENU},
    {"Shuffle", MENU_ICON_SHUFFLE, MENU_ROW_ACTION},
    {"Settings", MENU_ICON_SETTINGS, MENU_ROW_SUBMENU},
};

static const fixed_row_t k_settings_rows[] = {
    {"Backlight", MENU_ICON_BACKLIGHT, MENU_ROW_ACTION},
    {"Station", MENU_ICON_STATION, MENU_ROW_SUBMENU},
    {"Library", MENU_ICON_LIBRARY, MENU_ROW_SUBMENU},
    {"Rescan", MENU_ICON_RESCAN, MENU_ROW_ACTION},
    {"About", MENU_ICON_ABOUT, MENU_ROW_SUBMENU},
};

struct menu {
    bool        open;
    menu_list_t list;
    int         band;   /* absolute in the list */
    int         window; /* the list index drawn in the first of the five slots */

    char     browse_path[LIB_PATH_MAX]; /* relative to the Library root; "" is the root */
    lib_node_t browse_node;

    const library_t *lib; /* the caller's lease; NULL when closed or cardless */
    uint32_t         lib_generation;

    menu_facts_t facts; /* version/board/partition point into the buffers below */
    char         version_buf[MENU_FACTS_MAX];
    char         board_buf[MENU_FACTS_MAX];
    char         partition_buf[MENU_FACTS_MAX];

    char     status_path[LIB_PATH_MAX];
    mi_fmt_t status_format;
    uint32_t status_out_hz;
    uint32_t status_bitrate;

    char         action_path[LIB_PATH_MAX];
    int          action_backlight;

    /* The ratchet. */
    int      stage;
    int      acc_px;
    uint32_t slow_ms;

    /* The last thing drawn, so menu_version() can be precise. */
    char       title[MENU_TITLE_MAX];
    char       shown_title[MENU_TITLE_MAX];
    menu_row_t rows[MENU_ROWS];
    int        shown_count;
    int        shown_band;
    uint32_t   version;
};

/* ------------------------------------------------------------- small helpers */

static bool lib_ok(const menu_t *m)
{
    return m->lib && library_state(m->lib) == LIB_STATE_OK;
}

static void copy_opt(char *dst, size_t cap, const char *src, const char **out)
{
    if (!src) {
        dst[0] = 0;
        *out = NULL;
        return;
    }
    snprintf(dst, cap, "%s", src);
    *out = dst;
}

static void facts_copy(menu_t *m, const menu_facts_t *facts)
{
    if (!facts) {
        memset(&m->facts, 0, sizeof m->facts);
        m->version_buf[0] = 0;
        m->board_buf[0] = 0;
        m->partition_buf[0] = 0;
        return;
    }
    m->facts.shuffle_on = facts->shuffle_on;
    m->facts.rescanning = facts->rescanning;
    m->facts.backlight = facts->backlight;
    copy_opt(m->version_buf, sizeof m->version_buf, facts->version, &m->facts.version);
    copy_opt(m->board_buf, sizeof m->board_buf, facts->board, &m->facts.board);
    copy_opt(m->partition_buf, sizeof m->partition_buf, facts->partition, &m->facts.partition);
}

static void status_copy(menu_t *m, const mini_icecast_status_t *status)
{
    if (!status) {
        m->status_path[0] = 0;
        m->status_format = MI_FMT_NONE;
        m->status_out_hz = 0;
        m->status_bitrate = 0;
        return;
    }
    snprintf(m->status_path, sizeof m->status_path, "%s", status->path);
    m->status_format = status->format;
    m->status_out_hz = status->out_hz;
    m->status_bitrate = status->bitrate;
}

static void group_thousands(uint32_t n, char *out, size_t cap)
{
    char raw[16];
    snprintf(raw, sizeof raw, "%" PRIu32, n);
    size_t len = strlen(raw);
    size_t pos = 0;
    for (size_t i = 0; i < len; i++) {
        if (i && (len - i) % 3 == 0 && pos + 1 < cap) out[pos++] = ',';
        if (pos + 1 < cap) out[pos++] = raw[i];
    }
    out[pos] = 0;
}

static void format_items(char *out, size_t cap, uint32_t n, const char *singular,
                         const char *plural)
{
    char grouped[24];
    group_thousands(n, grouped, sizeof grouped);
    snprintf(out, cap, "%s %s", grouped, n == 1 ? singular : plural);
}

/* Binary multiples with one decimal: 12.4 GB is 12.4 * 1024^3 bytes. */
static void format_size(char *out, size_t cap, uint64_t bytes)
{
    if (bytes < 1024u) {
        snprintf(out, cap, "%llu B", (unsigned long long)bytes);
    } else if (bytes < 1024u * 1024u) {
        snprintf(out, cap, "%.1f KB", (double)bytes / 1024.0);
    } else if (bytes < 1024u * 1024u * 1024u) {
        snprintf(out, cap, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    } else {
        snprintf(out, cap, "%.1f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    }
}

/* Tenths of a kHz, the ".0" dropped: 44100 -> "44.1 kHz", 48000 -> "48 kHz". */
static void format_hz(char *out, size_t cap, uint32_t hz)
{
    if (hz == 0) {
        snprintf(out, cap, "%s", MENU_EM_DASH);
        return;
    }
    uint32_t tenths = (uint32_t)(((uint64_t)hz + 50u) / 100u);
    if (tenths % 10u == 0) snprintf(out, cap, "%" PRIu32 " kHz", tenths / 10u);
    else snprintf(out, cap, "%" PRIu32 ".%" PRIu32 " kHz", tenths / 10u, tenths % 10u);
}

static const char *format_name(mi_fmt_t fmt)
{
    switch (fmt) {
    case MI_FMT_MP3: return "MP3";
    case MI_FMT_FLAC: return "FLAC";
    case MI_FMT_WAV: return "WAV";
    default: return MENU_EM_DASH;
    }
}

static const char *backlight_name(int pct)
{
    switch (pct) {
    case 100: return "100%";
    case 60: return "60%";
    case 30: return "30%";
    default: return "Auto"; /* 0 is Auto; an unknown value falls back to it */
    }
}

static int next_backlight(int pct)
{
    switch (pct) {
    case 0: return 100;
    case 100: return 60;
    case 60: return 30;
    default: return 0; /* 30% returns to Auto */
    }
}

/* The empty-state line for Tracks and Browse. NO_DIR and ERROR both explain
 * themselves as a library error; EMPTY (and an OK generation with no Tracks,
 * which the library does not produce) is an empty library. */
static const char *note_label(const menu_t *m)
{
    if (!m->lib) return "No card";
    switch (library_state(m->lib)) {
    case LIB_STATE_NO_CARD: return "No card";
    case LIB_STATE_EMPTY: return "Library empty";
    default: return "Library error";
    }
}

static bool tracks_empty(const menu_t *m)
{
    return !lib_ok(m) || library_track_count(m->lib) == 0;
}

static lib_node_t browse_dir(const menu_t *m)
{
    if (!lib_ok(m)) return LIB_NODE_NONE;
    if (m->browse_node != LIB_NODE_NONE) return m->browse_node;
    return library_root(m->lib);
}

/* ------------------------------------------------------------------- lists */

static int list_count(const menu_t *m)
{
    switch (m->list) {
    case LIST_ROOT: return (int)(sizeof k_root_rows / sizeof k_root_rows[0]);
    case LIST_SETTINGS: return (int)(sizeof k_settings_rows / sizeof k_settings_rows[0]);
    case LIST_STATION: return 3;
    case LIST_LIBRARY: return 5;
    case LIST_ABOUT: return 3;
    case LIST_TRACKS:
        if (tracks_empty(m)) return 1; /* the note row */
        return (int)library_track_count(m->lib);
    case LIST_BROWSE:
        if (!lib_ok(m)) return 1; /* the note row */
        return (int)library_child_count(m->lib, browse_dir(m));
    }
    return 0;
}

static void clamp_view(menu_t *m)
{
    int count = list_count(m);
    if (count <= 0) {
        m->band = 0;
        m->window = 0;
        return;
    }
    if (m->band < 0) m->band = 0;
    if (m->band > count - 1) m->band = count - 1;
    if (count <= MENU_ROWS) {
        m->window = 0;
        return;
    }
    int max_window = count - MENU_ROWS;
    if (m->window < 0) m->window = 0;
    if (m->window > max_window) m->window = max_window;
    if (m->band < m->window) m->window = m->band;
    else if (m->band > m->window + MENU_ROWS - 1) m->window = m->band - (MENU_ROWS - 1);
    if (m->window < 0) m->window = 0;
    if (m->window > max_window) m->window = max_window;
}

/* --------------------------------------------------------------- the title */

static void build_title(menu_t *m)
{
    switch (m->list) {
    case LIST_ROOT: snprintf(m->title, sizeof m->title, "Menu"); return;
    case LIST_SETTINGS: snprintf(m->title, sizeof m->title, "Settings"); return;
    case LIST_TRACKS: snprintf(m->title, sizeof m->title, "Tracks"); return;
    case LIST_STATION: snprintf(m->title, sizeof m->title, "Station"); return;
    case LIST_LIBRARY: snprintf(m->title, sizeof m->title, "Library"); return;
    case LIST_ABOUT: snprintf(m->title, sizeof m->title, "About"); return;
    case LIST_BROWSE: break;
    }

    /* "Browse" then each path component prefixed with " /" (the spec's own form:
     * "Browse /Jazz /Blue Train"). */
    int used = snprintf(m->title, sizeof m->title, "Browse");
    if (used < 0) used = 0;
    const char *p = m->browse_path;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : strlen(p);
        if ((size_t)used + 1 >= sizeof m->title) {
            used = (int)sizeof m->title - 1;
            break;
        }
        int n = snprintf(m->title + used, sizeof m->title - (size_t)used, " /%.*s", (int)len, p);
        if (n < 0) break;
        if ((size_t)n >= sizeof m->title - (size_t)used) {
            used = (int)sizeof m->title - 1;
            break;
        }
        used += n;
        if (!slash) break;
        p = slash + 1;
    }
    m->title[sizeof m->title - 1] = 0;
}

/* ---------------------------------------------------------------- row model */

static bool track_playing(const menu_t *m, lib_node_t node)
{
    if (!m->lib || !m->status_path[0]) return false;
    char rel[LIB_PATH_MAX];
    if (library_relpath(m->lib, node, rel, sizeof rel) == (size_t)-1) return false;
    return strcmp(rel, m->status_path) == 0;
}

static void set_track_label(const menu_t *m, lib_node_t node, menu_row_t *r)
{
    uint16_t len = 0;
    const char *name = library_name(m->lib, node, &len);
    if (!name) {
        r->label[0] = 0;
        return;
    }
    char raw[LIB_NAME_MAX + 1];
    snprintf(raw, sizeof raw, "%.*s", (int)len, name);
    menu_clean_name(raw, r->label, sizeof r->label);
}

static void build_note(const menu_t *m, menu_row_t *r)
{
    r->kind = MENU_ROW_NOTE;
    r->icon = MENU_ICON_NONE;
    r->enabled = false;
    r->chevron = false;
    r->playing = false;
    snprintf(r->label, sizeof r->label, "%s", note_label(m));
}

static void build_fixed(const fixed_row_t *f, menu_row_t *r)
{
    r->icon = f->icon;
    r->kind = f->kind;
    r->enabled = true;
    r->chevron = f->kind == MENU_ROW_SUBMENU;
    snprintf(r->label, sizeof r->label, "%s", f->label);
}

static void build_root_row(const menu_t *m, int index, menu_row_t *r)
{
    if (index < 0 || index >= (int)(sizeof k_root_rows / sizeof k_root_rows[0])) return;
    build_fixed(&k_root_rows[index], r);
    switch (index) {
    case 0: /* Tracks */
    case 1: /* Browse */
        r->enabled = lib_ok(m);
        break;
    case 2: /* Shuffle */
        snprintf(r->value, sizeof r->value, "%s", m->facts.shuffle_on ? "On" : "Off");
        break;
    default: break;
    }
}

static void build_settings_row(const menu_t *m, int index, menu_row_t *r)
{
    if (index < 0 || index >= (int)(sizeof k_settings_rows / sizeof k_settings_rows[0])) return;
    build_fixed(&k_settings_rows[index], r);
    switch (index) {
    case 0: /* Backlight */
        snprintf(r->value, sizeof r->value, "%s", backlight_name(m->facts.backlight));
        break;
    case 3: /* Rescan */
        snprintf(r->label, sizeof r->label, "%s",
                 m->facts.rescanning ? "Rescanning..." : "Rescan");
        r->enabled = !m->facts.rescanning;
        break;
    default: break;
    }
}

static void build_track_row(const menu_t *m, lib_node_t node, menu_row_t *r)
{
    r->kind = MENU_ROW_TRACK;
    r->icon = MENU_ICON_TRACK;
    r->enabled = true;
    r->chevron = false;
    set_track_label(m, node, r);
    r->playing = track_playing(m, node);
}

static void build_browse_row(const menu_t *m, int index, menu_row_t *r)
{
    lib_node_t node = library_child(m->lib, browse_dir(m), (uint32_t)index);
    if (node == LIB_NODE_NONE) return;
    if (library_kind(m->lib, node) == LIB_DIR) {
        uint16_t len = 0;
        const char *name = library_name(m->lib, node, &len);
        r->kind = MENU_ROW_DIR;
        r->icon = MENU_ICON_DIRECTORY;
        r->enabled = true;
        r->chevron = true;
        if (name) snprintf(r->label, sizeof r->label, "%.*s", (int)len, name);
    } else {
        build_track_row(m, node, r);
    }
}

static void build_station_row(const menu_t *m, int index, menu_row_t *r)
{
    static const char *const labels[3] = {"Format", "Rate", "Bitrate"};
    if (index < 0 || index > 2) return;
    r->kind = MENU_ROW_VALUE;
    r->icon = MENU_ICON_NONE;
    r->enabled = true;
    r->chevron = false;
    snprintf(r->label, sizeof r->label, "%s", labels[index]);
    switch (index) {
    case 0: snprintf(r->value, sizeof r->value, "%s", format_name(m->status_format)); break;
    case 1: format_hz(r->value, sizeof r->value, m->status_out_hz); break;
    default:
        if (m->status_bitrate) snprintf(r->value, sizeof r->value, "%" PRIu32 " kbps", m->status_bitrate);
        else snprintf(r->value, sizeof r->value, "%s", MENU_EM_DASH);
        break;
    }
}

static void build_library_row(const menu_t *m, int index, menu_row_t *r)
{
    static const char *const labels[5] = {"State", "Tracks", "Directories", "Size", "Truncated"};
    if (index < 0 || index > 4) return;
    r->kind = MENU_ROW_VALUE;
    r->icon = MENU_ICON_NONE;
    r->enabled = true;
    r->chevron = false;
    snprintf(r->label, sizeof r->label, "%s", labels[index]);
    switch (index) {
    case 0:
        if (m->lib) snprintf(r->value, sizeof r->value, "%s", library_state_name(library_state(m->lib)));
        else snprintf(r->value, sizeof r->value, "%s", MENU_EM_DASH);
        break;
    case 1: format_items(r->value, sizeof r->value, library_track_count(m->lib), "track", "tracks"); break;
    case 2: format_items(r->value, sizeof r->value, library_dir_count(m->lib), "dir", "dirs"); break;
    case 3: format_size(r->value, sizeof r->value, library_total_bytes(m->lib)); break;
    default: snprintf(r->value, sizeof r->value, "%s", library_truncated(m->lib) ? "Yes" : "No"); break;
    }
}

static void build_about_row(const menu_t *m, int index, menu_row_t *r)
{
    static const char *const labels[3] = {"Firmware", "Board", "Partition"};
    static const menu_icon_t icons[3] = { MENU_ICON_FIRMWARE, MENU_ICON_BOARD,
                                          MENU_ICON_PARTITION };
    if (index < 0 || index > 2) return;
    r->kind = MENU_ROW_VALUE;
    r->icon = icons[index];
    r->enabled = true;
    r->chevron = false;
    snprintf(r->label, sizeof r->label, "%s", labels[index]);
    const char *v = NULL;
    switch (index) {
    case 0: v = m->facts.version; break;
    case 1: v = m->facts.board; break;
    default: v = m->facts.partition; break;
    }
    snprintf(r->value, sizeof r->value, "%s", v ? v : MENU_EM_DASH);
}

static void build_row(const menu_t *m, int index, menu_row_t *r)
{
    memset(r, 0, sizeof *r);
    switch (m->list) {
    case LIST_ROOT:
        build_root_row(m, index, r);
        break;
    case LIST_SETTINGS:
        build_settings_row(m, index, r);
        break;
    case LIST_TRACKS:
        if (tracks_empty(m)) build_note(m, r);
        else build_track_row(m, library_track_at(m->lib, (uint32_t)index), r);
        break;
    case LIST_BROWSE:
        if (!lib_ok(m)) build_note(m, r);
        else build_browse_row(m, index, r);
        break;
    case LIST_STATION:
        build_station_row(m, index, r);
        break;
    case LIST_LIBRARY:
        build_library_row(m, index, r);
        break;
    case LIST_ABOUT:
        build_about_row(m, index, r);
        break;
    }
}

/* Rebuild what the view would draw, and bump the version only when it differs. */
static void refresh(menu_t *m)
{
    if (!m->open) return;
    clamp_view(m);
    int count = list_count(m);
    build_title(m);

    menu_row_t next[MENU_ROWS];
    memset(next, 0, sizeof next);
    for (int i = 0; i < MENU_ROWS; i++) {
        int index = m->window + i;
        if (index < count) build_row(m, index, &next[i]);
    }
    int screen = m->band - m->window;

    bool changed = m->shown_count != count || m->shown_band != screen ||
                   strcmp(m->title, m->shown_title) != 0 ||
                   memcmp(next, m->rows, sizeof next) != 0;
    if (!changed) return;

    m->version++;
    memcpy(m->rows, next, sizeof next);
    snprintf(m->shown_title, sizeof m->shown_title, "%s", m->title);
    m->shown_count = count;
    m->shown_band = screen;
}

/* ------------------------------------------------------------ browse paths */

static void resolve_browse(menu_t *m)
{
    if (!m->lib) {
        m->browse_node = LIB_NODE_NONE;
        return;
    }
    if (!m->browse_path[0]) {
        m->browse_node = library_root(m->lib);
        return;
    }
    lib_node_t node = library_find(m->lib, m->browse_path);
    if (node == LIB_NODE_NONE || library_kind(m->lib, node) != LIB_DIR) {
        /* The remembered directory is gone: pop to Browse's root. */
        m->browse_path[0] = 0;
        m->browse_node = library_root(m->lib);
        return;
    }
    m->browse_node = node;
}

static void path_pop(char *path)
{
    char *slash = strrchr(path, '/');
    if (slash) *slash = 0;
    else path[0] = 0;
}

static bool path_append(char *path, size_t cap, const char *name)
{
    size_t used = strlen(path);
    size_t add = strlen(name);
    size_t sep = used ? 1u : 0u;
    if (used + sep + add + 1 > cap) return false;
    if (sep) path[used++] = '/';
    memcpy(path + used, name, add + 1);
    return true;
}

/* --------------------------------------------------------------- lifecycle */

menu_t *menu_new(void)
{
    menu_t *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    m->list = LIST_ROOT;
    m->stage = 1;
    snprintf(m->title, sizeof m->title, "Menu");
    return m;
}

void menu_free(menu_t *m) { free(m); }

void menu_open(menu_t *m, const library_t *lib, const menu_facts_t *facts)
{
    if (!m) return;
    facts_copy(m, facts);
    bool was_open = m->open;
    m->lib = lib;
    m->lib_generation = library_generation(lib);
    if (!was_open) {
        m->open = true;
        m->stage = 1;
        m->acc_px = 0;
        m->slow_ms = 0;
    }
    resolve_browse(m);
    refresh(m);
}

void menu_close(menu_t *m)
{
    if (!m) return;
    m->open = false;
    m->lib = NULL; /* the lease ends with the Menu; the path and band survive as strings */
    m->lib_generation = 0;
    m->acc_px = 0;
    m->slow_ms = 0;
}

bool menu_is_open(const menu_t *m) { return m && m->open; }

static bool same_text(const char *a, const char *b)
{
    if (!a || !b) return a == b;
    return strcmp(a, b) == 0;
}

/* Whether a 25 Hz status/facts poll says anything new. The rows are rebuilt from these
 * inputs, so identical polls must not walk the library five times. */
static bool status_same(const menu_t *m, const mini_icecast_status_t *st)
{
    if (!st) {
        return m->status_path[0] == 0 && m->status_format == MI_FMT_NONE &&
               m->status_out_hz == 0 && m->status_bitrate == 0;
    }
    return m->status_format == st->format && m->status_out_hz == st->out_hz &&
           m->status_bitrate == st->bitrate && strcmp(m->status_path, st->path) == 0;
}

static bool facts_same(const menu_t *m, const menu_facts_t *f)
{
    if (!f) {
        return !m->facts.version && !m->facts.board && !m->facts.partition &&
               !m->facts.shuffle_on && !m->facts.rescanning && m->facts.backlight == 0;
    }
    return m->facts.shuffle_on == f->shuffle_on && m->facts.rescanning == f->rescanning &&
           m->facts.backlight == f->backlight && same_text(m->facts.version, f->version) &&
           same_text(m->facts.board, f->board) && same_text(m->facts.partition, f->partition);
}

void menu_update(menu_t *m, const library_t *lib, const menu_facts_t *facts,
                 const mini_icecast_status_t *status)
{
    if (!m) return;
    uint32_t generation = library_generation(lib);
    bool new_tree = lib != m->lib || generation != m->lib_generation;
    if (!new_tree && status_same(m, status) && facts_same(m, facts)) return;

    status_copy(m, status);
    facts_copy(m, facts);
    m->lib = lib;
    m->lib_generation = generation;
    if (new_tree) resolve_browse(m);
    refresh(m);
}

uint32_t menu_version(const menu_t *m) { return m ? m->version : 0; }

bool menu_rows(const menu_t *m, menu_row_t rows[MENU_ROWS])
{
    if (!m || !m->open || !rows) return false;
    memcpy(rows, m->rows, sizeof m->rows);
    return true;
}

const char *menu_title(const menu_t *m) { return m ? m->title : ""; }
int menu_band(const menu_t *m) { return m ? m->band - m->window : 0; }

int menu_count(const menu_t *m) { return m ? list_count(m) : 0; }

int menu_position(const menu_t *m)
{
    if (!m) return 0;
    int count = list_count(m);
    if (count <= 0) return 0;
    if (m->band < 0) return 1;
    if (m->band >= count) return count;
    return m->band + 1;
}

/* -------------------------------------------------------------- navigation */

static void enter_list(menu_t *m, menu_list_t list)
{
    m->list = list;
    m->band = 0;
    m->window = 0;
    refresh(m);
}

static void enter_browse_root(menu_t *m)
{
    m->browse_path[0] = 0;
    m->browse_node = m->lib ? library_root(m->lib) : LIB_NODE_NONE;
    enter_list(m, LIST_BROWSE);
}

static bool enter_browse_child(menu_t *m, lib_node_t node)
{
    uint16_t len = 0;
    const char *name = library_name(m->lib, node, &len);
    if (!name) return false;
    char raw[LIB_NAME_MAX + 1];
    snprintf(raw, sizeof raw, "%.*s", (int)len, name);
    char path[LIB_PATH_MAX];
    snprintf(path, sizeof path, "%s", m->browse_path);
    if (!path_append(path, sizeof path, raw)) return false;
    snprintf(m->browse_path, sizeof m->browse_path, "%s", path);
    m->browse_node = node;
    enter_list(m, LIST_BROWSE);
    return true;
}

static menu_action_t play_node(menu_t *m, lib_node_t node)
{
    if (!m->lib || node == LIB_NODE_NONE) return MENU_ACTION_NONE;
    if (library_kind(m->lib, node) != LIB_TRACK) return MENU_ACTION_NONE;
    if (library_relpath(m->lib, node, m->action_path, sizeof m->action_path) == (size_t)-1) {
        m->action_path[0] = 0;
        return MENU_ACTION_NONE;
    }
    return MENU_ACTION_PLAY;
}

static void step_once(menu_t *m, int dir)
{
    int count = list_count(m);
    if (count <= 0) return;
    if (dir > 0) {
        if (m->band >= count - 1) return;
        if (m->band == m->window + MENU_ROWS - 1) m->window++;
        m->band++;
    } else {
        if (m->band <= 0) return;
        if (m->band == m->window) m->window--;
        m->band--;
    }
}

void menu_step(menu_t *m, int rows)
{
    if (!m || !m->open || rows == 0) return;
    long long n = rows;
    int dir = n > 0 ? 1 : -1;
    if (n < 0) n = -n;
    for (long long i = 0; i < n; i++) step_once(m, dir);
    refresh(m);
}

static int stage_for(int dy_px, uint32_t dt_ms)
{
    uint32_t speed = 0;
    unsigned mag = dy_px < 0 ? (unsigned)(-(dy_px + 1)) + 1u : (unsigned)dy_px;
    if (dt_ms) speed = (uint32_t)((uint64_t)mag * 1000u / dt_ms);
    if (speed >= MENU_SPEED_STAGE4) return 4;
    if (speed >= MENU_SPEED_STAGE2) return 2;
    return 1;
}

void menu_drag(menu_t *m, int dy_px, uint32_t dt_ms)
{
    if (!m || !m->open) return;

    int target = stage_for(dy_px, dt_ms);
    if (target >= m->stage) {
        m->stage = target;
        m->slow_ms = 0;
    } else {
        m->slow_ms += dt_ms;
        if (m->slow_ms >= MENU_STAGE_HOLD_MS) {
            m->stage = target;
            m->slow_ms = 0;
        }
    }

    if (dy_px == 0) return;
    if (m->acc_px != 0 && (dy_px > 0) != (m->acc_px > 0)) m->acc_px = 0;
    m->acc_px += dy_px;

    int per_row = MENU_RATCHET_PX / m->stage;
    if (per_row < 1) per_row = 1;
    while (m->acc_px >= per_row) {
        step_once(m, 1);
        m->acc_px -= per_row;
    }
    while (m->acc_px <= -per_row) {
        step_once(m, -1);
        m->acc_px += per_row;
    }
    refresh(m);
}

menu_action_t menu_activate(menu_t *m)
{
    if (!m || !m->open) return MENU_ACTION_NONE;
    refresh(m); /* the facts may have moved since the last update */
    int count = list_count(m);
    if (count <= 0) return MENU_ACTION_NONE;
    int screen = m->band - m->window;
    if (screen < 0 || screen >= MENU_ROWS) return MENU_ACTION_NONE;
    const menu_row_t *row = &m->rows[screen];
    if (!row->enabled) return MENU_ACTION_NONE;

    switch (m->list) {
    case LIST_ROOT:
        switch (m->band) {
        case 0: enter_list(m, LIST_TRACKS); break;
        case 1: enter_browse_root(m); break;
        case 2: return MENU_ACTION_SHUFFLE;
        case 3: enter_list(m, LIST_SETTINGS); break;
        default: break;
        }
        break;
    case LIST_SETTINGS:
        switch (m->band) {
        case 0:
            m->action_backlight = next_backlight(m->facts.backlight);
            return MENU_ACTION_BACKLIGHT;
        case 1: enter_list(m, LIST_STATION); break;
        case 2: enter_list(m, LIST_LIBRARY); break;
        case 3:
            if (m->facts.rescanning) return MENU_ACTION_NONE;
            return MENU_ACTION_RESCAN;
        case 4: enter_list(m, LIST_ABOUT); break;
        default: break;
        }
        break;
    case LIST_TRACKS:
        return play_node(m, library_track_at(m->lib, (uint32_t)m->band));
    case LIST_BROWSE: {
        lib_node_t child = library_child(m->lib, browse_dir(m), (uint32_t)m->band);
        if (library_kind(m->lib, child) == LIB_DIR) {
            enter_browse_child(m, child);
            break;
        }
        return play_node(m, child);
    }
    default:
        break;
    }
    return MENU_ACTION_NONE;
}

const char *menu_action_path(const menu_t *m) { return m ? m->action_path : ""; }
int menu_action_backlight(const menu_t *m) { return m ? m->action_backlight : 0; }

bool menu_back(menu_t *m)
{
    if (!m || !m->open) return false;

    switch (m->list) {
    case LIST_ROOT:
        menu_close(m);
        return true;
    case LIST_SETTINGS:
        m->list = LIST_ROOT;
        m->band = 3; /* the Settings row */
        break;
    case LIST_TRACKS:
        m->list = LIST_ROOT;
        m->band = 0; /* the Tracks row */
        break;
    case LIST_STATION:
        m->list = LIST_SETTINGS;
        m->band = 1;
        break;
    case LIST_LIBRARY:
        m->list = LIST_SETTINGS;
        m->band = 2;
        break;
    case LIST_ABOUT:
        m->list = LIST_SETTINGS;
        m->band = 4;
        break;
    case LIST_BROWSE: {
        if (!m->browse_path[0]) {
            m->list = LIST_ROOT;
            m->band = 1; /* the Browse row */
            break;
        }
        /* The row that opened this list: the directory's own row in its parent. */
        lib_node_t node = m->lib ? library_find(m->lib, m->browse_path) : LIB_NODE_NONE;
        lib_node_t parent = LIB_NODE_NONE;
        int index = 0;
        if (node != LIB_NODE_NONE) {
            parent = library_parent(m->lib, node);
            if (parent != LIB_NODE_NONE) {
                uint32_t children = library_child_count(m->lib, parent);
                for (uint32_t i = 0; i < children; i++) {
                    if (library_child(m->lib, parent, i) == node) {
                        index = (int)i;
                        break;
                    }
                }
            }
        }
        path_pop(m->browse_path);
        lib_node_t parent_node = m->lib ? library_find(m->lib, m->browse_path) : LIB_NODE_NONE;
        if (m->lib && m->browse_path[0] &&
            (parent_node == LIB_NODE_NONE || library_kind(m->lib, parent_node) != LIB_DIR)) {
            /* The parent vanished under a remembered path too: pop to the root. */
            m->browse_path[0] = 0;
            parent_node = library_root(m->lib);
        }
        m->browse_node = parent_node;
        m->band = index;
        break;
    }
    }
    clamp_view(m);
    refresh(m);
    return false;
}

/* --------------------------------------------------------------- name text */

/* The shared display-name cleaner, with the extension dropped: a row shows a file name,
 * and the Peek's title shows a tag title. One implementation, in cassette.c, so the two
 * cannot drift apart. */
const char *menu_clean_name(const char *name, char *buf, size_t n)
{
    return cassette_clean_name(name, true, buf, n);
}
