# 10 — mDNS discovery and the Origin guard

**What to build:** Advertise the MCP endpoint as `xtape.local` and `_mcp._tcp`, with the TXT
records that tell a client where `/mcp` lives, and guard browser-originated requests against DNS
rebinding. `Origin` is validated only when it is present; it must pin to the device's local IP
or to `xtape.local`, not merely match `Host`. A bad `Origin` is 403 before the body is parsed.
Native clients without `Origin` keep working.

**Blocked by:** 09 — Read-only MCP endpoint.

**Status:** ready-for-agent

- [ ] `dns-sd` or `avahi` discovers `_mcp._tcp` with `path=/mcp`, the endpoint and
      `device_type=mcp:device`.
- [ ] A present `Origin` with a wrong authority or missing `Host` returns 403; a request with
      no `Origin` is accepted; a mismatched `Host` alone is not rejected.
- [ ] `Origin` pinned to the local IP or `xtape.local` succeeds; an attacker domain resolving to
      the device does not.
- [ ] mDNS starts only after `/mcp` is listening, and the host build links no mDNS.
- [ ] The one-device-per-LAN limitation of the fixed hostname is documented.
