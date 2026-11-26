# 03 — The deck's name is one value

**What to build:** the device's name has one source of truth. It is spelled out in four places today
— the mDNS host and instance, the advertised endpoint in the TXT record, the MCP origin guard's list
of acceptable names, and the name carried in the handover record — so any rename is four edits that
can disagree, and a disagreement is a device that answers to one name and rejects requests addressed
to another.

**Blocked by:** None — can start immediately. This is a prefactor: it makes 04 a one-place change.

**Status:** ready-for-agent — HA-03 on the board (proposed).

- [ ] Every one of the four consumers asks the same thing for the name; none of them holds a literal.
- [ ] Behaviour is unchanged: the device still answers `k7`, still advertises `k7` and the same
      endpoint, still accepts the same names at the origin guard, still writes `k7` into the handover
      record.
- [ ] A host test proves the four agree by moving the one value at a test seam and reading all four.
- [ ] No spec changes: a rename is not being decided here, only where the name lives.

**Not in this:** a settable name, an alias, or anything a person can see. If this ticket changes
what any of them says, it has done too much.

Spec: none — the specs still describe a device called `k7`.
