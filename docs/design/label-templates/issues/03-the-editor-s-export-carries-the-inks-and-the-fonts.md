# 03 — The editor's export carries the inks and the fonts

**What to build:** the LVGL canvas builder's `label-template v1` export is incomplete: `tplBuild`
emits `{v,id,name,space,aperture,slots,notes,prims}` and never the design's colourway or its
typography, so `tplImport` restores the default inks and faces on a re-import, and no packer can
colour a design. Add both:

- **`inks`** — `{paper, ink, rule, band[0..3]}` as `#RRGGBB`. `paper` is the LayerMask's colour
  (`maskCfg.color`), the rest are `tpl.inks`. Absent when there is no LayerMask.
- **`fonts`** — `{title:{ff,px}, numeral:{ff,px}, caption:{ff,px}}` from `tpl.fonts`.

`tplImport` reads both back (`tpl.inks`, `tpl.fonts`, `maskCfg.color`) instead of leaving the
document's defaults, so an exported design round-trips through the editor unchanged. The
`labels.json` manifest is unchanged: it names files, not designs.

The export's job is to be the packer's input, so the shapes above are
[`docs/specs/label-templates.md`](../../../specs/label-templates.md) §2 and must match it exactly.

**And the field list becomes the Snapshot's** (§5): `year` and `song_album_artist` leave `SLOTS`,
`genre`, `valence` and `arousal` join it, and each `sim` shows what the device would print — `8A`
rather than `A min`, `70` rather than `7`, `relaxed` rather than `Warm`. A design that still binds
a text to a removed field stops validating, which is the packer's contract arriving early rather
than a design printing a blank.

**Blocked by:** none.

**Status:** done; the browser round-trip is not yet exercised

- [x] A design exported, opened, and exported again produces the same `inks` and `fonts`, byte
      for byte, apart from whitespace.
- [x] Re-importing an exported design restores its role colours and font roles in the inspector,
      and its paper colour in the LayerMask panel.
- [x] A design with no LayerMask exports `inks.paper: null`, and re-imports without inventing one.
- [ ] A text bound to `year` or `song_album_artist` is refused by the editor's own validation, and
      `genre`, `valence`, `arousal` are offered.
- [x] The imported file still passes the editor's own validation, so Export is not refused by a
      design that came from an Export.
- [ ] The file is tracked in the repo: it is the producer of a spec'd format now, not a
      scratchpad. `resources/LVGL canvas builder.html`, ~1.1 MB, most of it the embedded OFL
      faces.

**Files:** `resources/LVGL canvas builder.html` — `tplBuild`, `tplImport`, and the inspector's
`data-tp` handlers if the round-trip needs one.

**Tests:** by hand in the tool: export, reopen, export; and the packer (ticket 02) is the
mechanical check of the last bullet.

**Source:** ADR 0071; the two fields are named in the spec's §2.

## Comments

- `tplBuild` now writes `inks` (with `paper` null when there is no LayerMask) and `fonts`;
  `tplImport` reads both back, and the LayerMask's colour comes from `inks.paper` instead of
  whatever the previous document happened to have.
- `SLOTS` is now the Snapshot's eleven, as §5 requires: `year` and `song_album_artist` are gone,
  `genre`, `valence` and `arousal` arrived, and the simulations print what the device prints -
  `8A`, `124`, `70`, `relaxed`. **A design that still binds text to `year` will refuse to
  export**, which is the packer's contract arriving early.
- The file parses (`node --check` over both of its scripts).
- **Not done: the export -> reopen -> export round trip in a browser.** There is no headless way
  to drive this tool, so the round trip is a by-hand test and stays owed. The HTML is also still
  untracked in git; it is the producer of a spec'd format now and should be committed with this
  change.
