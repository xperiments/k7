# 02 — The K7's share window

**What to build:** The K7 side of the handover: a `Share` row in the Menu's Wi-Fi list, the 60 s
window behind it, a beacon every 200 ms on the station's channel while it is open, one answered
player, an ESP-NOW peer that exists only for that answer, and a network record assembled from NVS.

**Blocked by:** 01 — The handover's frames and its key.

**Status:** done — host tier, firmware build, and the K7's share window proven on the board (T15, T16, and the beacon heard by a second radio). The encrypted exchange still needs a player: PL-09.

- [x] `Share` reads `START`, then the countdown in seconds, then `STOP`, and `UNAVAILABLE` when the radio refused; it is the only trigger.
- [x] `esp_now_init()` on open, `esp_now_deinit()` on close; a device that never shares carries no ESP-NOW state — and no task either.
- [x] The window answers the first valid request's MAC, re-sends the record to it on a retry, ignores every other MAC, and closes at 60 s or on `STOP`.
- [x] The peer is added with the derived LMK and deleted when the window closes.
- [x] The record carries the home network when there is one, `K7.local` and the setup password always, `k7`, and the station's address.
- [x] A K7 with no home network opens a window and hands over only its own setup network.
- [x] The window opens no access point and leaves the setup network's state untouched.

Spec: `docs/specs/network-handover.md` H10–H18, H23–H32, T5–T6.

## Comments

- **Where the code is:** `components/portable/share/` (the window, the one-player policy, the
  record), the seam in `components/portable/app_board/include/app_board.h`, the radio in
  `components/app/board_esp32/board_esp32.c` (ESP-NOW plus PSA for X25519), the fake in
  `host/board_host.c`, the row in `components/portable/menu/`, and `share_init()` in the
  composition.
- **The receive path is split on purpose.** The adapter's callback runs on the Wi-Fi task and only
  copies the request; the answer — X25519, the peer, the record — happens on the window's own
  task, where there is stack for the curve.
- **The window's task exists only while the window does**, created by `share_start()` and deleted
  by itself when it closes: 6 KB of internal RAM is not reserved at boot for a feature a person
  uses ninety seconds a year (the same rule as P41). It leaves through the same lock `share_start()`
  takes, so a window opened during its exit cannot end up with no clock.
- **The spec gained one rule the implementation forced into the open.** H11 said the window "closes
  … after it has answered a player", which contradicts H25's retries: it now says the window stops
  **beaconing** once it has answered and stays open for that player's retries. ADR 0099's bullet
  says the same.
- **A model bug the change found, not caused:** `menu_facts_t` gained three fields, and the Menu
  copies and compares its facts field by field — so the new state was silently dropped until
  `facts_copy()` and `facts_same()` were taught about it. The device would have shown `START`
  forever. `t18_menu` now asserts the share row's three states and its action, and the Wi-Fi list
  is six rows that scroll.
- **Evidence:** `t42_share` — 109 checks, 0 failures, driving a fake clock and a fake radio,
  including the key registered for the player matching the one both keys derive, the second
  player ignored, the fourth answer capped, the record's contents decoded byte for byte, and the
  peer dropped on close. `t18_menu` — 647 checks. `make -C host test`, `layering` and
  `integration` are green.
- **Firmware:** `./tools/build.sh sunton build` completes; the image is **0x201130** in the 4 MB
  app partition, 50 % free. The build caught one real error the host could not: the wrong pointer
  type for `esp_wifi_get_channel()`'s second channel.
- **Not proven, and only a board can:** that a K7 with no home network at all — the setup network
  up, the station unassociated — sends ESP-NOW on its access point's interface, which is why the
  adapter picks `WIFI_IF_AP` when there is no address (T14). And the window's memory cost (T15).
- **`make -C host check`'s `label` step still fails in this environment** (headless Chrome's GPU
  process dies before any label assertion); unit, layering and integration pass.

## Board tier, 2026-11-18

Flash `./tools/build.sh sunton flash`: the S3 joins `192.168.0.13`, advertises `k7.local`, plays the
stream, and the share module initialises with nothing open.

| # | Result |
|---|---|
| T15 | **proven** — with the window open and the stream playing: `internal free 34355 B, largest 25600 B`. The build now also logs the figure *before* the window's allocations, so the cost is a subtraction; that pair belongs to PL-09's capture. |
| T16 | **proven** — the window opened at 46.8 s and logged `share window closed` at 106.7 s: **59.9 s**, closed itself, nothing left behind, no crash. |
| The beacon, on the air | **proven with an instrument** — a throwaway ESP-NOW sniffer on the second board (ESP32-D0WD-V3, `.scratch/sniff`) heard **23 beacons**: 36 bytes, version 1, type 1, from the K7's MAC `28:37:2f:87:e4:d8`, on **channel 1**, one key (`3028b927`) for the whole window, countdown 60→2 s. Two arrived **210 ms apart inside one 300 ms dwell**, which is the 200 ms cadence H14 asks for. Nothing was heard after the deadline. No other frame was on the air. |
| T11 | not yet — needs a player to send a request and decrypt the record (PL-09) |
| T14 | not yet — needs a K7 with no home network and a player to take `K7.local` (PL-09) |

The window has no remote trigger, so each of these needed a person to open it from the panel; the
instrument was already sweeping. The sniffer is transient (`.scratch/`) and is not the player.
