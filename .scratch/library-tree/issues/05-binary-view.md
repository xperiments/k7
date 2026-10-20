# 05 — Binary view of the same generation

**What to build:** Serve the arena as `GET /api/library.bin` with the versioned header, CRC,
`Content-Length`, media type and the same generation/ETag as the JSON. The blob must be the
in-memory arena emitted verbatim: no per-request serialisation and no filesystem access. An
independent parser proves that a non-C consumer can read it.

**Blocked by:** 04 — Library at boot and the JSON endpoint.

**Status:** ready-for-agent

- [ ] The header's magic, version, counts, string length and CRC parse and verify.
- [ ] An independent Python parser reconstructs every path and size from nodes, edges and the
      track index, and matches `os.walk()` of the fixture tree.
- [ ] `Content-Type`, `Content-Length` and ETag are correct; JSON and blob share the same
      generation.
- [ ] Serving the blob performs no SD access and no per-request serialisation.
