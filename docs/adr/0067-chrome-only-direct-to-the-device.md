# Chrome talks to the device directly; the Launcher is retired

[ADR 0058](./0058-the-bridge-is-a-typed-rpc-between-two-windows.md) opens with "the App cannot
reach the device". That was true when it was written, and it is not true any more.

Local Network Access §2.5 exempts a request from the mixed-content block when the browser knows
it is going to the local network before it resolves — and a `.local` hostname is one of the two
shapes that says so. The request is then gated on the `local-network` permission instead of the
scheme: §2.6 names it, and a grant is a person answering the prompt, not a flag.

**Measured, 2026-11-03, Chrome 156, on the deployed origin:** with the prompt granted,
`fetch('http://k7.local/api/status')` answered **200** with the device's CORS headers. The
premise 0057 and 0058 were built on is gone, and with it the reason for a second document, a
second window and a firmware partition.

## Decision

- **The App fetches the device itself.** `http://k7.local/api/…` from
  `https://k7.xperiments.dev`, one transport, one origin pair, no opener and no second tab.
- **The App is gated on `local-network`.** Until the permission is granted, no area renders:
  `ui/LocalNetworkGate.tsx` states the permission, asks for it, and retries. Denied or
  unsupported is a wall with instructions, never a silent fallback.
- **The Launcher is removed** — `launcher.html`, `launcher.js`, `launcher/`, its type file, its
  allowlist and the Shell build. The device serves no page: `/` and `/bridge` answer 404 like any
  unknown path; `/library`, `/stream`, `/api/*` and `/mcp` are unchanged.
- **The `frontdoor` partition leaves the table.** The firmware no longer packs a page, no longer
  flashes one, and no longer maps one into PSRAM at boot.
- **Chrome is the supported browser.** Safari and Firefox implement no `local-network`
  permission; the gate says so. That is the product's decision, not a defect to route around.

## Considered options

- **Keep the bridge for other browsers and denied prompts.** It keeps connected mode working
  everywhere, and it costs two transports kept in step, a window the person must leave open, and
  1.25 MB of partition for a fallback the product no longer wants.
- **Keep the Launcher as a device-facts page.** It repeats what the App's status line and the
  panel already say, and it is the only reason the device carries a page at all.
- **Keep the Shell and serve the Launcher from flash.** 0055's whole subject; with the page gone
  there is nothing to be resident.

## Consequences

- **One transport.** The direct device is `src/device/local.ts` over the request table in
  `OP_SPECS`; the bridge's wire (`parseMessage`, `BRIDGE_VERSION`, `LIMITS`, the 14-operation
  message table) goes with it.
- **Connected mode is Chrome-with-permission.** Anyone who denies the prompt, and every other
  browser, sees the gate — and cannot use the App at all, including analysis and the Last image.
  That is the deliberate cost.
- **The device's CORS headers become load-bearing.** `app_services.c` is what makes the direct
  response readable once the connection is permitted.
- **1.25 MB returns** to the partition table, and a flash no longer has to carry bytes that a
  LAN page served.
- **Revisit if** LNA becomes available in other engines, or if it is removed.

## Supersedes

- [ADR 0055](./0055-the-shell-is-resident.md) — the Shell is one image: there is no Shell.
- [ADR 0057](./0057-the-app-lives-at-a-public-origin.md) — its Launcher half only; the App's
  public origin stands.
- [ADR 0058](./0058-the-bridge-is-a-typed-rpc-between-two-windows.md) — there is no bridge.
