# 12 — MCP control tools behind a flag

**What to build:** Add the control tools to the MCP endpoint without making them the default.
With `XTAPE_MCP_CONTROL=1`, a client can play a Track, skip, go back and request a rescan. In
the default build the endpoint stays read-only: those tools are absent from `tools/list` and a
call naming one is an unknown tool. Control failures are tool errors and never interrupt the
live stream.

**Blocked by:** 08 — HTTP POST and play any Track; 09 — Read-only MCP endpoint; 11 — Card
insertion and rescan generations.

**Status:** done (2026-10-20)

- [x] The default build lists no control tools and answers a control-tool call with `-32602`.
- [x] With the flag set, `player_play`, `player_skip` and `player_prev` change the broadcast;
      an invalid path is `isError: true` and the stream continues.
- [x] `library_rescan` starts a background scan and returns immediately; a second call reports
      that a scan is already running.
- [x] Enabling the flag adds no new always-on surface to the default build.

## Comments

- `mcp.c` reads `XTAPE_MCP_CONTROL` at compile time. The default `tools/list` is the five
  read-only tools; a control name falls through to the existing `-32602` unknown-tool path.
  The control build selects a second static tools constant with `player_play`, `player_skip`,
  `player_prev` and `library_rescan`, all `readOnlyHint:false`, `destructiveHint:false`.
- `player_play` accepts `{path}` or `{id, generation}` (path wins if both). It resolves through
  `library_find` / the arena id and uses `mini_icecast_play`, so the change happens at the next
  chunk. A bad path/id/generation is a tool error and leaves the broadcast alone. `player_skip`
  and `player_prev` set the existing transport flags.
- `library_rescan` calls an `mcp_rescan_fn` supplied by `app_services` (the MCP component does
  not depend on the composition layer). It returns immediately; a second call is an `isError`
  "a scan is already running", and the scan never runs in the HTTP worker.
- The flag is wired in the managed host and IDF builds:
  `make -C host control` builds a separate `build/control` tree with `-DXTAPE_MCP_CONTROL=1`;
  `XTAPE_MCP_CONTROL=1 idf.py build` adds the compile definition in the MCP component.
  The default build has no control tools and no new route/surface.
- Verified: default `make -C host check` (156 HTTP checks, t14 164, T16 confirms five tools and
  `-32602` for `player_skip`); `make -C host control-check` (nine tools, annotations, valid
  path and id/generation, skip/prev, invalid path isError with the stream still alive, two
  simultaneous rescans one started and one "already running", generation advances); default
  firmware builds `build_sunton` (0x2fa490, 26% free) and `build61` (0x21c050, 47%);
  control firmware configures with "MCP control tools enabled".
- Not here: a board control run; the control binary is host-verified. The default read-only
  MCP surface and the `/api/play` route remain as tickets 08/09 left them.
