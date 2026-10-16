# Server research: synthesis and recommendation

This is the server-side counterpart to `../01-library-architecture.md`. That document
researched a **player**; the project is a **server**, and the conclusions differ sharply.
Read this one for the server direction.

**Question asked:** "We need to support MP3, FLAC, WAV — where does AAC enter?"

**Answer: nowhere.** Details and evidence below, plus a wire-format finding that matters
more than the AAC question did.

| File | Covers |
|---|---|
| `s0-server-code-review.md` | direct code review of `resources/`, bandwidth arithmetic, concurrency bugs |
| `s1-aac-licensing.md` | AAC/MP3/FLAC/WAV licensing, the Via LA rate card, the component clause |
| `s2-encoder-feasibility.md` | whether an S3 can encode in real time, measured |
| `s3-wire-format-and-clients.md` | whether the endless-WAV stream actually plays anywhere |

## 1. Where AAC enters: nowhere

The server decodes MP3/FLAC/WAV to PCM and serves **raw PCM in a WAV container**. There is
no encoder anywhere in the tree. All three input formats are royalty-free:

- **MP3** — the last core patent in the Fraunhofer/Technicolor program expired
  **2017-04-23**; the programme terminated. Decoder is `dr_mp3`, whose own header says
  *"Choice of public domain or MIT-0"* (read from the source; MIT-0 needs no attribution).
- **FLAC** — xiph.org states it is *"not covered by any known patent"*, reference
  implementations under a New BSD license.
- **WAV/PCM** — no licensing programme exists (an argument from absence, stated as such).
- `dr_flac` and `dr_wav` are likewise public-domain/MIT-0.

AAC would enter only by a **deliberate** decision: add a decoder (to read `.aac`/`.m4a` off
the SD card) or add an encoder (to emit a compressed stream). Both are avoidable:

- **Adding a decoder buys nothing** — MP3+FLAC+WAV already covers what users put on a card.
- **Adding an encoder is the expensive door.** The AAC pool covers **both** "end-user encoder
  and/or decoder products", fdk-aac grants **no patent license** ("NO EXPRESS OR IMPLIED
  LICENSES TO ANY PATENT CLAIMS … ARE GRANTED"), and patents bind the *technique*, so no
  implementation swap helps. Verified rate card: **$0.98/unit** for the first 500,000 units,
  declining to $0.10 above 75M, plus a **$15,000** initial fee ($1,000 for small entities);
  >2-channel counts as 1.5 units.
- **The clause that matters for a library**, verbatim: *"Licensee's products that are not
  end-user products (e.g. **components or implementations**) are not covered by the license
  and would require that the party incorporating such a component or implementation into its
  end-user product obtain a license."* So shipping AAC in xtape would silently transfer a
  licensing obligation onto xtape's users — a strong reason to keep it out of the core.

**Conclusion:** keep AAC out. Optionally expose a *codec backend seam* so a downstream user
can add it themselves under their own license, but ship nothing AAC in the default build.

## 2. The bigger finding: the endless-WAV stream has a compatibility cliff

This matters more than the AAC question, because it affects every client.

**The HTTP half is standards-legal.** RFC 9112 §6.3 defines close-delimited message bodies
when there is no `Content-Length` and no `Transfer-Encoding`; RFC 9110 §8.6 makes
`Content-Length` a SHOULD, not a MUST. So omitting length and relying on `Connection: close`
is correct HTTP.

**The WAV half is a convention, not a spec.** No RFC and no IANA registration reserves
`0xFFFFFFFF` to mean "unknown length". (The registered WAV type is `audio/vnd.wave` per
RFC 2361; `audio/wav` is unregistered.) It works only where parsers chose to tolerate it —
FFmpeg's `libavformat/wavdec.c` explicitly branches on `size != 0xFFFFFFFF` and otherwise
logs *"Ignoring maximum wav data size, file may be invalid"*.

**Measured client results** against a byte-faithful replica of the server's output:

