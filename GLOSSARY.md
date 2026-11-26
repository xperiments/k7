# Glossary

The words both contexts use. A context's own vocabulary is its own file —
[`docs/specs/CONTEXT.md`](./docs/specs/CONTEXT.md) for the stream server, [`web/CONTEXT.md`](./web/CONTEXT.md)
for the K7 app — and this file exists for the overlap: one definition per shared term, and the
three words that mean different things on each side. It is a glossary, not a spec.

## Shared, defined once

**Factory pack** — The K7Arts the build ships: every `assets/k7art/` file, its label packed into
the K7's embedded Label sheet and its J-card bundled into the App. Always present, never replaced
by a User pack.

**J-card** — The paper insert of a cassette case: one printed side, 4.0625 × 4 in, scored into a
flap, a spine and a cover. The face drawn for an album with no cover picture.

**Playlist** — An ordered selection of the Library's Tracks, authored by a person, stored as one
file on the card at `k7.playlists/<id>.k7list`, and walked by the broadcast while selected.

**Update file** — `k7.db` at a card's root: the one file that carries a Snapshot into the device.
Consumed once and deleted, so its absence means "nothing to update", not "no Snapshot".

**User pack** — A K7Art pack a person published and installed into the K7's Art partition.
Additive: its sheet and J-cards join the pool behind the Factory pack's, and "Reset to factory"
removes it.

**Cover** — The picture a Track carries. The App keeps it; the player never serves a picture, only
paths and measurements, and prints it on the Card. The picture is the source, the print is the
rendering; a real Cover always wins over a J-card.

## Same word, two jobs

| Word | Stream server | K7 app |
|---|---|---|
| **Card** | the drawn cassette on the white page | the removable volume whose `music` directory is the Library |
| **Install** | putting a Panel into the composition | adding the App to a launcher |
| **Track** | any of the three accepted source formats in the library tree | an MP3 or FLAC this browser analyzes |

Written across contexts, qualify it: "the drawn Card" against "the card", "a panel Install"
against "the app Install", "a library Track" against "a Track this browser can analyze".
