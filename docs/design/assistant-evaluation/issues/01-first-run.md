# 01 — the first Ask corpus run

**Status:** in review — baseline 1 recorded; human marks and the model variant are still outstanding.
**Blocked by:** none.
**Spec:** [`../spec.md`](../spec.md). **How to run:** [`../run-guide.md`](../run-guide.md).

## What

Run the frozen 66-row corpus against the five-combination matrix in real Chrome, on one machine,
three times per sentence, and record the result as the baseline.

This is not a re-run of the 10/12: that number measured a static prompt with no few-shot turns, no
library profile and no repairs. The shipped input has never been measured.

## Why

- The shipped `requestIntent` passes a system prompt, few-shot turns, a JSON Schema and up to two
  repairs. Nothing in the repository has measured that input.
- The purpose-word failure ("10 songs for a workout") and the artist failure ("songs by Daft Punk"
  → a query of `songs`) are recorded in prose, not measured against the current code.
- A prompt or normalizer change cannot be called an improvement until there is a number to improve
  on. This run is that number.

## Acceptance

- All five combinations present, three runs per sentence per combination, on one Chrome and one
  recorded model variant.
- Every row has an automatic verdict; every `unstable`/`unscored` row and the deterministic 10%
  audit sample has a human verdict; every other stable row is either human-marked or accepted from
  the automatic verdict and recorded as `accepted-auto`.
- The exported JSON exists, and its aggregate is appended under **Comments** below.
- No `contractGap` row has been counted as a model failure; no `unscored` row has been counted as
  a pass.
- Any human/automatic disagreement (`verdictSource: "human"`) is filed as a rubric bug against
  `../spec.md`, not worked around in the harness.

## What this ticket does not decide

- Whether to change the prompt. That is a separate change, and it needs this baseline and a full
  re-run (§7 of the research note).
- Whether to try `samplingMode`. That is an origin trial and an ADR of its own; it is not a
  harness setting.

## Comments

### Baseline 1 — 2026-11-14

**Run.** 990 runs: five combinations × 66 rows × 3, no invalid runs. Chrome 157 on macOS, 12
cores, 32 GB. The `chrome://on-device-internals` variant was **not captured** (the field is empty
in the export), so this is a number about this machine, not yet about Ask. Answer timeout 90 s.
Static prompt `e5c9ccb4`, schema `7a8190e4`, 9 examples (`7a188889`). Profile prompt `63d14187`,
schema `7c1b6578`, generation 1794654575, 1502 tracks. Raw blob:
`.scratch/k7-ask-evals-2026-11-14T14-39-53-473Z.json` (not committed).

| Combination | 3/3 pass | fail | unstable | unscored | run pass | recall | precision | repairs | normalized |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `systemOnly` · static | 12/64 | 43 | 8 | 1 | 23.7% | .34 | .33 | 0 | 0 |
| `systemExamples` · static | 32/64 | 18 | 12 | 2 | 55.6% | .74 | .75 | 0 | 0 |
| `requestIntent` · static | 31/64 | 20 | 10 | 3 | 56.6% | .76 | .76 | 9 | 30 |
| `requestIntent` · image | 25/64 | 20 | 18 | 1 | 52.5% | .76 | .65 | 15 | 29 |
| `requestIntent` · imageSearch | 33/64 | 20 | 11 | 0 | 60.1% | .78 | .68 | 18 | 29 |

Two contract-gap rows per combination (S8, N4); only S8 passes, in `imageSearch`. Schema
conformance 198/198 in every combination. Latency p50 1.3–1.7 s, p95 2.0–4.0 s, max 9.7 s.

**The twelve spike sentences:** 2/12 `systemOnly`, 7/12 `systemExamples`, 7/12 static, 6/12
image, 9/12 imageSearch. This does not reproduce 10/12 and cannot: the 10/12 measured a different
prompt, schema and page (note §1.4).

**What the run says**

- **The few-shot turns are the lever.** 12 → 32 rows at 3/3 (+20); recall .34 → .74.
- **The shipped static arm does not beat examples-alone** (31 vs 32; noise). The nine query
  repairs and thirty normalized runs are real work, but the frozen verdict scores the model's
  Intent, not the final one. Re-scored on `final`, the static arm is 32/64 — a derived report,
  not a second baseline.
