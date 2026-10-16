# xtape: a reusable ESP32-S3 internet-radio player component — architecture

This is the synthesis document. It turns the reconnaissance and research beside it into a
concrete component design, and it states plainly what is proven, what is inferred, and
what remains unmeasured.

**Companion files** (read for the evidence behind each claim):

| File | Covers |
|---|---|
| `00-audit-of-resources.md` | what `resources/` actually is, and its measured resampler |
| `radio-player-streaming.md` | transport research (ICY dialect, reconnection, TLS) |
| `radio-player-codecs.md` | codec research, decoder comparison, licensing |
| `02-packaging-and-licensing.md` | registry rules and the Espressif license constraint |
| `03-transport-verified-locally.md` | parser behaviour, **executed** |
| `04-live-stream-observations.md` | live station behaviour, **captured** |
| `05-heaac-verified-live.md` | HE-AAC mechanism, **proven on real bytes** |
| `06-who-sends-icy-status-line.md` | Icecast source read; where ICY actually comes from |
| `07-esp32s3-hardware-verified.md` | SoC facts: no DAC, core count, memory map |

## 0. The headline answers

**Is `resources/` a minimal source for a radio *stream player*? No.** It is a mini
Icecast **server** — SD card → decode → resample → HTTP clients. Every part a player
needs (HTTP/ICY *client*, streaming decode, I2S sink, buffer/clock management) is absent;
the data flows the opposite way. For a reusable library, keep the DSP middle and rebuild
the rest. (Full accounting: `00-audit-of-resources.md`.)

**Can it become a good library? Yes** — and the research removes most of the usual risk,
because the two scariest subsystems turned out to be solved problems:

- **Transport is not a risk.** `esp_http_client`'s vendored parser handles `ICY 200 OK`,
  `HTTP/1.x` with `icy-*` headers, endless streams, and chunked encoding. This was
  *executed*, not assumed (`03-transport-verified-locally.md`). No raw-socket client is
  needed.
- **The resampler already exists and is sound.** `mi_resample.c` is rate-exact with no
  accumulating drift (verified over 10⁹ frames) and costs ≈0.06 % of a host core
  (`00-audit-of-resources.md` appendix). It becomes the sample-rate seam.

The two genuine risks are **AAC licensing** and **the network↔DAC clock problem**. Both
are architectural, and both are addressed below.

## 1. Design principles

1. **The public API must not leak dependencies.** If `xtape.h` never names an
   `esp_http_client_handle_t` or a decoder struct, then those stay `PRIV_REQUIRES` and
   the library's surface and ABI stay small. This is also what makes the decoder
   pluggable (see §4).
2. **Everything that is uncertain at compile time becomes a runtime seam.** A stream's
   sample rate, channel count, and codec are *unknown until bytes arrive* — and HE-AAC
   even doubles the rate after the first frame (`05-heaac-verified-live.md`). So the
   output path and resampler must be reconfigurable at runtime, not fixed at connection
   time.
3. **Nothing may block forever, and no failure may be fatal.** A player runs for days
   against a network that will stall, drop, rate-limit, and 302. Stall detection, backoff,
   and resume belong inside the library (§5).
4. **The DSP core stays host-testable.** `resources/host_convert.c` proves the decode +
   resample path builds and runs under plain `gcc` on a laptop. Keeping that property is
   what makes the library trustworthy and fast to iterate on, since the parts that are
   hardest to debug (resampling, framing, drift) are exactly the parts that need no
   hardware.
5. **Be a good dependency, not a framework.** The registry already has
   `espressif/esp_hls_stream` for HLS and a whole GMF stack for media pipelines. xtape
   should compose with those for the 90 % case (one ICY stream → one I2S output) and not
   try to absorb them.

## 2. Component layout

The current flat `resources/` layout does not build (`CMakeLists.txt` points at
nonexistent `include/` and `third_party/`). A conventional ESP-IDF component:

```
xtape/                          # the component (directory name == component name)
├── CMakeLists.txt
├── idf_component.yml           # registry metadata + dependencies
├── Kconfig                     # tunables; also the require-a-tick-rate guard
├── LICENSE                     # required for registry upload
├── README.md
├── include/
│   └── xtape.h                 # the ONLY public header
├── src/
│   ├── xtape.c                 # public API, state machine, task orchestration
│   ├── xt_http.c/.h            # esp_http_client wrapper: ICY parse + metaint demux
│   ├── xt_ring.c/.h            # byte ring (1.5-2s prebuffer) + PCM ring; watermarks
│   ├── xt_decode.c/.h          # decoder BACKEND vtable + per-format shims
│   ├── xt_resample.c/.h        # lifted from resources/mi_resample.c
│   └── xt_out_i2s.c            # default I2S output backend
├── test/                       # host (gcc) tests — no ESP-IDF required
│   ├── test_resample.c
│   ├── test_icy_demux.c
│   └── test_decode_frames.c
└── examples/
    └── basic_player/           # main/ with Wi-Fi + xtape_start("http://...")
```

