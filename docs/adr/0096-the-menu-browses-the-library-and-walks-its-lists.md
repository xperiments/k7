# The Menu browses the library and walks its lists

[ADR 0022](./0022-the-menu-is-a-controls-mode.md) settled the Menu's tree when the cassette still
carried the Playlist: Tracks (the broadcast order, one Row per Track), Browse (a directory of the
Library tree), Shuffle and Settings, with Backlight, Station, Library, Rescan and About inside
Settings. Three things have changed since. A Playlist is a file on the card
([ADR 0095](./0095-a-playlist-is-a-file-on-its-card.md)) and the broadcast walks one through
`playlist_select`; the App can choose it, but the panel could not, so a person with the player in
front of them and no phone had no way to pick a list. Tracks and Browse were two doors into the
same tree, and the flat one only repeated what Browse opens on. Station and Library were read-only
facts under a heading that otherwise changes things.

The owner asked for the four changes: Tracks gone, a Playlists destination that chooses the walk,
Shuffle last, and Station and Library out of Settings — their facts still reachable.

## Decision

- **The root is Browse, Playlists, Settings, Shuffle, in that order.** Browse stays the library's
  one door; Playlists is the card's; Settings still changes things; Shuffle is a mode and sits
  last, after the settings it is not one of.
- **Tracks is retired.** Browse's root is the same tree in the library's name order, and every
  Track it holds is reachable there; the separate flat list added a second view and a second place
  for the playing dot, not a second capability.
- **Playlists is a root destination, and the panel chooses the walk.** The list holds
  `Library order` first — the broadcast's own tree order — and then the card's lists in name
  order, each Row carrying its declared row count; the Row being walked carries the dot, and
  `Library order` carries it when no list is. A tap calls the same selection the App's
  `POST /api/playlists/select` calls ([front-door.md](../specs/front-door.md), U3), so the choice
  lands in `k7.select` and survives a reboot. A list with no rows is dimmed; a refusal leaves the
  walk as it was.
- **The panel does not author a list.** It does not create, rename or delete one: the App writes
  the files ([ADR 0094](./0094-the-device-writes-the-lists-it-is-given.md)), the panel has no
  keyboard for a name, and a delete would need a confirm the panel has nowhere to show. The
  panel's one act on a list is to walk it.
- **Settings changes things only: Backlight, Rescan, Wi-Fi, About.** Station and Library leave it:
  they showed facts, not settings, and a settings page that also answers "why is it silent" mixes
  two jobs.
- **About is the player's diagnostics page.** It grows from three value Rows to eleven: Firmware,
  Board, Partition (the player); Format, Rate, Bitrate (the broadcast); State, Tracks,
  Directories, Size, Truncated (the library). The groups wear the glyphs the Menu already has —
  `micro-chip`, `tablet-android`, `disc`; `radio-waves`; `books` — so the list reads in three
  blocks and no glyph is added. Eleven Rows do not fit the page, so About scrolls, as Browse
  already does.
- **The model reads an index; the view reads the card.** The portable model takes the card's
  Playlist index and the current id in its facts; `cassette_menu.c` reads `k7.playlists/` when the
  Menu opens and again on a new generation, and `app_services_card_root()` gives it the mounted
  root. The model stays free of the card, as ADR 0022 required.
- **The Pad, the band and the ink are unchanged.** The root and Settings fit one page; Browse,
  Playlists and About scroll. The ratchet, the window, the band's contrast and the one-frame open
  are ADR 0022's and stay.

## Considered options

- **Keep Tracks beside Browse.** Rejected: the same tree flattened, and the flat list only gave
  the playing Track a second Row; one way into the library is enough.
- **Put Playlists inside Settings.** Rejected: it changes what the broadcast plays, which is the
  root's job, not a setting's; a person reaches for it as often as for Browse.
- **Let the panel delete or rename a Playlist.** Rejected for this cut: a rename needs a text
  entry on a panel with no keyboard, and a delete needs a confirmation the Menu has no surface
  for; the App owns both (ADR 0094, ADR 0095).
- **Delete Station and Library outright.** Rejected: their facts answer "why is it silent" and
  "is the card being read" on a device that may be away from the App, and About is the page that
  already answers "what is this player".
- **A separate Diagnostics destination.** Rejected: one more root Row for facts that About can
  hold; the list scrolls, and the Pad already does that for Browse.
- **A new glyph for Playlists.** Rejected: `radio-waves` already means the broadcast's order and
  `books` the library; a list needs no new face, and a glyph costs flash.

## Consequences

- The panel can choose the broadcast's order for the first time; before this only the App could.
  Both surfaces call `playlist_select`, so they cannot disagree about what is walking.
- About is the third scrolling list, and the Pad's window and clamp apply to it unchanged. The
  five Row slots, the band and the repaint budget are untouched.
- The model gains a Playlist index in its facts and a walk action; the view gains the card read
  and the `app_services_card_root()` seam. The library lease and the Menu's lifecycle are
  otherwise unchanged.
- A card with more than 64 lists, a list with no rows and a list the Library cannot resolve are
  refusals the panel shows as an inert or unchanged Row; the App still reports the missing rows
  in full.
- The README's Menu paragraph and icon line describe the old tree until the view lands; they
  change with it.
- The board pass is still owed for the Pad's feel and the acceleration thresholds; this ADR
  changes the tree, not the gesture.

## Supersedes

[ADR 0022](./0022-the-menu-is-a-controls-mode.md)'s tree — "four rows at the root and five in
Settings" — and its icon sentence that gives `radio-waves` to Tracks and Station and `books` to
the Browse and Library Rows. The four root Rows are now Browse, Playlists, Settings and Shuffle;
the Settings Rows are four, and the broadcast's and library's facts wear their icons inside About.
Everything else in ADR 0022 stands: the Menu is a mode of the Controls, the Pad is its one
activator, the band is a window, and the model is portable.
