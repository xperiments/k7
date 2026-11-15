# The assistant keeps one base session and clones it for each ask

The APP-42 session screen measured four ways to ask the same 66-sentence corpus three times each on
Chrome 157 (a 32 GB M2 Max; the session export left the model variant blank, the APP-40 baseline
on the same machine recorded `nano_v3_gpu_high_tier_model`). Creating a session per ask passed 19
rows at 1850 ms mean (std 761); create + omit passed 37 at 1130 ms (std 105); clone + omit passed
41 at 316 ms (std 100), with 2 unstable rows against 8 for create + omit and 15 for create. Cloning
without the omit matched create (17 vs 19) at 1028 ms: the clone is the speed, the omit is the
quality.

The Prompt API documents that `session.clone()` preserves the initial prompt and the conversation
context; the clone's run fields match or beat create's. The clone probe was not an equality — the
base answered `{"mood":["relaxed"],"genre":"","query":""}`, the clone
`{"mood":["relaxed"],"sort":"bpm","limit":20}` — which reads as sampling: the clone's answer
followed the example's `limit:20` and its run fields were no worse.

ADR 0084's **One session per Ask** was written for an API that only created. The clone keeps the
same isolation at the request boundary: the base is never prompted, so no ask's context reaches
another; only the base's creation is amortized.

## Decision

- **One base session per prompt.** `askModel` creates it on the first ask for a system prompt and
  example set, holds it, and never prompts it. Every ask gets a clone; the clone is prompted once
  and destroyed.
- **The schema is not echoed into the input.** The prompt's rules and turns carry the shape; the
  output is still constrained (`omitResponseConstraintInput`, k7-app §7 P2). The omit is the
  bigger of the two effects and stands on its own.
- **The base dies with the prompt.** The App closes the bases when the source profile changes and
  when the Ask area closes; the evaluation harness closes them after a run. A page unload is
  Chrome's.

## Considered options

- **Create per ask** (the old shape). 19 pass, 1850 ms. Create + omit is 37 at 1130 ms; rejected
  because the clone is faster still and the omit already wins without it.
- **Clone + omit** (chosen). 41 pass, 2 unstable, 316 ms.
- **A conversation session** (hold one session and keep prompting it). Rejected: each ask would
  see the previous asks' context, the Intent would drift, and a bad answer would poison the next.
  ADR 0086's conversation is the App's messages, not the model's context.
- **Lower `temperature`.** Rejected: a web page ignores `temperature`/`topK`; the Sampling
  Parameters origin trial was not available in the measured Chrome.

## Consequences

- The first ask pays the base's 867 ms; create paid 1850 ms for the same ask, so even the first is
  faster. The later asks are 316 ms instead of 1850.
- One base session stays in Chrome while the Ask area is open. It holds no answer, only the
  initial prompt, and it is destroyed on the profile change, the area close, or the page unload.
- The clone probe is one sample, not a guarantee of identical answers; the run's per-field profile
  and the Prompt API's documented clone semantics are the evidence.
- The one-session isolation survives: the base is never prompted, so no ask's context carries to
  another. The "no session held" half of ADR 0084's bullet is superseded; the provider, setup,
  contract, privacy and no-secret decisions stand.

## Supersedes

**ADR 0084**'s *One session per Ask* bullet — a session created, prompted once, and destroyed — is
superseded by the base session and the per-ask clone. The rest of ADR 0084 stands.
