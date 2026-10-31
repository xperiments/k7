# Adding a local I2S DAC to the broadcast appliance

## Verdict

- **The seam exists and is one function deep.** `emit_chunk()` hands a post-EQ, post-trim,
  1024-frame stereo int16 chunk to the encoder (`mini_icecast.c:677-685`). A sink tapped there
  inherits the tone section and the level meter, and misses only the encoder's 1024↔1152 frame
  alignment and its ≤26 ms silence padding. It gets *better* audio than the stream, not identical.
- **It fits, because the pacer already produces ahead of real time.** The DAC's jitter budget is
  not a new number: it is `lead_ms` (1500 ms) = **264,600 B** at 44.1 kHz stereo int16. Ring in
  PSRAM (265–512 KB); DMA **5,760 B** of internal RAM for **32.6 ms**.
- **The DAC must not be allowed to fail silently.** IDF repeats the last DMA buffer forever on
  underrun unless `auto_clear_*_cb` is set (verified in the TX ISR). Policy:
  `auto_clear_after_cb = true` + a short fade + a counter — never the default, a ~30 ms loop.
- **The clock question is not a problem here, and that is the single biggest difference from
  the radio-player research.** The station rate is immutable, and on the S3 the I2S divider is
  a rational `N + b/a` with `a ≤ 511`; 44,100 Hz from PLL_F160M is **exactly**
  6250/441 = 14 + 76/441 (`a = 441`), so the DAC and `esp_timer` run off the same 40 MHz
  crystal with **0 ppm** of division error. Nothing else is clocked by it.
- **Pins decide the board.** The bare S3 devkit has ≥20 free GPIOs. The Sunton 8048S050C has
  **four** (`0, 17, 43, 44`), two of which are the UART0 console — so the panel board cannot
  host a 3-wire DAC without losing flashing/monitoring. That is the finding that reshapes the
  question from "which codec" to "which board".

Axis: **a second consumer of the PCM the broadcast already produces**, on an ESP32-S3 whose
only output today is an MP3 stream clocked by its own encoder. Target: this repo, both supported
IDF versions (`esp32s3` only, 5.4.4 and 6.1), and the two boards in the tree.

Labels, as in `docs/research/radio-player-audio-out.md`: **VERIFIED** = read out of the primary
artefact linked on the same line (IDF source at a pinned tag, the local IDF trees at
`/Users/pedrocasaubon/esp/esp-idf` (v5.4.4) and `/Users/pedrocasaubon/esp/esp-idf-6.1` (v6.1),
cited as `IDF5.4:path:line` / `IDF6.1:path:line`, a datasheet, or `path:line` in this repo).
**LIKELY** = inference from verified material, no primary source for the exact claim.
**UNVERIFIED** = could not check, and why.