Notes that matter:

- `xtape/` should be the component root, and if it later lives in a repo with other
  components, `repository_info.path` points at it.
- `examples/basic_player` is auto-discovered by the registry; `resources/main.c` is a
  usable starting point for its Wi-Fi boilerplate (including
  `esp_wifi_set_ps(WIFI_PS_NONE)`, which the player should set by default with a comment
  explaining the latency tradeoff, since modem sleep causes exactly the stalls a radio
  player must avoid).
- The `test/` harness is the direct descendant of `resources/host_convert.c` and is why
  `src/xt_resample.c` and `src/xt_decode.c` must not include ESP-IDF headers.

## 3. Public API sketch

The API should be small, async, and callback-driven — a player is a long-running state
machine, not a request/response function.

```c
typedef enum { XT_IDLE, XT_CONNECTING, XT_BUFFERING, XT_PLAYING, XT_RETRYING, XT_ERROR } xt_state_t;

typedef struct {
    const char *url;              /* station URL: http(s) stream, or .pls/.m3u */
    const char *user_agent;       /* NULL -> a browser-like default (see §5) */
    uint32_t    prebuffer_ms;     /* start playback after this much audio (default ~2000) */
    uint32_t    stall_timeout_ms; /* no bytes for this long -> reconnect */
    xt_output_t *output;          /* NULL -> built-in I2S backend */
    xt_decoder_t *decoder;        /* NULL -> built-in default backend (§4) */
} xt_cfg_t;

/* Events, delivered from the library's own task context. */
typedef struct {
    void (*on_state)(xt_state_t st, void *ctx);
    void (*on_metadata)(const char *title, const char *url, void *ctx);  /* ICY StreamTitle/StreamUrl */
    void (*on_error)(esp_err_t err, const char *detail, void *ctx);
} xt_events_t;

esp_err_t xt_start(const xt_cfg_t *cfg, const xt_events_t *ev);
esp_err_t xt_stop(void);
esp_err_t xt_set_volume(uint8_t percent);   /* digital, or forwarded to the output backend */
esp_err_t xt_get_status(xt_status_t *out);  /* state, bitrate, sample rate, buffer fill */
```

Design decisions worth defending:

- **`on_metadata` carries both title and URL.** The live capture showed real stations send
  `StreamUrl` next to `StreamTitle` (Radio Paradise ships a cover-art URL), so the event
  exposes both. It costs nothing and is a ready-made album-art feature
  (`04-live-stream-observations.md`).
- **Events run in the library's task**, consistent with the `pcm_cb` contract already
  documented in `resources/mini_icecast.h:10-13`. That comment is a good precedent to
  keep: state the threading contract in the header, and tell users to copy data out.
- **`xt_set_volume` is not a codec-specific call.** The default I2S backend can scale
  digitally; a codec-backed output can use a hardware register. Same signature either way.
- **`url` accepts a playlist page**, because real stations hand out `.pls` files listing
  mirrors (`04-live-stream-observations.md` §5). Resolving that inside the library is
  worth it — it is the difference between "works with the URL the station publishes" and
  "user must dig out the real stream URL".

## 4. The decoder backend seam (and how it solves the licensing problem)

The decoder must be a **vtable**, not a hard dependency, because the licensing research
found a two-layer trap (`radio-player-codecs.md` §4.2,
`02-packaging-and-licensing.md`):

- **Copyright layer:** the good AAC decoders are unusable in a redistributable library —
  FAAD2 is GPLv2 with an explicit "any non-GPL usage … strictly forbidden", and
  ESP32-audioI2S (which vendors it) is GPLv3. Espressif's `esp_audio_codec` is
  "Espressif **Modified** MIT": free to *depend on* for an ESP32-S3 target, but
  redistribution for non-Espressif products is prohibited, so it must not be *vendored*.
- **Patent layer:** MP3 is royalty-free (last core patent expired 2017-04-23), but AAC is
  patent-encumbered regardless of implementation — fdk-aac's own NOTICE grants no patent
  license. This is a property of shipping an AAC decoder, not of choosing a bad one.

The resolution is to make the backend pluggable and ship the permissive default:

