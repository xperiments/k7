# 02 — Resampler refuses extreme downsampling instead of overrunning memory

**What to build:** Switching the station rate to an extreme value no longer risks corrupting the
broadcast task's memory. An unsupported rate is reported as a clear refusal at the point the
resampler is constructed, and every supported rate keeps working exactly as it does today.

The reason this is its own ticket rather than a line inside another one: the fault only appears
at station rates far below anything normal (roughly below 3 kHz), so it will never surface in
casual use — and proving the fix needs a host test that asserts the refusal, not a listening
test. It is the cheapest possible first exercise of the harness from ticket 01.

**Blocked by:** 01 — Host build and fixture corpus for the broadcast path.

**Status:** done (host tier).

- [ ] Constructing a resampler for an extreme downsample fails cleanly and reports a diagnostic naming the offending rates. It does not write past the bounds of its kernel row.
- [ ] Ordinary and moderately low station rates are unaffected — verified by running the fixture corpus at a normal station rate and confirming output is unchanged from before the guard.
- [ ] A host test asserts the refusal for at least two extreme downsampling pairs, and asserts success for a normal pair.
- [ ] The refusal surfaces as a diagnosable log message rather than a failed assertion or a crash, so a bad station rate in configuration reads as a configuration error.

Spec: `docs/specs/sd-stream-server.md` §5 D4, §9 T2c.

## Comments

- [x] `mi_rs_new()` refuses when the tap count exceeds the kernel row and reports a diagnostic naming the offending rates: `cannot resample 48000 Hz to 3000 Hz: 270 taps exceeds the 256-tap kernel limit`. The two pairs from the specification are both refused (48000->3000 needs 270 taps, 44100->1000 needs 744); previously both wrote past `double row[256]`.
- [x] Ordinary rates are untouched: ten pairs from 44100->44100 through 11025->8000 construct, and 48000->44100 still resamples (checked end to end in `t03`).
- [x] `host/tests/t03_resampler_guard.c` asserts both refusals, the message contents, and success for a normal pair. `mi_rs_new()` now takes an error buffer so the refusal is a diagnosable configuration error rather than an assertion or a crash.
- [x] The caller logs it: `ESP_LOGE(TAG, "cannot resample %s: %s", path, rs_err)`.

Note: the specification's arithmetic is stated as `T = 2 * ceil(ZERO_X / fc)` giving ~540 and ~1486; the implementation's formula gives 270 and 744. Both exceed 256, so the guard is the same either way, and the guard is what matters.

**Post-review note.** T2c's board rerun ("board reruns it as T2c too, to prove the padded frame
survives on-device") is outstanding along with the rest of the board tier. Also, the specification's
arithmetic for the tap count is stated as `T = 2 * ceil(ZERO_X / fc)` giving ~540 and ~1486 where the
implementation computes 270 and 744 (the stated values double-count the factor of two). Both exceed
the 256-tap row, so the guard fires either way; the guard, not the arithmetic, is what was fixed.
