# Deploying K7

**Goal.** One artifact from one repository, three documents at `https://k7.xperiments.dev`: the
**Site** at `/`, the **App** at `/app` ([`k7-site.md`](./k7-site.md)), and the **K7Art Editor** at
`/k7art` ([ADR 0112](../adr/0112-the-k7art-editor-is-the-sites-k7art-page.md)). The device serves
no page of any of them — it answers the API and the stream, plus `/setup` for a device that has
no network yet, and the App reaches it directly over the local network once a person grants the
`local-network` permission (ADR 0067). A development origin,
`https://k7-dev.xperiments.dev:8000`, exists so connected mode can be tested against the same
shape it ships in — real TLS, no warning, a genuine secure context. It has one documented
exception: the origin is on the LAN, so Chrome's `local-network` permission never gates it, and
its build mounts without the gate (§4 D12; [ADR 0082](../adr/0082-the-development-origin-is-trusted-by-a-development-build.md)).

**Status:** deployed. `https://k7.xperiments.dev/` serves the Site and `/app` the App, on Vercel
with its certificate, and the Site serves the released 0.6.2 parts for both boards. §7 records
what the origin answered when it was checked, and when. Not proven: A13's installability has not
been re-signed on the real origin, and no board has been flashed from the browser
([`k7-site.md`](./k7-site.md) §6).

---

## 1. Production: Vercel

**D1. Project settings.** The Vercel project's root directory is the **repository root** — the
editor lives outside `web/` — with build command `npm run build` (the root script builds `web/`
and the editor) and output `web/dist`. The build emits two documents: `index.html`, the Site, and
`app.html`, the App. It also builds the editor (`tools/LabelEditor/`, its own `npm ci` and
`npm run build`, `base: "/k7art/"`) and copies its `dist/` to `web/dist/k7art/`. One project, one
artifact, two packages. Keeping the root at `web` and relying on Vercel's "include source files
outside the root directory" is not taken: the repository root is what both packages need.

**D1a. One rewrite serves the App at `/app`, and one directory serves the editor.**
`/app` → `/app.html`, with the browser's address bar keeping `/app`; the development origin
answers the same path through a middleware (k7-site.md D2). `/k7art/` is served from
`k7art/index.html` and its base-relative assets; nothing else is rewritten: the App has no server
routes, only hash paths, and the editor has none at all.

**D2. Domains.** `k7.xperiments.dev` is added to the project; Vercel issues and renews the
certificate. `www.xperiments.dev` stays where it is — a different project, unaffected.

**D3. Previews are standalone-only.** Every Vercel preview is its own origin, so a preview can
never inherit the production origin's `local-network` grant and connected mode cannot be
exercised there. That is correct: a preview URL must never be able to command a player.

---

## 2. Headers, which is why Vercel and not Pages

GitHub Pages cannot set response headers at all, so it cannot do D4, D5 or D6. Vercel's
`vercel.json`:

```json
{
  "headers": [
    {
      "source": "/assets/(.*)",
      "headers": [{ "key": "Cache-Control", "value": "public, max-age=31536000, immutable" }]
    },
    {
      "source": "/(sw.js|index|index.html|app|app.html|k7art|k7art/index.html|manifest.webmanifest|shell.json|weights.lock.json|models.lock.json)",
      "headers": [{ "key": "Cache-Control", "value": "no-cache" }]
    },
    {
      "source": "/(.*)",
      "headers": [
        { "key": "X-Content-Type-Options", "value": "nosniff" },
        { "key": "Referrer-Policy", "value": "no-referrer" },
        { "key": "Permissions-Policy", "value": "geolocation=(), microphone=(), camera=()" },
        { "key": "Content-Security-Policy", "value": "default-src 'self'; script-src 'self' 'wasm-unsafe-eval' blob:; worker-src 'self' blob:; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; connect-src 'self' https: data: blob:; font-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'" }
      ]
    }
  ]
}
```

**D4. `no-cache` on the worker and the entries is the update mechanism.** The list names
`/app` as well as `/app.html`: a rewrite keeps the request path, and a header rule is matched
against that path, so the document's own address needs the entry. `/k7art` and
`/k7art/index.html` are named the same way: the editor is a tool whose Publish writes a pack, and
a cached copy of it is a cached packer. `sw.js` deliberately does not `skipWaiting`; the browser
must revalidate it on every navigation, or a new version waits until the CDN's TTL expires.
`index.html`, `app.html`, the manifest and the lock files are the same fact. The Site is the page
that names a firmware version and offers its flash; a cached copy of it offers a version the
origin may no longer serve.

**D4a. The manifest must be served as JSON, not as an opaque file.**
`/manifest.webmanifest` carries `application/manifest+json` (Vercel's default for the
extension, and `content_type_for` on the device already maps it), because a manifest with the
wrong content type is a manifest Chrome does not read — and an app Chrome will not install.

