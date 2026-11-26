# 03 — Library tree module, host tier

**What to build:** The deep module that owns the Library tree. It scans the Music directory
recursively with one directory handle at a time, sorts each directory by name, accepts only
Tracks whose name passes the source-format rule, validates UTF-8, enforces the path ceiling,
and stops at the memory cap with `truncated` set. It builds the immutable arena and the prebuilt
JSON, and it resolves a relative path by walking the tree. This ticket is host-testable and
does not wire the module into the boot path or HTTP.

**Blocked by:** 02 — Extract the source-format authority.

**Status:** done (2026-10-20)

- [x] A temporary tree containing nested directories, UTF-8 names, dot-files, a symlink, a
      128-byte name, a path just under and just over `FILENAME_MAX`, an unreadable directory
      and an empty root scans with the expected counts and DFS order.
- [x] Lookup resolves nested paths, matches case-insensitively as a fallback, refuses an
      ambiguous case fallback, and makes no filesystem call.
- [x] The JSON is valid and matches the tree; the arena header, nodes, edges, track index,
      strings and CRC parse with an independent test.
- [x] The budget cap produces `truncated`, keeps the indexed portion usable, and the arena is
      allocated from PSRAM, not internal RAM.
- [x] A name that is not valid UTF-8 is skipped and logged; it never reaches the JSON.

## Comments

- The module is host-tier only: `components/portable/library/` compiles on the host and is
  not required by the firmware yet. Ticket 04 wires it into boot and HTTP.
- The binary view is parsed by an independent reader in `t14_library`, including the header,
  nodes, edges, track index, strings and CRC.
- The JSON view is checked structurally in C and was also validated with `python3 -m json.tool`
  during development; a real parser remains the integration-tier check in ticket 04.
- Two host limits are acknowledged rather than hidden: APFS is case-insensitive, so the
  ambiguous case-fallback refusal is exercised by code review only; and the host cannot create
  an absolute path over `PATH_MAX`, so the path-ceiling test lowers it through
  `library_cfg_t.max_path`. Invalid UTF-8 names cannot be created on APFS either; the scan's
  rule is the exported `library_utf8_valid`, tested directly.
- PSRAM placement is a board property. The arena and JSON are requested with
  `MALLOC_CAP_SPIRAM`; the host shim ignores capabilities, and ticket 13 verifies the placement.
- Verified: `make -C host check` (133 t14 checks, full host suite), `make -C host tools`, and
  both `build_sunton` and `build61`.
