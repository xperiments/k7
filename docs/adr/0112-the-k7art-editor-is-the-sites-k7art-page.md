# The K7Art editor is the Site's `/k7art` page

The editor is a repo tool: `tools/LabelEditor/`, `npm run dev` at 5178, built by
`make -C host label` and booted in headless Chrome. Only someone with the checkout can use it.
The request is that it be accessible from **`k7.xperiments.dev/k7art`** - the Site's origin,
next to the Site at `/` and the App at `/app`.

That makes it a public, static page. Two consequences follow. A pack's label sheet cannot be
built by the Python packer at request time, so the browser builds it (ADR 0111's TypeScript
packer). And the editor's WebMCP tools, its `skill.md`, its LVGL preview and its local
documents become properties of a page anyone can open, served from the App's origin.

## Decision

- **The editor is the Site's third document**: the Site at `/`, the App at `/app`, the
  **K7Art editor** at `/k7art`. It keeps its guide at `/k7art/skill.md`.
- **It is built statically** from `tools/LabelEditor/` with Vite's `base: "/k7art/"`; the
  Site's build copies its `dist/` under `/k7art/`, and the deploy serves it from the same
  Vercel project and origin. Its fonts, `lvgl.js` and skill are under that base; no absolute
  `/` paths.
- **The pack is built in the browser** by the TypeScript packer (ADR 0111); Publish writes the
  `.k7artpack` download. The editor does not talk to the K7; the App installs the pack.
- **The editor's documents are the browser's**: localStorage at the Site's origin, with
  today's autosave and import. The editor is still where a design is made; the App is still
  what reaches the device.
- **The Site links it**, and the editor's README, skill and guide name the URL.
- **The Site's look does not swallow it.** The editor is its own full-page tool with its own
  ReUI chrome, as today; the Site's shell is not wrapped around it. The K7 look governs the
  chrome, as the editor already does.

## Considered options

- **Keep the editor local-only.** No deploy, no public WebMCP, and the browser packer could be
  the Python dev server's. Rejected by the request.
- **A separate origin or subdomain (`k7art.xperiments.dev`).** Cleaner isolation and a simpler
  base, but a second deployment, certificate, DNS entry and pack-import path for a page that
  belongs to the same product.
- **A `/dev` route.** It reads as a developer page and invites the assumption that it is not
  the product's editor; `/k7art` names the object.
- **A server-side packer behind the Site.** It would avoid the TypeScript port, but the static
  Vercel project would need a function, and a pack's build would depend on a backend for a
  design the browser already has. The port with byte parity is the smaller, testable seam.
- **The editor inside the App's shell at `/app/k7art`.** It would share the App's nav and
  device link, but the editor is a full-page tool and the App's shell is a reading surface.

## Consequences

- **The Site's build and CI gain the editor**: `npm ci` and `npm run build` in
  `tools/LabelEditor/`, the `dist/` under `/k7art/`, the base path, the assets and the skill;
  the deploy's headers cover the editor's wasm and fonts. `make -C host label` still builds and
  boots the editor for the host tier.
- **The packer moves into the browser** and is checked against `label_pack.py` for every
  factory K7Art (ADR 0111). The editor's `npm test` and CI gain the parity and pack tests.
- **The editor page is public.** Its WebMCP tools are offered, its skill is served, and its
  documents live in the visitor's browser. Nothing is uploaded by the editor; a pack is a
  download the user takes to the App. The page's security is the browser's, the Site's headers
  and the editor's own no-network rule for its tools.
- **The editor's base is `/k7art/` in the build and `/` in `npm run dev`** (the 5178 server
  stays). The e2e/connect scripts and the README's URLs follow; `index.html`, the fonts,
  `lvgl.js`, `skill.md` and its route are base-relative.
- **The docs change in the same commit:** [`k7-site.md`](../specs/k7-site.md) (the third
  document, the link and the build), [`k7-deploy.md`](../specs/k7-deploy.md) (the artifact, the
  build, the headers and CI), the editor's README and skill, and the App's Art packs link. The
  Kanvas tasks carry the code.
- **The editor is still the design tool, not the K7's control surface.** The device is reached
  only by the App; a public editor that could write to a device on the visitor's LAN would be a
  second trust boundary and is not taken.
