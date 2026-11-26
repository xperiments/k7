# 05 — The player finds the deck by the name it was handed

**What to build:** a renamed deck keeps playing on the speaker. The player looks the deck up by the
name the handover gave it, then by `k7.local`, then by the recorded address — so the name is what
survives a new DHCP lease, and a stale address is the last resort rather than the first.

**Blocked by:** 04 — A person names the deck.

**Status:** ready-for-agent — HA-05 on the board (proposed).

- [ ] The lookup order is the record's name first, then the bare product name, then the recorded
      address; the first that resolves is used.
- [ ] A player handed a record with no name behaves exactly as today.
- [ ] A deck renamed after a handover reconnects through the new name even when the recorded address
      is dead — this is the case the ticket exists for.
- [ ] Host tests cover the order, the empty-name fallback and the dead-address case against a scripted
      resolver and socket.
- [ ] Board check: rename the K7, change its lease, and the player comes back without a re-provision.

**Not in this:** a new handover. The record already carries a hostname; this is the player reading
the one it was given.

Spec: `docs/specs/k7player.md` R19, `docs/specs/network-handover.md`.
