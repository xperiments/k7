# The row carries the track's length, and the image is format 4

The App decodes every track it measures, so it already knows how long the track is:
`Analysis.track.duration` is the decoded length in seconds, and the analyser uses it to pick its
analysis window. The card's `k7.db` threw it away. A row carried the file's size in bytes — a
binding, not a length — and nothing else about the file; a library built from it could not show,
sort or filter by how long a track is without opening the audio again, and the device never opens
it before playback.

The question is only where the value goes. Format v3's row is 32 bytes with every bit spoken for:
the fingerprint, the file size, four 24-bit string offsets, and one packed `uint64` of
measurements. Its two reserved bits cannot hold a length. A separate section would be a second
structure to validate and to keep in step with the rows, and it would buy nothing: four bytes per
track is the whole cost.

## Decision

- **The length is a field on the row, in whole milliseconds.** The row becomes 36 bytes:
  `duration_ms` is the `uint32` at offset 8, the file `size` moves to 12, the four string offsets
  to 16, 19, 22 and 25, and the packed `measurements` word to 28. `0` is the one unknown; the
  producer rounds the decoded seconds, and clamps rather than wraps.
- **The image is format version 4, only.** `format_version` is 4 and `row_size` is 36. Versions
  1, 2 and 3 and the `XTAPESNP` magic are refused, exactly as ADR 0069 refused v2: there is no
  compatibility shim and no dual reader. The byte contract is
  [`docs/specs/snapshot-format.md`](../specs/snapshot-format.md).
- **Both ends change in one commit.** The browser's builder writes the field from the value it
  already measured; the C reader exposes it as `snapshot_row_t.duration_ms`; the App's own reader
  of the images it stored decodes it too. No second measurement pass is introduced.
- **The upgrade order is firmware, then App, then the card.** An old firmware refuses a v4 image
  and a v4 reader refuses a v3 image. A v3 card is refused and left in place; the device serves
  without analysis until the card is re-exported. Images this browser stored under a card scope
  are v3 as well, so they read as "no image" until that card is re-exported.
- **`duration_ms` is informational, not binding.** A row still matches on fingerprint *and* file
  size; the length never changes whether a row describes a track.

## Considered options

- **Read the length from the decoder when it is needed.** The device has the length only after it
  has opened the track, so a list, a sort or a search could not use it; and the App would re-open
  every file the image already described. The measurement exists at the one moment it is free.
- **Keep the version number 3 and widen the row.** The bytes would change anyway: old readers
  reject a 36-byte row, new readers reject a 32-byte one. That is a version break without a
  version number, and it leaves "v3" meaning two layouts on two cards.
- **A 16-bit seconds field in a 34-byte row.** It saves 10 KB at 5,000 tracks and loses the unit
  the device's own status and timeline already speak, and the decoded value's resolution. Two
  spare bytes would then need a reserved rule of their own.
- **A separate duration table.** Another region to validate, another ordering to keep, and no
  saving: the row is where the other per-track facts live.
- **Store the length as a tag on the file.** The device does not read tags, and writing into the
  user's audio files is a different feature with a different failure mode.

## Consequences

- The row grows 4 bytes. The canonical 3,485-track card goes from 177 KB to about 191 KB, and the
  5,000-track image from about 260 KB to about 280 KB, still well inside the 2 MB partition.
- Every card must be re-exported as v4, and every image a browser stored must be rebuilt. Until
  then the card's image is refused with a reason and the App's image source has no image; neither
  is a boot failure.
- The value is available to the query engine, the UI and the assistant, but this ADR adds only
  the field. Filters, sorts, the `library_track` JSON and the App's surfaces follow in their own
  change.
- The determinism rule covers the new field: the same decoded inputs produce the same bytes, and
  the host tests pin the offsets, `0` as unknown and the whole `uint32` range.

## Supersedes

None. This ADR changes the row layout ADR 0069 settled; it does not retire that decision, which
still governs where the image lives and how it is read.
