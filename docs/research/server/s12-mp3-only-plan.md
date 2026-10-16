# MP3-only server: the plan

**Final scope, replacing all earlier expansions:**

- **Input: MP3, FLAC, WAV** (exactly the three already implemented in `resources/mi_decode.c`)
- **Output: MP3 only**
- **Role: the ESP32 is the server** — clients connect to it

Everything researched for Opus, Vorbis, Ogg, WebM, and AAC is **out of scope**. This document
is the whole plan. Earlier files remain for the reasoning trail, but nothing in them beyond
this scope is needed.

Written 2026-10-16. Claims marked **[verified]** were checked by running code or reading the
file directly, and the commands are given at the end.

## 1. Why this scope is the *easy* one

The narrowing removes every hard problem the research had accumulated:

| Problem that existed | Status now |
|---|---|
| Endless-WAV rejected by Apple, broken in VLC master | **Gone.** MP3 is a self-delimiting frame stream; no length sentinel, no container. |
| ICY metadata meaningless / destructive in a PCM stream | **Gone.** ICY metadata is an MP3-only feature in Icecast (`metaint` appears only in `format.c` / `format_mp3.c`); it is *correct* here. |
| Ogg container to hand-write (~132 lines + traps) | **Not needed.** MP3-in-Ogg does not exist as a usable format. |
| Opus complexity/CPU ceiling, 120 KB pseudostack | **Not needed.** |
| Six decoders + two encoders in flash; RAM pressure | **Reduced** to three decoders + one encoder. |
| AAC patent licensing | **Not needed** (MP3 is royalty-free). |
| WebM/Matroska demuxer | **Not needed.** |

Bandwidth also improves 11×: 176 400 B/s (PCM 44.1 k stereo) → **16 000 B/s** at 128 kbps,
i.e. 1.41 Mbit/s → **0.128 Mbit/s** per client, and 635 MB/h → 57.6 MB/h.

**And it fixes the client-compatibility cliff**: MP3 over HTTP with ICY is the
best-supported live-audio path in existence — browsers, VLC, iOS, car head units, and
hardware internet radios all handle it. The format radio hardware actually supports is MP3.

## 2. What already works and stays

`resources/` already does most of the pipeline. Keep, essentially unchanged:

- **`mi_decode.c/.h`** — MP3/FLAC/WAV → stereo int16 at the file's rate. Its interface
  (`mi_dec_open` / `mi_dec_hz` / `mi_dec_read` / `mi_dec_close`) is already right.
- **`mi_resample.c/.h`** — resamples any input rate to the station rate. Verified sound
  earlier: unity passband, rate-exact with no accumulating drift, ~0.06 % of a host core.
- **The chunker + pacing model** in `mini_icecast.c` — fixed output cadence, starvation
  rebase. This becomes *more* important with an encoder in the path.
- **The HTTP/ICY server** — `Icy-MetaData: 1` opt-in, `icy-metaint` advertisement,
  `StreamTitle='...';` block emission, client fan-out, burst ring.
- **`main.c`'s Wi-Fi bring-up**, including `esp_wifi_set_ps(WIFI_PS_NONE)`.

## 3. What must change: the output stage

The server currently emits **raw PCM inside an endless WAV container**
(`wav_header()` at `mini_icecast.c:181`, `Content-Type: audio/wav` at `:224`). Three edits:

1. **`Content-Type: audio/wav` → `audio/mpeg`.**
2. **Delete the WAV header emission** (`wav_header()` and its call). MP3 needs no header —
   the stream *is* frames.
3. **Insert an MP3 encode step** between the resampler's output and `broadcast()`.

Everything downstream — `client_push()`'s `metaint` splitting, `send_meta()`'s 16-byte-block
framing, the burst ring, the pacing — works unchanged. **The bug to avoid**: `metaint` must be
counted over *encoded* bytes, which is what `client_push()` already does, because it operates
on the byte buffer handed to `broadcast()`. So the only real insertion point is
`chunk_append()` → `emit_chunk()`: encode before broadcasting.

