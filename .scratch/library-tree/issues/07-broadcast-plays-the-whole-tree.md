# 07 — The broadcast plays the whole Library tree

**What to build:** The live broadcast consumes the Playlist derived from the Library tree
instead of re-walking the Music directory. The playlist is depth-first with name-sorted
children, interleaving directories and Tracks. `skip` and `prev` become index arithmetic with
the same bounded history. The status snapshot reports the Track's relative path, generation and
playlist position, and a Track several levels deep plays.

**Blocked by:** 04 — Library at boot and the JSON endpoint.

**Status:** ready-for-agent

- [ ] The stream plays Tracks in exactly the JSON/playlist order.
- [ ] A Track three levels deep plays; `skip` and `prev` wrap correctly, and the history
      behaves as it does today.
- [ ] The status snapshot carries the relative path, generation, track index and library
      count.
- [ ] There is no per-pass `readdir()` playlist; a generation change resets the history.
