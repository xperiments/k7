# 03 — Vendor the encoder with its licence obligation, and pin its documented behaviour

**What to build:** The MP3 encoder is available to the project as unmodified vendored source
with its licence and a third-party notice in place, and a host test pins the specific behaviours
the specification depends on. Anyone can run that test on macOS in seconds and see the encoder's
real behaviour rather than a summary of it.

Two of those behaviours are traps that the research plan got wrong, so the test exists to stop
them being re-derived from the plan instead of from the encoder:

- The configuration check reports the MPEG tier, and the lowest tier's value is **zero**. A
  truthiness check silently rejects every configuration at that tier. Validity must be tested as
  "not an error", never as "truthy".
- The encoder's own flush entry point does not flush anything. It reports no pending bytes after
  a full encode, on a fresh encoder, and repeatedly. It cannot drain the bit reservoir and cannot
  emit a partial frame's worth of samples. Nothing may rely on it to flush a partial buffer.

**Blocked by:** 01 — Host build and fixture corpus for the broadcast path.

**Status:** ready-for-agent

- [ ] The encoder is present as its own unmodified vendored component. None of its files are copied into another component's source.
- [ ] Its licence text and a third-party notice naming it are present, satisfying the relink obligation that permits distributing this project under its own terms.
- [ ] A host test asserts: a valid configuration at the top tier reports that tier; an invalid rate/bit-rate pair is reported as an error; the lowest tier reports zero and is treated as **valid**; one pass consumes exactly one frame's worth of samples per channel and emits exactly one frame; a hundred passes emit an exact, known byte count; and the flush entry point reports no pending bytes when called after full frames, on a fresh encoder, and repeatedly.
- [ ] The test runs on the host with one command and no board.
- [ ] The GPL-licensed third-party encoder port named in the specification is **not** used anywhere.

Spec: `docs/specs/sd-stream-server.md` §4.1 (E1–E3), §8 (L1–L4), §9 T1, §13.1–13.2.
Decision: ADR 0002.

## Comments
