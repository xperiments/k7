# 11 — Card insertion and rescan generations

**What to build:** Make the Library tree survive a card that arrives after boot and a rescan
that happens while the broadcast is playing. A successful late mount publishes a complete
generation and the broadcast starts without a reboot. An explicit rescan builds a new generation
off to the side, publishes it atomically, lets the broadcast finish its current Track and lets
old JSON readers finish. Only one scan runs at a time.

**Blocked by:** 04 — Library at boot and the JSON endpoint; 07 — The broadcast plays the whole
Library tree; 08 — HTTP POST and play any Track.

**Status:** done (2026-10-20)

- [x] Boot without a card, insert one, and both the index and the broadcast recover without a
      reboot.
- [x] A rescan while playing publishes a new generation; the broadcast switches at the next
      Track and the surfaces switch at the swap.
- [x] A slow reader holding the old JSON completes; the retired generation is freed only after
      its last reader releases it.
- [x] A second rescan while one is running is declined, and the card-less state remains
      diagnosable.

## Comments

- `library` now owns a small generation store: `library_next_generation()`,
  `library_publish()`, `library_acquire()`, `library_release()` and
  `library_current_generation()`. A generation carries a refcount; `publish` swaps the current
  pointer and drops the publisher's reference on the old generation, and the last reader's
  release frees its arena and JSON off to the side. `library_cfg_t.generation` lets the caller
  put the right number in the JSON, the blob and the ETag.
- Every reader leases: `/api/library[.bin]` and `/index`/downloads acquire per request; MCP
  acquires per tool call; the broadcast acquires for a pass. The old JSON send completes even
  when a rescan publishes; the integration test holds the old response open, rescans, then
  reads generation 1 to the end.
- `app_services_rescan()` starts one FreeRTOS task (atomic busy flag, 409 on a second request).
  It takes the `mini_files` card lock (so a download cannot race the scan), parks the broadcast,
  builds off the side, publishes, unparks. Parking is reference-counted, so a download ending
  mid-scan cannot unpark the scan and vice versa. The broadcast notices
  the current generation changed only after its current Track returns, releases the old lease,
  and acquires the new one. `POST /api/rescan` with `{}` is the HTTP trigger (`202`/`409`).
- Boot publishes the boot generation before the file surface, broadcast and MCP start. When no
  card is present it publishes the `no_card` generation; the retry task, on a successful mount,
  calls `app_services_card_state(true)` and `app_services_rescan()`. A card-less rescan uses
  `library_open_no_card()` so the `no_card` state remains diagnostic.
- The old `mini_icecast_set_library()` override remains for the transport unit test; the
  production path passes NULL and leases the published generation.
- Verified: `make -C host check` (156 HTTP checks, t14 164 including the publish/lease block,
  layering, MCP, T17 rescan: slow old JSON (3000 tracks, >200 KB, larger than the socket buffers), new generation,
  broadcast count, two simultaneous rescans 202/409, card-less generation), `make -C host tools`, and both
  `build_sunton`/`build61` (0x2fa470, 26% free; 0x21c040, 47%).
- Not here: the actual card-insertion retry on hardware (ticket 13); card removal is still the
  documented stale-until-rescan case (now stated in README). `POST /api/rescan` requires the
  same JSON body envelope as every POST route (`Content-Type: application/json`, `{}`).
- Hard-review follow-ups: parking is reference-counted so download and rescan cannot unpark
  each other; the rescan takes the `mini_files` card lock before parking, so the two never walk
  FATFS together; card state is mutex-protected; `library_publish` rejects a stale number or the
  same pointer, `library_release` ignores a double release, and `library_close` refuses a
  current/leased generation. T17 now uses 3000 tracks (>200 KB) so the slow reader really
  outlives the publish, and asserts the body size.
