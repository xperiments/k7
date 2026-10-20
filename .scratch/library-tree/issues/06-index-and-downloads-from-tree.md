# 06 — Index and downloads from the tree

**What to build:** Make the human-facing index and the download surface read the Library tree
instead of walking the Music directory per request. `GET /` lists every Track in playlist order
with its size. `GET /music/<relative path>` serves the original bytes of a Track at any depth,
resolved by tree lookup. Traversal attempts are refused before any filesystem call. The old
directory-listing authority is retired.

**Blocked by:** 04 — Library at boot and the JSON endpoint.

**Status:** ready-for-agent

- [ ] The index lists exactly the Tracks found by `os.walk()`, in playlist order, with
      matching sizes.
- [ ] Every fixture Track, including nested ones, downloads byte-identically with the correct
      media type and length.
- [ ] Names containing `..`, an encoded slash, a NUL, a leading dot or a non-audio extension
      are refused before any filesystem access.
- [ ] `music_dir` no longer owns listing or path rules; the Library tree is the one authority.
