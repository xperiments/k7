# The network is chosen on the device's setup network, not compiled in

The stream server's first requirement is that the device join Wi-Fi "using credentials compiled
into the source" (`docs/specs/sd-stream-server.md`, R1), and the same document puts any runtime
mechanism for changing them out of scope, with the deferral recorded in its open questions. That
was right for a device whose only user was the person flashing it. It is wrong for a device that
is sold, and the two costs are not symmetrical:

- a network change is a rebuild, a USB cable and a laptop with ESP-IDF on it — for a password
  typed into a router's admin page;
- a device that cannot join has **no way to be told anything**, because ADR 0067 removed the last
  page it served and left every unknown path a 404. The failure mode of a mistyped password is a
  speaker that looks dead, and the recovery is `esptool erase_flash`.

ADR 0067 retired the Launcher because the App reaches the device directly over Local Network
Access, and because a page that "repeats what the App's status line and the panel already say"
was 1.25 MB of partition and a build step for nothing. Both reasons hold. Neither covers the one
page the App *cannot* reach: when there is no network, there is nothing to reach the device over,
and a browser that has never visited the App has no cached shell to fall back to.

## Decision

- **The credentials live in NVS, and there is no compiled copy.** One record — the home network's
  SSID, its password, and the setup password — written as one blob in the device's own namespace.
  `components/app/board_esp32/wifi_credentials.h`, its `.example`, its `__has_include` and its
  `#warning` are deleted. No record means **unprovisioned**, which is a state, not an error.
- **The device owns a setup network, named `K7.local`**, serving the same listener at
  `192.168.4.1`. It is up in three cases and only those three: at boot when no network is stored,
  at boot when no association was made within 20 s, and on demand from the Menu's Wi-Fi list. It
  closes 60 s after a successful join, and it **never** appears in response to a mid-session drop
  — a router reboot is not a setup session, and a deauth attacker does not get to raise an AP.
- **The device serves exactly one page, `GET /setup`**, on every interface. `/` still redirects to
  the App on every interface: 0067's decision stands, and a cached App is how a person in the
  mountains is offered the stream. The page exists because provisioning is the recovery path, and
  a recovery path must not depend on a cached shell, on Chrome, or on a permission grant.
- **The page is firmware, not the App.** A hand-written page in portable rodata, served by the
  existing route table. Putting the form in the App would make the device's recoverability a
  question about browser state.
- **The setup network carries the whole device surface.** It is a mirror, not a filtered subset:
  `/stream`, `/api/*` and `/mcp` answer on it exactly as on the LAN. The alternative costs the
  same peer-address machinery and makes the page unreachable from the LAN while the device is
  healthy. The honest framing that makes this consistent: **the setup password is a privacy
  curtain, not a security boundary** — WPA2-PSK is readable by anyone who joins — and the device
  is already `Open` on its LAN (see the glossary).
- **The join is applied live, and the attempt is recorded.** `esp_wifi_disconnect` →
  `set_config` → `connect`, no reboot, because a reboot costs the panel, the card scan and the
  page that would report the failure. The outcome — network, result, disconnect reason, tries —
  is stored so that a browser dropped mid-flow learns on reload that the password was wrong
  (reason 15) rather than being handed a clean form.
- **Five attempts, then stop.** A wrong password retried forever looks identical to a slow
  network; five is enough to ride out a channel change and few enough to be diagnosable.
- **The logic is portable and the host proves it.** The record's codec, the validation, the scan
  JSON, the page and the state machine live behind the `app_board` seam, so `make -C host check`
  fetches the page, rejects a 7-character password and walks a failed attempt to a successful
  one. `esp_wifi`, `nvs.h` and LVGL stay in the board adapter.

## Considered options

- **Keep the compiled header as a factory seed.** It preserves the developer's flash-and-go loop,
  and it puts a shipping binary's builder password in rodata forever. `idf.py flash` does not
  erase NVS, so the convenience is worth one provisioning round-trip per blank board.
- **A build-time Kconfig string**, which is what the stream server spec offered as the natural
  follow-up. Same rebuild, same leak, less obvious.
- **The setup UI inside the cached App.** It is the cheapest UI to build and it is the one a
  person reaches through `/`; it also makes provisioning depend on a warm cache, Chrome, and the
  `local-network` grant. The device would be unconfigurable from a fresh laptop and from a phone.
- **A captive portal.** It auto-opens the page, which is the best first-run experience available.
  It costs a stub DNS responder answering every name with 192.168.4.1, per-OS probe documents, and
  a fight with the device's own origin guard. The panel prints the SSID, the password and the
  address instead, and the page is reachable by typing.
- **An always-on AP+STA.** Nothing is ever locked out. It spends the stream's airtime and the
  device's scarcest internal RAM for the life of the device, and leaves a published-password AP
  in the air permanently. The Menu row recovers the same ground on demand.
- **Setup-only routes on the AP.** Tidier against 0067, identical machinery, and it strands the
  one owner who is on the LAN and wants to reconfigure a working device.

## Consequences

- **A blank board cannot serve until someone joins `K7.local`.** No compiled fallback, no serial
  path. That is the trade, and it is the feature working as intended rather than a gap.
- **The home network's password now sits in plaintext in NVS.** There is no flash encryption and
  no NVS key partition; the spec says so rather than leaving it implied, and the threat model is
  unchanged — a device in hand was always a reflash.
- **The panel is the setup password's only recovery**, so a device with a dead panel and a changed
  setup password needs `erase_flash`. Written into the spec's consequences, not a footnote.
- **`app_board_wait_online()` stops being an infinite block.** It waits for the boot window and
  reports through a callback, because in setup mode the appliance must still reach its update-file
  check and the panel must still come up.
- **`HTTP_MAX_ROUTES` grows** (20, with nineteen registered) and the page's bytes join the app
  partition. The comment above that constant records the last time a registration failed for
  exactly this reason.
- **A scan interrupts the stream for about two seconds**, and a successful join moves the AP to
  the home network's channel, which can drop the browser mid-flow. The attempt record is what
  makes both survivable, and the page says so.
- **Revisit if** the memory census with the setup network up does not fit — the documented lever is
  `SPIRAM_MALLOC_ALWAYSINTERNAL` at 4096, which buys ~120 KB of internal RAM for +7.6% of one
  core — or if Chrome's `local-network` behaviour or PWA installation changes the App's
  reachability story.

**Status:** designed; specified in `docs/specs/wifi-provisioning.md`. Nothing is built: the
tickets are on the board under the Setup group.

## Supersedes

[ADR 0067](./0067-chrome-only-direct-to-the-device.md) — **its "the device serves no page" clause
only.** Its decision, that the App fetches `http://k7.local` directly and is gated on the
`local-network` permission, stands unchanged; `/` still answers 302 to the App on every interface,
and the Launcher stays retired.

It also retires **R1** of `docs/specs/sd-stream-server.md` ("credentials compiled into the source")
and that document's exclusion of "any runtime mechanism for changing Wi-Fi credentials" — not an
ADR, so it is amended in place, pointing here.
