# The Ask evaluation rubric

**Status:** frozen, before the first run. This is the record the first measurement is scored
against; a change to it is a change to the contract, not a tuning of the harness.

**Provenance.** The plan is [`docs/research/assistant-evaluation.md`](../../research/assistant-evaluation.md),
written against `646ca76`. The tree was ten commits past it when this was frozen, and `ask.ts`
had moved once (`b7a8718`). Every function the note cites was checked against the current code;
its line numbers are stale (today: `relaxRanges` 391, `factsOf` 439, `buildSystemPrompt` 488,
`INTENT_EXAMPLES` 564, `requestIntent` 687). The note's claims about the Prompt API are taken as
research, not re-verified; no model was run while writing this.

**The question.** Ask turns one sentence into an **Intent** — `library_search` filters, never
results — using Chrome's built-in model. What can be changed, what should a local harness look
like, how should a sentence be scored, and what does the shipped input actually score? The last
number in the record is 10/12, and it measured a different input (§1.4 of the note): a static
prompt, no few-shot turns, no library profile, no repairs. **The shipped input has never been
measured.** The first run is not a re-run of the old number; it is the first run of this one.

## 1. Vocabulary

- **Sentence** — one line a person types. The model sees it; the scorer does too, because a
  purpose word is the sentence's, not the Intent's.
- **Intent** — the `SearchArgs` the model produces, camelCase in the App. A **refusal** is the
  empty Intent: `parseIntent` fails on it by design, so "play something" ends in an error message
  and a refusal is the *correct* answer, not a failure (§4.4).
- **Row** — one sentence plus its expected Intent and its `must` predicate.
- **Arm** — which input the model gets: `systemOnly` (the system prompt alone, the 10/12 frame),
  `systemExamples` (plus the few-shot turns), `requestIntent` (the shipped path, with repairs).
