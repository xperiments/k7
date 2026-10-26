# The Menu is a Controls mode, and its pad is one plate that ratchets

The controls already close a frame around the cassette, and the owner wanted a menu reachable
from the M cell without losing that frame. The decision is a **Menu mode of the Controls**,
opened by M, that turns the cassette area into a five-row list and the DJ block into a
**Pad** - one touch plate drawn as the block's six cells - that ratchets the list a row at a
time and activates the band on a tap.

## Decision

- **A mode of the Controls, not a Level or a screen.** ADR 0019 removed the Level machine and
  Bare is the only screen; the Menu adds no Level back. It exists on both **pages** - the white
  one the down swipe asks for and the dark one either horizontal swipe keeps - and turns with
  the page's ink. It opens on a tap on the M cell, only while the Controls are out and the
  134/256 pose has landed, and not while a knob is in edit mode.
- **Modal, and closed by the up swipe.** While it is up the only live inputs are the Pad and the
  Transport drawer's three buttons; the rows take no touches. An up swipe closes it, one rung
  above the up that closes the Controls; down and both horizontal swipes do nothing, and the
  two-finger VU gesture is ignored because the meter is behind the Menu. P closes it at any
  depth, and `<` closes it from the root (below).
- **Cut in one frame, with the cassette hidden.** The Menu, the pad's labels, the header title
  and the footer appear together in the frame the M tap lands, as the page flip and the knob
  editor already cut. The cassette container - tape, hubs, meter, glow and the **Cover** - is
  hidden for the Menu's lifetime, so the 40 ms reel tick has nothing to invalidate and nothing
  redraws under the text. The Cover's wish is never touched: hiding the container is not a way
  off the page, so closing the Menu gives the Cover back exactly as it was.
- **The frame is the existing window.** 478x284 at (110,76) - 38 px either side of the tape - with a 2 px border and 8 px corner,
  whose top and bottom edges are the bands' lines; its inner 474x280 holds five 56 px rows.
  No new rectangle is drawn; the window's own fill is recoloured to the page's while the Menu is
  up, because outside it the window is `k_col_bg` on both pages (the cassette stands on it).
- **The bands carry the location and the now playing.** The header band (768x71 at (16,6))
  carries the list's title and keeps the K7 mark; the footer band (768x115 at (16,359)) carries
  the now playing - song then group - and the band's position in the current list on the right.
- **The Pad is one plate.** It fills the DJ block's footprint - six 96x96 cells at a 94 px
  pitch, (594,76) to (784,360) - and is drawn as six carcases with a padded vertical rule
  between the two columns, but the cells never press. A vertical drag ratchets the list: 56 px
  of travel per row at rest, each step instant, and the list rests only on row boundaries. A
  fast drag multiplies the rows per 56 px in the stages the accelerated-scrolling patent
  describes (1x/2x/4x, with a speed threshold and a duration hysteresis); the constants are
  tuned on the board. A tap that never became a drag activates the **Band**.
- **A moving band, five fixed slots.** The band starts on the first visible row (or the row the
  list remembers) and moves one row per step in the drag's direction; when it reaches the last
  visible row the next step shifts the window one row and the band stays on that edge row. The
  list is not an LVGL scroll container and has no spacers: five row slots are rebound to the
  window's indices on each step, so the virtual list falls out of the structure rather than out
  of recycling. Whole-row steps mean nothing is ever drawn between rows, so there is no snap to
  implement, and `find_snap_point_y()` / `init_scroll_limits()` - which cannot see a recycled
  row - are not involved.
- **The rows are inert.** Only the Pad activates the band; a row is a drawing of an icon, a
  label and a chevron or a value. A Track row shows the same cleaned name the peek falls back
  to: no extension, apostrophes as spaces, parenthetical asides dropped, ellipsized to the row.
- **The drawer becomes Back / Enter / Close.** `<` goes up one List and closes the Menu at the
  root; `>` is the pad's tap; P closes the Menu from any depth. Their Jura letters swap to the
  icon font - circled left, circled right, cassette-tape - and transport prev/skip is suspended
  while the Menu is up.
- **The tree, four rows at the root and five in Settings.** Tracks (the Playlist), Browse (a
  directory of the Library tree, in the library's own name order with directories and Tracks
  mixed), Shuffle (Off/On) and Settings; Backlight (Auto/100/60/30), Station, Library, Rescan
  and About. Both one-page lists fit their page; only Tracks and Browse scroll. Station,
  Library and About are lists of label/value rows. Rescan starts on one tap and then reads
  "Rescanning..." and ignores taps; the Menu keeps its list and, when the new generation lands,
  re-resolves the path and pops to Browse's root if it has vanished. Backlight cycles in place
  and pins the panel with a hold until reboot, Auto being today's ladder. With no card or no
  library the Menu still opens: Tracks and Browse are dimmed and inert, their lists hold one
  non-selectable line, and the lists that explain and fix it stay reachable.
- **Shuffle is a mode, not an action.** The row reads Off/On and toggles the shuffle mode ADR
  0023 records.
