#pragma once
/*
 * menu - the Menu's model: the tree, the navigation, the visible window, the band and the
 * pad's ratchet. No LVGL, no card, no board: the view hands it a Library lease and the
 * broadcast's status and draws what it says.
 *
 * The Menu is a mode of the Controls (docs/specs/menu.md, ADR 0022). Its model owns:
 *   - which Menu list is on top: the root, Settings, Tracks, a Browse directory, or one of
 *     the read-only lists (Station, Library, About);
 *   - the band: the row a tap on the pad activates, absolute in the list;
 *   - the window: the five rows the page shows, derived from the band;
 *   - the pad's ratchet: one row per 54 px of travel, in 1x/2x/4x stages;
 *   - the row models: label, icon, value and state for the five visible rows;
 *   - the actions the view has to execute: play a Track, rescan, next backlight, toggle
 *     the shuffle mode, close.
 *
 * The view must hold a Library lease for as long as the Menu is open; the pointers the
 * model hands back (labels, the play path) are copied into the model's own buffers and do
 * not outlive the lease.
 *
 * Times are milliseconds as the caller measures them; `menu_drag()` takes the delta since
 * the previous call, which is what makes the ratchet host-testable.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "library.h"
#include "mini_icecast.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Five rows share the page's 6 px inset (docs/specs/menu.md). */
#define MENU_ROWS 5
#define MENU_LABEL_MAX 96
#define MENU_VALUE_MAX 32

typedef enum {
    MENU_ICON_NONE = 0,
    MENU_ICON_TRACKS,    /* the broadcast order destination */
    MENU_ICON_BROWSE,    /* the Library-tree destination */
    MENU_ICON_DIRECTORY, /* a directory row inside Browse */
    MENU_ICON_TRACK,     /* a Track row */
    MENU_ICON_SHUFFLE,
    MENU_ICON_SETTINGS,
    MENU_ICON_BACKLIGHT,
    MENU_ICON_STATION,
    MENU_ICON_LIBRARY,
    MENU_ICON_RESCAN,
    MENU_ICON_ABOUT,
    MENU_ICON_FIRMWARE,  /* About's three value rows */
    MENU_ICON_BOARD,
    MENU_ICON_PARTITION,
} menu_icon_t;

typedef enum {
    MENU_ROW_ACTION = 0, /* acts in place: Shuffle, Rescan, Backlight */
    MENU_ROW_SUBMENU,    /* opens another Menu list */
    MENU_ROW_TRACK,      /* plays; the path is the action's */
    MENU_ROW_DIR,        /* opens Browse on that directory */
    MENU_ROW_VALUE,      /* a read-only label/value row: activating does nothing */
    MENU_ROW_NOTE,       /* the empty-state line: activating does nothing */
} menu_row_kind_t;

/* One visible row. The label and value are the model's copies; the view may keep the
 * pointers for as long as the Menu is open and the row is not rebuilt. */
typedef struct {
    menu_row_kind_t kind;
    menu_icon_t     icon;
    bool            enabled;  /* false: dimmed and inert (no library, rescan running) */
    bool            playing;  /* the Track the broadcast is on */
    bool            chevron;  /* opens another list; the value is then empty */
    char            label[MENU_LABEL_MAX];
    char            value[MENU_VALUE_MAX];
} menu_row_t;

typedef enum {
    MENU_ACTION_NONE = 0,
    MENU_ACTION_PLAY,      /* menu_action_path() is the Track's relative path */
    MENU_ACTION_RESCAN,
    MENU_ACTION_BACKLIGHT, /* menu_action_backlight() is the new percent (0 = Auto) */
    MENU_ACTION_SHUFFLE,   /* toggle the mode (ADR 0023) */
    MENU_ACTION_CLOSE,     /* the view should close the Menu */
} menu_action_t;

/* What the view knows and the model cannot: the board's facts and the settings' current
 * values. `version`, `board` and `partition` are copied; NULL shows as an em dash. */
typedef struct {
    bool        shuffle_on;
    bool        rescanning;
    int         backlight; /* 0 = Auto, else 30, 60 or 100 */
    const char *version;
    const char *board;
    const char *partition;
} menu_facts_t;

typedef struct menu menu_t;

/* Allocate the model, closed at the root. NULL on no memory. */
menu_t *menu_new(void);
void    menu_free(menu_t *m);

/* Open on the remembered list and band (the root on the first open). `lib` may be NULL
 * (no card). The model stores the pointer; the caller keeps the lease while it is open. */
void menu_open(menu_t *m, const library_t *lib, const menu_facts_t *facts);
void menu_close(menu_t *m);
bool menu_is_open(const menu_t *m);

/* Feed the model while it is open: the lease (which may change with a generation), the
 * facts, and the broadcast's status. A new generation re-resolves the Browse path and
 * pops to Browse's root if the directory has vanished. Cheap enough for the 25 Hz status
 * poll: it only rebuilds when something that concerns the Menu changed. */
void menu_update(menu_t *m, const library_t *lib, const menu_facts_t *facts,
                 const mini_icecast_status_t *status);

/* Bumped whenever the rows, the title or the band changed. The view redraws when it
 * differs from the last value it drew. */
uint32_t menu_version(const menu_t *m);

/* Fill the five visible rows, top to bottom. False when the Menu is closed. */
bool menu_rows(const menu_t *m, menu_row_t rows[MENU_ROWS]);

/* The header's location: "Menu", "Settings", "Tracks", "Station", "Library", "About" or
 * "Browse" with the path from the root. Never NULL. */
const char *menu_title(const menu_t *m);

/* The band's screen row (0..MENU_ROWS-1). */
int menu_band(const menu_t *m);

/* Rows in the current list, and the band's 1-based position in it (0 when empty). */
int menu_count(const menu_t *m);
int menu_position(const menu_t *m);

/* The pad: `dy_px` since the previous call, positive downwards, and the milliseconds
 * since then. One row per 54 px at rest; faster drags step 2x/4x with a hysteresis.
 * `menu_step()` moves whole rows (the host tests and a future keypad use it). */
void menu_drag(menu_t *m, int dy_px, uint32_t dt_ms);
void menu_step(menu_t *m, int rows);

/* Activate the band: push a list, or return the action the view must execute. A disabled
 * row (no library, rescan running) returns MENU_ACTION_NONE. */
menu_action_t menu_activate(menu_t *m);
const char *menu_action_path(const menu_t *m);
int         menu_action_backlight(const menu_t *m);

/* Go up one list. True when that closed the Menu (the band was on the root). The band is
 * set to the row that opened the list it returns to. */
bool menu_back(menu_t *m);

/* The row label's cleaning, also used for the footer's fallback: the name without its
 * extension, apostrophes as spaces, parenthetical asides dropped, trimmed and truncated
 * into `buf`. Always NUL-terminates; returns buf. */
const char *menu_clean_name(const char *name, char *buf, size_t n);

#ifdef __cplusplus
}
#endif
