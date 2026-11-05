# 14 — Final documentation

**What to build:** Bring the README and the stream-server specification in line with the
implemented behaviour: recursion into subdirectories, the path ceiling, the JSON and binary
endpoints, the MCP endpoint and its read-only default, the memory and scan costs, and the
one-device-per-LAN mDNS limitation. Link the ADR. `CONTEXT.md` is already updated, so this
ticket does not re-open the vocabulary.

**Blocked by:** 13 — Board tier: nested card, scan cost, soak.

**Status:** done (2026-10-21)

- [x] The README no longer says the Music directory is a single flat directory; it documents
      recursion and the path ceiling.
- [x] The specification's URL table covers `/api/library`, `/api/library.bin`, `/api/play`,
      `/api/rescan` and `/mcp`, including the read-only default.
- [x] Memory, scan, truncation, mDNS and the one-device-per-LAN limitation are documented with
      the measured numbers.
- [x] The ADR is linked from the documentation, and no stale "no recursion" claim remains.

## Comments

- README now describes the recursive Music directory, the per-name 128-character limit, the
  `FILENAME_MAX` (1024-byte, 1025-byte buffer) path ceiling, the 4 MiB/20 000-track budget and
  the `truncated` behaviour. It has measured board numbers (64 tracks / 2.28 GB: 96 ms scan,
  6 285 B arena, 12 343 B JSON) and the 16 KiB main-stack requirement.
- The specification has a consolidated URL table for `/stream`, `/`, `/music/<path>`,
  `/api/library`, `/api/library.bin`, `/api/play`, `/api/rescan` and `/mcp`, including the
  read-only default and the control flag. It states `LIB_PATH_MAX = FILENAME_MAX + 1` (not a
  128-byte path buffer) and notes the 512-byte raw URL ceiling versus the MCP body path.
- Memory/scan/truncation are documented with the measured numbers and the cap diagnostic.
  mDNS `xtape.local`/`_mcp._tcp`, the fixed hostname and the one-device-per-LAN limitation are
  in the README and the MCP requirements; the stale "mDNS is out of scope" line is removed.
- The README architecture section links
  [`docs/adr/0012-boot-built-library-arena.md`](docs/adr/0012-boot-built-library-arena.md), and
  the architecture diagram/tree includes `library`, `library_http`, `mcp`, `mini_files` and
  the app-only pieces. No "no recursion"/flat-directory claim remains in README, spec or
  `CONTEXT.md`.
- The stale 1.04 MB/1.5 MB build row was replaced with the current `0x2fa490` (3.12 MB) in the
  4 MB factory partition; the board table carries the ticket-13 library/MCP/rescan soak rows.
- Verified: `make -C host check` (156 HTTP checks, t14 164, layering, MCP/T17), control-check,
  and both firmware builds.
