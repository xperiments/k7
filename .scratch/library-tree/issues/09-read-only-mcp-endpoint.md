# 09 — Read-only MCP endpoint

**What to build:** A standard MCP endpoint at `POST /mcp` on the existing port, speaking the
legacy 2025-11-25 lifecycle, stateless and JSON-only. A client can initialize, list the
read-only tools, query the Library tree and read the player status. The endpoint enforces the
agreed error contract and returns `MCP-Protocol-Version` on every JSON-RPC response. No control
tools are exposed by this ticket.

**Blocked by:** 04 — Library at boot and the JSON endpoint; 07 — The broadcast plays the whole
Library tree; 08 — HTTP POST and play any Track.

**Status:** ready-for-agent

- [ ] MCP Inspector or an equivalent client completes `initialize` → `notifications/initialized`
      → `tools/list` → `tools/call`, with version echo for 2025-11-25, 2025-06-18 and
      2025-03-26, and a fallback to 2025-11-25.
- [ ] A missing required `initialize` field is `-32602`; an unknown method is `-32601`, an
      unknown tool `-32602`, and a notification is 202 with no body.
- [ ] `notifications/initialized` carrying an `id` is `-32600`; malformed JSON, invalid
      envelope, missing length, wrong content type and an oversized body follow the agreed
      status table.
- [ ] `tools/list` contains only `library_*` and `player_status`, results are text-only, and no
      `Mcp-Session-Id` is emitted.
- [ ] `MCP-Protocol-Version` is present on every JSON-RPC response, and `Accept` is ignored.
