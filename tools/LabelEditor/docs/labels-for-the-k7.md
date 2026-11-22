# Labels for the K7

The cassette label is the `label` section of a **K7Art**: one file that also carries the
design's **J-card**, the paper face the App draws when an album has no cover picture. This is
the guide to making one. The container is [`docs/specs/k7art.md`](../../../docs/specs/k7art.md);
the label section's contract and its binary are
[`docs/specs/label-templates.md`](../../../docs/specs/label-templates.md); the pack a user
publishes is [`docs/specs/art-packs.md`](../../../docs/specs/art-packs.md). The vocabulary the
packer and the firmware share is
[`assets/labels/vocabulary.json`](../../../assets/labels/vocabulary.json).

## What a label is for

Hold a cassette up to the K7's screen with two fingers and the print becomes that Track's
**label**. The player draws the design in the cassette's own box and fills its Slots from what
it knows about the track — title, artist, BPM, Camelot key, mood, energy. The print goes back
to the Cover when the cassette is put down, and the label is drawn only while it is held up.

So a label is one design for the whole library: a layout plus references to the player's
Slots. It has to read for a 12-character title and for a 60-character one, and for a track
whose row is missing a mood. The same K7Art's J-card is the design on paper: one file, so the
palette and the faces cannot drift between the two.

## The shape you design in

- **The print is 748 × 359 design pixels**, origin top-left. The panel's screen is 800 × 480
  and the print sits at (25,19) on it. There is no other size and no zoom to design around.
- **The Card's window is a hole in it** — (95,101) 556 × 166, radius 83. The cassette's tape
  shows through, so nothing drawn there is seen on the player. The editor draws the design
  whole; the window is worth keeping in mind while you place type.

| Zone | Where | What goes there |
|---|---|---|
| Above the window | y 0…101, the full 748 wide | the title and artist, a 78 px numeral, captions |
| Below the window | y 267…359 | the small print: album, mood, key, captions; a numeral fits too |
| Left margin | x 0…95, y 101…267 | ornaments, a vertical mark, a side letter |
| Right margin | x 651…748, y 101…267 | the same |
| The window itself | x 95…651, y 101…267 | nothing — the tape is there |

Every role fits either strip: a title is about 29 px of ink, a caption 17, and a numeral's
digits 55, so the only rule is to keep the whole line out of the hole.

A layer may **bleed** past the print — a band to the edge and further — and the print crops
it, which is how a stripe runs off a sleeve.

- **The paper** is the label's background and its clip: a rounded rect filling the print, its
  colour and radius its own. Every design has one. The J-card has its own background colour,
  independent of the label's paper.
- **Canvas background** is the cassette's body and the screen behind it while the label is up:
  up to eight colours, cycled by the Track's cassette row (`row % count`). The whole list is
  packed; the editor's active one is only the preview. A design without one leaves the Track's
  cassette row alone, and a new design starts on the panel's page grey (`#909090`). The J-card
  has no canvas: it is paper, and its sheet is its own background colour.
- **Every layer carries its own colour**, a `#RRGGBB` value: a shape's fill, a text's type, a
  J-card layer's paint. Any colour goes on any element; there is no palette to declare.
- **Three type roles, and each is a face and a size**: `title` Archivo Narrow 600 at 32 px,
  `numeral` Barlow Condensed 700 at 78 px, `caption` IBM Plex Mono 500 at 18 px. Those are the
  three faces the sheet compiles; a face or size it does not carry is refused, because the
  panel would draw a different face than the editor showed. The J-card uses the same three
  faces at its own sizes, in its units.

## The J-card, the other half

A K7Art is one-to-one: there is no label without a J-card. The J-card is the printed insert of
a cassette case — one side, **4.0625 × 4 in** — drawn as one sheet of **1300 × 1280 units at
320 units per inch**, which makes every fold line an integer.

