# The Menu's footer shows the timeline, not the song

**Amended by the owner's tuning:** the bar's rest y is 416, not 424, in `cassette_layout` - one
number, so the Peek's strip moved with the footer's.

**Amended by [ADR 0037](./0037-the-footers-frame-is-page-ink.md):** the bar's border is page ink.
It is the Peek's primary on the dark page and `k_col_glass` on the white one; the times, the title
and the track keep the playing ink on both.

**Amended by [ADR 0033](./0033-the-timeline-turns-three-faces.md):** the bar is up whenever the
chrome is, not only in the Menu, and it turns three faces - the times, the artist, the song - every
four seconds.

The owner: *"menu related footer content must go or present that data in the same label with
times/progress bar we show in the peek page."*

While the Menu was up, the footer carried three strings: the Track's title on one line, its group
underneath, and the list's position (`3 / 1204`) on the right. All three were readings of things the
Menu already shows - its rows name the tracks and the selected row is the cursor - and none of them
was the one number a listener actually wants off the screen: how far into the Track the broadcast is.

## Resolution

- **The footer's Menu content is the Peek's timeline, not a likeness of it.** The same 568x52 bar,
  at the same (116,424), with its corner, its paper fill and its primary border; the same 20 px
  insets, 110 px time boxes, 10 px gap and 6 px / 288 px track; the same `m:ss` / `--:--` and the
  same two inks; and it slides from (116,488) the way the Peek's bar slides. It is drawn as a
  screen child rather than inside the band because 2 px of it hang below the band's bottom edge,
  and a parent clips its children.
- **Those numbers live in `cassette_layout`**, with the rest of the screen's fixed geometry, and
  both the Peek and the footer read them. `t16_layout` pins them. A copy that is allowed to hold
  its own copy of the numbers is a copy that drifts.
- **No words at all.** The title and the group are gone from the band; the Menu's rows are where a
  Track is named. The list's position is gone too: the Menu's own cursor is its position readout.
- **The band is empty when nothing plays.** The row is hidden rather than showing a placeholder,
  because an empty band is honest about a silent broadcast.
- **The fill's fraction is computed in the band** from `elapsed_ms / duration_ms`, clamped, so the
  caller does not have to agree with the footer about rounding; a `duration_ms` of 0 prints
  `--:--` and leaves the fill empty.
- `cassette_footer_set_playing()` and `cassette_menu_set_tags()` are gone with the strings; the
  Menu feeds `cassette_footer_set_timeline()` from the same `mini_icecast_status_t` its rows
  already follow.

## Considered options

- **Keep the song and add the timeline under it.** Rejected: the band is 72 px tall - two 28 px
  lines are what it holds - and the owner's first option was to drop the content altogether.
- **Move the Menu's list position into the timeline's row.** Rejected: the position is about the
  Menu's list, not about the Track, and mixing the two in one row would read as one quantity.
- **Reuse the Peek's objects.** Rejected: the Peek is a floating overlay with its own slide, its
  tethers and its tags; the band is a fixed row. They share the idiom and the inks, not the
  objects, which is the same line the two timelines already draw.

## Consequences

- The footer says one thing, and it is the thing the Peek says: a broadcast has a position.
- `cassette_view_set_tags()` no longer feeds the Menu - only the Peek's two tags - so the Menu
  reads no strings from outside its own model.
- The Menu's footer now needs the status every frame rather than on a text change; the call is a
  formatting pass over two numbers and a width only when the fill moves.
