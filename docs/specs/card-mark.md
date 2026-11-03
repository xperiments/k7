# The card's mark

**Goal.** A card can be recognised across sessions without reading every file on it. The mark
is a small file the **App** writes at a card's root; it names the card, so the browser's
card-level state — covers, the Last image, a run in progress — belongs to one card and not to
whichever card happens to be inserted.

It is a name, not a credential. Nothing on a FAT32 volume can be an identity: any reader can
copy it, edit it, or `dd` it onto a second card. The mark answers *"is this the card I
prepared?"* and never *"is this card allowed to do this?"* — the second question has no answer
on this device, which has no authentication at all.

**Status:** designed, not built. The card writer, the store scoping and the fingerprint are
all new; the device does not read the mark yet.

---

## 1. Card, and Folder

**K1. A Card is a directory that contains `music/`.** `music/` is the Library the device owns
and walks; it is the one structural fact that makes a directory a card.

**K2. Anything else is a Folder.** A dropped album rip, a picked collection, a backup — all
Folders. A Folder is measured exactly like a card, and it is **never marked**, because the App
must not write files into directories that belong to the user's music collection.

**K3. "Prepare as a Card" is an explicit act.** On a directory with no `music/`, the App offers
to create `music/` and write the mark. That is the only way a Folder becomes a Card, and it
takes a click, not a guess.

**K4. The mark is written only where K1 or K3 says so.** No path in this codebase writes
`k7.card` into a Folder.

---

## 2. Why it is needed at all

None of the App's stores are scoped to a card today:

| Store | Key | What a second card does |
|---|---|---|
| `k7-analysis` (today `xtape-analysis`) | `contentHash:analyserVersion:modelSet` | correct — an analysis is a fact about bytes and should be portable |
| `k7-media` | path below `music/` | **wrong**: `Artist/Album/01.mp3` on card B joins to card A's cover and title |
| `k7-snapshots` | the literal key `last` | **wrong**: one Last image for every card, and sending it to the device is a silent mistake |
| drafts | draft id | correct — a draft is the user's, not the card's |

**M1. The mark scopes the card-level stores.** `k7-media` keys on `(scope, path)`;
`k7-snapshots` keys on `scope`; a run in progress records its scope. `k7-analysis` keeps its
content key and is not scoped: the same file on two cards is measured once.

**M2. The Analysis stays portable.** A card mark never appears in a cache key for a track.

---

## 3. The file

**F1. Name and place.** `k7.card`, at the card's root — beside `k7.db`, the Update file. Root
is already the card's control surface, and the device's read (later) is one `open` rather than
a directory walk.

**F2. It is never `k7.db`.** The Update file is consumed and deleted by the device at boot
([`snapshot.md`](./snapshot.md) §U7). A mark that shared its name would be deleted, and a mark
that shared its file would be flashed.

**F3. JSON, small, versioned:**

```json
{ "v": 1, "id": "f0b1c2d3-…", "created": "2026-11-03T01:20:00Z", "label": "kitchen card" }
```

- `v` — the mark's own format, not the App's version.
- `id` — 122 random bits from `crypto.getRandomValues`, formatted as a UUID v4. Not
  `crypto.randomUUID`: the Device copy at `http://k7.local/app` is not a secure context, and a
  function that throws there is worse than sixteen bytes of `getRandomValues`, which works
  everywhere.
- `created` — ISO 8601, UTC, informational.
- `label` — optional, user-chosen, for the interface. Written by the App, never by the device.

**F4. No secrets, no measurements, no paths.** The mark is not a cache and not a backup; it is
a name and a label.

---

## 4. What the App does with one

| State of `k7.card` | App's behaviour |
|---|---|
| absent, card writable | create it, then use its id as the scope |
| absent, card read-only (write-protected SD, dropped folder, browser without the picker) | fingerprint scope (§5); say the card is unmarked |
| valid, `v` known | use `id` |
| valid, newer `v` | use `id` if it parses; never write; say the card was marked by a newer App |
| unparseable, or no usable `id` | do **not** overwrite; fingerprint scope; offer **re-mark this card** as an explicit action |
| two cards with the same `id` (a clone) | treated as the same card, deliberately |

**P1. Never overwrite a mark this App did not write.** A file with our name and unexpected
contents is either a newer version's mark or somebody else's mistake; silently replacing it
destroys the identity its cache hangs from and cannot be undone.

**P2. Clones are accepted, not detected.** It is impossible to tell a `dd` clone from the
original, and it does not need to be: every measurement is content-keyed, so the two cards
share analyses correctly. They also share covers, which is right for a clone and is the price
of not being able to tell.

**P3. A formatted card is a new card.** The mark is gone, so a new one is written and the old
scope is orphaned. Orphans are pruned by age in Settings, and **Forget this card** removes a
scope by hand.

**P4. The mark is written at the pick, not at the send.** The writable picker is already an
explicit "prepare this card" act, and the run needs a scope before it starts writing covers.
A dropped folder never writes anything.

---

## 5. The fingerprint

**G1. When there is no usable mark, the scope is a fingerprint of the card's contents:**
SHA-256 over the sorted lines `path:size:mtime` of every track below `music/` — the walk the
scan performs anyway, so it costs no extra I/O.

**G2. A fingerprint is a change detector, not a name.** Adding one track changes it, so the
covers scoped to the old fingerprint are lost. That is acceptable *only* in the degraded case:
a card that could never keep them is not a card whose covers can be trusted across edits.

**G3. The two concepts are never conflated.** The **mark** answers "which card"; the
**fingerprint** answers "which contents". Scope keys say which one they are: `mark:<id>` or
`fingerprint:<sha256>`.

---

## 6. The device reads it, later

**D1. Read-only, and additive.** The device may read `k7.card` at mount and report it in
`status` as `mark` (the id, or null). It never writes, repairs or deletes a mark. The global
premise stands: the only card write the device performs is deleting the Update file it has
already flashed.

**D2. What it buys.** Connected mode can say whether the card in the player is the card the App
prepared, which nothing can answer today: `status` reports a track count and a generation, both
of which a different card can reproduce.

**D3. What it does not buy.** No authorisation, no pairing, no lock. A card is whatever is in
the slot.

---

## 7. The rename's shims

The card's marks are new, so nothing has to be tolerated there. The **Update file** predates
the rename and does:

**R1.** The device's card reader tries `/k7.db` and then `/xtape.db`. The App writes `k7.db`
only. The App's card writer is the only thing that has ever written `xtape.db`, so the shim is
one release long and one `stat` wide.

---

## 8. Tests

**T1.** The mark's schema: unknown/extra fields ignored, a bad `id` rejected, a newer `v`
read-only.

**T2.** The write policy table in §4, one case per row, including "never overwrite" and
"re-mark is explicit".

**T3.** The fingerprint is stable across two walks of an unchanged card and changes when one
file's size or mtime changes.

**T4.** Two cards with identical paths produce different scopes and do not collide; a clone
produces the same scope and shares covers.

**T5.** The Update-file shim: a card holding only `xtape.db` still updates; a card holding both
prefers `k7.db`.
