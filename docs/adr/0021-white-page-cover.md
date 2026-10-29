# The white page's down swipe prints the Track's picture on the cassette

**Amended by [ADR 0025](./0025-four-rungs-real-art-initial-peek.md):** the cover is not a rung
of its own any more. It is printed with the white Page as one level - Real - so a down from Art
flips the paper and fades the picture in as a single step, and an up undoes both.

**Further amended by [ADR 0039](./0039-the-cassette-is-a-card.md):** the glow is retired and the
stack is no longer label/glow/cinta. On the white page the cassette itself is a tinted **Card**,
the picture is printed on it, and the pill window is cut by one A8 mask that the Card and the
print share. The mask is no longer `frame[0] - frame[1]`, and the frames are no longer raised
over the print.

The white controls page is the down swipe's, and the down swipe had nothing left to do
once the page was white: the drawer and the DJ block were already out. The owner wants the
playing Track's own picture there, on the cassette's body, revealed by that same swipe:
"escalemos la imagen al ancho de `s_frame[0]` y la alineemos arriba", masked to
`s_frame[0] - s_frame[1]`.

## Decision

- **The cover belongs to the white page: down turns it on, up turns it off.** The first
  down swipe opens the controls and flips the page white, as before. While that page is up,
  the next down swipe shows the cover; the up swipe hides it and leaves the controls, and
  the next up closes the controls. Every way off the page - either horizontal swipe, or the
  up that closes it - hides the cover with the page. A horizontal swipe that opens the
  controls on the dark page does not ask for the white page, so it does not ask for the
  cover either.
- **The mask is `frame[0] - frame[1]`, cut by an A8 bitmap.** `cassette_cover.c` builds one
  clip box at frame 0's current pixels with the frame's radius plus 2 px and `clip_corner`,
  and an A8 mask on that box: 255 over the print, 0 in the label window's pill. The hole
  takes the largest radius the rectangle allows - half its shorter side, a pill whose ends
  are the hubs' circles - and while the label is up the window's border follows the same
  pill (`cassette_tapes_set_cover_print()`). Because the window is really transparent, the
  glow and the cinta below show through; the image is not drawn there at all. The mask is
  built at the label's only pose (402x188), so there is no per-step mask and no opaque hole
  except the fallback when the mask cannot be allocated. The inner frame's border is drawn
  over the hole and the outer frame's over the label, so both frames stay crisp.
- **Label on top, glow in the middle, cinta at the bottom.** The tape body and the meter are
  created first; then the white page's edge mark; then the label. The stack, bottom to top,
  is cinta < glow < label. The label covers the body inside the outer frame; the tape exposed
  below the frame stays visible, under the glow. The two frames are lifted over the label
  while it is up (`cassette_tapes_raise_frames()`) and put back at the bottom when it goes,
  so the borders stay over the print without moving the cinta.
- **The glow is a cassette child, not a screen child.** The edge mark is created after the
  meter and before the label, so it is the middle layer by construction. It lights the cinta
  (including the tape exposed below the frame) and is covered by the label inside the outer
  frame; the label's window lets it show through. The peek and the chrome remain screen
  children above the cassette. No z-order call is made on show or hide.
- **Fade in and out, cut on a buffer change.** The down fades the label in and the up fades
  it out over 1 s (`lv_anim_path_ease_in_out`), on the container's `opa_layered`, so the
  A8 mask and the picture fade as one. A page flip and a Track change cut it instead: the
  loader reuses the one 464 KB buffer (a fade would draw it while it is written) and the
  pose leaves the mask's size.
- **Width is the frame's, top-aligned.** The image is scaled to frame 0's width and its
  height follows from its aspect; the clip box cuts whatever runs past the frame's bottom.
  It is not squeezed to the frame's height. The decoded buffer is the old Artwork
  screen's 603x385 fit, so the controls pose scales it down, and the buffer size does not
  depend on the pose. The label is only shown at the controls pose: the A8 mask is built at
  that pose (402x188), and the down swipe waits for the pose to land instead of showing a
  label at a size the mask does not cover.
