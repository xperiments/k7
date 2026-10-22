# The reel change rewinds the tape over the new song's first eight seconds

ADR 0005 made the reels follow the tape geometry, and that model has one moment it cannot
express: the track boundary. The broadcast's `elapsed_ms` goes back to zero, so in a single
40 ms step the packs flipped from end-of-side to start-of-side (400 px/168 px the other way),
both hub phases reset to zero (up to 35 of 36 art frames of jump), and the progress bar
dropped to empty. The label had already changed, because `set_track()` names the next file
before it opens. The screen's one moment of theatre — a side ends, the next one begins — was
one frame with three discontinuities in it.

## Decision

For `CASSETTE_REWIND_MS` (8 000 ms) after the screen first sees a new track, the frame is
interpolated between two states instead of taken from the position: the state that was on
screen at that moment, and the state normal play shows when the window closes. Both pack
fills move — one displayed position drives them, as before — and both hub phases move, so
the tape runs back onto the supply reel while the hubs turn in reverse. At the end of the
window the reel is exactly where play was going to put it, with no jump at either end.

- **Driven by the new track's elapsed**, not the wall clock: progress is
  `(elapsed - elapsed_at_detection) / 8000`. A slow frame drops drawn frames but cannot
  stretch the window, which is what makes 8 s a promise. The window opens where the screen
  first sees the new track (normally within one poll of its start), so its first frame is
  the frame that was already drawn; it is half-open, and the step that reaches 8 000 ms is
  already normal play.
- **The spin is a perceptual rate.** 36 frames per revolution at 25 Hz can show about one
  revolution per 1.44 s; a real rewind of a side is hundreds of revolutions in this window.
  The spin is 11 revolutions, rounded per reel to a whole number plus the fraction that
  keeps the frame already on screen, which works out to ~2 art frames per 40 ms update: fast enough
  to read as a rewind, slow enough to trace, where a shorter window needs a 4-frame
  strobe to turn as fast. `CASSETTE_REWIND_REVS` is the knob.
- **The whole transport eases out** over the last `CASSETTE_REWIND_EASE_MS` (1 200 ms): a
  constant rate for 6.8 s, then a rate falling to zero, normalised to reach exactly 1 at the
  window's end. The tape and the hubs wind down together.
- **The bar, the label and the VU are not part of the fiction.** The bar snaps to the new
  position (the packs are the metaphor; the bar is information), the label names the new
  song immediately, and the VU follows the new audio from the first frame.
- **A change inside a window restarts it** from what is currently drawn, not from the side it
  replaces, so tapping next twice has no jump either. The first status after boot is not a
  change: the empty screen already shows the start of a side, so there is nothing to rewind
  from.

## Considered options

- **Keep the snap.** The status quo: the visible break this adr exists to remove.
- **Hold the outgoing packs, then jump at the end of the window.** Moves the break in time.
- **One art frame per update (~1.44 s per revolution).** Reads at normal play speed, so only
  the reversed direction says "rewind"; the pack migration has to carry the whole effect.
- **Uncapped / physically honest `v/r` reverse spin.** Correct and unwatchable: at 25 Hz it is
  noise, and no art resolution available on this part changes that.
- **An `lv_anim` in `cassette_view.c` from the old sizes to the new ones.** Free easing, but
  the model's frame and the screen would disagree for 8 s, the hub art would have to be
  suppressed as well, and none of it is observable from the host tests. ADR 0005 put the
  arithmetic in `components/portable/cassette` for exactly this reason.
- **Rewind the progress bar with the tape.** Either the bar lies for 8 s or it stays 8 s
  behind the audio for the whole song.
- **Anchor to the wall clock at detection.** Always a full 8 s of screen time, but it overruns
  into the song's ninth second and adds wall-clock state to a model that otherwise derives
  everything from the position.

## Consequences

- For 8 s after a boundary, `cassette_step`'s frame is no longer a pure function of the tape
  position; after the window it is again. ADR 0005's "a track change starts the phase at
  zero" is superseded in part: the phase still resets, but what is drawn is the window.
- `cassette_frame_t` gains `rewinding` and `rewind_permille`; the constants live in
  `cassette.h` with the geometry. The adapter is unchanged: it already applies whatever
  pack fill and hub frame the model returns.
- The screen resizes both packs every update for 8 s, and each resize redraws the pack, its
  strand and its VU overlay. That is the measured cost to watch on the panel; a slow frame
  cannot change the window's length, only how coarsely it is drawn. If it is too heavy, the
  answer is a coarser diameter step or a lower `CASSETTE_REWIND_REVS`, not a longer window.
- The test in `host/tests/t10_cassette.c` pins the rules a panel would only show as "looks
  wrong": the first frame equals the one already on screen (fills exactly, hubs within one
  art frame), the tape only moves one way, the hubs change frame almost every step, the
  deceleration is in the tail, and the closing frame equals a reference model that only ever
  saw the new track.
- A track shorter than 8 s never finishes a rewind, so its packs stay part-way. That is
  honest — nothing finished rewinding — and it is why the window is not queued.
- Superseded in part by
  [ADR 0015](./0015-tape-edge-and-vu-fade-through-the-reel-change.md): the VU is hidden
  for the window and the tape's red edge turns to the tape colour; both return as it
  closes.
