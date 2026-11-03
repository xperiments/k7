# The card carries a mark

The App keeps three things in this browser: analyses, covers with their titles, and the Last
image. The first is keyed by the track's content hash and is portable. The other two are keyed
by path and by the literal string `last`, which means a second card does not just miss — it
**silently inherits**: `Artist/Album/01.mp3` shows the other card's cover, and the Last image
that gets sent to the player may have been built for a different card entirely.

A fingerprint of the card's contents would answer "have these contents changed", and it is the
wrong question: adding one track would invalidate every cover. The question is "which card is
this", and only something written on the card can answer it across sessions.

## Decision

- **`k7.card`**, a small JSON file at the root of a **Card** — a directory containing `music/`;
  anything else is a Folder and is never written to. It holds a random 122-bit id, a creation
  time and an optional user label. No secrets, no measurements, no paths.
- **The App writes it, the browser reads it.** Written at the explicit writable pick, never
  overwriting a file it did not write, never required: a read-only card falls back to a
  fingerprint of sorted `path:size:mtime`, and the scope key says which it is.
- **It scopes the card-level stores only.** Covers and the Last image key on the scope; an
  analysis stays keyed by content and remains portable across cards.
- **Clones are accepted.** It is impossible to tell a `dd` copy from the original, and it does
  not matter: measurements are content-keyed, so two clones share them correctly.
- **The device may read it and never writes it**, later, read-only, reported in `status`. The
  global premise stands: the only card write the device performs is deleting the Update file.

The file, the policy table and the fingerprint are [`card-mark.md`](../specs/card-mark.md).

## Consequences

- **The card root gains a second control file**, beside the Update file. It is not the Update
  file, is never deleted by the device, and is never mistaken for one.
- **A formatted card is a new card**, and its old scope is orphaned until pruned or forgotten.
- **A card a browser cannot write has no identity**, only a fingerprint, and loses its covers
  when its contents change. That is the honest limit of a card without a mark.
- **Nothing gains authorisation.** A mark can be copied and edited by anyone with a reader; it
  is a name, not a credential.
