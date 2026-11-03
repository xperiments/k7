# The card is the only way to the player

Four buttons stood at the top of Process — **Send to player**, **Save to card**, **Download
.db**, **Download JSON** — and they were four answers to one question ("where does the image
go?") asked at the wrong moment. The answer is the card. It is already in the reader, the
device is built to consume the Update file from its root, and every other route was either a
second write, a workaround for a browser that cannot write one, or a diagnostic.

## Decision

- **The App does not send an image over the network.** The `snapshot` operation is gone from
  the bridge table, from the protocol, from the Launcher and from the Device copy's transport;
  the channel is fourteen JSON operations with no binary `args`. `POST /api/snapshot` stays in
  the firmware — the device still validates and flashes an image it is given — but nothing in
  the App calls it. Removing the route is a firmware change and not this one.
- **A run writes the card by itself.** When every queued Track has settled, the image is built
  and written to the card root, and the page says when. The write is debounced because a queue
  settles one Track at a time and each write is the whole image.
- **A Playlist edit rewrites the card's image** while the card is still open: the lists ride the
  image ([ADR 0051](./0051-a-playlist-lives-in-the-snapshot.md)), so the lists area is patched
  into the image already on the card instead of a re-measurement. Only after a run in this
  session has written that card: a stored image is never written over a card that has not been
  measured here.
- **No download, no export.** A browser that cannot open a folder read-write cannot produce the
  App's output; it measures, and the page says what is missing. The JSON export went with them:
  it was a debugging view of a run, not an artifact the player consumes.
- **Intake asks for the card root and refuses anything else** ([`card/shape.ts`](../../web/src/card/shape.ts)).
  One level down is the `music` directory, one up is a folder holding cards, and neither can be
  written: the paths of a run below `music/` are what the device hashes, so a wrong choice
  produces an image whose rows match nothing. The three refusals name what was chosen and what
  to open instead.
- **A dropped card keeps its identity.** A dropped directory cannot be written (ADR 0010), so
  its mark cannot be created — but if the card already carries one it is *read*, and the scope
  is its `mark:<id>` rather than a fresh fingerprint. Without that, re-dropping a card after
  adding one Track changes the scope and the covers and last image look lost.
- **A card that has already been measured is not read again.** The media row stores the file's
  size, modification time and content hash, so a re-scan recognises a Track from the card's own
  record and marks it *already measured*: no hashing, no tag parse, no decode, no model. Rows
  from before those fields existed are measured once more rather than guessed at.

## What the user sees

Pick the card (read-write, one gesture) → the queue fills → the rows settle, and the ones the
card already had are marked **already measured** instantly → `k7.db` is written to the card and
the header says when. Put the card in the player. Editing a Playlist afterwards, with the card
still in the reader, rewrites the image; nothing has to be measured twice.

## Consequences

- **The Device copy can measure and read, and cannot deliver an image.** Its origin has no
  directory picker, so it can never write a card. That is the honest limit of a browser
  without the File System Access API, and the page says so instead of offering a download that
  the product no longer has.
- **A card that has never been opened read-write has no identity of its own.** A dropped
  unmarked card is scoped by its contents, and the page tells the user to open it once with
  Choose card to give it a mark.
- **The four operations are now one behaviour**, so there is no state where the browser holds
  measurements the card does not: every settled run ends in a write or in a sentence saying why
  it could not.
