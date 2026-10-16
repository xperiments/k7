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

**Status:** ready-for-agent

- [ ] The stream is reported by media tooling as MPEG audio at the configured station rate and bit rate, with no container header. Verified on the host first, then on the board.
- [ ] No audio container header is emitted anywhere in the stream.
- [ ] Every buffer handed to the broadcast consists of complete encoded frames. A host test drives more frames than one encoder accumulator holds and asserts no partial frame is ever broadcast and no sample is dropped at the boundary between the two granularities.
- [ ] A client requesting track information receives the advertised interval, and the current track title appears at the correct byte offset within the stream body.
- [ ] The advertised interval counts **encoded** bytes, not samples and not source bytes.
- [ ] A track whose length is not a whole number of encoder frames loses nothing at its end: the final partial buffer is encoded, and the encoded frame count is exactly what the track's length implies. A host test asserts this against a track deliberately sized to not divide evenly.
- [ ] The documented promise about what the visualiser callback receives is corrected to match what that callback now actually delivers, without changing the callback's own code or its consumer.

Spec: `docs/specs/sd-stream-server.md` §4.2 (E2–E5), §4.4 C1, §5 D2, §6.1, §9 T2, T2b, T3, T5.

## Comments
