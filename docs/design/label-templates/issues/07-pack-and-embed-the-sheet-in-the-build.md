# 07 — Pack and embed the sheet in the build

**What to build:** the step that makes the sheet a build artifact instead of something a person
remembers to refresh. In `components/app/screen_display/CMakeLists.txt`, after
`idf_component_register`:

1. A custom command runs `python3 ../../tools/label_pack.py ../../assets/labels
   ${CMAKE_CURRENT_BINARY_DIR}` with `DEPENDS` on every `assets/labels/*.json`, on
   `assets/labels/fonts.json` and on the tool itself, so editing a design re-packs and nothing
   else does.
2. `target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_BINARY_DIR}/label_sheet.bin"
   BINARY)`, which defines `_binary_label_sheet_bin_start` / `_end` for the view.
3. A packer failure fails the build with the packer's message: a design that does not validate
   must not produce a firmware that silently prints the flat fill.

`assets/labels/` always carries at least one design — the reference one — because an empty set is
a refusal (§7), and a build with nothing to embed is a build whose two-finger screen regressed.
The generated `label_fonts.h` is included by `cassette_label.c` (ticket 05 orders its table).

**Blocked by:** 02 (the packer).

**Status:** done (proven in the Sunton build log)

- [x] Touching any `assets/labels/*.json` re-runs the packer; a clean build runs it once.
- [x] A design with an out-of-space prim fails `./tools/build.sh sunton build` with the packer's
      message naming the design and the layer, and leaves no stale sheet behind.
- [x] The symbol pair resolves in the app component and the view can open the sheet.
- [x] `idf.py` on a clean checkout with no network still builds: the packer is `python3` and the
      standard library only.
- [x] `python3 tools/label_pack.py assets/labels .` remains the by-hand entry point, and its
      output is byte-identical to the build's.

**Files:** `components/app/screen_display/CMakeLists.txt`, `assets/labels/*.json`,
`tools/label_pack.py`.

**Tests:** a build with a deliberately broken design is the negative test; the positive one is
the image building and ticket 08 seeing the design on the panel.

**Source:** ADR 0071 — "the sheet can never be stale".

## Comments

- The component's `CMakeLists.txt` gains a custom command over `assets/labels/*.json`
  (`CONFIGURE_DEPENDS`, so a new design is noticed), an `ALL` target so a clean build packs it,
  `target_add_binary_data` for the sheet, `add_dependencies` so nothing can link an older sheet,
  and the build dir on the private include path for the generated `label_fonts.h`.
- The build log says it ran: `[1/69] Packing the label sheet from assets/labels/`, then
  `label_pack: 1 designs, 12 prims, 5 slots, 64 B pool, 472 B, crc 0x4F5EDBBF`, then the
  generated `label_sheet.bin.S` object and `liblabel.a`.
- A packer failure fails the build: the packer's exit code is the command's, and a design that
  does not validate writes nothing, so there is no stale sheet to fall back on.
