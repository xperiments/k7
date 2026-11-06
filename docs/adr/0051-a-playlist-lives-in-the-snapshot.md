# A Playlist lives in the Snapshot, and the broadcast can walk one

> **Amended by [ADR 0069](./0069-the-snapshot-is-resident-and-the-card-carries-the-selection.md).**
> The selected Playlist is no longer remembered in NVS. The device writes it to `k7.select` at
> the card root, a 32-byte CRC'd record, asynchronously on a task below the decoder. The format
> is v3 only, so the v1/v2 compatibility rules below no longer apply; the lists still live in
> the Snapshot, and the selection is still a choice, not a copy.

The front door grew a Playlists area, and a list had nowhere to live. The card is host-written
by policy, the page the device serves cannot write it at all (plain HTTP, no directory picker —
ADR 0049), and the device held no list of any kind: what it called its **Playlist** was the
derived order of the library tree, which is now called the **broadcast order** so that the word
can mean a list a person authored.

Three answers were available. Keep lists in the browser's IndexedDB: rejected by the owner,
because a mixtape that exists in one browser is not the mixtape. A sidecar file beside
`xtape.db`: nothing would read it, so it fails the same test. Or put them in the Snapshot,
which is already the one artifact the host produces for the device, already validated in full
before a byte is written, and already immutable once flashed.

## Decision

- **The Snapshot carries the lists.** `format_version 2` adds a list area after the string pool:
  a fixed-width list record (id, name, created, its rows), and for each row the Track's
  Library path — the same key `/api/play` resolves. Caps and byte layout are
  [`docs/specs/snapshot-format.md`](../specs/snapshot-format.md) §10.
- **A list is a selection, not a model of the library.** A Snapshot still never says what
  exists — the card and the boot scan do. A list says which of those tracks, and in what order.
- **An image is the whole truth for lists.** Every accepted image replaces the previous lists
  entirely; there is no merge, no delete-by-absence and no second write route.
- **The device walks a selected list.** `POST /api/playlists/select` takes a list id or `none`;
  the choice is remembered in NVS; at boot a selection naming a list the image no longer holds
  falls back to the tree's order. The selected list wraps at its end, skips a row whose
  fingerprint no longer matches the file, and is what **Shuffle mode** permutes.
- **The reader accepts v1 and v2.** An image with no lists is still written as v1, so a producer
  that has nothing to say about lists keeps producing something an older device can apply.
- **A row names a path, not a fingerprint.** The Track fingerprint binds a measurement to a
  file's size; a Playlist row has no measurement to protect, and binding the size would strand a
  row whose file was merely re-encoded. The slot is the path.

## What this costs

- **An edit is an image.** Changing one row rebuilds `xtape.db`, and `POST /api/snapshot` erases
  and rewrites the 2 MB partition and restarts the device. That is why the page drafts a
  Playlist and commits with an explicit **Send** rather than syncing.
- **A second validation surface on the reader.** Bounds, ordering, offsets and the CRC now cover
  a section the firmware did not have, and a bad list must be a refused image, not a boot loop.
- **Version lockstep.** A v2 image is refused by a firmware that knows only v1. There is no OTA
  and the page is served from the same flash, so page and firmware ship together; a browser that
  lost the page mid-update still holds v1 bytes in its **last image** only if it built them.

## What it deliberately does not do

The panel's Menu still browses the library tree, and nothing on the cassette screen names the
selected Playlist. Making the screen a list browser is display work with its own specification.
