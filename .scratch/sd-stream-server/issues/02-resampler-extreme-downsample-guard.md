# 02 — Resampler refuses extreme downsampling instead of overrunning memory

**What to build:** Switching the station rate to an extreme value no longer risks corrupting the
broadcast task's memory. An unsupported rate is reported as a clear refusal at the point the
resampler is constructed, and every supported rate keeps working exactly as it does today.

The reason this is its own ticket rather than a line inside another one: the fault only appears
at station rates far below anything normal (roughly below 3 kHz), so it will never surface in
casual use — and proving the fix needs a host test that asserts the refusal, not a listening
test. It is the cheapest possible first exercise of the harness from ticket 01.

**Blocked by:** 01 — Host build and fixture corpus for the broadcast path.

**Status:** ready-for-agent

- [ ] Constructing a resampler for an extreme downsample fails cleanly and reports a diagnostic naming the offending rates. It does not write past the bounds of its kernel row.
- [ ] Ordinary and moderately low station rates are unaffected — verified by running the fixture corpus at a normal station rate and confirming output is unchanged from before the guard.
- [ ] A host test asserts the refusal for at least two extreme downsampling pairs, and asserts success for a normal pair.
- [ ] The refusal surfaces as a diagnosable log message rather than a failed assertion or a crash, so a bad station rate in configuration reads as a configuration error.

Spec: `docs/specs/sd-stream-server.md` §5 D4, §9 T2c.

## Comments
