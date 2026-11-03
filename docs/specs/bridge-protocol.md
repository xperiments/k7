# The bridge: typed operations between the App and the Launcher

**Goal.** The **App** runs at `https://k7.xperiments.dev`, a secure origin, and can never
reach `http://k7.local`: an HTTPS page cannot fetch an HTTP endpoint (mixed content), cannot
embed it in a frame (blockable mixed content), and the browser's Local Network Access rules
forbid a public origin from addressing a LAN host in the first place. The **Launcher** is the
page that *is* on `http://k7.local`, so it is the only thing that can talk to the device's
own API. The bridge is how the App asks it to.

It is a request/response RPC with a closed operation table. It is not a proxy, not a tunnel,
and not a stream.

**Status:** designed, not built. No firmware carries the Launcher yet, and no App speaks this
protocol. The rules below are the contract both ends are written against.

**Vocabulary:** [the App's glossary](../../web/CONTEXT.md). The device's API is specified in
[`sd-stream-server.md`](./sd-stream-server.md) and [`front-door.md`](./front-door.md) §2; the
Image is byte-defined in [`snapshot-format.md`](./snapshot-format.md).

---

## 1. Who talks to whom

| Window name | Page | Default role |
|---|---|---|
| `k7-app` | the App at its HTTPS origin | client: sends every request |
| `k7-bridge` | the Launcher at `http://k7.local/` | agent: executes every request |

**W1. Either side may open the other.** A Launcher loading with no trusted opener shows its
gateway page and opens the App on a click. An App with no link offers **Connect**, which
opens the Launcher in the window named `k7-bridge`; the Launcher sees a trusted opener and
enters bridge mode instead of showing its Launch button. This is what lets an installed PWA
connect: it has no opener, ever, but it can open one.

**W2. The opener holds a handle; that is the only asymmetry.** Both directions end with the
opening side holding a `WindowProxy`, which is how `event.source` is validated on both ends.

**W3. Messages flow one way.** The App sends `req`; the Launcher sends `res`. The Launcher
never initiates a request, never subscribes, and never pushes. The App's existing status poll
is the heartbeat; there is no other liveness machinery.

**W4. Opening requires a click.** `window.open()` without a user gesture is blocked, so
Connect is a button. A blocked popup is `null`, and the App says which permission to grant.

**W5. An iframe is never used.** `<iframe src="http://k7.local">` from the HTTPS App is
blockable mixed content and is blocked in every browser; the opposite direction (an HTTP page
framing the HTTPS App) is not a secure context and is not the design either. The channel is
two windows or nothing.

---

## 2. The wire

Five message types. Every message is a plain object with a string `t`; unknown fields are
ignored, known fields are validated.

```json
{ "t": "hello",   "v": 1, "role": "app",      "app": "https://k7.xperiments.dev" }
{ "t": "welcome", "v": 1, "role": "launcher", "audio": { "playing": false, "volume": 1 } }
{ "t": "req",     "id": "7", "op": "status",  "args": null }
{ "t": "res",     "id": "7", "ok": true,  "data": { } }
{ "t": "res",     "id": "7", "ok": false, "error": { "code": "timeout", "message": "…" } }
{ "t": "bye" }
```

**W6. `hello` is sent by the side that holds a handle, and may be repeated.**
- App → Launcher: the App always has one — its `window.opener`, or the `k7-bridge` window it
  opened — so it says `hello` first and the Launcher answers `welcome`. It repeats the
  `hello` after a Launcher reload, which keeps the window and loses the session.
- Launcher → App: the wire allows it and nothing depends on it; in this build the Launcher
  holds no handle, so it answers rather than initiates.
- `hello` is idempotent. A peer that receives a valid `hello` answers `welcome`. A peer never
  sends `req` before it has received a `welcome`.

**W7. `role` is checked, not trusted.** The App requires `role === "launcher"`; the Launcher
requires `role === "app"`. A message with the wrong role is dropped, not answered.

**W8. Versions.** `v` is the protocol version, `1` today. A peer that does not know the `v` it
receives answers `welcome` with its own version and refuses `req` with `version`. The App then
runs standalone and says the player's firmware and the App disagree — the App is deployed and
the firmware is flashed, so skew is permanent reality, not a fault.

**W9. `targetOrigin` is always explicit.** Both sides pass the trusted origin on every
`postMessage`; `"*"` appears nowhere in this codebase. A message addressed to the wrong window
is dropped by the browser rather than read.

**W10. `bye` is best effort**, sent from `pagehide` on either side. A session also dies by idle
expiry (§6); no correct behaviour may depend on receiving `bye`.

---

## 3. Operations

Fifteen operations and no others. There is no path, method, header, query or body on the wire:
each op maps to exactly one request the Launcher makes, and the App cannot express anything
outside this table.

| Op | The Launcher's request | `args` | `data` | Timeout |
|---|---|---|---:|---:|
| `status` | `GET /api/status` | — | the status document | 4 s |
| `playlists` | `GET /api/playlists` | — | `{selected, walking, missing, lists}` | 6 s |
| `playlist` | `GET /api/playlists/<id>` | `{id: hex16}` | `{id, name, rows: string[]}` | 6 s |
| `select` | `POST /api/playlists/select` | `{id: hex16 \| null}` | `{order, selected}` | 10 s |
| `snapshot` | `POST /api/snapshot` | `{bytes: Uint8Array}` | `{applied, bytes}` | 30 s |
| `rescan` | `POST /api/rescan` | — | `{ok}` or a `refused` with status 409 | 10 s |
| `play` | `POST /api/play` | `{path: string}` | `{}` | 10 s |
| `library.search` | `POST /mcp` → `library_search` | `SearchArgs` | `{rows, total}` | 10 s |
| `library.list` | `POST /mcp` → `library_list` | `{path, depth, limit}` | `{entries}` | 10 s |
| `library.stats` | `POST /mcp` → `library_stats` | — | the stats document | 10 s |
| `library.track` | `POST /mcp` → `library_track` | `{path}` | the track document | 10 s |
| `stream.play` | none — the Launcher's own `<audio>` | — | `{playing}` | 2 s |
| `stream.pause` | none | — | `{playing}` | 2 s |
| `stream.volume` | none | `{level: 0..1}` | `{volume}` | 2 s |
| `stream.state` | none | — | `{playing, volume, blocked}` | 2 s |

**O1. `library.*` is four tools, not a JSON-RPC passthrough.** `/mcp` also exposes
`control_play`; the bridge does not, because `POST /api/play` is the narrower command the App
already uses. The Launcher's MCP client performs the `initialize` handshake itself and calls
exactly these four tools.

**O2. Nothing else is bridged.** `/api/library`, `/api/library.bin`, `/api/screen.bmp`,
`/api/scene`, `/mcp` itself and `/stream` are not operations. A feature that needs one of them
either is not a bridge feature or adds an operation to this table and to both ends, which is
the point of a table.

**O3. The stream is the Launcher's own.** `stream.*` never reaches the device: the Launcher
holds one `<audio src="/stream">` and the App controls it. A message is not a user gesture, so
the first `play()` can be refused by the autoplay policy; that is `blocked`, and the App tells
the user to press play in the bridge window. The Launcher also carries its own transport
controls, so that instruction can always be obeyed.

**O4. Serialized, one in flight.** The Launcher runs one request at a time and queues the rest
in arrival order. The device has four connection workers shared with the broadcast, one
flash-capable task, and a snapshot upload that restarts it; a bridge that fans out would make
"the player restarted" look like "the player is broken". Both ends enforce the operation's
budget: the App stops waiting and reports `timeout`, and the Launcher answers `timeout` on the
same budget without aborting the request — it holds the one-in-flight slot until the request
actually settles, so the device never sees two bridge requests at once.

**O5. Arguments are validated at both ends.** The Launcher validates `args` against the op's
schema before it builds a request; the App validates `data` after. Neither trusts the other,
and neither trusts the device: `status` is parsed, not cast.

---

## 4. Errors

`error.code` is a closed vocabulary. `error.message` is a short displayable sentence.
`error.status` is the device's HTTP status when there was one.

| Code | Who produces it | Meaning |
|---|---|---|
| `bad-message` | either | not an object, missing `t`/`op`/`id`, wrong types, duplicate in-flight `id` |
| `unknown-op` | Launcher | `op` is not in §3 |
| `bad-args` | Launcher | args fail the op's schema |
| `too-large` | either | request or response over the cap in §5 |
| `timeout` | either | no answer within the op's timeout |
| `unreachable` | Launcher | the device did not answer at all |
| `refused` | Launcher | the device answered and refused; `status` carries why (404, 409, 422) |
| `blocked` | Launcher | the browser refused `stream.play` (autoplay) |
| `version` | either | protocol version not understood |

**E1. Control flow uses `code`, never `message`.** The App's sentences are its own; a device's
`error` text may be shown only as `message` on a `refused`, because "A scan is already running"
is the device's fact and re-inventing it is worse.

---

## 5. Size, and what is transferred

**C1. A request's arguments are capped at 256 KiB** — the device's own upload limit
(`HTTP_UPLOAD_MAX`). The Launcher refuses locally rather than discovering it after a round
trip. Non-binary arguments are capped at 64 KiB of JSON.

**C2. A response is capped at 1 MiB.** Every bridged document is far smaller today; the cap
exists so a future op cannot fill the tab's memory through a channel nobody was watching.

**C3. Two ops carry binary this way.** `snapshot` sends `args.bytes` as a `Uint8Array` with the
buffer in the transfer list, so the App's copy is detached rather than duplicated.
`library.search` and friends return JSON. No op returns binary today; the rule is that a `data`
field named `bytes` is transferred, never cloned.

---

## 6. Sessions, and the three facts

**S1. The Launcher keeps a table** of `source WindowProxy → {origin, version, lastSeen}`. A
`req` from a source with no session is dropped. This is why a session is established by
`hello`: a Launcher reload loses `window.open`'s return value but keeps the window, and the App
can re-announce.

**S2. The App validates the source as its counterpart.** It accepts a message only when
`event.source === window.opener` (when the Launcher opened it) or `event.source === bridgeWindow`
(when it opened the Launcher). `event.origin` must equal `http://k7.local` exactly.

**S3. The Launcher validates the origin against its baked list** — `https://k7.xperiments.dev`,
plus a development origin only in a development build — and never derives it from the message,
the referrer or a URL parameter. §7, X2.

**S4. Idle expiry is 60 s.** The App's status poll is every 5 s while it is visible, so a
session that has been silent for a minute is gone. Expiry is a disconnect, not an error.

**S5. Three facts, not one.** *Bridge alive* (a session exists), *device reachable* (the last op
or poll got an answer), and *offline* (the browser has no network) are independent. The App's
chip composes them; a truthy bridge never implies a reachable device, and a snapshot upload
implies the device is about to restart, so a poll that fails for two seconds after one is
expected, not an alarm.

**S6. Reconnection is a click.** When the bridge dies, the App degrades to standalone and its
Connect button opens a fresh `k7-bridge`. When the App dies, the Launcher notices
`window.opener.closed` and returns to its gateway page.

---

## 7. Security rules

**X1.** Every `postMessage` has an explicit `targetOrigin`. No wildcard, ever.

**X2.** The Launcher's trusted App origins are compiled in at build time. There is no URL
parameter, no query string, no `document.referrer` and no stored setting that can add one.

**X3.** The App's trusted Launcher origin is the single string `http://k7.local`. An IP
address, a different port and an `http://` page on another host are all rejected; the App says
connected mode needs that name.

**X4.** A `req` is executed only from a sessioned source (§S1). `hello` alone does not grant
anything but `welcome`.

**X5.** The op table is the allowlist. There is no path, no method and no header on the wire, so
"proxy an arbitrary request" is not expressible.

**X6.** Message types, roles, ids, sizes and every argument are validated before use. An `id`
that is already in flight is `bad-message`, not a second request.

**X7. No credentials ever cross the bridge.** The Provider's key, the Invidious instance and
every `Settings` value stay in the App's storage. The device has no authentication and the
channel must not become a way to add one by accident.

**X8.** The App never sends track metadata anywhere, and the bridge is not an exception: the ops
that carry a path carry it to the device that owns the card.

**X9.** The Launcher never evaluates anything from a message. No `eval`, no `Function`, no URL
from a message, no `innerHTML`.

**X10.** The Launcher's page carries a CSP of its own (`default-src 'self'`), because it is the
one page on the LAN that holds a token-less ability to command the device.

---

## 8. Failure modes

| What happens | What the user sees |
|---|---|
| Launcher tab closed | App: standalone, "No player — Connect"; one click reopens the bridge |
| App tab closed | Launcher: back to the gateway page |
| Popup blocked | "Allow popups for this site, then press Connect" |
| Launcher reloaded | App re-`hello`s on its next poll; connected again within 5 s |
| App reloaded | Launcher keeps the window; the new document `hello`s, the old session expires |
| App opened by hand (no opener, no bridge) | standalone, Connect offered |
| Device off / unplugged | bridge alive, device unreachable; the chip says so |
| Snapshot accepted | the device restarts; polls fail for ~2 s and recover |
| Firmware older than the App | `version` refusal; standalone with a "firmware and app disagree" banner |
| Request over 256 KiB | `too-large` before anything is sent |
| Second App tab opens the same `k7-bridge` | the existing window is focused; one bridge, one session |

---

## 9. Tests

**T1. vitest, both state machines, adversarially.** For each side: wrong origin; wrong role;
no session; unknown op; extra fields; wrong types; oversize request and response; duplicate id;
timeout; `welcome` before `hello`; a `res` with no matching `req`; version skew; `bye` then a
`req`. These are pure functions over a fake `PostMessagePort`, and they run on every push.

**T2. Playwright, two real origins.** The Launcher on one origin and the App on another,
served by the test itself. Five tests: the handshake in both directions; one `status` round
trip; **`window.opener` still reachable after load** (the COOP regression guard — the mistake
that would silently kill connected mode); disconnect when the bridge window closes; the Shell
served from cache with `context.setOffline(true)`.

**T3. In CI the App is served at `http://localhost:<port>`.** `localhost` is a trustworthy
origin by definition, so it is a secure context with no certificate at all, while remaining a
different origin from the Launcher at `127.0.0.1:<port>`. mkcert is a local-development
concern, not a CI one.

**T4. `library.*` semantics are shared with the Image source.** The four ops have a second
implementation over the Last image ([`k7-app.md`](./k7-app.md) §4); one JSON conformance
corpus — rows in, queries and expected results out — is run by the App's vitest and by the
device's host tests, so the two implementations cannot drift silently.
