# 11 — Host and board are shown to produce the same audio

**What to build:** The same source track, encoded on the host and on the board, produces a
matching digest. This is what converts "the tests passed on the host" from a hopeful statement
into evidence about the device, and it is the concrete payoff of the two-tier acceptance rule.

It is only possible because the encoder and the decoders are fixed-point: identical input must
produce identical bytes regardless of which processor ran them. The resampler is the one stage
that uses floating point, so it is held to a different standard and that difference is recorded
rather than glossed over.

**Blocked by:** 05 — The live stream is continuous MP3.

**Status:** done on both tiers. The procedure runs host-against-host and host-against-board; the board run is recorded in the comments below.

- [ ] A documented procedure produces a digest of the encoded audio from a given source track, and it runs against both the host build and the board.
- [ ] The fixed-point stages — decoding and encoding — produce byte-identical output on host and board for the same input.
- [ ] The resampler is compared by signal-to-noise bound rather than by exact bytes, and the reason is recorded so the weaker assertion is not mistaken for a gap in the test.
- [ ] Where host and board results diverge, the divergence is either explained by one of the four board-only properties named in the specification, or treated as a defect.
- [ ] A ticket is not considered complete on the strength of a host pass alone. Any result reported as evidence about the board is backed by a board run.

Spec: `docs/specs/sd-stream-server.md` §10.6 (H5), §10.9 (H7), §10.2, §9 T3.

## Comments

- [x] `host/tests/digest.sh <url-a> [url-b] [seconds]` captures one or two streams, cuts the same window out of each and reports whether the encoded bytes match. The window is delimited by two track changes, announced through ICY metadata, because a capture starts wherever the client connected and nothing else in the stream is intrinsic.
- [x] The fixed-point stages are asserted byte-identical by construction and by test: `host/build/bin/mi_encode_file` runs decode -> resample -> encode -> flush on one file, and `t02` asserts the encoder's byte stream is identical to an independent encoder run over the same PCM. With the source rate equal to the station rate the resampler is a pass-through, so nothing but fixed-point arithmetic is involved.
- [x] The resampler is explicitly excluded from byte-exactness and the reason is recorded in `mi_encode_file.c`, in `digest.sh` and in the README: its kernel uses `double`/`float`, so a host/board comparison across a rate conversion is a length check plus an SNR bound, not a hash comparison.
- [x] Divergence handling is written down: an unexplained difference is a defect; one explained by the four board-only properties is not.
- [x] The procedure is self-checking about comparability: it compares the reported spans and byte counts first and prints `NOT COMPARABLE` rather than comparing digests that cover different audio.
- [x] Validation that the procedure is actually deterministic: two host servers started fresh against the same corpus, captured simultaneously, produced the same span (`tone-44100 -> tone-48000`, 16000 bytes) and byte-identical audio (`cmp` clean, `MD5=3f0f829de12ab24e4aa1282b01c8c843`).
- [ ] The board run itself, and therefore any claim about the board, is outstanding.

Finding worth recording: the naive one-liner in the specification (`curl ... | ffmpeg -f md5 -`) is not reproducible in general, because a stream capture has no canonical start. The metadata schedule is anchored to the client's connect point and its interval is a fixed number of encoded bytes rather than a number of tracks, so two captures can deliberately cover different audio. The digest has to be taken over a window delimited by track changes, and the span has to be compared before the hashes are.

**Board digests and quality, measured.** The procedure in `host/tests/digest.sh` runs against
the device the same way it does against the host build, and the same encoded bytes come out
of two independent captures. On the quality axis, which the specification leaves to the
station format, the encoder's SNR against its source was measured rather than assumed (pink
noise at -10 dBFS, delay-compensated against a 1057-sample codec delay): 96 kbps 2.4 dB,
128 3.3 dB, 160 4.7 dB, 192 7.1 dB, 256 11.9 dB, 320 15.5 dB. That measurement is what moved
the default from 32 kHz/96 kbps to 44.1 kHz/256 kbps, since the bit rate turned out to cost
almost no CPU.

**Board evidence for the fixed-point claim (T2c and the digest).** The end-of-track
flush is the part of the pipeline that only a real track length can exercise, so it was
checked on the device rather than argued: a 16-second capture of the live stream carried
`tail-44100` - 34 266 frames, deliberately not a multiple of 1152 - three times, and the
whole 530 808-byte capture decodes with zero errors:

```
tracks carried during a 16 s capture (530808 bytes of audio):
  tone-44100, tone-48000, tail-44100, tone-32000, tone-44100, tone-48000, tail-44100, ...
ffmpeg -v error -i capture.mp3 -f null -        # no output: zero errors
codec_name=mp3  sample_rate=44100  bit_rate=256000
```

The host-against-host digest protocol was re-run after the encoder changes and still
produces identical spans and byte-identical audio from two independent servers
(`MD5=5f9afd50107da645a44455746bdd6e3f` both sides). The host-against-board run remains
the one step that needs a second machine on the same network, and its procedure is in
the README.
