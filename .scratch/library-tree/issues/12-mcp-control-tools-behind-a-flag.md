# 12 — MCP control tools behind a flag

**What to build:** Add the control tools to the MCP endpoint without making them the default.
With `XTAPE_MCP_CONTROL=1`, a client can play a Track, skip, go back and request a rescan. In
the default build the endpoint stays read-only: those tools are absent from `tools/list` and a
call naming one is an unknown tool. Control failures are tool errors and never interrupt the
live stream.

**Blocked by:** 08 — HTTP POST and play any Track; 09 — Read-only MCP endpoint; 11 — Card
insertion and rescan generations.

**Status:** ready-for-agent

- [ ] The default build lists no control tools and answers a control-tool call with `-32602`.
- [ ] With the flag set, `player_play`, `player_skip` and `player_prev` change the broadcast;
      an invalid path is `isError: true` and the stream continues.
- [ ] `library_rescan` starts a background scan and returns immediately; a second call reports
      that a scan is already running.
- [ ] Enabling the flag adds no new always-on surface to the default build.
