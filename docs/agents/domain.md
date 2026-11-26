# Domain Docs

How the engineering skills consume this repo's domain documentation.

## Before exploring, read these

- **`CONTEXT-MAP.md`** at the root: the contexts, their global premises and their glossaries.
  This repo is multi-context — the stream server (`docs/specs/CONTEXT.md`) and the K7 app
  (`web/CONTEXT.md`). Read the one you are touching.
- **`docs/README.md`**: which folder owns what, and how a document says it is still current.
- The spec that names the behaviour (`docs/specs/`), then **the ADRs in `docs/adr/`** that
  touch the area. There are no per-context ADR directories: every accepted decision is at the
  root, numbered sequentially.

If one of these is missing, proceed silently; `/domain-modeling` creates them when a term or a
decision actually gets resolved, rather than upfront.

## Use the glossary's vocabulary

Name concepts as `CONTEXT.md` defines them — in issue titles, test names, hypotheses, refactor
proposals. Don't drift to synonyms the glossary avoids.

If the concept you need isn't in a glossary, that is a signal: either you are inventing
language the project does not use, or there is a real gap worth noting for `/domain-modeling`.

## Flag ADR conflicts

If your output contradicts an accepted ADR, say so instead of overriding it silently:

> _Contradicts ADR 0012 (a boot-built library arena) — but worth reopening because…_

An accepted ADR is superseded by a new one, never rewritten.
