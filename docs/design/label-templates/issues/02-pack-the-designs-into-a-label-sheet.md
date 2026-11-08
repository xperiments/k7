# 02 — Pack the designs into a label sheet

**What to build:** `tools/label_pack.py` and its inputs. It reads every `assets/labels/label.*.json`
(sorted by `id`), validates each against
[`docs/specs/label-templates.md`](../../../specs/label-templates.md) §7, and writes two files:

- `label_sheet.bin` — the sheet of §3, every design, in ascending `id` order.
- `label_fonts.h` — `#define LABEL_FONT_<SLUG> <index>` per entry of `assets/labels/fonts.json`
  and `LABEL_FONT_COUNT`, so the app's `k_label_font[]` is checked by the compiler.

It is also runnable by hand (`python3 tools/label_pack.py assets/labels build/`), which is how
the editor's export is checked before a build.

**The refusals are the point.** Each error names the design and the layer, and the build fails;
nothing is dropped, rounded or recoloured. The list is §7's table — the design's `space`, the
aperture against the Card's window `(95,101,556,166,r83)`, a prim outside 748x359, an unknown
role or slot, a text that is not ink, a `preview` past its `max`, a face or size missing from
`fonts.json`, the caps (48 prims, 16 slots, 32 designs), duplicate ids, an empty directory.

`assets/labels/fonts.json` carries the three Jura faces the screen already compiles, and the
three faces the designs are drawn in — which ticket 05 converts. The table is the contract; the
app's `k_label_font[]` only exists once ticket 06 needs it, by which time every row is compiled:

```json
{ "fonts": [ { "name": "Jura", "px": 20, "symbol": "jura_20" },
             { "name": "Jura", "px": 28, "symbol": "jura_28" },
             { "name": "Jura", "px": 36, "symbol": "jura_36" } ] }
```

**Blocked by:** 03 — the packer needs `inks` and `fonts` in the design file, and today's export
carries neither.

**Status:** done (host tier: the packer, its selftest and the build step)

- [x] A design with every feature (paper, bands, rule, a static text, two slots, one rotated
      prim, one literal colour with a note) packs to a sheet whose CRC, sizes and offsets agree
      with the spec.
- [x] Each refusal in §7 is a failing exit code and a message naming design and layer.
- [x] The same input packs to the same bytes twice (designs sorted by `id`).
- [x] `label_fonts.h` matches `fonts.json` index for index.
- [x] `assets/labels/label.k7-reference.json` — a plain, readable reference design at 748x359
      with the Card's aperture — packs, and `label_pack.py --check` passes on the tree.
- [x] The tool has no third-party dependency: `python3` and the standard library only.

**Files:** `tools/label_pack.py` (new), `assets/labels/fonts.json` (new),
`assets/labels/label.k7-reference.json` (new), `README.md` (one line under the tools' list, if
the section exists).

**Tests:** a `--selftest` that packs the reference design, re-reads it with a small in-tool
reader, and asserts the refusal messages for a fixture with a bad prim and one with a missing
face. The C side is ticket 01's `t32_label`.

**Source:** ADR 0071.

## Comments

- `tools/label_pack.py` is standard library only. It reads `label.*.json` sorted by `id`,
  validates each design against §7, and writes `label_sheet.bin` plus `label_fonts.h`.
  `--check` validates and writes nothing; `--selftest` packs a synthetic design, re-reads it and
  asserts seven refusals.
- The reference design packs to **472 B: 1 design, 12 prims, 5 slots, 64 B pool, crc
  0x4F5EDBBF**, identical across runs.
- **Its own verifier caught two bugs before the build did:** the magic was seven bytes where the
  format needs eight (`K7LBL` + three NULs), and the verifier compared pool offsets as if they
  were absolute. Both fixed, and the spec's magic row corrected.
- A slot's `max` is bounded at 96 - `LABEL_VALUE_MAX` - so a design can never ask for more than
  the resolver carries and the view never has to cut a value the sheet allowed.
- Refusals were run against the real design as well as synthetic ones: a broken aperture and a
  prim outside the print both fail, write nothing, and name the design and the layer.
