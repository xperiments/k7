# 04 — Library at boot and the JSON endpoint

**What to build:** Load and publish the Library tree during boot, in the order that keeps
memory safe, and serve `GET /api/library` from the prebuilt JSON with a real `Content-Length`,
a generation identifier and the no-card, no-directory, empty and truncated states. The device
must answer the request without touching the SD card. The boot log must make the scan cost and
the memory cost visible.

**Blocked by:** 03 — Library tree module, host tier.

**Status:** done (2026-10-20)

- [x] Boot logs the track and directory counts, total bytes, scan time, arena and JSON sizes,
      and free PSRAM before and after.
- [x] `GET /api/library` returns valid JSON with `state`, counts, `bytes`, `generation` and
      `truncated`; serving it performs no SD access.
- [x] A missing card or missing Music directory returns a diagnosable 200 with the right
      `state`, not a 500.
- [x] The JSON tree and every size match `os.walk()` of the host fixture tree.

## Comments

- `library` now produces a generation for the known "nothing to serve" outcomes too:
  `no_card` is built without touching the filesystem, and a failed `opendir` is `no_dir`.
  Truncation is the separate `truncated` boolean, as in §3.4; `state` stays `ok` or `empty`.
- `library_http` is a small adapter component: one route, `GET /api/library`, sending the
  prebuilt JSON with its exact `Content-Length` and `ETag: "gen-<n>"`. It has no filesystem
  call. The route is registered before `http_server_start()`.
- Boot order is now station, network, card, library, file surface, library endpoint,
  broadcast, listener. The boot log line carries counts, bytes, scan+build ms, arena and
  JSON sizes and PSRAM before/after. A failed library build is logged but does not stop the
  rest of the boot; `/api/library` is then unavailable until a later rescan.
- Generation is `1` for this boot. Ticket 11 replaces it with the publish/lease pair and
  increments it; the ETag already uses the same field as the body.
- Card arrival still updates `mini_files` but not `/api/library`; that is the rescan/card-
  arrival work in ticket 11, not this ticket.
- Verified: `make -C host check` (114 HTTP checks, t14 149 checks, layering, awkward names),
  `make -C host tools`, ASan/UBSan on t14, and both `build_sunton` and `build61`
  (app 0x2eadf0, 27% free; 0x20cc60, 49% free).
