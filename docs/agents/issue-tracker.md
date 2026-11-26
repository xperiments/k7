# Issue tracker: Local Markdown

Issues and specs for this repo live as tracked markdown files under `docs/design/`. There is no
hosted tracker. **The board is the tracker**: `kanvas.json`, driven by
`python3 .agents/skills/kanvas/kanvas.py`, holds each task's state, its dependencies and its
notes. A ticket file is the work's own record — what to build, what to check, what was found.
`.scratch/` is gitignored transient work and is never the record.

## Conventions

- One feature per directory: `docs/design/<feature-slug>/`
- The spec is `docs/design/<feature-slug>/spec.md`; the spec for a feature may also live in
  `docs/specs/` when it is a durable document rather than a working artifact. See
  `docs/design/sd-stream-server/` for an example of the latter: the spec lives at
  `docs/specs/sd-stream-server.md` and the directory holds only tickets.
- Implementation tickets are one file per ticket at
  `docs/design/<feature-slug>/issues/<NN>-<slug>.md`, numbered from `01` — never a single
  combined tickets file.
- **A shipped feature moves out.** When the board accepts a feature, its directory moves to
  `docs/archive/design/<feature-slug>/`, so `docs/design/` holds live work only and the
  reasoning of what shipped survives one level away.
- **State and dependencies are not in the file.** The board's task carries them — `start`,
  `finish`, `pause`, `depend` — and the ticket carries the work. The `Status:` and `Blocked by:`
  lines this document used to prescribe are gone: none of the 97 tickets ever had one, and a
  second place for state is a second truth.
- Comments and conversation history append to the bottom of the file under a `## Comments`
  heading.

## When a skill says "publish to the issue tracker"

Propose it on the board (`kanvas.py propose`, or `batch` for a plan), and put the working text in
a ticket file under `docs/design/<feature-slug>/issues/`, creating directories if needed.

## When a skill says "fetch the relevant ticket"

Read the board's task (`kanvas.py show <ID>`) for its state and notes, and the ticket file for
the work itself. The user will normally pass the path or the number.

## Working the frontier

`python3 .agents/skills/kanvas/kanvas.py ready` is the frontier: the tasks approved and
unblocked. The ticket numbers still order the reading, but a ticket is taken when the board says
it is ready, one ticket per fresh context.
