# Working in this repo

How to work here, for an agent or a person. The first half is about several sessions sharing one
tree at the same time, because that is the way this repo is worked and git will not warn anyone
when it goes wrong. The rest is the shape the repo already has.

## Start here

[`CONTEXT-MAP.md`](./CONTEXT-MAP.md) is the front door: the contexts, their global premises and
their glossaries. Read it, then the `CONTEXT.md` of the context you are touching — the
[stream server](./docs/specs/CONTEXT.md) or the [K7 app](./web/CONTEXT.md) — then the spec that
names the behaviour.

- **A global premise outranks the request.** Work that violates one is refused, not scheduled:
  name the premise and stop.
- **The spec is the contract; the code follows it.** Behaviour that is not in a spec starts with
  the spec.
- **A decision the specs cannot settle is an ADR** in [`docs/adr/`](./docs/adr/), landed in the
  same change. Its shape is a title that states the decision, the context that forced it,
  `## Decision`, `## Considered options`, `## Consequences`, and `## Supersedes` when it retires
  an earlier one. An accepted ADR is superseded, not rewritten.
- The [`README`](./README.md) is the manual: pins, flashing, the two-tier acceptance and the
  measured numbers. Do not restate it in a spec.

### The tree

| Path | What it is |
|---|---|
| `components/portable/` | The C that runs on the board **and** on the host. No target conditionals: the shim is what knows it is a Mac |
| `components/app/` | Board-only adapter, entry policy, panel. Not compiled on the host, and `make -C host layering` enforces that |
| `main/` | The firmware entry |
| `host/` | The host build, its shim, and the C and Python tests |
| `web/` | The K7 app: React, its tests, the artefact |
| `tools/` | `build.sh` (every firmware variant), screen capture, asset conversion |
| `docs/` | Specs, ADRs, research, and the tickets under `docs/design/` |

## Working alongside other agents

**One file has one writer at a time.** Git cannot enforce this and will not warn you: two agents
editing the same file do not conflict, the later write simply survives, silently. So:

- **Take a scope, not a task.** Every task names the paths it may write; everything else is
  read-only. If the work needs a file outside that scope, stop and report it — do not widen it
  yourself.
- **Read-only work is free.** Research, review, audits and greps write nothing, so any number can
  run in parallel in the same tree. Writing is the scarce resource; serialise it.
- **A subagent does not run git.** It returns its change; the session that owns the branch
  commits it with explicit paths (`git add <path>…`, never `git add -A`).
- **One committer per branch.** Two sessions committing to one branch interleave history that
  then has to be untangled by hand.
- **Never, in a shared tree:** `git stash` (it is global and will sweep a neighbour's work),
  `git clean -xfd`, `git checkout .`, `git reset --hard`. None of them asks first.
- **A worktree is a branch.** If you were given one, that branch is yours for the task: do not
  switch it, and do not create worktrees from inside it.

### Resources one session holds at a time

| Resource | Why |
|---|---|
| `build/<variant>/` | cmake caches absolute paths and the generated config; two `build.sh` runs on one variant corrupt it |
| `web/node_modules/` | concurrent `npm ci` or `npm install` corrupts it |
| `/dev/cu.usbserial-8344440` | one board. `build.sh` **kills** whatever holds the port before flashing, so a second `flash` silently ends the first session's `monitor` |
| the dev server on `8000` | one origin on one port; the dev certificates assume it |

Check before you take one (`lsof -t <port>`), and take a lock with `lockf -k .scratch/locks/<name>`
around anything the tool does not guard itself. (macOS has no `flock`; an atomic `mkdir` under the
same directory does for a shell that is not going through `lockf`.)

### The documents of record are the collision surface

Code merges; prose does not. `CONTEXT-MAP.md`, `README.md`, `AGENTS.md`, `docs/specs/` and
`docs/adr/` are single files every lane wants:

- Edit only the specs of the context you own. For another context's spec, propose a diff and let
  its lane apply it.
- **ADR numbers are sequential** — 0069 was the last when this was written. Take the next free
  number as the first thing you do, or leave the number to the integrating session: two agents
  that each invent the same one produce a rename and a set of broken links.
- A change that touches both contexts' specs usually wants one commit that lands in the
  integrating session, not two that meet in a conflict.

### When you raise subagents

- Give each writing child its scope in the prompt, by path. Without it, the scope is whatever it
  infers.
- Keep the readers parallel and the writers few; a fan-out of writers in one tree defeats the
  partitioning above.
- A child returns its diff and its evidence; the parent applies, commits and reports. That is
  also what makes a failed child cheap to discard.

## Commits

The subject is a sentence that names the change, often both halves of it — "Cache each cover
once, as a 422px AVIF, and carry it on the card". No prefix, no ticket id. The body says what was
wrong, why this shape fixes it, and what was tested, with the measured number when there is one.
Small corrective commits ("…10 px…", then "…16 px…") are kept rather than squashed: the history
is the record of how the thing got its shape. `master` stays linear — rebase before your branch
lands.

## Before you call it done

```bash
make -C host check                          # unit, layering and HTTP acceptance, no board
cd web && npm run typecheck && npm test && npm run build
./tools/build.sh sunton build               # firmware, when the change reaches the device
```

- CI (`.github/workflows/ci.yml`) runs the first two. The firmware is not in CI: it is proven by
  flashing.
- **Host evidence is not board evidence.** A phase is finished when it passes on the host *and*
  on the board. Never report a board claim from a host test. The four things the host cannot
  establish are in the README's "Two-tier acceptance".
- `<board>.local` and the serial port are shared with whoever else is flashing; do not leave a
  `monitor` attached.

## The app's UI

The ReUI skill is installed under `.agents/skills/reui` and is loaded before any UI work. Its
rules — semantic tokens, the spacing scale, the motion durations, the components as shipped —
are not optional, and components are installed rather than hand-rolled. The directory belongs to
the skill's own installer: do not edit it, and do not vendor it.

## Tracking

Work is tracked in `docs/agents/issue-tracker.md`: one directory per feature under `docs/design/`,
one ticket per file under its `issues/`, numbered from `01`. `.scratch/` is transient and is never
the record.

---

This file is shared too: change it at the integrating session, not from inside a lane.
