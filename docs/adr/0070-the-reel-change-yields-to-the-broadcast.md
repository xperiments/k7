# The reel change yields to the broadcast, and its packs step

ADR 0011 gave the reel change a window whose progress is the new Track's `elapsed_ms`, on the
reasoning that a slow *frame* would drop drawn frames rather than stretch the window. It cannot
say anything about a slow *stream*. `elapsed_ms` is counted in encoded frames
(`mini_icecast.c`'s status), so when the broadcast starves the window's own clock starves with
it. Measured on the board at one Track boundary: the window's 7 s between the ink's dim and its
return took 13.9 s of wall, and the broadcast logged five starvation rebases over 11 s, producing
audio at 50.2 % of real time.

The arithmetic of that boundary is a collision, not a coincidence. The decoded-audio ring is per
Track (ADR 0041): it is created empty and is empty again when the outgoing Track ends, so the
boundary is the one moment the broadcast has no cushion. The screen, on the same core 0 that the
decoder needs 0.72 of, does its heaviest redraw in the same second: at 25 Hz it resized both
packs ~200 times over the window, and each resize invalidates and repaints about 42 % of the
800×480 panel out of PSRAM. The decoder kept roughly half the core, the ring never refilled, and
because the window was clocked by that same starved audio it drew for 13.9 s instead of 8 —
twice the work to recover from, which kept the decoder down for longer.

ADR 0011 already named the escape for the redraw ("a coarser diameter step … not a longer
window"). This ADR takes it, and bounds the window by a second clock so it cannot amplify itself.

## Decision

- **The window closes on whichever clock is later**: the new Track's `elapsed_ms` or the wall
  clock, both measured from the moment the window opens. In a healthy stream the two agree to
  within a poll and nothing changes; a starving stream can no longer stretch the window, and a
  stream that runs ahead can still close it early.
- **The packs move in `CASSETTE_REWIND_PACK_STEPS` (24) even steps**, not on every poll. The
  step is anchored on the window's two ends — the first frame is the fill already on screen and
  the last is the fill normal play shows — so neither end moves. The hubs keep the smooth
  interpolation; their slices are a few hundred pixels, not a pack.
- **The screen gives the core back while the broadcast is behind.** The screen task rests at
  `UI_LAG_REST_MS` (120 ms) instead of its floor (40 ms) while `mini_icecast_lag_ms()` exceeds
  `UI_LAG_YIELD_MS` (150 ms) — the same lock-free signal the artwork loader already yields on.
  A healthy stream never trips it: the pacer holds 1.5 s of lead.
- **The screen's frame cost is measured across the whole pass**, `t0` at the top and the
  difference at the bottom, so the rest controller prices a frame that the LVGL port preempted.
  It was measured between two adjacent calls, so it read 0 and the rest was always the floor.

Nothing changes in the decoder, the encoder or their priorities: ADR 0041 stands.

## Considered options

- **A longer window.** Makes the collision worse in proportion.
- **Rewind less often (a lower `CASSETTE_REWIND_REVS`).** The spin is not the cost; the pack
  resize is.
- **Keep the window on the audio clock and rely on the yield alone.** The yield stops the
  starvation but the window still stretches, so the screen still draws ~2× the frames — the
  amplifier stays wired.
- **Anchor the window to the wall clock alone.** ADR 0011 rejected this because it overruns into
  the song's ninth second. With the later-of-two rule it cannot: the audio clock closes the
  window whenever the audio is healthy, and the wall clock is only a ceiling.
- **Raise `ice_decode` above the LVGL port.** This was the plan's first thought and it was
  measured on paper: at priority 5 the decoder needs ~5.7 s of the core to fill a 2.23 s ring
  while the broadcast drains it at 1×, so the animation would freeze for the whole window. The
  screen must yield, but only when the audio is actually behind.
- **A pre-roll at the boundary** (hold the first frame until the ring holds N ms). It does not
  help by itself: the fill rate is what is short, so the pre-roll only moves the shortage.
- **Lower the station rate.** It frees the encoder's core, which was never the limit (65 % with
  a 1.54× ceiling).

## Consequences

- The window is 8 s of wall in the worst case and no more. The tape lands on the fill and phase
  normal play shows at the close, so both ends remain seamless — the reason the window exists.
- The pack migration is now a staircase of 24 moves. It reads as mechanical, which suits a
  rewind, and the hubs still turn every poll; `CASSETTE_REWIND_PACK_STEPS` and
  `CASSETTE_REWIND_REVS` are the two knobs if it does not.
- The screen's cadence is now proportional to what a frame costs, and drops to ~8 Hz while the
  broadcast is behind. That is the trade this ADR makes: the audio is the product, the
  animation yields to it.
- `host/tests/t10_cassette.c` pins the new rules: the window closes on the wall clock when the
  audio stalls, on the audio clock when the wall is behind, at most one pack move per step, and
  the closing frame is still the reference frame.
- The telemetry now carries `decoded N s = X core-s per audio-second` and `ring starved N ms`,
  so the next disagreement about this path is settled by a number. If the decoded cost turns out
  to be 0.85 or more per audio-second, the screen fix cannot be enough and the follow-up is a
  decode pre-roll or a ring-driven priority boost, which would need its own ADR against 0041.
- `host/tests/board_soak.sh` reads the rebase count from the telemetry (the log prints only
  rebases #1–#5 and every 25th, so counting lines capped at five) and reports how many Track
  boundaries the run crossed, because a soak that never crosses one has not tested this path.

## Supersedes

ADR 0011 in part:

- its decision bullet "**Driven by the new track's elapsed**, not the wall clock", replaced by
  the later-of-two rule above; and
- its consequence "The screen resizes both packs every update for 8 s", replaced by the
  24-step migration.
