# The device writes the playlists it is given

The global premise lets the device write the card exactly twice: it deletes the **Update file** it
consumed, and it rewrites `k7.select`, the 32-byte record of the chosen **Playlist**. Everything
else is host work, because the device must not manage someone's music. That rule was written when
the only card state the device needed was a choice — which list to walk — and a list could only
change by flashing an image.

The app has since grown an editor, and the card that owns the lists is usually in the player, not
in a reader. Editing a list therefore means: take the card out, put it in the computer, edit,
write, put it back. The alternative the premise leaves — the device accepts nothing — says that
the one thing a person changes every week is the one thing that needs a card swap. And the thing
being written is not music: it is the same kind of statement `k7.select` already is, "play these,
in this order", which is why the premise's own rationale — *the smallest writes that let the card
say what to do next* — reaches it.

## Decision

- **The premise gains one write and one directory.** The device may write the playlists the app
  sends, as one file per list under `k7.playlists/` at the card root, and it may create that
  directory if it is absent. Nothing else changes: never a Track, never a library directory, never
  a repair, never an upload. The premise's text is amended in `CONTEXT-MAP.md` and in the stream
  server's `CONTEXT.md`; this ADR is the amendment, as ADR 0069 was for `k7.select`.
- **One request, one file, answered after the bytes land.** `PUT /api/playlists/<id>` takes
  `{name, rows}` and creates or replaces that list's file; `DELETE /api/playlists/<id>` removes
  it. The device writes on the same low-priority task that already writes `k7.select`, below the
  decoder, and answers only when the file is on the card. A playlist is content, not a
  preference: an answer that arrived before the write would make the editor's "on the player"
  badge survive a power cut it did not survive.
- **The writes are bounded and atomic.** The id is 16 lowercase hex characters and must match the
  file it names; the name is under 64 bytes; the rows are at most 8,192; the directory holds at
  most 64 lists. A write goes to a temporary name and is renamed onto the final one on the same
  volume, so a card that fills mid-write has the old file, not half of a new one. A refusal is a
  refusal — `400` for a body this device cannot accept, `503` with no card, `507` when the card is
  full — and a refused request changes nothing.
- **The device reads the directory when a card mounts, and updates its index on its own writes.**
  It never re-scans on a request: while the card is in the player the device is the only writer,
  so its index cannot be stale, and a card prepared in a computer is read at the moment it mounts.
- **The index names the store.** `GET /api/playlists` reports each list's id, name, creation time,
  row count and a fingerprint, computed while the file is read. The fingerprint is what lets the
  app say whether its own copy is current without one request per list.
- **Deleting the selected list clears the selection.** `k7.select` goes back to id 0 and the
  broadcast returns to the library's own order, exactly as it does when a selection names a list
  that is no longer there.

## Considered options

- **Keep the device read-only for lists.** The premise stays as written; editing needs the card in
  the computer. Rejected: it is the ritual this change exists to end, and it would leave the
  editor able to write a card in a reader but not the card in the player.
- **A playlist store in the device's flash.** NVS or SPIFFS, no card writes at all. Rejected: a
  list would then have two homes — the card's file and the device's flash — and the card that
  travels between machines would stop carrying its lists, contradicting
  [ADR 0092](./0092-a-playlist-belongs-to-its-card.md).
- **Accept the whole set in one request.** `PUT /api/playlists` with every list, mirroring "an
  accepted image replaces the lists entirely". Rejected: the device would delete files the request
  did not mention, which is a second destructive write to spell out, and the body grows with the
  library for a change that touches one list.
- **Write asynchronously and answer first.** The shape `k7.select` uses. Rejected: a selection can
  be lost without harm (it is a preference, and the broadcast falls back); a playlist cannot, and
  the editor's badge would claim a file that is not there.
- **Read the directory on every request.** Rejected: a `GET` every few seconds would mean a
  directory walk and up to 64 file reads on the card the decoder is streaming from, for an index
  that only changes when the device itself changes it.

## Consequences

- **The premise's text changes in two documents**, and the "two writes" language becomes three.
  `docs/specs/CONTEXT.md`, `CONTEXT-MAP.md` and the firmware's card-path comment all name the one
  directory the device may create.
- **The broadcast gains one failure mode**: a full card refuses a write, and the app must show it
  rather than a spinner that ends in silence.
- **The app can edit any list the player holds**, whether or not this browser has ever opened that
  card — the store is the card's, and the editor becomes a window onto it.
- **A card write is now a user-visible act in the editor**, so the page says when it is writing,
  when it wrote, and when the card refused.
- **Wear is bounded by the number of edits**, not by the size of the library: one file of a few
  kilobytes per changed list, where the image was a ~2 MB rewrite for the same act.

## Supersedes

None. It amends the global premise's write set, as
[ADR 0069](./0069-the-snapshot-is-resident-and-the-card-carries-the-selection.md) amended it for
`k7.select`, and it is the permission [ADR 0095](./0095-a-playlist-is-a-file-on-its-card.md)
depends on.
