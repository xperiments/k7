# 14 — Final documentation

**What to build:** Bring the README and the stream-server specification in line with the
implemented behaviour: recursion into subdirectories, the path ceiling, the JSON and binary
endpoints, the MCP endpoint and its read-only default, the memory and scan costs, and the
one-device-per-LAN mDNS limitation. Link the ADR. `CONTEXT.md` is already updated, so this
ticket does not re-open the vocabulary.

**Blocked by:** 13 — Board tier: nested card, scan cost, soak.

**Status:** ready-for-agent

- [ ] The README no longer says the Music directory is a single flat directory; it documents
      recursion and the path ceiling.
- [ ] The specification's URL table covers `/api/library`, `/api/library.bin`, `/api/play`,
      `/api/rescan` and `/mcp`, including the read-only default.
- [ ] Memory, scan, truncation, mDNS and the one-device-per-LAN limitation are documented with
      the measured numbers.
- [ ] The ADR is linked from the documentation, and no stale "no recursion" claim remains.
