# Wi-Fi provisioning: the setup network, the page, and the join

**Goal.** A device with no network is not a dead device. It raises its own access point, named
**`K7.local`**, serves one page at `http://192.168.4.1/setup`, and takes a **home network** and its
password from a person standing in front of it. The network is chosen once and stored; it is never
compiled in, and changing it never needs a rebuild.

The decision and its alternatives are [ADR 0076](../adr/0076-the-network-is-chosen-on-the-devices-setup-network.md).
The words are [`CONTEXT.md`](./CONTEXT.md), under **The network**. The routes join the front door's
listener and its access model, so [`front-door.md`](./front-door.md) and
[`sd-stream-server.md`](./sd-stream-server.md) are amended where they claimed the device had no
page and no runtime credentials.

**Status:** designed and specified; **nothing is built.** Every number here that is a memory
number is arithmetic until the board pass in §10 says otherwise, and this document must not be
read as proof that the access point fits. The tickets are on the board under the **Setup** group.

---

## 1. Scope

### In scope

| # | Requirement |
|---|---|
| P1 | A device with no stored credentials raises the **setup network** and serves the **setup page** |
| P2 | A device that cannot reach its stored network at boot raises the setup network instead of idling |
| P3 | The setup page lists what the radio can see, and takes a network and its password |
| P4 | A submitted network is joined live, without a reboot, and the **join attempt** is recorded |
| P5 | The setup password is editable, and recoverable from the panel |
| P6 | The credentials survive a reflash (`idf.py flash` does not erase NVS) |
| P7 | Every behaviour above has a host test, except the four things §10 marks board-only |

### Out of scope, explicitly

- **A captive portal.** No stub DNS, no per-OS probe documents, no auto-open. The panel prints the
  SSID, the password and the address; the person types them.
- **More than one network.** A second network is the same flow, not a preference list to debug.
- **An access point that stays up.** See §2.
- **Any change to the App.** `/setup` is deep-linkable from the App if it ever wants to link to it.
- **Hidden-SSID scanning.** Hidden networks are joined through the page's "Other network" row.
- **Enterprise (WPA2-802.1X) networks.** PSK and SAE only, which is what `esp_wifi` is configured
  for here.

---

## 2. The setup network

**P8.** The setup network is a WPA2 access point whose SSID is the literal string **`K7.local`**
(capital K, one dot, ≤32 bytes), serving the ordinary listener at **`192.168.4.1`** on port 80.
The device's mDNS hostname stays **`k7`**, so `http://k7.local/setup` resolves to the same page on
any client whose mDNS works; `192.168.4.1` is the path that works everywhere, and the panel names
both.

**P9. It has exactly three triggers, and no others:**

| Trigger | When |
|---|---|
| Unprovisioned boot | No record in NVS. The setup network comes up before the join is even attempted |
| Unreachable at boot | A record exists and no association is made within **20 s** of the boot |
| On demand | The Menu's `Settings → Wi-Fi → Setup` row (the panel's label for the setup network), any time the device is running |

**P10. A mid-session drop never raises it.** A device that has been associated and then loses its
network — a router rebooting, a deauth, a moved microwave — retries forever in silence with the
existing throttled log, and the setup network stays down. Raising an AP on every disconnect would
turn a 30-second router reboot into a setup session and hand a deauth attacker a way to force the
device to broadcast an access point. A device whose network was permanently changed is recovered
by power-cycling it, which lands on the "unreachable at boot" trigger.

**P11. It closes 60 s after a successful join**, and the closing is announced: the page counts it
down in words, and the panel's `Setup` row returns to `START`. A session raised on a device that is
**already** associated never starts that grace: nothing has joined, and the person is reconfiguring a
working device. (Found by doing exactly that on the board — the first build closed the hotspot while
they were still choosing.) It also closes when a person taps
`STOP` on the panel or `Stop setup` on the page. It does **not** close while a join attempt is
failing — the person is still there.

**P12. It is not filtered.** `/stream`, `/api/*`, `/mcp`, `/library` and `GET /` answer on it
exactly as on the LAN. The setup password is a curtain, not a boundary (§8).

**P13. mDNS is not restarted for it.** `app_board_mdns_start()` runs after
`app_board_wait_online()` as it does today; in setup mode that call returns when the boot window
closes, so `k7.local` and `_mcp._tcp` are advertised on whichever interface is up. A record for an
interface with no address is never published.

**P14. Channel.** With no station the AP boots on its own channel; once the station associates the
AP follows the station's channel, which can drop a client that is browsing the setup network
mid-join.
That is why P30 records the attempt rather than trusting the page's connection.

---

## 3. The setup page

