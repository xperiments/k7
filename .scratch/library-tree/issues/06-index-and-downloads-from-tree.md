# 06 — Index and downloads from the tree

**What to build:** Make the human-facing index and the download surface read the Library tree
instead of walking the Music directory per request. `GET /` lists every Track in playlist order
with its size. `GET /music/<relative path>` serves the original bytes of a Track at any depth,
resolved by tree lookup. Traversal attempts are refused before any filesystem call. The old
directory-listing authority is retired.

**Blocked by:** 04 — Library at boot and the JSON endpoint.

**Status:** done (2026-10-20)

- [x] The index lists exactly the Tracks found by `os.walk()`, in playlist order, with
      matching sizes.
- [x] Every fixture Track, including nested ones, downloads byte-identically with the correct
      media type and length.
- [x] Names containing `..`, an encoded slash, a NUL, a leading dot or a non-audio extension
      are refused before any filesystem access.
- [x] `music_dir` no longer owns listing or path rules; the Library tree is the one authority.

## Comments

- The index and the download route read `const library_t *` only. `music_dir.c/.h` and
  `t09_music_dir` are deleted; `mini_files` no longer owns a name rule, a join or a listing.
  The broadcast still has its own walk until ticket 07; this ticket does not claim otherwise.
- The index is Playlist order (`library_track_at`), shows the relative path and the node's
  size, and links `/music/<url-encoded relative path>` with the `/` separators preserved.
  `check_index.py` now derives the expected depth-first order and compares it, not just a set.
- The download route decodes one segment at a time, so `%2F` inside a segment is refused
  instead of becoming a separator the client did not send. It rejects empty, `.`/`..`,
  traversal-shaped, leading-dot and backslash segments, and embedded NULs before
  `library_find()`; then it opens `library_abspath()` of the resolved node. A file deleted
  after boot is a 404.
- The scan now rejects any name containing `..`, so a `mix..final.mp3` cannot be indexed while
  the route refuses it. Ticket 03's fixtures gain that case and an `library_abspath` check.
- A card inserted after boot still does not rebuild the generation (ticket 11). The retry now
  sets a card-mounted flag; the index says "Library not rebuilt" instead of falsely claiming
  there is no card, and the appliance log says the rescan path will rebuild it.
- README and `docs/specs/sd-stream-server.md` are updated from "single flat directory" to the
  tree, relative-path downloads and the new components; the final documentation pass is ticket
  14.
- Verified: `make -C host check` (145 HTTP checks, t14 152 checks, layering, awkward names),
  ASan/UBSan on t14, and both `build_sunton`/`build61` (0x2eb120, 27% free; 0x20cfc0, 49%).
