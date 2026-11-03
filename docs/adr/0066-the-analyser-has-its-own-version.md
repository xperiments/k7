# The analyser has its own version

`k7-analysis` is keyed by `sha256(bytes) : ANALYZER_VERSION : modelSet`. That key is the whole
reason a card measured once is not measured again — and `ANALYZER_VERSION` was `__APP_VERSION__`.

So every release invalidated every stored Analysis. A deploy that changed a button, a sentence
or the service worker's cache name made a five-thousand-track card re-measure itself from
scratch, and the "already measured" skip added at 0.6.0 silently did nothing after each deploy:
the media row matched, the cache key did not. The field's own documentation already said what it
was — *"the version of the analysis pipeline"* — and the code disagreed with it.

## Decision

- **`ANALYZER_VERSION` is a constant of its own** ([`version.ts`](../../web/src/domain/version.ts)),
  seeded at `1.0.0`, and it is the only thing that names the pipeline. It travels in three
  places that must agree: the cache key, an Analysis's `provenance.analyzer`, and the image's
  `analyser` header field.
- **It is bumped on purpose**, when the same bytes and the same models would now produce a
  different result: the mel front-end, a detector, a threshold, a field that must be filled.
  The model set is already part of the key, so new weights do not need it.
- **The app version keeps its own jobs** — the bundle, the shell cache name, the update banner
  — and has no business in a measurement's identity.
- **A test holds the line**: `tests/cache-key.test.ts` asserts the key never contains the app's
  version. A release that starts invalidating cards again fails there, not in someone's run.

## Consequences

- **One last invalidation.** Entries written before this change are keyed by an app version
  (`…:0.6.1:…`), so they miss once and are re-measured. A shim that read the old keys would
  have to hardcode a release number and would be dead the release after; the cost of one
  re-measure was judged lower than a permanent legacy path.
- **From here, a release costs nothing.** The cache survives it, and the skip keeps working
  because the key it probes is the key it wrote.
- **A pipeline change is now a decision someone makes**, with a visible consequence — rather
  than a side effect of shipping anything at all.