- **The parked loader comes back, without the Case.** `artwork_loader.c` returns to the
  panel board's build: the `artwork` locator sources, TJpgDec and the one 603x385 RGB565
  cover buffer (464,310 B). It also absorbs the peek's tags again, so `tags_loader.c` is
  gone and one task requests both for a Track. The Case backdrop (`assets/labels/cover.jpg`,
  768,000 B decoded) and the Label cover blob stay parked: the cassette's frames are the
  frame now. A new Track requests the picture eagerly, hides the previous one while the
  buffer is written, and the loader re-checks the request sequence under the LVGL lock
  before handing the buffer over, so a stale picture cannot land against a new Track.

## Considered options

- **An opaque hole instead of a real one.** The earlier cuts put a `k_col_bg` box over
  the picture. That works while the label is under the tape, but with the label on top it
  would hide the glow and the cinta in the window. The A8 mask costs 75,952 B at the
  controls pose (404-byte stride, 188 rows), is built only while the label is up, and is the
  only way to get a real window without four clipped pieces and their concave corners.
- **Leave the picture out of the cassette and show it as a plain rectangle.** It would miss
  the point: the frame is what makes it a cassette, and the window is what keeps the reels
  visible.
- **Draw the cover under the body line art.** An earlier cut did this and kept every tape
  over the print. The owner's final stack is label/glow/cinta, so the label has to cover the
  body inside the frame and the tape only stays visible below it.
- **Decode at the frame's width (402 px at the controls pose).** The buffer would fit the
  only place the cover is shown, but it would tie the decoder to one pose and the buffer
  to one more constant. The 603x385 fit is already tested by `t15_artwork`, and the draw
  scales it down at the controls pose.
- **Request the picture on the first show only.** Saves a card read and a decode on Tracks
  whose cover is never opened, but the first reveal would wait for the decode. The loader
  already backs off while the broadcast is behind and runs below the screen and the
  network, so the eager request is the better trade.
- **Keep `tags_loader.c` and add a second loader for the picture.** Two tasks would open
  the same Track and the request slot and the tags-first ordering would be duplicated. The
  old loader already had both halves; restoring it is one task.

## Consequences

- The panel board's build gains `artwork.c`, `id3.c` and `flac.c`, the TJpgDec include and
  `cassette_cover.c`; `tags_loader.c` is removed. The headless board builds none of the
  cover path, and `cassette_tapes_frame_rect()` returns zeroes there.
- The cover buffer is allocated at screen start and the loader logs the PSRAM margin
  before and after it; the boot log now shows about 464 KB less free than the tag-only
  build. The A8 label mask is 75,952 B, allocated on the first show and kept; the fallback
  hole costs nothing extra.
- `cassette_tapes` exposes its two frames' current pixels and radii so the label masks
  against the one source of geometry rather than a copy, and
  `cassette_tapes_set_cover_print()` swaps the window's border to the pill while the label is
  up. `cassette_tapes_raise_frames()` / `_lower_frames()` are the one z-order pair: the frames
  above the label while it is shown, back at the bottom when it goes. `cassette_view` owns the
  wish (`cassette_cover_set_shown()`), the page owns the hide, and `cassette_levels` owns the
  per-Track request.
- The vertical ladder is now three steps in each direction: down shows the controls on
  white, down prints the label; up clears the label, up closes the controls. The down that
  shows the label waits for the controls pose (134/256), because the A8 mask is built for
  that size; a down during the shrink does nothing and the next one shows it.
- The label fades over 1 s on the down and the up, and cuts on a page flip or a Track
  change. The fade repaints the label's own layer (mask and picture), not the whole panel;
  the frames stay raised over it until the fade out completes, then go back under the body.
- A Track with no picture, or a decode that fails, shows no label; the white page and the
  controls are unchanged. A PNG `APIC` is still located and refused, as before.
- `cassette_view.h` gains `cassette_view_set_cover()`, called by the loader under the LVGL
  lock; `artwork_loader.h` is no longer parked and describes both the picture and the
  tags.
