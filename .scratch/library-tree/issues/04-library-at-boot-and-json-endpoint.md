# 04 — Library at boot and the JSON endpoint

**What to build:** Load and publish the Library tree during boot, in the order that keeps
memory safe, and serve `GET /api/library` from the prebuilt JSON with a real `Content-Length`,
a generation identifier and the no-card, no-directory, empty and truncated states. The device
must answer the request without touching the SD card. The boot log must make the scan cost and
the memory cost visible.

**Blocked by:** 03 — Library tree module, host tier.

**Status:** ready-for-agent

- [ ] Boot logs the track and directory counts, total bytes, scan time, arena and JSON sizes,
      and free PSRAM before and after.
- [ ] `GET /api/library` returns valid JSON with `state`, counts, `bytes`, `generation` and
      `truncated`; serving it performs no SD access.
- [ ] A missing card or missing Music directory returns a diagnosable 200 with the right
      `state`, not a 500.
- [ ] The JSON tree and every size match `os.walk()` of the host fixture tree.
