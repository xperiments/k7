# Working in this repo

Several sessions share one tree, and git will not warn when two edit the same file.

## Ground rules

- **The premise outranks the spec; the spec outranks the code.** Work that violates a
  global premise is refused, not negotiated. A decision the specs cannot settle is an ADR
  in `docs/adr/`, landed in the same change — copy an existing one's shape. The README is
  the manual; a spec never restates it.
- **Read the map, then the sections.** `CONTEXT-MAP.md`, then `docs/README.md` for which
  folder owns what, then the `CONTEXT.md` sections that name the terms you are using, then the
  spec. They are glossaries, not chapters.
- **One file, one writer.** Take a write scope and ask before widening it. Readers run in
  parallel; writers serialise.
- **A subagent never runs git.** It returns its diff; the branch's owner commits explicit
  paths (`git add <path>`, never `-A`) and is its only committer. A worktree is that
  branch: do not switch it.
- **Never, in a shared tree:** `git stash`, `git clean -xfd`, `git checkout .`,
  `git reset --hard` — each silently takes a neighbour's work.
- **Own your context's specs.** For the other context's, propose a diff; a change spanning
  both is one commit in the integrating session. ADR numbers are sequential — take the
  next free one, or leave it to the importer.
- **Host evidence is not board evidence.** A phase is done on the host and on the board;
  firmware is proven by flashing, not by CI. Done means `make -C host check`, the web
  suite and the editor suite are green (see `.github/workflows/ci.yml`).

## How to work

- Plan first: what changes, in which paths, how it is checked — agreed before the first
  edit.
- Turn the request into 3–6 checks that prove it; watch them fail, loop until they pass;
  stop after three failures on one check and say what blocks it.
- The default model handles routine work; the cheapest read-only subagent does the
  searching; ask before the top tier.
- One task per session. Close with what changed, how it was verified, and every claim
  labelled measured, inferred or guessed.
