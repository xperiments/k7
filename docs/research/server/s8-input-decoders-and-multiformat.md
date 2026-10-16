# S8 — Decoding the expanded input set, and what six inputs / two outputs do to the architecture

**Question:** the radio server must now read **MP3, Ogg Vorbis, Opus, WebM, FLAC, WAV** off SD,
resample to one station format, and serve **MP3 and/or Opus-in-Ogg** forever. What does decoding
each cost on an ESP32-S3, and what does "many formats" do to the design?

**Method / date:** spec text, decoder source, and the ESP Component Registry API, read on
2026-10-16. Companion notes are **not** re-researched: licensing is in
[`s1-aac-licensing.md`](s1-aac-licensing.md) and [`../radio-player-codecs.md`](../radio-player-codecs.md)
§4, encoder feasibility and measured encoder cost in [`s2-encoder-feasibility.md`](s2-encoder-feasibility.md),
wire format and client behaviour in [`s3-wire-format-and-clients.md`](s3-wire-format-and-clients.md),
the decoder-seam recommendation in [`../01-library-architecture.md`](../01-library-architecture.md) §4.
The code under review is [`resources/mi_decode.c`](../../../resources/mi_decode.c) (`open/hz/read/close`,
file-shaped, seek callbacks) and [`resources/mi_resample.c`](../../../resources/mi_resample.c).

**Headline:** the whole expanded set turns out to be cheap and *already packaged for ESP-IDF* — a
family of permissively licensed `esphome/micro-*` decoders plus `78/esp-opus` covers MP3, Vorbis,
Opus, FLAC and WAV. **WebM is the only genuinely new work**, and it buys almost nothing. The bigger
finding is architectural: **pass-through (no decode at all) is worth designing for**, because the
expensive half of this pipeline is the *encoder*, not the decoder.

---

## 1. Verdicts up front

**(a) WebM: drop it from the default build; add it later behind a Kconfig flag using Mozilla
`nestegg` as a vendored demuxer, never hand-written and never bundled in the core.** WebM is not a
music-library container — it exists to carry VP8/VP9 video, and its *audio* codecs are exactly the
formats this component already reads natively. Hand-writing a seekable audio-only Matroska extractor
is ~900–1,400 lines of load-bearing bit-parsing for zero new audio capability. See §5.

**(b) Pass-through: yes, design for it — it is the single biggest CPU win available, and it saves
the encoder rather than the decoder.** See §7.

**(c) Many formats: build-time selection (Kconfig), with runtime probing inside the selected
set.** One output codec per firmware image. See §8.

---

## 2. Input format → decoder table

Every row is *verified* unless flagged. "RAM" is per decoder instance, and the `micro-*` figures are
the libraries' own published numbers for an ESP32-S3 at 240 MHz.

