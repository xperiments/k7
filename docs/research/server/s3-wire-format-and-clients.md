# S3 — Is the endless WAV/HTTP stream the right wire format? (server/emitter side)

Scope: the **emitter** design in `resources/mini_icecast.c` and **client compatibility**. The
client's own ICY *parsing* is already covered by `03-transport-verified-locally.md` and
`06-who-sends-icy-status-line.md`; this note does not redo it.

Date of checks: **2026-10-16**. Every "VERIFIED" claim below was either read out of
first-party source/spec, or **executed on this machine** against a byte-faithful replica of
the server's output (`s3-endless-wav-replica.py`, kept alongside this file). The replica was
confirmed to reproduce the wire bytes exactly: `RIFF`/`data` sizes `0xFFFFFFFF`, a
`StreamTitle='Track One';` block whose length byte lands at body offset `44 + 16000`, and
HTTP/1.0 close-delimited headers.

## 0. What the code actually puts on the wire

Read from `resources/mini_icecast.c`:

- Status line is `HTTP/1.0 200 OK`, with `Content-Type: audio/wav`, `icy-name`,
  `Cache-Control: no-cache`, `Connection: close`, and **no `Content-Length`**
  ([`mini_icecast.c:223-229`](../../../resources/mini_icecast.c)); `icy-metaint` is added
  only when the request contained `icy-metadata: 1` (case-insensitive, `:221`).
- `wav_header()` writes `RIFF` size = `data` size = `0xFFFFFFFF`
  ([`mini_icecast.c:181-191`](../../../resources/mini_icecast.c)).
- `client_push()` re-derives the split every call so a metadata block lands exactly every
  `metaint` bytes **of audio**, and `since_meta` is reset per client, not per stream
  ([`mini_icecast.c:93-109`](../../../resources/mini_icecast.c)); the first metadata block
  is forced on connect by `c->title_ver = 0` (`:241`), which is correct.
- Nothing ever emits `Accept-Ranges`, `Transfer-Encoding`, or a `206`.

Confirmed byte-for-byte by `curl`: header is 44 bytes, body offset `16044` = `0x02` +
`StreamTitle='Track One';` + NUL padding.

---

## 1. Is the endless-WAV trick standard or a hack?

**Split verdict, and the split matters: the HTTP side is standard, the WAV side is a
convention that only some parsers honour.**

### 1a. HTTP: a response with no `Content-Length` is explicitly legal and close-delimited — VERIFIED