| Client | Result |
|---|---|
| Chrome (`<audio>`) | **Plays**, `duration: Infinity` |
| ffmpeg/ffplay | **Plays** to EOF, with the warning above |
| VLC 3.0.23 | **Plays** |
| **VLC master / 4.x** | **FAILS** — `'data' chunk not found` |
| **Safari / iOS (CoreAudio)** | **REJECTS** (`kAudioFileInvalidFileError`) |
| Firefox | Plays, but reports a bogus ~27-hour duration |

**I independently confirmed the VLC regression in source**, because it determines
forward-compatibility. In `modules/demux/wav.c` (master), on the `data` chunk:

```c
if( vlc_stream_GetSize( p_demux->s, &i_stream_size ) != VLC_SUCCESS )
    goto error;
p_sys->i_data_pos = vlc_stream_Tell( p_demux->s );

if( !b_is_rf64 && i_stream_size >= i_size + p_sys->i_data_pos )
    p_sys->i_data_size = i_size;        /* never set when the size is unknown */
```

For an HTTP stream with no `Content-Length`, `vlc_stream_GetSize()` fails and the whole
demux aborts; if it somehow returned a size, `i_size = 0xFFFFFFFF` would fail the bound
check and `i_data_size` would remain 0, hitting the same guard at line 699:
`if (i_data_size == 0) … msg_Err("'data' chunk not found"); goto error;`. **So the design
is already broken on VLC's next release.**

**Apple is the harder blocker.** AVPlayer was tested end-to-end: the same WAV bytes that
play with a bounded length and `206` range support **fail** without them
(`AVFoundationErrorDomain -11850`). Apple requires *both* a known length and byte-range
support for WAV-over-HTTP. That is not fixable by header tweaks — it is incompatible with an
endless stream, full stop.

**And ICY-in-WAV is understood by no client.** Icecast gates inline ICY strictly to
`format_mp3.c` (`inline_metadata_interval`/`mp3_get_filter_meta` exist in no other format
plugin), and FFmpeg's WAV demuxer has no ICY concept — `ffprobe` parsed the injected metadata
bytes as PCM. The emitter's framing is byte-correct (length byte at `44 + metaint`), but any
client that sends `Icy-MetaData: 1` **loses 16,000-byte chunks of audio to clicks**. It is
invisible today only because browsers never send that header.

**So the current design's "seamless, works with any player" premise is false in practice.**
It works with Chrome and VLC 3, and is already broken on VLC 4 and all of Apple.

## 3. Recommendation

Ranked by value, and the order matters:

1. **Fix the wire format before adding any codec.** Two coherent options:
   - **(a) Serve stored files as-is** with `Content-Length` and `Accept-Ranges`/206. This was
     the *only* configuration that played correctly in **every** client tested, Apple
     included, and it is **strictly simpler** — no decode, no resample, no pacing, no CPU.
     The cost is losing seamless cross-track continuity (the client manages its own playlist),
     which is exactly what the current design bought.
   - **(b) Keep one endless station URL** but emit a **compressed** format, because that is
     what makes the stream self-delimiting in practice: an MP3 stream is parseable frame by
     frame, so no length and no `0xFFFFFFFF` sentinel is needed. This also makes ICY metadata
     *meaningful* — it is a real MP3/AAC feature rather than something injected into PCM.
     This option requires an encoder, so it depends on §4.
2. **Drop inline ICY from the PCM/WAV path** if (a) or a PCM variant survives. Keep
   `icy-name` and the existing `now-playing` accessor; expose now-playing over a small
   JSON/text endpoint instead of smuggling it into the audio body.
3. **Change the station format before reaching for an encoder.** `out_hz` already exists and
   the resampler absorbs any rate; only a mono downmix is missing. 22.05 kHz mono is
   **0.353 Mbit/s — 4× less than 44.1 kHz stereo — for free.**
