# Issue tracker: Local Markdown

Issues and specs for this repo live as tracked markdown files under `docs/design/`. There is
no hosted tracker, so local files are the tracker. `.scratch/` is gitignored transient work
and is never the record.

## Conventions

- One feature per directory: `docs/design/<feature-slug>/`
- The spec is `docs/design/<feature-slug>/spec.md`; the spec for a feature may also live in
  `docs/specs/` when it is a durable document rather than a working artifact. See
  `docs/design/sd-stream-server/` for an example of the latter: the spec lives at
  `docs/specs/sd-stream-server.md` and the directory holds only tickets.
- Implementation tickets are one file per ticket at `docs/design/<feature-slug>/issues/<NN>-<slug>.md`,
  numbered from `01` — never a single combined tickets file.
- Triage state is recorded as a `Status:` line near the top of each ticket file.
- `Blocked by:` lists the ticket numbers and titles that gate the ticket. A ticket is ready
  when every file it lists is done.
- Comments and conversation history append to the bottom of the file under a `## Comments`
  heading.

## When a skill says "publish to the issue tracker"

Create a new file under `docs/design/<feature-slug>/issues/`, creating directories if needed.

## When a skill says "fetch the relevant ticket"

Read the file at the referenced path. The user will normally pass the path or the number.

## Working the frontier

`ls docs/design/<feature-slug>/issues/` and take the lowest-numbered file whose blockers are all
complete. One ticket per fresh context.