| Role | Backend | License | Why |
|---|---|---|---|
| **Default (in-tree)** | `dr_mp3` low-level push API | public domain / MIT-0 | MP3 is royalty-free and this keeps the core redistributable |
| **Optional AAC** | `espressif/esp_audio_codec` as an external registry dependency | Espressif Modified MIT | HE-AAC for the low-bitrate tiers; consumer opts in, nothing is vendored |
| **Later** | libopus/opusfile | BSD-3 | royalty-free, for Opus stations |

This is not merely legal hygiene; it is also the right engineering shape. The decoder
interface a player needs is narrow, and pinning it down is what makes `esp_audio_codec`
swappable:

```c
typedef struct {
    const char *name;
    /* Push arbitrary bytes; return how many were consumed.
     * `out_rate`/`out_ch` are only valid once a frame has been decoded, and
     * MAY CHANGE after the first frames (HE-AAC SBR doubles the rate). */
    esp_err_t (*feed)(void *self, const uint8_t *in, size_t in_len, size_t *consumed,
                      int16_t *pcm, size_t pcm_cap_frames,
                      uint32_t *out_rate, uint8_t *out_ch, size_t *pcm_frames);
    esp_err_t (*reset)(void *self);
    void      (*close)(void *self);
} xt_decoder_vt_t;
```

Three requirements come straight from the verified evidence, not from taste:

1. **Push-shaped, frame-agnostic, no seek.** A live stream has no file and no end, and
   ADTS frames do not align with ICY metadata boundaries — the frame walk overran the
   `metaint` boundary by 344 bytes on a real stream (`05-heaac-verified-live.md` §4). The
   decoder must accept a byte stream with metadata already excised.
2. **The output rate is reported *after* decoding and may change.** Real SomaFM HE-AAC
   advertises `profile = LC` in its ADTS header while being 22050 Hz core → 44100 Hz
   output. A decoder that reports the header's rate is wrong by an octave.
3. **Channel count is discovered, never assumed.** HE-AAC v2's Parametric Stereo signals a
   mono core.

Keeping `dr_mp3`'s *low-level* API is deliberate: the high-level `drmp3` API wants
read/seek/tell callbacks and is file-oriented, whereas `drmp3dec_decode_frame` consumes
pushed bytes (`radio-player-codecs.md` §4.1).

## 5. Transport design

`esp_http_client` is the transport. This is the single largest simplification the research
produced, and it is backed by executed tests rather than documentation reading.

- Open with `esp_http_client_open()` → `esp_http_client_fetch_headers()` → loop
  `esp_http_client_read()`. The `perform()`/`read_response()` helpers buffer to completion
  and are therefore **unusable** for an endless stream. `esp_http_client_read()` also
  returns `-ESP_ERR_HTTP_EAGAIN` when the transport read times out, which is a usable
  liveness signal.
- Request ICY metadata with `esp_http_client_set_header(c, "Icy-MetaData", "1")`.
- **Copy response headers inside the callback — they do not survive it.** Two separate
  traps, both verified in source:
  1. `esp_http_client_get_header()` reads `client->request->headers`, i.e. only the
     headers *you sent* (`esp_http_client.c:370`).
  2. The `HTTP_EVENT_ON_HEADER` key/value strings are `free()`d the moment the handler
     returns (`esp_http_client.c:242-256`).
  So `icy-metaint`, `icy-name`, and `icy-br` must be **copied out** during the event, not
  looked up later. Match keys **case-insensitively** (verified wire case varies:
  `icy-name` lowercase, `Content-Type` capitalised).
- **`is_chunked` does not mean "chunked".** `fetch_headers()` does
  `if (content_length <= 0) { is_chunked = true; return 0; }`
  (`esp_http_client.c:1581-1584`), and `esp_http_client_is_chunked_response()` just returns
  that flag. So the flag conflates "Transfer-Encoding: chunked" with "no Content-Length" —
  which is *every* endless radio stream. Do not use it to decide whether to de-chunk;
  drive that off the actual `Transfer-Encoding` header captured in the event callback, and
  note that live streams stack **both** (verified: Zeno sends `transfer-encoding: chunked`
  *and* `icy-metaint: 16000`), so de-chunking must happen *before* counting `metaint` bytes.
- Beware the header-name/space variants: real Icecast writes `icy-name:value` with **no
  space** after the colon, while `resources/mini_icecast.c:225` writes `"icy-name: %s"` with
  one. A parser that requires the space passes the local fixture and fails on the internet.
- Treat "no `Content-Length`" as the normal endless-stream case (`content_length` arrives as
  `ULLONG_MAX` from the parser).