### Encoder choice: `shine`, vendored as a separate component

There is **no MP3 encoder component in the ESP Registry** (searching `shine` returns
`ComponentNotFoundError`), so it must be vendored. **[verified]** I built `toots/shine` from
source and ran it:

| Property | Verified value |
|---|---|
| State size | ~97.6 KB (95 896 B `shine_global_config` + 4 096 B bitstream buffer) |
| Frame size | 1152 samples = **26.12 ms** @ 44.1 kHz, 24.00 ms @ 48 kHz — `shine_samples_per_pass()` returns 1152 |
| Latency | **One pass** — the first call already emits a frame, so no startup buffering beyond one frame, plus ~11.6 ms polyphase filterbank content delay |
| Bitrate control | **CBR only** — zero `vbr` references anywhere in `src/` |
| Modes | Stereo, joint stereo, dual channel, **mono** |
| Rate/bitrate validity | `shine_check_config()` — 44.1/48/32 kHz (MPEG-I): **32–320 kbps**; 22.05/24/16 kHz (MPEG-II): 32–160; 11.025/12/8 kHz (MPEG-2.5): 32–64 |
| Output correctness | First frame begins `ff fb` = MPEG-1 Layer III sync **[verified]** |
| Actual bitrate | Exactly the target: 41 796 B for 2.612 s = **128.0 kbps**; 57 600 B for 2.400 s = **192.0 kbps** **[verified]** |
| Mono | Works, and produces **identical byte counts** to stereo at the same bitrate **[verified]** |

**Recommended station format: 44.1 kHz, 128 kbps CBR, joint stereo** — the universal
compatibility point, and valid per `shine_check_config(44100, 128) == 3` (MPEG-I)
**[verified]**. 48 kHz/192 kbps is available if fidelity matters more than reach.

Note the header's own ASCII table in `layer3.h` is easy to misread — its MPEG-I column spans
32–320 and its MPEG-2.5 column caps at 64 — so **call `shine_check_config()` at runtime rather
than trusting the table**, and log the result at startup.

### API to call

```c
/* from layer3.h, upstream */
shine_t  shine_initialise(shine_config_t *config);   /* config is COPIED; cannot change later */
int      shine_samples_per_pass(shine_t s);          /* 1152 */
unsigned char *shine_encode_buffer(shine_t s, int16_t **data, int *written);
unsigned char *shine_encode_buffer_interleaved(shine_t s, int16_t *data, int *written);
void     shine_close(shine_t s);
int      shine_check_config(int freq, int bitr);
```

The returned buffer is library-owned and **valid only until the next call** — so encode, then
immediately hand the bytes to `broadcast()` (or memcpy them). That lifetime rule is the one
easy way to introduce a use-after-free here.

## 4. The licence obligation, stated plainly

