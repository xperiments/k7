# 09 — Read-only MCP endpoint

**What to build:** A standard MCP endpoint at `POST /mcp` on the existing port, speaking the
legacy 2025-11-25 lifecycle, stateless and JSON-only. A client can initialize, list the
read-only tools, query the Library tree and read the player status. The endpoint enforces the
agreed error contract and returns `MCP-Protocol-Version` on every JSON-RPC response. No control
tools are exposed by this ticket.

**Blocked by:** 04 — Library at boot and the JSON endpoint; 07 — The broadcast plays the whole
Library tree; 08 — HTTP POST and play any Track.

**Status:** done (2026-10-20)

- [x] MCP Inspector or an equivalent client completes `initialize` → `notifications/initialized`
      → `tools/list` → `tools/call`, with version echo for 2025-11-25, 2025-06-18 and
      2025-03-26, and a fallback to 2025-11-25.
- [x] A missing required `initialize` field is `-32602`; an unknown method is `-32601`, an
      unknown tool `-32602`, and a notification is 202 with no body.
- [x] `notifications/initialized` carrying an `id` is `-32600`; malformed JSON, invalid
      envelope, missing length, wrong content type and an oversized body follow the agreed
      status table.
- [x] `tools/list` contains only `library_*` and `player_status`, results are text-only, and no
      `Mcp-Session-Id` is emitted.
- [x] `MCP-Protocol-Version` is present on every JSON-RPC response, and `Accept` is ignored.

## Comments

- `mcp` is a new portable component. It registers `POST /mcp`, and `GET`/`DELETE /mcp` (405 with
  `Allow: POST`). The lifecycle is legacy 2025-11-25: stateless, JSON-only, no SSE, no session
  header. `initialize` echoes 2025-11-25/2025-06-18/2025-03-26 and falls back to 2025-11-25;
  missing fields are -32602.
- The read-only tools are `library_stats`, `library_list`, `library_search`, `library_track` and
  `player_status`. Results are `content:[{type:"text",text:"..."}]` with no structuredContent.
  `library_list` caps at 200 entries and `library_search` at 50 tracks, both reporting
  `truncated`; control tools are omitted.
- Transport errors for `/mcp` are rendered by a route error hook, so 411/413/415/400/500 are
  JSON-RPC (`-32600`/`-32603`) with `MCP-Protocol-Version`. `/api/play` keeps its own error
  shape. Every JSON-RPC response carries the header; no `Mcp-Session-Id`; `Accept` ignored.
- The JSON tokenizer is in `mcp.c` (strict numbers, escapes, surrogate pairs, bounded tokens)
  and the response writer is bounded per response. The request body is the 8192-byte one from
  ticket 08.
- Tests: `mcp_tests.py` drives the wire protocol: three initialization versions and fallback,
  initialized-as-notification and as-request, ping, tools/list, all five tools, unknown
  method/tool, malformed JSON, bad envelope, boolean id, batch, unsupported version header,
  Accept, 411/413/415, GET/DELETE 405 with Allow, path under `/mcp`, long method notification
  and long id, and no session header. T16 runs it against the host server; a nested
  `Album/Deep/02 - Song.wav` is resolved through `library_track`.
- Verified: `make -C host check` (152 HTTP checks, t14 152, layering, awkward names),
  `make -C host tools`, and both `build_sunton`/`build61` (0x2ef200, 27% free; 0x210f40, 48%).
- Not here: mDNS and the Origin guard (ticket 10), control tools (ticket 12), card-arrival
  generation publishing (ticket 11), and board MCP coverage (ticket 13). The JSON tokenizer
  overlaps `mini_icecast.c`'s small string parser; extracting a shared JSON module is a future
  cleanup.