- Demux ICY metadata per the validated algorithm in `03-transport-verified-locally.md` §3:
  read the length byte, consume exactly `16*L` bytes, scan for `StreamTitle='`/`StreamUrl='`,
  terminate at the closing quote or region end, and return "need more" when a read splits a
  block. Handle `L == 0`.
- **Document the apostrophe truncation.** A title containing `'` truncates at the first
  quote (`It's a station` → `It`), because ICY has no escaping. Every player does this;
  silently returning a wrong title is the bug, not the truncation.
- **Length byte 0 means "keep the previous title", not "title is empty".** Icecast writes a
  single `"\0"` when the title is unchanged, and the existing `mini_icecast.c:77-80` server
  does the same. The player must retain the last non-empty title rather than clearing it.
- **Titles are ISO-8859-1 by default** (Icecast's default charset, UTF-8 converted on the
  way out), so the parser must tolerate non-UTF-8 bytes instead of assuming UTF-8.
- A metadata block can legitimately arrive **before any audio** (Icecast sends an initial
  17-byte block), so the demuxer must not assume it has seen audio first.

### Robustness (non-negotiable for a device that runs for days)

- **Set a browser-like `User-Agent` by default.** This is a measured integration hazard:
  the same SomaFM URL returned **302** with `curl/8` and **200** with a Chrome-like UA
  (`04-live-stream-observations.md` §4). The official client's default identity gets
  fingerprinted away from major stations.
- **A stalled or closed stream is the normal end of an Icecast connection, not an error.**
  Icecast sends `Connection: Close`, so FIN, `ESP_ERR_HTTP_CONNECTION_CLOSED`,
  `ESP_ERR_HTTP_READ_TIMEOUT`, and an `EAGAIN` that arrives after audio has started must all
  map to **reconnect** — never to a fatal state. This is the single most important rule for
  a player that is expected to run for days, because otherwise every station appears to
  "fail" every few minutes.
- **A byte-arrival watchdog**, not just socket timeouts: a stream can stall without
  closing. Reconnect with backoff after `stall_timeout_ms` of silence.
- **Treat 403/302 as retryable.** Repeated requests from one IP drew 403s after initial
  successes, i.e. transient rate-limiting is normal and must not be a fatal error state.