- **The band contrasts the page, like the M/R caps.** `k_col_bg` with `k_col_button_white` on the
  white page, `k_col_primary` with `k_col_bg` on the dark one; unselected rows take the page's
  ink. The playing Track's row carries a dot after its label in the row's own ink, and activating
  it restarts the Track.
- **The list and band are remembered**, until the app restarts: closing the Menu does not reset
  where you were, and reopening re-resolves the remembered path against the current generation.
- **Where the logic lives.** A new portable `menu` component holds the navigation, the window,
  the band, the ratchet and the row models, reads the Library through the lease it is given,
  and returns actions (play this path, rescan, next backlight, toggle shuffle, close).
  `screen_display/cassette_menu.c` is the LVGL view and executes them. The model is host-tested
  in `t18_menu`, as `cassette` and `cassette_layout` are.
- **The icons.** `xtape.ttf` gains five glyphs - folder, gear, sun, info, SD - beside the nine,
  generated by a `font:xtape` `lv_font_conv` script at two sizes - 24 px for the Rows, 36 px for
  the drawer's 64 px circles - and bpp 4. Tracks and Track files
  wear `radio-waves`, the Browse row wears `folder`, the directories inside Browse wear
  `cassette-tape` (the owner's choice), Shuffle wears `shuffle`, Settings `gear`, Backlight
  `sun`, Station `radio-waves`, Library `SD`, Rescan `endless-loop`, About `info`. The
  submenu chevron stays Jura's `>`.

## Considered options

- **A full-screen Level**, which the menu research note recommended. It re-introduces a screen
  the view no longer has, rebuilds the header, footer and back affordance, and loses the frame
  the owner wants the Menu to live inside.
- **Direct-touch rows**: tap a row to activate it, drag the list to scroll. The most familiar
  gesture, but it gives the list two selection models (a band and a finger), needs a hit area
  and press state per row, and cannot express "a touch area where scrolling occurs and a click
  targets the current item", which is what the owner asked for.
- **Native LVGL scroll + snap.** Its snap scan only sees the children that exist, and the snap
  area is computed once at scroll start, so a windowed list has no snap point for the unseen
  row; the settle would also repaint the page for 200-400 ms. The ratchet never shows a partial
  row, so there is nothing to snap.
- **Six real buttons** with fixed jobs (up/select/down, back/root/context). Six meanings to
  learn, six icons, and the same moves available twice once the drag also works.
- **A fixed centre band**, iPod-style. With five rows it wastes two rows at each end of the
  list, and every step repaints the page instead of two rows.
- **A "Now Playing" root row.** It duplicates the Menu's own exit (P, `<` from the root, the up
  swipe); the footer already says what is playing.
- **Forcing the white page while the Menu is open.** One look, but it re-writes the page on an
  action that never asked for a page, and breaks the page-ink rule every other part follows.
- **One-shot Shuffle**, as the first tree had it. The owner wants a mode; ADR 0023 records it.
- **Text-only rows until the icon set is redrawn.** The icon font exists for the drawer's
  Back/Enter/Close anyway; the rows get their five new glyphs rather than changing look twice.
- **Rescan at the root.** The owner put it in Settings, where maintenance belongs, and kept the
  root at four rows.

## Consequences

- A portable `menu` component with `t18_menu`; `screen_display/cassette_menu.c`; the drawer
  gains a menu mode with its three actions and the icon labels; `cassette_eq` gains the M hook
  and the state the Pad covers; the header and footer gain their Menu texts; `font_xtape_24.c`
  and `font_xtape_36.c` with their `package.json` script; five new glyphs in
  `assets/fonts/xtape.ttf`.
- Two capabilities leave the screen: the **Shuffle mode** in `mini_icecast` (ADR 0023) and a
  **backlight hold** so the idle ladder does not overwrite a manual pick. R stays a placeholder;
  P means Close only while the Menu is up; M opens it.
- Five row slots and one pad keep the object count independent of the library size. A step that
  moves only the band repaints two rows - 2 x 482 x 56 x 2 = 107,968 B painted, 215,936 B with
  the direct-mode sync copy - and a window shift repaints the page, 482 x 280 x 2 = 269,920 B
  painted; neither repaints the panel.
- Memory: no scroll container, no spacers and no per-Track objects; the row text lives in five
  fixed buffers owned by the model, and the view copies a row into LVGL when it redraws (the dots
  long mode needs a mutable label, so static text cannot be used); the icon font is a few KB.
- The board pass is owed: the ratchet feel and the acceleration thresholds, the tap/drag
  threshold, the band's contrast and the icon legibility, and the ignored M tap while the pose
  is still animating.
- `docs/specs/CONTEXT.md` gains **Controls**, **Page**, **Menu**, **Pad**, **Band**, **Menu
  list** and **Row**, and **Shuffle mode** in the broadcast's half. The tree, the geometry and
  the interaction rules are in [`docs/specs/menu.md`](../specs/menu.md).
- ADR 0016's two-finger "reserved for the menu" note stays superseded: ADR 0020 gave that
  gesture to the VU meter, and the Menu is the M cell's.