An `IDF5.4:`/`IDF6.1:` citation names a file and line in those trees, which are what this repo
actually builds against; each maps one-to-one onto the upstream file of the same path at tag
[`v5.4`](https://github.com/espressif/esp-idf/tree/v5.4) (e.g.
`components/esp_driver_i2s/i2s_common.c` is
[that file at v5.4](https://github.com/espressif/esp-idf/blob/v5.4/components/esp_driver_i2s/i2s_common.c)).

---

## 1. Where the sink plugs in

### 1.1 The call chain, end to end

```
ice_decode (core 0, prio 3)                       mini_icecast.c:775-822
  mi_dec_read → 384 KB decoded ring (PSRAM)       mini_icecast.c:761
ice_broadcast (core 1, prio 6)                    mini_icecast.c:1410-1412
  mi_ring_get(fifo)                               mini_icecast.c:905
  mi_rs_process(..., chunk_append)                mini_icecast.c:914
    chunk_append → S.chunk[1024*2]                mini_icecast.c:689-703
      emit_chunk()                                mini_icecast.c:677-685
        tone_apply(CHUNK_FRAMES)   ← EQ + trim   mini_icecast.c:667-675
        measure_level()            ← VU meter    mini_icecast.c:606-618
        mi_enc_feed(chunk, 1024, on_frame)        mini_icecast.c:684
          on_frame → broadcast(bytes) + pace()    mini_icecast.c:598-604
```

There is exactly one place where a complete, EQ'd, full-scale PCM chunk exists as a value:
the body of `emit_chunk()`, between `measure_level()` and `mi_enc_feed()`. **That is the
seam.** A second one is required for the tail of a track: `flush_pipeline()` applies the tone
to the partial chunk and feeds it separately (`mini_icecast.c:730-740`), so a tap placed only
in `emit_chunk()` would silently drop up to 1023 frames (23.2 ms) at every track boundary.

### 1.2 What a tap there inherits, and what it misses

| Property | A tap in `emit_chunk()` + `flush_pipeline()` | Why |
|---|---|---|
| 3-band EQ, per-chunk 2 dB slew | **inherited** | `tone_apply()` runs before the tap (`mini_icecast.c:667-675`, `tone_eq.c:142-161`) |
| Trim (GAIN, −12…+12 dB) | **inherited** | same call, `tone_eq.h:36-39` |
| Clamping at int16 full scale | **inherited** | `tone_eq_process` clamps (`tone_eq.h:8-15`) |
| Level metering (the VU meter) | **inherited** | `measure_level()` reads `S.chunk` after the EQ (`mini_icecast.c:606-618`) |
| The idle bed while a download owns the card | **inherited** | `idle_pump()` goes through the same `chunk_append` (`mini_icecast.c:705-725`) |
| MP3 frame alignment (1024 vs 1152) | **missed** | `mi_enc_feed` re-blocks into 1152-sample frames (`mi_encode.c` `acc[]`), so the stream lags the tap by up to 1152 frames = 26.1 ms |
| `mi_enc_flush()` silence padding | **missed** | the last partial encoder frame is completed with zeros (`mi_encode.c:344-350`), so the stream carries up to 26 ms of silence per track that the DAC never sees |
| Lossy coding | **missed (in the DAC's favour)** | the DAC gets the pre-encode PCM |

**Sample-for-sample?** Same samples, same order, same EQ, same trim — yes. Identical
*timeline*, no: the stream lags by up to one encoder frame, gains up to 26 ms of silence per
track, and is then lossily coded. The DAC is bit-exact to the EQ output; the stream is not.
For a product this is the right way round: the local output is the good one.

### 1.3 The seam must not block

`emit_chunk()` is called from `chunk_append()`, which is called from `mi_rs_process()`, which
is called from the broadcast task — the same task that runs `pace()` and therefore the whole
station's timing. A sink whose `write()` waits on I2S DMA would put a 33 ms back-pressure
inside the resampler's emit callback on every chunk. That is not fatal (the pacer would sleep
less), but it silently makes the DMA a second pacing authority — which is exactly the design
decision §3.5 is about. The interface must therefore be a **bounded, non-blocking push**
(§8.1), with the blocking write on the sink's own task.

### 1.4 Everything downstream of `pace()` is stream-only

`pace()` accounts *encoded* frames (`mini_icecast.c:576-596`), and `track_frames`,
`elapsed_ms` and `lag_ms` are all derived from it (`mini_icecast.c:1529`, `1553-1554`). With
the encoder switched off there is no equivalent accounting, so a local-only build must either
count frames at the sink or lose the screen's progress bar and the download path's backoff
signal (`mini_icecast_lag_ms()` is what `mini_files` backs off with — `README.md:532-539`).

---

## 2. The real-time problem

### 2.1 First, correct the premise

The encoder is **not** slower than real time at 44.1 kHz. Measured on the devkit, 10 786 µs
per 1152-frame encoder frame (`README.md:506-508`) against 26.1 ms of audio per frame is
**2.42×** real time. The worst synthetic figure (13 817 µs, `README.md:290`) is still 1.89×,
and the worst number anywhere in the tree (16.6 ms/frame on a 24/192 FLAC,
`docs/research/audio-decode-cost.md`) is 1.57×.

What is slower than real time is the **pipeline on hi-res material**: 0.63 card + 0.30 decode
+ 0.71 resample + 0.54 encode = 2.18 core-seconds per audio-second over two cores = 1.09 > 1.0
(`docs/research/audio-decode-cost.md`). That is why the 96 kHz source cap exists
(`mini_icecast.h:37-45`), and it is why a DAC changes the *failure mode* rather than the
capacity: the broadcast rebases, the DAC clicks.

The per-stage numbers the question quotes (decode ≈32 %, resample ≈12 %, SD ≈35 %) are the
192 kHz-FLAC case. For the normal corpus (16-bit/44.1 kHz FLAC) the same document measures
decode 14.6 %, SD 12.6 s/60 s = 21 %, resample 0 (bypass), encoder 61.1 %.

### 2.2 Can a DAC be fed from the 1500 ms lead? Yes — that *is* the budget

The pacer's contract is `played_us − now = lead_ms` (`mini_icecast.c:594-595`): it refuses to
produce more than `lead_ms` ahead, so **the maximum steady-state occupancy of any downstream
ring is `lead_ms`**. The DAC ring is not a new budget; it is the existing one, spent on a
second consumer.

Arithmetic, stereo int16 at 44,100 Hz (4 B/frame):

| Quantity | Value |
|---|---|
| bytes/s | 44,100 × 4 = **176,400** |
| one `MINI_ICECAST_CHUNK_FRAMES` (1024) | 4,096 B = **23.22 ms** |
| `lead_ms` 1500 | 1,500 × 176.4 = **264,600 B** (258.4 KiB) |
| ring at 256 KiB | 262,144 B = **1,486 ms** — 14 ms *short* of the lead: do not use |
| ring at 288 KiB | 294,912 B = **1,672 ms** |
| ring at 512 KiB | 524,288 B = **2,973 ms** (the recommended value; PSRAM is not the constraint) |

Where it must live: **PSRAM.** Internal RAM with the encoder kept internal is 35,327 B free
with an 18,432 B largest block (`README.md:1116-1130`); a 264 KB ring cannot be allocated
there at all. In the shipped default (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`,
`sdkconfig.defaults:66`) the encoder's state is already in PSRAM and internal free is
~155 KB — still not 264 KB. PSRAM is where `mi_ring_new(..., true)` already puts the 384 KB
decoded-audio ring (`mini_icecast.c:761`, `mi_ring.c:28-37`).

### 2.3 What refills it, and how fast

After a starvation rebase (`mini_icecast.c:584-593`) the pacer stops sleeping and runs at
production speed until it is `lead_ms` ahead again. Production capacity is `1 / (the largest
per-core occupancy)`, because every stage is serialised inside one task or the other:

| Board / condition | Encoder share | Capacity | Time to refill a 1 s deficit |
|---|---|---|---|
| devkit, real corpus | 41.4 % (`README.md:506-508`) | 2.42× | 1/(2.42−1) = **0.70 s** |
| Sunton, stream only | 59.9 % (`README.md:529`) | 1.67× | **1.49 s** |
| Sunton, with downloads | 68.9 % (`README.md:530`) | 1.45× | **2.22 s** |

So a rebase is not a permanent debit: the ring re-fills. A single rebase costs the DAC ring the
deficit (at most ~1 s, since that is the rebase threshold) and recovery takes 0.7–2.2 s. **With
a 1.5 s ring, the measured Sunton download soak — 17 rebases in 10 min, one per 35 s
(`README.md:530`) — would produce zero DAC underruns**, because there is far more refill time
than 2.2 s between stalls. Two rebases inside one refill window would not be survivable.

This is the quantitative answer to "sustainable with zero underruns while the encoder runs":
**yes, on the same evidence that already says the stream is sustainable**, because the DAC
consumes the same audio at the same mean rate and the ring is the same size as the lead. The
DAC adds no capacity requirement; it converts every *unrecovered* stall into an audible event
instead of a rebase log line.

### 2.4 The sink needs its own task, above the encoder

| task | core | prio | stack | why |
|---|---|---|---|---|
| `ice_broadcast` | 1 | 6 | 16 KB (`mini_icecast.c:46`) | resample + EQ + encode + pace |
| `ice_dac` (new) | 1 | **7** | 3 KB internal | blocks in `i2s_channel_write`; must preempt the encoder to refill DMA |
| `ice_telem` | 1 | 3 | 4 KB (`mini_icecast.c:47`) | 60 s log |
| `ice_decode` | 0 | 3 | 32 KB PSRAM (`mini_icecast.c:762`) | card + codec |
| LVGL port | 0 | 4 (`panel_rgb.c:47`) | 16 KB | panel refresh |

Two reasons it must be a task and not a callback: `i2s_channel_write()` takes a binary
semaphore, so a second concurrent writer gets `ESP_ERR_INVALID_STATE` rather than blocking
(`IDF5.4:components/esp_driver_i2s/i2s_common.c:1343`), i.e. exactly one writer task by
design; and filling DMA buffers from the `on_sent` ISR would require reading the ring from
ISR context, which cannot touch PSRAM when the cache is disabled — the very case
`CONFIG_I2S_ISR_IRAM_SAFE` exists for. Cost of the task: 176,400 B/s of `memcpy` into DMA
buffers (`i2s_channel_write` copies — `i2s_common.c:1364`) ≈ **0.04–0.15 %** of one core, plus 184
DMA-completion wakeups/s. It is not a CPU consumer.

### 2.5 What the peripheral actually does on underrun (VERIFIED from the ISR)

The DMA descriptor chain is built as a circle:

```c
/* Connect DMA descriptor as a circle */
STAILQ_NEXT(handle->dma.desc[i], qe) = (i < (num - 1)) ? (handle->dma.desc[i + 1]) : handle->dma.desc[0];
```
`IDF5.4:components/esp_driver_i2s/i2s_common.c:554-559`

The TX EOF ISR then only zeroes the finished buffer **if a flag is set**:

```c
if (handle->dma.auto_clear_before_cb) memset(curr_buf, 0, handle->dma.buf_size);   /* before on_sent */
if (handle->callbacks.on_sent) ...
if (handle->dma.auto_clear_after_cb)  memset(curr_buf, 0, handle->dma.buf_size);   /* after on_sent  */
```
`IDF5.4:components/esp_driver_i2s/i2s_common.c:666-695` (same code at
`IDF6.1:components/esp_driver_i2s/i2s_common.c:839-858`)

Therefore:

| Config | Underrun behaviour |
|---|---|
| `auto_clear_after_cb = false`, `auto_clear_before_cb = false` (**IDF default**, `I2S_CHANNEL_DEFAULT_CONFIG`) | the chain laps the writer and **re-sends the stale buffer contents forever** — the last `dma_desc_num × dma_frame_num` frames (32.6 ms at the defaults) looped. Not silence, not "hold last sample": a ~30 Hz buzz |
| `auto_clear_after_cb = true` | the just-sent buffer is zeroed after `on_sent`, so the next lap is **silence**; a callback can still read what was sent |
| `auto_clear_before_cb = true` | zeroed before `on_sent`; a callback gets a clean buffer to partly fill and **cannot** read the sent data |

The two flags are not "on/off" versions of each other: both mean "auto-fill with zero when no
new data arrives", and they differ only in which side of `on_sent` the memset happens
(`i2s_common.c:69-76`). **Cost of the flag:** one `memset` of `buf_size` per completed buffer —
960 B every 5.44 ms at the defaults, in internal RAM, in the ISR. Negligible, and it is the
difference between a buzz and silence.

**Right policy for this device:** `auto_clear_after_cb = true` (silence is the honest failure:
the card stopped, not the music), plus in the feeder:

1. when the ring is short of a full buffer, write zeros rather than a partial buffer, and
   ramp the last 64–128 frames down first (a hard cut to zero is itself a click);
2. ramp back up over the same length when data returns;
3. count `underruns` and the ring's minimum occupancy per telemetry window and log them next
   to the existing `starvation rebases` line (`mini_icecast.c:1279-1282`).

Freeze-the-clock (disable the channel) is *not* the right answer: `i2s_channel_disable()`
stops BCLK/WS, which most DACs (PCM5102A explicitly: "Automatic Power-Save Mode When LRCK And
BCK Are Deactivated") treat as power-down, and re-enabling re-links the descriptors from
`desc[0]` with `xQueueReset` (`i2s_common.c:121-137`) so stale audio is re-sent — the prior
research's §1.4 inference, still unconfirmed on hardware.

### 2.6 The existing telemetry is the instrument

`mini_icecast_lag_ms()` (`mini_icecast.c:1529`) plus the `starvation rebases` counter already
measure exactly the quantity the DAC cares about — how far the producer fell behind. The
experiment that answers "will the DAC underrun" without a DAC attached is: run the existing
download/soak matrix, log `lag_ms` at, say, 20 Hz, and compute the fraction of time the
*integral* of the shortfall exceeds `lead_ms`. That number is the underrun count the DAC would
have seen. This is a host-side analysis over an existing 60 s log line, not new firmware.

---

## 3. Rate and clocking

### 3.1 The station rate is immutable — verified, not assumed

- `S.station` is written exactly once, by `mi_station_resolve(cfg, &S.station, …)` in
  `mini_icecast_start()` (`mini_icecast.c:1314`).
- There is no setter: the public surface has `mini_icecast_eq_set`, `gain_set`, `skip`,
  `prev`, `play`, `shuffle_*`, `park/unpark` and no rate call (`mini_icecast.h:130-199`).
- Every post-start read is a read: `S.station.out_hz` appears 26 times in
  `mini_icecast.c`, and the only assignment pattern in the file is the resolve
  (`grep 'S\.station\.out_hz =' ` → no matches).
- The EQ's coefficients are built once for that rate (`tone_eq_init` at
  `mini_icecast.c:1322`) and only rebuilt on a dB change (`tone_eq.c:51,69,87`), and the
  encoder is constructed for it (`mini_icecast.c:1331`).

So a DAC configured at boot can never see a rate change. **This is the whole difference from
`docs/research/radio-player-audio-out.md` §4**, whose central problem was "the rate is known
only after the decoder parses the stream and can change between tracks". Here it cannot.

### 3.2 An exact 44.1 kHz bit clock is achievable — the arithmetic

On the S3 the I2S master clock is `sclk / (N + b/a)` with `N ≤ 255` and `a < 512`
(`IDF5.4:components/hal/esp32s3/include/hal/i2s_ll.h:35-36`, `:299-325`), and IDF searches for
the best `b/a` (`IDF5.4:components/hal/hal_utils.c:69-100`, called from
`IDF5.4:components/hal/i2s_hal.c:51-61`). BCLK is then MCLK divided by an integer
(`i2s_ll_tx_set_bck_div_num`, `i2s_ll.h:284-287`).

With the default source PLL_F160M = 160 MHz (`clk_tree_defs.h:311,317` in both trees) and
`mclk_multiple = 256`, MCLK = 44,100 × 256 = 11.2896 MHz:

```
160,000,000 / 11,289,600 = 6250/441 = 14 + 76/441          a = 441 ≤ 511  → exact
```

and the hardware encoding is lossless for that pair: `z = 76`, `x = 441/76 − 1 = 4`,
`y = 441 mod 76 = 61`, and `1/(x+1+y/z) = 1/(5 + 61/76) = 76/441` — the fraction asked for
(`i2s_ll.h:353-368`).

| target | source | exact ratio | a | error |
|---|---|---|---|---|
| 44,100 × 256 | PLL_F160M | 6250/441 = 14+76/441 | 441 | **0 ppm** |
| 44,100 × 256 | PLL_D2 (240 MHz) | 3125/147 = 21+38/147 | 147 | **0 ppm** |
| 48,000 × 256 | PLL_F160M | 13+1/48 | 48 | **0 ppm** |
| 44,100 × 384 (24-bit slots) | PLL_F160M | 9.4482… | none ≤ 511 | −1.75 ppm (44,100.077 Hz) |

**VERIFIED** for the ratiometric arithmetic and the divider's form; **LIKELY** for the
hardware realising the encoding exactly (derived from IDF's own `i2s_ll_tx_set_mclk`
mapping, not measured on a scope). There is **no APLL option on the S3**: `SOC_I2S_CLKS` is
`{PLL_F240M, PLL_F160M, XTAL, EXTERNAL}` and the S3 clock tree has no `SOC_MOD_CLK_APLL` at all
(`clk_tree_defs.h:102-126,311` in both trees) — unlike the classic ESP32. The fractional
divider is what replaces it.

### 3.3 What error is audible, and where drift would matter

Absolute accuracy is the crystal, not the divider: the WROOM-1 module specifies 40 MHz at
±10 ppm (`docs/research/radio-player-audio-out.md:174`). At 44,100 Hz that is
44,100 × 10⁻⁵ = **0.44 frames/s**, **0.017 cents** of pitch (`1200·log₂(1+10⁻⁵)`) — inaudible
by two orders of magnitude against the ~5–10 cent JND, and **36 ms/hour** of drift against the
wall clock.

Where drift *would* matter, and why it does not here:

- **Between the DAC and the pacer: it cannot happen.** MCLK comes from a PLL locked to the
  40 MHz crystal; `esp_timer` is the systimer clocked from XTAL
  (`clk_tree_defs.h:135-136`: `SYSTIMER_CLK_SRC_DEFAULT = SOC_MOD_CLK_XTAL`). One crystal, one
  timebase. The ring's occupancy therefore changes only by whole chunks (23.2 ms granularity),
  never by ppm.
- **Between the DAC and a stream client: irrelevant.** The stream is data over TCP; the client
  buffers and plays on its own clock. There is no clock to share.
- **Between the DAC's playhead and the display's `elapsed_ms`:** 36 ms/hour, which is smaller
  than the 26 ms encoder-frame granularity already documented for the progress bar
  (`mini_icecast.h:100-104`).

### 3.4 Native-rate (bit-perfect) mode: what it would cost

A per-track rate is a `i2s_channel_disable()` → `i2s_channel_reconfig_std_clock()` →
`i2s_channel_enable()` cycle, legal only in READY (`IDF5.4:i2s_std.c:376` and
`IDF6.1:i2s_std.c:395`, both `ESP_ERR_INVALID_STATE` otherwise). Consequences, in this repo's
terms:

1. **The EQ breaks.** `tone_eq_t.rate` is fixed at `tone_eq_init()` (`tone_eq.c:114-118`), so a
   native 96 kHz track would run the 200 Hz / 1 kHz / 4 kHz corner frequencies at 2.2× their
   labels. Fixing it means `tone_eq_init()` per rate change — which resets the biquad state and
   therefore clicks, on the very boundary you were trying to make clean.
2. **The stream needs the resampled copy anyway**, so the pipeline forks: one native path to
   the DAC, one resampled path to the encoder, either by decoding twice or by fanning out
   post-decode and resampling only the stream branch. The second is the only sane one, and it
   moves the resampler off the shared path (`mini_icecast.c:885-915`).
3. **The rate set widens but does not become free.** Broadcast-legal rates are the encoder's
   nine (`mi_encode.c:24-29`); a native DAC wants 44.1/48/88.2/96/176.4/192 kHz, of which the
   last two are above `MINI_ICECAST_MAX_SOURCE_HZ` and would re-open the over-capacity case the
   cap exists to prevent (`docs/research/audio-decode-cost.md`).
4. **Which sources are on the table:** anything the card holds (the resampler accepts any
   non-zero rate, `mi_resample.c:63-70`), so a hi-res library is exactly where native mode pays
   — and exactly where the board is already at 1.09× real time.

Verdict: bit-perfect is a **separate product decision** (a "local only, no encoder" mode), not a
flag on the streaming path. It costs a second PCM branch and a per-track I2S reconfiguration,
and it buys nothing for a 44.1 kHz library — which is most of one.

### 3.5 The two-masters problem, restated for this device

| Who | Clocked by | Can it be late? | Can it rebase? |
|---|---|---|---|
| Encoder (today's pacer) | `esp_timer` (XTAL) | yes — it sleeps and has a 1 s rebase (`mini_icecast.c:584-595`) | yes, silently |
| I2S DAC | PLL from the same XTAL | no — a missed DMA period is an audible event | **no**: a rebase is a gap |
| Stream client | its own clock, over TCP | yes (10 s send ring, `mini_icecast.c:63`) | yes, by buffering |

Since the first two share a crystal, "disagreement" can only come from the *policy*, not the
oscillator: the encoder may stop producing for up to 1 s and call it a rebase; the DAC cannot.
So the rule is: **the encoder keeps pacing, and the DAC's ring is the shock absorber that makes
the pacer's rebase invisible** — up to `lead_ms`. When the encoder is absent, the DMA's own
back-pressure becomes the pacer (§8.3), and there is no disagreement at all because there is
only one consumer.

What breaks if they disagree beyond the ring: the DAC underruns, plays the fade, and the audio
the DAC emits is then *behind* the stream's timeline by the deficit — permanently, until the
next track. Nothing resynchronises them, and nothing needs to.

---

## 4. The IDF API surface on 5.4.4 and 6.1

### 4.1 The premises in the brief, checked

| Claim | Result |
|---|---|
| `components/driver/deprecated/driver/i2s.h` exists in 5.4.4 | **VERIFIED** — `IDF5.4:components/driver/deprecated/driver/i2s.h`, starts with "This file is for the backward compatible to the deprecated I2S APIs" and `#warning`s unless `CONFIG_I2S_SUPPRESS_DEPRECATE_WARN` |
| …and is absent in 6.1 | **VERIFIED** — `components/driver/deprecated/` does not exist at all in `IDF6.1`; `find` for any `driver/i2s.h` returns nothing |
| `esp_driver_i2s/include/driver/i2s_std.h` exists in both | **VERIFIED** — both trees have `i2s_common.h i2s_etm.h i2s_pdm.h i2s_std.h i2s_tdm.h i2s_types.h` (+ `lp_i2s*.h`) |

Consequence: the legacy API is not an option. It is a 5.4-only header that the 6.1 build cannot
even see, so any use of it would fork the component — which the layering rules forbid
(`host/tests/layering.sh:86-93`: no `CONFIG_IDF_TARGET` in `portable/`, and every `.c` in
`portable/` must compile on the host, i.e. cannot include an IDF driver at all).

### 4.2 Struct and signature deltas that matter

| Item | 5.4.4 | 6.1 |
|---|---|---|
| `i2s_chan_config_t::id` | `i2s_port_t` | `int` |
| `tx_destination` / `rx_destination` | — | present, `I2S_DESTINATION_DMA = 0` (`IDF6.1:components/esp_hal_i2s/include/hal/i2s_types.h:77-78`) |
| `i2s_std_clk_config_t::bclk_div` | — | present, slave role only, "shouldn't be smaller than 8" |
| `I2S_CHANNEL_DEFAULT_CONFIG` | `dma_desc_num = 6`, `dma_frame_num = 240`, both `auto_clear_*` false, `allow_pd` false, `intr_priority` 0 | same + the two destination fields |
| `i2s_channel_reconfig_std_clock(handle, clk_cfg)` | `i2s_std.h:329` | identical, `i2s_std.h:335` |
| `auto_clear` / `auto_clear_after_cb` union, `auto_clear_before_cb` | present | present, identical wording |
| `i2s_channel_preload_data` | present (READY-only) | present (READY-only) |
| New in 6.1 | — | `i2s_channel_tune_rate()` (READY-only, "fine-tuning the mclk to match the speed of producer and consumer"), `i2s_channel_get_sync_count()`, `i2s_channel_config_tx_fifo_sync()` — the last two behind `SOC_I2S_SUPPORTS_TX_SYNC_CNT` / `_TX_FIFO_SYNC`, which **the S3 does not define**, so they do not exist for this target |
| IRAM control | `CONFIG_I2S_ISR_IRAM_SAFE` | same, plus `CONFIG_I2S_CTRL_FUNC_IN_IRAM`, plus a linker fragment pinning the two ISR callbacks `(noflash)` (`IDF6.1:components/esp_driver_i2s/linker.lf`) |

**The portable way to write the initialiser** (works on both, and survives the next delta):

```c
i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
chan.dma_desc_num  = 6;
chan.dma_frame_num = 240;
chan.auto_clear_after_cb = true;   /* §2.5: silence, not a stale-buffer loop */
i2s_std_config_t std = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(rate),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = bck, .ws = ws, .dout = din, .din = I2S_GPIO_UNUSED },
};
```

A hand-written compound literal that omits `.tx_destination` is *also* safe on 6.1 because the
enum's DMA value is 0, but the macro is the better habit.

### 4.3 Init sequence (identical on both versions)

1. `i2s_new_channel(&chan, &tx, NULL)` — TX only; the handle is `REGISTERED`.
2. `i2s_channel_init_std_mode(tx, &std)` — `READY`. GPIOs are claimed here (`gpio_matrix_out`).
3. *optional* `i2s_channel_preload_data(tx, pcm, n, &loaded)` while still `READY` — otherwise
   "the initial DMA buffer has no data inside, it will transmit the empty buffer after enabled"
   (`IDF5.4:components/esp_driver_i2s/include/driver/i2s_common.h:190-192`). This is the
   pop-free start: preload with a 32 ms pre-roll, then enable.
4. `i2s_channel_enable(tx)` — `RUNNING`; the TX ISR is allocated on the calling core, so call
   this from the core you want the interrupt on.
5. `i2s_channel_write(tx, buf, n, &written, timeout_ms)` in a loop.
6. Shutdown: `i2s_channel_disable(tx)`, `i2s_del_channel(tx)`.

`i2s_channel_write` is **blocking** and **copies**: it takes a binary semaphore
(`IDF5.4:i2s_common.c:1343`), then blocks on `xQueueReceive(msg_queue, …)` until a DMA
descriptor completes (`:1351-1357`), then `memcpy`s up to one buffer at a time (`:1364`).
On timeout it returns `ESP_ERR_TIMEOUT` with `*bytes_written` holding the partial count — a
caller with a finite timeout must loop, and a caller with `timeout_ms = 0` gets "as much as
currently fits", which is what a non-blocking push wants. All public I2S APIs are thread safe
but **not ISR safe** (docs, "Thread Safety",
[I2S docs](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/i2s.html)).

### 4.4 DMA sizing rules

- `dma_buffer_size = dma_frame_num × slot_num × slot_bit_width / 8` and must be
  **≤ 4092 B** (`IDF5.4:docs/en/api-reference/peripherals/i2s.rst:1147-1173`; the constant is
  `4095 − 3 = "I2S_DMA_BUFFER_MAX_SIZE"` on chips without L1-cache-visible internal memory,
  `i2s_common.c:70-76`, and the S3 does not define `SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE`, so
  4092 is the S3's number). The driver *clamps* an oversized buffer with a warning
  (`i2s_common.c:477-482`) — a silent sample-rate change if you do not read the log.
- `dma_frame_num` must be a **multiple of 3 for 24-bit data**, and so must
  `mclk_multiple` and the write size, "otherwise the data on the line or the sample rate will
  be incorrect" (`i2s.rst:377`). For 16-bit data this does not apply.
- Interrupt interval = `dma_frame_num / sample_rate`; `dma_desc_num` must exceed
  `polling_cycle / interrupt_interval` (`i2s.rst:1147-1158`). Here the feeder blocks rather
  than polls, so the useful rule is the inverse: depth = `dma_desc_num × dma_frame_num / rate`
  is your **last-line jitter absorber**, and 20–50 ms is the sane band.

| config | buf/frame | per buffer | total | depth @44.1 k | IRQ |
|---|---|---|---|---|---|
| 6 × 240 (default) | 4 B | 960 B | **5,760 B** | **32.6 ms** | 5.44 ms |
| 8 × 512 | 4 B | 2,048 B | 16,384 B | 92.9 ms | 11.6 ms |
| 6 × 1023 (max) | 4 B | 4,092 B | 24,552 B | 139 ms | 23.2 ms |

Recommendation: **6 × 240, 5,760 B, 32.6 ms.** It is under 6 % of the 102,408 B of internal RAM the encoder takes,
it fits the 18,432 B largest block even in the encoder-internal build, and the ring — not the
DMA — is what absorbs jitter.

### 4.5 IRAM / ISR-safe, and why it is load-bearing here

`CONFIG_I2S_ISR_IRAM_SAFE=y` keeps the TX EOF interrupt serviceable while the cache is off and
places the driver object in DRAM (`IDF5.4:components/esp_driver_i2s/Kconfig:3-9`; docs "IRAM
Safe"). In this firmware that is not boilerplate: on the panel build the app's rodata and instructions
live in PSRAM (`sdkconfig.defaults.sunton`: `CONFIG_SPIRAM_FETCH_INSTRUCTIONS`/`_RODATA`, and
`README.md:1044-1050`), the encoder's hot loop is already in IRAM
(14,351 B of code, 22,913 B with tables, `README.md:983-992`), and the panel's framebuffer
traffic saturates the cache. A deferred I2S EOF during a flash write is a guaranteed DMA
underrun. Cost: whatever the two ISR callbacks and GDMA's handler weigh — the 6.1 linker
fragment pins exactly `i2s_dma_rx_callback` / `i2s_dma_tx_callback`
(`IDF6.1:components/esp_driver_i2s/linker.lf`), i.e. a few hundred bytes to ~1 KB, against a
region that already holds 14 KB of shine. Affordable, and it must be **measured**, not assumed,
because that region is shared with data.

### 4.6 Build surface

`REQUIRES esp_driver_i2s` (in-tree component, `idf_component_register(... INCLUDE_DIRS include)`
— `IDF5.4:components/esp_driver_i2s/CMakeLists.txt`). No `idf_component.yml` entry is needed
while the IDF tree is in-tree; if the dependency is ever pinned through the component manager
the name is `espressif/esp_driver_i2s`, and the minimum useful floor is 5.3, where the driver
split out of `driver/`. The repo's existing pattern is the plain one:
`components/app/board_esp32/CMakeLists.txt` lists `esp_driver_sdspi esp_driver_spi …` with no
`.yml`, while only `screen_display` (LVGL, `esp_lcd_touch`) has one.

---

## 5. Hardware choice

### 5.1 The three candidates for *this* board

| | MAX98357A | PCM5102A | ES8311 / ES8388 (I²C codec) |
|---|---|---|---|
| What it is | mono Class-D amp, I²S in | stereo line-out DAC | stereo codec with digital volume |
| Pins | BCK, WS, DIN (no MCLK pin — "apply signals at the BCLK, LRCLK, and SDIN test points") | BCK, WS, DIN **or** +SCK; XSMT for mute | BCK, WS, DIN, MCLK (or derived), SDA, SCL, + RST/PA |
| Driver beyond I²S | nothing | nothing (3-wire mode) | I²C register init (`esp_codec_dev`) |
| Volume | **none in the chip**; gain 3/6/9/12/15 dB by strapping `GAIN_SLOT` (**UNVERIFIED** — see §11), mute by `SD_MODE` | none; digital only | register volume: ES8311 REG32; ES8388 `LDACVOL/RDACVOL` + `LOUT1VOL/ROUT1VOL` |
| Pop / start-stop | `SD_MODE` low = shutdown; no soft ramp documented | **soft up/down ramp + analog mute, 120 dB mute SNR, 150-sample ramp** | soft ramp in the DAC volume registers |
| Rate range | 8–96 kHz typical (datasheet unreachable) | **8–384 kHz**, f_SCK multiples 64…3072, ≤ 50 MHz | ES8311 table 8 k–96 k; ES8388 single-speed 8–50 k |
| Fits this device? | yes for the devkit; mono only, and 3.3 V logic on a 1.8–3.3 V VDDIO part | **yes** — 3 wires, 3.3 V, no control bus, real stereo, line out | over-specified, and needs pins the Sunton does not have |

Sources for the PCM5102A column, all **VERIFIED** from the datasheet
([PCM510xA, SLAS859C](https://www.ti.com/lit/ds/symlink/pcm5102a.pdf)): title "Audio Stereo DAC
with PLL and 32-bit, 384 kHz PCM Interface"; "The integrated PLL on the device removes the
requirement for a system clock (commonly known as master clock), allowing a 3-wire I2S
connection and reducing system EMI"; "Intelligent Muting System; Soft Up or Down Ramp and
Analog Mute For 120-dB Mute SNR"; "Automatic Power-Save Mode When LRCK And BCK Are
Deactivated"; XSMT pin "Soft mute control (Low) / soft un-mute (High)" and "When the XSMT pin
is pulled low, the incoming PCM data is attenuated to 0, closely followed by a hard analog
mute. This process takes 150 sample times"; "The internal PLL is disabled as soon as an external
SCK is supplied… if BCK and LRCK start correctly while SCK remains at ground level for 16
successive LRCK periods, then the internal PLL starts".

**A constraint that decides the slot format:** PCM5102A Table 11 lists the BCK-per-LRCK rates
the internal PLL accepts — at 44.1 kHz, **32 f_S (1.4112 MHz) or 64 f_S (2.8224 MHz)** only.
16-bit slots give BCLK = 32 f_S ✓; 32-bit slots give 64 f_S ✓; **24-bit slots (48 f_S) are not
in the table**, so "24-bit output" on a 3-wire PCM5102A is not a thing. Use 16-bit slots and
skip the conversion entirely.

The MAX98357A column is **weaker evidence**: `analog.com` and
`datasheets.maximintegrated.com` were unreachable from this environment (timeout, and an HTTP/2
reset on retry) — the same wall the prior research hit
(`docs/research/radio-player-audio-out.md:114,362`). What *was* readable, the evaluation-board
page mirrored at
[alldatasheet.jp](https://www.alldatasheet.jp/html-pdf/1340641/MAXIM/MAX98357A/725/4/MAX98357A.html),
confirms three signals (BCLK/LRCLK/SDIN, no MCLK), that `SD_MODE` both selects the stereo input
channel and enters shutdown when grounded, and 1.8–3.3 V VDDIO. The gain strap is **UNVERIFIED**.

The codec column is by citation: ES8311's `coeff_div[]` table, `mclk_div` 256 default, and
`use_mclk=false ⇒ DIG_MCLK = LRCK × 256` are verified from Espressif's own driver
([es8311.c](https://github.com/espressif/esp-adf/blob/master/components/esp_codec_dev/device/es8311/es8311.c)),
and ES8388's digital vs output-driver volume stages from its datasheet
([ES8388 Rev 4.0](http://www.xhxinchip.com/uploads/soft/20220907/1-220ZG54T5941.pdf)) — both as
recorded in `docs/research/radio-player-audio-out.md:124-125`. Everest's own site served no PDF
then and does not resolve from here now.

### 5.2 The pop/click story, compared honestly

| event | PCM5102A | MAX98357A | ES8311 |
|---|---|---|---|
| start | preload DMA + enable; PLL locks silently; XSMT high → 150-sample soft un-mute | `SD_MODE` release + first valid BCLK; no ramp documented → expect a click unless a GPIO mutes an external PA | write mute/volume registers before enabling I²S |
| stop | XSMT low → soft mute then analog mute; or stop BCLK and it power-saves | `SD_MODE` low | mute register, then disable |
| rate change | not applicable (station rate is fixed); in native mode, XSMT low around the disable/reconfig/enable | same | re-program the PLL registers before the new rate starts (`esp_codec_dev_open`) |
| underrun | silence if fed zeros; **stale loop if the driver's auto-clear is off** (§2.5) | same | same |

The PCM5102A is the only one of the three where the *chip* helps: a soft ramp is documented,
and one GPIO (XSMT) gives a clean mute that is independent of the firmware being healthy.

### 5.3 Does I²S conflict with the panel's DMA or the SPI card? (verified)

- **GDMA channels.** The S3 has **one AHB GDMA group with 5 pairs**
  (`IDF5.4:components/soc/esp32s3/include/soc/soc_caps.h:166-171`). The RGB panel takes one
  (`gdma_new_ahb_channel` + `gdma_connect(… GDMA_TRIG_PERIPH_LCD …)`,
  `IDF5.4:components/esp_lcd/rgb/esp_lcd_panel_rgb.c:63-66,894`), SPI2 for the card takes one,
  I²S TX takes one: **3 of 5**. There is no fixed per-peripheral channel table in the S3 HAL
  (`hal/esp32s3/include/hal/gdma_ll.h` has no "fixed channel" rule), so allocation is dynamic.
  Headroom: 2 pairs.
- **Peripheral muxing.** None: LCD_CAM and I²S are separate peripherals and the panel's pins
  (39/40/41/42 + 16 data) are unrelated to any I²S pin. The only shared resource is GPIO 19/20
  on the Sunton, already spent on the touch I²C.
- **PSRAM bandwidth.** The panel streams 800 × 480 × 2 B × 60 Hz ≈ **46 MB/s** out of PSRAM
  with `fb_in_psram = true` (`panel_rgb.c:95`). The DAC ring reads 176 KB/s = **0.38 %** of
  that. Not a contention question; the ring must simply not be given a higher DMA/cache
  priority than the display, and it is not on the DMA path at all (the driver memcpys from
  PSRAM into internal DMA buffers).
- **Interaction with the encoder's IRAM use:** additive, §4.5.

---

## 6. Pins

### 6.1 What the SoC makes unavailable, always

| pins | owner | source |
|---|---|---|
| 26, 27, 28, 29, 30, 31, 32 | MSPI flash: CS1, HD, WP, CS0, CLK, MISO, MOSI | `IDF5.4:components/soc/esp32s3/include/soc/spi_pins.h:11-17` |
| 33, 34, 35, 36, 37 | octal PSRAM: D4, D5, D6, D7, DQS | `spi_pins.h:18-22` |
| 19, 20 | USB-Serial-JTAG (D−, D+) | `IDF5.4:components/soc/esp32s3/include/soc/usb_pins.h` |
| 22, 23, 24, 25 | do not exist on the S3 (numbering jumps 21 → 26) | S3 datasheet pin list |
| 0, 3, 45, 46 | strapping: boot mode, JTAG source, VDD_SPI, ROM message. GPIO0 and GPIO46 **VERIFIED** (repo `README.md:166`; `esp_rom/esp32s3/include/esp32s3/rom/efuse.h:210-211` "uart print when GPIO46 is low when digital reset"); GPIO3 (STRAP_JTAG_SEL) and GPIO45 (VDD_SPI) **LIKELY** — the datasheet has exactly four strapping sub-sections (boot-mode, jtag-signal, rom-msg, vdd-spi) but its pin table's glyphs could not be extracted from the PDF | S3 datasheet §Strapping Pins; efuse headers |

That leaves GPIO **1–18, 21, 38–44, 47, 48** as ordinary I/O — 28 pins — before any board
claims one.

Two clarifications this census needed, both from the datasheet's pin list rather than from the
repo: the USB-Serial-JTAG pads are **19/20** (`IDF5.4:components/soc/esp32s3/include/soc/usb_pins.h`
— `USBPHY_DM_NUM 19`, `USBPHY_DP_NUM 20`), while **39/40/41/42** are the external JTAG port's
MTCK/MTDO/MTDI/MTMS. `README.md:173` says "Because the card is on SPI, the USB-JTAG pins are
untouched" without naming them - read charitably it means the external JTAG pads (39-42), which
the specification's SDMMC wiring would have spent; the conclusion holds for the devkit (the SPI
card leaves both the USB pads 19/20 and the JTAG pads 39-42 free), but the panel board spends
39–42 on HSYNC/VSYNC/DE/PCLK, so it has spent the JTAG port as well. The wording is worth
tightening in the README when this work is picked up, not before.

### 6.2 Bare S3 devkit (N16R8): what is free

Owned: 10, 11, 12, 13 (SPI card — `components/app/board_esp32/board_esp32.c:57-60`), 19/20
(USB), 26–37 (flash/PSRAM). Console on 43/44.

Free: **1, 2, 3, 4, 5, 6, 7, 8, 9, 14, 15, 16, 17, 18, 21, 38, 39, 40, 41, 42, 45, 46, 47, 48** —
24 pins, more than any DAC needs. Three of them (3, 45, 46) are strapping pins: usable as
outputs once the boot straps have been latched, which is why the recommendation below avoids
them.

**Recommended (devkit): BCK = 17, WS = 18, DIN = 16, MCLK = 15 (only if a codec needs it),
XSMT = 14.** Rationale: contiguous, no strapping pin, no USB, no flash, no card, keeps 43/44 for
the console and 0 for the BOOT button.

### 6.3 Sunton 8048S050C: what is free

| owner | pins | source |
|---|---|---|
| RGB panel data | 8, 3, 46, 9, 1, 5, 6, 7, 15, 16, 4, 45, 48, 47, 21, 14 | `components/app/screen_display/panel_rgb.c:35-37` |
| RGB panel sync | HSYNC 39, VSYNC 41, DE 40, PCLK 42 | `panel_rgb.c:90-93` |
| GT911 touch | SDA 19, SCL 20, INT 18, RST 38 | `components/app/screen_display/touch_gt911.c:42-45` |
| backlight | 2 | `README.md:723` |
| SPI card | 10, 11, 12, 13 | `board_esp32.c:57-60` |
| flash + octal PSRAM | 26–37 | §6.1 |

Count them: 16 + 4 + 4 + 1 + 4 + 12 = 41. The S3's GPIO set is 0–21 and 26–48 (the numbering
skips 22–25), i.e. 45 pins, so **exactly four are free: GPIO 0, 17, 43, 44.**

| pin | state | can it carry I²S? |
|---|---|---|
| 0 | BOOT strap, and the BOOT button on most devkits | output-capable after reset; driving it fights the button |
| 17 | free | yes — the one clean pin |
| 43 | U0TXD — the console (board profile: `"console": "uart0"`) | only by giving up the log |
| 44 | U0RXD — the console / flashing | only by giving up flashing and monitoring |

So the Sunton can host **one** wire honestly, two if the console is sacrificed, and not three.
Note also that the touch on 19/20 already means the native USB-Serial-JTAG console is
unavailable on that board, so 43/44 is the *only* console — moving it is not an option.

**The one audio output the Sunton can carry: I²S PDM TX.** One pin, no external DAC, an RC
low-pass on the output, 16-bit only (`i2s_channel_init_pdm_tx_mode`, `SOC_I2S_SUPPORTS_PDM_TX 1`
in both trees' `soc_caps.h`). It is a speaker-grade path with no volume control and no stereo —
but it is what fits, and it is worth naming so the "no pins" answer is not mistaken for "no
audio".

### 6.4 What the fourth and the extra pins cost

| extra signal | cost | note |
|---|---|---|
| MCLK | 1 GPIO (15 on the devkit). Clock is `rate × 256` = 11.2896 MHz, exact | PCM5102A does not need it; MAX98357A has no pin for it; ES8311/ES8388 do (ES8311 can derive it from LRCK) |
| I²C for a codec | 2 GPIO + an `i2c_master` bus | **on the Sunton it costs zero pins**: the codec can share I²C0 with the GT911 (address 0x30 vs 0x5D, `touch_gt911.c:143-150`). It does not rescue the board, because BCK/WS/DIN are still missing |
| amp mute / XSMT | 1 GPIO | the difference between a click and a ramp; PCM5102A's XSMT and MAX98357A's `SD_MODE` both want it |
| second stereo pair (TDM) | 1 GPIO (DIN2), `SOC_I2S_SUPPORTS_TDM 1` | not needed: the broadcast is stereo |

---

## 7. Memory and task budget

### 7.1 Internal DRAM (the scarce class)

| item | bytes | where | note |
|---|---|---|---|
| DMA buffers, 6 × 240 frames × 4 B | 5,760 | internal, `MALLOC_CAP_INTERNAL｜DMA` | mandatory: `I2S_DMA_ALLOC_CAPS`, `i2s_private.h:50`; descriptors "cannot be placed in PSRAM" |
| 6 × `lldesc_t` (12 B) | 72 | internal | `i2s_alloc_dma_desc`, `i2s_common.c:516-560` |
| channel object + msg queue (5 × ptr) + mutexes | ≈ 300 | internal | one `i2s_channel_obj_t`, one `xQueueCreateWithCaps(desc_num − 1)` |
| feeder task stack | 3,072 | internal | runs during flash/PSRAM-cache-off windows; do **not** put it in PSRAM |
| **subtotal, internal** | **≈ 9.2 KB** | | |
| PCM ring, 512 KiB | 524,288 | **PSRAM** | `mi_ring_new(cap, true)` |
| decoded-source ring (exists) | 393,216 | PSRAM | `mini_icecast.c:761` |

Fit against the two measured configurations:

| build | internal free | largest block | 9.2 KB sink? |
|---|---|---|---|
| encoder in PSRAM (shipped default, `sdkconfig.defaults:66`) | ~155 KB (`README.md:1133-1135`) | ~78 KB | yes, with room |
| encoder internal (`SPIRAM_MALLOC_ALWAYSINTERNAL=131072`) | 35,327 B (`README.md:1118`) | 18,432 B | yes, but it spends 26 % of the free total |

Nothing here needs to move to PSRAM that is not already there; what **cannot** move is the DMA
buffers, the descriptors, the feeder stack, and (by measurement, `README.md:222-228`) the
encoder's own state.

### 7.2 Per-sample conversion

- **16-bit Philips in 16-bit slots** (recommended): zero conversion. The ring stores exactly
  what the chunk holds.
- **16-in-32 left-justified** (if a codec insists): 2 shifts per frame, 88,200/s. At a
  pessimistic 4 cycles each that is 353 k cycles/s ≈ **0.15 %** of one core, and it doubles the
  ring only if the ring stores 32-bit — so widen on the way out, not on the way in.
- **Mono downmix for a MAX98357A**: `(l + r) >> 1`, 44,100 adds + shifts per second ≈ **0.06 %**
  of one core. Cheap — but the chip already does stereo→(L+R)/2 when `SD_MODE` selects it, so
  the software cost can be zero.

### 7.3 Tasks and priorities

One task, priority 7 on core 1, 3 KB internal stack, blocked in `i2s_channel_write` with a
finite timeout (e.g. 20 ms) so it can notice a stop request. It owns the channel: exactly one
writer by contract, which the driver enforces with a binary semaphore anyway
(`i2s_common.c:1343`). No polling, no timer: the DMA completion posts a message queue entry and
the write returns.

---

## 8. How it should be shaped in this codebase

### 8.1 A portable sink behind a small interface, with the I²S half in `app/`

The house pattern already exists twice: `app_board` (interface in `portable/app_board/include`,
implementation in `app/board_esp32/board_esp32.c`, host implementation in `host/board_host.c`)
and `screen_caps` ("a capability is asked for, not assumed",
`components/app/screen_display/screen_caps.h:1-37`, ADR 0044). The sink should mirror both.

**Push, not pull — and the reason is in `pace()`.** The broadcast task owns `S.chunk`, the EQ
state and the pacing clock; it is the only thing that can produce PCM, and it must never block
inside `mi_rs_process`'s emit callback (`mini_icecast.c:689-703,914`). So the broadcast *pushes*
a chunk boundary into the sink, and the sink *pulls* from its own ring on its own task. A
"pull" API where the sink calls back into the broadcast would have to re-enter the resampler.

```c
/* audio_out.h — the local playback seam. Header-only in the portable half. */
typedef struct audio_out audio_out_t;

typedef struct {
    /* Open the output at the station rate. Called once, after the station resolves. */
    esp_err_t (*open)(audio_out_t *self, uint32_t rate, uint8_t channels);
    /* Non-blocking: accept up to `bytes` of interleaved int16; a short return means the
     * ring is full and the sample was NOT consumed (the caller must not drop it silently). */
    size_t    (*write)(audio_out_t *self, const int16_t *pcm, size_t bytes);
    /* For the diagnostics line: occupancy, underruns, and the worst case seen. */
    void      (*stats)(audio_out_t *self, audio_out_stats_t *out);
    esp_err_t (*close)(audio_out_t *self);
} audio_out_ops_t;

struct audio_out { const audio_out_ops_t *ops; void *impl; };

/* What the board has, as a value — the screen_caps() shape (ADR 0044). */
typedef struct { bool i2s; bool codec; bool mono_only; } audio_out_caps_t;
const audio_out_caps_t *audio_out_caps(void);
/* NULL when the board has no local output. Never fatal: the broadcast is the product. */
audio_out_t *audio_out_open(uint32_t station_hz);
```

Layout:

```
components/portable/audio_out/        the interface + the ring pump + the fade/underrun policy
                                      (compiles on the host; add audio_out.c to host/Makefile)
components/app/board_esp32/i2s_out.c  i2s_std init, DMA, GPIOs, the feeder task
host/tests/t19_audio_out.c            a fake sink that stalls on demand: the ring and fade
                                      logic is host-testable, which is the point of the split
```

and `mini_icecast_cfg_t` gains one field (`audio_out_t *sink`), consumed at the two seams
(`emit_chunk`, `flush_pipeline`). That keeps `mini_icecast` dependent only on the interface,
which is a leaf, exactly as `audio_format` is (`host/tests/layering.sh:136-160`).

### 8.2 Kconfig / `sdkconfig.defaults` surface

```ini
CONFIG_APP_AUDIO_OUT_I2S=y            # build and open the local sink
CONFIG_APP_AUDIO_BCK_GPIO=17          # devkit defaults; the Sunton build sets its own or fails
CONFIG_APP_AUDIO_WS_GPIO=18
CONFIG_APP_AUDIO_DIN_GPIO=16
CONFIG_APP_AUDIO_MCLK_GPIO=-1         # -1 = no MCLK (PCM5102A 3-wire)
CONFIG_APP_AUDIO_MUTE_GPIO=-1         # XSMT / SD_MODE
CONFIG_APP_AUDIO_SINK_MONO=n          # 16-bit Philips stereo; y = (l+r)/2
CONFIG_APP_AUDIO_SINK_RING_MS=1500    # = lead_ms: the pacer never fills more, so this is the ceiling
CONFIG_APP_AUDIO_SINK_DMA_DESC=6
CONFIG_APP_AUDIO_SINK_DMA_FRAMES=240  # 6 × 240 × 4 B = 5,760 B ≈ 32.6 ms
CONFIG_I2S_ISR_IRAM_SAFE=y            # IDF's own option; §4.5
```

`sdkconfig.defaults` needs only the `I2S_ISR_IRAM_SAFE` line plus a comment; the rest belongs in
a `Kconfig.projbuild` under `components/app/board_esp32/`, where the board facts already live.

### 8.3 Two modes, one interface

- **Stream + DAC (default).** The encoder keeps pacing; the sink's ring absorbs the rebases;
  `mini_icecast_lag_ms()` and the PCM ring's watermark go on the same telemetry line.
- **Local only.** `CONFIG_APP_AUDIO_SINK_ONLY=y` skips `mi_enc_new()` (100 KB, 41 % of a core),
  skips `pace()`'s sleep, and makes the sink's `write()` *blocking* — the DMA's own back-pressure
  becomes the clock. This is the mode that is genuinely better than streaming (bit-exact,
  ~0.1 s of latency instead of 1.5 s), and the mode that would make native-rate playback
  coherent (§3.4). It also frees the whole 41 % the encoder was taking, which is the headroom a
  24/96 native file needs.

### 8.4 ADRs this needs

1. **"The local output is a second sink, and the encoder keeps the clock."** Records §1.3/§3.5:
   the tap point, the push/pull split, and that `lead_ms` is the DAC's jitter budget.
2. **"An underrun is silence with a ramp, and it is counted."** Records §2.5: the
   `auto_clear_after_cb` decision, the fade length, and the telemetry.
3. **"Local-only mode makes the DMA the clock."** Only if that mode is built — it changes the
   pacing authority, which is exactly the kind of thing this repo writes down (cf. ADR 0041,
   "the decoded source is its own task").
4. **ADR 0044's amendment**, or a sibling: "the board says whether it has an output", so the
   Sunton build fails loudly at boot instead of silently having no DAC.

---

## 9. What is already settled (reuse, do not re-derive)

| Claim | Where it is verified | Status here |
|---|---|---|
| No DAC on the S3; every output is external I²S or PDM | `docs/research/07-esp32s3-hardware-verified.md` §"No internal DAC", three independent checks | unchanged |
| DMA buffers/descriptors must be internal RAM; source buffers may be PSRAM | `radio-player-audio-out.md:222-224` | unchanged, and it is what makes the 512 KB ring legal |
| `i2s_channel_reconfig_std_clock` is READY-only | `radio-player-audio-out.md:162`, re-verified in both local trees (`i2s_std.c:376` / `:395`) | unchanged |
| `i2s_channel_write` blocks, copies, reports partial writes, is not ISR-safe | `radio-player-audio-out.md:69-80`, re-verified (`i2s_common.c:1329-1378`) | unchanged |
| The ≤4092 B and 24-bit-multiple-of-3 DMA rules | `radio-player-audio-out.md:65,84`; docs `i2s.rst:377,1147-1173` | unchanged |
| ES8311 / ES8388 register and volume facts | `radio-player-audio-out.md:124-125` (`esp_codec_dev` + datasheet) | unchanged; the codec is not recommended here for pin reasons |
| `esp_codec_dev` swallows `bytes_written` and drops PCM during a reconfig | `radio-player-audio-out.md:139-143` | **matters more** here: it means the codec path cannot report an underrun, so the sink must own its own accounting |
| S3 crystal ±10 ppm, and the drift arithmetic | `radio-player-audio-out.md:174-181` | superseded for this device: 0 ppm division error and one crystal |
| No APLL on the S3, fractional divider only | `radio-player-audio-out.md:67` | confirmed in both trees (`SOC_I2S_CLKS`, no `SOC_MOD_CLK_APLL`) |

### Where this repo's situation changes the conclusion

| Prior research said | Here |
|---|---|
| "the rate is known only after the decoder parses the stream, and can change" — the central design problem | **Does not exist.** The station rate is resolved once and immutable (§3.1), so the I²S channel is configured once at boot and never reconfigured |
| Recommend fixing 48 kHz as the output rate to avoid a resample for most streams | **44.1 kHz is already exact** and is the station rate; the resampler is bypassed for 44.1 k sources (`mi_resample.c:63-70`) |
| Drift control needs an adaptive resampler trimmed in ppm | **Not needed.** One crystal; the ring drifts by whole chunks only |
| The EQ/volume must be implemented in the library after the resampler | **Already exists** (`tone_eq`), and it is *before* the tap, so the DAC inherits it for free |
| No speaker/PA control, no pop story | **The stream has no pop story at all**; the DAC introduces one, and the PCM5102A is the only candidate with a documented soft ramp |
| A UI would fight the audio for memory | The UI is already budgeted in PSRAM, and its panel owns one of the five GDMA channels; I²S is a third of five |
| PSRAM may be absent; degrade to internal-only ring | **PSRAM is a hard prerequisite** (`README.md:181-193`), so the 512 KB ring is free |

---

## 10. Where this would be wasted work

- **A resampling / drift-control layer.** There is nothing to drift against. Any ppm-trimmable
  resampler for the DAC is dead code (§3.3).
- **A codec abstraction over `esp_codec_dev`.** With one board, one chip and no I²C volume
  requirement, wrapping `esp_codec_dev` adds a dependency, swallows `bytes_written`, and drops
  PCM during reconfigs (`radio-player-audio-out.md:139-143`). `i2s_std` directly is less code
  and better telemetry.
- **Native-rate / bit-perfect mode *before* the sink exists.** It needs the second PCM branch,
  a per-track `tone_eq_init`, and a local-only mode; it is a product decision on top of a
  working sink, not a substitute for one.
- **A second I²S peripheral for duplex.** Nothing here captures audio; the S3 has two I²S ports
  (`SOC_I2S_NUM 2` in 5.4 — 6.1 dropped the macro, not the port) and the second one should stay
  unused.
- **MCLK wiring for a PCM5102A.** The datasheet's whole point is 3 wires; adding SCK only
  disables the internal PLL and gains nothing.
- **Anything on the Sunton's pins.** There are none to spend (§6.3). Building it there means
  first deciding what the console is for.

---

## 11. What I did not verify

1. **MAX98357A's gain-strap table and its behaviour with no MCLK.** `analog.com` timed out and
   `datasheets.maximintegrated.com` redirected into the same wall; only an evaluation-board page
   mirror was readable. The gain values (3/6/9/12/15 dB via `GAIN_SLOT`) are **UNVERIFIED**; so
   is its sample-rate ceiling and whether it needs a specific BCLK/LRCK ratio. Check the vendor
   PDF before designing it in.
2. **The exact strapping-pin list for the S3.** The datasheet has a "Strapping Pins" chapter
   with exactly four sub-sections, and GPIO0/GPIO46 are confirmed from this repo and from
   Espressif's own `esp_rom` efuse header. GPIO3 and GPIO45 are LIKELY from the same chapter;
   my PDF text extraction could not read its glyphs (the table is a Type0-encoded text stream).
   The *decision* in §6 does not depend on it — the Sunton already spends 3, 45 and 46 on panel
   data, and the devkit recommendation avoids all four.
3. **Whether the S3's I²S divider realises 6250/441 exactly on silicon.** The arithmetic, the
   register width, and IDF's search are verified; the mapping from `(N, b, a)` to the hardware's
   `(x, y, z, yn1)` pattern is derived from `i2s_ll_tx_set_mclk` and matches for this pair, but
   nobody put a frequency counter on it. Experiment: enable the channel at MCLK = 11.2896 MHz,
   measure BCLK over 60 s with a counter, and compare against 1.4112 MHz × 60 — a 0 ppm result
   looks like exactly 84,672,000 cycles.
4. **Whether stale DMA content is re-sent after `i2s_channel_disable()`/`enable()`.** Inferred
   from the source (`i2s_common.c:121-137`: re-link from `desc[0]`, `xQueueReset`, buffers not
   zeroed) and inherited from the prior research, still not seen on a scope. It only matters for
   a rate change or a stop/start, so it does not block a fixed-rate sink.
5. **The underrun-audibility claim.** "A 32.6 ms buffer looping forever is a ~30 Hz buzz" is my
   reading of what the ISR does, not a recording. It is easy to confirm on hardware: run the
   sink with `auto_clear_after_cb = false`, stop feeding it, and listen — or capture the analog
   output and look for the period.
6. **The internal-RAM figure with the sink added.** ~9.2 KB is arithmetic plus the allocator's
   behaviour; the board's telemetry line (`mini_icecast.c:1279-1282`) is where it becomes a
   measurement. Expect the largest-free-block number to move, which is the number that matters.
7. **Whether the Sunton's pin 17 is truly unconnected.** It is free *in this repo's pin lists*;
   nothing here can see the PCB. Some Sunton variants wire an on-board amplifier to a spare pin.
   Check the schematic before soldering.
8. **PCM5102A's behaviour when BCLK/WS stop mid-frame at 44.1 kHz.** The datasheet documents
   auto power-save and the soft ramp; the exact click energy is a measurement.

---

## 12. Decision table

| Decision | Recommendation | Confidence | Cost if wrong |
|---|---|---|---|
| Tap point | `emit_chunk()` **and** `flush_pipeline()`, after `tone_apply` + `measure_level`, before `mi_enc_feed` | **VERIFIED** (the only place a complete EQ'd chunk exists) | a 23 ms tail lost per track if only `emit_chunk` is tapped |
| Interface shape | push from the broadcast, ring + task in the sink; vtable in `portable/audio_out`, I²S in `app/board_esp32` | **LIKELY** (mirrors `app_board` / `screen_caps`; required by `pace()`) | blocking the resampler's emit callback |
| Pacing authority | the encoder stays the pacer while streaming; the DMA paces in local-only mode | **VERIFIED** for today's code, **LIKELY** for the split | two pacers fighting; the ring hides it until it doesn't |
| Ring size | 512 KiB PSRAM (2.97 s), never < 288 KiB; bounded in practice by `lead_ms` = 264,600 B | **VERIFIED** arithmetic | underruns on a >1 s stall |
| DMA | 6 × 240 frames, 16-bit stereo, 5,760 B internal, 32.6 ms | **VERIFIED** (`i2s.rst:1147-1173`, `i2s_common.c:477-482`) | >4092 B/buffer is clamped with a log and changes the rate |
| Underrun policy | `auto_clear_after_cb = true` + 64–128-frame fade + counter | **VERIFIED** semantics, **LIKELY** the right product policy | default = a 32.6 ms loop, which is much worse than silence |
| Slot format | Philips, 16-bit data in 16-bit slots, no MCLK | **VERIFIED** for PCM5102A (Table 11 allows 32 f_S) | 24-bit slots are not accepted by the 3-wire PLL |
| Chip | PCM5102A (stereo, 3 wires, soft mute, XSMT) on the devkit; MAX98357A only if mono is acceptable; PDM if no pins | **VERIFIED** (datasheet) for PCM5102A; **LIKELY** for MAX98357A | a click at every start/stop with no ramp |
| Board | bare S3 devkit for any I²S DAC; the Sunton with PDM, or with its console sacrificed | **VERIFIED** pin census | a board with no console |
| Task | `ice_dac`, core 1, prio 7, 3 KB internal stack | **LIKELY** (priorities are app choices) | a late refill preempted by the encoder |
| IRAM | `CONFIG_I2S_ISR_IRAM_SAFE=y` | **VERIFIED** that it is needed under PSRAM-resident code + flash writes | an underrun during every flash operation |
| Layering | portable interface + host test with a fake sink; `app/` holds the I²S | **VERIFIED** (layering rules, `host/tests/layering.sh:86-160`) | a fork of a component, which the build refuses |

### The smallest change that produces a working I²S DAC, in order

1. **One field, one ring, no interface.** Add `uint32_t sink_ring_bytes` to
   `mini_icecast_cfg_t` (0 = no local sink), `mi_ring_new(sink_ring_bytes, true)` next to the
   burst ring (`mini_icecast.c:1358`), and a `mi_ring_put()` at the two seams
   (`emit_chunk`, `flush_pipeline`). Drops are counted, not silent. No vtable, no Kconfig, no
   `#ifdef` — and nothing changes for a build that passes 0.
2. **A feeder task in the same file**, prio 7 on core 1, which pops 960 B and calls
   `i2s_channel_write`. ~40 lines. It starts only when the ring exists.
3. **`i2s_std` init at boot**, in `components/app/board_esp32/` behind one function
   (`board_audio_start(uint32_t rate)`), called from `app_services_start()` **between** the
   network (step 2) and the broadcast (step 7) — after step 6, say — so the DMA buffers are
   claimed after Wi-Fi has its contiguous 25 KB and before the encoder takes its 100 KB.
   `auto_clear_after_cb = true`, 6 × 240, Philips 16-bit, no MCLK.
4. **Pins:** 17 / 18 / 16 on the devkit. Nothing on the Sunton (see §6.3).
5. **Then, and only then, the shape:** move the ring + fade + counters into
   `portable/audio_out`, add the host test, add the capability query, and write the two ADRs.
   This order gets a sound out of the board in an afternoon and still ends in the right
   architecture — and if step 1–4 shows the ring never drops below 1 s, the architecture
   question is answered by measurement rather than by this document.

### The experiment that would settle what this document cannot

Feed the sink into a 44.1 kHz PCM5102A on the devkit, log ring occupancy minimum + underrun
count + `mini_icecast_lag_ms()` on the existing 60 s telemetry line, and run the matrix the repo
already runs (`README.md:495-530`): 1 h idle, 1 h with a listener, 10 min with repeated
downloads, and one deliberate card pull. The number to watch is the underrun count on the
download soak: this document predicts **zero** with a 1.5 s ring, and non-zero within the first
minute with a 256 KiB ring (which is 14 ms short of the lead).