- **Playlist resolution** for `.pls`/`.m3u` (`.pls` is the one that mattered in practice;
  the `.m3u` sibling 404'd). Live playlists are sloppy: SomaFM serves lowercase `[playlist]`
  keys with three `FileN=` mirrors and `LengthN=-1`. Pick a mirror and be ready for `https`
  even when the playlist came over `http`, which means TLS is on the critical path. Do not
  cache a signed redirect target (Zeno hands out a JWT-signed `Location`) — re-walk the
  redirect on every reconnect.
- **Note the HTTPS→HTTP redirect downgrade**: `esp_http_client` refuses it by default
  (`ESP_ERR_HTTP_REDIRECT_DOWNGRADE`). Since station playlists often point at `http`
  mirrors, the library should resolve playlist entries itself rather than rely on the
  redirect follower.
- TLS cost is **unmeasured here** and competes with the decoder for SRAM. What is known is
  the cheap, exact knob: `MBEDTLS_ASYMMETRIC_CONTENT_LEN=y` with
  `SSL_IN_CONTENT_LEN=16384` / `SSL_OUT_CONTENT_LEN=4096` (≈20 KB/connection, ~12 KB less
  than symmetric). This needs a hardware measurement before the library publishes memory
  numbers.

## 6. Audio output and the clock problem

### The output hardware

The ESP32-S3 has **no internal DAC** (`07-esp32s3-hardware-verified.md`), so I2S to an
external amp or codec is the only option. Verified from the local IDF v5.4.2 headers:

- `driver/i2s_std.h`: `i2s_channel_init_std_mode()`,
  `i2s_channel_reconfig_std_clock()`, `i2s_channel_reconfig_std_slot()`, and the
  `i2s_std_config_t` / `i2s_std_clk_config_t` / `i2s_std_slot_config_t` /
  `i2s_std_gpio_config_t` structs.
- `driver/i2s_common.h`: `i2s_new_channel()`, `i2s_channel_enable()`,
  `i2s_channel_write()`, `i2s_channel_preload_data()`, plus
  `i2s_chan_config_t { dma_desc_num, dma_frame_num, ... }`.
- The same API exists in v5.5.5 and v6.1, so no `#if` gymnastics across the supported range.

Two useful details from the real headers:

- `dma_desc_num × dma_frame_num` sets the DMA depth, and every extra slot is latency.
  At 44.1 kHz stereo/16-bit, `6 × 1024` frames ≈ 23 ms of hardware buffer (24 KB) — enough
  to ride out scheduling jitter without adding audible delay.
- `i2s_channel_preload_data()` fills the DMA before `i2s_channel_enable()`, which removes
  the start-of-stream underrun click. Worth using.
- `i2s_channel_write(..., timeout_ms)` blocks until the DMA accepts the data, so it is the
  natural throttle for the audio task.

### Codec control: use `esp_codec_dev`, but know its rate limitation

`espressif/esp_codec_dev` (v1.6.2, verified from the installed component) abstracts
I2S + I2C codec control behind `esp_codec_dev_open/write/read/close/set_out_vol`. Its
supported chips, read from the package's `device/` directory, are:
`aw88298, cjc8910, es7210, es7243, es7243e, es8156, es8311, es8374, es8388, es8389,
tas5805m, zl38063`. That covers the common ESP32-S3 dev boards (ES8311 especially), and
it is the right integration layer so the library is not board-specific.

**But it cannot change sample rate in place.** Verified in `esp_codec_dev.c:149-215`:

```c
if (dev->input_opened || dev->output_opened) {
    ESP_LOGI(TAG, "Input already open");
    return ESP_CODEC_DEV_OK;          /* <-- new fs silently ignored */
}
```

A second `esp_codec_dev_open()` call with a different `esp_codec_dev_sample_info_t`
(`{bits_per_sample, channel, channel_mask, sample_rate, mclk_multiple}`) returns success
**without reconfiguring anything**. Changing the rate requires `esp_codec_dev_close()`
then `open()` again — which stops the DAC clock and risks an audible gap.

### The architecture decision: fix the output rate, resample into it

Because a rate change is expensive (and because HE-AAC only reveals its true output rate
after the first decoded frame), **the output rate must be fixed** and the stream resampled
into it. That is exactly the seam `mi_resample.c` already provides, and it removes any
need for `i2s_channel_reconfig_std_clock()` or a codec close/reopen in the steady state.

Consequences, in order of importance:

1. **The I2S clock is opened once, at `out_hz`** — default **48 000 Hz**, configurable, since
   48 kHz is the friendlier rate for the common codec MCLK ratios and avoids resampling the
   many 48 kHz streams at all — and never touched again during playback.
2. **The resampler is created lazily, after the first decoded frame**, once the *actual*
   stream rate and channel count are known — and re-created if they change mid-stream
   (adverts/station IDs occasionally differ). `mi_rs_new(in_hz, out_hz)` with
   `in_hz == out_hz` degenerates to a pass-through, so at a 48 kHz output a 48 kHz stream
   costs nothing. (If `out_hz` is instead configured to 44.1 kHz — the better choice on a
   board whose only clock is 44.1 kHz-friendly — the resampler handles it, per
   `00-audit-of-resources.md`.)
3. **Mono streams are duplicated to stereo** before the resampler, keeping one internal
   format everywhere (stereo int16), matching what `mi_decode.c:102-105` already does.
4. **A stated, documented limitation:** streams at an unsupported *input* rate are still
   handled, because the resampler absorbs them. There is no "unsupported sample rate"
   failure mode for PCM rates — which is a genuine robustness win over per-stream
   clock reconfiguration.
5. **Per-stream clock reconfiguration remains available as a fallback**, not the default.
   `i2s_channel_reconfig_std_clock()` requires the channel to be **disabled first**, which
   stops BCLK/WS and resets the DMA/FIFO and message queue — so it is only viable at a track
   or station boundary, never mid-song, and (unverified) may re-send stale DMA content.

### The clock problem, quantified — and why it is *not* urgent

The network delivers at the stream's *nominal* rate while the DAC consumes at its crystal's
*actual* rate. For the ESP32-S3-WROOM-1 the XTAL is **±10 ppm**; even a pessimistic ±100 ppm
is small. The arithmetic that matters:

- At 44.1 kHz, 20 ppm is **0.882 samples/s**. Draining a 2-second buffer (88 200 frames)
  therefore takes **~28 hours**; at 100 ppm, ~5.5 hours.
- So with a 2-second buffer, **no drift correction is needed for hours at a time.** An
  earlier draft of this section implied corrections every ~25 s by confusing "time to shift
  occupancy by one frame" with "time to drain the buffer". The buffer is the drift budget,
  and it is large.

The practical rule:

1. **Measure buffer occupancy; correct only on a wide threshold** (e.g. ±500 ms around the
   1.5 s target in a 2 s buffer). At 20 ppm that fires roughly **once every 7 hours**, and
   correcting by one duplicated/dropped stereo frame (22.7 µs) is inaudible.
2. **The resampler *can* be trimmed if wanted, cheaply.** Its fixed-point step is private
   and set once, but the resolution is excellent: **±1 LSB of the 32.32 step = 0.25 ppm** at
   44.1→48 kHz, so a ±400 LSB trim is ±100 ppm and costs no extra CPU (just an adjusted
   accumulator increment). Adding a `mi_rs_set_ratio_trim()` is a small, well-contained
   change — but it should be a *slow* correction loop, not a per-buffer control loop. Do
   **not** feed it jitter: the resampler's own conversion is exact (constant −9-frame
   startup transient, zero accumulation over 10⁹ frames, per `00-audit-of-resources.md`).
   Keep rate conversion and drift absorption as separate concerns.
