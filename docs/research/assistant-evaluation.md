# How to test what Ask understands, and what Chrome lets us change

**Status:** research · **Updated:** 2026-11-15

**Scope:** research only; no source changed and no model run. Written 2026-11-14 against
`646ca76`; the working tree also carries another lane's uncommitted Snapshot, image-source and
player files, which this note did not inspect. Every external URL was read on 2026-11-14. Labels:
**VERIFIED** = read in the cited primary source (Chrome's own docs, Chromium source at a named
revision, the Prompt API explainer, this repository's code); **REPORTED** = a credible third
party's observation, not the vendor's; **UNVERIFIED** = could not prove, and says what was tried.

**The question.** Ask sends one sentence to Chrome's built-in model and gets back an **Intent** —
`library_search` filters, never results. The measured number is 10/12 on a twelve-sentence page.
What can the Prompt API actually vary, can any of it run in CI, what should a local harness look
like, how should a sentence be scored, what could raise the quality without losing the 10/12, and
which sentences should the corpus be?

**The answer in twelve lines.**

1. **The 10/12 did not measure the shipped path.** The APP-11 page passed **only** a system prompt
   (`initialPrompts: [{role:'system'}]`), a hand-copied schema, no few-shot turns, no library
   profile and no repairs. `requestIntent` today passes `buildSystemPrompt(profile)`,
   `intentExamples(profile)`, `intentSchema(profile)`, and can make up to two model calls.
   Nothing in the repository has measured that input. (§1.4)
2. **On the web, `temperature` and `topK` are dead.** They are Chrome-Extensions-only and are
   *silently ignored* in a page. The web knob is `samplingMode` (seven presets), and it is behind
   the Sampling Parameters origin trial, not on stable. (§1.2)
3. **`responseConstraint` is a JSON Schema object or a `RegExp`.** Unsupported keywords raise
   `NotSupportedError`; an unproducible answer raises `SyntaxError`. Chrome publishes no list of
   supported keywords, and the constrained decoder is not in the open tree — the exact subset is
   **UNVERIFIED**. (§1.3)
4. **The model is a downloadable Chrome component, chosen by a GPU probe** (2B or 4B variant), kept
   by the component updater, purged under disk pressure or after 30 days of ineligibility. The same
   corpus can therefore score differently on two machines, by design. (§1.5)
5. **No Chrome documentation promises built-in AI in headless, and CI cannot have it today.**
   GitHub-hosted runners have no Chrome profile with the component; Chrome for Testing ships no
   component updater. The repo's "not tested in CI" is correct. (§2)
6. **The harness should be a dev-only page on the app's own origin** (`web/evals/`), importing
   `requestIntent`/`parseIntent`/`buildSystemPrompt`/`intentSchema`/`intentExamples` from
   `web/src/playlists/ask/`. Vite's build has an explicit `input` of `site` + `app`, so a page
   outside it is not shipped. Add one trace sink so repairs are visible; do not copy the path. (§3)
7. **Score the Intent, not the string.** `parseIntent` is already the judge; score its output on
   recall, precision, forbidden keys, bound direction, and whether the result is non-empty. (§4)
8. **The empty Intent is a refusal.** `parseIntent({})` returns a failure by design, so "play
   something" ends in an error message; the corpus must say so rather than expect `{}`. (§4.4)
9. **The known over-fill is also a search bug.** `relaxRanges` needs a mood, so the five-range
   workout Intent never relaxes — it just reports "Nothing matched". (§5.6)
10. **The cheapest real fixes are app-side and measurable**: a purpose-word normalizer, relax
    without a mood, and one few-shot row for the two known misses. Prompt tightening is the
    expensive one: it already scored 8/12 once. (§5)
11. **A 60-sentence corpus** with a `must` predicate per row and the twelve spike sentences as a
    regression subset is in §6.
12. **Build the harness first, freeze the corpus, then change one thing at a time.** A prompt or
    normalizer change ships only with a measured re-run against the same corpus. (§7)

---

## 1. What the Prompt API lets you vary

### 1.1 The surface, as documented

| Declared member | What it does | Status |
|---|---|---|
| `LanguageModel.availability(options?)` | `"unavailable" \| "downloadable" \| "downloading" \| "available"` | **VERIFIED** — [`availability.idl`](https://github.com/chromium/chromium/blob/88fbc01a19ead9ccbe32f3f858a4c82aa54b53f8/third_party/blink/renderer/modules/ai/availability.idl), [docs](https://developer.chrome.com/docs/ai/prompt-api) |
| `LanguageModel.create(options?)` | session; `initialPrompts`, `monitor`, `signal`, `expectedInputs`/`expectedOutputs` | **VERIFIED** — [`language_model_create_options.idl`](https://github.com/chromium/chromium/blob/fbff40b173fab44a55950051b8d40fdf9b267926/third_party/blink/renderer/modules/ai/language_model_create_options.idl) |
| `session.prompt(input, {responseConstraint, signal})` | one answer as a string | **VERIFIED** — [`language_model.idl`](https://github.com/chromium/chromium/blob/fbff40b173fab44a55950051b8d40fdf9b267926/third_party/blink/renderer/modules/ai/language_model.idl) |
| `session.append`, `session.clone`, `session.destroy` | more context, fork, free | **VERIFIED** — same IDL / [docs](https://developer.chrome.com/docs/ai/prompt-api) |
| `session.contextUsage`, `contextWindow`, `measureContextUsage` | tokens used, window, and the cost of a constraint | **VERIFIED** — same IDL / [docs](https://developer.chrome.com/docs/ai/prompt-api) |
| `session.promptStreaming`, `contextoverflow`, `QuotaExceededError` | streaming and overflow | **VERIFIED** — docs |
| `responseConstraint` | a JSON Schema object or a `RegExp` | **VERIFIED** — [explainer](https://github.com/webmachinelearning/prompt-api/blob/main/README.md), docs |
| `omitResponseConstraintInput` | do not put the schema in the context window | **VERIFIED** — IDL (`boolean = false`) + explainer; *not* in the older revision `88fbc0…`, so it is newer than the spike |
| `prefix: true` on a trailing `"assistant"` message | prefill the start of the answer | **VERIFIED** — IDL + [docs](https://developer.chrome.com/docs/ai/prompt-api) |
| `expectedInputs`/`expectedOutputs` | modality and language expectations; `en`, `ja`, `es`, `de`, `fr` | **VERIFIED** — docs; "specifying a language … does not actually influence the model's output" (**VERIFIED**, explainer) |
| `topK`, `temperature`, `LanguageModel.params()` | extensions only; **ignored on the web** | **VERIFIED** — docs ("Only available when using the Prompt API for Chrome Extensions") and explainer's deprecation notice |
| `samplingMode` | `most-predictable` … `most-creative` | **VERIFIED in source**, gated: `[RuntimeEnabled=AIPromptAPIParams]`, which is `"experimental"` + `origin_trial_feature_name: "AIPromptAPIParams"` in [`runtime_enabled_features.json5`](https://github.com/chromium/chromium/blob/fbff40b173fab44a55950051b8d40fdf9b267926/third_party/blink/renderer/platform/runtime_enabled_features.json5) |
| `thinkingEffort` | `none`…`high` | **UNVERIFIED as shipped**: documented in the explainer only; no such member in `language_model_create_options.idl` at `fbff40b`, and the docs page does not mention it. It may be ahead of Chrome. |
| `tools` / tool-call | function calling | **VERIFIED** — IDL (`RuntimeEnabled=AIPromptAPIToolUse`) + explainer. Out of scope for Ask: the model must not call anything. |

Two consequences for Ask's `web/src/builtin-model.ts`: it passes exactly `{initialPrompts}` to
`create()` and `{responseConstraint}` to `prompt()`, which is the smallest legal use of the API. It
sets no sampling control because, on the web, there is nothing to set. Any harness that "sets
temperature" would be measuring nothing while looking like it did.

### 1.2 Determinism

- Chrome documents **no** determinism guarantee. The closest thing is
  `samplingMode: "most-predictable"`, described as "Highest determinism and predictability" and
  aimed at "testing, code generation, or content extraction" (**VERIFIED**, explainer).
- That preset is behind an origin trial (**VERIFIED**, `AIPromptAPIParams` is
  `"experimental"` with an origin-trial name and `origin_trial_allows_third_party: true`). Using it
  means registering the App's origin for the trial, in an ADR of its own; on stable Chrome without
  the token the option is absent.
- The model itself varies by machine: Chrome "estimates the device's GPU performance by running a
  representative shader" and picks a larger (e.g. 4B) or smaller (e.g. 2B) Gemini Nano variant, or
  CPU (**VERIFIED**, [model management](https://developer.chrome.com/docs/ai/understand-built-in-model-management)).
  So a corpus run is only comparable between machines that report the same variant.
- Therefore a harness must run each sentence **more than once** and report a stability rate, and
  must record what it ran on. Three runs is the smallest number that can show flake without
  costing 3× the ~1–5 s per answer the ADR measured.

### 1.3 `responseConstraint`: what is actually supported

- The value may be a JSON Schema object **or** a `RegExp` (**VERIFIED**, explainer).
- A valid schema that uses features the user agent does not support → `NotSupportedError`; a
  schema the model cannot satisfy → `SyntaxError`; anything else → `TypeError` (**VERIFIED**,
  explainer).
- The docs' examples use only `type`, `properties`, `required`, `additionalProperties`, `items`,
  `pattern`, `maxItems`, `minimum`, `maximum` (**VERIFIED**, [structured output](https://developer.chrome.com/docs/ai/structured-output-for-prompt-api)).
  `enum` is **not** in any Chrome example; `web/src/playlists/ask/prompt.ts` relies on it, and the
  spike reports it worked (repository evidence, not a Chrome statement).
- **The supported subset is UNVERIFIED.** Chrome publishes no keyword list. I checked: the Prompt
  API page, the structured-output page, the explainer's "Structured output" section, the IDL, the
  `on_device_model` mojom (which carries the schema as an opaque `string json_schema`), and
  `services/on_device_model/ml/chrome_ml.cc` (no schema handling) — the constrained decoding lives
  behind `ChromeMLAPI` in a component that is not in the open tree
  ([mojom](https://github.com/chromium/chromium/blob/88fbc01a19ead9ccbe32f3f858a4c82aa54b53f8/services/on_device_model/public/mojom/on_device_model.mojom)).
- Chrome ships a [JSON Schema Tester](https://googlechrome.github.io/samples/json-schema-tester/)
  for exactly this question (**VERIFIED**, linked from the docs). Before adding `description` or
  `pattern` to the Intent schema, run it there.
- The schema is also *shown to the model* by default, i.e. it is prompt text as well as a
  constraint, and it costs context (**VERIFIED**, docs: "Your implementation can include a JSON
  Schema … This uses some of the context window"). `ask/prompt.ts`'s comment that the schema "is a
  request and the parser is the judge" is the right reading.

### 1.4 The 10/12 measured a different input than what ships

This is the single most important correction in this note.

- The spike page (`index.html`, built by `.scratch/nano-spike/build.mjs`) creates the session with
  `initialPrompts: [{role:'system', content: SYSTEM_PROMPT}]` and prompts with a **hand-copied**
  `SCHEMA`. It has no `examples`, no library profile, no repair retry (**VERIFIED**, reading both
  files).
- Its build script lifts `const SYSTEM_PROMPT = [` — a literal array the assistant no longer
  contains; the prompt is now assembled by `buildSystemPrompt(profile)` in `ask/prompt.ts`. The
  page therefore cannot be rebuilt, and whatever it ran is an older prompt (**VERIFIED**,
  `build.mjs` lines 16–40 vs `ask/prompt.ts`, `buildSystemPrompt`).
- ADR 0084's note of 2026-11-13 says the prompt "now carries a word-to-filter table and few-shot
  turns passed as `initialPrompts`". That is a *later* change than the 10/12 run described in the
  ADR's Decision section. The 8/12 tightened prompt was measured on the same older frame.
- `requestIntent` today also repairs: once for an answer that is not a usable object, and once for
  a `query` that is not a name the sentence used (`ask/request.ts`, `requestIntent` and
  `repairQuery`). Neither existed in the spike.

So: **10/12 is the number for a static prompt with no examples, no profile, no repairs.** The
shipped input has never been measured, and the number may be better or worse. A new measurement is
not a re-run of the old one; it is the first run of the current one.

The ADR also names the two misses as a duration ("about an hour" → `limit: 75`) and the artist
("songs by Daft Punk" → a query of `"songs"`), while the brief for this note names the artist and
the workout phrase. Both readings are recorded here; the corpus carries all three rows so a re-run
settles which description is right.

### 1.5 The model's lifecycle, which the harness must record

- Downloaded by the first `create()` call of *any* Gemini-Nano API, after user activation
  (**VERIFIED**, docs + model management). Ask's **Set up the assistant** is that gesture.
- Floor: desktop Chrome (Windows 10/11, macOS 13+, Linux, ChromeOS Plus), 22 GB free on the profile
  volume, GPU > 4 GB VRAM **or** 16 GB RAM with 4 cores, unmetered connection
  (**VERIFIED**, docs). The model is removed if free space falls below 10 GB (**VERIFIED**).
- Installed and updated by Chrome's **component updater** — the installer "is owned by
  `ComponentUpdaterService`" (**VERIFIED**, [`on_device_model_component.h`](https://github.com/chromium/chromium/blob/88fbc01a19ead9ccbe32f3f858a4c82aa54b53f8/components/optimization_guide/core/model_execution/on_device_model_component.h)).
  Its install states include `kInstallNotComplete`, `kModelInstallerNotRegisteredForUnknownReason`,
  `kModelInstalledTooLate`.
- Updates are full re-downloads and are hot-swapped; a prompt running during the swap can fail
  (**VERIFIED**, model management).
- `chrome://on-device-internals` is the only place the version and size are visible, and JavaScript
  cannot read it (**VERIFIED**, model management). The harness must ask the human to copy it.

---

## 2. CI and headless

**The repo's statement stands, with a sharper reason.** `web/src/playlists/ask/request.ts` says the
ask "is not tested: the built-in model is Chrome's and CI has none", and `.github/workflows/ci.yml`
runs only Node on `ubuntu-latest` (plus the host build) — no browser at all, and the Playwright job is
commented out (**VERIFIED**, repository).

What is documented, and what is not:

- **No headless story exists in Chrome's docs.** None of the Prompt API, structured-output,
  session-management, model-management, get-started or debug pages mentions headless
  (**UNVERIFIED as a positive claim**, stated as the result of reading those six pages on
  2026-11-14). The requirements they do state are ordinary desktop Chrome, a profile volume with
  22 GB, and a component download — none of which is a statement about headless or about CfT.
- **The model arrives through Chrome's component updater** (§1.5). Chrome for Testing is a pinned
  automation build that is not the branded component-updated browser, so a fresh CfT profile has no
  model and no mechanism to fetch one (**REPORTED**: the `@browserless/ai` README says "Chrome for
  Testing does not download Gemini Nano for you" and that a model must be packed from a machine that
  already has it; the Microlink write-up says the same of a GPU-less headless fleet —
  [npm](https://www.npmjs.com/package/@browserless/ai/v/13.9.8), [blog](https://microlink.io/blog/chrome-built-in-ai-from-nodejs)).
- **Automation may not expose the API at all.** A third party reports that `LanguageModel` is not
  exposed when Chrome is launched programmatically, even with
  `--enable-features=PromptAPIForGeminiNano,OptimizationGuideOnDeviceModel`, while a manually
  launched Chrome on the same machine shows `downloadable` (**REPORTED**,
  [tmc/misc investigation](https://github.com/tmc/misc/blob/bbbb7f1bdbdab33fc79a2357d7510bfd0cef87e0/chrome-to-har/INVESTIGATION_RESULTS.md),
  [langmodel notes](https://github.com/tmc/misc/blob/bbbb7f1bdbdab33fc79a2357d7510bfd0cef87e0/chrome-to-har/docs/langmodel.md)).
  That would make even a self-hosted runner with a warmed profile unusable, and it is
  **UNVERIFIED**: I could not test it, and the claim conflicts with the browserless approach, which
  reports driving the API from Node through a browserless page.
- The flag names do exist: `OptimizationGuideOnDeviceModel` is a real feature, enabled by default
  on Windows/macOS/Linux (**VERIFIED**,
  [`optimization_guide_features.cc`](https://github.com/chromium/chromium/blob/88fbc01a19ead9ccbe32f3f858a4c82aa54b53f8/components/optimization_guide/core/optimization_guide_features.cc)),
  and `AIPromptAPI` is `stable` on those platforms at `fbff40b` (**VERIFIED**,
  `runtime_enabled_features.json5`). So flags are not what is missing; the component is.

**Verdict.** The corpus is a **manual, local, real-Chrome** measurement, run by a person on a
machine that already has the model. CI keeps testing what it can: `parseIntent`, `isNameQuery`,
`buildSystemPrompt`, `intentSchema`, `intentExamples` — already covered in
`web/src/playlists/ask/*.test.ts`. The harness's own scoring code should also be unit-tested in
Vitest against recorded model answers, so a rubric change is a CI-checkable diff even though the
model is not.

---

## 3. The harness

### 3.1 Where it lives

**A dev-only page inside `web/`, on the app's own origin** — not a standalone `evals/` folder, and
not a route in the shipped App.

- `web/evals/index.html` + `web/src/evals/*` is served by `npm run dev` at
  `https://k7-dev.xperiments.dev:8000/evals/` (**VERIFIED**, `vite.config.ts`: `server.port 8000`,
  `host: true`, mkcert pair) — the same origin Ask runs on, which sidesteps the question of whether
  the model download is per-origin (the docs say "the model is downloaded separately the first time
  an origin uses the API", which reads like per-origin; **UNVERIFIED** in source).
- It is dev-only **by construction**: `build.rollupOptions.input` lists exactly `site`
  (`index.html`) and `app` (`app.html`), so nothing under `evals/` is emitted into `dist`
  (**VERIFIED**, `vite.config.ts`). `scripts/check-artifact.mjs` is the place to add one assertion
  that `dist` has no `evals` entry, if that comfort is wanted.
- Put the corpus, the scoring and the page under `web/src/evals/` so `tsconfig.json`'s
  `include: ["src", …]` typechecks them, and the page under `web/evals/` so Vite serves it at a
  clean path. The imports from `../playlists/ask` are then the real modules, not a copy.
- A route in the App (`#/evals`) is the wrong shape: the App mounts the device gate, the analysis
  worker and the poll (`src/ui/App.tsx`), none of which the harness needs, and a shipped route is a
  shipped route.

### 3.2 What it imports

```ts
import { requestIntent } from '../playlists/ask';
import { parseIntent } from '../playlists/ask/parse';
import { buildSystemPrompt, intentExamples, intentSchema } from '../playlists/ask/prompt';
import { searchImage } from '../library/imageSource';
import { loadSnapshot } from '../storage/snapshots';
import { listMedia } from '../storage/media';
```

- **The shipped path is one call**: `requestIntent(sentence, profile)`. It is the only thing that
  exercises `askModel`'s session, the few-shot turns, the schema, and both repairs. The harness
  must call it, not reassemble it.
- The parts are exported separately as well, so a harness can also run "system prompt only" as a
  **control arm** — that is how the 10/12 baseline and the shipped path become comparable on the
  same day, on the same machine.
- **Repairs are invisible from outside.** `requestIntent` returns only the final Intent or a
  failure. To score "how often did it need a repair" the harness needs a trace. The least invasive
  change is one optional parameter on `requestIntent`, e.g.
  `trace?: (attempt: {call: 1 | 2; reason: string; answer: string; ms: number}) => void`, called
  around each `askModel`. It changes no behaviour, is dead code in the App (nothing passes it), and
  keeps the harness honest. `builtin-model.ts` would need the same `ms` reported or measured by the
  caller with `performance.now()`; the latter is enough and touches nothing.

### 3.3 What it runs against

Three scopes, cheapest first; a run records which one it used.

1. **Intent only, static prompt** (`profile: null`). No library, no device. This is the arm the
   10/12 was measured in, and the one every row can run.
2. **Intent only, the real profile.** The profile is already **stored in the Last image**
   (`StoredSnapshot.profile`, `web/src/storage/snapshots.ts`), so `loadSnapshot(scope)` gives the
   exact aggregates `buildSystemPrompt`/`intentSchema` receive in the App — no device, no session
   (§`web/src/playlists/profile.ts` explains when the App uses it: only when the player runs the
   same generation).
3. **Intent + the real search.** `readImageRows(snapshot.bytes)` + `listMedia(scope)` +
   `searchImage(rows, media, intent)` runs the same matching the Last image path uses
   (`web/src/library/source.ts` 118). This is what answers "does the expected Intent return tracks,
   and does the model's Intent return nothing on this library".

Do **not** point the harness at the device: the player answers over MCP, needs the Local Network
permission and a board, and `searchTracks` would make the corpus depend on hardware. The Last image
is the same rows the App would search, in this browser, offline.

### 3.4 What one run records

Per sentence, per repetition:

| Field | Why |
|---|---|
| `sentence`, `id`, `set` (`spike` \| `corpus`) | join on the corpus |
| `profile`: `none` \| `image`, and its `generation`/`tracks` | which prompt/schema was used |
| `arm`: `requestIntent` \| `systemOnly` \| `systemExamples` | the control |
| `examples`: count and a hash of the joined turns | the few-shot set is an input |
| `systemSha`, `schemaSha`, `systemChars` | so a prompt edit invalidates the old numbers |
| `availability` before each run | the model can disappear mid-corpus (§1.5) |
| `raw`: the string `session.prompt` returned | never re-derive it; it is the evidence |
| `calls`: 1 or 2, and `repair`: `none` \| `notJson` \| `queryNotName` | from the trace |
| `parsed`: `parseIntent(raw)` result, camelCase | the contract's own judgement |
| `schemaOk`: harness check against the *same* `intentSchema` | separate from parse |
| `latency`: `createMs`, `promptMs`, `totalMs` | ADR 0084's 12.6 s cold / 0.4 s per session / 1–5 s per answer |
| `search`: `matched`, `tracks.length`, `truncated` (scope 3) | result usefulness |
| `verdictAuto`, `verdictHuman` (`right`/`wrong`/`unsure`), `note` | the human is the tie-break |
| `machine`: `userAgent`, `navigator.hardwareConcurrency`, `deviceMemory`, and the
  `chrome://on-device-internals` variant the human pastes | results are per-variant (§1.2) |

Export: one JSON blob with `{at, machine, arm, scope, profile, runs: [...]}` offered as a download
(`Blob` + `a[download]`). Do not commit the blob; put the aggregate in the ADR or a research note,
as ADR 0084 did. If raw evidence must be kept, `.scratch/` is the repo's place for transient data.

Human marking: keep the spike's Right/Wrong buttons. They are the ground truth for the rows the
parser cannot judge (does the sentence *mean* `relaxed`?), and they are what makes a disagreement
with the automatic verdict visible as a rubric bug rather than a model bug.

### 3.5 Avoiding the harness in the artifact

- Vite's explicit `input` already excludes it (§3.1).
- Never import an `evals` module from `src/main.tsx` or `src/ui/**`; an import is what ships.
- `check-artifact.mjs` can gain one line: fail if `dist/evals` exists. Cheap, permanent.
- The service worker's precache list comes from `emitted` (`vite.config.ts`), so it cannot grow to
  include the harness either.

---

## 4. Scoring

### 4.1 Five axes, each with a numerator a person can check

1. **Shape** — `raw` parses as JSON and satisfies `intentSchema(profile)`. This is Chrome's promise,
   and the spike measured 12/12 on it. Report per row; it should stay 100%.
2. **Contract** — `parseIntent(raw)` returns `SearchArgs` (not an `IntentFailure`). This is what the
   App can actually run. It is *more lenient* than shape (unknown keys dropped, numbers clamped) and
   *stricter* in one way: an empty object is a failure.
3. **Recall** — of the keys the corpus says the sentence justifies, how many are in `parsed`.
   `|expected ∩ parsed| / |expected|`. A `*_min`/`*_max` pair counts as one filter when both are
   expected.
4. **Precision** — of the filters in `parsed`, how many are justified. `|parsed ∩ allowed| / |parsed|`,
   where `allowed` = expected ∪ documented synonyms. This is the axis the workout over-fill fails.
5. **Forbidden** — a per-row blacklist (`!{bpm_min,bpm_max,valence_min,…}`) that must be empty. It
   is separate from precision because a forbidden key is a *product* failure ("you did not ask for
   this"), not an arithmetic one.

Then two outcome checks:

6. **Usefulness** — run `searchImage` with `parsed` and require `matched >= 1` where the row marks
   `rows`. For the purpose rows this is the real question: an Intent can be "right" and return
   nothing.
7. **Stability** — per row, the automatic verdict identical across 3 runs; report the flake rate.

### 4.2 Bounds: one-sided, two-sided, and direction

- A **one-sided** expectation (`bpm_max` only, from "slow") is *failed* by the addition of
  `bpm_min`: that is an invented bound. Precision catches it; a numeric tolerance would not.
- A **two-sided** expectation (`85..95`) is a partial recall when only one bound comes back, and a
  *narrowing* when the model returns `88..92`: both bounds present, the range wrong. Score
  `|parsed − expected|` per bound and separately flag a range that is strictly inside the expected
  one (the over-fill signature) or strictly outside it (the too-vague signature).
- A **tolerance** is per axis and is the prompt's own granularity, not a round number: BPM ±3
  (the prompt's examples use 125 and 95), the four 0–100 axes ±5, a `limit` that the sentence states
  exactly (`"10 songs"`) exact, and a `limit` derived from a duration a band (one hour → 12–18, per
  the spike's own note).

### 4.3 Synonyms

- **Keys**: compare through `keyCode` (`web/src/library/imageSource.ts` 111), which already folds
  Camelot and note names, so `F#m`, `F♯m` and "F sharp minor" are one key. This is also the only
  normalization the App itself performs downstream.
- **Moods**: the five heads are closed (`MOOD_VALUES`); the model's *word* need not be. "upbeat" →
  `happy` is a rubric synonym on the sentence side, not a value.
- **Genre**: case-insensitive, trimmed; a genre that is not in the profile's closed enum is a miss
  when a profile is in use, because the schema's enum drops it.
- **Everything else**: no synonymy. `query` must be compared as text (trimmed, case-folded) against
  the name the sentence wrote, and `isNameQuery` already encodes the App's own rule for whether a
  query is legitimate.

### 4.4 The empty Intent is a refusal, not an answer

`parseIntent` fails on an object with no filters, and `prompt.test.ts` says so out loud: "`{}` is
the designed no-filter answer, and `parseIntent` refuses it by design" (**VERIFIED**,
`web/src/playlists/ask/prompt.test.ts`, the `INTENT_EXAMPLES` case). `Ask.tsx` turns that into an
error message with a retry (**VERIFIED**, `Ask.tsx` 391–393). So "play something", "surprise me"
and "hello" are rows where the *correct* outcome is an error, and the row must be scored for **not inventing** a filter. Any
harness that expects `{}` to parse will report a false failure on the model's best answer. If the
product wants a real no-filter answer, that is a spec change (an empty `SearchArgs` that the search
accepts), not a prompt change.

### 4.5 What methodology to borrow, and what not to

- **Chrome's own evals course** is the vendor's guidance and the closest thing to a house style: it
  starts with "build rule-based evaluations" before any judge model, and treats a judge as the last
  layer ([Learn Evals](https://developer.chrome.com/docs/ai/evals)). For a 60-row corpus whose
  output is one JSON object, rule-based is the whole job.
- **Contract testing** is the right mental model, and this repository already lives by it:
  `parseIntent` is the contract, the corpus is the fixture set, and a prompt is an implementation
  detail of one side. The corpus belongs in a test file where it can be run without a model
  (Vitest, against recorded raw answers), and in the browser where the model is real.
- **JSON Schema validity** is a hard gate, but it is Chrome's gate, not the product's. Report it;
  do not score quality with it.
- **No LLM-as-judge.** The output is a closed object with ten numeric keys and four enums; there is
  nothing subjective to judge, and a judge model would add a second unmeasured model to a
  measurement problem. The one thing a judge would add — "does this sentence mean what the Intent
  says" — is exactly what the human Right/Wrong column is for, at n = 60.

---

## 5. What can raise the quality

| Option | Cost | Supported today? | Measurable by the harness? | Evidence it would help |
|---|---|---|---|---|
| Prompt wording | one string; re-measure | yes, app-owned | yes, `systemSha` | The one tightening attempt **lost** (8/12, ADR 0084). Do not repeat wholesale. |
| Schema `description`s on properties | a few hundred chars of context | **UNVERIFIED** — no Chrome example; run the JSON Schema Tester first | yes | none yet |
| Tighter `enum`/range/`pattern` | none | ranges and `additionalProperties` measured by the spike; `enum` not in Chrome docs | yes | Prevents out-of-set values, not invented ones |
| More / curated few-shot | context + latency; `measureContextUsage` can price it | yes (`initialPrompts`) | yes | The two known misses are **absent from `INTENT_EXAMPLES`** (§5.2) |
| Lower `temperature` / `topK` | — | **no**: ignored on web, extensions only | would silently measure nothing | Explainer deprecation notice |
| `samplingMode: "most-predictable"` | an origin trial for the App's origin; absent on stable | **origin trial only** | yes | Chrome calls it the mode for "testing … content extraction" |
| Two-pass "which filters are grounded?" | a second call: +0.4 s and +1–5 s, doubling the timeout risk | yes | yes | Plausible; the shipped repair is already a second call |
| Deterministic post-processing | pure code, no model | yes | yes, and unit-testable | The over-fill is exactly a "drop unrequested bounds" problem |
| One repair | already shipped | yes | with a trace sink | Already part of the shipped path |
| A different model / provider | key, network, an ADR reversal | no (ADR 0084 removed it) | yes | Out of scope; ADR 0061's WebLLM was never built |

### 5.1 The purpose-word case, specifically

The observed failure: "10 songs for a workout" →
`{bpm 130–160, energy 70–90, danceability 70–90, valence 60–80, arousal 60–80, sort: bpm}`. The
prompt's WORD→FILTER table says a purpose word maps to **two** filters (`energy_min`, `arousal_min`)
and the sentence's count maps to `limit`; the other three pairs and the sort are inventions. The
five-filter intersection is empty on a 1,495-track library with 1,259 analysed rows, so the user sees
"Nothing matched" — and `relaxRanges` cannot save it, because it returns `null` without a mood
(`playlists/relax.ts`, `relaxRanges`; `relax.test.ts` "drops the numeric ranges only when a mood is
left to search on").

What would fix it, in increasing cost:

1. **App-side, deterministic: a purpose-word normalizer.** After `parseIntent`, if the sentence
   contains a purpose word from the table and no explicit tempo/valence/danceability word, drop
   `bpm_*`, `danceability_*`, `valence_*` and any `sort` the sentence did not ask for. This is
   `isNameQuery`-shaped code: a small, tested function that uses the sentence it already has. It
   cannot fix an invented *value*, only an invented *filter*, which is exactly this failure. Risk:
   "fast techno for a workout" legitimately has `bpm_min`; the normalizer must key off the presence
   of an explicit tempo word, so the corpus needs both sentences (S2 and S10).
2. **App-side: let `relaxRanges` fire without a mood.** When the exact search returns nothing and
   the Intent has ≥3 numeric pairs, retry with the pair the table prescribes for the sentence's
   purpose word (or just the count). One extra local search, no model call. This is a search
   policy change and needs a spec line (k7-app §7 P2 currently names relaxation only "when a mood
   can stand in").
3. **Prompt: one rule and one few-shot.** Add "a purpose word sets the table's filters and the
   count; it never sets BPM, valence or danceability" and the example
   `"10 songs for a workout" → {"limit":10,"energy_min":70,"arousal_min":60}`. Cost: a prompt edit
   that must be re-measured on the whole corpus, because prompt changes have already lost once.
4. **`samplingMode: "most-predictable"`** — only after the app-side fixes are measured, and only as
   an origin-trial experiment with its own ADR; it changes the API surface for every user for a
   variance reduction nobody has yet quantified.

### 5.2 Two repo facts that make the few-shot option concrete

- `INTENT_EXAMPLES` (`ask/prompt.ts`) has **no workout row** and **no purpose row except sleep**.
  The two known misses — the artist and the purpose — are therefore the two things the examples do
  not teach. `Ask.tsx` even offers "10 songs for a workout" as one of three `OPENERS`
  (`chat.ts` 158–162): the app suggests the sentence the model is weakest at.
- The examples are already asserted to parse (`prompt.test.ts`), so adding one is a one-line test
  change plus a re-measure.

---

## 6. The corpus

### 6.1 How to read it

- **Expected Intent** is what `parseIntent` should yield for the *static* prompt
  (`profile: null`), in the App's camelCase, with the prompt's own thresholds (fast 125, slow 95,
  chill 40, workout 70, dance 70, sad 40, happy 60, arousal 60/65). With a profile the same row's
  numbers become that library's quartiles — the harness reads `profile.*.p75`/`p25`, the same
  quantities `factsOf` uses (`ask/facts.ts`) — so only the *shape* of the expectation is fixed.
- **Must** is the row's pass condition, in the codes of §4.1: `parse` (a runnable Intent),
  `fail` (a refusal is the correct outcome), `{a,b}` required keys, `!{a,b}` forbidden keys,
  `≤n` at most n numeric pairs, `~v` within tolerance, `rows` non-empty result on the reference
  library.
- Rows marked **S** are the twelve spike sentences, kept verbatim as the regression subset.
- Every row is run 3×; a row passes only if its automatic verdict is identical in all three.

| # | Sentence | Expected Intent (static) | Must |
|---|---|---|---|
| S1 | `tracks between 85 and 95 BPM` | `{bpm_min:85,bpm_max:95}` | `parse {bpm_min,bpm_max} ≤2 ~85 ~95 rows` |
| S2 | `fast aggressive techno` | `{genre:"techno",mood:["aggressive"],bpm_min:125,arousal_min:65}` | `parse {genre,mood} {bpm_min} ≤3` |
| S3 | `happy party songs` | `{mood:["happy","party"]}` | `parse {mood} !{query} ≤1` |
| S4 | `sad and slow` | `{mood:["sad"],valence_max:40,bpm_max:95}` | `parse {mood} !{bpm_min} ≤2` |
| S5 | `something in A minor` | `{key:["Am"]}` | `parse {key} !{query} ≤1` |
| S6 | `Camelot 8A` | `{key:["8A"]}` | `parse {key} !{query} key=8A` |
| S7 | `about an hour of relaxed music` | `{mood:["relaxed"],limit:15}` | `parse {mood,limit} limit∈[12,18] !{bpm}` |
| S8 | `the 20 most danceable tracks` | `{sort:"danceability",limit:20}` | `parse {sort,limit} limit=20` (see note) |
| S9 | `songs by Daft Punk` | `{query:"Daft Punk"}` | `parse {query} !{genre,mood}` |
| S10 | `10 songs for a workout` | `{limit:10,energy_min:70,arousal_min:60}` | `parse {limit} limit=10 !{bpm,danceability,valence} ≤2 rows` |
| S11 | `música alegre para bailar` | `{mood:["happy"],danceability_min:70}` | `parse {mood} rows?` |
| S12 | `de la musique calme pour lire` | `{mood:["relaxed"],energy_max:40}` | `parse {mood} rows?` |
| 13 | `tracks under 100 BPM` | `{bpm_max:100}` | `parse {bpm_max} !{bpm_min} rows` |
| 14 | `at least 130 BPM` | `{bpm_min:130}` | `parse {bpm_min} !{bpm_max} ~130` |
| 15 | `slow` | `{bpm_max:95}` | `parse {bpm_max} !{bpm_min}` |
| 16 | `fast` | `{bpm_min:125}` | `parse {bpm_min} !{bpm_max}` |
| 17 | `very slow` | `{bpm_max:≤95}` | `parse {bpm_max} bpm_max≥70 !{bpm_min}` |
| 18 | `140 to 120 BPM` | `{bpm_min:120,bpm_max:140}` | `parse bpm_min≤140` (the parser swaps, not the model) |
| 19 | `techno` | `{genre:"techno"}` | `parse {genre} !{query} ≤1` |
| 20 | `some jazz please` | `{genre:"jazz"}` | `parse {genre} !{query}` |
| 21 | `reggaeton for the party` | `{genre:"reggaeton",mood:["party"],danceability_min:70}` | `parse {genre,mood,danceability_min} ≤3` |
| 22 | `lo-fi beats to relax to` | `{mood:["relaxed"],energy_max:40}` | `parse {mood} !{query:"lo-fi"} ≤2` |
| 23 | `the album Discovery` | `{query:"Discovery"}` | `parse {query} !{genre}` |
| 24 | `play Blue Monday` | `{query:"Blue Monday"}` | `parse {query} !{genre,mood} ≤1` |
| 25 | `songs by the Beatles` | `{query:"the Beatles"}` | `parse {query contains "beatles"} !{genre,mood}` |
| 26 | `music like Radiohead` | `{query:"Radiohead"}` | `parse {query} !{genre}` |
| 27 | `something like Bonobo but faster` | `{query:"Bonobo",bpm_min:125}` | `parse {query,bpm_min} ≤2` |
| 28 | `a song called Nightcall` | `{query:"Nightcall"}` | `parse {query} !{mood,genre}` |
| 29 | `something in F# minor` | `{key:["F#m"]}` | `parse {key} keyCode≠0 !{query}` |
| 30 | `B flat major` | `{key:["Bb"]}` | `parse {key} keyCode≠0 !{query}` |
| 31 | `tracks in 8A or 9A` | `{key:["8A","9A"]}` | `parse {key} both present !{query}` |
| 32 | `E minor to dance to` | `{key:["Em"],danceability_min:70}` | `parse {key,danceability_min}` |
| 33 | `happy` | `{mood:["happy"],valence_min:60}` | `parse {mood} ~60 !{bpm}` |
| 34 | `sad songs` | `{mood:["sad"],valence_max:40}` | `parse {mood,valence_max} !{query}` |
| 35 | `relaxing music` | `{mood:["relaxed"],energy_max:40}` | `parse {mood} ≤2 !{query}` |
| 36 | `aggressive` | `{mood:["aggressive"],arousal_min:65}` | `parse {mood} ≤2` |
| 37 | `party` | `{mood:["party"],danceability_min:70}` | `parse {mood} ≤2` |
| 38 | `music for sleeping` | `{mood:["relaxed"],energy_max:40}` | `parse {mood,energy_max} !{bpm} ≤2 rows` |
| 39 | `something to study to` | `{mood:["relaxed"],energy_max:40}` | `parse {mood} !{bpm,danceability,valence} ≤2` |
| 40 | `party playlist` | `{mood:["party"],danceability_min:70}` | `parse {mood} !{bpm} ≤2` |
| 41 | `music for a dinner party` | `{mood:["party"],danceability_min:70}` | `parse {mood} !{bpm,valence} ≤2 rows` |
| 42 | `workout` | `{energy_min:70,arousal_min:60}` | `parse {energy_min} !{bpm,danceability,valence} ≤2` |
| 43 | `something for the gym` | `{energy_min:70,arousal_min:60}` | `parse {energy_min} !{bpm} ≤2 rows` |
| 44 | `50 tracks` | `{limit:50}` | `parse {limit} limit=50 ≤1` |
| 45 | `three hours of ambient` | `{genre:"ambient",limit:45}` | `parse {genre,limit} limit∈[30,50]` |
| 46 | `the whole library` | `{limit:100}` | `parse {limit} limit=100` |
| 47 | `skip 40` | `{offset:40}` | `parse {offset} offset=40 ≤2` |
| 48 | `sort by BPM` | `{sort:"bpm"}` | `parse {sort} sort="bpm"` |
| 49 | `alphabetical` | `{sort:"title"}` | `parse {sort} sort="title"` |
| 50 | `música triste` | `{mood:["sad"],valence_max:40}` | `parse {mood} !{query}` |
| 51 | `entspannte Musik zum Lesen` | `{mood:["relaxed"],energy_max:40}` | `parse {mood} !{query}` |
| 52 | `静かな曲` | `{mood:["relaxed"],energy_max:40}` | `parse {mood} !{query}` |
| 53 | `play something` | `{}` then a refusal | `fail !{query,bpm,mood,genre}` (see §4.4) |
| 54 | `hello` | `{}` then a refusal | `fail !{anything but limit}` |
| 55 | `make it faster` | `{}` then a refusal | `fail !{bpm,danceability,valence}` (no context; ADR 0086) |
| 56 | `anything but country` | `{}` then a refusal | `fail !{genre:"country"}` (negation unsupported) |
| 57 | `music that isn't sad` | `{}` or `{mood:["happy"]}` | `parse or fail !{valence_max}` |
| 58 | `songs between 300 and 400 BPM` | `{bpm_min:180,bpm_max:180}` | `parse bpm_max=180` (clamp) |
| 59 | `add 5 songs to the playlist` | `{}` then a refusal | `fail !{limit,query}` (not a search) |
| 60 | `play the song from that car video` | `{}` then a refusal | `fail !{query}` (no name in the sentence) |

**S8 is a rubric trap worth keeping.** The device sorts ascending only (the prompt says so; and
`searchImage`'s `compare` is `a - b`, `imageSource.ts` 258–273), so
`{sort:"danceability",limit:20}` returns the *least* danceable twenty. The Intent is the best the
contract can express and the result is the opposite of the sentence. Keep the row, expect the
Intent, and record that the answer is wrong for a reason the model cannot fix — it is a contract
gap, not a prompt failure.

**The languages.** Spanish, German and Japanese rows are gated: the model's set is `en`, `ja`, `es`,
`de`, `fr` (**VERIFIED**, docs). The app sets no `expectedInputs`, so these rows measure the model's
multilingual behaviour, not a configured language.

---

## 7. Recommendation

**Stage 0 — freeze the rubric (no code).** Take §6 into a ticket under `docs/design/`, with the 60
rows and their `must` codes, and the rule that a row passes only on 3/3 identical verdicts. Decide
now what "better" means: the shipped baseline is **unknown**, so the first run defines it.

**Stage 1 — the harness, measuring only.** Build `web/evals/` + `web/src/evals/`, importing the real
assistant modules. Add the optional `trace` parameter to `requestIntent` (behaviour unchanged).
Run three arms on one machine: `systemOnly` (the 10/12 control), `systemExamples`, and
`requestIntent` with the static profile; then `requestIntent` with the Last image's stored profile,
plus the search in scope 3. Record `chrome://on-device-internals`. Ship no product change. Success: a JSON export and a
first honest number for the shipped input, with per-row verdicts and repairs counted.

**Stage 2 — the cheap fixes, one at a time, each re-measured.** In this order:
(a) `relaxRanges` for a range-heavy Intent without a mood (search policy; spec line in k7-app §7);
(b) the purpose-word normalizer after `parseIntent` (pure, unit-tested in Vitest, corpus rows 38–43
and S10 the gate); (c) one purpose few-shot row and one sentence of prompt rule — only if (a) and
(b) leave rows failing, and only with a full re-run, because a prompt edit is the change that has
already lost once (8/12).
Success: S10 and rows 38–43 pass `must` in 3/3, no spike row regresses below its Stage-1 verdict,
and the shipped path's right-count is ≥ the Stage-1 number.

**Stage 3 — optional, and only with an ADR.** `samplingMode: "most-predictable"` behind the Sampling
Parameters origin trial, if Stage 1's flake rate is high enough to matter. It changes the API
surface for every user and cannot ship on stable.

**What not to do.**

- Do not set `temperature`/`topK`: they are ignored in a page, and a harness that reports "we tried
  a lower temperature" would be reporting a no-op.
- Do not tighten the prompt wholesale; that measurement exists and it is 8/12.
- Do not add an LLM judge to a closed-object task, and do not let an LLM write the expected Intents:
  the expectations are the contract, and a model-written expectation cannot fail.
- Do not put the harness behind a shipped route, and do not import it from `src/main.tsx`.
- Do not claim CI coverage of the model, and do not move the e2e job's activation criteria to
  include it.
- Do not edit the prompt without a corpus run; the ADR-driven rule and the 8/12 both say so.

---

## Sources

Chrome documentation (all read 2026-11-14):

- [The Prompt API](https://developer.chrome.com/docs/ai/prompt-api) — availability, create, sampling,
  initial prompts, prefix, expected inputs/outputs, append, responseConstraint, sessions, workers,
  policy.
- [Structured output support for the Prompt API](https://developer.chrome.com/docs/ai/structured-output-for-prompt-api)
  — schema examples, the tester, the `SyntaxError`/`NotSupportedError` behaviour.
- [Understand built-in model management in Chrome](https://developer.chrome.com/docs/ai/understand-built-in-model-management)
  — download trigger, GPU probe and variants, updates, purges.
- [Get started with built-in AI](https://developer.chrome.com/docs/ai/get-started) — hardware floor,
  per-origin download.
- [Session management](https://developer.chrome.com/docs/ai/session-management),
  [Debug the built-in model](https://developer.chrome.com/docs/ai/debug-built-in-model) — context and
  on-device internals.
- [Learn Evals](https://developer.chrome.com/docs/ai/evals) — the vendor's eval course, rule-based
  first.
- [JSON Schema Tester](https://googlechrome.github.io/samples/json-schema-tester/) — the documented
  way to check that a schema is accepted.

Specification and source:

- [Prompt API explainer](https://github.com/webmachinelearning/prompt-api/blob/main/README.md) —
  `responseConstraint` (schema or RegExp), error types, `omitResponseConstraintInput`,
  `samplingMode`, the deprecation of `topK`/`temperature` on the web, `thinkingEffort`, language
  expectations, workers.
- Chromium at `88fbc01a19ead9ccbe32f3f858a4c82aa54b53f8`:
  [`availability.idl`](https://github.com/chromium/chromium/blob/88fbc01a19ead9ccbe32f3f858a4c82aa54b53f8/third_party/blink/renderer/modules/ai/availability.idl),
  [`language_model_create_options.idl`](https://github.com/chromium/chromium/blob/88fbc01a19ead9ccbe32f3f858a4c82aa54b53f8/third_party/blink/renderer/modules/ai/language_model_create_options.idl),
  [`on_device_model_component.h`](https://github.com/chromium/chromium/blob/88fbc01a19ead9ccbe32f3f858a4c82aa54b53f8/components/optimization_guide/core/model_execution/on_device_model_component.h),
  [`optimization_guide_features.cc`](https://github.com/chromium/chromium/blob/88fbc01a19ead9ccbe32f3f858a4c82aa54b53f8/components/optimization_guide/core/optimization_guide_features.cc),
  [`on_device_model.mojom`](https://github.com/chromium/chromium/blob/88fbc01a19ead9ccbe32f3f858a4c82aa54b53f8/services/on_device_model/public/mojom/on_device_model.mojom).
- Chromium at `fbff40b173fab44a55950051b8d40fdf9b267926` (October 2026):
  [`language_model.idl`](https://github.com/chromium/chromium/blob/fbff40b173fab44a55950051b8d40fdf9b267926/third_party/blink/renderer/modules/ai/language_model.idl),
  [`language_model_create_options.idl`](https://github.com/chromium/chromium/blob/fbff40b173fab44a55950051b8d40fdf9b267926/third_party/blink/renderer/modules/ai/language_model_create_options.idl)
  (`samplingMode`, `prefix`, tools),
  [`runtime_enabled_features.json5`](https://github.com/chromium/chromium/blob/fbff40b173fab44a55950051b8d40fdf9b267926/third_party/blink/renderer/platform/runtime_enabled_features.json5)
  (`AIPromptAPI` stable, `AIPromptAPIParams` origin trial).

Third party (REPORTED, not relied on for any API fact):

- [`@browserless/ai` README](https://www.npmjs.com/package/@browserless/ai/v/13.9.8) — Chrome for
  Testing does not download the model.
- [Microlink, "Chrome built-in AI from Node.js"](https://microlink.io/blog/chrome-built-in-ai-from-nodejs)
  — no component updater in a headless fleet.
- [tmc/misc Chrome AI investigation](https://github.com/tmc/misc/blob/bbbb7f1bdbdab33fc79a2357d7510bfd0cef87e0/chrome-to-har/INVESTIGATION_RESULTS.md)
  — claims `LanguageModel` is not exposed to programmatically launched Chrome; conflicts with the
  browserless reports and is unresolved.

This repository:

- `web/src/playlists/ask/` — `request` (`requestIntent`, `repairQuery`), `parse` (`parseIntent`,
  `isNameQuery`), `prompt` (`buildSystemPrompt`, `INTENT_EXAMPLES`/`intentExamples`,
  `INTENT_SCHEMA`/`intentSchema`), and the `purpose`, `facts`, `keys` and `contract` modules behind
  them.
- `web/src/playlists/relax.ts` — `relaxRanges`.
- `web/src/builtin-model.ts` — `askModel`: one session per ask, `initialPrompts`, one
  `session.prompt(sentence, {responseConstraint})`, no sampling option.
- `web/src/playlists/chat.ts`, `web/src/ui/areas/Ask.tsx` — the deterministic answer, the error path,
  the `OPENERS`.
- `web/src/library/source.ts`, `web/src/library/imageSource.ts` — the Last image search.
- `web/src/domain/key.ts` — `keyCode`, `camelotName`: the device's key grammar.
- `docs/adr/0084-…`, `docs/adr/0086-…`, `docs/specs/k7-app.md` §7 — the contract, the measured
  number, the conversation.
- `.scratch/nano-spike/build.mjs`, `page.html` — the APP-11 harness, and why its 10/12 is not the
  shipped input.
