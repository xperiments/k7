# The network handover: a K7 gives a K7Player a network over ESP-NOW

**Goal.** A K7Player with no credentials is given the network its K7 is on, by the K7, without
anyone typing anything and without the household's password going over the air in clear. The K7
does it inside a window a person opens, for one player, and forgets the whole thing afterwards.

The decision and its alternatives are [ADR 0099](../adr/0099-the-network-is-handed-over-esp-now-in-a-window.md);
the relationship that makes the K7 forget is
[ADR 0098](../adr/0098-the-player-is-a-client-the-k7-keeps-no-record-of.md). The words are the
two glossaries: **handover**, **share window**, **sweep**, **beacon**, **network record** in
[`player/CONTEXT.md`](../../player/CONTEXT.md), and the network vocabulary the K7 already owns in
[`CONTEXT.md`](./CONTEXT.md#the-network).

**Status:** built, and proven over the air on **2026-11-18** (§7): a window opened on the K7, a
player whose record had been erased swept, asked, and stored the record, and it then joined the
home network and played. What the same run did not close: the AP fallback (T14), a sweep that has
to walk every channel (T12 — the K7 was on channel 1), and the window closing on its own (T16).
The handover has not been re-measured since the radio became always-on.

---

## 1. Scope

### In scope

| # | Requirement |
|---|---|
| H1 | A person opens a **share window** on the K7 from its Menu, and closes it early |
| H2 | While the window is open the K7 beacons a secret-free frame every 200 ms on its own channel |
| H3 | A K7Player with no record sweeps 1–13 and hears a beacon |
| H4 | The K7Player asks, and exactly one player is answered per window |
| H5 | The two derive a key by X25519 and the K7 sends the **network record** encrypted under it |
| H6 | The record carries the home network when the K7 has one, the K7's own setup network always, and the K7's hostname and address |
| H7 | The K7Player stores the record and prefers the home network to the K7's own |
| H8 | The window closes on its countdown, on a stop, or after a player is answered, and the peer is removed |
| H9 | Every behaviour above has a host test except the six things §7 marks board-only |

### Out of scope, explicitly

- **A registry.** The K7 keeps no MAC, no name and no record of who it has helped
  ([ADR 0098](../adr/0098-the-player-is-a-client-the-k7-keeps-no-record-of.md)).
- **Discovery after the join.** Once a player is on a network it resolves `k7.local` by mDNS and
  needs nothing from this protocol again.
- **Re-provisioning the K7 from the player.** The handover flows one way: the K7 has the network,
  the player does not.
- **A second player in one window.** A second window answers a second player.
- **5 GHz.** Neither radio has it.
- **Encryption of the beacon.** A beacon carries no secret by construction.

---

## 2. The pair, in one paragraph

The K7's radio is pinned to its station's channel — moving it would drop the broadcast off its own
network — and ESP-NOW is a same-channel protocol. A K7Player that has never been provisioned is
pinned to nothing. So the one that can afford to move does: the player sweeps, the K7 stays where
its network put it, and the two meet on the K7's channel. Everything else in this document is a
consequence of that sentence.

---

## 3. The share window (the K7)

**H10. One trigger, and it is a person's.** The Menu's Wi-Fi list gains a `Share` row: `START`
when no window is open, the countdown (`43s`) while one is, `STOP` to end it. It is the only way
to open a window, it is the same posture as the setup network's on-demand trigger
([`wifi-provisioning.md`](./wifi-provisioning.md), P9) and the setup page's window
([ADR 0077](../adr/0077-the-setup-surface-exists-only-while-its-hotspot-does.md)), and there is
no remote way to open one.

**H11. It lasts 60 s.** The countdown is the row's value and is recomputed from the deadline, not
decremented, so a slow frame does not stretch it. It closes on the countdown and on `STOP`. Once
it has answered a player it **stops beaconing** — the beacon's job is done — but stays open until
then, because the rest of the window is what answers that player's retries (H25).

**H12. It raises no access point.** The K7's setup network is a different window with a different
job; opening a share window does not raise `K7.local` and does not spend its memory. The measured
cost of the window is its beacon and ESP-NOW itself — the census is T15.

**H13. ESP-NOW is up while the K7 has a use for it, and the window borrows it.** The radio is
initialised once — by the window, or at boot when the player's audio travels over ESP-NOW
(`espnow-stream.md` E10) — and a window's close only tears it down when nothing else is using it.
With the audio on HTTP that is exactly the old rule: a boot that never opens a window carries
nothing for this feature. With the audio on ESP-NOW the radio is already up, and the window costs
its beacon and nothing else.

**H14. One beacon every 200 ms**, on the channel the radio is already on, to the broadcast
address. Five a second for at most 60 s is 300 frames, which is the whole airtime cost of a
window.

**H15. The beacon carries no secret**: a version, its type, an ephemeral X25519 public key, the
channel and the seconds left. A passive listener learns that a K7 in this room is sharing, and
nothing it can join a network with.

**H16. The ephemeral key is made when the window opens, and dies with it.** Two windows never
share a key, which is what makes a captured RECORD useless later without a replay mechanism.

**H17. A K7 with no home network opens a window too.** Its record then carries only its own setup
network, and a player paired to it joins `K7.local` directly — the mountain case that B7 of
[`wifi-provisioning.md`](./wifi-provisioning.md) already proved a client can stream over.

**H18. The K7's panel says it is sharing** — the row's countdown is the whole announcement. No
banner, no screen change, nothing on the strip: the Menu is the settings surface.

---

## 4. The sweep (the K7Player)

**H19. Sweep order is 1–13, dwelling 250 ms on each** — about 3.25 s a sweep. The dwell must be
longer than the beacon's period (H14) and is, by 50 ms: a visit to a channel is then guaranteed to
contain a beacon, where a dwell equal to the period would be a coin toss against the phase.

**H20. A player with no record sweeps until it is provisioned.** It has nothing else to do with
its radio.

**H21. A player with a record does not sweep while it is connected.** It cannot: the stream is on
the channel it is listening to. It sweeps only in the failing half of the recovery alternation —
**20 s retrying the network it has, then 10 s sweeping**, for as long as it is failing.

**H22. The sweep is not a scan.** `esp_wifi_scan_start()` walks the channels for access points
and returns a list; a sweep sets the channel (`esp_wifi_set_channel()`) and waits for a frame.
The distinction is the difference between finding a network and finding a device.

---

## 5. The exchange

Three frames, one direction each, in this order:

| # | From → to | Address | Encryption | Size |
|---|---|---|---|---|
| 1 | K7 → all | broadcast | none | 36 B |
| 2 | player → K7 | unicast | none | 34 B |
| 3 | K7 → player | unicast | ESP-NOW, LMK | ≤ 235 B |

**H23. A frame is at most 250 bytes** (`ESP_NOW_MAX_DATA_LEN`), which is the ceiling every field
belongs under. The longest record is 235 B (§ 6) and the fifteen bytes left are the margin, not a
reserve for a field anyone wants to add.

### 5.1 Beacon — 36 bytes

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | `version` = 1 |
| 1 | 1 | `type` = 0x01 |
| 2 | 32 | the window's X25519 public key |
| 34 | 1 | the channel the radio is on |
| 35 | 1 | seconds left in the window |

The sender's MAC is the frame's source address, which ESP-NOW hands the receiver in
`esp_now_recv_info_t.src_addr`; putting it in the body would be the same fact twice.

### 5.2 Request — 34 bytes

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | `version` = 1 |
| 1 | 1 | `type` = 0x02 |
| 2 | 32 | the player's X25519 public key |

**H24. It is not the player's MAC either**, for the same reason: the K7 replies to the address the
frame came from, which is also the address it would register.

**H24a. The request is the one frame that must travel in clear.** The K7 derives the key from the
request's public key, so an encrypted request is one it cannot read — and an implementation that
registers the peer *encrypted* before sending encrypts it, because ESP-NOW's `encrypt` flag applies
in both directions. Found on the boards, not on paper: the player heard the beacon, asked four
times, and the K7 never answered until the request was sent between a plaintext registration and
the encrypted one.

**H25. The first valid request to arrive is the one answered.** The K7 records that MAC for the
rest of the window and re-sends the RECORD to it on a repeat request, up to three times, so a
lost RECORD costs a retry rather than a new window. A request from any other MAC is ignored until
the window closes — that is what "one player per window" means in practice.

**H26. A request that arrives after the window closed is ignored**, not answered and not counted.

### 5.3 The key

**H27. LMK = HKDF-SHA256(ikm = X25519(own private, peer public), salt = empty, info =
`"k7-handover-v1"` ‖ k7_public ‖ player_public, length = 16).** The transcript is fixed in that
order — the K7's key first — so both sides derive the same value without negotiating an order,
and a key from another window or another pair does not decrypt. The `info` is therefore always
78 bytes: 14 for the label, then the two 32-byte keys. An absent salt is RFC 5869's 32 zero
bytes, not an empty HMAC key. The curve is each firmware's own call; everything after the shared
secret is one module both firmwares compile, so the derivation has one implementation and one set
of test vectors.

**H28. A shared secret that is all zeroes is refused** by both sides and the exchange ends: that
is the low-order-point answer, and it is not a network record.

**H29. Both sides use the default PMK** and neither calls `esp_now_set_pmk()`, so the LMK is the
only per-peer secret and there is one less thing for the two firmwares to disagree about.

**H30. Each side registers the other and nothing else.** The K7 calls
`esp_now_add_peer(src_addr, lmk, encrypt = true)` and deletes it when the window closes (H8). The
player registers the K7 — from the beacon's source address — *after* it sends its request, so the
window for the RECORD to arrive is opened by the thing that asked for it.

### 5.4 Record — 3 + the fields + a checksum, at most 235 bytes

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | `version` = 1 |
| 1 | 1 | `type` = 0x03 |
| 2 | 1 | `flags`: bit 0 = the home network is present, bit 1 = the setup network is present |
| 3 | 1 + n | `home_ssid_len` then the SSID, 1–32 bytes |
| … | 1 + n | `home_pass_len` then the password, 0–63 bytes (0 = open network) |
| … | 1 + n | `ap_ssid_len` then the SSID, 1–32 bytes |
| … | 1 + n | `ap_pass_len` then the password, 8–63 bytes |
| … | 1 + n | `host_len` then the hostname, 1–32 bytes |
| … | 4 | the IPv4 address, big-endian |
| … | 1 | `crc8` — CRC-8/ATM, polynomial 0x07, init 0x00, no reflection, no final xor — over every byte before it |

The longest legal record is `3 + (1+32) + (1+63) + (1+32) + (1+63) + (1+32) + 4 + 1` = **235
bytes**. A field presents only when its flag bit is set; a flag bit without the length that
matches is a malformed record and is refused whole.

**H31. The record is written down, not computed, on the K7.** The home network is the one in NVS
when there is one (`wifi_record`'s `rec`), the setup network is `K7.local` and the setup password
(`ap`, `12345678` when absent), the hostname is `k7` and the address is the station's when it has
one, `192.168.4.1` when it does not.

**H32. The password fields never leave the K7 again.** No route, no panel and no log returns
one — the same rule as [`wifi-provisioning.md`](./wifi-provisioning.md), P17.

---

## 6. The record's meaning (the K7Player)

**H33. The player prefers the home network and falls back to the K7's own**, in that order, both
being in the record it holds. The preference is a policy in the player's firmware, not a mode a
person selects, and it is the same order every boot.

**H34. "The home network failed" is 25 s or five join attempts**, whichever comes first — the
K7's own ceiling for an attempt ([`wifi-provisioning.md`](./wifi-provisioning.md), P29) applied
to the player, so a wrong password is not retried forever.

**H35. The setup network is at `192.168.4.1`, always** — the K7's access point is built with that
address (P8), so the player needs no field to find it. The record's IPv4 is the home network's
address and is used for that network only.

**H36. The hostname is resolved with mDNS before the IPv4 is trusted.** A K7 whose DHCP lease
moves is found by `k7.local`; the address is the fallback for a network whose mDNS does not
answer. A player that has neither sweeps (H21).

**H37. The record is stored only after it validates**: version 1, the flags consistent with the
lengths, a non-empty SSID for every network it claims, and the CRC matching. A record that fails
any test is discarded and the player stays in `Searching`; a half-written record is not a record.

---

## 7. Tests

Host tests run the portable cores of both firmwares — the K7's window state machine with a
scripted radio, and the player's with a scripted beacon — so neither needs a board to be shown
wrong. The two X25519 sides are the same module, tested against a fixed key pair and a known
answer.

| # | Test | Tier |
|---|---|---|
| T1 | Beacon, request and record encode and decode round-trip; a truncated frame, an unknown version and an unknown type are all refused | host |
| T2 | The longest legal record is 235 B and the encoder asserts the 250 B ceiling rather than trusting it | host |
| T3 | Both sides derive the same LMK from a fixed pair of keys, and a different transcript gives a different LMK | host |
| T4 | A zero shared secret is refused | host |
| T5 | The window: opens on the Menu's `START`, beacons at 200 ms, answers the first MAC, ignores a second, re-sends to the first on a retry, closes at 60 s or on `STOP` | host |
| T6 | The window's peer is added on answer and deleted on close | host |
| T7 | The player: sweeps 1–13 in order at 250 ms, asks, retries the request up to three times, and stores a valid record | host |
| T8 | The player refuses a record whose flags, lengths or CRC disagree, and keeps `Searching` | host |
| T9 | The preference order: home, then the setup network, then the recovery alternation | host |
| T10 | A replayed RECORD from an earlier window does not decrypt under a new window's key | host |
| T11 | Two boards: a window opened on one, a real sweep on the other, a record exchanged over the air | **board only** |
| T12 | A K7 on each channel 1–13 is found by the sweep (the channel walk is real, not arithmetic) | **board only** |
| T13 | A provisioned player joins the home network and plays the stream | **board only** |
| T14 | An unprovisioned K7 hands over `K7.local` and the player streams from `192.168.4.1` | **board only** |
| T15 | The census with a window open: internal RAM, largest block, and the encoder's frame cost beside the boot log's | **board only** |
| T16 | A window left to expire closes itself and the peer list is empty afterwards | **board only** |

**Measured on the board, 2026-11-18**, with a WROOM-32 and a PCM5102A, the K7 on `192.168.0.13`,
and the player's network record erased so the handover had to happen:

| # | Result |
|---|---|
| T11 | **proven** — `share window open for 60 s on channel 1`; the player logged `heard a K7 on channel 1: asking`; the K7 `answered 3c:8a:1f:7f:f5:c8`; the player stored it — `record stored: xperiments-2.4G and K7.local` |
| T13 | **proven** — `joined xperiments-2.4G at 192.168.0.24`, and the light reported `playing (net connected, stream streaming, audio 1, failed 0)` |
| T15 | **proven for the census** — the window costs **6 624 B** of internal RAM (56 695 → 50 071 B) and the largest free block stays **31 744 B** throughout; the player's boot heap is 201 020 B free, 177 212 B with the stream, decoder and ring up. The encoder's frame cost beside the boot log's was not read in this run, so §8.1 stays open |
| T12 | **not proven** — the K7 was on channel 1, so the walk across 1–13 was not exercised |
| T14 | **not proven** — the AP fallback: an unprovisioned K7 handing over `K7.local` |
| T16 | **not recorded** — the run opened a window for 60 s and did not log whether it closed itself |

The player is an ordinary listener from the K7's side — `client 0 connected`, no special case — and
the handover has not been re-measured since the radio became always-on.

---

## 8. Open questions

1. **Does ESP-NOW cost the encoder anything?** The S3's encoder already holds 41–60 % of a core.
   ESP-NOW's beacons are five frames a second, which is nothing; whether the coex arbiter
   perturbs the encoder's worst frame is T15's question, and the answer goes in the README.
2. **How long is a sweep in the presence of a hidden K7?** H19's arithmetic assumes the player's
   radio really is on the named channel for the dwell. If `esp_wifi_set_channel()` costs an
   association teardown on the player, the dwell is longer than stated and the sweep should be
   measured (T12).
3. **Should the player keep a captured beacon's key between sweeps?** It does not today: each
   request makes a fresh key pair. Keeping one per sweep would save a derivation per channel; it
   is one millisecond of arithmetic, not worth a state.
