# Expanded format set (MP3, Opus/Ogg, Vorbis, WebM): what it actually costs

Synthesis of the second research round, for the requirement change to:

- **Output:** MP3, and/or Opus in Ogg
- **Input:** MP3, Ogg Vorbis, Opus, WebM — *plus* keeping FLAC and WAV
- **Role:** the ESP32 remains the server (clients connect to it)

Written 2026-10-16. Evidence for each claim is in the per-topic files; this file is the
decision layer. Verification I ran myself is marked **[checked]**.

## 1. The single most important finding: no container code needs to be hand-written

The first round feared that adopting Opus meant writing an Ogg muxer. On current evidence
that is false in both directions — there is a permissive component for each side:

| Direction | Component | Licence | What it gives |
|---|---|---|---|
| **Ogg demux (input)** | `esphome/micro-ogg-demuxer` | **Apache-2.0** | "lightweight, platform-agnostic Ogg container demuxer"; used by `micro-opus`'s and `micro-vorbis`'s Ogg paths **[checked: repo licence + description]** |
| **Ogg mux (output)** | `georgik/ogg` v1.3.5 | **BSD-3-Clause** | thin IDF wrapper whose `.gitmodules` points at `https://github.com/xiph/ogg.git`, i.e. **upstream libogg**, which contains the encoding half (`ogg_stream_packetin`, `ogg_stream_pageout`, `ogg_stream_flush`, `ogg_page_checksum_set`) **[checked: `.gitmodules`, `idf_component.yml`]** |
| Ogg mux, turnkey alternative | `espressif/esp_muxer` v1.2.3 | Espressif Modified MIT | its README matrix is **OPUS×OGG = Y**, MP3×OGG = **N** **[checked]** |

**Consequence:** the remaining Ogg work is the *codec-specific packetisation* on top of
libogg — `OpusHead`/`OpusTags` and granule positions per RFC 7845 — not a container
implementation. That is a much smaller, well-specified task.

**And it is already written, twice, under Apache-2.0.** Two complete, working Ogg page
writers ship inside `esphome-libs` test directories and can be adapted rather than invented:

- `esphome/micro-opus` → `tests/support/ogg_mux.h` — ~208 lines including the CRC table and
  `OpusHead`/`OpusTags` builders;
- `esphome/micro-ogg-demuxer` → `tests/ogg_page_builder.*` — ~239 lines.

So the estimate for the muxing work is **~200–400 lines of ordinary code** on top of
`georgik/ogg` (whose upstream `libogg` is `framing.c` + `bitwise.c` = 3,227 lines), with no
research risk left in it. `esp_muxer` remains the zero-code option if the Espressif licence
is acceptable — its `muxer_data_callback` is documented as "suitable for living stream
scenario", so it can write to a socket rather than only a file, and `OpusHead`/`OpusTags`
generation was confirmed present in its prebuilt S3 binary (its source is not published,
so its pre-skip/granule correctness cannot be audited).

Note the licence split, which matches the project's existing policy: `georgik/ogg` (BSD-3)
can be the **core** dependency, `esp_muxer` (Espressif-restricted) an optional alternative.

## 2. The decode set is already covered, permissively — use `esphome/micro-*`

A coherent family exists in the registry, all Apache-2.0 (one dual-licensed), covering the
entire requested input list. Measured figures are from the `s8` research:

| Input | Component | Licence | Measured on S3 |
|---|---|---|---|
| Ogg Vorbis | `esphome/micro-vorbis` v0.1.0 | Apache-2.0 **AND** BSD-3 | **9.5× realtime**, integer fork of Tremor, arena allocator, ~76 KB (20 internal + 56 PSRAM) |
| Opus (incl. Ogg) | `esphome/micro-opus` v0.4.1 | Apache-2.0 | **6.6× realtime** CELT stereo; ships an **`OggOpusDecoder`** C++ class **[checked: header + README]**; RAM 30–50 KB state + **120 KB/thread pseudostack** |
| MP3 | `esphome/micro-mp3` v0.4.0 | Apache-2.0 | 22.8 KB, 13.1× realtime |
| FLAC | `esphome/micro-flac` v0.3.0 | Apache-2.0 | decode **and encode**, Xtensa asm |
| WAV | `esphome/micro-wav` v0.2.0 | Apache-2.0 | — |
| umbrella | `esphome/micro-decoder` v0.4.0 | Apache-2.0 | all five behind one C++ API; requires **idf >=5.5** |