**D5. `immutable` on `/assets/` and `/k7art/assets/`** is safe because the filenames are
content-hashed, and it is the same rule the device already applies in `cache_control_for`. The
editor's `public` files are not hashed — `/k7art/fonts/`, `/k7art/lvgl.js`, `/k7art/skill.md` —
so they carry a short `max-age` instead, never `immutable`.

**D6. The CSP is the defence against injected script.** The App holds no secret, so the policy is
not a key's keeper; it is what stops script that got in from reaching further. Notes on the two
entries that look odd and are not:
- `script-src blob:` — Essentia, ORT and TagLib are fetched as text, verified by hash and
  imported from a blob URL; that is the integrity design, not a hole.
- `connect-src https:` — the **Invidious Instance** is a setting and the weights come from a CDN,
  so no list can be enumerated ahead of time. A tighter list would break every self-hosted
  Instance.
- `img-src https:` — for the same reason, from the other side: a Video's thumbnail comes from
  the **Invidious Instance** the user chose, which is a hostname no build can know. A picture
  is not code; the policy that matters is `script-src`, and it is unchanged.
- **The frame sends a referrer of its own.** D6 also sets `Referrer-Policy: no-referrer` on
  every response, and YouTube's player refuses a frame that arrives with no referrer — it shows
  **error 153**, `embedder.identity.missing.referrer`. So the iframe carries
  `referrerpolicy="origin"`: the element's policy overrides the document's, and `https://k7.
  xperiments.dev/` is all the player learns. Nothing else on the origin sends a referrer.
- `frame-src https://www.youtube-nocookie.com https://www.youtube.com` — exactly one frame,
  named twice: the inline player on `#/media/video/:id`. It is not the user's Invidious
  Instance, because a header cannot name a hostname that is a setting; framing the Instance
  would need `frame-src https:`, and an Instance that refuses framing shows a blank player
  with no way to say why. The no-cookie host is the reliable one, and the page loads it only
  on a click.

**D7. No `COOP` header, yet.** `Cross-Origin-Opener-Policy: same-origin` would have severed
`window.opener` and killed the bridge; ADR 0067 removed the bridge, so that reason is gone. It is
still unset because turning it on makes the document cross-origin isolated, which changes what
the CDN's weights may load (`COEP`) — a separate, deliberate decision, not a "security headers"
sweep.

---

## 3. DNS

**D8. Production.** `k7` → the record Vercel asks for, proxy as Vercel recommends.

**D9. Development.** `k7-dev` → an `A` record at `192.168.0.38`, **DNS only** (no proxy). A
proxy cannot reach a LAN address, and the certificate comes from mkcert, not from the edge.

---

## 4. The development origin

**D10. One certificate, trusted once per machine.**

```sh
brew install mkcert nss
mkcert -install
mkcert -cert-file web/.certs/k7-dev.pem -key-file web/.certs/k7-dev-key.pem \
  k7-dev.xperiments.dev 192.168.0.38 localhost 127.0.0.1
```

`.certs/` is gitignored. The same pair serves `npm run dev` and `vite preview` at the
development origin.

**D11. The port is part of the origin.** `https://k7-dev.xperiments.dev:8000` is the App's
development origin, in full, including `:8000`. Serving the same certificate on another port
makes another origin — another CORS origin for the device, and another name in D12a.

**D12. Connected development is the development build, not the preview.** `npm run dev` on the
development origin. Its build mounts without the `local-network` gate, because the origin is a
LAN page and the permission does not gate local→local traffic (LNA §2.2; k7-app.md A3). `vite
preview` serves the shipping artifact itself — worker, headers and the gate — and is for
checking that shape, not for connected work: from the development origin it cannot answer the
gate.

**D12a. The development firmware trusts the development origin.** `./tools/build.sh sunton
build flash` — the panel's test build — compiles with `CONFIG_K7_TRUST_DEV_ORIGIN=y`
(`sdkconfig.defaults.sunton`), so the device's CORS reply and its MCP guard accept both
`https://k7.xperiments.dev` and `https://k7-dev.xperiments.dev:8000`, naming the request's own
origin. Only the release leaves the option out: `./tools/build.sh sunton release` rebuilds from
`sdkconfig.defaults.release`, so its binary carries the deployed origin alone. The `dev` variant
is kept for a build that names the option itself. Recorded in
[ADR 0082](../adr/0082-the-development-origin-is-trusted-by-a-development-build.md).

---

## 5. The one artifact

| | Command | What it bakes in |
|---|---|---|
| The Site and the App | `npm run build` in `web/` | trusts `http://k7.local`; the `local-network` permission is asked for at first render; the Site's firmware version and hashes come from `firmware.json`; the parts under `public/firmware/<version>/` ship with the deploy |
| The K7Art Editor (the same deploy) | `npm run build` in `tools/LabelEditor/`, copied under `web/dist/k7art/` | the `/k7art/` base; the browser packer and its parity with `label_pack.py`; no device access, no network |

**D13. There is no device artifact.** ADR 0067 removed the Launcher and the Shell: the firmware
packs no page of the Site, the App or the editor, `/` and `/bridge` answer 302 to the App at
`/app`, and the App at `/app` and the editor at `/k7art` are the UI a browser is sent to. The one
page the firmware serves is `GET /setup`
([ADR 0076](../adr/0076-the-network-is-chosen-on-the-devices-setup-network.md)).

