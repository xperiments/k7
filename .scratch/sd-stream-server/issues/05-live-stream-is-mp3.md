# 05 — The live stream is continuous MP3

**What to build:** A player, browser, or phone pointed at the stream endpoint receives a
continuous MP3 stream and starts playing it. Media tooling identifies it as MPEG audio rather
than as a WAV file. A player that asks for track information is told the current track at the
advertised interval.

The live stream is the one thing every client shares: they all hear the same track at the same
moment, and it cannot be paused or seeked. That property is unchanged from today — only the
audio format on the wire changes.

This ticket carries the encoder's insertion into the broadcast, and with it two problems that
only exist because the internal chunk size and the encoder's frame size do not divide evenly.
Every byte handed to the broadcast must be a complete encoded frame, and the leftover samples at
the end of a track must be encoded rather than silently dropped.

**Blocked by:** 02 — Resampler refuses extreme downsampling instead of overrunning memory; 04 — Station format is validated before anything is served.

**Status:** done (host tier). T3 and T5 have not been run on the board.

- [ ] The stream is reported by media tooling as MPEG audio at the configured station rate and bit rate, with no container header. Verified on the host first, then on the board.
- [ ] No audio container header is emitted anywhere in the stream.
- [ ] Every buffer handed to the broadcast consists of complete encoded frames. A host test drives more frames than one encoder accumulator holds and asserts no partial frame is ever broadcast and no sample is dropped at the boundary between the two granularities.
- [ ] A client requesting track information receives the advertised interval, and the current track title appears at the correct byte offset within the stream body.
- [ ] The advertised interval counts **encoded** bytes, not samples and not source bytes.
- [ ] A track whose length is not a whole number of encoder frames loses nothing at its end: the final partial buffer is encoded, and the encoded frame count is exactly what the track's length implies. A host test asserts this against a track deliberately sized to not divide evenly.
- [ ] The documented promise about what the visualiser callback receives is corrected to match what that callback now actually delivers, without changing the callback's own code or its consumer.

Spec: `docs/specs/sd-stream-server.md` §4.2 (E2–E5), §4.4 C1, §5 D2, §6.1, §9 T2, T2b, T3, T5.

## Comments

- [x] `ffprobe` reports MPEG audio at the station format with no container header; the capture starts on a frame sync and decodes with zero errors.
- [x] No container header is emitted anywhere: `wav_header()` and its call site are gone, and the response is `Content-Type: audio/mpeg`.
- [x] `host/tests/t02_encoder_pipeline.c` drives 2620 chunks of 1024 frames (2 682 880 frames) through the accumulator and asserts the encoder was only ever handed whole 1152-frame blocks, that every `out()` call carried exactly one complete MP3 frame (header length == buffer length), that emitted + still-pending equals a reference encoder's byte total, and that the bytes are identical to that reference. A second phase uses a deliberately non-multiple-of-1152 frame count (100 000) fed in uneven pieces.
- [x] ICY metadata: `icy-metaint: 16000` is advertised and the `StreamTitle='...';` block sits at exactly byte 16 000 of the body with zero-filled padding (asserted by offset, not by searching).
- [x] The interval counts encoded bytes: `client_push()` splits encoded frames, and the test asserts the exact offset.
- [x] The end of a track loses nothing: `mi_enc_flush()` completes the residual block with silence and encodes it, so the encoder is called exactly `ceil(frames/1152)` times, and the byte stream matches the reference.
- [x] The `pcm_cb` promise is corrected in `mini_icecast.h` and `main/main.c` (same PCM content, unencoded 1024-frame chunks, up to one encoder frame ahead) with no code change to the callback or its consumer.
- [x] D2 fixed: `icy-name:` goes out with no space after the colon (asserted), and the test also asserts the non-canonical form is absent.

**Specification refinement, from measurement (E5/T2b).** `shine`'s bit writer emits whole 32-bit words and holds each frame's last few bits in its cache, so at the moment of the flush a track's final frame can be up to 3 bytes short of complete (`shine_flush()` returns 0 bytes and cannot retrieve them). `mi_enc_flush()` therefore emits every frame that *is* complete and leaves the incomplete one buffered; it is emitted, in order, as soon as the next block is encoded — deferred, never dropped. The test asserts exactly that: at the flush, `emitted + pending == reference` and `pending < one frame`; after one more block, the whole track has been emitted and is byte-identical to the reference. A literal "output frames == ceil(frames/1152) at the instant of the flush" is not achievable with this encoder, and asserting it would have meant broadcasting a truncated final frame.

**Post-review correction, with the measurement that prompted it.** The first version of the
end-of-track assertion was too weak to be evidence: `pending < one encoder frame` is satisfied by
withholding a whole frame, which is not what happens. What actually happens, measured: at a flush the
buffer can hold up to a full frame's worth of bytes (416 B in a 10-call run), and the number of bytes
genuinely *missing* from that frame is what shine's bit writer is still holding in its cache - fewer
than four, because the cache is narrower than 32 bits. `t02` now asserts exactly that against the
reference stream (`next_len > pending` and `next_len - pending <= 3`), for both the 2 682 880-frame
case and the deliberately uneven one.

On E5's other intent - keeping the bit reservoir from bleeding across a track boundary - the honest
position is: this encoder cannot be reset per track without destroying and re-initialising 96 KB of
state (the specification's own §4.3 rules that out at runtime), so the reservoir does carry across
boundaries. That is harmless **because the byte stream is continuous**: the concatenation is one
legal MP3 stream, which is verified rather than asserted - the T3 capture spans about eight track
boundaries in five seconds of fixture loop and `ffmpeg -v error -f null -` reports zero errors over
all of it.

**Board run (partial).** The station format is validated and logged on the device
(`station format: 44100 Hz, 128 kbps CBR, joint stereo, MPEG-1`) and the encoder,
staging buffers and burst ring all take the external-memory path. The stream tests
themselves (T3 wire format, T5 ICY metadata, T2b's tail on a real file) need
`/sdcard/music` to exist, so they are outstanding until the card mounts.
