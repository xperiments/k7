# 09 — The editor exports only what the packer accepts

**What to build:** the LVGL canvas builder is the producer of `label-template v1`, so its Export
must write a file the packer reads. It did not. A fresh document — the one the page opens with —
exported `fonts.caption = Archivo Narrow 600 / 18`, which the sheet has never carried, so every
design made from the default was refused at `./tools/build.sh sunton build` with "font role
caption is 'Archivo Narrow 600' 18, which assets/labels/fonts.json does not carry". Three smaller
holes were in the same path: the export wrote `"aperture": null` instead of omitting the key, it
accepted an aperture mask that was not the Card's window, and its Bleed checkbox let a prim sit
outside 748x359 — all three refused by the packer, none of them by the editor.

Make the page refuse what the sheet cannot carry, at the moment the design is made:

- **The font roles are the sheet's pairs.** The role pickers offer exactly
  [`assets/labels/fonts.json`](../../../../assets/labels/fonts.json)'s label faces —
  `Archivo Narrow 600` 24, `Barlow Condensed 700` 64, `IBM Plex Mono 500` 18 — as a face
  *and* a size, and the default caption role becomes `IBM Plex Mono 500` 18. `tplBuild` refuses
  a role that is not on the table, and `tplImport` refuses a file that names one.
- **The aperture is the Card's window.** `tplBuild` omits the key when there is no mask and
  refuses any other window; `tplImport` refuses it too.
- **No Bleed.** The print clips at 748x359 and the sheet has no such field, so the checkbox goes
  and the bounds are always checked.
- **The sample document is a design.** Its two texts are bound to `song_title` and `bpm` (they
  pointed at `title` and `value`, which are not Slots), so a first Export works.

And keep the two tables from drifting again: `python3 tools/label_pack.py --check-editor`
compares the page's `SHEET_FONTS`, its role defaults and its `SLOTS` with the packer's own, and
`make -C host check` runs it.

**Blocked by:** 03 (the export this completes).

**Status:** done, exercised in headless Chrome against the packer

- [x] A fresh document exports with no errors, and the file packs: 1 design, 5 prims, 2 slots,
      240 B, crc 0xD0925572.
- [x] Export → Open → Export is byte-identical, and the opened tab holds the same design.
- [x] A role whose face and size are not on the sheet is refused by Export and by Open, and a
      refused Open leaves no empty tab behind.
- [x] An aperture that is not `(95,101) 556x166 r83` is refused; a design with no mask exports
      without an `aperture` key.
- [x] A prim outside 748x359 is refused with its layer named, Bleed or not.
- [x] `label_pack.py --check-editor` reports a face, a default, a missing slot and an added slot
      correctly, and passes on the page as shipped.

**Files:** `resources/LVGL canvas builder.html` (`SHEET_FONTS`, `defaultTpl`, `migrateFonts`,
`tplBuild`, `tplImport`, the Template panel, the Open handler), `tools/label_pack.py`
(`check_editor`), `host/Makefile` (`editor`), `docs/specs/label-templates.md` §1-2.

**Tests:** the headless harness is a throwaway (Chrome `--dump-dom` over a copy of the page with a
probe appended); the durable check is `--check-editor`. The packer's refusals are its own test
(ticket 02), and the round trip is the page's.

**Source:** [`label-templates.md`](../../../specs/label-templates.md) §1 and §2, and the refusal
table in §7 — the packer's contract, arriving early.

## Comments

- The bug was found by exporting the page's own default document in headless Chrome and feeding
  the JSON to the packer: `font role caption is 'Archivo Narrow 600' 18, which
  assets/labels/fonts.json does not carry`. The editor's three built-in faces were right; the
  caption *size* paired with the wrong one. Nothing in the page could have caught it, because the
  page did not know the sheet's table — that is what `SHEET_FONTS` and `--check-editor` add.
- `migrateFonts` now snaps a saved document's role onto the sheet's table: the exact pair stays,
  anything else becomes the role's default. The one design this can touch is a document saved
  from the old caption default (`Archivo Narrow 600` 18), which becomes `IBM Plex Mono 500` 18.
- The round trip is exercised: a design exported, opened through the page's own Open path, and
  exported again compares `JSON.stringify`-equal, and the reopened tab re-packs to the same sheet.
- The editor's checks run before the packer's: `tplImport` throws on a file naming an off-table
  face or another window, and the Open handler rolls the new tab back so a refused file does not
  leave an empty document behind.
- `--check-editor` was proved against four mutants of the page (a face, a default, an added slot,
  a missing slot); each is named by the check, and the page as shipped passes.
