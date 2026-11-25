# 02 — The deck says which deck it is

**What to build:** a home-automation system can tell one deck from another and name it. Today the
status document names the *card* (`mark`), never the board, and a config entry that cannot key on a
stable identity either refuses the device or keys on a hostname that changes.

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent — HA-02 on the board (proposed).

- [ ] The status document carries the board's **id**: the station MAC as twelve lowercase hex
      characters, no separators. It is the same on every boot and every re-flash that keeps the
      settings; two boards never share it.
- [ ] It also carries **`fw`** (the running app version) and **`board`** (the board's short name), so
      a device page can be filled without a second request.
- [ ] The fields are additive: an older App, an older test and the MCP `player_status` are untouched,
      and a reader that does not know them ignores them.
- [ ] The device names itself from the radio's MAC, not from flash contents that `erase_flash` can
      take away — a wiped deck still knows which deck it is.
- [ ] Host test covers the document's shape and the id's format; the board check reads the same MAC
      the serial log reports at boot.

**Not in this:** a name. The id is machine identity; the human-readable address is its own ticket.

Spec: `docs/specs/front-door.md` §2, `docs/specs/sd-stream-server.md` §6.
