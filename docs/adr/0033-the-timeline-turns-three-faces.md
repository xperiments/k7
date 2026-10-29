# The timeline turns three faces

**Amends [ADR 0032](./0032-the-footer-shows-the-timeline.md)**, which put the Peek's timeline in the
footer only while the Menu was up and printed the times for as long as it was there.

The owner: *"it must be present all the time in artline/white modes. Also toggle progress bar with
Artist then Song name (so 3 steps in loop) when on artline/white modes."*

Two things were wrong with the first cut. The bar was a Menu readout, so the pages that actually
carry the chrome - Art and Real - showed an empty band most of the time. And a broadcast's *position*
is the least interesting thing about it for more than a few seconds: the artist and the song are what
a listener glances at.

## Resolution

- **The bar is up whenever the chrome is**, on both pages, with or without the Menu. It is the
  Peek's bar in the Peek's place, so the two pages' bottom band and the Peek's overlay read as one
  instrument that happens to move.
- **It turns three faces every four seconds**: the times and the track, then the **artist**, then
  the **song**, then round again.
  - the artist is the Track's first directory (`cassette_path_group`),
  - the song is its title with the file's furniture taken off (`menu_clean_name`),
  - a face with no words is **skipped**, so the rotation never sits on an empty bar,
  - with nothing playing the bar stays on the times and prints `--:--` with no fill.
- **One label between the times.** Both word faces are the same centred `jura_28` label with the
  bar's 20 px inset, ellipsized, so switching faces is a `set_text` and two hidden flags - not a
  second row of objects to keep in step.
- **The view feeds it every frame**, from the same `mini_icecast_status_t` the rows follow: the
  Menu no longer owns the footer, and the footer no longer knows what a Menu is.

## Considered options

- **Rotate only while the Menu is up.** Rejected by the owner: the pages with the chrome are where
  the band is seen most.
- **A tag per face** (the Peek's own floating pills). Rejected: the bar is one instrument with one
  rect; floating pills belong to the Peek's gesture, not to a band.
- **Longer or shorter than four seconds.** Four is a compromise between reading a title and
  catching the position; it is `CYCLE_MS` in the footer.
- **Scrolling the text instead of swapping it.** Rejected: a marquee is a second animation to tune
  and it makes the bar move while the tape is the thing that should look alive.

## Consequences

- The band's timeline is now a small state machine of its own (three faces, a timer, a fallback when
  a face loses its words), and the Menu's footer code is gone with the strings it used to build.
- The `--:--` total still means "length unknown", and an empty fill means "no position yet"; both
  are visible only on the times face.
- The dump cannot photograph the rotation: it reads the framebuffer row by row over about ninety
  seconds, so a capture at full resolution is a mosaic of different faces. Pin `CYCLE_MS` to see one.
