# 13 — Lift the burst ring and the ICY splitter out of the fan-out

**What to build:** The **burst** is a hand-rolled circular byte ring of
`[len16 LE][whole encoded frame]` records, and the **metadata interval** is a byte-count
splitter that interleaves `StreamTitle` blocks into the same byte stream. Both are static
functions welded to `http_send_all(fd)`, so the rules that decide what a client receives on
connect — record framing, eviction, wraparound, `metaint` boundaries, 16-byte metadata
blocks — can only be observed by connecting a client.

Extract them into two deep modules:

- `mi_burst`: `new(capacity)` / `put(frame)` / `begin(cursor)` / `next(cursor, out, cap)` /
  `used()` / `free()`. It owns record framing, eviction and wraparound, refuses a record
  that cannot fit rather than looping, and takes no lock — the fan-out holds the lock.
- `mi_icy`: `new(metaint)` / `push(audio, n, title, title_version, sink, ctx)` / `free()`.
  It owns metaint counting, the length byte, 16-byte padding, and the rule that an
  unchanged title still emits a zero-length block. It calls a sink with either an audio
  span or one complete metadata block; it never touches a socket.

`broadcast()` and `mini_icecast_register_client()` become the adapter: they hold the lock,
choose the sink (`http_send_all`), and keep the timeout semantics unchanged.

**Blocked by:** none

**Status:** done (host tier and both firmware builds; board tier not run - no board attached)

- [ ] The ring's capacity invariant (`capacity >= largest record + 2`) is enforced by the
      module, not assumed by the caller.
- [ ] A record that straddles the end of the ring is returned intact.
- [ ] Eviction drops whole records, oldest first.
- [ ] Metadata lands exactly every `metaint` encoded bytes, including when a burst record
      is split across the boundary.
- [ ] An unchanged title still emits a zero-length metadata block (stream stays aligned).
- [ ] A title long enough for two metadata blocks is padded correctly.
- [ ] `mini_icecast.c` no longer contains `ring_`/`send_meta` arithmetic.
- [ ] Host unit tests and the integration suite (metadata, burst-on-connect) pass.

**Files:** `components/mini_icecast/include/mi_burst.h` (new),
`components/mini_icecast/mi_burst.c` (new),
`components/mini_icecast/include/mi_icy.h` (new),
`components/mini_icecast/mi_icy.c` (new),
`components/mini_icecast/mini_icecast.c`, `host/tests/t07_burst_icy.c` (new),
`host/Makefile`.

**Tests:** t07 drives the ring through several times its capacity with uneven record sizes
and asserts every surviving record is byte-identical; drives the ICY splitter with a
`metaint` that does not divide the chunk size and asserts the exact byte sequence, the
zero-length unchanged-title block, and the two-block title.

**Source:** architecture review, candidate 1.

## Comments

- Added `mi_burst.c/.h` (record framing, eviction, wraparound, capacity invariant enforced at
  construction) and `mi_icy.c/.h` (metaint counting, length byte, 16-byte padding, the
  zero-length unchanged-title block). The fan-out is now the adapter with the lock and the fd.
- `t07_burst_icy` drives the ring past capacity with an independent oracle and asserts the
  exact ICY byte sequence, including a metaint that does not divide the chunk size.
- The ring refuses an oversized record rather than relying on the caller's capacity formula;
  the formula is now a sizing rule, not an invariant the eviction loop assumes.
- Verified: unit and integration suites, both firmware builds. The metadata and hostile-name
  integration checks still pass byte-for-byte.

- Board check (Sunton, 2026-10-18): the metadata block builder's 4 KB stack buffer was
  reduced to 161 B - the most a 120-character title can need. This frame is on the broadcast
  task's stack, which is also the decoder's; the measured broadcast headroom is now
  17 556 B.