`shine` is **LGPL-2.0-only** **[verified: `COPYING` is "GNU *Library* General Public License,
Version 2, June 1991", and the string "or later" occurs zero times]**. Note this is *plain*
LGPL-2.0 — `toots/shine` ships only `COPYING`, with no `LICENSE.txt` carrying extra conditions
(as LAME does) and no per-file headers.

LGPL-2.0 §6 **[verified by reading the clause]** permits distributing a work that uses the
library **"under terms of your choice"**, provided you:

1. give **prominent notice** that the Library is used and is covered by this License, and
   supply a copy of the licence; and
2. either accompany it with the Library's **complete corresponding source** *and* the
   work's object/source code **so the user can relink against a modified Library**, or supply
   a written offer to do so.

**How to satisfy this cleanly, without licensing your own code under LGPL:**

- Keep `shine` as a **separate component directory**, vendored as unmodified source — never
  copy its files into your own source tree, never statically pre-build it into a blob.
- Because it ships as source in the consumer's build tree, the relink path in §6a exists
  naturally: the user can modify shine and rebuild.
- Ship `LICENSES/LGPL-2.0.txt` plus a `THIRD_PARTY_NOTICES` entry naming shine.
- Your own component's licence is unaffected — §6 is exactly the clause that permits this.

This is the standard, defensible arrangement for an LGPL encoder in an ESP-IDF component, and
it is why `shine` must not simply be pasted into `src/`.

**Do not use `fknrdcls/mp3_shine_esp32`** — it is **GPL-3.0** **[verified]** and would impose
GPL on the whole project. If its Xtensa optimisations are ever wanted, they must be obtained
from a differently-licensed source or written fresh.

## 5. Defects to fix while you are in here

These are independent of the encoder and were found by reading the code:

1. **The accept loop blocks on client setup.** `accept_task` calls `handle_new(fd)` inline,
   and `handle_new` does a blocking header read with a **2 s** `SO_RCVTIMEO`. One client that
   connects and sends nothing stalls all new connections. Move connection setup off the accept
   thread, and raise the `listen(ls, 2)` backlog.
2. **`"icy-name: %s"` has a space after the colon**; real Icecast emits none. Harmless to
   permissive parsers, but this server is a natural loopback fixture, so emit canonical
   headers.
3. **`max_clients` documented 1..4, silently coerced to 2** (`mini_icecast.c:395`) while the
   array is sized 4 — either honour the value or reject it with a diagnostic.
4. **Resampler stack overflow** at extreme downsampling: `mi_resample.c:47`'s `double row[256]`
   is indexed by an unbounded `T`; `48000→3000` gives T=270 and `44100→1000` gives T=744. Not
   reachable at a 44.1/48 kHz station, but `out_hz` is configurable, so clamp or assert it.

## 6. Build order

1. **Vendor `shine` as its own component** with the LGPL notice and a host test that asserts
   the config table and a known bitrate (the checks in §3 are reproducible).
2. **Add the encode step** to the existing pipeline: `chunk_append()` → shine → `broadcast()`.
   Change `Content-Type` to `audio/mpeg` and delete the WAV header.
3. **Verify the wire format** end-to-end: `ffprobe`/VLC should detect MPEG audio, and
   `curl | ffmpeg -i -` should decode; then confirm `icy-metaint` lands at 16 000 encoded bytes.
4. **Fix the accept-loop defect** (§5) and soak-test for days.
5. **Package**: `README.md`, `LICENSE`, `idf_component.yml` (needs `version`, `description`,
   `license`, `targets: [esp32s3]`, `dependencies.idf: ">=5.4"`).

Host-testable throughout: steps 1–3 need no ESP32, which is the property to preserve.

## 7. Open questions

- **Encoder CPU on real silicon.** `shine`'s S3 figures come from a port README (53.4 % of one
  core) and an unverified 1.79× realtime measurement; I could not re-verify the underlying
  benchmarks. **Measure it on your board** before committing to a station format.
- **Total memory.** 97.6 KB encoder state + the decoder's working set + the 88 KB burst ring +
  Wi-Fi/lwIP. On a PSRAM-less S3 this may not fit; on a PSRAM board it will. The resampler and
  burst ring should be PSRAM-preferred with internal fallback.
- **Audio quality of `shine` vs LAME** is real but unavoidable at this power budget; `shine`
  is the only fixed-point MP3 encoder that fits.
- Chrome/VLC proved endless-WAV *worked*; MP3 removes the question, but a smoke test against
  your actual target clients is still worth an hour.

## Reproduction

```bash
# 1. get and build shine
git clone https://github.com/toots/shine && cd shine
gcc -O2 -Isrc/lib -c src/lib/*.c

# 2. config validity + bitrate accuracy (see the two probes used above)
#    shine_check_config(44100,128) == 3 ; encode 100 frames and compare bytes/audio-seconds
```

Files: `s12-mp3-only-plan.md` (this file). Supporting evidence for the surviving components:
`s0-server-code-review.md`, `s7-encoder-and-muxer-components.md`,
`s11-licensing-verified-and-packaging.md`.