- **Scope** — how much of the browser's library is in play: `static` (`profile: null`), `image`
  (the Last image's stored profile), `imageSearch` (plus the real `searchImage`).
- **Run** — one sentence, one arm, one scope, one repetition. **Row verdict** — the three runs'
  automatic verdicts, which must agree (below).
- **Automatic verdict** — `scoreRow`'s pass/fail/unscored. **Human verdict** — `right` / `wrong`
  / `unsure`, per row, the tie-break for what the parser cannot judge.

## 2. The corpus

`web/src/evals/corpus.ts` is the data; this table is the same data as the frozen record. It is
sixty rows from the note's §6 plus six added while porting. `S1`–`S12` are the spike's sentences,
kept verbatim as the regression subset; `N1`–`N6` are the added rows. `parse` needs a runnable
Intent, `fail` needs a refusal, `either` accepts both; `{a,b}` are required filters, `!{a,b}`
forbidden ones, `≤Np` the most measurement axes, `~key:value` a tolerance, `key=value` an exact
value, `rows` a non-empty result on the reference library.

| # | Sentence | Expected Intent | Must |
|---|---|---|---|
| S1 | `tracks between 85 and 95 BPM` | `{"bpmMin":85,"bpmMax":95}` | `parse {bpmMin,bpmMax} ≤1p ~bpmMin:3 ~bpmMax:3 rows` |
| S2 | `fast aggressive techno` | `{"genre":"techno","mood":["aggressive"],"bpmMin":125,"arousalMin":65}` | `parse {genre,mood,bpmMin} ≤2p` |
| S3 | `happy party songs` | `{"mood":["happy","party"]}` | `parse {mood} !{query} ≤0p` |
| S4 | `sad and slow` | `{"mood":["sad"],"valenceMax":40,"bpmMax":95}` | `parse {mood} !{bpmMin} ≤2p` |
| S5 | `something in A minor` | `{"key":["Am"]}` | `parse {key} !{query} ≤0p key=["Am"]` |
| S6 | `Camelot 8A` | `{"key":["8A"]}` | `parse {key} !{query} ≤0p key=["8A"]` |
| S7 | `about an hour of relaxed music` | `{"mood":["relaxed"],"limit":15}` | `parse {mood,limit} !{bpmMin,bpmMax} ~limit:[12,18]` |
| S8 | `the 20 most danceable tracks` | `{"sort":"danceability","limit":20}` | `parse {sort,limit} sort="danceability" limit=20` |
| S9 | `songs by Daft Punk` | `{"query":"Daft Punk"}` | `parse {query} !{genre,mood} query="Daft Punk"` |
| S10 | `10 songs for a workout` | `{"limit":10,"energyMin":70,"arousalMin":60}` | `parse {limit} !{bpmMin,bpmMax,danceabilityMin,danceabilityMax,valenceMin,valenceMax} ≤2p limit=10 rows` |
| S11 | `música alegre para bailar` | `{"mood":["happy"],"danceabilityMin":70}` | `parse {mood} ≤1p` |
| S12 | `de la musique calme pour lire` | `{"mood":["relaxed"],"energyMax":40}` | `parse {mood} ≤1p` |
| 13 | `tracks under 100 BPM` | `{"bpmMax":100}` | `parse {bpmMax} !{bpmMin} ≤1p ~bpmMax:3 rows` |
| 14 | `at least 130 BPM` | `{"bpmMin":130}` | `parse {bpmMin} !{bpmMax} ≤1p ~bpmMin:3` |
| 15 | `slow` | `{"bpmMax":95}` | `parse {bpmMax} !{bpmMin} ≤1p` |
| 16 | `fast` | `{"bpmMin":125}` | `parse {bpmMin} !{bpmMax} ≤1p` |
| 17 | `very slow` | `{"bpmMax":95}` | `parse {bpmMax} !{bpmMin} ≤1p ~bpmMax:5` |
| 18 | `140 to 120 BPM` | `{"bpmMin":120,"bpmMax":140}` | `parse {bpmMin,bpmMax} ≤1p bpmMin=120 bpmMax=140` |
| 19 | `techno` | `{"genre":"techno"}` | `parse {genre} !{query} ≤0p` |
| 20 | `some jazz please` | `{"genre":"jazz"}` | `parse {genre} !{query} ≤0p` |
| 21 | `reggaeton for the party` | `{"genre":"reggaeton","mood":["party"],"danceabilityMin":70}` | `parse {genre,mood,danceabilityMin} ≤1p` |
| 22 | `lo-fi beats to relax to` | `{"mood":["relaxed"],"energyMax":40}` | `parse {mood} !{query,bpmMin,bpmMax} ≤1p` |
| 23 | `the album Discovery` | `{"query":"Discovery"}` | `parse {query} !{genre} query="Discovery"` |
| 24 | `play Blue Monday` | `{"query":"Blue Monday"}` | `parse {query} !{genre,mood} ≤0p query="Blue Monday"` |
| 25 | `songs by the Beatles` | `{"query":"the Beatles"}` | `parse {query} !{genre,mood} query∈{the Beatles|Beatles}` |
| 26 | `music like Radiohead` | `{"query":"Radiohead"}` | `parse {query} !{genre} query="Radiohead"` |
| 27 | `something like Bonobo but faster` | `{"query":"Bonobo","bpmMin":125}` | `parse {query,bpmMin} ≤1p query="Bonobo"` |
| 28 | `a song called Nightcall` | `{"query":"Nightcall"}` | `parse {query} !{mood,genre} query="Nightcall"` |
| 29 | `something in F# minor` | `{"key":["F#m"]}` | `parse {key} !{query} ≤0p key=["F#m"]` |
| 30 | `B flat major` | `{"key":["Bb"]}` | `parse {key} !{query} ≤0p key=["Bb"]` |
| 31 | `tracks in 8A or 9A` | `{"key":["8A","9A"]}` | `parse {key} !{query} ≤0p key=["8A","9A"]` |
| 32 | `E minor to dance to` | `{"key":["Em"],"danceabilityMin":70}` | `parse {key,danceabilityMin} ≤1p` |
| 33 | `happy` | `{"mood":["happy"],"valenceMin":60}` | `parse {mood} !{bpmMin,bpmMax} ≤1p ~valenceMin:5` |
| 34 | `sad songs` | `{"mood":["sad"],"valenceMax":40}` | `parse {mood,valenceMax} !{query} ≤1p` |
| 35 | `relaxing music` | `{"mood":["relaxed"],"energyMax":40}` | `parse {mood} !{query} ≤1p` |
| 36 | `aggressive` | `{"mood":["aggressive"],"arousalMin":65}` | `parse {mood} ≤1p` |
| 37 | `party` | `{"mood":["party"],"danceabilityMin":70}` | `parse {mood} ≤1p` |
| 38 | `music for sleeping` | `{"mood":["relaxed"],"energyMax":40}` | `parse {mood,energyMax} !{bpmMin,bpmMax} ≤1p rows` |
| 39 | `something to study to` | `{"mood":["relaxed"],"energyMax":40}` | `parse {mood} !{bpmMin,bpmMax,danceabilityMin,danceabilityMax,valenceMin,valenceMax} ≤1p` |
| 40 | `party playlist` | `{"mood":["party"],"danceabilityMin":70}` | `parse {mood} !{bpmMin,bpmMax} ≤1p` |
| 41 | `music for a dinner party` | `{"mood":["party"],"danceabilityMin":70}` | `parse {mood} !{bpmMin,bpmMax,valenceMin,valenceMax} ≤1p rows` |
| 42 | `workout` | `{"energyMin":70,"arousalMin":60}` | `parse {energyMin} !{bpmMin,bpmMax,danceabilityMin,danceabilityMax,valenceMin,valenceMax} ≤2p` |
| 43 | `something for the gym` | `{"energyMin":70,"arousalMin":60}` | `parse {energyMin} !{bpmMin,bpmMax} ≤2p rows` |
| 44 | `50 tracks` | `{"limit":50}` | `parse {limit} ≤0p limit=50` |
| 45 | `three hours of ambient` | `{"genre":"ambient","limit":45}` | `parse {genre,limit} ≤0p ~limit:[30,50]` |
| 46 | `the whole library` | `{"limit":100}` | `parse {limit} ≤0p limit=100` |
| 47 | `skip 40` | `{"offset":40}` | `parse {offset} ≤0p offset=40` |
| 48 | `sort by BPM` | `{"sort":"bpm"}` | `parse {sort} ≤0p sort="bpm"` |
| 49 | `alphabetical` | `{"sort":"title"}` | `parse {sort} ≤0p sort="title"` |
| 50 | `música triste` | `{"mood":["sad"],"valenceMax":40}` | `parse {mood} !{query} ≤1p` |
| 51 | `entspannte Musik zum Lesen` | `{"mood":["relaxed"],"energyMax":40}` | `parse {mood} !{query} ≤1p` |
| 52 | `静かな曲` | `{"mood":["relaxed"],"energyMax":40}` | `parse {mood} !{query} ≤1p` |
| 53 | `play something` | `— (refusal)` | `fail !{query,bpmMin,bpmMax,mood,genre}` |
| 54 | `hello` | `— (refusal)` | `either !{query,genre,key,mood,sort,bpmMin,bpmMax,energyMin,energyMax,danceabilityMin,danceabilityMax,valenceMin,valenceMax,arousalMin,arousalMax,durationMin,durationMax}` |
| 55 | `make it faster` | `— (refusal)` | `fail !{bpmMin,bpmMax,danceabilityMin,danceabilityMax,valenceMin,valenceMax}` |
| 56 | `anything but country` | `— (refusal)` | `fail !{genre}` |
| 57 | `music that isn't sad` | `{"mood":["happy"]}` | `either !{valenceMax}` |
| 58 | `songs between 300 and 400 BPM` | `{"bpmMin":180,"bpmMax":180}` | `parse {bpmMin,bpmMax} ≤1p bpmMin=180 bpmMax=180` |
| 59 | `add 5 songs to the playlist` | `— (refusal)` | `fail !{limit,query}` |
| 60 | `play the song from that car video` | `— (refusal)` | `fail !{query}` |
| N1 | `rápido para correr` | `{"bpmMin":125,"energyMin":70,"arousalMin":60}` | `parse {bpmMin,energyMin} !{danceabilityMin,danceabilityMax,valenceMin,valenceMax} ≤3p` |
| N2 | `tracks under three minutes` | `{"durationMax":180}` | `parse {durationMax} !{durationMin,limit} ≤1p durationMax=180` |
| N3 | `nothing aggressive` | `— (refusal)` | `fail !{mood,arousalMin,arousalMax,genre}` |
| N4 | `tracks by Daft Punk and Justice` | `{"query":"Daft Punk"}` | `parse {query} !{genre,mood,bpmMin,bpmMax} ≤0p query∈{Daft Punk|Justice}` |
| N5 | `similar to Bonobo` | `{"query":"Bonobo"}` | `parse {query} !{genre,mood,bpmMin,bpmMax,energyMin,energyMax,danceabilityMin,danceabilityMax,valenceMin,valenceMax,arousalMin,arousalMax,durationMin,durationMax} ≤0p query="Bonobo"` |
| N6 | `fast techno for a workout` | `{"genre":"techno","bpmMin":125,"energyMin":70,"arousalMin":60}` | `parse {genre,bpmMin,energyMin} !{danceabilityMin,danceabilityMax,valenceMin,valenceMax} ≤3p` |

`corpus.test.ts` reads this table and fails if a row's id and sentence are not in it, so the prose
cannot drift from the data.

### 2.1 The six added rows

They cover gaps the note names but its table does not test: `N1` an explicit tempo plus a purpose
word in another language (the multilingual normalizer's positive case), `N2` a track's length in
minutes (the key the App distinguishes from a count), `N3` a negated mood (a refusal, not the
opposite filter), `N4` two artist names (the contract has no OR), `N5` the prompt's own "similar
to X" phrasing, and `N6` the guard sentence §5.1 names — "fast techno for a workout" — which is
absent from §6 even though the normalizer's whole risk is misreading it.

### 2.2 Corpus fixes, and why

Every row's sentence is the note's; only predicates and a few expectations changed.

- **17 `very slow`** — the note's expected Intent is `{bpm_max:≤95}`, which is not a number and
  cannot be compared. The prompt fixes "very slow" at `bpm_max 95`, so the row expects 95 with a
  five-point tolerance and forbids `bpmMin`. The old `bpm_max≥70` gate was true of any clamped
  value and tested nothing.
- **18 `140 to 120 BPM`** — the note asserts `bpm_min≤140`, which is satisfied by the *unswapped*
  pair and so cannot catch the parser's swap. The row now asserts the parsed result: 120/140.
- **57 `music that isn't sad`** — "parse or fail" was not expressible. It is `outcome: 'either'`
  with `valenceMax` forbidden: a refusal and a happy reading both pass, and the exact wrong
  reading fails.
- **58 `songs between 300 and 400 BPM`** — both bounds clamp to the folded range; the row asserts
  `180/180`, so a parser change that stops clamping is caught.
- **S8 `the 20 most danceable tracks`** — `contractGap: true`. The Intent is the best the contract
  can express; `searchImage` sorts ascending only, so the result is the least danceable twenty.
  It is reported separately and never counted as a model failure.
- **22 `lo-fi beats to relax to`** — `!{query:"lo-fi"}` banned a value inside a key-level notation.
  It is a ban on `query` (with the BPM pair), which is what the note meant.
- **25 `songs by the Beatles`** — "query contains beatles" is a case-folded one-of, not a key.
- **56 `anything but country`** — `!{genre:"country"}` is a genre ban; since the expected outcome
  is a refusal, a parsed `{genre:"country"}` already fails, and the ban documents why.
- **54 `hello`** — `!{anything but limit}` meant "a bare limit is tolerable". It is `either` plus a
  forbidden list that leaves `limit` and `offset` alone.
- **S3 `happy party songs`** — unchanged. The prompt's table says `party, dance` sets
  `mood ["party"], danceability_min`; S3's expected omits the danceability, so precision's
  `allowed` set includes the matched purpose row's filters. A model following the prompt is not
  punished by the rubric.
- **S2 / S10** — the note's `≤3`/`≤2` counted keys in places. `maxPairs` counts measurement axes,
  and each row's number is the axes its sentence justifies.

## 3. The predicate

`Must` (in `corpus.ts`) is the only pass condition; `scoreRow` (in `score.ts`) evaluates it.

- **outcome** — `parse`, `fail`, or `either`.
- **required / forbidden** — filter keys, checked one by one against `parseIntent`'s output.
- **maxPairs** — measurement axes, not keys. `bpmMin`+`bpmMax` is one axis; `limit` is not an axis.
- **tolerances** — an absolute ± or an inclusive interval, per key.
- **exact** — numbers exactly, strings case-folded and trimmed, arrays as sets (keys by `keyCode`).
- **queryOneOf** — a query must be one of these, case-folded.
- **rows: 'nonEmpty'** — the reference search must return at least one track; without a search the
  row is `unscored`, not passed.
- **contractGap** — a row-level flag: the Intent is right, the contract cannot express the
  sentence, and the row is reported separately.
- **derived** — keys whose number is the library's, resolved through `factsOf(profile)` so the
  prompt and the scorer read one table. `S1`'s 85/95 are the sentence's and never derived.

## 4. The axes

1. **Shape** — `raw` is JSON exactly as `responseConstraint` should have made it, and satisfies
   `intentSchema(profile)`. Chrome's promise, not the product's; reported per run, never the
   verdict.
2. **Contract** — `parseIntent(raw)` returns `SearchArgs`, not a failure. This is what the App can
   run. Stricter than shape in one way: an empty object is a failure.
3. **Recall** — of the expected filters, the share present in the parsed Intent. A pair is one
   filter; pairs count once.
4. **Precision** — of the parsed filters, the share the row justifies: expected filters plus the
   matched purpose row's own filters. A filter that is in neither is an invention, even when it is
   a filter the contract has.
5. **Forbidden** — the row's blacklist must be empty. Separate from precision because a forbidden
   key is a product failure ("I did not ask for this"), not an arithmetic one.
6. **Bounds** — per side: an **invented** bound beside a one-sided expectation fails; a range
   strictly inside the expected one is `narrowed`; strictly outside is `too-vague`; one side
   missing is `partial`. Tolerance decides whether a narrowed or vague range also fails.
7. **Usefulness** — the reference search with the Intent that ran (the parsed one, or its
   normalized form on the shipped arm) returns at least one track, where the row marks `rows`.
8. **Stability** — the row's three automatic verdicts are identical (below).

## 5. The pass rule

- A sentence runs **three times** per arm and scope.
- A row **passes only when all three automatic verdicts are `pass`**. Two passes and a failure is
  `unstable`, not a pass; fewer than three runs cannot pass.
- A row marked `contractGap` is reported separately and excluded from the model's numerator and
  denominator.
- A row that asks for `rows` at a scope without a search is `unscored`; it is not a failure, and it
  is not a pass.
- The human verdict is per row. Where it disagrees with the row's automatic verdict, the harness
  flags a **rubric bug**: the disagreement is a defect in this document, not in the model.

The shipped arm also records **`normalized`** when the purpose normalizer changed the model's
Intent. It is reported separately and on purpose: a normalized run explains part of a change in
the shipped number, and it is not evidence that the model improved. The row's verdict is still the
model's Intent scored against this rubric.

## 6. The first run

The matrix is five combinations, not the 3×3 cross product, because the two control arms exist to
be compared with the 10/12 frame, which was static:

| Arm | Scope |
|---|---|
| `systemOnly` | `static` |
| `systemExamples` | `static` |
| `requestIntent` | `static` |
| `requestIntent` | `image` |
| `requestIntent` | `imageSearch` |

That is 66 rows × 3 runs × 5 combinations = **990 model calls**. The harness saves each finished
row, so a sitting can be resumed; the model can disappear mid-run (Chrome updates it by hot swap),
and the run records the error, continues, and marks the affected rows invalid.

**What the first run defines as the baseline.** The per-arm, per-row automatic verdicts and the
human verdicts from one machine, one Chrome, one model variant. From then on:

- a number is **comparable** only when `systemSha`, `schemaSha`, the examples hash, the arm, the
  scope and the recorded model variant all match;
- a later run **improves** on the baseline when the shipped arm's passing rows are at least the
  baseline's and no spike row regresses below its baseline verdict;
- a number without a recorded `chrome://on-device-internals` variant is a number about a machine,
  not about Ask.

**What the baseline is not.** It is not the 10/12 and it does not replace it: 10/12 measured a
static prompt with no examples, no profile and no repairs. The two numbers answer different
questions and neither can be compared to the other.

## 7. What this rubric does not do

- No `temperature` or `topK`: Chrome ignores both in a page, and a harness that set them would be
  measuring a no-op.
- No LLM judge: the output is a closed object, and a second unmeasured model in a measurement
  problem is the thing this avoids.
- No model-written expected Intents. The expectations are the note's table and the prompt's own
  WORD→FILTER table; a model-written expectation cannot fail.
- No `samplingMode`: it is an origin trial, absent on stable, and a different API surface. If the
  first run's flake rate justifies it, that is a separate ADR, not a harness setting.
- No CI coverage of the model. CI runs the corpus, the scorer and the fixtures; the model is a
  manual, local, real-Chrome run.

**Prepared, not applied.** [`prompt-change.patch`](./prompt-change.patch) is the §5.1.3 rule and
few-shot row as a diff against the master it was prepared from; it applies cleanly today. The
branch `evals/phase-4-prompt` carries the same change plus the examples-test entry, one commit
labelled `DO NOT MERGE — only after a corpus re-run`. Neither is on master. The patch is measured
before it is merged, not after.

## 8. Still unverified

- Chrome's supported `responseConstraint` keyword subset (the note's §1.3). The harness reports
  shape failures rather than assuming the subset.
- Whether `availability()` behaves identically in a manually launched Chrome and an automated one;
  the harness runs in a manually launched one, which is the point.
- The model variant. The run records what the human pastes from `chrome://on-device-internals`;
  nothing here can read it.
