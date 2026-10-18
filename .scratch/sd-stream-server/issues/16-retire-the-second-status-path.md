# 16 — Retire the second status path

**What to build:** The broadcast's interface carries two competing answers to "what is
playing": `mini_icecast_status()` (which the cassette reads) and a per-chunk PCM callback
(which only a console line reads). The callback's two implementations — in `main/main.c`
and `host/icecast_host.c` — are byte-identical RMS loops, and the display the callback was
added for does not use it.

Compute the level where the PCM already passes through, put it in the **status** snapshot
as two 0..1 RMS fields, and delete the callback, both copies and the two volatile floats.
The console line and the cassette then read one interface.

**Blocked by:** none

**Status:** done (host tier and both firmware builds; board tier not run - no board attached)

- [ ] `mini_icecast_status_t` carries `level_l` and `level_r`, 0..1 RMS, updated per chunk.
- [ ] The values are atomic reads for the status caller (same discipline as `track_frames`).
- [ ] `mini_icecast_pcm_cb_t`, `pcm_cb`, `pcm_cb_ctx` are gone from the public header.
- [ ] `main/main.c` and `host/icecast_host.c` each lose their `on_pcm` copy.
- [ ] The console line still reports L/R percentages.
- [ ] `t06_status` asserts the level is in range and non-zero while a fixture is playing.

**Files:** `components/mini_icecast/include/mini_icecast.h`,
`components/mini_icecast/mini_icecast.c`, `main/main.c`, `host/icecast_host.c`,
`host/tests/t06_status.c`.

**Interface change:** this removes a documented callback. If a future FFT wants the raw
PCM, it gets a new interface then; the status field is the display's interface now.

**Tests:** t06 extended; the integration suite's console/status checks unchanged.

**Source:** architecture review, candidate 7.

## Comments

- `mini_icecast_status_t` carries `level_l`/`level_r` (0..1 RMS), computed in `emit_chunk()`
  where the PCM already is, and read lock-free like `elapsed_ms`.
- The callback, both `on_pcm()` copies and both volatile float pairs are gone. The device and
  host console lines now read the same snapshot the cassette does.
- `t06_status` pins the range and that the level becomes non-zero while a fixture plays.
- **Docs:** the spec's §4.4 C1 is marked superseded (the callback it described no longer
  exists), and the README's station-format example no longer sets `.pcm_cb`.
- Verified: unit, layering and integration suites; both firmware builds.
