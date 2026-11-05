# Deploying K7

**Goal.** One artifact from one repository: the **App** at `https://k7.xperiments.dev`. The
device serves no page — it answers the API and the stream, and the App reaches it directly over
the local network once a person grants the `local-network` permission (ADR 0067). A development
origin, `https://k7-dev.xperiments.dev:8000`, exists so connected mode can be tested against the
same shape it ships in — real TLS, no warning, a genuine secure context.

**Status:** the config, the two checkers and the CI workflow exist; nothing has been deployed,
no Vercel project points at it, and no certificate or DNS record has been created.

---

## 1. Production: Vercel

**D1. Project settings.** Root directory `web`, build command `npm run build`, output `dist`.

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

`.certs/` is gitignored. The same pair serves `vite preview` and the App at the development
origin.

**D11. The port is part of the origin.** `https://k7-dev.xperiments.dev:8000` is the App's
development origin, in full, including `:8000`. Serving the same certificate on another port
makes another origin, with its own `local-network` grant to give.

**D12. Serve the built App, not the dev server.** `vite preview --port 8000 --host` with
`preview.https` set from `.certs/`, so the artifact being tested is the artifact being shipped's
shape: worker, headers and all.

---

## 5. The one artifact

| | Command | What it bakes in |
|---|---|---|
| The App (Vercel) | `npm run build` | trusts `http://k7.local`; the `local-network` permission is asked for at first render |

**D13. There is no device artifact.** ADR 0067 removed the Launcher and the Shell: the firmware
packs no page, `/` and `/bridge` answer 404, and the App at its own origin is the only UI.

**D14. A shipping build refuses a development origin.** The deployed artifact fails the check if
the development origin's string appears anywhere in `dist`.

---

## 6. CI

Three jobs, all on a pull request:

| Job | What it runs |
|---|---|
| `web` | `npm ci`, `npm run typecheck`, `npm test`, `npm run build`, the artifact guard (D14), and a manifest shape check — `id`, `name`/`short_name`, `start_url`, `scope`, `display`, `theme_color`, and 192/512/512-maskable icons — so A9 cannot regress silently |
| `host` | `make -C host check` — the device's own fixtures, including the two Update-file names |
| `e2e` | documented but commented until a Playwright dependency lands: one status round trip from the App's origin to `http://k7.local` with the `local-network` permission granted |

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
5. `k7.local` serves the API, not a page — its root redirects a browser to the App. The App
   asks for the `local-network` permission and, once granted, the chip says connected.
6. A card is picked, marked, measured, and `k7.db` written; the device flashes it and restarts.
7. Deny the permission and reload: the gate blocks every area and says how to restore it.
8. Open the App in a browser with no LNA API (Safari, Firefox): the same gate, naming Chrome.

---

## 8. Rollback

**D17. Vercel's previous deployment is the rollback**, and it is one reload away because the
worker waits instead of replacing the page. There is nothing on the device to downgrade for the
App — the App is the deployed thing.
