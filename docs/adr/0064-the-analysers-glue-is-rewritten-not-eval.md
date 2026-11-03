# The analyser's glue is rewritten, not eval

The hosted App's policy is `script-src 'self' 'wasm-unsafe-eval' blob:`
([`vercel.json`](../../web/vercel.json)): WebAssembly may compile, JavaScript may not be built
out of strings. That is deliberate — the App runs a model in a worker and holds the user's
Provider key, and neither wants arbitrary code generation.

Essentia's pinned WebAssembly build breaks it. The module is fetched as a *weight*, checked
against its sha256 and imported from a blob URL, which the policy allows; but inside it, the
Emscripten embind glue builds invokers with `new Function(...)`. The first one runs during
`new Essentia(...)` — before any Track is measured — so the analyser never initialises and
every row fails with:

> Evaluating a string as JavaScript violates the following Content Security Policy directive:
> `script-src 'self' 'wasm-unsafe-eval' blob:`

`'unsafe-eval'` would fix the symptom and defeat the policy. The pinned build predates
`-sDYNAMIC_EXECUTION=0`, which is the Emscripten flag that writes these invokers out as
closures, so the same result is done to the text after verification.

## Decision

- **Three exact sites are rewritten** in `web/src/weights/essentia-glue.ts`, before the text
  becomes a blob: `createNamedFunction`, `embind__requireFunction`'s `makeDynCaller`, and
  `craftEmvalAllocator`. Each is replaced with the closure its string body produced.
- **A site that is not found is an error, not a shrug** (`WeightIncompatibleError`, so the UI
  says the analyser cannot start rather than failing every Track one by one). A new Essentia
  version must be re-audited; it is not silently loaded and left to fail at the first Track.
- **The rewrite is proved, not assumed.** `web/tests/essentia-glue.test.ts` reads the pinned
  bytes, asserts the token is gone, and runs `KeyExtractor`, `RhythmExtractor2013` and
  `BeatTrackerDegara` through both the patched and the unpatched module: the numbers must be
  equal. A no-op rewrite fails the token assertion.
- **The policy is untouched.** No `unsafe-eval` anywhere, and `worker-src`/`connect-src` keep
  their meaning.

## Consequences

- **A vendor file is edited at load.** The alternative — shipping a patched copy — would mean
  the App no longer runs the bytes its manifest pins, or a release of its own weight repo. The
  patch is deterministic, tested against the pinned bytes, and refuses unknown shapes.
- **`taglib-wasm` has the same problem, and is not fixed.** Its embind glue calls a dynamic
  `Function` when wasm invokes a JavaScript method — which is exactly what **Save tags** does
  (`taglib.edit(bytes, (audio) => …)`). That module is bundled, not fetched, so it cannot be
  rewritten at load, and it needs a build-time transform or a different tag writer. Recorded
  here so the next report of this error starts one step ahead.
- **The patch does not add a false positive.** Its search token is assembled
  (`['new', 'Function'].join(' ')`), so an audit of `dist/` that greps for `new Function`
  finds only code that would actually call it — which is `taglib`'s chunk, above, and nothing
  in this one.
