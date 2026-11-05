# 05 — Binary view of the same generation

**What to build:** Serve the arena as `GET /api/library.bin` with the versioned header, CRC,
`Content-Length`, media type and the same generation/ETag as the JSON. The blob must be the
in-memory arena emitted verbatim: no per-request serialisation and no filesystem access. An
independent parser proves that a non-C consumer can read it.

**Blocked by:** 04 — Library at boot and the JSON endpoint.

**Status:** done (2026-10-20)

- [x] The header's magic, version, counts, string length and CRC parse and verify.
- [x] An independent Python parser reconstructs every path and size from nodes, edges and the
      track index, and matches `os.walk()` of the fixture tree.
- [x] `Content-Type`, `Content-Length` and ETag are correct; JSON and blob share the same
      generation.
- [x] Serving the blob performs no SD access and no per-request serialisation.

## Comments

- The blob is the arena pointer, not a copy or a rendering: `library_blob()` returns
  `lib->arena` and the route sends it with its exact size. A frozen-fixture test fetches the
  binary before and after deleting a track from the card and compares the bytes.
- `check_library_bin.py` reads only the wire layout: 64-byte header, 32-byte nodes, u32 edge
  array, u32 track index and string table. It verifies magic, version, header size, LE and
  truncated flags, counts, string length, every parent/child edge, each node's track index,
  the track array, the total bytes and the CRC (via `binascii.crc32` over the payload). It
  reconstructs every path and size and compares with `os.walk()`.
- The JSON and blob routes share `library_generation()` for the ETag and the body's
  `generation`; the header's generation is the same field. The corrupted-CRC test flips a
  node flags byte, which no other check looks at, so it fails for the CRC and not for a path
  mismatch.
- `truncated` is a budget flag, not a promise that the arena is smaller: a JSON-only budget
  overflow leaves a complete arena. The cap test uses `--subset` explicitly; the parser accepts
  a full arena with the flag set.
- Host only reconstructs paths against `os.walk()`; on the board the parser runs
  `--header-only`, because there is no host tree to compare against.
- Generation is still `1` for this boot; ticket 11 makes it move. The ETag and the body already
  share the field.
- Verified: `make -C host check` (128 HTTP checks, t14 149 checks, layering, awkward names),
  `make -C host tools`, and both `build_sunton`/`build61` (0x2eaf50, 27% free; 0x20cdb0, 49%).