| Input | Recommended decoder | Licence | RAM (per instance) | Streaming-capable? | IDF component (registry) |
|---|---|---|---|---|---|
| **MP3** | `esphome/micro-mp3` (fixed-point fork of OpenCore) — or keep `dr_mp3` | Apache-2.0 | **~22.8 KB** internal (21.3 KB state + 1.5 KB input buffer) + 4.5 KB caller PCM | **Yes** — `decode()` on pushed bytes, frame sync built in | [`esphome/micro-mp3` **v0.4.0**](https://components.espressif.com/components/esphome/micro-mp3) |
| **Ogg Vorbis** | `esphome/micro-vorbis` (fixed-point fork of **Tremor**, `lowmem` branch) | **Apache-2.0 AND BSD-3-Clause** | **~76 KB total** for 48 kHz stereo 128 kbps: ≈20 KB internal (codebook tables preferred internal) + ≈56 KB PSRAM (state ≈13 KB stereo + demuxer + output) | **Yes** — `decode(input, len, out, out_size, &consumed, &written)`; Ogg parsing zero-copy | [`esphome/micro-vorbis` **v0.1.0**](https://components.espressif.com/components/esphome/micro-vorbis) |
| **Opus (Ogg)** | `esphome/micro-opus` (`OggOpusDecoder`, or raw `opus_decode`) | Apache-2.0 | **state ~30–50 KB + pseudostack 120 KB/thread** (60–240 KB configurable) + 1–61 KB Ogg buffers | **Yes** — both Ogg-Opus and raw-packet paths | [`esphome/micro-opus` **v0.4.1**](https://components.espressif.com/components/esphome/micro-opus); also [`78/esp-opus` **v1.0.5**, MIT](https://components.espressif.com/components/78/esp-opus) |
| **FLAC** | keep `dr_flac` (already in tree) or `esphome/micro-flac` | `dr_flac` public domain / MIT-0; micro-flac Apache-2.0 | `dr_flac` **89.4 KB** (Espressif-measured); micro-flac adds Xtensa asm | **Yes** (push API); native Ogg-FLAC is *not* in `dr_flac` (`DR_FLAC_NO_OGG` is set in `mi_decode.c:15`) | [`esphome/micro-flac` **v0.3.0**](https://components.espressif.com/components/esphome/micro-flac) |
| **WAV** | keep `dr_wav` or `esphome/micro-wav` | `dr_wav` public domain / MIT-0; micro-wav Apache-2.0 | negligible (4-byte accumulator) | **Yes** — "byte-by-byte streaming … any chunk size" | [`esphome/micro-wav` **v0.2.0**](https://components.espressif.com/components/esphome/micro-wav) |
| **WebM** | **none first-party.** Mozilla `nestegg` (vendored) behind a Kconfig flag | ISC-style | ≈110 KB of C source; runtime small (arena/`calloc` per element) | **Seekable**, not stream-only | **No component exists** — registry `?q=webm`, `matroska`, `mkv`, `ebml` all return `ComponentNotFoundError` |
| *(bundle)* | `esphome/micro-decoder` — wires all five `micro-*` codecs behind one C++ API | Apache-2.0 | sum of the above | Yes; it is the player-shaped layer, not the server-shaped one | [`esphome/micro-decoder` **v0.4.0**](https://components.espressif.com/components/esphome/micro-decoder) |

Two registry facts worth keeping:

* **No WebM/Matroska/EBML component exists.** Verified by API:
  `curl 'https://components.espressif.com/api/components?q=ebml'` →
  `{"error":"ComponentNotFoundError"}` (same for `webm`, `matroska`, `mkv`). Espressif's own
  [`esp_extractor` **v1.0.7**](https://components.espressif.com/components/espressif/esp_extractor)
  supports MP4 / TS / FLV / WAV / OGG / AVI / CAF and **not** WebM
  ([README container table](https://components-file.espressif.com/components/espressif/esp_extractor/1.0.7/readme.md)).
* **An Ogg muxer *does* exist first-party** —
  [`esp_muxer` **v1.2.3**](https://components.espressif.com/components/espressif/esp_muxer), whose
  README marks OPUS in **OGG** as `Y`
  ([table](https://components-file.espressif.com/components/espressif/esp_muxer/1.2.3/readme.md)).
  This directly contradicts the older note in [`s2-encoder-feasibility.md`](s2-encoder-feasibility.md)
  §"Architectural cost specific to Opus" that "there is no documented Ogg muxer for encoding".
  **Caveat that keeps the conclusion intact:** `esp_muxer` (and `esp_audio_codec`) are **Espressif
  Modified MIT**, whose redistribution clause the licensing notes already flag as hostile to a
  redistributable library — see [`s1-aac-licensing.md`](s1-aac-licensing.md) and
  [`../radio-player-codecs.md`](../radio-player-codecs.md) §4.1.

---

## 3. Ogg Vorbis

### 3.1 Three headers, all required, in order — and the third is load-bearing

The Vorbis I specification is explicit, and the wording matters for a server that finds a `.ogg` on
an SD card
([Vorbis I spec §1.3.1](https://xiph.org/vorbis/doc/Vorbis_I_spec.html#x1-480001.3.1)):

> "Before decoding can begin, a decoder must initialize using the bitstream headers matching the
> stream to be decoded. Vorbis uses **three header packets; all are required, in-order**, by this
> specification. Once set up, decode may begin at any audio packet belonging to the Vorbis stream."

and ([§4.2](https://xiph.org/vorbis/doc/Vorbis_I_spec.html#x1-680004.2)):

> "A Vorbis bitstream begins with three header packets. The header packets are, in order, the
> identification header, the comments header, and the setup header. **All are required for decode
> compliance. An end-of-packet condition during decoding the first or third header packet renders
> the stream undecodable. End-of-packet decoding the comment header is a non-fatal error
> condition.**"

| Header | Packet signature | What the decoder needs from it |
|---|---|---|
| **Identification** (type 1) | `0x01` + `"vorbis"` | `vorbis_version` (must be 0), `audio_channels` > 0, `audio_sample_rate` > 0, three bitrate hints, `blocksize_0`/`blocksize_1` (exponents; legal values 64…8192, `bs0 ≤ bs1`), framing bit must be 1 ([§4.2.2](https://xiph.org/vorbis/doc/Vorbis_I_spec.html#x1-720004.2.2)) |
| **Comment** (type 3) | `0x03` + `"vorbis"` | **Nothing needed to decode** — it is the vendor string + user comments (Vorbis comment). Losing it is a *non-fatal* error per §4.2, i.e. it is metadata |
| **Setup** (type 5) | `0x05` + `"vorbis"` | **Everything**: codebooks, time-domain transform placeholders, floor configs, residue configs, channel mappings, mode configs, final framing bit ([§4.2.4](https://xiph.org/vorbis/doc/Vorbis_I_spec.html#x1-760004.2.4)) |

**Direct consequence for the server:** a decoder cannot emit a single PCM frame from a `.ogg` until
it has consumed the **entire setup header**, which is the bulk of the codec configuration and is
unbounded in size. On a local file this is trivially satisfied — the setup header sits in the first
two pages. On a *stream* it means the decoder is a byte-eating state machine that may be handed the
header across several `decode()` calls; `micro-vorbis` documents exactly this:
"the Tremor decoder state and codebook tables are allocated as the three header packets are
processed, **which can span several `decode()` calls when input arrives in small chunks**"
([README](https://github.com/esphome-libs/micro-vorbis)). Practical implication: **the first track's
setup header must be fully parsed before the station can produce any audio**, so the server's
connect-time burst can be empty for the first ~10–50 ms of a Vorbis track. Budget for that in the
`lead_ms` shock absorber, not with a hack.

### 3.2 Decoder options

* **`libvorbis` — float-only: VERIFIED.** No fixed-point build path exists; the decoder is written
  in `float` throughout (`lib/block.c`, `lib/floor1.c`, `lib/res0.c`, `lib/mdct.c`). Xiph's own
  fixed-point answer is Tremor, not a `libvorbis` configure option. Licence BSD-3-Clause.
  **Disqualified for the decode path on an S3** — not because the S3 lacks an FPU (it has one), but
  because Tremor and `micro-vorbis` are strictly cheaper and already Xtensa-tuned.
* **`tremor` (`libvorbisidec`) — integer, decode-only, BSD-3: VERIFIED.** Its README states it
  plainly: *"No floating point operations are used at any point in either setup or decode. This
  decoder library will properly decode any past, current or future Vorbis I file or stream."*
  ([tremor README](https://gitlab.xiph.org/xiph/tremor/-/raw/master/README)); the build produces
  `libvorbisidec`, i.e. the **decode** library. Licence: Xiph BSD-3-Clause
  ([COPYING](https://gitlab.xiph.org/xiph/tremor/-/raw/master/COPYING)). *The `lowmem` branch is
  referenced by the `micro-vorbis` README but I did not fetch the branch itself — UNVERIFIED as a
  branch; the fork relationship is asserted by the fork.*
* **`stb_vorbis` — dual MIT / public domain: VERIFIED.** Header line 1 is `// Ogg Vorbis audio
  decoder - v1.22 - public domain`; the end of the file offers `ALTERNATIVE A - MIT License` /
  `ALTERNATIVE B - Public Domain (<www.unlicense.org>)`
  ([stb_vorbis.c](https://raw.githubusercontent.com/nothings/stb/master/stb_vorbis.c)). It has a
  genuine push API — `stb_vorbis_open_pushdata` / `stb_vorbis_decode_frame_pushdata` — documented as
  *"this API allows you to get blocks of data from any source and hand them to stb_vorbis … 
  stb_vorbis may not have enough data to work with and you will need to give it the same data again
  PLUS more"*, returning `VORBIS_need_more_data` — which is precisely the setup-header-across-chunks
  case above. **But it is float-only** (`float ***output`, and `stb_vorbis_decode_frame_pushdata`
  hands back `float` channel buffers) and it documents four hard limitations in its own header:
  *"floor 0 not supported (used in old ogg vorbis files pre-2004); lossless sample-truncation at
  beginning ignored; **cannot concatenate multiple vorbis streams**; sample positions are 32-bit"*.
  Memory is data-dependent, and it is the one library that tells you its own number: *"In my test
  files the maximal-size usage is ~150KB"*, with `stb_vorbis_info.setup_memory_required` /
  `setup_temp_memory_required` / `temp_memory_required` exposed so you can query it after opening.
  **The "cannot concatenate multiple vorbis streams" limitation matters here**: a library that
  plays a folder of tracks is a chaining decoder, and stb_vorbis is not one.

**Recommendation: `esphome/micro-vorbis`.** It is an integer fork of Tremor's lowmem branch, it is
Apache-2.0 AND BSD-3-Clause (both permissive; the dual label reflects Tremor's BSD text and the
fork's Apache glue), it has an arena allocator, and its measured throughput on an S3 at 240 MHz is
**3.2 s of wall clock for 30 s of 48 kHz stereo ~128 kbit/s audio = 9.5× real time** with state in
PSRAM ([micro-vorbis README benchmark table](https://github.com/esphome-libs/micro-vorbis)).
`stb_vorbis` is the right fallback only if a single-file C dependency is worth more than 6× the CPU
and the loss of chaining.

### 3.3 Granule position for a live stream

Ogg pages carry a 64-bit `granule_position` (RFC 3533 §6 field 4: *"an 8 Byte field containing
position information. For example, for an audio stream, it MAY contain the total number of PCM
samples encoded after including all frames finished on this page … A special value of -1 (in two's
complement) indicates that no packets finish on this page"*
[RFC 3533 §6](https://www.rfc-editor.org/rfc/rfc3533.html#section-6)). For Vorbis it is specified in
[Vorbis I spec Appendix A.2](https://xiph.org/vorbis/doc/Vorbis_I_spec.html#x1-132000A.2):

* *"The granule position of these first pages containing only headers is zero."*
* *"The granule position of a page represents the end PCM sample position of the last packet
  completed on that page … A page that is entirely spanned by a single packet (that completes on a
  subsequent page) has no granule position, and the granule position is set to '-1'."*
* The last sample is **not** reliably the middle of the final block: if the current packet is a long
  block and the next a short one, the last decodable sample sits at
  `(3*long_block_length/4) - (short_block_length/4)`.
* **A non-zero first granule is legal and means something**: a positive start means the stream
  begins at a time offset ("This is a common case when connecting to the middle of broadcast
  stream"), and a *negative* inferred start "indicates that output samples preceding time zero
  should be discarded during decoding" — i.e. the container can crop the head of the stream.

**What this means for the server:** granule position is a *timing hint you do not need* for a
file-to-PCM pipeline, and a *correctness requirement you cannot ignore* at end-of-track (Vorbis
discards the final partial block, so a naive "decode everything" implementation emits a few ms of
padding per track). The cheap correct policy is: **decode all packets, count PCM, and trim the tail
using the last granule position**; ignore the head offsets only if you accept a constant sub-10 ms
offset at the start of each track. Do **not** try to make granule positions the station's master
clock — the station clock is the pacing loop, and Ogg granule is per-track.

---

## 4. Opus

### 4.1 The pushed-packet API, and the 48 kHz claim

`libopus`'s decoder is designed for exactly this shape
([`include/opus.h`](https://raw.githubusercontent.com/xiph/opus/main/include/opus.h)):

* `OpusDecoder *opus_decoder_create(opus_int32 Fs, int channels, int *error)` — *"Fs is the sampling
  rate and must be 8000, 12000, 16000, 24000, or 48000"*.
* `int opus_decode(OpusDecoder *st, const unsigned char *data, opus_int32 len, opus_int16 *pcm, int frame_size, int decode_fec)`
  with `frame_size` = *"Number of samples per channel of available space in `pcm`. If this is less
  than the maximum packet duration (120 ms; **5760 for 48 kHz**), this function will not be capable
  of decoding some packets."*
* Doc comment on `opus_decoder_create`, verbatim: **"Internally Opus stores data at 48000 Hz, so
  that should be the default value for Fs. However, the decoder can efficiently decode to buffers at
  8, 12, 16, and 24 kHz so if for some reason the caller cannot use data at the full sample rate, or
  knows the compressed data doesn't use the full frequency range, it can request decoding at a
  reduced rate."**
* `opus_decoder_get_size(channels)` exists so you can supply your own allocator
  (`opus_decoder_init`), and the docs warn *"future versions of this code may change the size, so no
  assumptions should be made about it."*
* Decoder CTLs available: `OPUS_GET_SAMPLE_RATE`, `OPUS_GET_LAST_PACKET_DURATION`,
  `OPUS_GET_FINAL_RANGE`, `OPUS_GET_GAIN`, `OPUS_GET_PITCH`, `OPUS_GET_BANDWIDTH`,
  `OPUS_RESET_STATE` ([`opus_defines.h`](https://raw.githubusercontent.com/xiph/opus/main/include/opus_defines.h)
  request codes 4027–4039, `OPUS_RESET_STATE 4028`).
* Packets are **stateful and ordered**: *"Opus is a stateful codec with overlapping blocks and as a
  result Opus packets are not coded independently of each other. Packets must be passed into the
  decoder serially and in the correct order for a correct decode."* For a server this is the reason
  a **track change must reset the decoder** (`OPUS_RESET_STATE`) rather than continue.

**Does a decoder need the original sample rate? No — and this is the crisp answer for the resampler
seam.** RFC 7845 §5.1 field 5, verbatim:

> "**Input Sample Rate** (32 bits, unsigned, little endian): This is the sample rate of the original
> input (before encoding), in Hz. **This field is _not_ the sample rate to use for playback of the
> encoded data.** … Regardless of the audio bandwidth, the reference decoder supports decoding any
> stream at a sample rate of 8, 12, 16, 24, or 48 kHz. **The original sample rate of the audio passed
> to the encoder is not preserved by the lossy compression.**"

and RFC 7845 §4 notes the granule unit:

> "The granule position of an audio data page is in units of PCM audio samples at a fixed rate of
> **48 kHz** … Therefore, the value in the granule position field always counts samples assuming a
> 48 kHz decoding rate."

So the decoder seam for Opus is: **always `opus_decoder_create(48000, N)` and feed 48 kHz PCM to the
shared resampler.** Do not try to honour `input sample rate`; it exists for tools that want to write
PCM back out at the original rate (RFC 7845 §5.1 says so explicitly). The existing
`mi_resample.c` — which already accepts an arbitrary in/out pair and bypasses when `in_hz == out_hz`
([`mi_resample.h`](../../../resources/mi_resample.h)) — is the right seam unchanged. If the station
rate is chosen as 48 kHz (the sibling notes' recommendation) then Opus needs **no resampling at all**.

### 4.2 Pre-skip — the constant-offset bug

RFC 7845 §4.2 defines it, and §5.1 field 4 gives the operational rule:

> "[Pre-skip is] the number of samples (at 48 kHz) to discard from the decoder output when starting
> playback, and also the number to subtract from a page's granule position to calculate its PCM
> sample position."

> "A 'pre-skip' field in the ID header signals the number of samples that SHOULD be skipped (decoded
> but discarded) at the beginning of the stream … **These samples need to be stored and decoded**, as
> Opus is an asymptotically convergent predictive codec … However, a player will want to skip these
> samples after decoding them."

and the position formula (RFC 7845 §4.3): `PCM sample position = granule position − pre-skip`.

Three things a server must get right:

1. **Decode, then discard.** You cannot skip *bytes* or *packets*; you must run them through the
   decoder and drop the first `pre-skip` PCM samples. Dropping packets instead corrupts the
   predictor state.
2. **Pre-skip is in 48 kHz samples regardless of `Fs`.** If you decode at 24 kHz, the discard count
   halves.
3. **End trimming is the mirror image** (RFC 7845 §4.4): the EOS page's granule position may indicate
   *less* audio than the final packet decodes to — *"The difference between these granule positions
   indicates how many samples to keep after decoding the packets that completed on the final page.
   The remaining samples are discarded."*

Ignoring (1) and (3) yields a track that is offset by a few ms at the start and a few ms long at the
end — inaudible in isolation, and **audible as a click at every track boundary**, which is exactly
what a seamless station is selling. This is the concrete form of "getting it wrong causes a small
constant offset".

### 4.3 Chaining (needed for track changes)

RFC 7845 §3 mandates the stream structure that makes this tractable: the ID header is **alone on the
first page** with the BOS flag; the comment header (the second mandatory packet) may span pages but
**must finish the page it completes on**; *"All subsequent pages are audio data pages"*. RFC 3533
§5 defines chaining: *"In chaining, complete logical bitstreams are concatenated … Each chained
logical bitstream MUST have a unique serial number within the scope of the physical bitstream"*
([RFC 3533 §5](https://www.rfc-editor.org/rfc/rfc3533.html#section-5)).

For a **player** mid-stream chaining is a hazard (the sibling notes already record that clients
"that expect ICY treat an in-stream Ogg metadata change (usually a new stream serial number) as
end-of-stream" — [`../radio-player-codecs.md`](../radio-player-codecs.md) §6). For a **server** it is
the opposite: the server *chooses* whether to chain, and §7 below argues it should not, for the
station's whole lifetime.

### 4.4 libopus on ESP32-S3

* **No Xtensa assembly upstream in `celt/`.** The repo's arch directories are
  `celt/{arm,mips,x86}` and `silk/{arm,mips,x86,xtensa}` — i.e. **CELT (music) has no Xtensa path;
  only SILK (speech) does** (verified from the GitHub contents API for
  [`xiph/opus`](https://github.com/xiph/opus/tree/main/celt) and
  [`silk`](https://github.com/xiph/opus/tree/main/silk)). This is why the `micro-opus` fork exists:
  it claims **"~17–25% faster decoding on ESP32/ESP32-S3"** via LX6/LX7 DSP instructions
  (`MULSH`, `CLAMPS`, `NSAU`, `MULA.DD.*`, `ROUND.S`) — [micro-opus README](https://github.com/esphome-libs/micro-opus).
* **Measured Opus decode on an S3** (micro-opus' own benchmark, 30 s clips, octal PSRAM, 240 MHz,
  floating-point default): CELT stereo 128 kbit/s **4.5 s → 6.6× real time**; SILK mono 10 kbit/s
  **1.3 s → 23.0× real time**. Fixed-point is 14 % *slower* for single-stream CELT and 18 % *faster*
  for SILK; with 4 concurrent tasks fixed-point wins slightly ([benchmark README](https://github.com/esphome-libs/micro-opus/blob/main/examples/decode_benchmark/README.md)).
* **The RAM catch is the pseudostack, not the codec.** `micro-opus` defaults to a
  **120 KB per-thread pseudostack** (configurable 60–240 KB) plus ~30–50 KB of state per instance,
  and says *"PSRAM is strongly recommended. Without it, multi-threaded usage may exhaust internal
  RAM."* The plain `opus_decoder_get_size()` computation confirms the small part: `OpusDecoder` header
  + `silk_Get_Decoder_Size()` + `celt_decoder_get_size(channels)`
  ([`src/opus_decoder.c`](https://raw.githubusercontent.com/xiph/opus/main/src/opus_decoder.c)).
  **Plan for ~150 KB of PSRAM per active Opus decoder, and keep the pseudostack in PSRAM.**
* Components: `esphome/micro-opus` **v0.4.1**, Apache-2.0 (state + pseudostack sizing above);
  `78/esp-opus` **v1.0.5**, **MIT**, idf ≥ 5.0 — *readme URL 404s on the component server, so its
  packaging (prebuilt lib vs source, targets) is UNVERIFIED*; `espressif/esp_audio_codec` v2.6.2,
  Espressif Modified MIT (see §2 caveat); `tangled-wires/iotta-opus` v0.5.6, **FSL-1.1-ALv2** — a
  non-free licence, disqualifying for a redistributable library.

---

## 5. WebM / Matroska

### 5.1 It is a published Standard now, and that is good news for citations

**RFC 9559, "Matroska Media Container Format Specification" (Standards Track, October 2024)** exists
— [RFC 9559](https://www.rfc-editor.org/rfc/rfc9559.html). It updates RFC 8794, the EBML
specification. So the framing is normative, not folklore.

EBML: an element is `Element ID` + `Element Data Size` + `Element Data`, both IDs and sizes encoded
as **Variable-Size Integers** whose length is signalled by a `VINT_WIDTH`/`VINT_MARKER` pair
(RFC 8794 §4). Two gotchas dominate a hand-rolled reader:

* **Sizes are not mandated short.** *"the VINT_DATA component of the Element Data Size is not
  mandated to be encoded at the shortest valid length"* (RFC 8794 §6.1).
* **Unknown-sized master elements are legal** — all-ones `VINT_DATA` — and *"Only a Master Element is
  allowed to be of unknown size"*, with the end determined by a schema-aware "which element can be a
  parent" rule set (RFC 8794 §6.2). A live/streamed WebM typically has an **unknown-sized Segment**,
  which means your parser must know the schema to find the end.

### 5.2 The minimum element set for an audio-only extractor

| Level | Element (ID) | Why it is needed |
|---|---|---|
| Root | **EBML header** (`0x1A45DFA3`) → `DocType`, `DocTypeVersion`, `EBMLMaxSizeLength` | Validate it is WebM/Matroska before parsing (WebM: *"The demuxer MUST only open webm DocType files"*) |
| Top | **Segment** (`0x18538067`) | Container; frequently unknown-sized |
| Top | **Info** (`0x1549A966`) → `TimestampScale` (`0x2AD7B1`, default 1 000 000 ns) | Converts block ticks to time; needed for any timestamp arithmetic |
| Top | **Tracks** (`0x1654AE6B`) → **TrackEntry** (`0xAE`) | Must be parsed *before* any frame can be decoded |
| TrackEntry | `TrackNumber` (`0xD7`), `TrackType` (`0x83`, 2 = audio), **`CodecID` (`0x86`)**, **`CodecPrivate` (`0x63A2`)**, `CodecDelay` (`0x56AA`), `SeekPreRoll` (`0x56BB`), `Channels` (`0x9F`), `SamplingFrequency` (`0xB5`), `BitDepth` (`0x6264`), `FlagLacing` (`0x9C`) | `CodecID` selects the decoder; `CodecPrivate` is the decoder's initialisation data; `CodecDelay` carries Opus pre-skip |
| Top | **Cluster** (`0x1F43B675`) → `Timestamp` (`0xE7`) | Frame grouping; cluster timestamps are the base for block timestamps |
| Cluster | **SimpleBlock** (`0xA3`), **BlockGroup** (`0xA0`) → `Block` (`0xA1`) | The frames themselves |
| Top | `Cues` (`0x1C53BB6B`), `SeekHead` (`0x114D9B74`) | Only for *seeking*; a sequential extractor can ignore both |

IDs above are from RFC 9559 §5.1 and its EBML-schema table at the end of the document; `CodecID`
`0x86` / `CodecPrivate` `0x63A2` are §5.1.4.1.21 / §5.1.4.1.22, `CodecDelay` §5.1.4.1.25.

**Block structure** (RFC 9559 §10.1 / §10.2): a `Block`/`SimpleBlock` body is a VINT **TrackNumber**,
a **signed 16-bit Timestamp** in Track Ticks, then 1 byte of flags — `SimpleBlock`'s byte is
`KEY | Rsvrd(3) | INV | LACING(2) | DIS`, and `Block`'s is `Rsvrd(4) | INV | LACING(2) | UNU` — then
the lace/frame data. `LACING` is `00` none, `01` Xiph, `11` EBML, `10` fixed-size (§10.3; lacing
"MUST NOT be used to store a single frame").

### 5.3 What each `CodecID` implies — and how WebM differs from Ogg

From the [Matroska codec mappings](https://www.matroska.org/technical/codec_specs.html):

| CodecID | Frames as stored | CodecPrivate / initialisation |
|---|---|---|
| **`A_OPUS`** | "using a similar encapsulation as the Ogg Encapsulation" — one Opus packet per frame | **`CodecPrivate` MUST be present and contain the Identification Header (RFC 7845 §5.1)** — i.e. the 19-byte `OpusHead`. `Channels` MUST equal the ID header's output channel count; `SamplingFrequency` MUST equal its input sample rate; **`CodecDelay` MUST be present, set to pre-skip in Matroska Ticks, `CodecDelay = pre-skip * 1e9 / 48000`**; `SeekPreRoll` SHOULD be 80 ms. **Note there is no `OpusTags` here** — the comment header is metadata and is simply absent |
| **`A_VORBIS`** | one Vorbis packet per frame | **`CodecPrivate` contains the first three Vorbis packets in order**, with Xiph-style lacing prefix: *"Byte 1: number of distinct packets #p minus one … This MUST be '2' for current Vorbis headers. Bytes 2..n: lengths of the first #p packets, coded in Xiph-style lacing. … followed by the Identification Header … the Comment Header … the Setup Header"* |
| **`A_MPEG/L3`** | "Frames correspond to Audio Frames of a Layer III bitstream" | **none** ("Initialization: none") — so an MP3-in-Matroska is byte-identical frames to an MP3 file, and the existing `dr_mp3` frame-sync path applies unchanged |
| **`A_FLAC`** | FLAC framing per **RFC 9639 §10.2** | *"All FLAC data before the first audio frame"* — the `STREAMINFO` block and any metadata blocks |
| **`A_PCM/INT/LIT`** | raw little-endian integer PCM | **none**; bit depth "MUST be read and set from the `BitDepth` element"; *"Audio samples MUST be considered as signed values, unless the audio bit depth is 8 which MUST be interpreted as unsigned values"* |

So the answers to "does WebM store Vorbis/Opus differently from Ogg":

1. **Yes, and in a way that changes your decoder seam.** In Ogg, the three Vorbis headers arrive
   *interleaved in the byte stream* and the decoder discovers them by parsing pages. In Matroska they
   are handed to you **out of band, pre-laced, in one `CodecPrivate` blob**. Similarly Opus's
   `OpusHead` comes from `CodecPrivate`, not from a BOS page. A decoder API that only accepts pushed
   Ogg bytes (like `micro-vorbis`'s `OggVorbisDecoder` or `micro-opus`'s `OggOpusDecoder`) therefore
   **cannot** be fed a WebM track without a shim — you need the *packet-level* API
   (`opus_decode` directly; Tremor's `vorbis_synthesis_*`) and to synthesise the headers yourself.
2. **Lacing replaces Ogg's page lacing.** Both containers lace, but Matroska laces *inside* a Block
   (three modes) while Ogg laces across pages (segment table of 0–255 values, RFC 3533 §6). One Opus
   *frame* per Matroska frame is the mapping; one Ogg *packet* per Opus frame is the Ogg mapping.
3. **Timestamps change units.** Ogg Opus uses a 48 kHz granule count; Matroska uses signed 16-bit
   Track Ticks plus cluster `Timestamp`, with `TimestampScale` converting to ns. `CodecDelay` carries
   the pre-skip so the decoder seam can be fed the same `pre-skip` value — but the *plumbing* is
   entirely different.

### 5.4 Existing parsers, with real numbers

| Parser | Language | Size | Licence | Fit |
|---|---|---|---|---|
| **[mozilla/nestegg](https://github.com/mozilla/nestegg)** | C | `src/nestegg.c` = **110 108 B / 4 269 lines** | **ISC-style** (*"Permission to use, copy, modify, and distribute this software for any purpose with or without fee is hereby granted"*) | **Best fit.** No stdio dependency: `#include <string.h>` and `calloc` only; all I/O via a caller-supplied `nestegg_io { read, seek, tell, userdata }`. It is the WebM demuxer Firefox ships, and it already enumerates `NESTEGG_CODEC_OPUS/VORBIS/FLAC/MP3/PCM`. **It requires seek** (io must supply `seek`) |
| **[webmproject/libwebm](https://github.com/webmproject/libwebm)** `mkvparser` | C++ | `mkvparser.cc` = **194 446 B / 8 108 lines** | BSD-3-Clause | Authoritative and complete, but C++ exceptions/STL-oriented and 2× the size; heavier fit for a C component |
| **libebml** (Matroska-Org) | C++ | `EbmlElement.h` alone is 34 KB | **LGPL-2.1** → *"GNU LESSER GENERAL PUBLIC LICENSE Version 2.1"* | A generic EBML DOM, not a demuxer. **LGPL disqualifies it** for a permissively licensed component |
| `Wykerd/mkv-parser` | C | ~3 KB | MIT | "Lightweight matroska & webm file parser" — nothing like enough to demux (no lacing/blocks at scale); *not fetched, UNVERIFIED* |
| `RSATom/mkvparse` | — | — | — | **Not fetchable** (raw URL 404s); likely not the repo I intended. UNVERIFIED |
| `Izaron/matroska-subtitles` | C++ | ~60 KB | none declared | Subtitle-oriented. UNVERIFIED |

### 5.5 The realistic cost of hand-writing a seekable audio-only extractor

Honest estimate, from the element set in §5.2 and the structure of `nestegg` (4 269 lines for a
*complete* demuxer): an audio-only extractor needs

* EBML VINT reader with unshortened sizes and unknown-sized masters — ~150–250 lines;
* schema-aware descent with a **parent-element whitelist** (needed only to terminate unknown-sized
  masters) — ~100–200 lines;
* `Segment` → `Info`/`Tracks`/`Cluster` walking, `TrackEntry` selection and `CodecPrivate` capture —
  ~200–300 lines;
* `SimpleBlock` + `BlockGroup`/`Block` parsing with all three lacing modes — ~200–300 lines;
* timestamp arithmetic (`TimestampScale`, cluster + signed block offsets, `CodecDelay`) — ~50–100;
* resync/error recovery, multi-track skipping, Opus/Vorbis/MP3/FLAC/PCM dispatch — ~200–400.

**Total ≈ 900–1 400 lines of new C, most of it bit-parsing that a fuzzer should be pointed at.**
That is not large in absolute terms; it is large *relative to the benefit* (§5.6), and every one of
those lines is a security boundary, because the input is an untrusted file off a card. `nestegg`
adapter work, by contrast, is roughly **300–500 lines**: implement `nestegg_io` over FatFS, pick the
audio `TrackType`, pull `CodecPrivate` via `nestegg_track_codec_data`, read packets with
`nestegg_read_packet` + `nestegg_packet_track` + `nestegg_packet_data`, and skip video packets.

### 5.6 Video tracks, and legality

WebM **commonly** carries VP8/VP9 alongside audio: the WebM container guidelines say *"Video codec
SHOULD be VP8 or VP9 … The Codec ID SHOULD be 'V_VP8' or 'V_VP9'"* and, separately, *"Audio codec
SHOULD be Vorbis or Opus"* / *"The Codec ID SHOULD be 'A_VORBIS' or 'A_OPUS'"*
([WebM Container Guidelines](https://www.webmproject.org/docs/container/)). Audio-only files use
`audio/webm`; the guidelines note `canPlayType('audio/webm')` support expectations.

An audio-only implementation **must skip, not decode, video**: in `nestegg` that is
`nestegg_track_type(t) == NESTEGG_TRACK_AUDIO` before touching the packet; in a hand-written parser
it means ignoring any Block whose VINT track number is not the chosen audio track, and never
allocating per-frame buffers for video. Two practical consequences:

* **Seeking gets worse, not better.** WebM seeking is defined through `Cues` (keyframe index), and
  *"Seeking will be disabled if the webm file does not have a key frame Cues element."* An
  audio-only extractor does not need Cues for sequential playback, but it *does* need them if the
  server ever wants to start mid-file.
* **Legality is not affected by skipping video.** No format licence attaches to "the VP8 bytes I
  never read"; the licensing questions for this component are the AAC/Opus/MP3 patent questions
  already settled in [`s1-aac-licensing.md`](s1-aac-licensing.md) and
  [`../radio-player-codecs.md`](../radio-player-codecs.md) §4.2. **Cross-referenced, not
  re-researched.** The one thing worth stating: a WebM file's *audio* is Opus or Vorbis, whose
  licensing is already in scope and already clean, so admitting WebM adds **no new royalty
  question** — its cost is purely code and bug surface.

---

## 6. The architecture: many inputs, many outputs, one stream

### 6.1 What has to stay fixed, and at what layer

Today the design fixes one thing — **PCM at `out_hz`, stereo int16** — and lets the encoder layer be
absent (`mini_icecast.c` broadcasts resampled PCM) or added. With a real output codec, the invariant
splits into three layers with three different lifetimes:

| Layer | Must be fixed | Lifetime | Why |
|---|---|---|---|
| **PCM station format** | sample rate, channels, sample format | whole station run (runtime-configurable at start) | Resampler state and the `pcm_cb` visualiser contract; also the seam that lets any input feed any output |
| **Codec** (MP3 vs Opus) | the actual codec | **whole station run, decided at build** | A codec stream is a *syntax*; MP3 frame headers and Opus TOC bytes have nothing in common. There is no "MP3-or-Opus" stream |
| **Encoder framing** | frame duration/bitrate, and (for Ogg) page/serial layout | whole stream | MP3's 1152-sample granule vs Opus's 2.5 ms-multiple packets |

**"MP3 and/or Opus" therefore cannot mean runtime-switchable in one stream.** It means one of:

* **build-time choice** — a Kconfig choice selecting exactly one encoder backend, bound through the
  same vtable the decoder seam already uses. *Recommended.*
* **runtime config with a restart** — the codec is read from config/NVS at boot; changing it
  restarts the stream. Same thing, later binding.
* **two ports / two mount points** — e.g. `/stream.mp3` and `/stream.opus`, each with its own
  encoder and its own `pacing` state. This *is* coherent HTTP (content negotiation via URL, not via
  `Accept`), and it doubles encoder state per active format. It is also the only way to give *both*
  families of clients one host. Cost: two encoder instances resident, and one of them is idle when
  only one client family is connected.

The vtable shape is already prescribed for licensing reasons by
[`../01-library-architecture.md`](../01-library-architecture.md) §4; the encoder should reuse it
rather than invent a second seam.

### 6.2 Can the format change mid-stream?

**MP3: no, and it is worse than "no".** A bare MP3 stream is a sequence of self-describing frames —
there is no container layer in which to place a "here comes Opus" marker, and any such marker is
just garbage that the decoder's sync search skips. Even an MP3→MP3 *rate* change is only survivable
because each frame carries its own sample-rate index: the decoder resyncs and follows, but the PCM
timeline is discontinuous and the resampler must be rebuilt mid-stream. So an MP3 station is
**one codec, and effectively one sample rate, forever**.

**Ogg: yes in the spec, no in practice for radio.** Ogg chaining (RFC 3533 §5) exists precisely to
concatenate logical bitstreams, each with a unique serial — the container-level mechanism to signal
a format (or even codec) change in-band. RFC 7845 §7.2 is titled **"Continuous Chaining"** and is
motivated by radio: *"In some applications, such as Internet radio, it is desirable to cut a long
stream into smaller chains, e.g., so the comment header can be updated … The drawback of this
approach is that it creates a small discontinuity at the boundary due to the lossy nature of Opus"*,
with a 5-step recipe (encode the last frame with prediction disabled, set the last page's granule
near the end, **begin the second segment with a copy of that last frame**, set the second stream's
`pre-skip` to join them, continue the encoder without reset).

But the *client* side is the blocker, and the sibling notes already established it: clients that
expect ICY treat a new Ogg serial as end-of-stream —
[`../radio-player-codecs.md`](../radio-player-codecs.md) §6, citing the Icecast list thread. So
chaining is a spec-sanctioned mechanism that this component **should not use for track changes**.
Track changes should happen *inside one logical bitstream*: decode, resample, encode continuously,
and let the track boundary be nothing more than a change of source.

### 6.3 Where the fixed format actually constrains the decoders

Because PCM is fixed, every input decoder must produce stereo int16. That is already
`mi_decode.c`'s job (it duplicates mono and takes the first two of >2 channels — `mi_decode.c:102-105`),
and it is the one place where the existing code is exactly right for the expanded set. Two additions
are needed:

* **Vorbis/Opus can change channel count or rate per stream** — but a *track* is a stream here, so
  the decoder is re-created per track, and only `mi_dec_hz()` has to be re-read. `micro-vorbis`
  offers downmix-to-mono/stereo and per-role channel selection
  ([README](https://github.com/esphome-libs/micro-vorbis)), which removes the need for the existing
  ad-hoc channel folding for Vorbis.
* **Opus is always 48 kHz** (§4.1), Vorbis carries its own `audio_sample_rate`, MP3 its own frame
  rate — so `mi_dec_hz()` remains meaningful and the resampler stays the only rate-aware component.

---

## 7. Pass-through: the biggest CPU win, and where it applies

**The premise is right and the payoff is bigger than it looks.** Decode is *cheap*; encode is
*expensive*. Measured on an S3 at 240 MHz:

| Stage | Measured cost |
|---|---|
| MP3 decode (128 kbit/s, 48 kHz stereo) | **13.1× real time** (7.6 % of a core) |
| Vorbis decode (≈128 kbit/s, 48 kHz stereo) | **9.5× real time** (10.5 %) |
| Opus decode (CELT 128 kbit/s, stereo) | **6.6× real time** (15.2 %) |
| Opus **encode** (Espressif, complexity 0, 90 kbit/s) | **24.9 % CPU → 4.0× real time** |
| MP3 **encode** (shine, 128 kbit/s) | **~1.8× real time → ~53 % of a core** |

Decode figures: [micro-mp3](https://github.com/esphome-libs/micro-mp3),
[micro-vorbis](https://github.com/esphome-libs/micro-vorbis),
[micro-opus](https://github.com/esphome-libs/micro-opus) benchmarks. Encode figures: Espressif's
`esp_audio_codec` S3R8 table and the shine port, both tabulated in
[`s2-encoder-feasibility.md`](s2-encoder-feasibility.md) §2–§5.

So for an **Opus station**: decode+encode of an Opus file costs ~15 % + ~25 % ≈ **40 % of a core**,
and pass-through removes **all of it**. For an **MP3 station**: decode+encode of an MP3 file costs
~8 % + ~53 % ≈ **61 % of a core**, and pass-through removes all of it — on the *cheapest encoder
available*, which is the one that is barely real-time. **Pass-through is what makes a music-grade
MP3 station comfortable rather than marginal on an S3.**

**But it does not compose with "resample everything to one station format", and that is the design
decision.** Pass-through requires the file to be *already* in the wire format, which means:

| Pass-through case | Applies when | Cost to implement |
|---|---|---|
| **MP3 → MP3** | file's `sampleRate == out_hz`, `channels == 2`, and the file is not free-format; strip ID3v2/Xing and emit frames | ~150–250 lines: frame walk + `frame_bytes` copy. `dr_mp3` already exposes `drmp3_get_frame_size`/frame headers |
| **Ogg Opus → Ogg Opus** | file's `OpusHead` channels/family match the station's; pre-skip and granule positions must be **rewritten** (the station is one continuous granule timeline, not per-track) | ~300–500 lines: Ogg page re-emit, serial rewrite, granule offsetting, `OpusHead`/`OpusTags` synthesis |
| **Ogg Vorbis → Ogg Vorbis** | only if the station's output codec were Vorbis — **excluded**, the outputs are MP3 and Opus | n/a |
| **FLAC → FLAC** | only if the output were FLAC — excluded (though `micro-flac` *can* encode) | n/a |
| **Anything → MP3** | requires decode (MP3 has no cheap transcode) | n/a |

**So the honest quantification is:** pass-through applies to exactly the files that already match the
station codec *and* rate *and* channel count, and its value scales with how homogeneous the SD card
is. A card of 44.1 kHz stereo MP3s served as a 44.1 kHz MP3 station: **pass-through on every track,
~61 % of a core saved.** A card of mixed 48 kHz Opus and 44.1 kHz MP3 served as one station format:
pass-through on the matching subset only, and the resampler's `in_hz == out_hz` bypass
([`mi_resample.h`](../../../resources/mi_resample.h)) is already the seam that decides it.

**Recommended shape:** make the decoder seam **`mi_dec_open()` → either a decoder or a
pass-through `frame_source`**, with the same `read()`-shaped interface — and note that pass-through
does **not** need seek at all, which is a strict simplification over the present file-shaped API.
Two design rules make this safe:

1. **Never splice a codec change.** Pass-through and transcoded tracks must not be mixed within one
   stream *if* the container would have to change; MP3→(transcoded)MP3 is fine because both ends are
   bare MP3 frames at the same station rate. Opus→(transcoded)Opus is fine for the same reason,
   provided the Ogg serial and granule timeline are managed by the station, not copied from files.
2. **Choose the station rate to maximise the hit rate.** If the library is mostly 44.1 kHz, a
   44.1 kHz station is the one that makes pass-through fire; the sibling note's recommendation of
   48 kHz is better for Opus *decoding* (no resample) and worse for MP3 *pass-through*. That is a
   genuine trade and it should be a Kconfig/config value, not a hard-coded default.

**Do not treat this as an optimisation to add later.** It changes the decoder interface (from
"always PCM" to "PCM or frames") and the encoder interface (from "always encode" to "encode or
bypass"), and retrofitting either is a rewrite of both seams.

---

## 8. Flash and RAM: is six decoders + two encoders + containers plausible?

**Flash: yes, comfortably, and it is not the constraint.** ESP-IDF's stock partition tables are
[`partitions_singleapp.csv`](https://raw.githubusercontent.com/espressif/esp-idf/v5.5.1/components/partition_table/partitions_singleapp.csv)
(**factory app = 1 MB**) and
[`partitions_two_ota.csv`](https://raw.githubusercontent.com/espressif/esp-idf/v5.5.1/components/partition_table/partitions_two_ota.csv)
(**two 1 MB OTA slots**). Every decoder in §2 is a small C library: `dr_mp3.h` is 209 KB of *source*
and its compiled decoder is a few tens of KB; `micro-opus`'s own benchmark reports **~100 KB of code**
plus 498 KB of test audio (`Flash ~640KB | 100KB code + 498KB music + 38KB speech`,
[benchmark README](https://github.com/esphome-libs/micro-opus/blob/main/examples/decode_benchmark/README.md)).
Six decoders, one encoder and one container parser land in the low hundreds of KB — an 8 MB part with
1 MB app slots has room, and an 8 MB part can be repartitioned generously anyway.

**RAM: this is the constraint, and it is why build-time selection is the right answer, not merely a
licensing convenience.** The per-instance numbers above (§2) are for *one* active decoder, and the
`s2`/`radio-player-codecs` working budget already reserves 150–250 KB of the 512 KB for Wi-Fi, lwIP,
the HTTP server, ring buffers and stacks. Now add: Vorbis ~76 KB (mostly PSRAM), Opus **~30–50 KB
state + 120 KB pseudostack** (all PSRAM-preferred), FLAC 89.4 KB, MP3 22.8 KB, plus an encoder
(shine's fixed tables, Opus encoder state, or an Ogg muxer). That sum is fine **if the resident set
is one decoder plus one encoder**, and it is not obviously fine if every decoder is linked and
instantiated. PSRAM makes it survivable, but the sibling notes' warning stands: PSRAM is bandwidth-
limited and cache-sensitive, and decoder scratch in PSRAM is unmeasured
([`../radio-player-codecs.md`](../radio-player-codecs.md) §5.3 — **UNVERIFIED** there and still
unmeasured here).

**Therefore: Kconfig, at two levels.**

* One **Kconfig choice for the output codec** (MP3 | Opus/Ogg), selecting exactly one encoder.
* Per-format **`bool` Kconfig options** for input codecs (`XTAPE_IN_MP3`, `_VORBIS`, `_OPUS`,
  `_FLAC`, `_WAV`, `_WEBM`), each defaulting `y` except WebM, with `depends on` pulling the registry
  component so a disabled codec is not even downloaded.
* **Runtime probing stays**, but only *within* the compiled set: `mi_dec_open()` keeps its
  `path`-and-extension signature for now, and should grow a magic-byte probe (`OggS` + `vorbis`/`OpusHead`
  for Ogg, `fLaC`, `RIFF…WAVE`, `ID3`/`0xFFEx` for MP3, `0x1A45DFA3` for WebM) because extensions
  lie. The `DecoderSource` pattern in `micro-decoder` — a registered set of per-format decoders with
  a common interface — is the shape to copy, and the existing
  [`mi_decode.h`](../../../resources/mi_decode.h) interface is already close enough that the change
  is additive rather than a rewrite.

---

## 9. VERIFIED vs UNVERIFIED

**VERIFIED (spec text, decoder source, or the registry API):**

1. Vorbis I requires **three header packets, in order**; end-of-packet while reading the **first or
   third** renders the stream undecodable; end-of-packet in the **comment** header is non-fatal
   (Vorbis I §1.3.1, §4.2).
2. The **setup header** carries codebooks, floors, residues, mappings and modes and ends in a framing
   bit (§4.2.4) — nothing decodes before it.
3. Vorbis granule position semantics, the `-1` "no packet finishes" value, header pages at granule 0,
   and the "last decodable sample is not the middle of the block" caveat (Vorbis I Appendix A.2;
   RFC 3533 §6 field 4).
4. `tremor` is integer-only and decode-only, BSD-3-Clause (its README and COPYING).
5. `libvorbis` has no fixed-point path and is written in `float` (source spot-check of
   `lib/block.c`, `lib/floor1.c`, `lib/res0.c`).
6. `stb_vorbis` is dual MIT/public-domain, is float-only, has a documented push API with
   `VORBIS_need_more_data`, and documents four limitations including **"cannot concatenate multiple
   vorbis streams"** and "~150KB" maximal usage (its own header).
7. `esphome/micro-vorbis` v0.1.0 is a Tremor `lowmem` fork, Apache-2.0 AND BSD-3-Clause, 9.5× real
   time on S3, ~76 KB total for 48 kHz stereo (registry API + README).
8. `opus_decoder_create` accepts 8000/12000/16000/24000/48000; `opus_decode`'s `frame_size` is "max
   duration … 120ms; 5760 for 48kHz"; **"Internally Opus stores data at 48000 Hz"**; packets are
   stateful and must be passed serially; `opus_decoder_get_size` exists and its size is explicitly
   unstable (opus.h).
9. RFC 7845 §5.1: **input sample rate "is _not_ the sample rate to use for playback"**, the original
   rate "is not preserved by the lossy compression", and the recommended playback procedure is
   "if the hardware supports 48 kHz playback, decode at 48 kHz".
10. RFC 7845 §4/§5.1: pre-skip is **in 48 kHz samples**, must be **decoded and then discarded**, and
    granule position counts 48 kHz samples; `PCM sample position = granule − pre-skip` (§4.3);
    end trimming per §4.4; the ID header is alone on the BOS page and the comment header must finish
    its page (§3).
11. RFC 7845 §7.2 defines **continuous chaining** for internet radio, with the 5-step recipe
    including copying the last frame and rewriting pre-skip.
12. RFC 3533 §5 defines chaining and requires a unique serial per chained logical bitstream; §6 gives
    the page header layout, `header_size = number_page_segments + 27`, and the three `header_type`
    flags.
13. **RFC 9559** is the Matroska specification (Oct 2024) and **RFC 8794** is EBML; sizes need not be
    shortest-encoded, and unknown-sized **master** elements are legal.
14. The Matroska element set, `SimpleBlock`/`Block` layouts, the four `LACING` modes, and
    `CodecID = 0x86` / `CodecPrivate = 0x63A2` / `CodecDelay = 0x56AA` (RFC 9559 §10.1–§10.3,
    §5.1.4.1.21–25).
15. `A_OPUS` requires `CodecPrivate` = the RFC 7845 identification header and `CodecDelay = pre-skip
    × 1e9 / 48000`; `A_VORBIS` requires all **three** Vorbis packets pre-laced in `CodecPrivate`;
    `A_MPEG/L3`, `A_FLAC` and `A_PCM/INT/LIT` initialisation rules (matroska.org codec mappings;
    A_FLAC defers to RFC 9639 §10.2).
16. WebM: `DocType` SHOULD be `webm`; video SHOULD be `V_VP8`/`V_VP9`; audio SHOULD be `A_VORBIS` or
    `A_OPUS`; **"The demuxer MUST only open webm DocType files"**; seeking needs `Cues`.
17. **No WebM/Matroska/EBML component exists** on the ESP Component Registry (`q=webm|matroska|mkv|ebml`
    → `ComponentNotFoundError`); `esp_extractor` v1.0.7 covers MP4/TS/FLV/WAV/OGG/AVI/CAF only.
18. **`esp_muxer` v1.2.3 supports OPUS in OGG** — which corrects the "no documented Ogg muxer" claim in
    `s2` — but both `esp_muxer` and `esp_audio_codec` are Espressif Modified MIT.
19. `nestegg` is 110 108 B / 4 269 lines of C, ISC-style, `calloc`+`string.h` only, I/O via
    `nestegg_io {read, seek, tell}` (source and header).
20. `libwebm`'s `mkvparser.cc` is 194 446 B / 8 108 lines, BSD-3; `libebml` is **LGPL-2.1**.
21. `xiph/opus` has `celt/{arm,mips,x86}` and `silk/{arm,mips,x86,xtensa}` — **no Xtensa CELT path**.
22. Measured decode throughput on S3: MP3 13.1×, Vorbis 9.5×, Opus CELT 6.6× real time; Opus encode
    24.9 % CPU (Espressif) and shine MP3 ~1.8× real time — all as tabulated in §3 of `s2`.
23. ESP-IDF's stock single-app and two-OTA partition tables give a **1 MB** app slot each.

**UNVERIFIED / LIKELY — do not present as fact:**

1. **Tremor's `lowmem` branch** as a distinct upstream branch (the `micro-vorbis` README asserts the
   fork; I did not fetch the branch).
2. **`78/esp-opus` v1.0.5 packaging** — the component server's readme URL 404s, so whether it ships
   prebuilt per-target blobs and which targets are supported is unknown. Its MIT licence *is*
   verified from the registry API.
3. **`stb_vorbis`'s practical RAM on an S3** — "~150KB" is the author's desktop test files; the
   `setup_memory_required` query is the only honest way to know for a given file, and nobody has run
   it here.
4. **`Wykerd/mkv-parser`, `RSATom/…mkvparse`, `Izaron/matroska-subtitles`** — sizes/licences from the
   GitHub search API only; none read.
5. **The 900–1 400-line estimate for a hand-written Matroska extractor** is my construction from the
   required element set and `nestegg`'s size, not a measurement of any existing artefact.
6. **Whether `nestegg` builds and runs on ESP32-S3 at all** — no IDF component, no upstream ESP32
   support, and the `calloc`-per-element allocation pattern may need an arena. Untested.
7. **Whether an 8 MB S3 board's chosen partition scheme actually gives the app enough room** — the
   author's part is an 8 MB flash but the partition table is not in `resources/`, so I verified only
   IDF's *stock* 1 MB app slots.
8. **Any PSRAM-vs-internal-RAM penalty for the new decoders** — `micro-vorbis` claims "negligible
   penalty" on S3 for state+demuxer in PSRAM, and `micro-opus` "strongly recommends" PSRAM, but
   neither is a measurement under this server's load (decoder + encoder + HTTP server + Wi-Fi).
9. **Pass-through savings when both decode and encode are bypassed** is arithmetic from the measured
   stage costs, not a measured end-to-end station.
10. **Ogg chaining behaviour of the specific clients** the server will face — the client-side claim
    rests on the sibling note's Icecast-list citation, which is about metadata updates, not
    explicitly about chained *codec* changes.
