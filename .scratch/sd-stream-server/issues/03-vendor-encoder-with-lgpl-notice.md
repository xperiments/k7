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

**Status:** done (host tier).

- [ ] The encoder is present as its own unmodified vendored component. None of its files are copied into another component's source.
- [ ] Its licence text and a third-party notice naming it are present, satisfying the relink obligation that permits distributing this project under its own terms.
- [ ] A host test asserts: a valid configuration at the top tier reports that tier; an invalid rate/bit-rate pair is reported as an error; the lowest tier reports zero and is treated as **valid**; one pass consumes exactly one frame's worth of samples per channel and emits exactly one frame; a hundred passes emit an exact, known byte count; and the flush entry point reports no pending bytes when called after full frames, on a fresh encoder, and repeatedly.
- [ ] The test runs on the host with one command and no board.
- [ ] The GPL-licensed third-party encoder port named in the specification is **not** used anywhere.

Spec: `docs/specs/sd-stream-server.md` §4.1 (E1–E3), §8 (L1–L4), §9 T1, §13.1–13.2.
Decision: ADR 0002.

## Comments

- [x] `components/shine/` is toots/shine @ `ab5e3526b64af1a2eaa43aa6f441a7312e013519`, vendored unmodified (sources, `COPYING`, `README.md`, `ChangeLog`), built from source by both the firmware and the host build. Nothing is copied into another component.
- [x] `LICENSES/LGPL-2.0.txt` is byte-identical to `components/shine/COPYING`, and `THIRD_PARTY_NOTICES` names shine with its version and licence. `make -C host layering` asserts both, and asserts that `fknrdcls/mp3_shine_esp32` appears nowhere.
- [x] `host/tests/t01_shine_behaviour.c` asserts every value the specification depends on: `shine_check_config(44100,128)==3`, `(48000,192)==3`, `(16000,64)==2`, `(8000,64)==0` **and valid**, `(8000,128)==-1`, `(44101,128)==-1`, `shine_samples_per_pass==1152`, 100 passes == 41 796 bytes whose concatenation walks as exactly 100 whole frames with no stray byte, `sizeof(shine_global_config)==95896`, and `shine_flush()` reporting 0 bytes after full frames, repeatedly, and on a fresh encoder.
- [x] `make -C host test` runs it in well under a second with no board.
- [x] The GPL-3.0 port is not used.

Finding worth carrying forward: shine's returned byte counts are **32-bit-word aligned, not frame aligned** — measured, only 312 of 1000 calls begin with a frame sync, because the bit writer carries each frame's leftover bits into the next call. The concatenated bytes are a correct frame-continuous MP3 stream, but a caller that assumes one call == one frame is wrong. `mi_encode.c` re-frames on that basis (ticket 05).