- `Content-Length` is optional. RFC 9110 §8.6: a server "SHOULD send a Content-Length
  header field when the content size is known prior to sending the complete header
  section" — it is a SHOULD tied to *knowing* the size, and no rule requires one otherwise
  ([RFC 9110 §8.6](https://www.rfc-editor.org/rfc/rfc9110.html#section-8.6)).
- RFC 9112 §6.3 rule 8 is the exact normative basis for this design: "Otherwise, this is a
  response message without a declared message body length, so the message body length is
  determined by the number of octets received prior to the server closing the connection."
  The same section warns the mechanism "exists primarily for backwards compatibility with
  HTTP/1.0" ([RFC 9112 §6.3](https://www.rfc-editor.org/rfc/rfc9112.html#section-6.3)).
- So `Connection: close` + no `Content-Length` is **valid HTTP/1.1**, not a protocol
  abuse — but it is the deliberate "end is not knowable" framing. `Connection: close` is
  honoured in HTTP/1.1 ([RFC 9112 §9.6](https://www.rfc-editor.org/rfc/rfc9112.html#section-9.6));
  the server sends `HTTP/1.0`, which makes the same framing unambiguous rather than
  forcing HTTP/1.0 semantics on the client.

### 1b. WAV: `0xFFFFFFFF` as "size unknown" is a de-facto convention, not a normative rule — VERIFIED as convention, UNVERIFIED as norm

- **FFmpeg treats it as normative-in-practice and says so in a log string.**
  `libavformat/wavdec.c` branches on it explicitly:

  ```c
  } else if (size > 0 && size != 0xFFFFFFFF) {
      data_size    = size;
      wav->data_end = avio_tell(pb) + size;
  } else {
      av_log(s, AV_LOG_WARNING, "Ignoring maximum wav data size, "
                                "file may be invalid\n");
      data_size    = 0;
      next_tag_ofs = wav->data_end = INT64_MAX;
  }
  ```

  ([FFmpeg `libavformat/wavdec.c:472-481`](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/wavdec.c)).
  `INT64_MAX` means "read to EOF", i.e. streaming. Note it is a **warning**: FFmpeg
  regards this as an invalid file that it chooses to tolerate.
- **VLC 3.0 does not special-case the value at all**, it merely refuses to trust it —
  see §2c. That asymmetry is why the same header plays in one and not the other.
- The WAV/RIFF format itself is registered with IANA as **`audio/vnd.wave`**, and RFC 2361
  is the registry document; it enumerates WAVE Format IDs but says nothing about a
  `0xFFFFFFFF` size sentinel ([RFC 2361](https://www.rfc-editor.org/rfc/rfc2361.html)).
  **I could not find any normative statement — Microsoft/IBM RIFF spec text, RFC, or IANA
  registration — that reserves `0xFFFFFFFF` to mean "unknown length".** Treat it as a
  widespread implementation convention that FFmpeg honours, not as a specified feature.

**VERDICT on (1):** the transport framing is sound HTTP; the payload framing is a hack
that works on the parsers that decided to tolerate it and fails on those that did not.

---

## 2. Which real clients actually play it? (all executed against the replica)

| Client (version tested here) | Result | Cause |
|---|---|---|
| **Chrome / Chromium** (headless, via `<audio>`) | **PLAYS** — `readyState 4`, `duration = Infinity`, `currentTime` advances, no `MediaError` | FFmpeg demuxer honours `0xFFFFFFFF` ⓐ |
| **ffmpeg / ffplay** (9.0.2) | **PLAYS** — decodes to EOF | same ⓐ |
| **VLC 3.0.23** | **PLAYS** — logs `chunk: fcc='data' size=4294967295`, streams | ⓑ |
| **VLC master / 4.x** (built later) | **FAILS — `An error occurred during wav demuxing`** | registry regression ⓑ |
| **Safari / iOS WebKit** (AVFoundation + CoreAudio) | **FAILS** — "cannot play" | CoreAudio rejects unknown length ⓒ |
| **Firefox** (source analysis; no Firefox on this machine) | **plays, but `duration` ≈ 27 h (bogus)** | no `0xFFFFFFFF` special case ⓓ |
| **macOS `afplay`** | not a valid test — cannot open HTTP URLs at all (also fails on MP3) | — |
| **Sonos** | **cannot ever work** — WAV is not a supported format | vendor doc ⓔ |
| **Hardware internet radios** | **UNVERIFIED / LIKELY NO** — no primary source found | ⓕ |

### ⓐ Chrome and FFmpeg **do** play it — VERIFIED by execution

Headless Chrome 141 (macOS) against the replica:

```
RESULT {"readyState":4,"networkState":1,"duration":"Infinity","currentTime":0.02322,
        "paused":false,"seeking":false,"buffered":"2.28","error":null,
        "events":["waiting","loadedmetadata","suspend","canplay","playing","play() resolved"]}
```

`duration: Infinity` is the correct signal — not `NaN`, not a bogus finite number. Two
confirmations of the mechanism:

- `ffprobe` on the live replica logs `Ignoring maximum wav data size, file may be invalid`,
  then reports `Duration: N/A` and `Audio: pcm_s16le, 44100 Hz, 2 channels` — exactly the
  `wavdec.c` branch above.
- A control run with a **finite** `data` size but still no `Content-Length` also played
  (`duration: Infinity`), so for Chrome/FFmpeg the `Infinity` comes from the *absent
  Content-Length*, not from the `0xFFFFFFFF` marker. The marker is merely tolerated.

Chromium routes `audio/wav` to the FFmpeg demuxer — `media/base/mime_util_internal.cc`
registers `AddContainerWithCodecs("audio/wav", wav_codecs)` alongside `audio/x-wav`, with
`wav_codecs = {PCM}` ([Chromium `media/base/mime_util_internal.cc:292,385-386`](https://chromium.googlesource.com/chromium/src/+/main/media/base/mime_util_internal.cc)).
(`media/audio/wav_audio_handler.cc` is *not* the media-playback path — it is a small
utility for reading WAV files in the audio subsystem.)

### ⓑ VLC 3.0.23 plays it; VLC master **fails** — VERIFIED by execution + source

Executed, `-I dummy --no-video --no-audio`, verbose:

```
wav demux debug: chunk: fcc=`fmt ` size=16
wav demux debug: format: 0x0001, fourcc: araw, channels: 2, freq: 44100 Hz ...
wav demux debug: chunk: fcc=`data` size=4294967295
main input debug: Stream buffering done (1050 ms in 1377 ms)
```

It parsed the `data` chunk and streamed — **no error**. The 3.0.x source explains why: VLC
only adopts the declared size when the stream's own size can cover it, and otherwise leaves
`i_data_size = 0`, which means "no boundary, read to EOF":

```c
if( !b_is_rf64 || i_size < UINT32_MAX ) {
    int64_t i_stream_size = stream_Size( p_demux->s );
    if( i_stream_size > 0 && i_stream_size >= i_size + p_sys->i_data_pos )
        p_sys->i_data_size = i_size;          /* otherwise stays 0 == read to EOF */
}
```
([VLC 3.0.x `modules/demux/wav.c:419-424`](https://github.com/videolan/vlc/blob/3.0.x/modules/demux/wav.c)).

**Current VLC master changed this, and the change breaks the stream.** Master keeps
`i_data_size == 0` when the size is unknown and then tries to *skip* the chunk:

```c
else if( ChunkSkip( p_demux, i_size ) != VLC_SUCCESS )   /* i_size = 0xFFFFFFFF */
    goto error;
...
if( p_sys->i_data_pos == 0 || p_sys->i_data_size == 0 || p_sys->i_frame_samples == 0 ) {
    msg_Err( p_demux, "'%s' chunk not found", ... );
    goto error;
}
```
([VLC master `modules/demux/wav.c:660-670,699-704`](https://github.com/videolan/vlc/blob/master/modules/demux/wav.c)).
On a close-delimited HTTP response `vlc_stream_GetSize()` cannot succeed (VLC's HTTP access
only sets a size when it sees `Content-Length`, [`modules/access/http.c:807-812`](https://github.com/videolan/vlc/blob/master/modules/access/http.c)),
so this path is reached and the demuxer errors out. **VLC 3.0.23 is the last VLC release
line that plays this stream; VLC 4.x will report a demux error.** (VERIFIED by source read;
I could not execute a VLC 4 build here.)

### ⓒ Safari / iOS: CoreAudio refuses an unknown-length WAV — VERIFIED by execution at the CoreAudio layer

A WKWebView harness could not be made to play *any* audio headlessly on this machine
(MP3 and normal WAV both stalled), so the browser-level claim is not executable here.
Instead I tested the layer Safari actually sits on: WebKit drives `AVURLAsset`/`AVPlayer`
([WebKit `MediaPlayerPrivateAVFoundationObjC.mm:1017`](https://github.com/WebKit/WebKit/blob/main/Source/WebCore/platform/graphics/avfoundation/objc/MediaPlayerPrivateAVFoundationObjC.mm)),
which hands the bytes to CoreAudio's `AudioFileOpenWithCallbacks`. I called that
directly (`s3-coreaudio-unknown-length-test.c`):

```
finite sizes, known length                     AudioFileOpenWithCallbacks = 0  [ok] dataFormat=0 Hz=44100 ch=2 bits=16 dataByteCount=705600
finite sizes, UNKNOWN length (-1)              AudioFileOpenWithCallbacks = 1685348671  (fourcc 'dta?')
0xFFFFFFFF sizes, known length                 AudioFileOpenWithCallbacks = 0  [ok] dataFormat=0 Hz=44100 ch=2 bits=16 dataByteCount=705600
0xFFFFFFFF sizes, UNKNOWN length (-1)          AudioFileOpenWithCallbacks = 1685348671  (fourcc 'dta?')
```

Read this carefully — it separates the two questions cleanly:

- The `0xFFFFFFFF` marker is **fine** for CoreAudio: "0xFFFFFFFF sizes, known length" opens
  successfully and reports the right format (`pcm`, 44100, 2ch, 16-bit).
- What CoreAudio **refuses** is an unknown total length: `kAudioFileInvalidFileError`
  (`'dta?'`, 1685348671). An endless, close-delimited stream is by definition unknown-length.
- **Corroborated end-to-end with `AVPlayer`** (the real consumer) and a purpose-built
  Range-capable server: a *finite* WAV over HTTP gives `item.status = ReadyToPlay`,
  `asset.duration = 3.000 s`; the *same bytes* from a server without byte-range support give
  `item.status = Failed`, `AVFoundationErrorDomain Code=-11850 "Operation Stopped"`,
  `NSLocalizedFailureReason = "The server is not correctly configured."`, underlying
  `NSOSStatusErrorDomain -12939`. The endless-WAV replica fails identically.

**Consequence, and it is stronger than the original framing of this question: on Apple
platforms this server cannot be played by Safari/iOS even if the endless-WAV trick were
replaced by plain file serving — because AVFoundation requires byte-range (`206`) support
for WAV over HTTP, and the server offers neither ranges nor a length.** (MP3 over the same
lengthless server *does* play: `item.status = ReadyToPlay`, `duration = INDEFINITE`. So the
Apple stack distinguishes "unknown length" as acceptable for MP3/AAC and unacceptable for
WAV.) **The AVFoundation result is VERIFIED for `AVPlayer`, and LIKELY (not directly
measured) for Safari itself**, since I could not drive Safari or WKWebView here.

### ⓓ Firefox: plays, but reports a ~27-hour bogus duration — VERIFIED by source read, not executed

No Firefox on this machine, so this is source-level only. Firefox has its own WAV demuxer
([`dom/media/wave/WaveDemuxer.cpp`](https://github.com/mozilla/gecko-dev/blob/master/dom/media/wave/WaveDemuxer.cpp)).
There is **no `0xFFFFFFFF` special case** anywhere in the file (grepped), and the `data`
branch cannot loop — it `break`s out of the chunk walk immediately:

```c
uint32_t chunkSize = mHeaderParser.GiveHeader().ChunkSize();
...
} else if (chunkName == DATA_CODE) {
  mDataLength = chunkSize;                 // == 0xFFFFFFFF
  if (mFirstChunkOffset != mOffset) { mFirstChunkOffset = mOffset; }
  break;                                   // no mOffset += chunkSize for 'data'
}
```
([`WaveDemuxer.cpp:104-120`](https://github.com/mozilla/gecko-dev/blob/master/dom/media/wave/WaveDemuxer.cpp)).
So **init succeeds** — an earlier draft of this note wrongly claimed a uint32 wraparound and
`NS_ERROR_DOM_MEDIA_METADATA_ERR`; `mOffset` is `uint64_t`
([`WaveDemuxer.h:228`](https://github.com/mozilla/gecko-dev/blob/master/dom/media/wave/WaveDemuxer.h))
and the reconciliation block only *lowers* `mDataLength`
(`WaveDemuxer.cpp:136-146`). What Firefox then does is worse in a different way:

```c
int64_t numSamples = static_cast<int64_t>(mDataLength) * 8 / mChannels / mSampleFormat;
...  mInfo->mDuration = Duration();
```
(`Duration()` at `WaveDemuxer.cpp:404-419`). With `mDataLength = 4294967295`, 2 channels and
16 bits, Firefox computes ≈ 1.07e9 samples ≈ **27 hours** of duration for an endless stream,
and `Init()` only requires `mInfo->mDuration.IsPositive()` (`:176`). Playback continues to
EOF, so audio is fine, but `HTMLMediaElement.duration`, the seek bar and progress UI are
wrong by ~5 orders of magnitude. **LIKELY (derived from code, not executed** — and the exact
symptom is version-sensitive).

### ⓔ Sonos: WAV is not supported at all — VERIFIED from vendor documentation

Sonos's own supported-formats table lists AAC-LC/HE-AAC/HEv2-AAC, FLAC, MP3, Ogg Vorbis and
WMA with their MIME types and transport methods — **WAV, PCM and WAVE do not appear**
([Sonos, "Supported audio formats"](https://docs.sonos.com/docs/supported-audio-formats.md)).
The same page's metadata guidance presumes a compressed stream: "Sonos players read
metadata using the Icecast2 ICY streaming protocol for streaming content that is not HLS …
Handle the `icy-metaint` header … Players only check for the `StreamTitle` key." So an
endless WAV is doubly wrong for Sonos: unsupported container **and** an ICY mechanism that
Sonos expects to find in a stream it can decode.

### ⓕ Hardware internet radios: I could NOT verify a single specific device

This is the honest gap in this note. What I could establish:

- Marantz NA7004 user manual lists `WMA / MP3 / WAV / MPEG-4 AAC / FLAC` for **USB** media,
  a local-file path, not a network stream
  ([NA7004 manual, PDF](https://www.marantz.com/on/demandware.static/-/Library-Sites-marantz_europe_shared/default/dwa859b30c/archive-downloads/na7004_u_fr_ug_v00.pdf)).
  "Decodes WAV" in a spec sheet therefore says nothing about *streaming* WAV.
- Frontier Silicon (the dominant internet-radio module vendor behind many Roberts/Revo/
  Ocean Digital/Sangean-class devices) product-brief PDFs were either 404 or unreachable
  from here (`frontiersmart.com/sites/default/files/Chorus4_PB (1).pdf` → 404;
  `frontier-silicon.com/.../Jupiter-6-5S_PB.pdf` → connection timeout), and the Ocean
  Digital manual PDF I did download uses subset font encodings that defeated text
  extraction. **No primary source I could reach states whether these devices accept raw WAV
  over HTTP, and I will not guess.**
- The strongest available evidence is negative-by-omission: every vendor that *does* publish
  a streaming format list (Sonos above) lists only compressed formats, and the ICY-metaint
  mechanism they describe is only ever associated with MP3/AAC-class streams (see §4).
  **Working hypothesis, clearly labelled inference: consumer internet radios stream
  MP3/AAC (sometimes FLAC/Ogg) and play WAV only from local USB/SD media.**

---

## 3. MIME type and container consequences

**`audio/wav` is not an IANA-registered media type.** I fetched the IANA media-types
registry directly: the audio registry contains `L8` and `L16` and **no** `wav`, `wave`,
`x-wav` or `vnd.wave` entry ([IANA media types registry](https://www.iana.org/assignments/media-types/media-types.xml)).
The registered WAVE type is **`audio/vnd.wave`**, defined by RFC 2361 as
`audio/vnd.wave; codec=XXX` where `XXX` is the WAVE Format ID
([RFC 2361 §1 and Abstract](https://www.rfc-editor.org/rfc/rfc2361.html)) — so a
spec-correct type for this stream would be `audio/vnd.wave; codec=1` (WAVE_FORMAT_PCM).

What clients do with the unregistered name, VERIFIED:

- **Chromium accepts it and maps it to PCM** — `AddContainerWithCodecs("audio/wav", wav_codecs)`
  ([`mime_util_internal.cc:385`](https://chromium.googlesource.com/chromium/src/+/main/media/base/mime_util_internal.cc)).
  `audio/x-wav` is an alias (`:386`).
- **WebKit accepts all three spellings but normalises them**: `audio/wav` and
  `audio/x-wav` are registered as synonyms of one another and mapped to the `"wav"`
  extension ([WebKit `MIMETypeRegistry.cpp:382-383,713-714`](https://github.com/WebKit/WebKit/blob/main/Source/WebCore/platform/MIMETypeRegistry.cpp)).
  So `audio/wav` is not the reason Safari fails.
- RFC 9110 §8.3 permits omitting `Content-Type` when the type is unknown and lets a
  recipient sniff, which is not the situation here — the type is present and descriptive
  ([RFC 9110 §8.3](https://www.rfc-editor.org/rfc/rfc9110.html#section-8.3)). Switching to
  `audio/vnd.wave` is a small correctness win and, on the evidence above, would change no
  client's behaviour.

**Does `0xFFFFFFFF` cause a bogus duration or a refusal?** Both, depending on client, and
the two must not be conflated:

- Chrome/FFmpeg: `duration = Infinity` (correct), no refusal. CoreAudio: accepts the marker,
  refuses unknown *length* (§2c). So the **unknown length, not the marker, is the killer**
  for Apple; the marker is what FFmpeg warns about.

**Do `Connection: close` + no `Content-Length` force HTTP/1.0-style handling?** Not "force" —
they *select* close-delimitation, which RFC 9112 §6.3 rule 8 defines for HTTP/1.1 too, and
which the same section calls backwards-compatible. There is no alternative for an endless
body: `Content-Length` is impossible, and chunked encoding cannot help a *live* stream
because each chunk arrival is still bounded by the connection. A server that sends neither
length nor chunked **must** close to signal the end
([RFC 9112 §6.3](https://www.rfc-editor.org/rfc/rfc9112.html#section-6.3)). Practical
consequences: no keep-alive/connection reuse for the stream (irrelevant here), and
`HTTP/1.0` plus `Connection: close` is the well-trodden SHOUTcast/Icecast shape, so this part
is idiomatic.

**Is `Cache-Control: no-cache` enough to stop intermediaries buffering?** It is the right
directive for the risk that actually applies, but it is not what the name suggests:

- `no-cache` means the response MUST NOT be reused without revalidation — it does **not**
  forbid storing ([RFC 9111 §5.2.2.4](https://www.rfc-editor.org/rfc/rfc9111.html#section-5.2.2.4)).
  `no-store` is the directive that forbids storing ([§5.2.2.5](https://www.rfc-editor.org/rfc/rfc9111.html#section-5.2.2.5)).
- For an endless stream this distinction is mostly academic: a cache cannot store or buffer
  a body whose end it cannot see, and `no-cache` already prevents reuse. **The real
  intermediary risk is different: a buffering proxy cannot know when the body ends, so the
  only safe behaviours are to stream through or to reject** — which is another reason the
  design should not rely on intermediaries being helpful. Send `no-store` as well if the
  goal is to be explicit about not writing to disk; it costs one header.

---

## 4. The ICY-metadata-inside-a-WAV-stream oddity — does it work anywhere?

**No client understands it. VERIFIED, from two independent first-party sources plus this
machine's own execution.**

1. **Icecast gates inline ICY metadata to the MP3 format plugin, exclusively.** The
   `Icy-MetaData` request handling, the `icy-metaint` response header, the 16-byte-block
   length byte and the periodic injection all live in `format_mp3.c`:
   `state->inline_metadata_interval = atoi(metadata)` + `plugin->get_buffer = mp3_get_filter_meta`
   ([Icecast `src/format_mp3.c:100-126`](https://github.com/xiph/Icecast-Server/blob/master/src/format_mp3.c)),
   the per-listener interval and `icy-metaint` emission (`:227-235`, `:748`), and the
   injection itself (`:594-621`). Icecast ships `format_ogg.c`, `format_opus.c`,
   `format_flac.c`, `format_vorbis.c`, `format_ebml.c`, `format_midi.c`, `format_text.c`
   and `format_skeleton.c` — and `inline_metadata_interval` / `get_filter_meta` appear in
   **none** of them. The reference ICY implementation is MP3-only (format `audio/mpeg`).
2. **FFmpeg's WAV demuxer has no ICY concept at all.** Grepping
   `libavformat/wavdec.c` and `libavformat/riffdec.c` for `icy`/`ICY` returns nothing. So
   FFmpeg neither strips nor interprets the blocks — it hands them to the PCM decoder as
   samples. This is visible in the execution: `ffprobe` on the replica printed the injected
   bytes as *audio-derived metadata*, e.g. a stream-info entry reading
   `… StreamTitle: Track One` with PCM bytes as the key, and `Duration: N/A`. The player is
   literally parsing the metadata block as PCM.
3. **Structure review of the emitter itself: the framing is correct, the placement is not.**
   `send_meta()` gets the ICY wire format right — `(len + 15) / 16` blocks, one length byte,
   `16 * blocks` bytes, `L == 0` when the title has not changed
   ([`mini_icecast.c:75-90`](../../../resources/mini_icecast.c)) — and `client_push()`
   lands blocks on exact `metaint` boundaries. Tested against the replica, the block sits at
   body offset `16044` (`0x02` + `StreamTitle='Track One';`), which is exactly `44 + 16000`.
   Two consequences that are real for the clients that *do* opt in:
   - The **first** boundary is `44 + metaint`, because the 44 WAV header bytes are pushed
     through `client_push()` before any audio (`:243`). Every subsequent boundary resumes on
     `metaint` of audio. That is self-consistent, and a client that counts from the start of
     the body — which is what the ICY convention means — gets the blocks in the right place.
   - A client that opts in receives metadata blocks **in place of audio bytes**, so it loses
     `metaint` bytes of audio per block. Only a demuxer that both understands ICY *and*
     knows the container is WAV could compensate. None does.

**Therefore: on Chrome/Safari/Firefox/ffmpeg/VLC the `Icy-MetaData` request is never sent,
so the metadata is never injected, so the stream stays pure PCM — which is the only reason
this part is invisible in practice.** Any client that *does* ask (a radio-style client; Sonos
documentation shows Sonos sends `icy-metadata: 1`) will hear a burst of garbage samples
every 16 000 bytes — a click/rasp, at 2.75 Hz, plus a lost 20 ms of audio each time. **There
is no player in which the ICY metadata will be understood while the audio plays correctly.**

**The alternative is strictly better and already half-built:** advertise the station with
`icy-name` only (which the server already does, and which is what clients that have no
inline-metadata support can use), and expose the now-playing title out-of-band — the HTTP
endpoint the project already has (`mini_icecast_now_playing()`, `mini_icecast.h:33`) can
back a small JSON poll. Keep the ICY machinery only for a future MP3/AAC stream, where it is
the de-facto standard and is actually parsed.

---

## 5. Alternatives, with honest "simpler or harder"

**(a) Serve the files as-is — progressive HTTP with `Content-Length`, `Accept-Ranges`,
`206` range responses, no decode at all.**

- *Compatibility:* the best of any option here. This is the only configuration that played
  correctly in **every** client I could test: AVFoundation `ReadyToPlay` with a real
  duration (finite WAV, Range-capable server), Chrome, ffplay. It is also what Sonos's
  documented "File formats … Transport Methods: HTTP, HTTPS" assumes
  ([Sonos](https://docs.sonos.com/docs/supported-audio-formats.md)).
- *Complexity:* **much simpler than what exists.** No PCM pipeline, no resampler, no
  pacing, no WAV header synthesis, no ICY framing, no per-client metadata state, no
  burst-on-connect ring buffer, and ~1.4 Mbit/s × N of Wi-Fi traffic becomes ~0.13 Mbit/s × N.
  Range serving is a handful of lines (`Content-Range`, `206`, seek on the SD file).
- *What is lost:* seamless track changes (a listener hears the current file then the next
  connection drops), no single now-playing stream, and clients that only do live streams
  (many hardware radios) cannot use a file URL as a station. Also WAV files become 635 MB/h
  downloads again — but that is the source material's problem, not the server's.
- Note: **it does not rescue Safari for WAV**, because Safari requires ranges — but ranges
  are exactly what this option adds.

**(b) Compressed stream (MP3, or Opus with an Ogg/WebM container).**

- *Compatibility:* restores everything the current design broke — internet radios and Sonos
  (MP3/AAC/FLAC/Ogg are on the supported lists), Safari/iOS (AVFoundation plays an
  indefinite-length MP3 — I verified `ReadyToPlay` with `duration = INDEFINITE` over a
  lengthless HTTP/1.0 response), and it makes the **existing ICY metadata code finally
  meaningful**, because ICY inline metadata is MP3/AAC convention and is parsed by real
  clients. Also 176 KB/s → 16–24 KB/s per client.
- *Complexity:* **harder** — an encoder is new CPU/memory subsystem, cross-referenced to
  `s2-encoder-feasibility.md` and `s1-aac-licensing.md`; not researched here.

**(c) HLS, via Espressif's own component.**

- The component exists and is current: **`espressif/esp_hls_stream` v1.0.4** (published
  2026-09-23) on the ESP Component Registry — "Espressif HLS stream extractor", depending on
  `espressif/esp_extractor`, `espressif/gmf_io` and `espressif/media_lib_sal`
  ([component registry entry](https://components.espressif.com/components/espressif/esp_hls_stream);
  [readme](https://components.espressif.com/components/espressif/esp_hls_stream/versions/1.0.4/readme)).
  **Important qualification: it is an *extractor/reader*** — "playlist and segment handling,
  optional encryption, and integration with ESP Extractor and ESP-GMF I/O" — i.e. it makes an
  ESP32 an HLS **client**, exposing `esp_extractor_open/parse_stream/read_frame` and
  `esp_gmf_io_hls_init()`. Nothing in it serves HLS. An HLS *server* would still have to be
  written on top (segment the PCM or a compressed stream, emit `.m3u8`, serve segments).
- *Compatibility:* HLS is what modern players want — Sonos documents HLS support and ID3v2
  metadata framing for it ("For HLS content, use ID3v2 frames")
  ([Sonos](https://docs.sonos.com/docs/supported-audio-formats.md)). Safari's native path.
- *Complexity:* **harder, and for a LAN radio server probably the wrong trade** — it adds
  segmentation, playlist generation, segment lifetime management, and a much bigger surface
  (segments are finite files, so this also means the endless-stream problem is replaced by
  a real HTTP file server, i.e. most of option (a) plus the segmentation). The Espressif
  component does **not** cover the server half.

---

## 6. Verdict

**An endless WAV over HTTP is a clever, standards-legal framing choice built on a payload
convention that only FFmpeg-lineage parsers honour — and it is the wrong wire format for a
radio server whose clients are unknown.** The HTTP half is fine (RFC 9112 §6.3 rule 8
explicitly allows close-delimited responses) and so is the RIFF header as far as several
clients are concerned (Chrome, ffmpeg, VLC 3.x play it correctly, with `duration: Infinity`
rather than a bogus duration). The failures are concentrated exactly where "radio" clients
live:

- **Apple (Safari/iOS/macOS): cannot play** — CoreAudio rejects unknown-length WAV
  (`kAudioFileInvalidFileError`) and AVFoundation additionally demands byte ranges this
  server does not offer.
- **VLC 4.x: cannot play** — the master `wav.c` demuxer errors on an unbounded `data` chunk
  where 3.0.x silently read to EOF.
- **Firefox: plays, with a ~27-hour bogus duration** — no `0xFFFFFFFF` handling, so the
  header value is reported as the real length.
- **Sonos: cannot play** — WAV is not a supported format.
- **Hardware internet radios: UNVERIFIED, but every published streaming format list I could
  reach excludes WAV**, so assume no.
- **ICY-in-WAV: understood by no client anywhere**, and actively harmful to any client that
  opts in (clicks replacing audio every 16 000 bytes).

**Single most important compatibility risk:** Safari and iOS will not play the stream at
all — not because of `Content-Type`, and not because of the `0xFFFFFFFF` marker, but because
a live WAV has no knowable length and AVFoundation requires byte-range-capable serving for
WAV. Any phone, tablet or Mac is therefore a guaranteed failure, and no header tweak fixes
it; only changing the container (MP3/FLAC/HLS) or serving bounded files does.

**Recommended direction (in order of effort):** (1) drop the inline ICY metadata for the WAV
path and keep `icy-name` + the existing now-playing endpoint — it can only help, and it
removes a real defect; (2) serve files as-is with `Content-Length`/`Accept-Ranges`, which is
strictly simpler than the current pipeline and is the only option that played everywhere
including Apple; (3) if a single never-ending station URL is required, that requires an
encoder (MP3), which is also what makes ICY meaningful and restores Sonos/radio/Safari.

## Reproduction (this machine, 2026-10-16)

```bash
# faithful replica of the server's bytes:
python3 docs/research/server/s3-endless-wav-replica.py 8731 16000 &
curl -sS -H 'Icy-MetaData: 1' -D - -o body.bin --max-time 8 http://127.0.0.1:8731/
xxd -l 44 body.bin            # RIFF ffffffff WAVEfmt ... data ffffffff
xxd -s 16044 -l 33 body.bin   # 02 'StreamTitle=...' -> metadata on an exact boundary

# Chrome (headless) — plays. Load a page whose <audio src> is the stream URL, e.g.:
#   <audio autoplay src="http://127.0.0.1:8731/"></audio><pre id="out"></pre>
# plus a timer that writes {readyState,duration,currentTime,error} into #out, served over
# http://127.0.0.1:8732/ ; then read #out back out of the DOM:
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new \
  --autoplay-policy=no-user-gesture-required --virtual-time-budget=9000 --dump-dom \
  'http://127.0.0.1:8732/page.html?url=http%3A%2F%2F127.0.0.1%3A8731%2F'
ffprobe -i http://127.0.0.1:8731/          # "Ignoring maximum wav data size, file may be invalid"
/Applications/VLC.app/Contents/MacOS/VLC -I dummy --no-video --no-audio --verbose 2 \
  http://127.0.0.1:8731/                   # "chunk: fcc=`data` size=4294967295"

# CoreAudio (what Safari sits on) — the decisive layer test:
cc -o coreaudio_test docs/research/server/s3-coreaudio-unknown-length-test.c \
   -framework AudioToolbox -lm && ./coreaudio_test
```

## Explicitly NOT verified

- **Whether any specific hardware internet radio plays raw WAV over HTTP.** No vendor
  primary source reached; Frontier Silicon briefs 404/timeout; the Ocean Digital manual PDF
  uses subset fonts that defeated extraction. Do not treat the negative as measured.
- **Firefox behaviour measured in a browser.** The "plays, but reports ≈27 h duration"
  conclusion is derived from `WaveDemuxer.cpp`'s `Duration()` and the absence of any
  `0xFFFFFFFF` special case; no Firefox existed on this machine. (An earlier draft of this
  note claimed Firefox fails with `NS_ERROR_DOM_MEDIA_METADATA_ERR` via a uint32
  wraparound; that was wrong — `mOffset` is `uint64_t` and the `data` branch breaks out of
  the chunk walk — and has been corrected.)
- **Safari itself**, as opposed to AVFoundation/CoreAudio underneath it. The WKWebView
  harness could not play *any* media headlessly here (MP3 and normal WAV both stalled at
  `readyState 0`), so the WebKit-level result is inference from the layer below.
- **VLC 4.x by execution.** The regression is read from `master`'s `modules/demux/wav.c`;
  only VLC 3.0.23 was run.
- **Any intermediary/proxy behaviour in practice** — the caching analysis is RFC-based, no
  proxy was placed in the path.