**D14. A shipping build refuses a development origin.** The deployed App artifact fails the check
if the development origin's string appears anywhere in `dist`; the editor's artifact, under
`dist/k7art/`, fails it the same way — its built `skill.md` names `/k7art`, not the development
server at `:5178`. The release firmware compiles without `CONFIG_K7_TRUST_DEV_ORIGIN` (D12a), so
its binary carries no development origin either.

**D15. A shipping build refuses a Flash it cannot serve.** The Site may name only a firmware
version whose parts are present in `web/public/firmware/<version>/`, and every part's size and
sha256 must match `firmware.json` (k7-site.md R4). A deploy that forgets the binaries fails
before it can offer a button that 404s.

---

## 6. CI

Three jobs, all on a pull request:

| Job | What it runs |
|---|---|
| `web` | `npm ci`, `npm run typecheck`, `npm test`, `npm run build`, the artifact guard (D14), the firmware manifest guard (D15, k7-site.md R4), the worker shell check (k7-site.md D3), and a manifest shape check — `id`, `name`/`short_name`, `start_url: "/app"`, `scope: "/app"`, `display`, `theme_color`, and 192/512/512-maskable icons — so A9 cannot regress silently; plus the editor's build and tests: `npm ci`, `npm test` and `npm run build` in `tools/LabelEditor/`, its sheet writer against `label_pack.py` for every `assets/k7art/*.json`, and its `/k7art` boot |
| `host` | `make -C host check` — the device's own fixtures, including the two Update-file names |
| `e2e` | documented but commented until a Playwright dependency lands: one status round trip from the App's origin to `http://k7.local` with the `local-network` permission granted |

**D16. The firmware build stays a local step.** It needs ESP-IDF; CI proves the artifact, not
the flash.

---

## 7. First run, in order

1. `k7.xperiments.dev/` serves the Site, `/app` the App and `/k7art` the editor; `curl -sI` shows
   no `COOP`, `no-cache` on `/app`, `/app.html`, `/k7art`, `/k7art/index.html`, `/index.html` and
   `/sw.js`, Vercel's `max-age=0, must-revalidate` on `/` (D4's list names the files, and the root
   path revalidates before every use anyway), `immutable` on `/assets/` and `/k7art/assets/`, and
   `application/manifest+json` on the manifest.
2. The Site offers the flash only in Chrome or Edge on a computer, and the install only where
   `local-network` can answer; Safari and Firefox get a sentence and the manual flash path.
3. The App reports `isSecureContext: true` and a registered worker, and Ask reports the built-in
   model's state.
4. It installs from either document: Chrome shows the omnibox install icon on the first visit
   after the engagement heuristic, the **Install K7** control does the same in one click, and the
   result opens at `/app` in its own window with no address bar. The 512 maskable icon is not
   cropped.
5. The worker serves `/app` with the network off; a navigation to `/` goes to the network.
6. The Site flashes a bare board from this origin, naming a version that `firmware.json` carries
   and Chrome's own Web Serial picker chose (k7-site.md B1).
7. `k7.local` serves the API, not a page — its root redirects a browser to `/app#/player`. The
   App asks for the `local-network` permission and, once granted, the chip says connected.
8. A card is picked, marked, measured, and `k7.db` written; the device flashes it and restarts.
9. Deny the permission and reload: the gate blocks every area and says how to restore it.
10. Open the App in a browser with no LNA API (Safari, Firefox): the same gate, naming Chrome.

**Checked on the real origin, 2026-11-20**, with `curl`:

| What | Answer |
|---|---|
| The two documents | `/` answers 200 `text/html`, `<title>K7 — your music, on a cassette</title>`; `/app` answers 200, `<title>K7</title>` |
| The edge and the certificate | `server: Vercel`, `strict-transport-security: max-age=63072000`, HTTPS with no warning |
| The headers D4, D4a and D5 ask for | `no-cache` on `/app`, `/app.html`, `/index.html`, `/sw.js` and the manifest; `public, max-age=31536000, immutable` on `/assets/…`; `application/manifest+json` on `/manifest.webmanifest` |
| The worker and the manifest | `/sw.js` 200, `/manifest.webmanifest` 200 |
| The parts the Site offers | all six at 0.6.2 answer 200 at the sizes [`k7-site.md`](./k7-site.md) F3 lists: the K7's 21 120 B, 3 072 B and 2 111 536 B, the player's 26 176 B, 3 072 B and 891 712 B |

**Not recorded:** §7's browser steps — items 2–5 and 8–10 — need a person at a browser, and are
not part of this check. A13's installability has not been re-signed, and no board has been flashed
from the browser.

---

## 8. Rollback

**D17. Vercel's previous deployment is the rollback**, and it is one reload away because the
worker waits instead of replacing the page. There is nothing on the device to downgrade for the
App — the App is the deployed thing.
