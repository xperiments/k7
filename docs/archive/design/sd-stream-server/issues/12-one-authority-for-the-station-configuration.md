# 12 — One authority for the station configuration

**What to build:** `mini_icecast_cfg_t` says every zero field means "the documented
default", but only `mini_icecast_start()` applies that rule. `mini_icecast_check_station()`
validates the raw fields, so a configuration that uses the defaults is refused by the check
and served by the start — and the boot path calls the check first, while the host entry
point never calls it at all.

Add a station module that resolves the configuration **once**: fill the documented default
table, then do the arithmetic, then check the client limit. It returns either a resolved
station or a refusal naming the offending value. `check_station()` becomes
resolve-and-discard; `start()` becomes resolve-and-use. Nothing else may hold a default.

**Blocked by:** none

**Status:** done (host tier and both firmware builds; board tier not run - no board attached)

- [ ] `mi_station_resolve(cfg, out, err, err_size)` fills the default table and validates
      the station format and the client limit together.
- [ ] `mini_icecast_check_station()` and `mini_icecast_start()` both call it; no second
      default table exists anywhere in the component.
- [ ] `out_hz = 0` (documented default) is accepted by both entry points.
- [ ] An invalid format produces the same refusal message from both entry points.
- [ ] `t04_format_validation` pins the default table and the zero-means-default rule.

**Files:** `components/portable/mini_icecast/include/mi_station.h` (new),
`components/portable/mini_icecast/mi_station.c` (new),
`components/portable/mini_icecast/mini_icecast.c`,
`components/portable/mini_icecast/include/mini_icecast.h`,
`host/tests/t04_format_validation.c`, `host/Makefile`.

**Interface:** `esp_err_t mi_station_resolve(const mini_icecast_cfg_t *cfg,
mi_station_t *out, char *err, size_t err_size);` — `out` receives the resolved station
format, metadata interval, lead, burst length, client limit, station name and port.
`out` may be NULL, which is the check-only call.

**Tests:** host unit test (t04) for defaults, the zero rule, both refusals; the resolved
station is asserted to be what `mini_icecast_start()` would use.

**Source:** architecture review, candidate 3.

## Comments

- Added `components/portable/mini_icecast/mi_station.c/.h`: the default table, the client-range check and
  `mi_enc_check_format()`, in one `mi_station_resolve()`.
- `mini_icecast_check_station()` is now resolve-and-discard; `mini_icecast_start()` is
  resolve-and-use. The resolved station is what the rest of the broadcast reads.
- `t04_format_validation` now pins the default table and asserts that `out_hz = 0` is accepted
  by both entry points, and that an invalid format produces the same refusal from both.
- Verified: `make -C host test`; `idf.py -B build61 build`; the Sunton build.