**P15.** `GET /setup` answers `text/html; charset=utf-8` with one hand-written page, held in
portable rodata, **no external asset, no webfont, no framework, no build step** — **7520 bytes** measured (the page a browser on the hotspot fetched)
of HTML with an inline style block and the script that drives the five routes. It is served on every
interface, including the LAN, so a person on the network can reconfigure a working device without
walking to it.

**P16. What it shows, in order:**

1. The state in one line: *unprovisioned* → "Choose the Wi-Fi network this K7 should join";
   *setup* → the same, plus the last attempt's outcome when there is one; *joining* → "Joining
   *network*…" with the form inert, because the request is open; *joined* → "Connected to
   *network* at *address*. This setup network closes in N seconds."
2. The **picker** — the scan's result, one row per network, the home network marked, each with its
   name, signal and a lock when it is secured.
3. The **password** field, with a show/hide toggle, always empty. An open network hides it and says
   "open — no password".
4. **Other network** — a name field and a password field, for a hidden SSID or one the scan missed.
5. **Setup password** — a new-password field and a `Set` button, with the sentence "takes effect
   the next time the setup network starts". Empty means keep.
6. The footnote: *"The radio only sees 2.4 GHz networks. A 5 GHz-only network will not appear here
   and cannot be joined."*

**P17. It never pre-fills a password and no route ever returns one** — not the network's, not the
setup one. The setup password is shown on the panel, where the person physically is. A read-only
field that lies about being write-only is worse than an empty one.

**P18. The scan is one blocking request** (`GET /api/setup/scan`), bounded by the scan's own
configuration — about 1.6 s of airtime for the band — and **one at a time**: a second request while
one is running is a `429`, because the radio is doing the work and a queue of scans would only make
the first slower. The page shows "Scanning…" rather
than an empty list that fills itself later. A scan costs about two seconds of airtime — up to 13 channels
at ~120 ms — and briefly interrupts an associated station and the stream; the page says so when the
device is already joined.

**P19. One row per network, not per BSSID.** Access points sharing an SSID collapse to the
strongest, and the list is sorted by signal, descending.

**P20. Open networks are joinable.** The picker says so and the password field disappears. The
device does not refuse a network its owner asked for.

---

## 4. The routes

**Seven** registrations on the existing listener. The route table's ceiling (`HTTP_MAX_ROUTES`, 20)
grows to **28**; nineteen were registered before this change and twenty-six are after it, and the
comment above that constant records the build that failed because a registration did not fit.

| Route | Method | Body | Answer |
|---|---|---|---|
| `/setup` | GET | — | the page, `text/html` |
| `/api/setup` | GET | — | the state, the stored network's SSID, the setup network's address, the last attempt |
| `/api/setup/scan` | GET | — | the picker's list |
| `/api/setup/join` | POST | `{"ssid","password"}` | the outcome, or a refusal (blocks ≤25 s) |
| `/api/setup/ap` | POST | `{"on":true\|false}` | `204` |
| `/api/setup/ap-password` | POST | `{"password"}` | `204`, applied at the next start |
| `/api/setup/forget` | POST | `{}` | `204`: the network is erased, the setup password is not |

**P21. `GET /api/setup` answers:**

```json
{"state":"joined","network":"xperiments-2.4G","address":"192.168.0.44",
 "hotspot":{"ssid":"K7.local","address":"192.168.4.1","up":false,"failed":false},
 "closes_in_ms":0,"last_attempt":null}
```
and, on a failed attempt, `"state":"setup"` with
```json
{"last_attempt":{"ssid":"xperiments-2.4G","ok":false,"reason":15,"tries":5}}
```

`state` is one of `unprovisioned`, `setup`, `joining`, `joined`. The setup password is never a
field, and neither is any password.

**P22. Refusals are specific, and every one names what to do:**

| Condition | Answer |
|---|---|
| A 7- or 64-character password, or a 0- or 33-byte SSID | `400` naming the rule |
| A join submitted with no setup session | `409`, "start it: Menu → Settings → Wi-Fi → Setup" |
| A scan already running | `429`, "a scan is already running" |
| A body that is not `application/json` | `415`, by the existing body rule |
| A join while one is already in flight | `409` |

**The join request carries its own answer.** `POST /api/setup/join` blocks until the attempt
resolves — associated, or five failures — and answers `200` with either
`{"ok":true,"address":"192.168.0.44"}` or `{"ok":false,"reason":15,"tries":5}`. One request, one
answer, no polling loop, in the same shape as the scan. The record is still there for the case the
answer never arrives (§6, P30), which is the only case the channel move can spoil.

