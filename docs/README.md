# The docs

What each folder holds, when to read it, and how a document says whether it is still true. The
entry point is [`CONTEXT-MAP.md`](../CONTEXT-MAP.md) at the root — the contexts, their premises
and their glossaries — and this file is the map of the documents themselves.

| Folder | What it holds | Read it when |
|---|---|---|
| [`adr/`](./adr/) | Decisions the specs could not settle. An accepted ADR is superseded by a newer one, never rewritten; `## Supersedes` names what it retires | You are changing a decision, or asking why one was made |
| [`specs/`](./specs/) | What the system must do: the stream server, the app, the site, the look | You are changing behaviour — the spec is the contract |
| [`design/`](./design/) | The feature being worked now: one directory per feature, one ticket per file, state on the board. A shipped feature's directory moves to `archive/design/` | You are working that feature |
| [`research/`](./research/) | Explorations and comparisons of one moment: evidence and a conclusion, not current truth | You are deciding something that was already investigated |
| [`agents/`](./agents/) | Instructions for agents: the issue tracker, the board and build procedure, session usage | You are setting up a session |
| [`journey.md`](./journey.md) | The project's diary: a map of steps, each linking the document that owns it | You want the arc, not the rules |
| [`archive/`](./archive/) | Documents that are no longer true, kept for their reasoning — including every shipped feature's tickets | Almost never — only when tracing why something was once done |

Do not read a folder to decide something; read the one document that owns it.

## Status

A spec and a research document open with one line:

`**Status:** accepted · **Updated:** 2026-11-20`

- `draft` — being written; not a contract yet.
- `accepted` — current; for a spec, the code follows it.
- `superseded` — replaced by another document, which the header names.
- `obsolete` — no longer true, and nothing replaces it.

A research document opens with `**Status:** research`, the date it was taken, and
`**Conclusion:**` — one sentence, the document's own verdict, so the note is not mistaken for
current truth. A note that reaches no verdict carries no conclusion line.

ADRs carry no status. Being accepted is what makes one an ADR, and a newer ADR supersedes an
older one by naming it. Tickets under `design/` carry no status either: the board in
`kanvas.json` is where their state lives, and a ticket is the record of the work.

## Keeping it clean

Once a quarter, or when a folder stops being trustworthy, ask an agent for three lists:
documents with no status line, documents that restate a spec or another document, and documents
whose conclusion an ADR has since settled. Merge the second group into its owner, move the third
to `archive/` and fix what links to it. Show the lists, move nothing without a person's yes.