3. **The real risk is the network, not the crystal.** If the network delivers faster than
   realtime, latency grows without bound; the high-watermark trim is a **correctness**
   requirement. Drift correction is a rare housekeeping task. A design that gets these two
   priorities backwards will glitch on jitter while over-engineering for drift.

### Three defects to fix when lifting `mi_resample.c` (all verified)

The kernel is sound and stays, but it is not shippable as-is:

1. **Stack overflow via unbounded tap count.** `mi_resample.c:47` declares
   `double row[256]` and indexes it by `k < T`, where
   `T = 2*ceil(ZERO_X/fc)` and `fc = 0.95*min(1, out/in)`. Measured values:
   `44100→48000` gives T=18, `48000→44100` gives T=20, `48000→8000` gives T=102
   (all fine), but **`48000→3000` gives T=270 and `44100→1000` gives T=744 — both overflow
   `row[256]`**. So any downsampling below a ratio of ~0.066 corrupts the stack. Note the
   *player's* normal direction is upsampling (a low-rate stream to a fixed 48 kHz output),
   so the common paths are safe — but an 8 kHz talk stream into a 48 kHz output uses T=102,
   and a user-configured low output rate or an unusual stream can reach the overflow. Fix
   by sizing `row` to `T` (allocate, or compute the bound and assert it).
2. **Uncapped allocations and no reset.** `mi_rs_new` uses plain `malloc`/`calloc` with no
   heap caps, so the tables cannot be steered to PSRAM or kept in internal RAM by intent.
   Table size is `(NPH+1)*T*4 B` — 9.3 KB at T=18 but **52.6 KB at T=102** — plus ~8–10 KB
   of history. And there is no in-place reset, so a mid-stream rate change requires
   `free` + `new`. Add heap-capped allocation and a reset/reconfigure entry point.
3. **`emit()` flushes a partial batch at the end of every call** (`mi_resample.c:103`), so
   output block sizes vary from call to call. For a DMA-fed DAC that means variable write
   sizes and needless buffer churn. Separate "emit what is ready" from "flush the tail" so
   the caller can control flush points.

HE-AAC's 2× case is handled cleanly by the existing code (it is a pure 2× upsample, T=18),
which is a pleasant result: the codec that looked like the biggest risk is the easy case for
the resampler.

### Pipeline and buffer sizing

```
Wi-Fi task ──► xt_http (ICY demux) ──► decode ring ──► decoder ──► resampler ──► PCM ring ──► I2S task ──► DMA
    core 0                                     (byte stream)              (stereo int16)      core 1
```

- Two rings, not one: a **compressed-byte ring** between HTTP and the decoder, and a
  **PCM ring** between resampler and I2S sized in *time*. Conflating them makes both sizing
  decisions harder. The audio research recommends **1.5–2 s of audio in the byte ring**
  (which is what actually rides out a Wi-Fi hiccup — the compressed form is ~10× smaller
  than PCM, so the same resilience costs ~20 KB instead of ~350 KB) plus a shorter PCM ring
  feeding the DMA. Sizing the *byte* ring generously is the cheap win.
- PCM ring arithmetic, which is the number that matters:
  `bytes = seconds × rate × 4` (stereo int16). So **2 s at 44.1 kHz = 352 KB**, and 1 s =
  176 KB. That is larger than internal SRAM comfortably allows, which is precisely why the
  PCM ring belongs in **PSRAM when present**, with an internal-RAM fallback at a reduced
  depth (e.g. 500 ms = 88 KB) when it is not.