**P23. The setup routes inherit the device's existing protections**: they require
`application/json`, so a cross-origin form cannot drive them; only `/mcp` validates `Origin`, so
this is the same posture as the rest of `/api/*` and not a new gap. The page calls them from its
own origin, so no CORS header is involved.

---

## 5. The record

**P24.** One blob in NVS, in the device's own namespace — not ESP-IDF's `WIFI_STORAGE_FLASH` copy.
`esp_wifi_set_storage(WIFI_STORAGE_RAM)` is called explicitly so that the flash holds the password
**once**, in one format, and our record is the only source of truth. A device that ran an earlier
build already has that copy: the first boot after this change erases ESP-IDF's `nvs.net80211`
namespace, because a leftover copy is still a password on the flash. The record is written before
a join is attempted, so a power cut in the middle cannot leave a half-provisioned device.

**Forgetting.** The record is also erased on request: `POST /api/setup/forget` drops it, the radio
stops retrying it, and a device left with no record raises the setup network again (P9). The setup
password is its own key (P25) and survives — a device can forget a network and keep its hotspot's
password, which is the reason the two are not one blob. Without this, a failed attempt (whose choice
is stored first, above) leaves a device chasing a network nobody wants at every boot.

**P25. Two keys in that namespace, not one blob.** `rec` holds the home network — magic `K7WF`,
format version, the SSID (≤32 bytes), its password (≤63) and the lengths. `ap` holds the setup
password as an ordinary string. They are separate because a device can have a setup password
before it has a network at all — the page's password field is reachable on a device that has never
joined anything — and because "unprovisioned" must keep meaning exactly one thing. Absent,
wrong-magic or wrong-version `rec` reads as **unprovisioned**; an absent `ap` means `12345678`.

**P26. The record is plaintext.** No flash encryption, no NVS key partition. A person with the
device can read the home network's password out of the flash; they can also reflash it. Stating
it is the point.

**P27. Validation happens at the edge and in the portable codec**, so the page, the route and
the host test agree: SSID 1–32 bytes with no control characters; a network password is empty
(open) or 8–63 printable ASCII; a setup password is 8–63 printable ASCII.

---

## 6. The join flow

**P28. Applied live, never by rebooting.** `esp_wifi_disconnect()`,
`esp_wifi_set_config(WIFI_IF_STA, …)`, `esp_wifi_connect()`. A reboot costs the panel, the boot
mark, the card scan and the page that would report the failure.

**P29. Five attempts, or 25 s, and the attempt is over.** A disconnect is retried up to five
times, and `POST /api/setup/join` stops at 25 s whichever comes first; the recorded attempt carries
the tries it reached, so a report of "4 tries" is the ceiling and one of "5" is the cap. A wrong
password retried forever is indistinguishable from a slow network. Measured on the board: a real
wrong password produced four disconnect events inside the 25 s window — the auth timeout is about
six seconds a cycle — so the ceiling is what usually ends the attempt, and the answers still name
the reason (`15`, "wrong password").

**P30. The attempt is recorded, and it outlives the browser's connection.** The join attempt — the
SSID, the result, the last disconnect reason and the number of tries — is stored, and
`GET /api/setup` returns it. If the AP moved to the home network's channel and dropped the page
mid-flow, the person rejoins the setup network and reloads, and the page opens with **"Could not join
*xperiments-2.4G*: wrong password (reason 15), 5 attempts"** instead of an innocent-looking form.
Telling the person *why* is what this page is for.

**P31. Reasons are surfaced in the panel's words, not only in ESP-IDF's:** 15 wrong password, 201
no access point found (out of range, or a 5 GHz-only network), 205 the setup network's own
access point was
dropped, 2/4 authenticating/failing (the router refused the device), anything else "join failed
(*n*)". The mapping lives in portable code and is host-tested.

**P32. A station that associates on its own while a session is up counts as joined**, so a device
whose stored network comes back does not sit in setup mode. The grace timer starts, and the page
says so on its next reload or in a join's answer.

**P33. A successful join stores the record first**, then starts the grace timer, then reports.

---

## 7. The panel

**P34.** The Menu gains one row and one list — the existing model, a five-row window, `MENU_ROWS`
5, `MENU_LABEL_MAX` 96, `MENU_VALUE_MAX` 32. A new glyph for `Wi-Fi` joins `cassette_icons.h`, in
the strip's own style, and `MENU_ICON_WIFI` joins the closed enum.

```
Settings
├─ Backlight   Station   Library   Wi-Fi ›   About
   Wi-Fi
   ├─ Setup              action   START / STOP
   ├─ Network            value    the stored SSID, or none
   ├─ Password           value    the setup password, in clear
   ├─ Reset password     action   RESET
   └─ Address            value    192.168.4.1 while up, — while down
```

