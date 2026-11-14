# 01 — the first Ask corpus run

**Status:** ready.
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
- Every row has an automatic verdict; every `unstable` row and every row the parser cannot judge
  has a human verdict.
- The exported JSON exists, and its aggregate is appended under **Comments** below.
- No `contractGap` row has been counted as a model failure; no `unscored` row has been counted as
  a pass.
- Any human/automatic disagreement is filed as a rubric bug against `../spec.md`, not worked around
  in the harness.

## What this ticket does not decide

- Whether to change the prompt. That is a separate change, and it needs this baseline and a full
  re-run (§7 of the research note).
- Whether to try `samplingMode`. That is an origin trial and an ADR of its own; it is not a
  harness setting.

## Comments

Append the run here: date, machine, `chrome://on-device-internals` variant, per-arm pass counts,
the unstable rows, and the link or path to the raw blob (`.scratch/`, never committed).
