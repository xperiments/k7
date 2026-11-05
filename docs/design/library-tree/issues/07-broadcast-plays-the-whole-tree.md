# 07 — The broadcast plays the whole Library tree

**What to build:** The live broadcast consumes the Playlist derived from the Library tree
instead of re-walking the Music directory. The playlist is depth-first with name-sorted
children, interleaving directories and Tracks. `skip` and `prev` become index arithmetic with
the same bounded history. The status snapshot reports the Track's relative path, generation and
playlist position, and a Track several levels deep plays.

**Blocked by:** 04 — Library at boot and the JSON endpoint.

**Status:** done (2026-10-20)

- [x] The stream plays Tracks in exactly the JSON/playlist order.
- [x] A Track three levels deep plays; `skip` and `prev` wrap correctly, and the history
      behaves as it does today.
- [x] The status snapshot carries the relative path, generation, track index and library
      count.
- [x] There is no per-pass `readdir()` playlist; a generation change resets the history.

## Comments

- The broadcast holds `const library_t *` and follows `library_track_at()`; there is no
  `opendir`/`readdir`/`telldir` left in `mini_icecast.c`. `cfg.music_dir` and
  `mini_icecast_music_dir()` are gone; the Library generation is the input.
- `skip`/`prev` are Playlist-index arithmetic over the same 16-entry history. A new lap
  resets the history, so `prev` at the first Track replays it, as the old directory pass did;
  `skip` at the last Track wraps to index 0.
- `mini_icecast_set_library()` is the seam for ticket 11: it swaps the generation, clears the
  transport flags and lets the broadcast reset history and start at the new generation's first
  Track. The caller must keep the old generation alive until the swap (leases arrive in 11).
- Status now carries `path`, `generation`, `track_index` and `library_count`; idle snapshots
  report empty strings, generation 0, index 0 and format `MI_FMT_NONE`, with no repeated
  `title_ver` bump every idle loop.
- Tests: t12 now uses a three-level `Album/Deep/three.wav` first, drives skip/prev/wrap/replay,
  checks path/generation/index/count, then swaps to a second Library generation and proves it
  starts at its own first Track. t06 checks the status path/generation/index.
- The broadcast still has no app-level generation publisher; ticket 11 wires
  `mini_icecast_set_library` into the rescan/card-arrival paths.
- Board stream-order coverage is not added here; ticket 13 is the board tier. Host tests cover
  playlist order, skip/prev/wrap and the three-level Track.
- Verified: `make -C host check` (145 HTTP checks, t14 152, t12 20, layering, awkward names),
  `make -C host tools`, and both `build_sunton`/`build61` (0x2eb3c0, 27% free; 0x20d210, 49%).
