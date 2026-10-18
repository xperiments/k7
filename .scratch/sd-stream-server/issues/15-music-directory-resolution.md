# 15 — Concentrate the music-directory resolution

**What to build:** The rule that a name never escapes the **music directory** is written
twice with different strength. `download_route` checks the name, joins the path, then
re-verifies the joined path sits under the directory; `index_route` relies on the name check
alone. Neither copy is reachable without an HTTP request.

Add a music-directory module that owns the rule:

- `music_resolve(dir, name, out, why, why_size)` — safety, source-format acceptance, joining
  and the containment check in one place. Returns `MUSIC_OK` with a path, or a named
  refusal (`UNSAFE`, `UNSUPPORTED`, `TOO_LONG`, `NO_CARD`, `NO_DIR`).
- `music_dir_list(dir, fn, ctx)` — walks the directory and calls `fn(name, size)` for each
  **track** it accepts, reusing the same predicate. Returns the count, `-1` when the
  directory cannot be read; `fn` returns false to stop.

`download_route` and `index_route` become adapters that map results to HTTP status codes and
render markup. Trailing-slash directories are handled in one place.

**Blocked by:** none

**Status:** done (host tier and both firmware builds; board tier not run - no board attached)

- [ ] `..`, a leading dot, `/`, `\`, an empty name and an embedded NUL are all refused by
      one function.
- [ ] A non-audio extension is refused as unsupported (404 at the route).
- [ ] A name that would leave the directory is refused even if it passed the name check.
- [ ] The index lists exactly the files `music_resolve` would serve, and nothing else.
- [ ] `music_dir_list` stops early when the callback asks it to.
- [ ] UTF-8 names, awkward names and over-long names are covered by a test table.
- [ ] Host unit tests and the integration suite pass.

**Files:** `components/mini_files/include/music_dir.h` (new),
`components/mini_files/music_dir.c` (new),
`components/mini_files/mini_files.c`, `host/tests/t09_music_dir.c` (new),
`host/Makefile`.

**Tests:** t09 uses `mkdtemp` with a mix of accepted and refused names, including
`..%2f`-style names after decoding, a name at the FAT LFN limit and one over it.

**Source:** architecture review, candidate 6.

## Comments

- Added `music_dir.c/.h`: `music_resolve()` owns name safety, format acceptance, joining and the
  containment check; `music_dir_list()` walks the directory with the same predicate;
  `music_name_is_listable()` is the index's form of the rule.
- Both routes are adapters now: the download route maps `music_result_t` to 400/404/503, and
  the index renders only what `music_dir_list()` reports. The index can no longer be more
  permissive than the download.
- `t09_music_dir` covers `..`, slashes, backslashes, dot-files, the LFN limit, UTF-8, a
  trailing-slash directory, early stop and an unreadable directory.
- Verified: unit, layering and integration suites; both firmware builds.
