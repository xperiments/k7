# The App lives at a public origin, and the device serves a Launcher

> **Partly superseded by [ADR 0067](./0067-chrome-only-direct-to-the-device.md).** The App's
> public origin stands; the Launcher the device served beside it does not.

[ADR 0049](./0049-the-front-door-is-plain-http.md) took the app off TLS because the only ways
past a browser's certificate warning were a per-machine trust step or a public certificate on
the device — "a name in public DNS pointing at its LAN address, not a better certificate". It
named that condition and it has arrived in a different shape: the name points at a **static
host**, not at the device. `k7.xperiments.dev` is ordinary Vercel infrastructure with an
ordinary certificate, and the player keeps plain HTTP on the LAN. No certificate ships in the
firmware, no machine trusts anything, and the App is finally a secure context.

## Decision

- **The App is deployed at `https://k7.xperiments.dev`** (renamed from `xtape` by
  [ADR 0062](./0062-xtape-becomes-k7.md)) and served as a real PWA: worker, install, offline
  shell, directory handles, WebGPU.
- **The device serves the Launcher at `http://k7.local/`** — a few kilobytes of plain HTTP
  whose jobs are the device's facts, the stream's audio, and executing the bridge's operations
  ([ADR 0058](./0058-the-bridge-is-a-typed-rpc-between-two-windows.md)). It also serves the
  App's own bytes at `/app` as a **Device copy** for a LAN with no internet.
- **The bridge is the only way the two meet.** An HTTPS page cannot fetch, frame or otherwise
  reach an HTTP LAN device; a window on that origin can, and does.

This supersedes 0049's "the front door is plain HTTP" and amends 0048 and 0055: the device
still serves *a* page, so the Shell image and the black frame it costs remain — but the page is
the Launcher, not the app.

## Consequences

- **Connected mode requires the new firmware.** The Launcher *is* firmware; an unflashed board
  serves the old app and cannot bridge.
- **Two origins, two stores.** Work done in the Device copy cannot be handed to the App; the
  card is the only thing they share.
- **The fallback is thin on purpose**: no worker, no picker, no WebGPU there, and its weights
  come from the HTTP cache rather than a worker's.
- **Nothing about 0049's other decisions changes.** The card stays in the computer, the device
  still accepts no Track, and the trust step it rejected still does not exist.