**P35. A setup network that cannot start says so in the row** — `UNAVAILABLE` — and the firmware retries
every 5 s with a throttled log line, rather than giving up. The retry is the same posture as the
station's reconnect loop: a transient failure recovers, and a permanent one is diagnosable on the
panel instead of looking like a device with no setup mode. The row says `UNAVAILABLE` continuously
between retries, not for only part of each one.

**P35a. Raising the access point never happens under the machine's lock.** `esp_wifi_set_mode()`
restarts the radio and takes a few hundred milliseconds, and the panel asks for a status copy
twenty-five times a second; a lock held across the start freezes the glass at the moment someone
presses Setup. The state is read and written under the lock on either side of the call, and two
callers racing is harmless because starting an access point that is already up is not an error.
`t38_setup` measures it: a status copy completes in under 150 ms while the fake radio spends 400 ms
starting.

**P36. Nothing else on the device announces setup mode.** Not the cassette screen, not the header,
not the strip — the strip carries the broadcast's colour under ADR 0074 and 0075's power cap, and
an AP indicator would have to compete with the signal it was built for. The Menu is the settings
surface; that is what it is for.

**P37. The panel is the setup password's only recovery channel.** A device with a dead panel and a
changed setup password is recovered with `esptool erase_flash`, which also clears the home
network. This is the cost of P5 and it is accepted, not hidden.

---

## 8. Access model

**P38.** The setup network carries the whole device surface (§2, P12), and the setup page is
reachable from the LAN as well. So `/setup` is an unauthenticated page that can point the device at
a rogue access point or lock it off its own network. Anyone who can already reach port 80 owns the
broadcast, so this is not a new *privilege* — but it is a new *capability*, and it is stated here
beside the access model's existing admissions rather than discovered later.

**P39. The setup password is a privacy curtain, not a security boundary.** It keeps a neighbour
from casually joining the speaker's setup network. It defends nothing against a person who can reach the
device's LAN, and WPA2-PSK is readable by any client that joins. This is why it is shown on the
panel and why it is not pretended to be more than it is.

---

## 9. Memory and airtime

**P40. The memory census with the setup network up and the stream playing is a requirement, not
an intention.** The setup network's allocation lands at the tightest moment the device has: after
`esp_wifi_init()` took its contiguous DMA buffers and after the encoder holds ~100 KB of internal
state — the largest free internal block is **18 432 B** and 35 327 B is free (README). The boot log
records the census, and the README carries the number next to the existing one. Until that number
exists, this feature is not accepted.

**Measured on the board, 2026-11-10:** the access point starts at 1884 ms with `internal free
123183 B, largest 49152 B`, and ten seconds later — the stream playing — `internal free 43815 B,
largest 31744 B`. Both are **better** than the pre-change worst case (35 327 B free, 18 432 B
largest) because the measured device was unprovisioned: with no station association its lwIP state
is not in the pair. The station-associated case, and an on-demand hotspot beside it, are still
unmeasured and are B4's remaining question.

**P41. If it does not fit, the documented lever is applied rather than a new one invented:**
`SPIRAM_MALLOC_ALWAYSINTERNAL` from 131072 to 4096, which moves the encoder's 95 KB to PSRAM and
takes internal free to 154 643 B for **+7.6 %** of one core (41.4 % → 44.5 %). The AP's buffers are
not reserved at boot "just in case": that spends the memory on every device forever for a mode used
ninety seconds a year, and hides the real cost from the census that is supposed to tell the truth.

**P42. Airtime.** Beacons every 100 ms on the station's channel, and a scan at ~2 s. The soak runs
with the setup network up and the stream playing, and the encoder's logged frame cost must stay in its
recorded band with zero rebases.

---

## 10. Tests

Host tests run in `make -C host check` against `host/board_host.c`'s scripted fake: a scan list the
test sets, a join the test makes fail with a reason, and a clock the test advances.

**B1, B2, B7 and B3 have a harness**: `sh host/tests/board_setup.sh --home-pass` joins this
machine to the hotspot, runs them from the far side, and puts the interface back. It cannot run B6
or B8 — a cable and eyes are the only instruments those have.

