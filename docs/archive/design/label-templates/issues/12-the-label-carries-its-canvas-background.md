# 12 — The label carries its own canvas background

**What to build:** a label design defines a list of canvas background colours; the player picks
one by the Track's cassette row (`row % count`) and paints the cassette's body — the Card's
plastic and mat, the window's rim — and the 800x480 screen behind it while the two-finger pose
shows the tape, and nothing else. The editor's active colour is only its preview. It is the editor's Screen colour, carried on the design and packed
for the player. [`label-templates.md`](../../../../specs/label-templates.md) §1, §2, §3, §6, §7.

- **The file.** `label-template v1` gains `canvas: { backgrounds: ["#rrggbb", ...], active: n }`
  — one to eight presets, `active` in range. Absent means no override: the sheet carries
  `has_canvas = 0` and the player leaves the page alone. The editor writes it for every design it
  exports, starting a new design on the panel's page (`#909090`).
- **The sheet.** Version 4. The template record's old reserved byte 15 is `canvas_count`, the
  record stays 64 B, and an eight-colour row follows the template array (32 B per design). The
  packer refuses a canvas it cannot pack; the reader validates the count and the row and exposes
  the colours. The player picks `canvas[cassette_row % count]`.
- **The player.** While the two-finger pose is up, the cassette's body and the 800x480 screen
  wear `canvas[cassette_row % count]`; the pose's exit restores the Track's row, and a Track
  change while the pose is up picks by the new row and repaints. A design without a canvas leaves
  the row exactly as it is.
- **The editor.** The Canvas section lists the presets (add, select, recolour, remove; eight max);
  the design canvas and the LVGL preview use the active one; a document saved with the old single
  `bg` migrates to one preset. Export and import, and the WebMCP tools, carry the presets and the
  active one.

**Status:** in progress — the host gate and the firmware build are green; the board check is the remaining step.

- [x] The spec is updated (§1, §2, §3, §4, §6, §7, §8) and this ticket exists.
- [ ] The editor authors, stores, exports and imports the presets.
- [x] The packer, sheet v4 and reader carry the list; the host tests cover present, absent and a
      malformed canvas.
- [ ] The panel paints the row's colour at the two-finger pose and restores the row on exit.
- [ ] A design without a canvas behaves exactly as it does today.

**Files:** `docs/specs/label-templates.md`, `assets/labels/vocabulary.json` and its two generated
copies, `tools/LabelEditor/…`, `tools/label_pack.py`, `components/portable/label/…`,
`components/app/screen_display/cassette_label.*` and `cassette_view.c`,
`host/tests/t32_label.c`, `host/tests/t33_label_sheet.c`.

**Tests:** the packer's selftest; t32 and t33; `make -C host check`; the editor's `npm test`,
`typecheck`, `build` and `skill:check`; the board: hold the cassette, release it, change track.

**Source:** the board's LB-10..LB-13; `docs/specs/label-templates.md` §1 (the screen is the
design's), §2 (the file), §3 (the sheet), §6 (the rendering) and §7 (the refusals).

## Comments

2026-11-20 — the host and firmware evidence:

- `make -C host check` is green with the label boot check driven by a Chrome that can start. The
  default Chrome on this machine aborts in its GPU process (LB-09, in review, covers the skip);
  the local `CHROME=` wrapper that adds `--no-sandbox` is not part of the change.
- `make -C host test`, t32 (the reader and the `label_canvas` refusals), t33 (84 checks on the
  shipping sheet; 86 on a temp design that carries a canvas), and the packer selftest pass.
- `./tools/build.sh sunton build`: `sd_stream_server.bin` 0x202cd0, 50 % of the app partition
  free.

The board check, still to do:

1. Flash this branch's firmware (`./tools/build.sh sunton build flash`).
2. A design that carries a canvas: hold the cassette for the two-finger pose and see its body and
   the page in that colour; put it down and see the Track's row return; change track and see the
   design's canvas follow.
3. A design without a canvas: the page must stay the palette's white page.

2026-11-20, after the first board flash: the canvas painted the 800x480 screen, but the Card
covers that screen at the two-finger pose, so the viewer saw nothing. The canvas now paints the
Card's plastic, its front plate and the window's rim as well, restoring the Track's row when the
pose goes down; the screen behind them is painted with them.

2026-11-20, the row model: the first cut packed only the editor's active preset, so the K7's
cassette body never changed between the two colours. The sheet now carries the whole list
(version 4) and the player picks `canvas[cassette_row % count]`: seven colours pin one per
cassette row, a shorter list cycles. The editor's active is the preview.