**Do not vendor** `tremor` / `libvorbis` / `stb_vorbis`: libvorbis and `stb_vorbis` are
float-only, and `stb_vorbis`'s own header states it **cannot concatenate multiple Vorbis
streams** — disqualifying for a station that changes tracks. (`micro-vorbis` is the integer
fork that solves exactly this.)

**Three costs to accept, and they are real:**

1. **It is C++.** The umbrella and the Ogg wrappers are C++ classes. For a C library this
   forces C++ on every consumer; the individual decoders may expose C APIs, which must be
   checked per component.
2. **It is shaped as a client.** `micro-decoder` is "C++ audio decoding library with
   HTTP/HTTPS streaming" — built to pull a stream, not to read SD-card files and re-serve
   them. Whether each decoder can be driven from a file handle is the key integration
   question.
3. **`micro-decoder` needs `idf >= 5.5`**, stricter than the rest of the stack (the Ogg
   muxer needs only `>= 5.0`). Targeting IDF 5.4 rules out the umbrella.

## 3. WebM: drop it from the default build

This is the recommendation, and the reasoning is not "it's hard" but "it adds nothing":

- **No component exists.** Registry searches for `webm`, `matroska`, `ebml` return
  `ComponentNotFoundError`; `esp_extractor` covers MP4/TS/FLV/WAV/OGG/AVI/CAF only
  **[checked: registry API]**.
- **Hand-writing an audio-only extractor is ~900–1,400 lines** of bit-parsing over
  untrusted input (an estimate, not a measurement).
- **It adds zero new audio capability.** WebM's audio is *only* Opus, Vorbis, MP3, or FLAC —
  every one of which the new decoder set already handles natively. So WebM buys bug surface
  and no formats.
- **No new royalty question** — cross-referenced to the licensing work, not re-researched.
- **If ever needed,** Mozilla's **`nestegg`** is the right parser: 4,269 lines of C, **ISC**
  licence, only `calloc` + caller-supplied `read`/`seek`/`tell` I/O, and it already
  enumerates OPUS/VORBIS/FLAC/MP3/PCM. An adapter is ~300–500 lines. Not verified to build
  on S3.

**The technical gotcha that makes WebM genuinely awkward** (worth recording even though WebM
is dropped): Matroska carries codec headers **out of band in `CodecPrivate`** — all three
Vorbis header packets pre-laced, and for Opus the `OpusHead` plus
`CodecDelay = pre-skip * 1e9 / 48000`. The Ogg-shaped `micro-vorbis`/`micro-opus` decoders
therefore **cannot be fed a WebM track** without a shim to their packet-level API. So WebM
isn't merely "another container"; it's an incompatible framing for the decoders chosen.

Matroska is now properly citable: **RFC 9559** (Matroska Media Container Format, Standards
Track, October 2024) and **RFC 8794** (EBML, July 2020) **[checked: both fetched from
rfc-editor.org]**.

## 4. MP3 and Opus cannot be one stream

This is a hard architectural consequence, not a preference:

- An Ogg-Opus stream is delimited and signalled by a container.
- A bare MP3 stream has **no container layer at all**, so there is no place to signal a
  format change mid-stream. This is not a limitation to work around — MP3 is *never*
  Ogg-encapsulated in practice: ffmpeg's Ogg muxer rejects MP3 outright (`oggenc.c` allows
  only Vorbis/Theora/Speex/FLAC/Opus/VP8), and Icecast's Ogg plugin only dispatches to
  `initial_vorbis_page()`/`initial_opus_page()`. Icecast serves MP3 as bare frames with
  `audio/mpeg` + `icy-metaint`. **So the existing MP3/ICY path needs zero container work.**
- Ogg **chaining** is spec-legal (RFC 3533 §5) but clients treat a new serial number as
  end-of-stream, so chaining should not be used to switch formats for a listener.

**Recommendation:** a Kconfig `choice` selecting exactly **one** output codec, behind the
encoder vtable already recommended in `../01-library-architecture.md` §4. If both client
families must be served simultaneously, expose two mounts (`/stream.mp3`, `/stream.opus`)
and accept that both encoders are resident. Do **not** attempt one stream that changes codec.

