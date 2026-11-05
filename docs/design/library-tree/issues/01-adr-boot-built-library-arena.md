# 01 — ADR: the library is a boot-built PSRAM arena

**What to build:** Record the architecture decision the plan depends on: the device builds one
immutable Library tree at boot from the Music directory, keeps it in PSRAM, resolves HTTP and
MCP requests by tree lookup rather than by joining filesystem paths, and publishes generations
on rescan. The ADR should state the alternatives that were rejected, the consequences for boot
time and memory, and the path-length ceiling. No source code changes.

**Blocked by:** None — can start immediately.

**Status:** done (2026-10-20)

- [x] An ADR exists and states the decision, the alternatives (per-request walk, flat path list,
      an on-card index cache), and the consequences (boot scan, PSRAM budget, generation
      lifecycle, `FILENAME_MAX` path ceiling).
- [x] The ADR links the research document and the decisions recorded there.
- [x] The ADR is accepted and linked from the documentation; no source has changed.
