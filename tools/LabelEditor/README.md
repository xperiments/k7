# Label Editor

The cassette label's design tool: draw the print at its one real size (748 × 359 at 25, 19 on
the panel's 800 × 480 screen), preview it with the real LVGL build, and export
`label-template v1` — the file `assets/labels/templates/` takes.

It is the professional rebuild of the one-page `resources/LVGL canvas builder.html`: same
document model, same template JSON, same exports, on React and the ReUI design system.

![The editor in Design: layers, the print on the panel, and the inspector](docs/design.png)

![Preview: the same design drawn by LVGL, with the test values beside it](docs/preview.png)

```bash
cd tools/LabelEditor
npm install
npm run dev        # http://localhost:5178
npm test           # the template and zip engines
npm run e2e        # Playwright, in the installed Chrome
npm run typecheck
npm run build      # dist/
```

## What it does

- **One canvas, the panel's geometry.** The print sits where the device prints it, the
  pasteboard around it is where a layer may bleed, and every layer outside the print is marked.
- **Documents.** Several designs open at once, autosaved to `localStorage`. Documents made in
  the one-page builder (`lvgl-canvas.docs.v1`) are migrated on first load.
- **Import.** The **Import** button, `⌘O`, the Export menu's *Import a JSON file*, or a `.json`
  dropped anywhere on the window: a `label-template v1` design or an editor document opens in
  a new tab. Anything the packer would refuse is refused on the way in, with the reason, and
  leaves no half-open tab behind.
- **The label model.** Colour roles (paper, ink, eight bands, rule), three type roles, slots
  and their test values; groups and the LayerMask, which clips to a rounded print.
- **An inspector that gives the open section the whole column.** Sections open one at a time
  (click the open header to close it), so no section is half-hidden behind a scrollbar; the
  inspector itself collapses to a rail and remembers that across reloads. An empty static
  text says so — a placeholder and a line under the field, and an `empty` badge in Layers.

  ![An empty static text: the field says what it prints, and a line says why nothing does](docs/empty-text.png)
- **Real preview.** LVGL 9.6 compiled to WebAssembly (`public/lvgl.js`), the same build the
  panel runs, rendering the design and the aperture.
- **Export.** One `label-template v1` file, or every open design as a zip with a PNG preview
  and `labels.json`. Export refuses what the packer would refuse, with the reason on screen.

## How it is built

```
src/engine/     the model and every operation: one Editor store, no React
src/components/ the views: app bar, tool rail, layers, canvas, inspector, dialogs
src/engine/tpl.ts   the template JSON: build (one view of a document) and import (its inverse)
src/engine/editor.ts  documents, undo, selection, gestures, masks, groups, the LVGL preview
public/lvgl.js  the emscripten build, with its wasm inlined as a data URI
public/fonts/   the three faces the sheet compiles, from assets/fonts/
```

The `Editor` is one external store (`useSyncExternalStore`); a view calls a method and the
whole app re-renders. That is why the canvas, the layer list and the inspector cannot disagree.

The chrome is sized in rem from a 125% root (`src/index.css`), so type, controls, spacing and
panel widths scale together. The label's own type is design pixels (inline `px` on the stage)
and never scales with the browser's text size.

The render is React; the geometry is the document's own pixels. A layer is a `<div>` at its
design coordinates inside a stage scaled by the zoom, which is also why the pointer maths is
`(clientX − stage.left) / zoom`.

## Coupling to the repository

- **The vocabulary.** `src/engine/vocab.generated.ts` is a copy of `assets/labels/vocabulary.json`
  inside the generated markers. The one-page builder's copy is still the one
  `python3 tools/label_pack.py --check-vocabulary` reads, and `make -C host label` still boots
  `resources/LVGL canvas builder.html`. **Retiring that page means:** repointing `EDITOR` in
  `tools/label_pack.py` (the block markers and the block's `const VOCAB` shape), repointing the
  boot check in `host/Makefile` and `host/tests/label_boot_tests.py`, and updating the paths
  named in `docs/specs/label-templates.md`. The fonts are no longer embedded as base64 in the
  page — they are files in `public/fonts/` — so the packer's font-hash check needs that change
  too. None of it is done here on purpose: it edits another lane's files.
- **The file format is unchanged.** `tplBuild`/`tplImport` are a faithful port; exports were
  compared byte for byte against the one-page builder for a default design and for a complex
  design with an aperture, a rotated rect, a static text and two slots.

## Browser tests and connecting

`npm run e2e` drives the Chrome already on the machine (`channel: "chrome"` in
`playwright.config.ts`, so there is no browser download) and starts `npm run dev` itself.
`e2e/editor.spec.ts` covers the boot, the inspector's one-section-at-a-time behaviour (and
that the open one cannot be closed), collapsing the inspector with the state surviving a
reload, importing a template, and grouping two layers. `npm run e2e:ui` opens the
interactive runner.

To look at a browser you are already using, give that Chrome a debugging port:

```bash
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
  --remote-debugging-port=9222 \
  --user-data-dir=/tmp/k7-debug \
  http://localhost:5178

npm run connect     # in another terminal: prints the state, writes docs/connected.png
```

`connect` prints the viewport, the root font size, whether the inspector is collapsed (and
its stored flag), the open documents, the zoom, and each inspector section's open state, then
takes a screenshot. `window.__editor` is exposed in development, so the same script can read
any editor state you want to add.

## Keyboard

`V` `R` `S` `O` `T` tools · `⌘Z` / `⇧⌘Z` undo/redo · `⌘C` / `⌘V` copy/paste ·
`⌘G` / `⇧⌘G` group/ungroup · `⌫` delete · arrows nudge (⇧ = 10 px) · `⌘S` save document ·
`⌘E` export template · `⌘O` import a JSON file · `⌘↵` preview/design · `?` the sheet.

## Known limits

- The desktop layout targets 1280 px and up. Below `xl` the Layers panel is hidden and below
  `lg` the Inspector is too; the canvas still pans and zooms.
- The layer tree's drag-and-drop is the builder's own run-of-the-array model, not ReUI's
  `sortable`: a group and the LayerMask must stay one contiguous run, and a generic sortable
  would allow a nesting the file format cannot express.
- Uploaded fonts preview in the DOM but LVGL only draws a host font once it has been registered
  with the module, which happens on upload; a font the sheet does not compile cannot be
  exported (Export says so).
- A template that predates the eight-band colourway re-exports with its band array padded to
  eight, exactly as the one-page builder did: the panel needs a colour for every band. The
  designs that already carry eight (`label.neon`, `label.retro`) round-trip byte for byte.
