# 10 — Eight band colours

**What to build:** raise the band palette from four colours to eight, through the design file,
the sheet, the packer, the editor and the app, without breaking a design that already carries
four — [`docs/specs/label-templates.md`](../../../../specs/label-templates.md) §2, §3 and §7.

- **The design file stays `label-template v1`.** `inks.band` is **four to eight** colours. A
  design that declares four leaves the sheet's remaining four `0`, and a prim that names
  `band.4`..`band.7` without declaring it is refused by the packer: there is no colour to print.
  The editor always exports eight.
- **The sheet becomes version 2.** The template record grows from 48 to 64 bytes: paper, ink,
  rule and eight bands occupy offsets 16..59, then four reserved bytes. A prim's `fill` stays
  paper `0`, ink `1`, rule `2`, but the bands are now `3..10` and the literal colour is `11`.
- **The reader and the app carry eight.** `LABEL_BAND_COUNT` is 8, `label_inks_t.band[8]`, and
  `cassette_label.c` paints bands 4..7 like the first four.
- **The editor is the same palette.** Its template panel shows eight band colour inputs, its
  role tables carry `band.0`..`band.7`, and its Export writes all eight. Opening a four-band
  design pads the missing four from the editor's default palette, so the controls are never
  blank.

**Status:** done

- [x] The packer's self-test covers four bands, eight bands, a three-band refusal, a nine-band
      refusal and a prim naming an undeclared band.
- [x] `label_pack.py --check-editor` fails when the page defaults fewer than eight bands or its
      role tables stop at `band.3`.
- [x] The two shipped designs still pack unchanged: 2 designs, 32 prims, 6 slots, 101 B pool,
      **1,077 B**, crc 0x2558F901.
- [x] The host `t32_label` reader test builds a 64-byte template, reads band 7 and refuses sheet
      version 1.
- [ ] The panel paints a design that uses `band.4`..`band.7` (needs the panel; the app's switch
      is the only new path).

**Files:** `components/portable/label/include/label.h`, `components/portable/label/label.c`,
`components/app/screen_display/cassette_label.c`, `tools/label_pack.py`,
`resources/LVGL canvas builder.html`, `host/tests/t32_label.c`,
`docs/specs/label-templates.md`.

**Tests:** `python3 tools/label_pack.py --selftest`, `--check-editor`, `--check` over
`assets/labels`, `make -C host check`, and the Sunton build.

**Source:** [`label-templates.md`](../../../../specs/label-templates.md) §2, §3 and §7.
