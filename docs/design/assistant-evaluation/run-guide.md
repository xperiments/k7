# Running the Ask evaluation

The model is Chrome's, it is not in CI, and nothing here can run it for you. This is the manual,
local, real-Chrome measurement the rubric in [`spec.md`](./spec.md) scores. **No model number is
in the repository yet**; the first run produces the baseline, and it is yours to run.

## Before you start

- Desktop Chrome 148 or later, with built-in AI on, on a machine that meets Chrome's floor.
  `chrome://on-device-internals` is the only place the model's version and size are visible.
- **Free disk and a quiet Chrome.** The component updater hot-swaps the model; a prompt running
  during a swap can fail. The harness records such a failure and marks the affected rows invalid
  rather than stopping the run.
- The repository's dev certificate (the one `k7-deploy.md` §4 sets up with mkcert). Without it the
  dev server is plain HTTP, the page still works, but the origin differs from the App's.
- A **Last image** in this browser if you want scopes 2 and 3. Scope 1 needs no library at all;
  scopes 2 and 3 read the stored Snapshot and its media rows and never touch the device or MCP.

## Start the harness

```bash
cd web
npm run dev
```

Open **`https://k7-dev.xperiments.dev:8000/evals/`**. The page is dev-only: `vite.config.ts`'s
build input lists exactly `site` and `app`, and `scripts/check-artifact.mjs` fails if an `evals`
entry ever reaches `dist`. There is no shipped route and no import from `src/main.tsx` or
`src/ui/**`.

The page shows Chrome's availability first:

- `available` — the matrix is runnable.
- `downloadable` — press **Set up the model** (the same gesture Ask needs); wait for the download.
- `downloading` — the page waits; do not start a run.
- `unavailable` / `absent` — the machine cannot run the model; the harness says which.
  There is nothing to measure here.
- the model disappears mid-run — the run records the error, continues to the next sentence, and
  marks that row invalid. Re-run the invalid rows when it is back.

## What to run

The frozen matrix is five combinations, not the 3×3 cross product (spec §6):

| Arm | What it sends | Scope |
|---|---|---|
| `systemOnly` | `buildSystemPrompt(profile)` — the 10/12 frame | static |
| `systemExamples` | the same plus `intentExamples(profile)` | static |
| `requestIntent` | the shipped path, including both repairs | static, image, imageSearch |

Each sentence runs **three times** per combination: 60 rows × 3 × 5 = 900 calls, roughly 25–80
minutes on the ADR's 1–5 s per answer. The harness saves every finished row, so you can stop and
resume; a run is only frozen when all five combinations have three runs per sentence.

Scope 2 and 3 need the Last image. The harness reads it with `newestSnapshot()` — the image this
browser built or sent most recently — and uses its stored `profile` (or the rows' own aggregates
when an older record has none). Scope 3 also lists the card's media rows and runs the real
`searchImage`, so the `rows` conditions in the corpus can be scored. No device, no MCP, no Local
Network permission.

## Paste the machine

Before the run, copy the model's variant and version out of `chrome://on-device-internals` into
the **Machine** box. JavaScript cannot read that page, and a result without it is a result about a
machine, not about Ask. The run record also carries `userAgent`, `navigator.hardwareConcurrency`
and `deviceMemory`.

## Mark the rows

The table shows each row's three automatic verdicts and its three answers. For rows the parser
cannot judge — does "relaxed" *mean* relaxed? — mark **Right**, **Wrong** or **Unsure**, and put
the reason in the note. One human verdict per row:

- an automatic verdict is the default; only mark where you disagree or the parser cannot decide;
- if the human verdict disagrees with the row's automatic verdict, the page flags a **rubric
  bug** — the disagreement is a defect in `spec.md`, not in the model. Write it down; do not
  silently side with either.

## Export

**Export run** downloads one JSON blob: `{at, machine, matrix, records}`, where each record carries
the sentence, arm, scope, profile, example count and hash, `systemSha`, `schemaSha`, the
availability before the run, the raw answer(s), the repair kind, the parsed Intent, the schema
check, latency, the search result, both verdicts, the note and the machine. Nothing is uploaded
and nothing is committed: `.scratch/` is the place for a raw blob, and the aggregate goes into
the run ticket's **Comments**.

The export is also what makes the rubric reusable: **Export fixtures** writes an `answers.json`
shaped like `src/evals/fixtures/answers.json`, so a real run can replace the reconstructed
fixtures in a normal commit.

## After the run

1. Append the per-arm pass counts, the unstable rows and the model variant to
   [`issues/01-first-run.md`](./issues/01-first-run.md) under **Comments**.
2. If a human verdict disagreed with an automatic one, fix the rubric first; a model change
   measured against a broken rubric measures nothing.
3. Only then consider a prompt or normalizer change, and only with a full re-run — prompt edits
   have already lost once (8/12, ADR 0084).

CI keeps running the corpus, the scorer and the fixtures: `npm test` proves the judge, not the
model.
