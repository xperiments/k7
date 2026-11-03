# The bridge is a typed RPC between two windows

The App cannot reach the device. An HTTPS page cannot fetch `http://k7.local` (mixed content),
cannot frame it (`<iframe>` is blockable mixed content and is always blocked), and a public
origin addressing a LAN host runs into Local Network Access rules before any of that. The only
document that can talk to the device is one **on** `http://k7.local`, so the Launcher is that
document and the bridge is how the App asks it for things.

## Decision

- **Two windows, one direction.** The App sends every request; the Launcher executes and
  answers. No subscriptions, no push, no streaming.
- **Either window may open the other.** The Launcher opens the App in the window `k7-app`; the
  App opens the Launcher in `k7-bridge` on a click. The second direction is what lets an
  installed PWA connect at all, and what turns a closed bridge into a button instead of a dead
  end. Sessions are established by `hello` and kept in a source table, because a reload keeps
  the window and loses `window.open`'s return value.
- **A closed table of fifteen operations**, each mapping to exactly one request. No path, no
  method, no header and no body crosses the wire, so "proxy an arbitrary request" is not
  expressible. `/api/library`, `control_play` and `/stream` are not operations.
- **One request in flight.** Four connection workers and one flash-capable task are the
  device's, and a snapshot restarts it; a bridge that fans out would make a reboot look like a
  fault.
- **Baked allowlists.** The Launcher's app origins and the App's launcher origin are compiled
  in. No URL parameter, referrer or setting can add one.

The full contract is [`bridge-protocol.md`](../specs/bridge-protocol.md).

## Considered options

- **An MV3 extension** with `host_permissions`. It works and it is a secure context, and it
  costs every user an install that only the Web Store, enterprise policy or Developer mode can
  perform — the ceremony 0049 rejected, moved to a different dialog.
- **A public certificate on the device** — a DNS record per network and a renewal per device.
- **A local proxy daemon on the computer.** Ceremony again, and a second runtime to maintain.
- **An iframe.** No browser will do it, in either direction, for this origin pair.

## Consequences

- **The opener costs cross-origin isolation.** `window.opener` survives only when the opened
  document is `unsafe-none`; `COOP: same-origin` isolates it and severs the bridge, and
  `same-origin-allow-popups` does not help from the App's side. So the App sets no COOP — which
  means no `SharedArrayBuffer`, so ONNX Runtime stays single-threaded. That is already true on
  the Device copy, WebGPU does not care, and a Playwright test guards the header so a future
  hardening pass cannot silently break connected mode.
- **The bridge is a second window the user keeps open.** Closing it is a banner and a button,
  not an error.
- **Autoplay applies.** A message is not a user gesture, so the first `stream.play` can be
  refused; the Launcher carries its own transport controls so the instruction can be obeyed.
