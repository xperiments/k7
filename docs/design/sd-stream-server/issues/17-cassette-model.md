# 17 — Give the cassette screen a model it can be tested through

**What to build:** The cassette's arithmetic — a 20-frame reel advance with a clamped dt
accumulator, pack fill from the track position, the parenthesised-title trim, the 1 Hz
progress throttle, the "only rewrite a label when it changed" rule — is only reachable by
mutating LVGL objects. `main/` is not compiled by the host tier at all, so the newest and
most-changed code in the repository has no test seam.

Add a `cassette` component with no LVGL include and one verb:

```c
const cassette_frame_t *cassette_step(cassette_t *c,
                                      const mini_icecast_status_t *st,
                                      uint32_t now_ms);
```

`st == NULL` advances time only (the reel timer's call). The frame carries the cleaned
title, `title_changed`, `permille`, `permille_changed`, `pack_fill[2]` (0..1000),
`hub_frame[2]`, `playing`, `level_l`, `level_r`. The reel loop length, frame count and bar
period are constructor inputs, so a test can use 4 frames instead of 20. Layout — pixel
diameters, positions, fonts — stays in the adapter.

`ui_cassette.c` becomes that adapter: create the objects, apply a frame, keep the
redraw-only-when-changed rule. The commented-out label variant, the unused statics
(`s_file`, `s_facts`, `s_time`, `shown_*`), `fmt_time()` and the unused geometry macros go.

**Blocked by:** 16 — the frame carries the level, which 16 puts in the status.

**Status:** done (host tier and both firmware builds; board tier not run - no board attached)

- [ ] The reel advances one frame per `loop_ms / frame_count`, wraps, and clamps a long dt.
- [ ] The reels do not move when nothing is playing.
- [ ] `permille` follows elapsed/duration, clamps at 1000, and steps at most once per bar
      period.
- [ ] `(live, 1984, Le Spectrum) Song` cleans to `Song`; trailing space is trimmed.
- [ ] A title is reported as changed only when the cleaned text differs.
- [ ] The adapter contains no reel/pack/title arithmetic.
- [ ] `main/CMakeLists.txt` requires the component; `host/Makefile` compiles it.
- [ ] Host unit tests and both firmware builds pass.

**Files:** `components/portable/cassette/include/cassette.h` (new),
`components/portable/cassette/cassette.c` (new), `components/portable/cassette/CMakeLists.txt` (new),
`main/ui_cassette.c`, `main/ui.c`, `main/CMakeLists.txt`,
`host/tests/t10_cassette.c` (new), `host/Makefile`.

**Tests:** t10 feeds synthetic statuses on a time series and asserts reel frames, wrap,
dt clamp, pack fill, bar throttle, title cleaning and change flags — no panel, no serial
dump.

**Source:** architecture review, candidate 2.

## Comments

- Added `components/portable/cassette/` with one verb, `cassette_step(status, now_ms)`, and a frame
  carrying the cleaned title and its change flag, permille and its change flag, pack fill,
  hub frames, playing and the level. No LVGL include.
- `main/ui_cassette.c` is the adapter: objects, layout, diameters, applying a frame. The reel
  arithmetic, pack fill, bar throttle and title cleaning are gone from it, as are the
  commented-out label variant, the unused statics, `fmt_time()` and the unused macros.
- `t10_cassette` pins the reel rate, wrap, the 250 ms dt clamp, stopping/resuming, the 1 Hz bar
  throttle, pack fill, cleaning and the change flags.
- **Bug found by the new test:** the old `title_clean()` trimmed only trailing whitespace, so
  "(live) Song" displayed as " Song". The model trims both ends.
- Also updated the stale `ui_fit.c` references in ADR 0004 and the README.
- Verified: unit, layering and integration suites; both firmware builds (the Sunton build
  compiles the adapter against the LVGL port).
