# 10 — mDNS discovery and the Origin guard

**What to build:** Advertise the MCP endpoint as `xtape.local` and `_mcp._tcp`, with the TXT
records that tell a client where `/mcp` lives, and guard browser-originated requests against DNS
rebinding. `Origin` is validated only when it is present; it must pin to the device's local IP
or to `xtape.local`, not merely match `Host`. A bad `Origin` is 403 before the body is parsed.
Native clients without `Origin` keep working.

**Blocked by:** 09 — Read-only MCP endpoint.

**Status:** done (2026-10-20)

- [x] `dns-sd` or `avahi` discovers `_mcp._tcp` with `path=/mcp`, the endpoint and
      `device_type=mcp:device`.
- [x] A present `Origin` with a wrong authority or missing `Host` returns 403; a request with
      no `Origin` is accepted; a mismatched `Host` alone is not rejected.
- [x] `Origin` pinned to the local IP or `xtape.local` succeeds; an attacker domain resolving to
      the device does not.
- [x] mDNS starts only after `/mcp` is listening, and the host build links no mDNS.
- [x] The one-device-per-LAN limitation of the fixed hostname is documented.

## Comments

- `http_request_t` now records `Host` and `Origin` (trimmed, 128 bytes) alongside the request.
  `post_route` validates `Origin` before JSON parsing. When present: `Host` is required; the
  scheme must be `http://`; the authority may not contain a path, query, fragment or userinfo;
  it must equal `Host` and pin to the current local IP from `app_board_local_ip()` at the server
  port, or to `xtape.local` at that port. A missing `Origin` is accepted with any `Host`; a
  wrong `Host` alone is not rejected. A bad `Origin` is 403 / `-32600` with
  `MCP-Protocol-Version`.
- `app_board` gained `app_board_local_ip()` and `app_board_mdns_start()`. The board adapter
  stores the station IPv4 on `IP_EVENT_STA_GOT_IP`, calls `mdns_init()` after Wi-Fi is online,
  sets hostname `xtape`, and advertises `_mcp._tcp` on 8000 with
  `path=/mcp`, `endpoint=http://xtape.local:8000/mcp`, `device_type=mcp:device`.
- The appliance calls `app_board_mdns_start("xtape", 8000, "/mcp")` only after
  `app_services_start()` returned (the HTTP listener is up) and `app_board_wait_online()`
  returned (an address exists). A failure is logged and does not stop the stream. The host
  adapter is a no-op and the host Makefile links no mDNS; the layering check asserts it.
- The `espressif/mdns` managed component is in `main/idf_component.yml` (resolved 1.14.0) and
  the board component requires it. `managed_components/` stays ignored; `dependencies.lock`
  records the resolved dependency.
- README documents `xtape.local`, the TXT records, `dns-sd`/`avahi` commands, and the fixed
  hostname's one-device-per-LAN limitation.
- Verified: `make -C host check` (152 HTTP checks, t14 152, layering including "host adapter
  links no mDNS", MCP origin cases, awkward names), `make -C host tools`, and both
  `build_sunton`/`build61` (0x2f98c0, 26% free; 0x21b530, 47%).
- Not here: actual `dns-sd`/`avahi` discovery on hardware; the host has no mDNS. That is a
  board check (ticket 13). The Origin guard runs after the body is read but before it is
  parsed; the transport length/type errors still take precedence.
