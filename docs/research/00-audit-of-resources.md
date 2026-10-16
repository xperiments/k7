# Audit: is `resources/` a minimal source for an ESP32-S3 radio stream **player**?

Scope: the 10 files in `resources/` (892 lines of C/H total), read in full. This is the
hand-written reconnaissance for the xtape radio-player library research; the three
topic files beside it (`radio-player-streaming.md`, `radio-player-codecs.md`,
`radio-player-audio-out.md`) cover what a player needs from primary sources.

## Verdict

**No — `resources/` is not a player at all. It is a mini Icecast *server*.**

It reads audio files off an SD card, decodes them, and *broadcasts* them as a
never-ending WAV/ICY HTTP stream to remote clients. The data flows
`SD card → decode → resample → TCP clients`. A radio stream player needs the
opposite direction: `TCP client → demux/ICY → decode → resample → I2S/DAC`.

The two share a decoder, a resampler, and a ring buffer — a genuinely useful
~40% of the code — but every part that touches the network, the clock, and the
audio sink is inverted or absent.

## File-by-file

| File | Lines | What it is | Role in a player lib |
|---|---|---|---|
| `mini_icecast.h` | 37 | Public API of the server: config struct, `start/skip/now_playing` | **Discard.** No API overlap. Only a naming precedent. |
| `mini_icecast.c` | 419 | HTTP server, ICY *emission*, pacing, burst ring, client fan-out | **Discard the server; mine 3 patterns.** |
| `mi_decode.h` | 14 | `mi_dec_open/hz/read/close` over mp3/flac/wav | **Replace the impl, keep the shape.** |
| `mi_decode.c` | 122 | dr_libs wrappers, file-based | **Rewrite for streaming.** |
| `mi_resample.h` | 12 | `mi_rs_new/process/free`, callback-emit | **Keep — best asset in the tree.** |
| `mi_resample.c` | 112 | Windowed-sinc/Blackman streaming resampler, 32.32 phase | **Keep.** |
| `host_convert.c` | 23 | gcc host test for decode+resample | **Keep the idea** — the library needs an off-device test harness. |
| `main.c` | 153 | Wi-Fi STA + SD mount + demo app | **Discard** (that's server UX), but it is the template for an `examples/` app. |
| `CMakeLists.txt` | 6 | `idf_component_register(...)` | **Fix — does not build.** |
| `sdkconfig.defaults` | 15 | Target/freertos/lwip/FATFS/PSRAM knobs | **Partly reusable** (see below). |

## What is genuinely reusable

**1. `mi_resample.c` — a real streaming sample-rate converter.** 112 lines,
no ESP-IDF dependency, no static state, `calloc`/`malloc` only, emits through a
callback, and bypasses when `in_hz == out_hz`. For a player this is not a
nice-to-have: a live stream arrives at an *unknown* rate (44.1 k, 48 k, 32 k,
22.05 k, plus HE-AAC's SBR-doubled rates) while the I2S channel generally must be
opened at a *known* one. That conversion has to live somewhere, and this
already does it correctly, including anti-aliasing on downsample
(`fc = 0.95 * min(1, out/in)`, so a 48 k → 44.1 k stream is filtered rather than
aliased). Its off-device testability (`host_convert.c`, plain `gcc -lm`) is the
reason to trust it and the pattern to keep. This was **measured off-device**, not
assumed — see the appendix: the converter is numerically sound, rate-exact with no
accumulating drift, and cheap enough on this class of CPU that its filter can be
made steeper if the aliasing figures below are judged too weak. What it does *not*
do is track a real DAC's clock, so `rubberband`-style drift correction remains a
gap the player layer must own.

**2. The chunked-pipeline idea.** `mini_icecast.c` decodes → resamples → accumulates
into fixed 1024-frame chunks → emits. Because the *output* format never changes,
track changes are seamless. A player has the same problem profile: the network
delivers arbitrarily-sized reads, the codec wants frames, the DAC wants a steady
cadence. The fixed-size-chunk + callback-emit structure transfers directly.

**3. The burst ring buffer.** `ring_put`/`ring_peek`/`ring_burst`
(`mini_icecast.c:111-159`) is a variable-length-record ring (2-byte length prefix)
allocated with `heap_caps_malloc(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` and
falling back to internal `malloc`. A player needs the *same* structure for
prebuffering a live stream before playback starts, to absorb Wi-Fi hiccups, so
the record framing and the PSRAM-with-fallback allocation idiom are worth lifting.
Note it never uses `realloc`-free logic and is safe under the `S.lock` mutex.

**4. Pacing.** `pace()` (`mini_icecast.c:291-302`) maps PCM frames played to wall
clock and delays the producer, with an explicit rebase path when it starves. A
player needs this *inverted* (the DAC's DMA completion rate is the clock, the
network is the throttled producer), but the starvation-rebase reasoning and the
`CONFIG_FREERTOS_HZ=1000` requirement for 1 ms `vTaskDelay` granularity both carry
over.

**5. ICY knowledge, as a spec reference.** The header emission in
`handle_new` (`mini_icecast.c:221-228`) and `send_meta` (`mini_icecast.c:75-90`)
implement ICY *as a producer*: `Icy-MetaData: 1` request handling, `icy-metaint`
advertisement, and the `StreamTitle='...';` body with the length byte counted in
16-byte blocks. A player must parse exactly this from the other side, so the file
is a working reference for the wire format — and usefully documents one gotcha
already: single quotes are sanitised to spaces (`mini_icecast.c:284`), which
tells you real titles contain quotes and a parser must not assume they don't.

**6. `main.c`'s Wi-Fi bring-up.** STA init, the event-group "wait for IP" pattern,
and `esp_wifi_set_ps(WIFI_PS_NONE)` (`main.c:69`) with the comment that modem
sleep adds latency spikes. That last line is a real, hard-won player-relevant
insight and should be the library's documented default.

**7. `sdkconfig.defaults` — selectively.** The lwIP socket-buffer raises
(`TCP_SND_BUF_DEFAULT=32768`, `TCP_WND_DEFAULT=16384`) are documented in-file as
being for PCM *egress* to clients; a player wants a large **receive** buffer
instead, which is a different knob. `CONFIG_FREERTOS_HZ=1000` is right for both.
The `SPIRAM` block is commented out and is where a player's stream buffer should
live.

## What is missing for a player

Nothing about the player exists yet:

- **HTTP/ICY client.** No `esp_http_client`, no `esp-tls`, no mbedtls, only
  `lwip/sockets.h` used server-side. No ICY header *parsing*, no metaint
  demultiplexing from the body, no `StreamTitle` extraction, no redirect/`.pls`/`.m3u`
  handling, no reconnect logic.
- **Streaming decoder API.** `mi_decode.c` is fundamentally file-shaped: `fopen`,
  `fseek`/`ftell` for every dr_lib seek callback, an extension-based format check
  (`strrchr(path, '.')`), and a per-file `d->hz` read once at open. A live stream
  has no path, no seek, no end, and — critically — **no container to announce its
  sample rate**. `mi_dec_read` returns interleaved stereo at the *file's* rate;
  a player needs a decoder that accepts pushed bytes and tolerates resync.
- **Streaming MP3/AAC.** Only `dr_mp3`/`dr_flac`/`dr_wav` are referenced, and only
  MP3 is relevant to radio. **No AAC at all** — and AAC (including HE-AAC) is the
  dominant non-MP3 radio codec, which is the single largest functional hole.
- **The I2S/DAC sink.** Zero output code. `driver/gpio.h` appears only for the
  BOOT-button skip; there is no `driver/i2s_std.h`, no codec control, no DMA.
- **The network→DAC clock problem.** The server *is* the clock
  (`pace()` sets the pace and clients obey). In a player the DAC is the clock and
  the network is uncontrolled, so buffer occupancy must be actively managed.
  Nothing in `resources/` addresses this.
- **Concurrency structure.** Both tasks are pinned to core 1; the server needs one
  producer and a mutex. A player needs a genuine producer/consumer pipeline
  (network → ring → decode → ring → I2S) with watermark signalling.
- **HTTP side of the public API.** `mini_icecast_start()` takes `music_dir`; a
  player's `start()` takes a URL and needs `play/pause/stop/volume/status` plus a
  now-playing callback, i.e. a different interface entirely.

## Build blockers (independent of the player question)

As committed, `resources/` does not compile:

1. **Missing dependencies.** `mi_decode.c:12-20` includes `dr_mp3.h`, `dr_flac.h`,
   `dr_wav.h`. None exist anywhere in the tree, and no `third_party/` or
   `include/` directory exists.
2. **`CMakeLists.txt` points at directories that aren't there** —
   `INCLUDE_DIRS "include"` and `PRIV_INCLUDE_DIRS "third_party"` — while the
   headers actually sit flat in the same directory as the sources.
3. **No license/attribution for the vendored decoders.** dr_libs is public-domain-ish
   dual-licensed (MIT-0 / Unlicense); whatever replaces it in a *library others
   will vendor* needs an explicit license decision and a `LICENSES/` note. This is
   a distribution blocker, not a build blocker, and it is the constraint most
   likely to dictate the codec choice.
4. **No repo hygiene.** Not a git repository, no README, no license file, no
   versioning, and no `idf_component.yml`. For "a lib for use in other projects"
   all four are required, since an ESP-IDF component is consumed either by
   `EXTRA_COMPONENT_DIRS` or by the component registry via `idf_component.yml`.
5. **`sdkconfig.defaults` is app-level, not component-level.** A component cannot
   ship `sdkconfig.defaults` and expect it to apply; the requirements
   (`FREERTOS_HZ`, lwIP receive buffers, PSRAM) must be documented for the
   consumer and, where possible, asserted at compile time via `#error`.

## The structural conclusion for the library

The reusable core is a **codec + resampler + ring buffer + chunker** — roughly
`mi_resample.c` verbatim, the *shape* of `mi_decode.c`, and three patterns from
`mini_icecast.c`. Everything that makes it a radio player (ICY client, streaming
AAC/MP3 decode, I2S sink, clock/buffer management, task pipeline) must be built.
So `resources/` is best treated as a **starting point for the DSP middle of the
library**, not as a player source. The three research files that follow determine
what the missing parts should be, and `01-library-architecture.md` merges all of
it into a concrete component design.

---

## Appendix: measured behaviour of `mi_resample.c`

Since the resampler is the one component recommended for survival, it was measured
rather than eyeballed. All figures come from three throwaway host probes compiled
against the unmodified `mi_resample.c` with plain `gcc -O2 ... -lm` (same toolchain
independence that `host_convert.c` relies on). These are **x86 host** numbers; the
CPU-cost column is the only one that does not transfer directly to the S3.

### 1. Frequency response (RMS gain in dB, steady-state, measured on the last 580 of 600 blocks)

`48000 → 44100` (new Nyquist 22.05 kHz; `fc = 0.95 × 44100/48000 ≈ 20.9 kHz`):

| Input tone | 1 k | 5 k | 11 k | 15 k | 17 k | 19 k | 20 k | 21 k | 22 k | 22.5 k | 23 k | 23.5 k | 24 k |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Gain | 0.00 | −0.00 | −0.00 | −0.01 | −0.30 | −1.87 | −3.59 | −6.18 | −9.85 | −12.15 | −14.74 | −17.28 | −inf |

`44100 → 48000` (upsample): flat to 0.00 dB through 13 kHz, −0.09 dB at 16 k,
−1.92 dB at 19 k, −6.96 dB at 22 k.

**Reading:** the low-pass is a well-formed Blackman-windowed sinc with unity DC gain
(the `sum` normalisation at `mi_resample.c:52` works — measured 1 kHz gain is exactly
0.00 dB at all three rate pairs, and the bypass path is bit-exact). The transition
band is shallow, which is expected from `ZERO_X = 8` (≈16 taps) and is stated in its
own comment. The consequence is real but minor for radio: content in the 22.05–24 kHz
band arrives at roughly −10 to −17 dB *folded back* into 0–2 kHz, i.e. a possible
faint alias rather than a broken stream. Internet radio is encoded with content almost
never above 16 kHz, so this is a tail case — but it is the exact knob to adjust
(`ZERO_X`, `mi_resample.c:9`) if the measurement is ever judged unacceptable.

### 2. Rate accuracy over time — the finding that matters most for a player

Output frames counted over long runs against the exact rational expectation:

| Rate pair | Input frames | Output frames | Exact | Error |
|---|---|---|---|---|
| 48000 → 44100 | 4 096 000 | 3 763 191 | 3 763 200.0 | −9.0 |
| 44100 → 48000 | 4 096 000 | 4 458 222 | 4 458 231.3 | −9.3 |
| 32000 → 44100 | 4 096 000 | 5 644 788 | 5 644 800.0 | −12.0 |
| 22050 → 48000 | 4 096 000 | 8 916 443 | 8 916 462.6 | −19.6 |
| 96000 → 44100 | 4 096 000 | 1 881 592 | 1 881 600.0 | −8.0 |
| 44100 → 44100 | 4 096 000 | 4 096 000 | 4 096 000.0 | 0.0 (exact) |

The error is **not cumulative**. Sampled at four checkpoints across a single
`48000 → 44100` run of **1 024 000 000 input frames** (~6 hours of audio, 940.8 M
output frames):

```
after      1 024 in: out     932  error  -8.80 frames
after     10 240 in: out   9 399  error  -9.00 frames
after    100 352 in: out  92 190  error  -8.40 frames
after  1 000 448 in: out 919 153  error  -8.60 frames
total: 1 024 000 000 in -> 940 799 991 out, exact 940 800 000.0, error -9.00 frames
```

**Reading:** the residual is a fixed **−9 frames (≈0.20 ms)** start-up transient from
the `T/2 − 1` zero-padding used to prime the history buffer
(`mi_resample.c:55-57`) — it appears immediately and is still −9.00 frames after a
billion input frames. The 32.32 phase accumulator (`step = (in << 32) / out`) is
therefore **rate-exact with zero long-term drift**, which is precisely the property a
player that runs for days requires. It also means the `uint64_t pos` does not lose
precision: at 10⁹ frames `pos` ≈ 4.4 × 10¹⁸, still safely inside 64 bits, because
`pos` is kept relative to a sliding buffer and re-based by the `drop` logic at
`mi_resample.c:96-101` rather than counting absolute time.

*Caveat for the library:* this proves the *algorithm* does not drift. It says nothing
about the network-vs-DAC clock mismatch, which is a hardware-crystal problem the
player layer must handle separately (buffer occupancy feedback).

### 3. CPU cost

Wall/CPU time to process 6 144 000 input frames at 1024 frames per call:

| Rate pair | CPU seconds | Audio seconds processed | Cost |
|---|---|---|---|
| 48000 → 44100 | 0.08 | 128 | ≈0.06 % of one host core |
| 44100 → 48000 | 0.07 | 139 | ≈0.05 % of one host core |
| 48000 → 48000 (bypass) | 0.00 | 128 | nil |

**Reading:** ~0.06 % of one core on this host. Since no Cortex/Xtensa part was
available here, the honest ESP32-S3 estimate requires a scaling assumption and is
therefore labelled an **inference, not a measurement**: a 240 MHz Xtensa LX7 is
roughly two orders of magnitude slower than the host core used here, which puts the
resampler somewhere in the low single digits as a percentage of one S3 core. That is
consistent with the code's own premise (it precomputes a 128-phase table so the inner
loop is `T` float MACs per output frame, ≈16–32 taps). The practical conclusion holds
either way: **the resampler is not the CPU bottleneck in a radio player** — MP3/AAC
decoding is — so spending more taps on a steeper filter is affordable if wanted. This
should be re-measured on real hardware before the library promises a CPU budget.

Reproduce with: compile `mi_resample.c` into a host binary that feeds a generated
sine or DC buffer and counts emitted frames (the three probes are transient; keep
them as `test/` targets in the library so the resampler stays covered off-device).
