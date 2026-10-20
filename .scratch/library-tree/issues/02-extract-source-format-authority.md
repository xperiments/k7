# 02 — Extract the source-format authority

**What to build:** One shared authority for the three source formats (`.mp3`, `.flac`, `.wav`) so
the new library can decide what a Track is without depending on the broadcast component. The
broadcast, the index, the download route and the library must all agree on the same list. This
is a prefactor: the device behaves exactly as it does today.

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

- [ ] The format authority lives in one place and both the library and the broadcast use it.
- [ ] Format recognition is unchanged: case-insensitive extensions, and callers still ignore
      names beginning with a dot.
- [ ] The host suite and both firmware builds pass.
- [ ] The layering check proves there is no component cycle between the library and the
      broadcast.
