# The tape's red edge turns to the tape colour while the VU fades out through the reel change

The reel change rewinds the tape on screen for the new song's first eight seconds
(ADR 0011). The screen kept drawing the VU through it, following the new audio from the
first frame, and the tape's solid red edge kept running around the migrating packs. The
product reads that as a meter playing while the tape winds back, which is the one motion
the window is meant to show; the rewind should look like a transport at rest. The tape's
own lines should not disappear, though: they are the tape.

## Decision

The view treats the tape's red edge and the VU differently through the reel change:

- **The tape's own lines** - the two strand lines and the solid edge arc on each pack -
  **cross-fade from `COL_TAPE_DARK` to `COL_TAPE`**. On the pack the edge merges with the
  tape; the strands remain visible as dark tape. They stay at full opacity.
- **The VU** - the arcs and the compact rows on the Label cover, lit and unlit dashes
  alike - **fades out to zero**, so no ghost of the meter is left behind.

Both use one envelope derived from the model's `rewind_permille`: full at the window's
start, a one-second ramp to the tape colour / zero, held for the body of the window, and
a one-second ramp back as the window closes. `REWIND_FADE_MS` is that 1 000 ms; the rest
of the window's length stays in `CASSETTE_REWIND_MS`.

The progress bar and the titles do not fade or recolour. They are chrome and
information, not tape, and the bar and titles already follow the new song from its
first frame. The pack fill (`COL_TAPE`) is untouched: the packs are the rewind's own
subject.

The model is unchanged. It still reports `rewinding` and `rewind_permille`, and the view
turns that into an opacity and a colour; the host tier's reel tests keep their meaning.

## Considered options

- **Fade the tape lines out to transparent with the VU.** The first cut: it hides the
  playing edge, but it also opens a moving gap where the strand should be, and the reels
  lose the tape path that makes the migration readable. The product wants the lines to
  become tape, not to vanish.
- **Turn the tape lines to `COL_TAPE` instantly at the window's start.** Cheap, but the
  screen would snap; the reel change is the project's one long transition and the edge
  changing in one frame is a second break in it.
- **Recolour only the strand and leave the pack edge red.** The edge is the part that
  reads as playback, so the pack would still look like it is running.
- **Dim the VU instead of turning it off.** A 30 % ghost of the meter would remain the
  dominant thing on the reels and would look broken rather than off.
- **Put the envelope in the portable model.** It would make the cross-fade host-testable,
  but the model's contract is tape position, not presentation; the view already owns
  every other alpha and colour on the screen, and the rewind's own frame is tested
  without it.
- **Fade the pack fill too.** The dark discs are what the eye follows during the rewind;
  removing them would leave the reels floating.

## Consequences

- `cassette_view.c` gains `REWIND_FADE_MS` and `rewind_tape_opa()`; `tape_edge_color()`
  mixes `COL_TAPE_DARK` and `COL_TAPE` with the envelope, `vu_opa()` scales the lit and
  unlit dash opacities, and `tape_opa_apply()` sets the strand colours and invalidates
  the packs and VU overlays. The pack draw callback uses the mixed colour at full
  opacity.
- The packs are invalidated whenever the envelope changes. The rewind already resizes
  both packs every update, so the cross-fade adds no new full-pack redraw of its own.
- The serial log prints `rewind: tape edge now COL_TAPE, VU hidden` when the envelope
  reaches zero and `rewind: tape edge back to COL_TAPE_DARK, VU back` when it returns, so
  the panel pass can see it without a screenshot.
- The VU data still follows the new audio from the first frame; only its drawing is
  suppressed. [ADR 0011](./0011-cassette-rewinds-on-a-track-change.md) is superseded in
  part (the VU and the tape edge are no longer part of the visible fiction).
- The board pass is owed: the cross-fade's feel and its cost on the panel need a track
  boundary, because the host tier does not compile the LVGL view.
