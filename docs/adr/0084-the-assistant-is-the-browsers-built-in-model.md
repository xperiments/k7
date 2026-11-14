# The assistant is the browser's built-in model

> **Note:** the provider, setup and one-session decisions below stand. The page's UI — the
> Intent panel, the separate Run and the results card — is amended by
> [ADR 0086](./0086-the-assistant-is-a-conversation.md).

ADR 0053 made the **Provider** a setting: an OpenAI-compatible endpoint, a model name and a key,
all in this browser. That answered the constraints of its time — a plain-HTTP origin that could
not run a model, and an assistant that had to be remote to exist at all. It bought a key in
`localStorage`, a CORS sentence for an endpoint on the same machine, a Test whose "unreachable"
could be a lie, and a Settings card whose three fields were the feature's whole installation.

Two of those constraints are gone. The App is served over HTTPS and is Chrome-only
([ADR 0057](./0057-the-app-lives-at-a-public-origin.md),
[ADR 0067](./0067-chrome-only-direct-to-the-device.md)), and Chrome ships a model: Gemini Nano
behind the Prompt API, running in the browser, on desktop, with no key and no network. The App had
already specified a local Provider — WebLLM on WebGPU, roughly 1 GB of pinned Qwen weights
([ADR 0061](./0061-the-local-provider-is-pinned-not-verified.md)) — and had never built it.

The remote path was not deleted on an opinion. A throwaway page asked the built-in model, with the
exact system prompt Ask uses and the Intent's JSON Schema as a `responseConstraint`, twelve
sentences whose expected Intents were written down first. On Chrome 157 on macOS, ten of twelve
came back semantically right and all twelve schema-valid; the Spanish and French rows both passed,
and both are in Chrome's documented language set. The two misses were a duration
("about an hour" became a limit of 75, not 15) and an artist name ("songs by Daft Punk" searched
for "songs"). The model also over-fills mood asks with ranges the sentence did not ask for, and
uses `query` for adjectives as well as names.

## Decision

- **One Provider: the built-in model.** No endpoint, no base URL, no key, no presets, no CORS
  failure sentence, no Settings card. The word *Provider* retires; the glossary's term is
  **Built-in model**.
- **Ask owns the setup.** Five states: no Prompt API (not Chrome 148+ on desktop), `unavailable`
  (below the hardware floor, or built-in AI turned off in Chrome — the API cannot tell which),
  `downloadable`, `downloading`, `available`. Ask shows one sentence for the first two, a sentence
  and **Set up the assistant** for `downloadable`, progress for `downloading`, and the box for
  `available`. Nothing downloads on boot and nothing is prefetched.
- **One session per Ask.** A session is created with the system prompt, prompts once, and is
  destroyed; no context carries between asks and no session is held while the person browses.
- **The contract is unchanged.** The model produces an **Intent** and never results
  ([ADR 0050](./0050-the-assistant-waits-for-a-secure-origin.md),
  [ADR 0053](./0053-the-assistant-writes-filters.md)); `parseIntent` judges the answer, clamps
  every number and drops unknown keys; one repair attempt and then an error. A JSON Schema passed
  as `responseConstraint` shapes the answer — the spike shows Chrome accepts ranges, enums and
  `additionalProperties` — but the schema is a request and the validator is the judge.
- **Nothing leaves the machine.** With the built-in model the sentence, the Intent and the library
  stay where they are: the request is a local call, not a fetch.
- **The App holds no secret.** No key is stored, and no other secret replaces it.
- **WebGPU leaves the App's requirements.** The analyser is ONNX Runtime on wasm, and nothing else
  gated on `navigator.gpu`; it was in the App for WebLLM.

## Considered options

- **Keep the remote Provider as a fallback.** Rejected: a fallback that needs a base URL, a model
  and a key is not configured on the machine that lacks the built-in model. It keeps the setup and
  the secret this change removes, and doubles the failures Ask has to name.
- **Keep WebLLM beside Nano.** Rejected: two local engines, two weight paths, two quality profiles
  and two download stories, for a path that was never built. The built-in model is zero bytes the
  App ships.
- **A device proxy for the remote call.** Rejected already in
  [ADR 0056](./0056-a-providers-failure-is-named.md), and still wrong: the device is the half with
  the least room, and the sentence would leave the browser.
- **Keep Provider as the umbrella word.** Rejected: it named where to point a key, and there is
  nowhere to point one.

## Consequences

- **A machine below Chrome's floor loses Ask.** It keeps the filter form — ADR 0053's degrade to
  the form — and Ask says which floor it needs instead of offering a key. Mobile is not a
  regression: the App is already walled there on the `local-network` permission (A3).
- **The first ask is slow.** 12.6 s on the spike's Mac (the model's load), about 0.4 s per session
  after that, and 1–5 s per answer. The spinner and the thirty-second timeout already cover it.
- **Quality is 10/12, not 12/12.** The two misses are visible in the Intent and editable, which is
  the mitigation ADR 0053 chose. A tightened prompt was measured on the same rubric and came back
  8/12 — the duration and the artist fixed, the invented `query` and the over-fill worse — so it is
  not in the build. The bar was set before the run and was not moved.
- **The App cannot remove the model.** The download is Chrome's component in Chrome's profile; the
  App can start it and watch it, and removal is in Chrome's settings. The setup says so.
- **The languages are Chrome's.** English, Japanese, Spanish, German and French at the time of
  writing. `web/README.md` states the set, not the App.
- **No key, no secret, no rate limit, no bill.** The cost is availability, not money.
- **`Weights` narrows to the analysis.** O7 leaves only the Invidious Instance hostile to offline;
  Ask works offline once the model is installed, and the model's own download is its one network
  moment.
- **Nothing is prefetched for the assistant.** The 41 MB the worker keeps is the analyser's; the
  model is Chrome's and is not in it.

## Supersedes

- **ADR 0053** — "the provider is a setting, not a build" is reversed. Its shape rule, the model
  writes filters and never results, is adopted unchanged.
- **ADR 0056** — there is no CORS failure to name. Its principle, name the failure rather than
  guess, is adopted by the new sentences: not installed, the download failed, the model answered
  nothing.
- **ADR 0061** — there are no weights for the App to pin; the model is Chrome's and the page never
  fetches it.

## Note (2026-11-13)

The prompt now carries a word-to-filter table and few-shot turns passed as `initialPrompts`, and
the schema declares no required keys and lists free text last, so the constrained decoder fills
the numbers before the strings. The contract is unchanged: the model writes filters and
`parseIntent` judges them; an inverted bound pair is swapped, and Ask may relax one numeric range
once when nothing matches and a mood can stand in.
The Ask prompt now also carries the library's aggregates when this browser built the image the
player is running: its genres, track count, and each measurement's range and quartiles. That
amends ADR 0053's "no measurement or library size": aggregates only, never a track, artist, path
or per-track measurement, and only into the model that runs on this machine. Without a profile of
the running image, the prompt is the static one.
