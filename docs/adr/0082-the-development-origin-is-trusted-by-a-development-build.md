# The development origin is trusted by a development build, never by a release

[`k7-deploy.md`](../specs/k7-deploy.md) §4 gives the App a development origin,
`https://k7-dev.xperiments.dev:8000`, so connected mode can be exercised in the shape it
ships in: real TLS, a trusted certificate, a genuine secure context. It does not work, for
two separate reasons, both measured against the board at `k7.local`:

- **The player names one origin.** Its CORS reply is `Access-Control-Allow-Origin:
  https://k7.xperiments.dev`, compiled in as `K7_APP_ORIGIN`; Chrome refuses the dev
  origin's response, so no device call can be read.
- **The permission is never asked.** `k7-dev` resolves to a LAN address, and Local Network
  Access only gates a request whose target is *less public* than its initiator (LNA §2.2).
  A page served from the LAN making a LAN request is not a local network request, so Chrome
  never prompts, and `ui/LocalNetworkGate.tsx` waits on an answer that cannot come.

The App cannot be gated on a permission the browser will never ask for, and the device
cannot read a reply through an origin it does not name. Something has to give, and it has to
give only where development happens.

## Decision

- **A development firmware trusts the development origin beside the deployed one.**
  `CONFIG_K7_TRUST_DEV_ORIGIN` is a build option, off by default; `tools/build.sh dev`
  compiles the panel variant with `sdkconfig.defaults.dev`, which turns it on. The CORS
  reply then names the request's own origin when it is one of the two, and carries
  `Vary: Origin`; the MCP guard accepts either. A release is the `sunton` variant, which
  never includes the option, so a shipped binary carries no development origin at all.
- **A development App build does not mount the permission gate.** `npm run dev` builds with
  `__DEVICE_NEEDS_PERMISSION__` false, because the page it serves is on the LAN and the
  permission is not the gate there. The shipping build keeps the gate and the direct
  `http://k7.local` fetch; nothing about the deployed App changes.
- **The exceptions do not compose into a mode.** There is still one device transport, one
  link, and one origin the App trusts (`k7-app.md` L1). The differences are a compiled-in
  second origin in a development firmware and a build input in a development App; no
  runtime setting selects either.

## Considered options

- **Proxy the player through the dev server, same-origin.** No firmware change, and the dev
  App would need no permission. But it stops exercising the cross-origin path the product
  ships, and it needs a second device base plus a bypassed gate in the App — a different
  shape dressed as the same one. Rejected: the dev origin exists to test that path.
- **Trust `k7-dev` in every firmware build.** One image, one flash, no variant. It makes
  every device in the field trust a hostname the product does not control at the edge (a
  DNS-only record pointing at a LAN machine), for a need only development has. Rejected.
- **Grant the permission by hand in Chrome.** The permission is not guaranteed to be
  offered for a local origin, and a hidden settings step is not a working development
  workflow. Rejected.
- **Leave the dev origin disconnected.** It would only serve the standalone areas; connected
  mode is the reason the origin exists. Rejected.

## Consequences

- **The development board runs the development firmware.** `./tools/build.sh dev build
  flash`. The shipping App still works against it, because that build trusts both origins;
  a shipping board keeps its one origin.
- **The gate is not exercised from the development origin.** It cannot be: Chrome does not
  gate local→local traffic. The real path — the prompt, the grant, the direct fetch — stays
  proven on the deployed origin, where [ADR 0067](./0067-chrome-only-direct-to-the-device.md)
  measured it.
- **The development build is not the shipping artifact.** `vite preview` serves the shipping
  artifact and is disconnected from the dev origin; connected development is `npm run dev`.
- **The CORS reply varies by `Origin` in a development build.** `Vary: Origin` keeps a cache
  from handing one App's reply to the other. A shipping reply still names one origin and
  does not vary.
- **A second origin string exists in the source of truth only when the option is on.** The
  release's binary does not contain it, and `check-artifact.mjs` continues to refuse a
  shipping App artifact that names the development origin.

## Supersedes

None. It amends `k7-app.md` A3, L2 and L4, and `k7-deploy.md` §4.
