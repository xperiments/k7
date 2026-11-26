# 04 — A person names the deck

**What to build:** two decks on one LAN stop fighting over `k7.local`. A person gives the device a
word — *kitchen*, *studio* — and it answers at `k7-kitchen.local`, which is short enough to type and
to remember, and unique enough that a neighbour's `kitchen.local` is not a collision. The old name
keeps answering as an alias, so nothing already built breaks on a one-deck LAN.

**Blocked by:** 03 — The deck's name is one value.

**Status:** ready-for-agent — HA-04 on the board (proposed).

- [ ] The setup page — the one place a person can type — offers a name: a lowercase DNS label,
      letters, digits and single hyphens, refused with a sentence when it is not one.
- [ ] The name is stored with the other settings and survives a reboot; a device that has never been
      named answers `k7.local` exactly as today, so the setting is optional.
- [ ] A named device answers `k7-<name>.local`, advertises that name in its TXT endpoint, accepts it
      at the MCP origin guard, and carries it in the handover record — all from the one value 03
      established.
- [ ] `k7.local` keeps answering as an alias while a name is set. This is the compatibility promise,
      and it is the reason the change is additive.
- [ ] An ADR records the rule: the name is the person's word, the address is the product's word plus
      theirs, and the bare product name is an alias.
- [ ] Board check: `dns-sd` sees the new instance, a reboot keeps it, `http://k7.local` still answers,
      and `erase_flash` returns the deck to `k7`.
- [ ] The spec's name statements — the front door, the setup page, the MCP origin guard — are updated
      in the same change.

**Not in this:** renaming from the App or the panel. The panel has a Pad, not a keyboard; the App is a
follow-up. **Not proven with one board:** two named units answering their own names — the check above
runs on one deck, and the two-unit shape is recorded as owed until a second board is on the bench.

Spec: `docs/specs/front-door.md`, `docs/specs/wifi-provisioning.md`, `docs/specs/network-handover.md`.
