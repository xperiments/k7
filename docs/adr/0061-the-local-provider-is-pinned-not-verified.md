# The local Provider is pinned, not verified

[ADR 0048](./0048-the-device-serves-the-analysis-app.md) settled that the weights are *verified,
not just pinned*: a pinned URL is not integrity, so every JavaScript module and every ONNX file
is checked against a manifest `sha256` before use, on every cold load. The rule was written for
24 MB of models and 17 MB of runtimes fetched from a CDN.

A WebLLM model is roughly 1 GB. WebCrypto cannot hash a 1 GB stream incrementally, so the same
rule means either a bespoke verified-proxy in the service worker (fetch in chunks, hash each
chunk against a lock file, assemble into Cache Storage, serve only verified bytes) or a minute
of first-run work that every user pays to defend against a threat with a small blast radius.

## Decision

- **The model bytes are pinned to an immutable Hugging Face commit**, recorded in a lock file
  with file names and sizes, and fetched over TLS. They are not hash-verified.
- **The JavaScript modules stay verified.** Essentia, ONNX Runtime and TagLib are executable —
  tampered bytes run as code — so they keep the `sha256` check they have today.
- **The asymmetry is deliberate and recorded here**, so that a future reader does not "fix" the
  large path into a slower first run for no gain.

## Considered options

- **A verified proxy in the service worker**, with per-chunk hashes. It works, and it makes a
  data file that cannot execute into the most expensive thing in the first-run path.
- **A mirror on storage we control** with a lock file. The same weaker guarantee as a pinned
  commit, plus a bucket, a bill and a copy to keep current.

## Consequences

- **A tampered model cannot run code.** The worst it does is produce a wrong filter in a box
  whose output is shown and editable before anything is searched.
- **A pinned commit still fails closed on the obvious attacks**: a mutable URL, a wrong model,
  a truncated file fails the runtime rather than the user.
- **The provider's key never travels.** With the local Provider selected, the Ask's sentence,
  the Intent and the library stay in the tab, which is the only configuration where the App's
  privacy claim is unconditional.
