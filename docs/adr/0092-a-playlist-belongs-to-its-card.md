# A Playlist belongs to its card, and the page says how far it has travelled

> **Amended by [ADR 0095](./0095-a-playlist-is-a-file-on-its-card.md).** The card is still the
> store and the page still says how far a list has travelled. What changed: the store is
> `k7.playlists/<id>.k7list`, not the list area of the image; the fingerprint replaces comparing
> rows against a copy of the image; a card that is not open shows only its pending edits, because
> there is no image copy to read; and the Device card is editable now that the device takes a
> write.

[ADR 0051](./0051-a-playlist-lives-in-the-snapshot.md) put the lists in the Snapshot and turned
down its two alternatives by name: a mixtape kept in one browser's IndexedDB "is not the mixtape",
and a sidecar file beside the image "has nothing to read it". The Snapshot stayed the delivery.
The authoring store drifted back to the browser anyway. `web/src/playlists/drafts.ts` became what
a list was read from, `toSnapshotPlaylists()` became what an image carried, and the Playlists area
showed two collections — the device's lists from `GET /api/playlists`, and this browser's drafts —
so after a run the same list appeared twice, once in each block, and the only thing telling the
two apart was the block they sat in. The area also ignored the header's card switcher, the one
control every other area follows: with a virtual card selected it still showed the Device card's
lists, and with the player holding a different card it still called that card's lists "On the
device".

Three facts shaped the decision. A card in the computer carries its lists inside the `k7.db`
image written to it, and the App already rewrites that file's list area on every edit while the
card is open (`patchListArea` in `web/src/ui/state/analysis.tsx`). The **Device card** is the one
source the App can never write, and the player answers for it at `/api/playlists` and
`/api/playlists/<id>`. And a permission to read a card does not survive a reload while the
selected card does, so a page that could only read cards would have nothing to show until someone
picked one again.

## Decision

- **The selected card answers for its lists.** With the **Device card** selected the player
  answers; with an SD or virtual card selected, the list area of the `k7.db` that card carries
  does. This is the card switcher's own selection, the one C4 names, and the Playlists area stops
  being an exception to it.
- **A card that is not open is answered by the App's own copy of the file it wrote.** The **Last
  image** is stored per **Scope** in `web/src/storage/snapshots.ts`, and it is the bytes the App
  put on that card; the page reads the list area from it and says *on this card as of the 14th*
  rather than claiming a card it has not read. The card itself is read when it is open this
  session — a pick, or Prepare holding it — and **Read the card** asks for the grant when someone
  wants the card's current bytes. This is the rule Media already uses for a library: the player
  now, else the Last image this browser made.
- **One list appears once.** Rows are joined by the list's id — the 16 hex characters derived
  from its name and creation time, which the draft and the image already share — so a list is one
  row with a state, not two rows in two blocks.
- **Two independent facts per list.** How far it has **reached** — only here, on this card, on the
  player — and whether the source's copy is **current**: the source has these rows, or has an
  older copy (*edited — the card has the older copy*), or the last write failed and says so. A
  list can be on the player and out of date here; one ladder cannot say both.
- **A card's list this browser has never seen is adopted.** Opening it creates a draft that keeps
  its id, name, creation time and rows, under that card's scope. From then on it is an ordinary
  draft, and the next write puts it back on the card. A card prepared or edited on another
  computer is therefore editable here.
- **A deletion is a source write.** The list leaves this browser immediately and leaves the card
  when the card is written; until then the row carries a **pending removal** that names itself
  (*removed — the card still carries it*) and the write clears it.
- **The Device card's lists stay read-only.** *Walk this* is the one command, as
  `web/src/device/playlists.ts` has it. A draft under that card's scope is still authorable and is
  written when the card is next in the computer, because this browser's scope names that card.
- **No new file format, and no send from this page.** The Snapshot and its list area remain the
  only delivery and the image remains the whole truth for the device (ADR 0051); the image still
  goes through Prepare and the card (ADR 0065). Nothing is written beside `k7.db`.
- **The page, Editor and Assistant are three places, not two collections.** `#/playlists` is the
  selected card's lists; `#/playlists/:id` is the editor; `#/playlists/:id/ask` is the same editor
  with the assistant showing. See [ADR 0093](./0093-ask-edits-the-list-it-is-beside.md) for the
  assistant's move.

## Considered options

- **One `k7.playlists` file at the card root.** A JSON list of lists beside `k7.card`, `k7.db` and
  `k7.covers`. Rejected: a fourth place for the same rows when the Snapshot already carries them,
  and the App would then have to keep three copies — the file, the image and the drafts — in
  agreement, which is the drift this decision exists to end.
- **One file per list, `.k7list` or plain `.m3u8`.** Rejected: the file name becomes the list's
  identity, which collides with the id the device walks and remembers at `k7.select`, and the
  device reads its lists from the image, so an M3U would be read by nothing on the device while
  adding a parser on the host.
- **Two collections scoped to the selected card.** The smallest change, and rejected: it keeps the
  duplicate rows and keeps storage in the page's vocabulary.
- **The card as the only truth, with drafts removed.** Rejected: the Device card cannot be
  written, and the read grant does not survive a reload, so a list could be neither authored nor
  shown without picking the card again. The browser's store is a copy, not a home — which is what
  ADR 0051 meant.
- **Ask for the card's grant whenever the page opens.** Rejected: a permission prompt on a page
  visit, and nothing to show when it is refused. The App's own copy answers without a prompt.
- **Delete as a local action only.** Rejected: the card's list comes back on the next read, so the
  page shows a list the person deleted.
- **A send button on the page.** Rejected: an image carries measurements as well as lists, and the
  device reboots when it takes one; two doors to one POST is what ADR 0051's one-write rule and
  ADR 0065's one delivery are there to prevent.

## Consequences

- **The App needs the read side of the list area**, where only `patchListArea` writes it today. A
  card's lists are then readable with the player off, which is also what makes the editor's row
  facts answer for a card (see `docs/specs/front-door.md` §4.2).
- **The page computes reach and currency per row** from three reads — the drafts, the card or its
  copy, and the player's status — and caches nothing new.
- **Prepare is unchanged.** A run still builds an image from `toSnapshotPlaylists()`, and an edit
  still patches the card's copy while it is open. The new read changes what the page shows, not
  what the device is sent.
- **A card written by another browser shows its lists when it is opened**, and before that shows
  only what this browser holds; the page names which of the two it is showing.
- **`web/CONTEXT.md` gains two words and narrows one.** **List source** names the answer for a
  card's lists, **Pending** names a change the card has not taken, and **Draft** becomes a state a
  list can be in rather than a place it is kept.
- **The host pins the state join.** The resolution rule and the join are pure functions with tests,
  because reach and currency are the kind of thing that is wrong quietly.

## Supersedes

None. It amends
[ADR 0051](./0051-a-playlist-lives-in-the-snapshot.md)'s authoring side: the lists still live in
the Snapshot and an accepted image is still the whole truth for the device. What changes is where
the App reads a list from, and what the page says about it.
