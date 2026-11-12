# A virtual card is a staged folder under a working directory

Prepare (today "Process") is the only place the App produces the card's image, and it produces
it against a card that is already in the reader: pick the card read-write, measure what is on
it, write `k7.db` ([ADR 0065](./0065-the-card-is-the-only-way-to-the-player.md)). There is no way
to build a card *before* the physical SD is in the machine, and no way to compose a card's
`music/` tree in the App — the user arranges files in Finder and the App measures what it finds.
The rework makes the card the object the App works on: a card can be prepared ahead of time as a
**virtual card**, composed by copying audio in, measured continuously, and written onto a real SD
later.

The platform constrains the shape, read against primary sources in
[`docs/research/virtual-sd-cards.md`](../research/virtual-sd-cards.md): the web has no block-device
access and no way to author a FAT32/exFAT image, so a virtual card cannot be a disk image; OPFS
would work but is quota-capped, invisible and evictable; a picked directory's read-write grant
does not reliably survive a browser restart.

## Decision

- **A virtual card is a card-shaped folder under a user-granted working directory.** The user
  grants one directory read-write (`showDirectoryPicker({mode:'readwrite'})`); the App keeps one
  subfolder per virtual card — `music/` plus `k7.db`, `k7.card`, `k7.covers` — so a virtual card
  is *a Card that is not on an SD yet*, the same shape as a real one. It is not OPFS and not a
  disk image.
- **Capacity is a soft cap, not an allocation.** "Create a card" asks for a name and a size
  (16/32/64/128/256 GB + custom), matched to a real SD the user owns; the App counts usage
  against it and against the working directory's real free space. Nothing is pre-allocated.
- **The working directory's write grant is re-asked once per session.** It does not reliably
  survive a browser restart, so the App shows one "reconnect working directory" click at session
  start and remembers where it was. OPFS stays for the existing caches (covers, analyses) and
  nothing more.
- **Compose is virtual-only.** Dropping a folder copies its files (streamed) into the virtual
  card's `music/`, preserving the folder structure, and measures each file as its copy finishes —
  no full-card scan first. A **real SD stays "measure in place"** as today and is the finish
  target; a mounted real SD also gets the auto-reconciliation watcher, so files dropped onto it
  in Finder are measured in place. This keeps the rule that the App must not write audio into the
  user's music collection ([`card-mark.md`](./../specs/card-mark.md) K2) and avoids slow,
  fragmenting FAT32 writes during composition.
- **The watcher is polling, and `k7.db` is rebuilt per file.** The browser has no native
  filesystem watcher, so reconciliation is a periodic re-walk and diff by content hash + size +
  mtime, ignoring copies still in progress. Each finished analysis is folded into the measured
  set and `k7.db` is rebuilt and rewritten (a small debounce so a 100-file drop commits a few
  times, not 100). The image stays complete — incremental is per-file *timing*, not an append
  format, because the device consumes a complete image at boot
  ([`snapshot.md`](./../specs/snapshot.md) §1).
- **A playlist belongs to a card.** A Playlist row is a path below a card's `music/`, so the
  current "a Draft belongs to the user, not a card" is retired: the Playlists area is entered
  within a card, each card's playlists are authored against its own library and ride that card's
  image. Cross-card copying is deferred.
- **The finish is "write to SD".** It picks the mounted SD read-write and copies the virtual
  card's contents into its root, re-copying only files whose content hash changed, then verifies
  size/hash and shows a "safe to eject" state. The mark carries over, so the SD is recognised as
  the prepared card. Copying in the OS stays a free fallback.
- **The area is renamed Prepare.** It names the act, not the machinery.

## Considered options

- **OPFS as the virtual-card store.** Always available, no permission, but quota-capped,
  invisible to the user, and evictable — a 128 GB card is a ceiling the browser may sit under.
  Rejected: the working directory is real disk the user can see, back up and point at a big
  drive.
- **A FAT32/exFAT image written in the browser.** No first-party path exists to author a
  mountable image or write a raw SD (WebUSB blocklists Mass Storage). Rejected as impossible;
  materialization is a file copy.
- **Drop-to-compose onto a real SD too.** Unifies real and virtual, but writes audio into a real
  card's `music/` (against `card-mark.md` K2) and does slow FAT32 writes during composition.
  Rejected: compose happens once in the working directory, and the copy to the SD happens once at
  "write to SD".
- **Keep playlists detached, fold them in at write time.** A playlist would be a list of paths
  that may reference nothing on the target card, and the device already hides that by skipping
  missing rows. Rejected: a playlist must live on the card whose contents it names.

## Consequences

- **A virtual card is only ever materialized by copying.** There is no "flash this card" in the
  browser; the SD is reached through a read-write pick or the OS file manager.
- **The App writes audio bytes into a directory it owns** — a virtual card's `music/` — for the
  first time. It still never writes into a Folder (the user's music collection), so the
  read-only-drop rule of [ADR 0010](./0010-tagged-copies-and-xtape-manifest.md) stands for
  Folders and is deliberately not extended to virtual cards.
- **Two cards can share a mark** (a virtual card and the SD written from it, or a `dd` clone),
  which `card-mark.md` already accepts as "the same card"; measurements and covers are
  content-addressed, so nothing is lost.
- **The Playlists area becomes card-scoped.** The "drafts survive every card" convenience is
  gone; a mix is re-authored per card until a copy action is built later.
- **The real-SD path is unchanged in behaviour**, except that a mounted SD now gets the
  reconciler.

## Supersedes

None outright. It reworks the flow [ADR 0065](./0065-the-card-is-the-only-way-to-the-player.md)
established (pick → measure → write) by adding the virtual-card staging path, and it retires the
"Draft belongs to the user, not a card" definition in
[`web/CONTEXT.md`](../../web/CONTEXT.md).