| Panel | x | What the App does with it |
|---|---|---|
| Flap | 0…320 (1 in) | nothing yet: authored and exported, with no surface to draw it on |
| Spine | 320…480 (0.5 in) | the shelf's left face |
| Cover | 480…1300 (2.5625 in) | the case front and the album card |

- **The panels are guides, not clipping.** Art may cross a fold; the editor draws the guides at
  x = 320 and x = 480, with a focus for the sheet and for each panel. It is one printed side:
  no back, no tracklist and no print export.
- **Its texts bind to three J-card fields**, not to the label's Slots: `title` is the album's
  name, `artist` its byline (one artist, or "N artists" for a compilation), and `meta` the line
  the App passes — `Side A · Side B` by default, the album's name on a Track's case. Its three
  sizes are its own; the defaults are 56, 136 and 31 units, and the editor's simulations are
  `Neon Skyline`, `The Analog Sons` and `Side A · Side B`.
- **The cover has a safe square.** The App's album card is square, so the central 820 × 820 of
  the cover — x 480…1300, y 230…1050 — is what the card shows. Keep the words inside it; the
  rest is the case's portrait face. The cover part also fills the case front, and the spine
  part fills the shelf's left face, both at their own sizes.
- **A real cover picture still wins.** The J-card is the face of an album that has none. The
  flap has no surface yet; a later view — the case's back, or the unfolded sheet — is where it
  would show.

## What the player fills

Eleven Slots, resolved from the track's snapshot when the track changes. A Slot the track
has no value for prints empty — the shapes and the background stay, the words go.

| Slot | Prints | Example |
|---|---|---|
| `song_title` | the title | Neon Skyline |
| `song_artist` | the artist | The Analog Sons |
| `song_album` | the album | Night Drive |
| `genre` | the genre | Synthwave |
| `bpm` | beats per minute | 124 |
| `musical_key` | the Camelot key | 8A |
| `energy` | 0–100 | 70 |
| `danceability` | 0–100 | 62 |
| `valence` | 0–100 | 55 |
| `arousal` | 0–100 | 48 |
| `mood` | happy, sad, aggressive, relaxed or party | relaxed |

A value is cut at 96 characters and then wrapped: a text's width is its wrap width, so a long
title grows downward inside its box and the print crops what runs past the edge. One design
holds up to 48 layers and 16 Slots; the sheet holds up to 32 designs. The J-card's three fields
are the App's, not the Snapshot's, and carry no `max`.

## Two roads to a design

**By hand.** `npm run dev` (5178), or the deployed editor at **`/k7art`**, then Design: the
tool rail adds rects, squares, circles and texts, the Inspector sets a layer's colour and the
type roles, Layers keeps the order. The label surface draws the print; the J-card surface draws the
one sheet, its three guides and the safe square. The **Preview** pane draws the label with the
real LVGL build and the J-card in the DOM. **Export** writes the K7Art.

**From a brief.** [`label-prompts.md`](label-prompts.md) distils the look of a real cassette
collection — a saturated field, a hero number, a side badge, small mono print — into a prompt
shape and seven pasteable prompts. Three of them are built: mustard-and-ink *HiFi 1969*,
the navy-magenta *808 Stripes*, and the cream *Discs C-60*.

**With an agent.** The editor offers its actions as WebMCP tools, and ships a skill that says
how to use them: [`skills/k7-art/SKILL.md`](../skills/k7-art/SKILL.md).
The dev server serves the same file at `http://localhost:5178/skill.md`, the page at
`/k7art/skill.md`, and the page also answers it as a tool — `k7art-guide` — which is the road
for a session that will not fetch a route. An agent that can drive WebMCP — ChatGPT desktop
with the page open, Chrome with `--enable-features=WebMCP`, or any browser agent — can then
read the design, lay out the paper, its layers' colours and type, bind the label's Slots and the
J-card's fields, validate, and export. What to ask for is a brief, not a tool call:

