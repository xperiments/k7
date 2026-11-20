# 01 — The origin serves two documents: the Site at `/`, the App at `/app`

**What to build:** the split in [`docs/specs/k7-site.md`](../../../specs/k7-site.md) §1 and §5 —
one artifact, two documents, one origin.

- `web/app.html` is today's `index.html`: the App's shell and its module entry. `web/index.html`
  is the Site's shell, which ticket 04 fills in; until then it may be a minimal page that says
  what K7 is and links to `/app`.
- `vite.config.ts` builds both entries; `vercel.json` rewrites `/app` → `/app.html` and keeps the
  address bar on `/app`; a dev-server middleware answers `/app` with the same document, so
  `https://k7-dev.xperiments.dev:8000/app` has the shipped shape (k7-site.md D2; k7-deploy.md
  D11–D12).
- `web/public/manifest.webmanifest`: `id: "/"` unchanged, `start_url: "/app"`, `scope: "/app"`;
  both documents link it. The registered worker and its cache list move in ticket 05.
- `front_door.c`'s fallback, the Kconfig default and `host/tests/http_tests.sh`'s expected
  `Location` become `https://k7.xperiments.dev/app#/player`. Every generated `sdkconfig` keeps the
  old string until it is reconfigured; the ticket says so where a builder will read it.
- `ui/LocalNetworkGate.tsx` gains the one link: *No player yet? Flash one*, to `/` in a new tab
  (k7-site.md S7).

**Blocked by:** none.

**Status:** pending

- [ ] The build emits `index.html` and `app.html`; `/` renders the Site and `/app` the App, in
      `npm run dev`, `npm run preview` and the Vercel deploy.
- [ ] The App's routes still work under the path: `/app#/process`, `/app#/playlists/:id`,
      `/app#/media/album/:album`; an empty hash still lands on the card-state default.
- [ ] The manifest is `id: "/"`, `start_url: "/app"`, `scope: "/app"`, and both documents link it.
- [ ] The device's `/`, `/index.html`, `/bridge` and `/app` answer 302 to `/app#/player`, and the
      host test asserts that exact string.
- [ ] The gate's link opens `/` in a new tab and leaves a standalone window for a browser.
- [ ] `npm run typecheck`, `npm test` and `make -C host check` are green.

**Files:** `web/index.html`, `web/app.html` (new), `web/vite.config.ts`, `vercel.json`,
`web/public/manifest.webmanifest`, `web/src/main.tsx` (the App entry's document),
`web/src/ui/LocalNetworkGate.tsx`, `components/portable/front_door/front_door.c`,
`components/portable/front_door/Kconfig`, `host/tests/http_tests.sh`.

**Tests:** k7-site.md T1, T2, T9; the existing web suite for the routes and the gate.

**Source:** ADR 0078.

## Comments
