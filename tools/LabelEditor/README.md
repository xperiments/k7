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
npm run e2e:webmcp # only the native WebMCP test (skips if Chrome has no API)
npm run test-label  # build a test label (HOLA) through the tool surface
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

## WebMCP: the editor's actions as agent tools

On load the editor offers its actions to the browser as WebMCP tools:
`document.modelContext.registerTool(tool, { signal })`, with `navigator.modelContext`
as the fallback older builds use. WebMCP is a Chrome 146+ preview behind
`#enable-webmcp-testing`; without it nothing is registered and the app is unchanged.

There is deliberately **no external MCP server** — nothing on stdio, nothing on HTTP. The
tools live in the page that owns the design, so an MCP client reaches them through a
browser: native WebMCP, the bridge below, or Playwright (`scripts/test-label.mjs`). A server
would have to own a page to be useful, and then two places would hold the same design.

A session that will not fetch a route still gets the guide: `label-editor-guide` returns it as
a tool result. A session that cannot call the tools at all can still make the label — the same
guide says which visible controls do what.

| Tool | What it does |
|---|---|
| `label-editor-read` | The coordinate space, fields, roles, type faces, open documents, layers, mask, groups, selection and export errors. |
| `label-editor-guide` | The guide to designing a K7 label in one result — the print and the Card's window, the fields, the workflow, the refusals, and the visible-controls path. The same file is served at `/skill.md`; this is for a session that blocks routes. |
| `label-editor-add-layer` | Add a rect, square, circle or text at a design-pixel position; a text is static unless it names a field. |
| `label-editor-update-layer` | Change one layer's position, size, rotation, text or field, role, font role, alignment, name, test value or lock. |
| `label-editor-delete-layer` | Delete a layer. |
| `label-editor-select-layer` | Select layers, so the person at the screen sees what the agent is working on. |
| `label-editor-move-layer` | Raise, lower, or send a layer to the top or bottom of the stack. |
| `label-editor-group-layers` | Group the selection, or ungroup it. |
| `label-editor-set-design` | Template id and name, background, ink, rule, the eight bands, paper and its radius, a type role's face and size. |
| `label-editor-set-field-value` | Set what a field previews (its simulated value). |
| `label-editor-import-template` | Open a `label-template v1` object (or JSON text) in a new document, with the packer's refusals. |
| `label-editor-export-template` | Return the active or a named design as `label-template v1` plus the errors that would stop an export. |
| `label-editor-validate` | Check every open design and return the reasons any would not export. |
| `label-editor-document` | New, switch, rename or close a document. |
| `label-editor-history` | Undo or redo. |

The same tools are always on the page, with or without the browser API:

```js
const design = await window.__labelEditorMcp.call("label-editor-read");
await window.__labelEditorMcp.call("label-editor-add-layer", {
  type: "text",
  text: "Side A",
  x: 40,
  y: 300,
  role: "band.6",
});
const tools = window.__labelEditorMcp.tools();   // name, description, inputSchema
const status = window.__labelEditorMcp.status(); // { available, via, registered, tools }
```

The status bar's **WebMCP** button opens *Agent tools*: the registration state, every
tool with its description, and a copy of the list as JSON.

![The Agent tools dialog, with all 15 tools registered](docs/agent-tools.png) Each call is an ordinary edit
of the open design — it updates the UI and is undoable. No tool downloads a file or calls
the network; import takes a template object and export returns one.

`e2e/webmcp.spec.ts` stubs `document.modelContext`, asserts all 15 tools register, runs
add/update/read/export through them, and checks that a refused call changes nothing. A
second test removes the API and asserts the bridge still works.

`e2e/webmcp-native.spec.ts` uses Chrome's own WebMCP instead: the `webmcp-native` project
launches Chrome with `--enable-features=WebMCP` (the feature behind the
`#enable-webmcp-testing` flag), waits for the 15 tools, and adds a text through
`document.modelContext.executeTool`. It skips on a Chrome that does not expose the API, so
`npm run e2e` stays green everywhere.

Three ways to try it by hand:

1. **No flag, the bridge.** `npm run dev`, then in the console:
   ```js
   await window.__labelEditorMcp.call("label-editor-read");
   await window.__labelEditorMcp.call("label-editor-add-layer", {
     type: "text", text: "Side A", x: 40, y: 300,
   });
   ```
   The canvas and inspector update; `label-editor-history` undoes it.
2. **The real API in this Chrome.**
   ```bash
   "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
     --enable-features=WebMCP \
     http://localhost:5178
   ```
   Open the status bar's **WebMCP** button: it should say *15 tools registered* through
   `document.modelContext`. In the console:
   ```js
   const tools = await document.modelContext.getTools();
   const tool = tools.find((t) => t.name === "label-editor-read");
   JSON.parse(await document.modelContext.executeTool(tool, {}));
   ```
   Chrome's testing API returns a tool's result as JSON text, which is why the result is
   parsed here.
3. **Automated, watching.** `npm run e2e:ui` opens the interactive runner; tick
   `webmcp.spec.ts` (the stub) or run `npm run e2e:webmcp` for the native path.

## Designing a label with an agent

The editor ships a skill for agents:
[`skills/k7-labels/SKILL.md`](skills/k7-labels/SKILL.md). It is the guide
to the K7's label — the print's box, the Card's window, the fields, the three type pairs — plus
a tool reference generated from the page's own registry. The dev server serves it at
`http://localhost:5178/skill.md`, and a build emits it as `dist/skill.md`, so an agent that can
read the page can read what its tools are for.

Give it to an agent that can drive WebMCP — ChatGPT desktop with the page open, or Chrome with
`--enable-features=WebMCP` — and describe the label you want. The agent reads the design, lays
out paper, bands and type, binds the fields, validates, and hands back the exported JSON.

`npm run skill` regenerates the skill's tool reference from `src/webmcp/tools.ts`;
`npm run skill:check` (and `src/webmcp/skill.test.ts`) refuse a stale one, so adding a tool
means running `npm run skill`. [docs/labels-for-the-k7.md](docs/labels-for-the-k7.md) is the
same ground for a person: what the label is for, the zones that read, and how a design reaches
the player.

## A test label, in one command

`npm run test-label` builds a label through the page's own tool surface — the same calls an
agent makes — validates it, and writes `label-template v1`:

- `.scratch/label.test-hola.json` — the design
- `.scratch/test-hola.png` — the print, at the current zoom

It needs the editor open (`npm run dev`) and takes `--text`, `--id`, `--name`, `--out`,
`--url`, `--head` (watch it run) and `--no-shot`. `--out -` prints the template to stdout;
point `--out` at `assets/labels/templates/label.test-hola.json` to add the design to the
sheet `make -C host label` packs. The JSON this produced packs with the real packer:
`label_pack: 1 designs, 3 prims, 0 slots, 183 B`.

The sequence, if you would rather run it from the console:

```js
await __labelEditorMcp.call("label-editor-document", { action: "new", name: "HOLA Test" });
await __labelEditorMcp.call("label-editor-set-design", {
  templateId: "label.test-hola",
  name: "HOLA Test",
  paper: "#c7a867",
  paperRadius: 12,
});
await __labelEditorMcp.call("label-editor-add-layer", {
  type: "rect", x: 0, y: 0, width: 748, height: 359, role: "band.7",
});
await __labelEditorMcp.call("label-editor-add-layer", {
  type: "text", text: "HOLA", x: 274, y: 140, width: 200,
  role: "band.6", fontRole: "numeral", align: "center",
});
await __labelEditorMcp.call("label-editor-export-template", {});
```

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
