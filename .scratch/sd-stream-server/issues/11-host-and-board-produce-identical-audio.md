# 11 — Host and board are shown to produce the same audio

**What to build:** The same source track, encoded on the host and on the board, produces a
matching digest. This is what converts "the tests passed on the host" from a hopeful statement
into evidence about the device, and it is the concrete payoff of the two-tier acceptance rule.

It is only possible because the encoder and the decoders are fixed-point: identical input must
produce identical bytes regardless of which processor ran them. The resampler is the one stage
that uses floating point, so it is held to a different standard and that difference is recorded
rather than glossed over.

**Blocked by:** 05 — The live stream is continuous MP3.

**Status:** ready-for-agent

- [ ] A documented procedure produces a digest of the encoded audio from a given source track, and it runs against both the host build and the board.
- [ ] The fixed-point stages — decoding and encoding — produce byte-identical output on host and board for the same input.
- [ ] The resampler is compared by signal-to-noise bound rather than by exact bytes, and the reason is recorded so the weaker assertion is not mistaken for a gap in the test.
- [ ] Where host and board results diverge, the divergence is either explained by one of the four board-only properties named in the specification, or treated as a defect.
- [ ] A ticket is not considered complete on the strength of a host pass alone. Any result reported as evidence about the board is backed by a board run.

Spec: `docs/specs/sd-stream-server.md` §10.6 (H5), §10.9 (H7), §10.2, §9 T3.

## Comments
