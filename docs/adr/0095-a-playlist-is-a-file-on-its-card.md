# A Playlist is a file on its card

[ADR 0051](./0051-a-playlist-lives-in-the-snapshot.md) put the **Playlists** inside the
**Snapshot** because the Snapshot was the one artifact the host produced for the device and the
page could write no card at all. It chose that over a browser store ("a mixtape that exists in one
browser is not the mixtape") and over a sidecar file ("nothing would read it"). Both reasons
still stand; the container was the part that did not.

The image is a delivery, not a store. The device consumes `k7.db` and deletes it, so a card that
has been through the player carries no lists at all — they live in the device's flash and in
whatever the browser kept. A list edit rewrites the whole 2 MB image (the `patchListArea` path),
and reading a list back means parsing the image. Meanwhile the one thing a person changes every
week is a list, and it was the thing with no file on the card.

[ADR 0094](./0094-the-device-writes-the-lists-it-is-given.md) gives the device the write that
makes a file useful with the card in the player. This ADR decides the file, and takes the lists
out of the image.

## Decision

- **One file per list, at `k7.playlists/<id>.k7list`.** The directory keeps the card root's
  `k7.*` family (`k7.card`, `k7.db`, `k7.select`, `k7.covers`). The file's name is the list's id,
  16 lowercase hex characters, and the id is repeated inside the file and must agree with the
  name: the id is what `select` and `k7.select` carry, so a rename changes the file's contents and
  never its path.
- **A line-based text file, versioned.** `K7LIST 1`, then `id`, `name`, `created`, `rows <n>`, then
  one Snapshot path per line — UTF-8, LF, CRLF accepted on read, one line per value, so no
  escaping and no newline inside a name or a path (a list's name comes from a single-line field or
  from `nameFor()`, which folds whitespace). A reader refuses an unknown version, a name over 63
  bytes, a path over 1 KB, more than 8,192 rows, or a count that disagrees with the lines.
- **Format v5: the image carries measurements only.** The list area, the list strings and the
  header's list fields are gone; bytes 64..80 of the header are reserved and zero; and
  `row_pool_size` goes with them, because the pool is now only the rows' strings. The app's
  `readListArea`, `patchListArea`, `toSnapshotPlaylists` and Prepare's lists-only write mode are
  deleted, and v4 and older are refused like every version before them.
- **The app writes the file, and writes through the device.** With the card in the computer it
  writes the file through the card's handle; with the card in the player it sends the same list to
  the device ([ADR 0094](./0094-the-device-writes-the-lists-it-is-given.md)). The write is
  debounced after an edit, a refusal is shown and retried, and a deletion is the same act in the
  other direction.
- **The browser keeps pending edits, not lists.** A **Draft** is a list whose last edit has not
  reached its own card yet — because the card is out, the device is not answering, or the write
  was refused — and it is keyed by the list's own card, not by the card this browser last worked
  on. The card is the store; the browser is the queue in front of it.
- **Any card the player holds is editable.** The app no longer needs to have opened a card to
  author for it: `GET /api/playlists/<id>` gives a list's rows, the editor changes them, and the
  write goes back. A row the card's library cannot resolve is reported as missing, as it always
  was.
- **A closed card shows only its pending edits.** There is no mirror of a card's lists: a card
  that is neither in the computer nor in the player has no readable files, and the page says so
  rather than showing a dated copy.
- **Nothing old is carried.** No v4 list reader, no migration of lists out of an old image, no
  capability handshake with an older firmware. A card is re-prepared; a list that exists only in
  an old image is retyped or rewritten by hand. The lists are small and theirs.

## Considered options

- **Keep the lists in the image**, with the device writing only the selection. Rejected: the image
  is deleted by the device, so a card carries nothing, and every list edit is a 2 MB rewrite of
  the card for a few rows.
- **One `k7.playlists` file holding all the lists.** A single write, one parser, no directory.
  Rejected: it is the image by another name — a list edit rewrites every list — and it forgoes the
  one property a file per list gives: a list can be copied, diffed, or replaced on its own.
- **The name as the file name** (`<name>.k7list`). Readable in a listing, but a rename becomes a
  delete-and-create, two lists with one name collide, and the device has to open every file to
  find the selected id.
- **JSON.** Consistent with every other store the app owns, at the cost of a heap-allocated token
  array on the device for a file that can be 200 rows, and of escaping both ways for a format that
  has one string per line.
- **Keep a mirror of the files in the browser** so a closed card still lists. Rejected: a second
  home for list contents, whose staleness would be invisible, which is the drift this decision
  ends.
- **Keep the image's list area as a fallback for one version.** Rejected: two parsers and two
  truths for the length of an upgrade, and an ambiguous answer while both exist.

## Consequences

- **ADR 0051 is superseded**; [ADR 0065](./0065-the-card-is-the-only-way-to-the-player.md) now
  governs measurements only, and [ADR 0092](./0092-a-playlist-belongs-to-its-card.md) keeps its
  "the card is the store" and loses its image copy.
- **`docs/specs/snapshot-format.md` goes to v5**: no list area, `total_size` without it, the
  string pool one region, and the validation and versioning sections updated.
- **The Playlists area loses its two image-shaped paths**: the copy of a card's last image is no
  longer a source of lists, and a list edit no longer patches the image.
- **The device and the app must land together**, and the card must be prepared by the new app: a
  v4 image is refused, and a card with no `k7.playlists/` has no lists until the app writes them.
- **`web/CONTEXT.md` narrows three words**: **Playlist** gains its file, **Draft** becomes a
  pending edit, **List source** becomes the card's directory or the player's index, and the image
  stops being one.

## Supersedes

[ADR 0051](./0051-a-playlist-lives-in-the-snapshot.md): a Playlist no longer lives in the
Snapshot. What survives from it, and is not re-decided here: a list is a selection, not a model of
the library; the device walks one list at a time; a selection that names a list the card no longer
has falls back to the library's order; and a row names a path, never a fingerprint.
