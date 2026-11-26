# Working in this repo

How to work here, for an agent or a person. The first half is about several sessions sharing one
tree at the same time, because that is the way this repo is worked and git will not warn anyone
when it goes wrong. The rest is the shape the repo already has.

## Start here

[`CONTEXT-MAP.md`](./CONTEXT-MAP.md) is the front door: the contexts, their global premises and
their glossaries. Read it, then the sections of the `CONTEXT.md` of the context you are touching —
the [stream server](./docs/specs/CONTEXT.md) or the [K7 app](./web/CONTEXT.md) — that name the
terms you are about to use, then the spec that names the behaviour. Both are glossaries, not
chapters: `grep -n '^#'` lists their sections, and the stream server's whole file is about 11k
tokens, re-read on every later turn it stays in the chat.

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

### Model, plan and proof

Work is planned before it is written and proved before it is called done.

- **Model.** The default model is enough for everyday features, fixes, tests and docs. A
  read-only subagent on the cheapest model does the searching and reading. The larger model is
  for a multi-file change, a long run, or a fix the default failed twice. The largest is for a
  migration, a refactor, or the bug nothing below it could fix — ask before paying for it, and try
  the tier below at low effort first.
- **Plan first.** On anything non-trivial, write the plan — what changes, in which paths, and how
  it will be checked — and get agreement before the first edit.
- **Finish line.** Turn the request into three to six checks that prove it: a test, a command, a
  number. Write them down, show that they fail, then loop until they pass.
- **Stop at three.** Three failed attempts on the same check means stop and say what is blocking,
  not a fourth.
- **Prove it with a run.** Name the verification before the edit and run it after. Host evidence
  is not board evidence (see *Before you call it done*).
- **Label every claim.** Close with what changed, how it was verified, and each claim marked
  measured, inferred or guessed. Anything guessed is not done.
- **One task per session.** A new task starts a new session, or a summary of what it needs; a
  long chat that drifts carries the last task's context into the next one.

Usage: [`docs/agents/agent-usage.md`](./docs/agents/agent-usage.md) has where the meter is and how
to audit a session.

### The tree

| Path | What it is |
|---|---|
| `components/portable/` | The C that runs on the board **and** on the host. No target conditionals: the shim is what knows it is a Mac |
| `components/app/` | Board-only adapter, entry policy, panel. Not compiled on the host, and `make -C host layering` enforces that |
| `main/` | The firmware entry |
| `player/` | The K7Player's firmware: its own ESP-IDF project on the classic ESP32, its own board adapter and host harness, sharing only `components/portable` ([ADR 0097](./docs/adr/0097-the-player-is-an-esp32-classic-in-its-own-project.md)) |
| `host/` | The host build, its shim, and the C and Python tests; `make -C host check` runs the player's host tier too |
| `web/` | The K7 app: React, its tests, the artefact |
| `assets/k7art/` | The factory K7Arts: each file's label section packs into the K7's label sheet, and its J-card is the App's cover for an album with no picture |
| `tools/` | `build.sh` (every firmware variant), screen capture, asset conversion |
| `tools/LabelEditor/` | The K7Art editor: one K7Art's label and J-card on two surfaces, served at `/k7art`; Export writes a K7Art, Publish a `.k7artpack` |
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
cd tools/LabelEditor && npm test && npm run build   # the editor, and its sheet writer's parity with label_pack.py
./tools/build.sh sunton build               # firmware, when the change reaches the device
./player/build.sh build                     # the K7Player's firmware, when it reaches that board
```

- CI (`.github/workflows/ci.yml`) runs the first three. The firmware is not in CI: it is proven by
  flashing.
- **Host evidence is not board evidence.** A phase is finished when it passes on the host *and*
  on the board. Never report a board claim from a host test. The four things the host cannot
  establish are in the README's "Two-tier acceptance".
- `<board>.local` and the serial port are shared with whoever else is flashing; do not leave a
  `monitor` attached.

## The app's UI

[`docs/specs/k7-look.md`](./docs/specs/k7-look.md) is the UI contract: a near-black page, an
orange primary and a neutral ink, outline for what is available and fill for what is current, and
the exceptions it names (the coverflow, content images). It governs colour, shape, type, state and
motion. [ADR 0080](./docs/adr/0080-the-k7-look-is-the-products-one-skin.md) records the skin, and
[ADR 0081](./docs/adr/0081-the-look-has-a-primary-and-a-secondary.md) the primary and the
secondary.

An album with no cover picture wears a K7Art's J-card — its cover part on the case front and
the album card, its spine on the shelf — not one of the five drawn CSS faces; a real cover
picture still wins ([`docs/specs/art-packs.md`](./docs/specs/art-packs.md) §6).

The ReUI skill is installed under `.agents/skills/reui` and is loaded before any UI work. ReUI is
the component source: components are installed rather than hand-rolled, and its rules — the
spacing scale, the text floor, accessibility — still hold where the K7 spec is silent. The spec
names every edit to an installed file; nothing in `components/ui` or `components/reui` is
restyled outside it. The skill directory belongs to its own installer: do not edit it, and do not
vendor it.

## Tracking

Work is tracked in `docs/agents/issue-tracker.md`: one directory per feature under `docs/design/`,
one ticket per file under its `issues/`, numbered from `01`. `.scratch/` is transient and is never
the record.

---

This file is shared too: change it at the integrating session, not from inside a lane.

 Plan and track work on the Kanvas board; follow .agents/skills/kanvas/SKILL.md.