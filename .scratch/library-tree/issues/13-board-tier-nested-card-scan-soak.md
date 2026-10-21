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

**Status:** done (2026-10-21)

- [x] The boot log reports scan time, arena/JSON sizes, PSRAM before and after, and `truncated`
      at the cap.
- [x] A three-level Track plays, and the index, JSON and binary views match the card.
- [x] MCP Inspector or an equivalent client completes the read-only flow through
      `xtape.local`; control tools work only when the flag is enabled.
- [x] A ten-minute soak with a rescan and a download shows no faults and no starvation rebases
      attributable to the scan, and records whether the synchronous scan stays or switches to
      the asynchronous `indexing` state.

## Comments

- Board: ESP32-S3 v0.2, 16 MB flash / 8 MB PSRAM, 30 420 MB SDHC over SPI, ESP-IDF 6.1.
  Flashed `271ba61` plus this ticket's build; the attached card holds 64 FLAC tracks.
- **Stack overflow found and fixed.** The first full-card boot panicked with
  "A stack overflow in task main": the boot scan's 1 KB path buffers, `tmp_abs_path` and the
  scanner frame do not fit the 6 144 B main stack. `CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384`
  in `sdkconfig.defaults` fixed it; the same card then booted with no fault. Rescans use the
  8 KB task and were stable in the soak.
- Boot log (full card):
  `library: state=ok tracks=64 dirs=0 bytes=2277100658 scan=96 ms arena=6285 B json=12343 B psram=5295632 -> 5275936 B truncated=0`.
  A temporary `max_tracks=1` build logged `tracks=1 ... scan=7 ms arena=228 B json=424 B ...
  truncated=1`, proving the cap path.
- **Nested fixture.** A temporary board fixture created
  `/sdcard/music/zz-xtape-nested/Deep/one-second.wav`; the library reported
  `tracks=65 dirs=2 scan=137 ms arena=6430 B json=12631 B`. JSON found the path; the binary
  view verified `68 nodes, 67 edges, 65 tracks, CRC and parentage`; the download was 88 244 B
  with `audio/wav`; `POST /api/play` returned 200 and `player_status` showed
  `zz-xtape-nested/Deep/one-second.wav`. The fixture was then removed and the card verified
  back at 64 tracks / 0 dirs.
- **MCP/mDNS.** `initialize` and `tools/list` over `http://xtape.local:8000/mcp` returned
  200, `MCP-Protocol-Version: 2025-11-25` and the five read-only tools; `dns-sd` listed
  `_mcp._tcp`; no control tools in the default build. Control tools were verified on the host
  control build (ticket 12); no control firmware was left on the board.
- **10-minute soak** (`RESCAN=1 MAX_COST=70 sh host/tests/board_soak.sh <base> <serial> 10
  downloads`): one stream client, repeated 46 MB downloads and four published rescans.
  `starvation rebases: 0`, `asserts/overflows/reboots: 0`, encoder 52.9 %, internal minimum
  66 043 -> 57 855 B, external 2 039 048 -> 2 079 160 B. Synchronous boot scan stays: 64
  tracks scan in about 100 ms, far below any async benefit.
- Reproducible scripts: `host/tests/board_library.sh` (boot log, JSON, binary header/CRC,
  nested track if present, MCP through xtape.local, dns-sd) and `board_soak.sh` with
  `RESCAN=1`. The nested fixture itself is created by a temporary board build; the card is
  left clean.
- Not here: control tools on the board itself; the host control build covers the flag.
