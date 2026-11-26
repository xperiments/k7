# 04 — The player sweeps, asks and stores

**What to build:** The K7Player's side of the handover: the channel sweep, the request, the peer
that receives the encrypted record, the record's validation, and the preference order it then
runs — home first, the K7's own network second, then the 20 s / 10 s alternation.

**Blocked by:** 01 — The handover's frames and its key; 03 — `player/`, and a host harness.

**Status:** done — host tier, and the handover run between the two boards.

- [x] While it has no record it sweeps 1–13 in order at 250 ms a channel, and the sweep is a channel set and a wait, not a scan.
- [x] A beacon heard starts a request; the request is retried up to three times; a valid record is stored and a malformed one is refused whole.
- [x] The stored record survives a reflash, and no surface returns it.
- [x] The preference order and the 25 s / 5 tries ceiling are the K7's own numbers applied to the player.
- [x] While connected it never sweeps; while failing it alternates 20 s of retrying with 10 s of sweeping.
- [x] `k7.local` is resolved before the stored IPv4 is used.

Spec: `docs/specs/network-handover.md` H19–H22, H33–H37, T7–T10; `docs/specs/k7player.md` R16–R22.

## Comments

- **Where it is:** `player/components/player/player_net.c` is the policy; `player_board.h` grew the
  record store, the station, the handover radio, the channel, the curve and mDNS; the ESP32
  adapter implements them (ESP-NOW, PSA, NVS, `esp_wifi`, mdns); the host fake scripts every one
  of them. The record's blob is the handover's own bytes, loaded through `ho_record_decode`, so a
  record damaged in flash is no record at all and the player sweeps.
- **The board found a protocol bug the host could not.** The player heard the beacon, asked four
  times, and the K7 never answered: registering the K7 as the **encrypted** peer before sending
  encrypts the request too, and the K7 cannot derive the key until it has read that request. The
  fix is the order — plaintext registration, send, then encrypted registration for the answer —
  and it is now written into the spec as H24a, because both firmwares are separate
  implementations and this is a one-session bug for whoever writes the next one.
- **The harness was taught the property that broke.** The fake now records, per sent frame,
  whether the peer registration was encrypted at that moment, and `t02_net` asserts every request
  went out in clear. It passed while the bug was live; it does not any more.
- **Two machine bugs the test caught first:** the sweep advanced one channel per *tick* rather
  than following its schedule (a late tick lost a dwell, and with it the beacon a player would
  have heard), and the disconnect baseline was sampled *after* the join, so an attempt that fails
  inside `join()` looked like one that had never failed.
- **Evidence, host:** `t02_net` — **59 checks, 0 failures** (sweep order and dwell, the request and
  its three retries, a damaged record refused, the store, home-then-AP, the alternation's two
  phases, a drop, the record surviving a restart); `t01_status` — 15. `make -C host test layering
  player integration` green.
- **Evidence, boards:** with a Share window opened on the K7, the player's console read
  ```
  I (59622) net: heard a K7 on channel 1: asking
  I (59822) net: record stored: xperiments-2.4G and K7.local
  I (59832) net: joining xperiments-2.4G
  I (63732) net: the K7 is at 192.168.0.13
  I (63732) net: joined xperiments-2.4G at 192.168.0.24
  ```
  One ask, no retry; the record arrived **encrypted and decrypted**, which is only possible if both
  ends derived the same LMK; the home network was joined; the K7 was found by name. This is T11's
  exchange and T13's join; playing the stream is PL-05's.
- **Firmware:** `k7player.bin` is `0xca800` in the 1 MB app partition, 21 % free — the default
  partition table holds for now; A2DP (PL-08) is what will need its own.
- **`make -C host check`'s `label` step still fails in this environment**; everything before it
  passes.