- **The image arms are more variable, not obviously worse.** 18 unstable rows vs 10, run pass
  52.5% vs 56.6%: the profile prompt costs 3/3 stability, not clear accuracy.
- **`imageSearch` is the best run-level arm** (60.1%) and the only one where the `rows`
  conditions are scored; 11 rows are still unstable.
- **Flake is material**: 8–18 rows per arm never agree 3/3. That is the case for considering
  `samplingMode` only after the rubric is settled.

**What this baseline cannot support yet**

- **No human verdicts.** `verdictHuman` is null everywhere, so the rubric-bug check is empty and
  the rows the parser cannot judge are unmarked. The acceptance criteria above are not met.
- **No model variant.** The machine is known; the model is not.
- **Values are not asserted.** `required` checks that `genre`/`mood`/`key` are present, not that
  they say the sentence's thing: row 19 passes with `genre: "Electronic"` as readily as with
  `"techno"`. Six rows have an expected genre the profile's enum excludes (S2, 19, 20, 21, 45,
  N6), and some pass on presence alone. This is a rubric defect the run found, not a model result.
- **The effective shipped Intent is not the frozen verdict.** The query repair and the purpose
  normalizer change `final`; the frozen rule scores `parseIntent(raw)`.

### Baseline 2 — rubric v2

Re-scored from the same 990 answers, no model calls. v2 asserts the expected genre word, every
expected key by `keyCode` and that every expected mood is present; a profile row whose expected
genre its enum cannot express is `unscored` (`profileGap`), not a failure.

| Combination | 3/3 pass | fail | unstable | unscored | gaps | of non-gap |
|---|---:|---:|---:|---:|---:|---:|
| `systemOnly` · static | 11 | 44 | 8 | 1 | 2 | 11/64 |
| `systemExamples` · static | 31 | 20 | 12 | 1 | 2 | 31/64 |
| `requestIntent` · static | 30 | 21 | 11 | 2 | 2 | 30/64 |
| `requestIntent` · image | 24 | 18 | 15 | 1 | 8 | 24/58 |
| `requestIntent` · imageSearch | 31 | 17 | 10 | 0 | 8 (1 pass) | 31/58 |

The library is 1502 tracks with genres `Pop`, `Hard Rock`, `Rock`, `Alternative Rock`,
`Synth-Pop`, `Electronic`, `Trance`, `Jazz, Pop, Rock`, … and all five moods. Six rows expect a
genre it does not have (S2, 19, 20, 21, 45, N6), which is the six profile gaps; S8 and N4 are the
two contract gaps. Effective (the Intent that ran, after repair/normalizer): 11, 31, 31, 25 and
34 rows at 3/3.

**The audit so far.** 60 rows human-marked, 50 accepted from stable automatic verdicts (only
`systemOnly` has been accepted), 220 rows unmarked; 5 sampled rows marked. **No genuine rubric
bug**: the five rows the page flagged (42, 44, 22, 30, 37) are all unstable rows where the human
resolved the flake. The page required a *unanimous* automatic verdict before calling a human
verdict a disagreement; that is now the rule.

**What remains before Baseline 2 is frozen**

1. On each of the five tabs: **Accept stable**, then **Review only** and mark what it still lists
   (the audit sample is most of it, ~27 rows). The page's counter should read 0.
2. Paste the `chrome://on-device-internals` variant and export again.
3. Then the baseline is the table above plus the audit record; nothing else changes it.

**Before the baseline is frozen**

1. Paste the `chrome://on-device-internals` variant and mark the failing/unstable rows in the
   page; re-export (no model calls are needed).
2. Fix rubric v2: assert the expected genre and key values and that the expected moods are
   present; record the profile's genres and moods and mark a row `unscored` where the profile's
   enum excludes its expected genre; report the effective `final` verdict beside the model verdict.
3. Re-score these 990 answers under v2 — static is exact, the image arms need the recorded
   profile — and only then decide the prompt patch.
