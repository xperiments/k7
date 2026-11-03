# xtape becomes K7

The product has been called K7 on its own glass since [ADR 0055](./0055-the-shell-is-resident.md):
the boot mark the firmware paints is a K7, because that is what a cassette is called in French
and Catalan, and the thing is shaped like one. The repository, the hostname, the card's files
and the tag written into people's music are still `xtape`. Two names for one object is worse
than a rename, and the domain — `k7.xperiments.dev` — settles which one is the product.

## Decision

- **Every identifier containing `xtape`/`X-TAPE` becomes `k7`/`K7`**: the mDNS hostname, the
  Update file, the card's mark, the stored-analysis tag, the Snapshot magic, the package name,
  the IndexedDB stores, the `XTAPE_*` build variables, and every document written from here.
- **Three shims keep bytes already in the world working**, because renaming code does not rename
  a card or a file:
  - the Snapshot validator accepts `K7SNAP` or `XTAPESNP`;
  - the card reader tries `/k7.db` then `/xtape.db`;
  - the tag reader accepts `K7` or `XTAPE`, and the writer removes the old frame when it writes.

  There is no hostname shim, because there is nothing to shim: an unrenamed board serves the old
  app at `http://xtape.local` and no Launcher, so it could never have bridged anyway. The App's
  allowlist is `http://k7.local` and stays one entry.
- **History is not rewritten.** Git history and ADRs 0001–0056 say xtape because that was the
  name; a record that edits itself is not a record. New ADRs say K7 and cite the old ones.
- **The repository is renamed**, so the AGPL source offer's `SOURCE_URL` moves with it.

## Consequences

- **One release of read-both, in four places.** Without it the rename is not a rename but a
  break that strands every card and every tagged file.
- **`k7.db` and `K7SNAP` do not work on old firmware**, which needs a flash. That direction is
  unavoidable and it is one owner, one board.
- **All browser storage is fresh anyway**: both the App's origin and the device's hostname
  change, so no IndexedDB store survives the move and renaming them orphans nothing.
- **The Device copy's address changes** from `http://xtape.local/` to `http://k7.local/app`;
  bookmarks need updating, which is the point of the rename.