> Open http://localhost:5178, read /skill.md, and make a label for a synthwave track: deep
> navy background, a blue band across the top with the title, artist and BPM, and the album, mood
> and key in the small print below the window. Give its J-card the same navy and a big BPM on
> the cover. Export the K7Art.

The human's receipt is the status bar's **WebMCP** button (Agent tools), which lists every
tool the page registered while the agent was working.

If the agent has no tools at all — a browser session that only clicks, or one that blocks the
page's routes — the label is still finishable: the skill carries a *Driving the visible
controls* walkthrough that maps every tool to the panel it lives in (rail, Inspector sections,
Layers, Export), and the canvas may open on an earlier autosaved design, so the first step is
the tab strip's **+**.

## How a design reaches the player

1. **Export** in the editor writes one **K7Art** to `assets/k7art/k7.<slug>.json`. The id is
   `k7.<slug>`, the folder is the set — every file in it is packed, in id order — and a file
   that is not a K7Art fails the build, naming itself. The editor is where an older design is
   imported and migrated into a K7Art with a starter J-card.
2. **The packer** (`make -C host label`, `tools/label_pack.py`) reads each K7Art's `label`
   section into the factory **Label sheet** (`label_sheet.bin`, version 4) and validates the
   J-card too. The sheet's bytes and version are unchanged; the J-card is not in it.
3. **The firmware** embeds the factory sheet and reads it in place from flash — no PSRAM, no
   JSON parsing — and prints a design at the two-finger pose. The App's build bundles the same
   files' `jcard` sections as the **Factory pack**'s J-cards, so an album with no cover picture
   draws one. The **Factory pack** is not a file on the device: it is the embedded sheet plus the
   App's bundled J-cards.
4. **A user's pack** is the other road: **Publish** in `/k7art` takes any subset of the open
   designs, a pack id, a name and an author, and writes one `.k7artpack` as a download — the
   binary label sheet built in the browser (checked byte for byte against `label_pack.py`),
   the designs' J-cards and the header. The App's **Settings → Art packs** page imports it and
   uploads it to the K7's `POST /api/art`; the device stores it in its `art` partition, reads
   its sheet in place and serves it read-only. One **User pack** is active at a time and it is
   additive: the factory designs and the user's are one pool, factory first. **Reset to
   factory** is `DELETE /api/art`.
5. **On the device** the label appears at the two-finger pose only, and nothing repaints it
   while it is up except a track change. The J-card is the App's; no J-card byte reaches the
   firmware.

## Before you land one

- **Validate in the editor** (`k7art-validate`, or Export's errors): id shape `k7.<slug>`, every
  layer's colour is `#RRGGBB`, the label's face-and-size pairs, a text with neither a Slot nor a
  static string, the J-card's three fields and its safe square, 48 layers and 16 Slots, a
  colour on every shape.
- **Look at it in Preview**: the label is the same LVGL build the panel runs; the J-card is the
  DOM render with the safe-square guide.
- **Pack it**: `make -C host label` refuses what the packer refuses, by name, and prints the
  sheet's size (`label_pack: N designs, …`). A user's pack is Publish's, installed from the
  App's Art packs page.
- **On the board, the pose is the test**: hold the cassette up, then change track and watch
  the fields swap. An album with no picture is where the J-card shows.

## Where the truth lives

| | |
|---|---|
| The container | [`docs/specs/k7art.md`](../../../docs/specs/k7art.md) |
| The label section | [`docs/specs/label-templates.md`](../../../docs/specs/label-templates.md) |
| The pack | [`docs/specs/art-packs.md`](../../../docs/specs/art-packs.md) |
| The vocabulary | [`assets/labels/vocabulary.json`](../../../assets/labels/vocabulary.json) |
| The editor, its tools and the bridge | [`README.md`](../README.md) |
| The agent skill | [`skills/k7-art/SKILL.md`](../skills/k7-art/SKILL.md), served at `/k7art/skill.md` |
| The editor page | `/k7art` |
