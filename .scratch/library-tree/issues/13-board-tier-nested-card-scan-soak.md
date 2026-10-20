# 13 — Board tier: nested card, scan cost, soak

**What to build:** Prove the whole thing on the device with a real nested card. Boot must log
the scan time, arena and JSON sizes, PSRAM before and after, and the truncated state at the cap.
A Track several levels deep must play, the index, JSON and binary must match the card, an MCP
client must reach the device through `xtape.local`, and a ten-minute soak with a rescan and a
download must stay healthy. This tier decides whether the synchronous boot scan stays.

**Blocked by:** 02 — Extract the source-format authority; 03 — Library tree module, host tier;
04 — Library at boot and the JSON endpoint; 05 — Binary view; 06 — Index and downloads from the
tree; 07 — The broadcast plays the whole Library tree; 08 — HTTP POST and play any Track; 09 —
Read-only MCP endpoint; 10 — mDNS discovery and the Origin guard; 11 — Card insertion and rescan
generations; 12 — MCP control tools behind a flag.

**Status:** ready-for-agent

- [ ] The boot log reports scan time, arena/JSON sizes, PSRAM before and after, and `truncated`
      at the cap.
- [ ] A three-level Track plays, and the index, JSON and binary views match the card.
- [ ] MCP Inspector or an equivalent client completes the read-only flow through
      `xtape.local`; control tools work only when the flag is enabled.
- [ ] A ten-minute soak with a rescan and a download shows no faults and no starvation rebases
      attributable to the scan, and records whether the synchronous scan stays or switches to
      the asynchronous `indexing` state.
