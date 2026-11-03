# Deploying K7

**Goal.** Two artifacts from one repository: the **App** at `https://k7.xperiments.dev`, and the
**Shell** packed into the firmware so the device can serve the **Launcher** and the Device copy.
A development origin, `https://k7-dev.xperiments.dev:8000`, exists so connected mode can be
tested against the same shape it ships in — real TLS, no warning, a genuine secure context.

**Status:** the config, the two checkers and the CI workflow exist; nothing has been deployed,
no Vercel project points at it, and no certificate or DNS record has been created.

---

## 1. Production: Vercel

**D1. Project settings.** Root directory `web`, build command `npm run build`, output `dist`.
`build:shell` is the device's artifact and is never the Vercel build.

**D2. Domains.** `k7.xperiments.dev` is added to the project; Vercel issues and renews the
certificate. `www.xperiments.dev` stays where it is — a different project, unaffected.

**D3. Previews are standalone-only.** Every Vercel preview is its own origin, and the
Launcher's allowlist does not contain it, so connected mode cannot be exercised on a preview.
That is correct: a preview URL must never be able to command a player.

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
      "source": "/(sw.js|index.html|manifest.webmanifest|shell.json|weights.lock.json|models.lock.json)",
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

**D4. `no-cache` on the worker and the entry is the update mechanism.** `sw.js` deliberately
does not `skipWaiting`; the browser must revalidate it on every navigation, or a new version
waits until the CDN's TTL expires. `index.html`, the manifest and the two lock files are the
same fact.

**D4a. The manifest must be served as JSON, not as an opaque file.**
`/manifest.webmanifest` carries `application/manifest+json` (Vercel's default for the
extension, and `content_type_for` on the device already maps it), because a manifest with the
wrong content type is a manifest Chrome does not read — and an app Chrome will not install.

**D5. `immutable` on `/assets/`** is safe because the filenames are content-hashed, and it is
the same rule the device already applies in `cache_control_for`.

**D6. The CSP is the App's only protection for the Provider key**, which lives in this origin's
storage. Notes on the two entries that look odd and are not:
- `script-src blob:` — Essentia, ORT and TagLib are fetched as text, verified by hash and
  imported from a blob URL; that is the integrity design, not a hole.
- `connect-src https:` — the **Provider**'s base URL is configured by the user, so no list can
  be enumerated ahead of time. A tighter list would break every self-hosted endpoint.
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

**D7. No `COOP` header, ever.** `Cross-Origin-Opener-Policy: same-origin` would sever
`window.opener` and silently kill connected mode; `same-origin-allow-popups` would do the same
from the App's side. The App's isolation is the one capability this design trades away
(ADR 0058). A future "security headers" sweep must not add them back; the Playwright test in
`bridge-protocol.md` §T2 exists to catch it.

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

`.certs/` is gitignored. The same pair serves `vite preview` and the App at the address the
Launcher's development allowlist contains.

**D11. The port is part of the origin.** `https://k7-dev.xperiments.dev:8000` is what the
Launcher is built to open, in full, including `:8000`. Serving the same certificate on another
port makes another origin and needs another allowlist entry.

**D12. Serve the built App, not the dev server.** `vite preview --port 8000 --host` with
`preview.https` set from `.certs/`, so the artifact being tested is the artifact being shipped's
shape: worker, headers and all.

---

## 5. The two artifacts

| | Command | Origin baked in |
|---|---|---|
| HTTPS artifact (Vercel) | `npm run build` | the App has no launcher baked in; it trusts `http://k7.local` |
| Device artifact (Shell) | `K7_APP_ORIGINS=https://k7.xperiments.dev npm run build:shell` | the Launcher's allowlist |
| Development Shell | `K7_APP_ORIGINS=https://k7.xperiments.dev,https://k7-dev.xperiments.dev:8000 npm run build:shell` | two entries, one of them a warning |

**D13. The Launcher's allowlist is written at build** from `K7_APP_ORIGINS`, as
`launcher-origins.js` beside the Launcher. **The first entry is the one *Launch K7* opens**;
the rest are trusted to message the bridge. A development Shell therefore lists the
development origin first, and the shipping origin second if it needs to. It is a file rather than an inline script so the
Launcher keeps `default-src 'self'` with no `'unsafe-inline'`; the hosted build's copy holds
the production origin and the Shell build replaces the file. Nothing reads an origin at
runtime.

**D14. A shipping build refuses a development origin.** Two guards, because this is the one
mistake in the design with a security consequence:
- the web build fails if `K7_APP_ORIGINS` contains anything but `https://k7.xperiments.dev`
  while `K7_REQUIRE_SHELL=1` is set;
- the deployed artifact fails the check if the development origin's string appears anywhere in
  `dist`.

**D15. `VITE_LOCAL_PROVIDER=0` for the Shell build.** WebGPU needs a secure context, so the
Device copy can never run the local Provider; shipping `web-llm` there would be dead bytes in
flash.

---

## 6. CI

Four jobs, all on a pull request:

| Job | What it runs |
|---|---|
| `web` | `npm ci`, `npm run typecheck`, `npm test`, `npm run build`, the artifact guard (D14), and a manifest shape check — `id`, `name`/`short_name`, `start_url`, `scope`, `display`, `theme_color`, and 192/512/512-maskable icons — so A9 cannot regress silently |
| `e2e` | `npx playwright install --with-deps chromium`, then the two-origin suite; the App is served at `http://localhost:<port>` so no certificate is needed (`bridge-protocol.md` §T3) |
| `host` | `make -C host check` — the device's own fixtures, including `/app` mapping and the two Update-file names |
| `shell` | `K7_REQUIRE_SHELL=1 npm run build:shell` with a clean `dist-shell`, so a missing entry is a failure rather than a 503 nobody noticed |

**D16. The firmware build stays a local step.** It needs ESP-IDF; CI proves the artifact, not
the flash.

---

## 7. First run, in order

1. `k7.xperiments.dev` serves the App; `curl -sI` shows no `COOP`, `no-cache` on `/sw.js`,
   `immutable` on `/assets/`.
2. The App reports `isSecureContext: true`, a registered worker and `navigator.gpu`.
3. It installs: Chrome shows the omnibox install icon on the first visit after the engagement
   heuristic, the App's own **Install K7** button does the same in one click, and the result
   opens in its own window with no address bar. The 512 maskable icon is not cropped.
4. The worker serves a reload with the network off.
5. `k7.local` serves the Launcher; Launch opens the App in `k7-app`; the chip says connected.
6. A card is picked, marked, measured, and `k7.db` written; the device flashes it and restarts.
7. Close the bridge window: the App degrades to standalone and Connect restores it.
8. Open `k7.local` with no Launcher at all: Connect opens the bridge from the App's side.

---

## 8. Rollback

**D17. Vercel's previous deployment is the rollback**, and it is one reload away because the
worker waits instead of replacing the page. There is no version to downgrade on the device for
the App — the App is the deployed thing; only the firmware holds the Launcher.