## 4a. CORRECTION: Opus encoding is not limited to complexity 0 — and it is the right default

An earlier round concluded "Opus is real, but only at the bottom of the complexity ladder"
and that music-grade Opus was probably not real-time. **That conclusion is retired.** It was
based on Espressif's `esp_audio_codec` measurement (24.9 % CPU at complexity 0 = 4× realtime)
and a community libopus port. A better component exists, and I verified its contents myself
by downloading the distributed archive:

**`esphome/micro-opus` v0.4.1 — Apache-2.0 AND BSD-3-Clause, `idf >=4.4`.** The published
7.5 MB component archive ships:

- the **full libopus** source tree — **267 `.c` files**;
- the **encoder** sources explicitly present and compiled: `silk/enc_API.c`,
  `celt/celt_encoder.c`, `src/opus_encoder.c`, and `cmake/sources.cmake` defines
  `OPUS_ENCODER_SOURCES`, referenced twice in `CMakeLists.txt`;
- **`lib/opus/include` exported in `INCLUDE_DIRS`**, so consumers get the **plain C API** —
  `opus_encoder_create`, `opus_encode`, `opus_encoder_ctl`, `OPUS_SET_COMPLEXITY`,
  `OPUS_SET_BITRATE`, `opus_decoder_create` are all present in the exported `opus.h`.

The last point dissolves the strongest objection to the earlier candidates: this is **not** a
C++-only wrapper with no bitrate setter. You get upstream libopus as C, with the encoder, and
its own Xtensa LX7 assembly for **both** CELT and SILK.

Its shipped benchmark (`examples/encode_benchmark`) is a credible methodology — it is
pre-configured for 240 MHz + PSRAM + fixed-point, decodes 20 ms PCM frames and times **only**
`opus_encode()`, and auto-skips a configuration once it falls behind real time. It tests
48 kHz stereo music (CELT, 64–192 kbit/s) and 16 kHz mono speech (SILK) across complexities
0/2/5/8/10. Per the benchmark's own documentation the music series is real-time across the
range, which is what retires the complexity-0 ceiling.

**Two hard constraints this introduces:**

1. **Opus has no 22.05 kHz input rate.** `opus.h` permits 8/12/16/24/48 kHz only. So the
   attractive "22.05 kHz mono for talk radio" bandwidth lever from §3 is **unavailable on the
   Opus path** (it remains available for an MP3 or PCM station). A 48 kHz Stereo station is
   the natural Opus configuration, which also happens to make the resampler a no-op for Opus
   input.
2. **Memory does not fit without PSRAM.** Opus needs ~30–50 KB of encoder state **plus a
   120 KB pseudostack per thread** (documented in the component's own `Kconfig`; it is
   allocated from PSRAM when available and is configurable down to ~60 KB). `shine` needs a
   separate ~97.6 KB. Those cannot co-reside on a PSRAM-less S3, which is a further argument
   for the one-codec Kconfig choice rather than shipping both.
3. **Fixed-point is mandatory for SILK encoding** on the S3 — the component's own README
   states floating-point SILK is 4–6× slower and fails real-time even at the lowest
   complexity; CELT is only 10–40 % slower in float.

**Revised output recommendation:** make **Opus-in-Ogg the default output** (Apache-2.0/BSD-3
libopus, no LGPL compliance burden) and keep **MP3 encode as an opt-in backend**, because
`shine` is LGPL-2.0-only (see `s11-licensing-verified-and-packaging.md`). That keeps the
default build free of both LGPL obligations and any royalty surface, and it costs no extra
design: `shine` simply becomes another implementation behind the encoder vtable.

## 5. Design pass-through in from the start

This is the highest-leverage design decision in the whole expansion, and it is a
**"do it now or rewrite later"** choice.

The measured asymmetry: MP3 **decode** costs 7.6 % of a core but `shine` MP3 **encode** costs
~53 %; Opus decode ~15 % vs encode ~25 %. So for an MP3 station serving MP3 files, the cost
collapses from ~61 % of a core to **~0** if frames are passed through untouched.

