# S2 — Can an ESP32-S3 encode audio in real time? (encoder feasibility)

**Question:** the radio server (`resources/mini_icecast.c`) decodes MP3/FLAC/WAV off SD,
resamples to one station format, and streams **raw PCM** in a WAV body — 176 KB/s
(~1.41 Mbit/s) *per client* (`mini_icecast.c:11`). If it encoded instead, which encoder is
real-time-feasible on an ESP32-S3, and what does that buy?

**Method / date:** checked against encoder source trees, the encoders' own READMEs and
benchmarks, Espressif's ESP-IDF v5.4.2 tree installed at `~/.espressif/v5.4.2/esp-idf`,
Espressif's component READMEs, and the relevant specs, on 2026-10-16. SoC facts (no DAC,
2 cores, FPU present, 2× I2S, memory map) are **not** repeated here — see
[07-esp32s3-hardware-verified.md](../07-esp32s3-hardware-verified.md).

**Headline:** yes, an S3 can encode in real time — and there are **hard, first-party
measurements** for three encoders (fixed-point MP3 via shine, FLAC via `micro-flac`, and
Opus/AAC via Espressif's own `esp_audio_codec`). But at this server's client cap, the
bandwidth saving buys nothing: PCM at 2–4 clients is 2.8–5.6 Mbit/s against Espressif's own
20 Mbit/s lab TCP-TX figure for the S3. Recommendation is in [§9](#9-recommendation).

---

## 1. The CPU budget, precisely

Station format: 44 100 frames/s, stereo, 16-bit.

| Quantity | Arithmetic | Value |
|---|---|---|
| Frames to encode per second | given | 44 100 frames/s (= 88 200 samples/s) |
| Cycles available on one core | 240 MHz | 240 000 000 cycles/s |
| **Cycle budget per audio frame** | 240 000 000 ÷ 44 100 | **≈ 5 442 cycles/frame** |
| Real-time condition for the encoder alone | — | must use ≤ 100 % of one core |
| "x× realtime" ⇒ core load | — | 1/x of one core ⇒ 240/x MHz-equivalent |

Worked examples (all from the measurements in §3–§5):

- **shine MP3**, measured ~1.8× realtime → **~55 % of one core**, i.e. ≈ 133 MHz-equivalent.
- **Opus** (Espressif), 24.9 % CPU → 4.0× realtime → ≈ 60 MHz-equivalent.
- **AAC-LC** (Espressif), 12.9 % CPU → 7.8× realtime → ≈ 31 MHz-equivalent.
- **FLAC** (`micro-flac`) defaults, 21.3× realtime → **4.7 %** ≈ 11 MHz; worst measured
  setting (LPC-8 + stereo search + Rice partitions) 5.9× → 16.9 % ≈ 41 MHz.

The two cores are already assigned: `mini_icecast_start()` pins `ice_radio` (decode +
resample + pace + broadcast) to **core 1**, with the comment "Wi-Fi lives on core 0; keep the
radio on core 1" (`mini_icecast.c:405-407`). So the audio budget is *not* 240 MHz total —
it is one core, and that core also performs the `send()` calls for every client
(`broadcast()` runs in `radio_task`). A second core exists but does not buy an encoder a
free 240 MHz; Wi-Fi ISR/`lwIP` work and the radio's own socket writes are real. Two
consequences for budgeting:

1. Encode cost is **additive** to the existing decode + resample cost on core 1 (§6).
2. Any encoder below ~2× realtime has almost no headroom for SD stalls, cache misses, or the
   burst-on-connect copy — and the pacing loop `pace()` (`mini_icecast.c:291-302`) will
   simply fall behind, which listeners hear as a stutter. `lead_ms` (default 1500 ms) is the
   only shock absorber.

---

## 2. Comparison table

"Typical bitrate" bandwidth is payload bytes/s (and Mbit/s) per client; PCM baseline is
176 400 B/s = 1.411 Mbit/s.

| Encoder | Format | License | Fixed/float | Real-time on S3? | Evidence | Bandwidth @ typical bitrate |
|---|---|---|---|---|---|---|
| **shine** (ESP32 port, `mp3_shine_esp32`) | MP3 (raw frames, ICY-capable) | LGPL-2 (GNU Library GPL v2) | Fixed-point integer DSP; `double` only in the trivial energy model | **YES**, ~1.8–1.9× realtime ≈ 53–56 % of one core | [port README: "ESP32-S3: Single core takes 53.4 % of real time"](https://gitflic.ru/project/anton-malakhov/mp3_shine_esp32) (48 kHz/16-bit/2ch, 256 kbps); [independent test on S3: 78 872–79 000 frames/s @ 44.1 kHz stereo](https://www.pschatzmann.ch/home/2026/06/20/mp3-encoing-for-microcontrollers/) | 128 kbps → 16 000 B/s, **0.128 Mbit/s** (11.0× less) |
| **LAME** (libmp3lame) | MP3 | LGPL-2 | Float/double-heavy (`psymodel`, FFT, quantization) | **NO — non-starter** | [ESP32 port benchmark: 16 kHz stereo 10 s → 1.38× realtime](https://github.com/commarmi76/libmp3lame_for_esp32) — i.e. it cannot even do 16 kHz with margin, let alone 44.1 kHz | n/a |
| **libopus via Espressif `esp_audio_codec`** | Opus (needs Ogg to be playable) | [Espressif Modified MIT](https://raw.githubusercontent.com/espressif/esp-adf-libs/master/esp_audio_codec/LICENSE) (restricted to Espressif products); upstream [BSD-3](https://github.com/xiph/opus/blob/main/COPYING) + royalty-free patents | Espressif build (not fixed-point-only) | **YES at complexity 0**: 24.9 % CPU @ 48 kHz stereo, 90 kbps | [esp_audio_codec README perf table, ESP32-S3R8](https://github.com/espressif/esp-adf-libs/blob/master/esp_audio_codec/README.md) (`OPUS | 48000 | 2 | 29.4 KB | 24.9 %`, "bitrate 90 kbps, complexity 0") | 64–90 kbps → 8 000–11 250 B/s, **0.064–0.09 Mbit/s** (16–22× less) |
| generic libopus port (`esp-libopus`) | Opus | BSD-3 | fixed-point build; heavy `alloca` | **Marginal/NO for music**: 48 kHz only at complexity 1 → 85 % CPU; 24 kHz above complexity 4 not realtime; 16 kHz c1 → 70 % | [esp-libopus README](https://github.com/XasWorks/esp-libopus) | as above |
| **`micro-flac`** | FLAC (lossless) | [Apache-2.0](https://github.com/esphome-libs/micro-flac/blob/main/LICENSE) | **Integer arithmetic only** ("All integer arithmetic, so output is identical on every platform") | **YES, comfortably**: 21.3× realtime defaults; 5.9× worst tuned | [micro-flac encoding benchmark, ESP32-S3 @ 240 MHz](https://github.com/esphome-libs/micro-flac/blob/main/examples/encode_benchmark/README.md) | measured **0.879 B/sample = 44.0 % of PCM** → 77 500 B/s, **0.62 Mbit/s** (2.3× less) |
| **libFLAC** (reference) | FLAC | BSD-3 (Xiph) | Encoder analysis uses `double` (22 hits in `lpc.c`); **no fixed-point build option** | UNVERIFIED — plausibly real-time but heavier than `micro-flac`; never measured on ESP32 here | [`configure.ac`/`CMakeLists.txt` contain no `fixed` option; `src/libFLAC/lpc.c` uses `double`](https://github.com/xiph/flac) | ~same ratio |
| **libvorbis** | Vorbis | BSD-3 | **Float only** (402 `float` references in `lib/*.c`; no `FIXED_POINT` support at all) | **NO** | [xiph/vorbis source](https://github.com/xiph/vorbis) | n/a |
| **tremor** | Vorbis | BSD-3 | Integer | **Decoder only — not an encoder** | [tremor README: "No floating point operations are used at any point in either setup or decode. This decoder library will properly decode any past, current or future Vorbis I file or stream."](https://gitlab.xiph.org/xiph/tremor/-/raw/master/README) | n/a |
| **AAC-LC via `esp_audio_codec`** | AAC | Espressif Modified MIT (patents: see [02-packaging-and-licensing.md](../02-packaging-and-licensing.md) — cross-ref, not re-researched) | Espressif build | **YES**: 12.9 % CPU @ 48 kHz stereo, 90 kbps | [same perf table](https://github.com/espressif/esp-adf-libs/blob/master/esp_audio_codec/README.md) | 96 kbps → 12 000 B/s, **0.096 Mbit/s** |
| Helix MP3 encoder | MP3 | **UNVERIFIED** (repo reports no recognised SPDX licence) | fixed-point | not measured here | [knik0/helix_mp3enc](https://github.com/knik0/helix_mp3enc) | — |

Decoder-vs-encoder precision: **libmad, minimp3, libmpg123 and Helix-the-decoder are
decoders**, not encoders. The only *encoder* discussed here that people confuse with a
decoder is Helix (a decoder port is common; the encoder separate). Espressif's own
`esp_audio_codec` ships **no MP3 encoder** — its encoder list is AAC, AMR-NB/WB, ADPCM,
G711A/U, PCM, ALAC, OPUS, LC3, SBC, G722, and never MP3, FLAC or Vorbis
([README](https://github.com/espressif/esp-adf-libs/blob/master/esp_audio_codec/README.md)).
That is a useful signal: inside Espressif, only Opus and AAC are considered worth encoding.

---

## 3. MP3: LAME is out, shine is the candidate

**The patents are effectively gone, with one honest caveat.** Fraunhofer's own blog states
the licensing programme ended because the patents expired: *"That day, the last of the core
mp3 patents which were part of the licensing program by Fraunhofer and Technicolor,
expired"* — 23 April 2017 — but it also warns *"the end of the mp3 licensing program does
not automatically mean that all mp3 technology is available license-free now. Apart from
the core mp3 patents included in the licensing program, there might still be some
implementation-specific patents"*
([Fraunhofer Audio Blog, 2017-05-18](https://www.audioblog.iis.fraunhofer.com/mp3-software-patents-licenses)).
VERIFIED as a first-party statement; note it is not an unconditional all-clear. LAME's own
README still carries its stale 2011 warning and a dead `mp3licensing.com` link
([lameproject/lame README](https://github.com/lameproject/lame)).

**LAME is a non-starter on this class of MCU.** It is float/double-heavy and built for
desktops. The only concrete ESP32 port found — `commarmi76/libmp3lame_for_esp32`, "Experiments
to port lame mp3 encoder to esp32" — encodes **16 kHz stereo** at best **1.38× realtime** after
-O3 and `-ffast-math`
([README](https://github.com/commarmi76/libmp3lame_for_esp32)). Scaling that to 44.1 kHz
stereo is not a small step: 1.38× realtime at 16 kHz means ≈ 0.72 s of CPU per second of audio;
at 44.1 kHz the same code must handle 2.76× more samples per second, i.e. ≈ 2.0 s of CPU per
second of audio, or **about two cores short**. **VERDICT: not viable; do not attempt.**

**shine is fixed-point and has real ESP32 measurements.** Upstream
[`toots/shine`](https://github.com/toots/shine) README: *"a blazing fast mp3 encoding library
implemented in fixed-point arithmetic … for architectures without an FPU"*. Source
inspection confirms the heavy path is integer: the polyphase filterbank uses `mul0`/`muladd`
on `int32_t`/`uint32_t` with `int64_t` accumulators (`src/lib/l3subband.c`), MDCT tables are
`int32_t cos_l[18][36]` (`src/lib/types.h`), and the quantisation loop is table-driven
integers. **Caveat found in source:** it is not 100 % float-free — `src/lib/l3loop.c` calls
`log((double)…)` twice per scalefactor band per granule per channel (~6 600 double `log()`
calls per second at 44.1 kHz stereo) for the energy model, plus `pow()`/`sqrt()` at init, and
`double steptab[128]` lives in the hot struct. On an LX7 there is no double FPU, so those
calls are software double — a real but small tax.

Measured performance on an **ESP32-S3**:

- The maintained IDF port [`anton-malakhov/mp3_shine_esp32`](https://gitflic.ru/project/anton-malakhov/mp3_shine_esp32)
  (a 2023 revival of the 2019 `fknrdcls` port) reports, tested at **48 kHz/16-bit/2 channels,
  256 kbps**: **"ESP32-S3: Single core takes 53.4 % of real time"** (also ESP32-S2 Mini
  56.9 %, classic ESP32 71.6 %). It adds: *"You should init the encoder ASAP in your code as
  the encoder needs large contiguous chunks of RAM."*
- An independent Arduino test by Phil Schatzmann on **ESP32-S3**, 44.1 kHz stereo, default
  128 kbps, measured **78 872–79 000 frames/s** (= **1.79× realtime**, ≈ 56 % of one core)
  ([blog](https://www.pschatzmann.ch/home/2026/06/20/mp3-encoing-for-microcontrollers/),
  [test sketch](https://github.com/pschatzmann/arduino-audio-tools/blob/main/examples/tests/codecs/test-mp3-shine/test-mp3-shine.ino),
  wrapper default `_bitrate = 128` in
  [`CodecMP3Shine.h`](https://github.com/pschatzmann/arduino-audio-tools/blob/main/src/AudioTools/AudioCodecs/CodecMP3Shine.h)).

Two independent S3 measurements agree at **~1.8–1.9× realtime**. VERIFIED as reported
measurements (I read both primary sources); the only unverified assumption is the clock —
Arduino-ESP32 defaults the S3 to 240 MHz, which I did **not** independently confirm, and the
port README says "single core" without naming the frequency.

**Quality caveat, quoted from source:** *"the encoding algorithm is rather simple. In
particular, it does not have any Psychoacoustic Model"* (shine README, repeated in the port
README). Also, shine supports joint stereo (`-j`, `JOINT_STEREO` in `src/bin/main.c:91`) but
the ESP32/Arduino wrapper configures plain `STEREO` for 2 channels. Expect MP3 that is
formats-compatible but audibly below LAME at the same bitrate. **This is not a loudness or
compatibility problem, only a quality-per-bit one.**

**Licensing:** upstream `COPYING` is the **GNU Library GPL v2** (LGPL-2); the maintained
`anton-malakhov` port carries the same LGPL-2 `COPYING`. Note the **older**
[`fknrdcls/mp3_shine_esp32`](https://github.com/fknrdcls/mp3_shine_esp32) is **GPL-3.0** —
do not use that one in a permissively-licensed library. LGPL-2 on an MCU means static
linking, which triggers the relink obligation; the library's packaging rules already handle
this class of dependency (see [02-packaging-and-licensing.md](../02-packaging-and-licensing.md)), but it is a real constraint,
not a formality.

---

## 4. Opus: license-clean, Espressif-supported, and only cheap at low complexity

**Licensing is verified and clean.** `xiph/opus` `COPYING` is three-clause BSD
([file](https://github.com/xiph/opus/blob/main/COPYING)). Xiph's own licence page: *"Opus has
a freely available specification, a BSD-licensed, high-quality reference encoder and decoder,
and protective, royalty-free licenses for the required patents"* — plus a precise caveat:
*"four companies that did not directly participate in the development of Opus, Qualcomm,
Huawei, France Telecom, and Ericsson, filed IPR disclosures with potentially royalty-bearing
terms"*, mitigated by *"external counsel Dergosits & Noah has advised us that Opus can be
implemented without the need to license the patents disclosed by Qualcomm, Huawei, France
Telecom, or Ericsson"* ([opus-codec.org/license](https://opus-codec.org/license/)). VERIFIED.

**Complexity control is real and is the whole ballgame.** RFC 6716 §2.1.5: *"In the reference
encoder, the complexity is selected using an integer from 0 to 10, where 0 is the lowest
complexity and 10 is the highest"*, and the trade-offs are concrete — LPC/whitening order
*"between 8, in lowest-complexity mode, and 16, for best quality"*, with warped (higher-quality)
analysis *"only done in higher complexity modes"*
([RFC 6716](https://www.rfc-editor.org/rfc/rfc6716.txt)). The API exposes it as
`OPUS_SET_COMPLEXITY(complexity)`, documented as *"a value from 1 to 10"* in `include/opus.h`
(the RFC says the reference encoder accepts 0–10; Espressif's component also says 0–10).

**ESP32-S3 measurements — two very different numbers, and the difference matters:**

- Espressif's own component (authoritative for this platform) measured on an **ESP32-S3R8**:
  **OPUS encoder, 48 kHz stereo, 90 kbps, complexity 0 → 24.9 % CPU, 29.4 KB heap**; the
  README's note adds *"For OPUS encoder test bitrate set as 90kbps, complexity set as 0"* and
  *"Encoder cpu usage is highly dependent on certain encoding settings (like bitrate or
  complexity)"* ([esp_audio_codec README](https://github.com/espressif/esp-adf-libs/blob/master/esp_audio_codec/README.md)).
  The same README documents the Opus encoder as 8/12/16/24/48 kHz, mono/dual, 20–510 kbps,
  complexity 0–10.
- The generic community port is far worse: *"Encoding however is much more intensive. 16kHz
  samplerate at encoding complexity 1 will use 70 % CPU. 24kHz samplerate at anything above
  complexity 4 will not be able to run at realtime. 48kHz can only run at realtime with
  complexity 1, and even then it uses 85 % CPU. It still sounds understandable at 16kHz if
  you just need basic VoIP or voice recognition though, but don't expect to encode high
  quality music"* ([esp-libopus README](https://github.com/XasWorks/esp-libopus); also warns
  the encoder uses `alloca` and wants ≥ 30 KB stack).

So: Opus encoding on an S3 is real, but **only at the bottom of the complexity ladder**.
Complexity 0 stereo at 90 kbps is fine for speech; for music at 44.1 kHz stereo you would want
complexity ~5–10, and both the community measurement and the RFC's own description of what
higher complexity adds say a 240 MHz Xtensa cannot do that in real time. VERIFIED for the two
numbers; the extrapolation to "music quality is not real-time" is **LIKELY, not measured**.

**Architectural cost specific to Opus:** Opus is not served as bare packets. `esp_audio_codec`
encoders emit raw frames, and its container/parser list (OGG → Vorbis, Opus) belongs to the
**decoder** side; there is no documented Ogg **muxer** for encoding. A browser needs an
Ogg-encapsulated stream (`OpusHead`/`OpusTags` + page framing), so adopting Opus means writing
or vendoring an Ogg muxer — nontrivial framing code, not just a codec call.

**Metadata: `icy-metaint` does not apply to Ogg.** VERIFIED in Icecast's source:
`icy-metaint` is emitted only in the MPEG path
([`src/format_mp3.c`](https://gitlab.xiph.org/xiph/icecast-server/-/blob/master/src/format_mp3.c));
`src/format_ogg.c` has no `icy-metaint` handling and reads metadata from Vorbis comments
(`update_comments()`, `ogg_decode`), while [`src/format_vorbis.c`](https://gitlab.xiph.org/xiph/icecast-server/-/blob/master/src/format_vorbis.c)
re-emits the comment header as a new logical stream (`ogg_stream_packetin(&…->new_os, …)`,
`vorbis_commentheader_out`). Icecast's config docs likewise describe ICY metadata as being for
*"legacy, non-Ogg streams like MP3"* and expose `icy-metadata-interval` only "for streams using
ICY metadata". **Implication for xtape:** the existing `send_meta()`/`metaint` machinery
(`mini_icecast.c:75-109`) would be dead code for an Opus station, replaced by a chained
comment-header mechanism that not every player honours — the [Icecast mailing list thread
"Meta Data for opus and FLAC do not work"](http://lists.xiph.org/pipermail/icecast/2019-August/014739.html)
is consistent with this being a long-standing rough edge (mailing-list report: HEARSAY, not a
spec).

---

## 5. Vorbis, AAC, FLAC

**Vorbis: no encoder exists.** `libvorbis` is float-only (402 `float`/`double` references in
`lib/*.c`; no `FIXED_POINT` support anywhere), and Vorbis's fixed-point implementation,
**Tremor, is decode-only** by its own README's wording ("integer playback codec", "in either
setup or **decode**"). There is no realistic Vorbis encoder path on this MCU. VERIFIED from
source.

**AAC: technically the best CPU/bitrate ratio, legally the worst.**
`esp_audio_codec` ships **AAC-LC** (mono/dual, 44.1/48 kHz among others, 12–160 kbps, ADTS
header optional) and it measured **12.9 % CPU at 48 kHz stereo / 90 kbps** on the ESP32-S3R8
([README](https://github.com/espressif/esp-adf-libs/blob/master/esp_audio_codec/README.md)) —
cheaper than Opus for the same bitrate. But **no FOSS AAC encoder is generally considered
patent-clean for redistribution**; that analysis belongs to the licensing note, not here —
see [02-packaging-and-licensing.md](../02-packaging-and-licensing.md) and the AAC/licence file owned by the other agent. Note
also the component's licence is the *Espressif Modified MIT* restricting use to Espressif
products (fine for xtape's stated target, but it cannot be vendored).

**FLAC is the surprise: lossless, license-clean, and the cheapest encoder measured.**
`esphome-libs/micro-flac` is a purpose-built ESP32 FLAC encoder+decoder, **Apache-2.0**,
*"All integer arithmetic"*, with these ESP32-S3 @ 240 MHz encode measurements on a 30 s
48 kHz stereo music clip ([benchmark README](https://github.com/esphome-libs/micro-flac/blob/main/examples/encode_benchmark/README.md)):

| Settings | Real-time | Bytes/sample | FLAC vs PCM |
|---|---|---|---|
| Defaults (fixed predictors), block 4096 | **21.3×** | 0.879 | **44.0 %** |
| Defaults, block 1152 | 20.9× | 0.878 | 43.9 % |
| `max_rice_partition_order` 6 | 17.9× | 0.874 | 43.7 % |
| `lpc` order 8 | 9.0× | 0.836 | 41.8 % |
| `lpc` order 8 + stereo search + partitions (size within 0.01 % of `flac -5`) | 5.9× | 0.828 | **41.4 %** |

That is a **2.3–2.4× bandwidth reduction for zero quality loss at 5–17 % of one core**, from a
C-only implementation (the README notes *"Encoder has no Xtensa assembly"*, so this is not
even the ceiling), with a tiny footprint (~2.1 KB object + ≤4.3 KB stack). Independent
sanity check on the ratio: MDN's audio guide says lossless compression is *"unlikely to get
more than 40–50 % compression"* ([MDN web audio codec guide](https://developer.mozilla.org/en-US/docs/Web/Media/Guides/Formats/Audio_codecs)),
which matches the measured 41–44 % of PCM.

**Reference `libFLAC` is a different story, and it was not measured on an ESP32.** Xiph's FAQ
concedes the asymmetry — *"FLAC is asymmetric … optimized for decoding speed at the expense of
encoding speed"*, and separately *"the encoder settings have a big effect on the encoding
time but not the decoding time"* — and its man page labels `-e` "Do exhaustive model search
(expensive!)" and `-p` "exhaustive search of LP coefficient precision (expensive!)", with
`-l 0` (fixed predictors) *"faster, but compresses weaker - typically five percentage points /
ten percent larger files"* ([FAQ](https://xiph.org/flac/faq.html),
[flac.md](https://github.com/xiph/flac/blob/master/man/flac.md)). `configure.ac` has **no**
fixed-point option and `src/libFLAC/lpc.c` is `double`-based. **UNVERIFIED: nobody has
published a libFLAC encoder benchmark on an ESP32 that I could find; `micro-flac` is the only
ESP32 FLAC encoder with numbers.** Do not assume libFLAC's desktop speed transfers.

---

## 6. Bandwidth arithmetic and how many clients actually fit

Payload bytes/s per client (no TCP/IP overhead — Espressif's iperf figures below are also
payload/goodput, so the comparison is payload-to-payload):

| Format | Math | Bytes/s | Mbit/s | vs PCM |
|---|---|---|---|---|
| PCM 44.1 kHz stereo 16-bit | 44 100 × 2 × 2 | 176 400 | **1.411** | 1.00 |
| PCM 32 kHz stereo 16-bit | 32 000 × 4 | 128 000 | 1.024 | 0.73 |
| PCM 32 kHz **mono** 16-bit | 32 000 × 2 | 64 000 | **0.512** | 0.36 |
| PCM 22.05 kHz **mono** 16-bit | 22 050 × 2 | 44 100 | **0.353** | 0.25 |
| FLAC 44.1 kHz stereo (measured 0.879 B/sample) | 0.879 × 88 200 | 77 500 | **0.62** | 0.44 |
| FLAC, best measured settings (0.828 B/sample) | 0.828 × 88 200 | 73 000 | 0.58 | 0.41 |
| MP3 192 kbps | 192 000 ÷ 8 | 24 000 | 0.192 | 0.136 |
| MP3 128 kbps | 128 000 ÷ 8 | 16 000 | **0.128** | 0.091 |
| AAC 96 kbps | 96 000 ÷ 8 | 12 000 | 0.096 | 0.068 |
| Opus 64 kbps | 64 000 ÷ 8 | 8 000 | **0.064** | 0.045 |

ICY metadata overhead on top: with `metaint = 16000`, the server writes at most
1 + 160 bytes per 16 000 audio bytes (≤ 1.0 %), and in the steady state just **one zero byte
per 16 000 bytes** (`send_meta()`, `mini_icecast.c:75-90`) — 0.006 %, negligible in every
case. (Note that 16 000 *compressed* bytes is ~1.0 s at 128 kbps but only 0.09 s of PCM, so
`metaint`'s meaning in time changes if encoding is adopted.)

**Per-client arithmetic against Espressif's own Wi-Fi figure.** ESP-IDF's Wi-Fi guide,
"ESP32-S3 Wi-Fi Throughput", reports for the S3 ([docs/en/api-guides/wifi.rst, v5.4](https://github.com/espressif/esp-idf/blob/v5.4.2/docs/en/api-guides/wifi.rst)):
**TCP RX 20 Mbit/s and TCP TX 20 Mbit/s "Air In Lab"; 73 Mbit/s RX / 83 Mbit/s TX in a
shield-box**; UDP 30 / 98 Mbit/s. The feature list states *"Up to 20 MBit/s TCP throughput
and 30 MBit/s UDP throughput over the air"*. The tool is the
[iperf example](https://github.com/espressif/esp-idf/blob/v5.4.2/examples/wifi/iperf/README.md)
with `sdkconfig.defaults.esp32s3`. The device transmits here, so **TCP TX** is the number to
use: **20 Mbit/s realistic, 83 Mbit/s best-case-shielded.**

- Payload capacity at 20 Mbit/s: PCM **14.2 clients**; FLAC **32 clients**; MP3 128 kbps
  **156 clients**; 32 kHz mono PCM **39 clients**. At 83 Mbit/s: PCM **58.8**.
- **But the code's own cap is the binding constraint**: `MAX_CLIENTS 4` and the config default
  is **2** (`mini_icecast.c:32,395`; header comment "1..4, default 2"). So the server as
  shipped peaks at **2 clients = 2.82 Mbit/s (14 % of Espressif's lab TCP TX)** and at most
  **4 clients = 5.65 Mbit/s (28 %)**.
- Therefore: **at the current cap, PCM bandwidth is not the bottleneck.** Memory is a much
  earlier limit than the network — every client's socket needs `CONFIG_LWIP_TCP_SND_BUF_DEFAULT=32768`
  of send buffer (`resources/sdkconfig.defaults`), and the burst ring is
  `out_hz × 4 × burst_ms / 1000` bytes (`mini_icecast.c:397`) = 88 KB at 44.1 kHz / 500 ms.
  Raising `MAX_CLIENTS` past ~8–16 is a heap-and-socket exercise long before it is a Wi-Fi
  throughput exercise.

Caveat on the throughput numbers: they are Espressif's **lab and shielded-box** figures
([source](https://github.com/espressif/esp-idf/blob/v5.4.2/docs/en/api-guides/wifi.rst)), not
a home LAN through two walls. Treat 20 Mbit/s as an optimistic ceiling for a real room; even
halving it leaves ≥ 7 PCM clients.

---

## 7. What encoding costs architecturally — and the cheaper lever nobody should skip

**Pipeline cost is additive.** Today `radio_task` (core 1) runs
`mi_dec_read → mi_rs_process → chunk_append → emit_chunk → broadcast + pace`
(`mini_icecast.c:328-349, 304-326`). An encoder inserts between `chunk_append` and
`broadcast`: decode + resample + **encode** + send, all on core 1. The task/core layout does
**not** have to change (which is good — Wi-Fi owns core 0), but core 1's headroom does:

- FLAC (`micro-flac`, 5–17 %) or Opus/AAC (13–25 %) fit alongside a decoder with room to spare.
- shine MP3 (~55 %) consumes over half of core 1 *before* the MP3/FLAC decoder (Espressif's
  own table puts MP3 decode at 8.17 % and FLAC decode at 8.0 % of a core on the S3R8 for
  **their** decoders, not xtape's [`mi_decode.c`]) and before the per-client `send()` work.
  It is feasible, but it is the option that turns "LAN radio" into "a machine that must not
  hiccup".

Two implementation details that would have to change with encoding:

- `pcm_cb` contract: the header promises the *same* PCM that goes on the wire reaches
  `pcm_cb` (`mini_icecast.h:8-11`, `mini_icecast.c:7-8`). With an encoder that is false — the
  callback would receive pre-encode PCM and the doc/comment must be corrected.
- The burst ring already stores length-prefixed records (`[len16 LE][chunk]`,
  `mini_icecast.c:58`), so it can carry variable-length encoded frames without a redesign;
  only the ring sizing (`bytes/s`, not `out_hz × 4`) changes.

**The overlooked lever: change the station format.** `out_hz` is already a single config
field (`mini_icecast.h`), and the resampler already handles arbitrary rates
(`mi_resample.c`). Dropping the station from 44.1 kHz to **32 kHz mono is 0.512 Mbit/s —
2.75× less, for free**, and **22.05 kHz mono is 0.353 Mbit/s — exactly 4× less**. That is:

- more bandwidth saved than FLAC (2.3×) at far lower CPU cost (one resample, which is
  already done), with no new dependency, no licence question, and no client-support risk;
- less than MP3's 11×, but with **no encoder CPU at all**, leaving the whole core for decode
  and more clients;
- and it shrinks the burst ring proportionally (88 KB → 32 KB at 32 kHz mono), which is
  valuable on a 512 KB-SRAM part.

Cost: 22.05 kHz mono limits audio to ~11 kHz bandwidth and loses stereo — fine for talk
radio, audibly poor for music. 32 kHz mono (~16 kHz bandwidth) is a defensible middle. Mono
requires a downmix step, which does not exist today (the decoder already duplicates mono to
stereo, `mi_decode.h:5-6`).

---

## 8. VERIFIED vs UNVERIFIED — explicit ledger

**VERIFIED (primary source read in this pass):**

- shine is fixed-point in its DSP path, with `double` energy-model calls in `l3loop.c`
  (`toots/shine` source; README; LGPL-2 `COPYING`).
- shine on ESP32-S3: **53.4 % of realtime** at 48 kHz/2ch/256 kbps (port README) and
  **78 872–79 000 frames/s** at 44.1 kHz/2ch/128 kbps (Schatzmann blog + test sketch +
  wrapper default bitrate). Two independent measurements, ~1.8–1.9× realtime.
- LAME on ESP32: **1.38× realtime at 16 kHz stereo** (port README) → non-starter at 44.1 kHz.
- ESP32-S3 encoder CPU table from Espressif (`esp_audio_codec` README, ESP32-S3R8):
  **Opus 24.9 %**, **AAC 12.9 %** at 48 kHz stereo / 90 kbps (Opus at complexity 0);
  decoder side MP3 8.17 %, FLAC 8.0 % at 44.1 kHz stereo.
- Opus licence: BSD-3 + royalty-free Xiph/Broadcom/Microsoft grants, with the
  Qualcomm/Huawei/France Telecom/Ericsson disclosure caveat (opus-codec.org/licence).
- Opus complexity is 0–10 with substantive quality/complexity levers (RFC 6716 §2.1.5,
  §5.2.3.2).
- `micro-flac`: integer-only, Apache-2.0, and an **ESP32-S3 @ 240 MHz encode benchmark** —
  21.3× realtime defaults at **0.879 B/sample (44.0 % of PCM)**, 5.9× at 0.828 B/sample.
- `libvorbis` is float-only, `tremor` is decode-only (both from their own sources).
- `icy-metaint` exists only in Icecast's MPEG path; Ogg metadata is a re-emitted comment
  header (Icecast source).
- ESP32-S3 Wi-Fi: TCP TX **20 Mbit/s air-in-lab, 83 Mbit/s shield-box** (ESP-IDF wifi.rst).
- ESP32-S3 MP3: the MP3 core patents' licensing programme ended 2017-04-23 with a caveat about
  implementation-specific patents (Fraunhofer, first-party).
- ESP32-S3 has **SIMD instructions available**: `SOC_SIMD_INSTRUCTION_SUPPORTED 1`
  ([`soc_caps.h:85`](https://github.com/espressif/esp-idf/blob/v5.4.2/components/soc/esp32s3/include/soc/soc_caps.h#L85),
  `SOC_SIMD_PREFERRED_DATA_ALIGNMENT 16`) with a `MALLOC_CAP_SIMD` heap flag
  ([mem_alloc docs](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/mem_alloc.html)).
  **Not verified: whether GCC auto-vectorises these encoders for Xtensa SIMD** — none of the
  measured encoders' sources claim SIMD use, and `micro-flac` explicitly says its encoder has
  *no* Xtensa assembly. Treat SIMD as unexplored headroom, not as banked performance.

**UNVERIFIED / LIKELY:**

- **Clock speed behind the shine measurements.** Neither source states 240 MHz; Arduino-ESP32
  defaults the S3 to 240 MHz. If either ran at 160 MHz, the numbers get *worse*, not better.
- **No libFLAC encoder measurement exists on any ESP32** that I could find. Its cost is
  inferred from its `double`-based LPC analysis and Xiph's own "asymmetric / expensive
  options" language — an inference, not a measurement.
- **No measurement of Opus encoding at music-quality complexity (≥5) on an ESP32-S3.** The
  24.9 % figure is complexity 0; the community port's numbers (48 kHz c1 = 85 % CPU, 24 kHz
  >c4 not realtime) say higher complexity will not fit, and the RFC explains why, but there
  is no direct S3 measurement at complexity 5–10. **The honest statement: complexity-0 Opus
  is provably real-time on an S3; music-grade Opus is probably not.**
- **Client-side support for compressed streams** was not verified from primary sources.
  MDN documents container support only (FLAC → native FLAC/Ogg/MP4; Opus → Ogg/WebM/MP4;
  MP3 → MPEG/ADTS/MP4) and does not settle which browsers play a *live, endless* Ogg-Opus or
  native-FLAC HTTP stream. **This must be spot-checked on the actual target clients
  (Safari/iOS especially) before choosing Opus or FLAC over MP3.**
- Espressif's throughput figures are lab/shielded-box, not a real RF environment.
- 24-bit/32-bit FLAC and `micro-flac` on 25–32-bit input are much slower (3.3–4.1× realtime)
  and partly outside the RFC 9639 streamable subset — irrelevant for 16-bit PCM output, noted
  for completeness.
- Helix MP3 encoder licence and performance: not verified.

---

## 9. Recommendation

**Do not add an encoder to fix bandwidth at the current client cap.** The server defaults to
**2 clients = 2.8 Mbit/s**, and its hard cap is **4 = 5.65 Mbit/s**, against Espressif's own
**20 Mbit/s** TCP-TX figure for the ESP32-S3. `MAX_CLIENTS`, socket send buffers and heap run
out first. Encoding would spend 5–55 % of the one audio core to compress a stream that is
using 14–28 % of the radio.

**If bandwidth is genuinely the goal, in priority order:**

1. **Change the station format first — it is free.** 32 kHz mono = **0.512 Mbit/s** (2.75×
   less), 22.05 kHz mono = **0.353 Mbit/s** (exactly 4× less), using the `out_hz` field and
   resampler that already exist, with a mono downmix added. No dependency, no licence, no
   client-support risk, less RAM. For a talk-radio/background-music box this beats every
   encoder on effort-per-byte. **Do this before any encoder work.**
2. **If compression is required and the clients must be maximally compatible, use MP3 via
   shine** (`anton-malakhov/mp3_shine_esp32`, LGPL-2): ~1.8× realtime (≈ 55 % of core 1) at
   128 kbps → **0.128 Mbit/s, 11× less**, keeps `Content-Type: audio/mpeg` and the existing
   ICY `metaint`/now-playing code path unchanged, and is the only compressed format *every*
   client decodes. Accept: audibly-below-LAME quality (no psychoacoustic model), plain
   stereo, large contiguous RAM at init, ~55 % of the audio core, and LGPL-2 static-link
   obligations. Budget the remaining core carefully, because decode + resample + send are
   additive on the same core.
3. **If licence-clean and CPU-cheap matter more than the size of the win, use FLAC via
   `micro-flac`** (Apache-2.0, integer-only, 21× realtime at defaults, ~2 KB + ≤ 4.3 KB
   stack). It is the lowest-risk encoder measured on this exact chip and gives **0.62 Mbit/s
   (2.3× less), losslessly**. Choose it over shine when you would rather not touch MP3
   licensing/quality at all and a 2.3× cut is enough — but note it is *not* a big win, and it
   loses ICY metadata (same Ogg/native-container metadata problem as Opus, and no
   `icy-metaint`).
4. **Opus is the best format and the wrong default here.** Espressif's component makes it
   real (24.9 % CPU at complexity 0, 90 kbps), it is BSD-3/royalty-free upstream, and 64–90
   kbps is the best bytes-per-quality available. But: complexity 0 is a speech-grade setting
   for a music station; you must write Ogg encapsulation (`esp_audio_codec` documents no
   muxer); `icy-metaint` cannot carry the now-playing title; the component's licence forbids
   non-Espressif reuse; and client support for live Ogg-Opus is unverified. Revisit only if
   the station becomes speech-first and you control the clients.
5. **AAC: technically excellent, legally out of scope** — do not adopt without the licence
   analysis in the neighbouring note.

**Bottom line:** an ESP32-S3 *can* encode in real time — provably, with measurements, for
fixed-point MP3 (shine), FLAC (`micro-flac`) and complexity-0 Opus (Espressif). But the
current server does not need it. Fix the station format first; add shine MP3 only if a
client-compatible compressed stream is genuinely required; prefer `micro-flac` if you want the
cheapest, cleanest encoder instead.
