# 02 — Extract the source-format authority

**What to build:** One shared authority for the three source formats (`.mp3`, `.flac`, `.wav`) so
the new library can decide what a Track is without depending on the broadcast component. The
broadcast, the index, the download route and the library must all agree on the same list. This
is a prefactor: the device behaves exactly as it does today.

**Blocked by:** None — can start immediately.

**Status:** done (2026-10-20)

- [x] The format authority lives in one place and both the library and the broadcast use it.
- [x] Format recognition is unchanged: case-insensitive extensions, and callers still ignore
      names beginning with a dot.
- [x] The host suite and both firmware builds pass.
- [x] The layering check proves there is no component cycle between the library and the
      broadcast.

## Comments

- `components/portable/audio_format/` now owns the extension→enum rule; `mi_decode.h` and
  `mini_icecast.h` include it, so no consumer includes the decoder just for a format.
- The layering check gains **S5**: `audio_format` includes/requires no consumer, both public
  headers take the rule from it, and a single component defines it. The library branch of the
  rule is present and activates when ticket 03 adds the component; the library does not exist
  yet, so that half is not exercised today.
- Verified: `make -C host test`, `make -C host layering`, `make -C host tools`, and both
  `build_sunton` and `build61` firmware builds pass.
