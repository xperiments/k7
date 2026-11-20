# Labels for the K7

The cassette label is the one piece of design the player shows at the size it was drawn. This
is the guide to making one; the contract it lands in is
[`docs/specs/label-templates.md`](../../../docs/specs/label-templates.md), and the vocabulary
the packer and the firmware share is
[`assets/labels/vocabulary.json`](../../../assets/labels/vocabulary.json).

## What a label is for

Hold a cassette up to the K7's screen with two fingers and the print becomes that Track's
**label**. The player draws the design in the cassette's own box and fills its fields from what
it knows about the track — title, artist, BPM, Camelot key, mood, energy. The print goes back
to the Cover when the cassette is put down, and the label is drawn only while it is held up.

So a label is one design for the whole library: a layout plus references to the player's
fields. It has to read for a 12-character title and for a 60-character one, and for a track
whose row is missing a mood.

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

- **Paper** is the design's background: a rounded rect filling the print, its colour and
  radius the LayerMask's. A design without paper has no background.
- **Canvas background** is the cassette's body and the screen behind it while the label is up:
  up to eight colours, cycled by the Track's cassette row (`row % count`). The whole list is
  packed; the editor's active one is only the preview. A design without one leaves the Track's
  cassette row alone, and a new design starts on the panel's page grey (`#909090`).
- **Ink, rule and eight bands** are the colour roles. A shape paints with any role (or one
  literal hex, explained in a note); a text prints in the ink or in one of the bands, never
  on the paper.
- **Three type roles, and each is a face and a size**: `title` Archivo Narrow 600 at 32 px,
  `numeral` Barlow Condensed 700 at 78 px, `caption` IBM Plex Mono 500 at 18 px. Those are the
  three faces the sheet compiles; a face or size it does not carry is refused, because the
  panel would draw a different face than the editor showed.

## What the player fills

Eleven fields, resolved from the track's snapshot when the track changes. A field the track
has no value for prints empty — the paper, bands and rules stay, the words go.

| Field | Prints | Example |
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
holds up to 48 layers and 16 fields; the sheet holds up to 32 designs.

## Two roads to a design

**By hand.** `npm run dev` (5178), then Design: the tool rail adds rects, squares, circles and
texts, the Inspector sets the colourway and the type roles, Layers keeps the order. The
**Preview** pane draws it with the real LVGL build. **Export** writes the JSON.

**From a brief.** [`label-prompts.md`](label-prompts.md) distils the look of a real cassette
collection — a saturated field, a hero number, a side badge, small mono print — into a prompt
shape and four pasteable prompts. Two of them are built: mustard-and-ink *HiFi 1969*, and the
navy-magenta *808 Stripes*.

**With an agent.** The editor offers its actions as WebMCP tools, and ships a skill that says
how to use them: [`skills/k7-labels/SKILL.md`](../skills/k7-labels/SKILL.md).
The dev server serves the same file at `http://localhost:5178/skill.md`, and the page also
answers it as a tool — `label-editor-guide` — which is the road for a session that will not
fetch a route. An agent that can drive WebMCP — ChatGPT desktop with the page open, Chrome with
`--enable-features=WebMCP`, or any browser agent — can then read the design, lay out paper,
bands and type, bind the fields, validate, and export. What to ask for is a brief, not a tool
call:

> Open http://localhost:5178, read /skill.md, and make a label for a synthwave track: deep
> navy paper, a blue band across the top with the title, artist and BPM, and the album, mood
> and key in the small print below the window. Export the JSON.

The human's receipt is the status bar's **WebMCP** button (Agent tools), which lists every
tool the page registered while the agent was working.

If the agent has no tools at all — a browser session that only clicks, or one that blocks the
page's routes — the label is still finishable: the skill carries a *Driving the visible
controls* walkthrough that maps every tool to the panel it lives in (rail, Inspector sections,
Layers, Export), and the canvas may open on an earlier autosaved design, so the first step is
the tab strip's **+**.

## How a design reaches the player

1. The design is a file: `assets/labels/templates/label.<name>.json`. The folder is the set —
   every design in it is packed, in id order, and a file that is not a design fails the build
   naming itself.
2. `make -C host label` packs them into one **Label sheet** (`label_sheet.bin`), with the
   fonts, and checks it against the vocabulary.
3. The firmware build embeds the sheet; the player reads it in place from flash — no PSRAM, no
   JSON parsing — and addresses a design by its index.
4. On the device the label appears at the two-finger pose only. Nothing repaints it while it is
   up except a track change.

## Before you land one

- **Validate in the editor** (`label-editor-validate`, or Export's errors): id shape, the eight
  bands, the face-and-size pairs, a text with no field, 48 layers and 16 fields, paper when a
  shape needs it.
- **Look at it in Preview**, which is the same LVGL build the panel runs.
- **Pack it**: `make -C host label` refuses what the packer refuses, by name, and prints the
  sheet's size (`label_pack: N designs, …`).
- **On the board, the pose is the test**: hold the cassette up, then change track and watch
  the fields swap.

## Where the truth lives

| | |
|---|---|
| The contract | [`docs/specs/label-templates.md`](../../../docs/specs/label-templates.md) |
| The vocabulary | [`assets/labels/vocabulary.json`](../../../assets/labels/vocabulary.json) |
| The editor, its tools and the bridge | [`README.md`](../README.md) |
| The agent skill | [`skills/k7-labels/SKILL.md`](../skills/k7-labels/SKILL.md), served at `/skill.md` |