| # | Test | Tier |
|---|---|---|
| H1 | `GET /setup` answers 200 `text/html` and the body contains the picker, the password field, the "Other network" row and the 2.4 GHz line | host |
| H2 | `GET /api/setup` with no record says `unprovisioned`, and with a record names the SSID | host |
| H3 | The scan list collapses two BSSIDs of one SSID to the stronger, sorts by signal, flags open networks, and never carries a password | host |
| H4 | A join with a 7-character password is a 400 naming the rule; a 33-byte SSID likewise | host |
| H5 | A join with no session is a 409 naming the Menu path; a second concurrent join is a 409 | host |
| H6 | A failed join (fake reason 15) is reported by `GET /api/setup` as `last_attempt`, with the try count, and the page renders it | host |
| H7 | A successful join writes the record, starts the grace timer, and the state reports `joined`; the clock's 60 s later reports no session | host |
| H8 | A new setup password is accepted, validated, never returned by any route, and applied only at the next session | host |
| H9 | `GET /` still answers 302 to the App while the setup network is up, and `/setup` is not shadowed by it | host |
| H10 | Five failed attempts stop the retries and the state says so | host |
| H11 | The record's codec round-trips, and wrong magic/version/short blobs read as no record | host |
| H12 | The reason mapping names 2, 4, 15, 201, 205 and the fallback | host |
| H13 | Two scans at once: one answers, the other is a 429 | host |
| B1 | Unprovisioned boot: the setup network appears, `192.168.4.1/setup` and `k7.local/setup` both answer, the panel shows the password | **board only** |
| B2 | A wrong password reports reason 15 on reload after the channel move | **board only** |
| B3 | A successful join closes the setup network after 60 s and the App then reaches the device on the LAN | **board only** |
| B4 | The memory census with the setup network up and the stream playing, recorded in the README | **board only** |
| B5 | The soak with the setup network up: no rebases, frame cost within band | **board only** |
| B6 | `idf.py flash` preserves the record; `erase_flash` returns the device to `unprovisioned` | **board only** |
| B7 | A client on the setup network plays `http://192.168.4.1/stream` — the mountain case | **board only** |
| B8 | The Menu starts and stops the setup network, restores the default password, and shows `UNAVAILABLE` when it cannot start | **board only** |

**Measured on the board, 2026-11-10**, with `sh host/tests/board_setup.sh --home-pass`:

| # | Result |
|---|---|
| B1 | **proven** — `/setup` 200 from the hotspot, the 2.4 GHz line present, and `http://k7.local/setup` resolved there by mDNS |
| B2 | **proven twice** — a real wrong password ended with `ok:false`; the reason was **15** against one network and **2** against another, so both are mapped to the person's words, and the attempt was in `last_attempt` after a reload |
| B3 | **proven** — the device joined `xperiments-2.4G`, the hotspot closed on its own, and `http://k7.local/api/status` answered from the LAN afterwards |
| B4 | **proven** — see §9 |
| B6 | **proven** — `idf.py flash` again, and the device rejoined by itself (`app: http://192.168.0.13/`), so the record survives a flash |
| B7 | **proven** — 132–146 KB in four seconds over the hotspot, with the ICY name header |
| B8 | **not proven** — the rows are covered by `t18_menu`; how they look on the glass needs eyes |
| B5 | **proven for the thing it guards** — eight minutes with the hotspot up and a client streaming: **0 starvation rebases, 0 ms ring-starved, 0 reboots**, internal free no lower than 25 355 B. The encoder read **64.0 %** of one core against the script's 35 % bound, which this station format is already documented as exceeding (README: 52.9 % without the hotspot, on synthetic material), so the hotspot's measured cost is about **11 points** of one core on this material — real, and not enough to starve. |

---

## 11. Open questions

1. **Does the access point fit?** §9 is arithmetic. `esp_netif`'s AP netif, its DHCP server and the
   AP's own accounting are not measured here, and the allocation is late by design. B4 settles it,
   and P41 is what happens if it does not.
2. **Does a client survive the channel move?** If it always drops — not sometimes — the page's
   "reload and read the reason" loop becomes the only feedback, and the grace window may need to
   start reporting differently. B2 is the experiment.
3. **Is the cached App on the setup network real?** The mountain case (`/` → cached
   `https://k7.xperiments.dev` → LNA granted → `http://k7.local`) is unverified: nobody has
   measured Chrome's `local-network` grant from a shell served out of a cache with no network. B7
   proves the stream; it does not prove the App. If it fails, `/setup` is the only page that works
   there — which is why it is firmware.
4. **Scan accuracy while the AP is up.** `esp_wifi_scan_start()` in AP+STA may miss beacons on a
   channel the AP is serving. If B2's scan looks thin, a second scan on the same request is the
   obvious retry, and the 5 s ceiling has the room.
5. **The captive portal**, deferred by ADR 0076 and worth revisiting only if first-run experience
   is measured to be the problem — it would auto-open the one page, at the cost of a DNS responder
   and a fight with the origin guard.