4. **If compression is genuinely needed, pick by whether you need ICY:**
   - **`shine` MP3** (fixed-point, LGPL-2) — **1.79× real time** at 44.1 kHz stereo/128 kbps
     from two independent measurements; keeps `Content-Type: audio/mpeg` + `icy-metaint`, so
     now-playing keeps working. 0.128 Mbit/s.
   - **`esphome/micro-flac`** (Apache-2.0, integer-only, Xtensa assembly) — **21.3× real
     time**, 44% of PCM size, lossless. Cleanest license and by far the cheapest CPU, but no
     ICY and lossless is only a 2.3× win here.
   - **Opus** — best format, wrong default: measured only at complexity **0** on S3,
     needs a hand-written Ogg muxer, loses ICY, and the available component is C++ and
     Espressif-license-restricted.
   - **`lame` MP3 is a confirmed non-starter** (1.38× real time at only 16 kHz stereo).
   - **AAC: no** (§1).
5. **Watch the CPU budget if encoding.** Encode cost is *additive* on core 1, which already
   hosts the radio task; Wi-Fi owns core 0. Opus at complexity 0 was measured at 24.9% CPU
   (48 kHz stereo), `shine` at ~53% of a core, micro-flac at ~5%.

## 4. Concrete code defects to fix (independent of codec decisions)

From `s0-server-code-review.md`:

1. **The accept loop is blocked by client setup** — `handle_new(fd)` runs inline in
   `accept_task` with a **2 s** blocking header read, so one idle client stalls all new
   connections. Move connection setup off the accept thread (per-connection task or poll loop).
2. **`listen(ls, 2)` backlog** is too small and compounds (1).
3. **`max_clients` documented 1..4, silently coerced to 2** with no diagnostic
   (`mini_icecast.c:395`), while the array is sized 4.
4. **`"icy-name: %s"` has a space after the colon**; real Icecast emits none. Harmless for
   permissive parsers, but this server is the obvious loopback fixture, so it should emit
   canonical headers or deliberately emit both forms.
5. **Encoder-facing contract bugs to fix when the time comes:** the `pcm_cb` comment claiming
   callers receive "the very same PCM that goes on the wire" becomes false once an encoder
   sits in the pipeline; and `metaint` changes meaning (16,000 bytes ≈ 0.09 s of PCM but
   ≈1 s at 128 kbps), so now-playing granularity shifts with the codec.

## 5. What is verified vs. not

**Verified by execution or primary source:**

- All three input formats are royalty-free; `dr_mp3`/`dr_flac`/`dr_wav` are public-domain/MIT-0.
- The Via LA AAC rate card ($0.98 → $0.10, $15,000 initial, 1.5 units for >2 channels) and
  the "components or implementations are not covered" clause.
- fdk-aac grants no patent license; the pool covers encoders and decoders.
- Espressif's S3 TCP TX figure is **20 Mbit/s** air-in-lab (83 Mbit/s shield box).
- `shine` MP3 is **1.79× real time**; `micro-flac` is **21.3× real time** at 44% of PCM.
- The endless-WAV design fails on **VLC master** (confirmed in VLC source myself) and on
  **Apple** (CoreAudio layer + AVPlayer end-to-end), and ICY-in-WAV is understood by no
  client tested.
- RFC 9112 §6.3 makes close-delimited bodies legal; no spec reserves `0xFFFFFFFF` in WAV.

**Not verified — do not present as fact:**

- Whether any **specific hardware internet radio** plays raw WAV over HTTP. Vendor docs
  either 404'd or used unextractable subset fonts, so the negative result is
  *negative-by-omission only*. This is the single most important open question for the
  "stream to a real radio" use case, and it needs a device or a vendor datasheet.
- Firefox and Safari by direct execution (no Firefox installed; the WKWebView harness stalled
  on all media including MP3) — those two rest on source/layer analysis.
- The clock frequency behind the `shine` measurements.
- Any `libFLAC` encoder measurement on any ESP32 (the micro-flac figure is its own benchmark).
- Opus at music-grade complexity (≥5) on S3.
- Whether `esp_hls_stream` could help: it is v1.0.4 and current, but it is an HLS
  **client-side extractor/reader**, not a server — it does **not** cover the server half.
