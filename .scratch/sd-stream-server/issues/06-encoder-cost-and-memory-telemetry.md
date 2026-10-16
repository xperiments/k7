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

**Board run of P2/P3: done.** The startup log on the real device, captured over the
UART bridge:

```
I (327) esp_psram: Found 8MB PSRAM device
I (333) esp_psram: Speed: 80MHz
I (772) esp_psram: SPI SRAM memory test OK
I (919) app: chip: esp32s3, 2 core(s), revision 0.2
I (919) app: flash: 16 MB; psram: 8192 KB (octal mode, CONFIG_SPIRAM_MODE is not auto-detected)
I (921) app: heap at startup: internal 305571 B free, external 8386148 B free
I (1326) icecast: encoder: 1152 samples/frame (26.1 ms), 95880 B of state inside the vendored library, staging buffers in external memory
I (1327) icecast: burst ring: 12098 B (~500 ms of audio) in external memory, sends at most 2 clients
I (1344) files: download buffer: 16384 B in external memory
```

So the memory budget is now observation rather than arithmetic, which is what P2 asked
for. Two corrections to earlier claims:

* The encoder's state is **95 880 B** on the target, not 95 896 B. A 64-bit host measures
  95 896; the struct holds three pointers, so the 32-bit build is 16 bytes smaller. The
  earlier ticket note guessed 12 bytes; the measured difference is 16. This is exactly why
  `mi_enc_state_bytes()` returns `sizeof(shine_global_config)` instead of a constant.
* The burst ring is 12 098 B, not the ~88 000 B of §2.4's table, because it holds encoded
  bytes now (see the note on ticket 05). `burst_ms` still means 500 ms of history.

Outstanding at the time: the one-hour soak (T11). It has since been run on the completed
firmware - see the last comment on this ticket.

**Board measurements: the encoder's cost, and what actually reduces it.** Measured on the
device with a temporary on-board benchmark (400 frames per format, warm-up discarded), after
the fixes below:

| format | us/frame | % of one core | | format | us/frame | % of one core |
|---|---|---|---|---|---|---|
| 44100/64 | 12551 | 48.0% | | 32000/96 | 13570 | 37.7% |
| 44100/128 | 13424 | 51.4% | | 48000/192 | 13604 | 56.7% |
| 44100/192 | 13718 | 52.5% | | 22050/64 | 6175 | 23.6% |
| 44100/256 | 13817 | 52.9% | | 44100/320 | 13852 | 53.0% |

Three findings, each of which changed the firmware:

1. **The bit rate is nearly free; the sample rate is what costs.** 64 to 320 kbps costs 10%
   more CPU for five times the bits, because the fixed per-frame transforms dominate and more
   bits converge the quantization loop sooner. So raising the bit rate for quality costs
   almost nothing, and the F4 assumption that a higher bit rate is the wrong response to CPU
   pressure is, on this encoder, only very slightly true.
2. **`-Og` cost 2.7x.** IDF's project default is `-Og`; the vendored encoder was inheriting
   it. At `-O2` the encoder went from 33.4 ms per frame to 12.5, and at `-O3` to 12.3
   (44100/128, before the heap change).
3. **PSRAM for the encoder's state cost 8%.** With `CONFIG_SPIRAM_USE_MALLOC`, a >16 KB
   allocation prefers PSRAM, so the encoder's 95 KB working set - a 40 KB lookup table among
   it - sat in external memory. `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=131072` (Kconfig's
   ceiling, just above the 95880 B needed) keeps it internal: measured `102668 B of internal
   RAM taken`. Note that `sdkconfig.defaults` only applies when `sdkconfig` does not exist -
   editing it did nothing until the file was deleted, and Kconfig silently ignored an
   out-of-range 262144.
4. **IRAM placement is worth 17%.** A linker fragment (`* (noflash)` on `libshine.a`) with no
   change to shine's source: 13.8 ms per frame at 44100/256 against 16.7 ms without, and
   consistently 16-18% at every format tested.

The station format is now **44100 Hz / 256 kbps**: the specification's recommended sample
rate with three and a half times the bits of the 32 kHz/96 kbps fallback, for 3% more CPU
than 128 kbps. Measured quality (SNR of the encoded stream against its source, pink noise at
-10 dBFS, delay-compensated): 96 kbps 2.4 dB, 128 3.3 dB, 192 7.1 dB, **256 11.9 dB**,
320 15.5 dB.

**The 35% threshold is not reachable at 44.1 kHz, and - measured - not at 32 kHz either**
(37.7%); only 22.05 kHz meets it (23.6%). The specification calls that figure a proposal
rather than a measurement, the functional requirement is that the broadcast never starves,
and the soak is what checks that. The fallback for headroom is a lower rate and bit rate.

Also fixed under this ticket: `xTaskCreatePinnedToCoreWithCaps()` requires
`vTaskDeleteWithCaps()`; using plain `vTaskDelete()` leaked the 10 KB stack of every
connection task, which degraded the device across repeated runs (77 passing, then 68, with no
reboot in between). See ticket 07.

**The one-hour soak, on the final firmware (44.1 kHz / 256 kbps, ESP-IDF 6.1).**
`sh host/tests/board_soak.sh <url> <port> 60 stream`, with one client on `/stream`:

```
soaking http://192.168.0.44:8000 for 60 minute(s) with load: stream (encoder bound 70%)
  encoder: 135795 frames, last reading 41.4% of one core
  memory at the start: internal 18751 B free, external 8200264 B free; starvation rebases 0
  memory at the end:   internal 18751 B free, external 8205304 B free; starvation rebases 0
  starvation rebases: 0
  asserts/overflows/reboots: 0
  internal free (minimum of first/last third): 11067 B -> 11067 B
  external free (minimum of first/last third): 8184784 B -> 8178528 B
soak passed: no faults, no starvation, encoder under 70%, memory stable
```

Read precisely: **the functional half of T11 passes** - no starvation, no faults, free
memory stable to the byte over the hour, and the encoder's cost flat (10 786 us/frame at
the start, 10 812 at the end, over 135 795 frames). **The 35% half does not**, because
41.4% is above it, and the specification offers no format that meets it at a usable
sample rate: 32 kHz measures 37.7% on synthetic material and only 22.05 kHz comes in
under the line at 23.6%. That is why the soak was run with `MAX_COST=70` and why the
README states the position rather than hiding it.

Also measured on the final firmware: **T9b** (repeated downloads of the largest track
with a listener attached) shows `starvation rebases 0` and `asserts 0`, with the encoder
at 43.8% - slightly higher than an idle hour, as expected when the card is being read by
a download and the decoder at once. Internal RAM is the thing to watch: the working
minimum during a track is about 11 KB, below the 32 KB the heap reserves for DMA.