- **It saves the encoder, not the decoder** — that is the whole point, since the encoder is
  the expensive stage.
- **It applies when** the file's codec equals the station codec *and* rate and channels
  match. A 44.1 kHz station maximises MP3 pass-through; a 48 kHz station eliminates Opus
  resampling. That is a real parameter trade-off to surface in configuration.
- **What it changes structurally:** the decoder interface stops being "always produce PCM"
  and becomes "**PCM or frames**"; the encoder stops being "always encode" and becomes
  "**encode or bypass**". Retrofitting this later means rewriting **both** seams, so the
  interfaces in `../01-library-architecture.md` §4 and §6 must be reshaped **now** even if
  pass-through ships later.

## 6. Opus decode details that touch the existing design

From RFC 7845 and `libopus` **[as reported in `s8`, with spec quotes]**:

- `opus.h` states *"Internally Opus stores data at 48000 Hz"*, and RFC 7845 §5.1 says the
  input sample rate *"is **not** the sample rate to use for playback"*. **So the decoder seam
  can always be `opus_decoder_create(48000, N)` and `mi_resample.c` remains the correct
  seam, unchanged** — a tidy result that means no new resampling design is needed.
- **Pre-skip must be decoded then discarded** (not skipped in the packet stream), and
  `PCM position = granule - pre-skip`, with end-trimming per §4.4. Getting this wrong yields
  a small constant offset rather than an obvious failure — a classic silent bug.
- RFC 7845 §7.2 defines **"Continuous Chaining"** specifically motivated by internet radio;
  note how it differs from ordinary chaining (which clients treat as EOS).
- `libopus` has **no upstream Xtensa CELT assembly** (`celt/{arm,mips,x86}`; only SILK has
  Xtensa) — which is precisely why `micro-opus` exists as a wrapper adding Xtensa DSP
  (+17–25 %).

## 7. Resource structure: Kconfig, not "link everything"

- **Flash is not the constraint** — IDF stock app partitions are 1 MB, and Opus is ~100 KB of
  code.
- **RAM is the constraint** — Opus alone wants 30–50 KB of state plus a **120 KB per-thread
  pseudostack**.
- Therefore: one Kconfig `choice` for the output codec, per-format `bool` options for inputs
  (**WebM defaults to `n`**), and **runtime magic-byte probing within the compiled set**
  rather than by file extension — the current `mi_decode.c` dispatches on `strrchr(path,'.')`,
  which is exactly the thing to replace.

## 8. Revised build order

1. **Reshape the two seams** (decoder: PCM-or-frames; encoder: encode-or-bypass) and lift the
   resampler, with host tests. No new formats yet — but get the interfaces right per §5.
2. **Decoder backends** via `esphome/micro-*` behind a C-facing vtable, with runtime
   magic-byte probing instead of extension matching.
3. **Ogg mux** on `georgik/ogg`/libogg: `OpusHead`/`OpusTags`, granule positions, page
   sizing, pre-skip.
4. **Opus encode** (`78/esp-opus-encoder` MIT, or `esp_audio_codec`) and **MP3 encode**
   (`shine`, vendored — no component exists).
5. **Pass-through** once the generated path works, since the seams already accommodate it.
6. WebM only on demand, behind a Kconfig flag, using `nestegg`.

## 9. Open questions the research could not close

- Whether `micro-decoder`/`micro-*` can decode from an **SD-card file** rather than an HTTP
  stream (the client/server shape mismatch).
- Whether the individual `micro-*` decoders expose **C** APIs, or only C++.
- Tremor `lowmem` branch existence; `78/esp-opus` packaging/targets; whether `nestegg`
  builds on S3; practical `stb_vorbis` RAM.
- Whether *client* devices tolerate Ogg **chaining** (chained *codec*/format changes
  specifically — the sibling citation concerns metadata updates).
- Whether `georgik/ogg`'s `CMakeLists.txt` actually compiles the full libogg source set
  (consistent with it, but not confirmed by building).
- The pass-through saving is **arithmetic from measured stage costs, not an end-to-end
  measurement**.
- **Still outstanding from the first round:** whether any specific hardware internet radio
  will play a live Ogg-Opus HTTP stream. This remains the most decision-relevant unverified
  question for the "stream to a real radio" use case.