- `resources/mini_icecast.c:398-399` shows the allocation idiom
  (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM) → malloc` fallback), but FreeRTOS offers a
  purpose-built ring that is preferable for the byte ring:
  **`xRingbufferCreateWithCaps(size, RINGBUF_TYPE_BYTEBUF, MALLOC_CAP_SPIRAM)`**, which
  gives produce/consume blocking semantics across tasks without a hand-rolled lock. Prefer
  it for the compressed-byte ring. (For PCM the record framing matters less than
  throughput, so either works; the existing `ring_put`/`ring_peek` code in
  `mini_icecast.c:111-159` is a working reference if a hand-rolled ring is chosen.)
- Keep **decoder state and I2S DMA in internal SRAM**; a decoder touching PSRAM during a
  flash-cache stall is the classic failure mode. Note the useful detail that
  `i2s_channel_write()` **copies into internal DMA buffers**, so the *source* buffer may be
  in PSRAM without penalty — it is the decoder's working set, not the PCM ring, that should
  stay internal.
- DMA depth: IDF defaults work out to roughly 30 ms at 48 kHz, with a practical maximum
  around 170 ms. Target **20–50 ms** — enough to survive scheduling jitter, short enough
  that stop/pause feels immediate.
- Core assignment: Wi-Fi/lwIP on core 0, the audio pipeline on core 1, with the I2S write
  task the highest priority and the network task lower. This is a small refinement of
  `mini_icecast.c:406-407`, which pins both of its tasks to core 1; for a player, giving the
  DAC feeder its own un-preempted path is what prevents dropouts. (Wi-Fi's own task runs at
  priority 23 and is pinned to core 0 by IDF — do not fight it.)
- Stream 8–16 KB per `i2s_channel_write()` call (≈46–93 ms at 44.1 kHz) — large enough to
  amortise per-call overhead, small enough to stay responsive. `i2s_channel_write()` blocks
  on a DMA-free message with `timeout_ms`, returns `ESP_ERR_TIMEOUT` with a partial write
  (check `bytes_written`), and a second concurrent writer gets `ESP_ERR_INVALID_STATE` — so
  one dedicated out-task owns the channel.

### Volume

`esp_codec_dev_set_out_vol()`/`get_out_vol()`/`set_out_mute()` exist and route to hardware
where the codec supports it; note that `esp_codec_dev_open()` will allocate a **software
volume** handler (`audio_codec_new_sw_vol()`) when the codec has no `set_vol`. So the
library gets a uniform volume API either way, and `xt_set_volume()` inside the library must
prefer the hardware path when present — software scaling costs SNR (you are throwing away
bits), whereas the codec's own register does not.

### Board abstraction seam

The library should define its *own* output vtable rather than exposing I2S or
`esp_codec_dev` types in the public header, for the same reason as the decoder vtable:

```c
typedef struct {
    esp_err_t (*open)(void *self, uint32_t rate_hz, uint8_t channels);   /* fixed rate! */
    esp_err_t (*write)(void *self, const int16_t *stereo, size_t frames, uint32_t timeout_ms);
    esp_err_t (*set_volume)(void *self, uint8_t percent);
    esp_err_t (*close)(void *self);
} xt_output_vt_t;
```

The built-in backend wraps `esp_codec_dev` (or raw `i2s_std` for a bare MAX98357A/PCM5102A
with no control interface — those need I2S only). The seam also makes the whole pipeline
**testable on a host** by writing PCM to a file or a null sink, which is what keeps the DSP
and framing tests runnable without hardware.

## 7. Packaging

- `idf_component_register(SRCS ... INCLUDE_DIRS "include" PRIV_REQUIRES ...)` — the
  decoder, HTTP client, and I2S driver are **private** requirements as long as §3's API
  holds. That is what keeps the public surface stable.
- `idf_component.yml` with `version`, `description`, `license` (SPDX or a `LICENSE` file —
  either is required, and omitting both is how a component silently becomes `Custom`,
  per `02-packaging-and-licensing.md`), `targets: [esp32s3]`, and `dependencies.idf: ">=5.4"`
  (justified: the ICY parser path and the I2S standard-mode API are present and identical
  across the v5.4.2 / v5.5.5 / v6.1 trees checked).
- A `LICENSES/` directory enumerating third-party terms, because `dr_mp3` (and any opt-in
  codec) carries attribution obligations.
- The name `xtape` is **unclaimed** on the registry; the publishing namespace is the
  user's call.

## 8. Build order (each step independently testable)

1. **Lift and harden the DSP core.** `xt_resample.c` from `mi_resample.c`, plus host tests
   that assert the properties already measured (unity passband, anti-alias attenuation,
   rate exactness over 10⁸ frames, bypass bit-exactness). No hardware needed.
2. **ICY demux as a pure function**, with a host test built from the real framing edge
   cases in `03-transport-verified-locally.md` and the captured Radio Paradise byte layout.
3. **Decoder backend + `dr_mp3` push backend**, host-tested against a synthetic ADTS/MP3
   byte stream.
4. **Transport**, wired to `esp_http_client`, tested against `resources/mini_icecast.c`
   running as a loopback station — the server is already a conformant ICY emitter with a
   known `metaint`, which makes it an excellent fixture (per `radio-player-streaming.md` §0).
5. **I2S output backend** and the clock/buffer strategy from §6.
6. **Reconnect/state machine**, then soak-test for days.

Steps 1–4 need no ESP32 at all, which is why the library should be structured so the DSP
and framing never include ESP-IDF headers.

## 9. What is proven vs. assumed

**Proven by execution or primary source in this research:**

- `esp_http_client`'s parser accepts `ICY 200 OK` and HTTP forms, surfaces `icy-*` headers,
  reports `status_code=200`, and signals endless streams as `content_length = ULLONG_MAX`.
- `esp_http_client_get_header()` reads request headers, not response headers, and
  `HTTP_EVENT_ON_HEADER` values are freed when the handler returns — so headers must be
  copied in the callback.
- `is_chunked` is set when `content_length <= 0` (`esp_http_client.c:1581-1584`), so it means
  "length unknown" as much as "chunked" — it cannot be used to decide whether to de-chunk.
- ICY framing on a live stream: `metaint=16000`, length byte 7 → 112 bytes,
  `StreamTitle` + `StreamUrl`, next interval at `2*metaint + 16*L`. A second capture
  (NPO Radio 1, `metaint=16000`) showed a 3-block/48-byte region with the next MP3 sync at
  16049, and Icecast's writer uses `len_byte = (len-1)/16 + 1` with a single `"\0"` sent
  when the title is unchanged.
- A real station's "aacp" stream is ADTS / MPEG-2 / profile **LC** / 22050 Hz / stereo,
  walking 122 frames self-consistently — SBR is invisible in headers.
- `mi_resample.c`: unity passband, −14.7 dB at 23 kHz, rate-exact with a constant −9-frame
  startup transient (not cumulative) over 10⁹ frames, ≈0.06 % of a host core; **and** a
  `double row[256]` that overflows for `out/in < 0.066` (T=270 at 48k→3k, T=744 at 44.1k→1k).
- ESP32-S3 has **no internal DAC** (three independent confirmations); 2 cores; FPU; 2× I2S;
  optional PSRAM. `SOC_DRAM_LOW=0x3FC88000`, `SOC_DRAM_HIGH=0x3FD00000`.
- `esp_codec_dev` v1.6.2 ships 12 codec drivers (`es8311`, `es8388`, `es7210`, …) but
  `esp_codec_dev_open()` silently ignores the sample info if already open
  (`esp_codec_dev.c:155-158`), so a rate change requires close→reopen.
- Modern Icecast never sends `ICY 200 OK` to listeners (it rejects any request protocol that
  is not `ICE`/`HTTP`, and normalises shoutcast sources to `SOURCE ... HTTP/1.0`); that path
  exists for legacy SHOUTcast DNAS servers.
- Espressif's audio components are `Custom`/Espressif-Modified-MIT, not Apache-2.0: usable as
  an ESP32 dependency, prohibited to redistribute for non-Espressif products.

**Assumed / unmeasured — do not present as fact:**

- Free heap on a real S3 after Wi-Fi + lwIP + I2S. No board was available, and this is the
  number the memory budget actually needs.
- Resampler CPU on real Xtensa hardware (host number only, scaled by inference).
- TLS/mbedtls SRAM cost for `https` streams (the configuration knob is known; the cost is not).
- Whether any station currently sends `ICY 200 OK`.
- Whether re-enabling an I2S channel re-transmits stale DMA content (inferred from IDF source,
  not measured) — worth a scope check before relying on rate reconfiguration.
- The `NPH=128` + linear-interpolation error floor (~−80 dB is an estimate, not a measurement).
- Any codec distribution percentage across the whole internet (the Icecast public
  directory is empty, so no census exists); the SomaFM numbers are one station, chosen
  because it is machine-readable.
- HE-AAC/AAC-Plus heap and CPU on the S3 from a primary measurement (Espressif says
  "higher" than AAC-LC without figures).
- Vendor datasheets for ES8311/ES7210, MAX98357A/B, UDA1334ATS, and PCM3060's MCLK table were
  **not** reachable; the ES8388 and PCM5102A datasheets *were* verified (per
  `radio-player-audio-out.md`).
