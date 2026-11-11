# The setup surface exists only while its hotspot does

[ADR 0076](./0076-the-network-is-chosen-on-the-devices-setup-network.md) put the one page the device
serves at `/setup`, and — as one of its consequences, not as a decision anyone argued for separately —
served it on **every interface**. The reasoning there was that a device on your LAN with a dead panel
still needs a way in, and the page is the cheapest one.

Seen working on the board, that reads differently. `GET /setup` is an unauthenticated page that
rewrites the device's network, and this firmware has no authentication anywhere. On the LAN it is the
most consequential thing on port 80, and it is served for the whole life of a device that is working
perfectly well. The owner's call, having seen it: **the surface should not exist outside the window
it was built for.**

## Decision

- **The setup page and all seven `/api/setup*` routes answer only while the setup network is up.** With
  no session, every one of those paths is a `404` on every interface — the same answer the device
  gives any path it does not serve.
- **The hotspot is raised by the boot triggers or the panel, never by the network.** The three triggers
  of 0076 stand: unprovisioned at boot, no association within 20 s of boot, and the panel's `Setup`
  row. `POST /api/setup/ap {"on":true}` is inside the gate, so a LAN client cannot raise it — that is
  the point of this decision, not an oversight in it.
- **Everything else is unchanged.** `/stream`, `/api/*`, `/mcp` and `/library` still answer on the setup
  network exactly as on the LAN, because that is what makes the hotspot useful to a client that is on
  it; only the surface that *changes* the device is scoped.

## Considered options

- **Leave it as 0076 had it.** A working device is reconfigurable from the LAN without walking to it.
  It is also a page on the LAN that re-points the device, on a device with no authentication — rejected
  by the owner once it was visible.
- **The page gated, the API open.** Tidier to look at and no safer: the API can do everything the page
  can, so gating only the page would be decoration with an audit trail.
- **Gate by interface instead of by session** — answer `/setup` only on the setup netif. Same effect
  while the device is unprovisioned, and it breaks the one case the gate is *for*: a client that is on
  the hotspot and asks for the page by the LAN's address. Session-scoped is the rule a person can state.

## Consequences

- **A LAN user with a dead panel must power-cycle the device** to get a hotspot: the panel's row and
  the boot triggers are the only ways in. That is the stranded case 0076 named, accepted here on the
  record rather than discovered later.
- **The `409` "no session" answer on a join is now unreachable through HTTP.** It stays as a guard for
  the race where the session closes between the gate and the call, and `setup_join()` keeps returning
  it for any future caller.
- **The board harness runs inside the window**, so it is unaffected: it joins the hotspot first, and
  every route it uses is open while it is there.
- **Revisit if** the stranded case starts to matter — a panel that dies is a real failure, and the
  cheapest mitigation would be a longer boot window rather than a page on the LAN.

**Status:** built, host-tested (`H5`, `H14`) and flashed; the gate's host half is
`host/tests/setup_tests.py`, which stops the hotspot and asserts a `404` on `/setup`, `/api/setup`,
`/api/setup/scan`, the join and the hotspot's own switch.

## Supersedes

[ADR 0076](./0076-the-network-is-chosen-on-the-devices-setup-network.md) — **its "on every interface"
clause only.** Everything else in it stands: the record in NVS, the three triggers, the grace, the
click-free page, the live join and its recorded attempt.
