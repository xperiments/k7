# 03 — Library tree module, host tier

**What to build:** The deep module that owns the Library tree. It scans the Music directory
recursively with one directory handle at a time, sorts each directory by name, accepts only
Tracks whose name passes the source-format rule, validates UTF-8, enforces the path ceiling,
and stops at the memory cap with `truncated` set. It builds the immutable arena and the prebuilt
JSON, and it resolves a relative path by walking the tree. This ticket is host-testable and
does not wire the module into the boot path or HTTP.

**Blocked by:** 02 — Extract the source-format authority.

**Status:** ready-for-agent

- [ ] A temporary tree containing nested directories, UTF-8 names, dot-files, a symlink, a
      128-byte name, a path just under and just over `FILENAME_MAX`, an unreadable directory
      and an empty root scans with the expected counts and DFS order.
- [ ] Lookup resolves nested paths, matches case-insensitively as a fallback, refuses an
      ambiguous case fallback, and makes no filesystem call.
- [ ] The JSON is valid and matches the tree; the arena header, nodes, edges, track index,
      strings and CRC parse with an independent test.
- [ ] The budget cap produces `truncated`, keeps the indexed portion usable, and the arena is
      allocated from PSRAM, not internal RAM.
- [ ] A name that is not valid UTF-8 is skipped and logged; it never reaches the JSON.
