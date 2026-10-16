# 06 — Encoder cost and memory are measured on the device

**What to build:** The device reports what encoding actually costs it, so the choice of station
format stops resting on a figure taken from someone else's unverified benchmark. Startup reports
the station format, the memory available in both memory classes, and which allocation path each
large buffer took; a running device reports encoder cost and free memory periodically.

This is the ticket that turns the specification's memory budget from arithmetic into observation,
and it is what makes the acceptance threshold for the recommended format decidable. The soak is
the evidence, not a formality.

**Blocked by:** 05 — The live stream is continuous MP3.

**Status:** done (host tier and device code). The one-hour soak is a board-only test and has not been run; no board was attached.

- [ ] Startup reports the chip model, flash size, external memory size and mode, and free memory in both the internal and external classes.
- [ ] Startup reports which allocation path each large buffer took — external memory or internal fallback — so a device without external memory is diagnosable rather than mysterious.
- [ ] The encoder's per-frame cost is measured on the device, averaged over at least a thousand frames, and logged.
- [ ] Free memory in both classes is logged periodically, at least once a minute.
- [ ] A one-hour soak with a client attached to the live stream shows encoder cost below the threshold stated in the specification, free memory stable across the hour, and no starvation rebase events logged.
- [ ] The same measurement is available as a host baseline, and is recorded as scaling data only — never as a pass or fail, because host processor speed says nothing about the device's.
- [ ] If the threshold is exceeded, the documented fallback is a **lower** rate and bit rate. Raising the bit rate is not an available response to a cost problem.

Spec: `docs/specs/sd-stream-server.md` §4.3 F4, §2.4 (P2, P3), §4.1, §9 T11, T11h.

## Comments

- [x] Startup reports the chip, flash size, PSRAM size and mode, and free heap in both classes (`log_hardware()` in `main/main.c`).
- [x] Startup reports which path each large allocation took: the encoder staging buffers (`... staging buffers in external memory`) and the burst ring (`burst ring: 12098 B (~500 ms of audio) in external memory`), each with the internal fallback named when it is used.
- [x] Encoder cost is measured on device with `esp_timer_get_time()` around `shine_encode_buffer_interleaved()`, accumulated over every frame, and logged once a minute with the frame count and the cost as a percentage of one core.
- [x] Free memory in both classes is logged every 60 s, together with the cumulative starvation-rebase count. Every rebase is also logged as it happens, so "zero starvation rebases" is checkable from the log rather than assumed.
- [x] The host baseline is `make -C host bench` (>= 1 M frames, monotonic clock). Measured here: 869 encoder calls over 1 000 000 input frames, ~138 us/frame inside the encoder, 0.53 % of one core *on this host* — recorded as scaling data only, never as a pass or fail.
- [x] The documented fallback under CPU pressure is a **lower** rate and bit rate (32 kHz / 96 kbps), never a higher one; that is stated in the README's station-format section.

Honest limitation: the encoder's own 96 KB of state is allocated by `calloc()` inside the vendored library, and ESP-IDF serves that from internal RAM. Redirecting it would mean modifying shine, which L1 forbids. Every allocation this project controls takes the PSRAM-preferred path, and the encoder's state size is reported at startup so the internal-RAM budget is visible rather than guessed.

**Post-review note: the burst ring is now sized in encoded bytes.** §2.4 budgets "~88 000 B" for the
ring at `burst_ms = 500`, which is the arithmetic for a *PCM* ring (44100 x 4 bytes/s x 0.5 s). After
ticket 05 the ring holds encoded MP3, so it is sized from the station's byte rate:
`bitrate * 1000 / 8 * burst_ms / 1000 + slack` = 12 098 B at 128 kbps. `burst_ms` still means exactly
what it meant - the duration of history a connecting client receives - and R3's "burst fan-out
unchanged" holds; what changed is that 88 KB of the budget is no longer needed. That is a deviation
from §2.4's table, and §12 already flags that budget as arithmetic rather than observation. The
startup log prints the real size and the path it took, which is what turns it into observation.
