# The Site: the product page, the flash, and the install

**Goal.** `https://k7.xperiments.dev/` is the **Site**: one public page that says what K7 is,
flashes the player's firmware from a browser, and installs the App. The App itself moves to
`/app` and is otherwise unchanged, behind its gate. The Site is the only place a person with no
ESP-IDF environment can put K7 on a board, and the only page that offers the install where the
App can actually run.

The origin does not move: `xperiments.dev` and its certificate are the ones already deployed
([ADR 0057](../adr/0057-the-app-lives-at-a-public-origin.md)). What moves is which document owns
the root ([ADR 0078](../adr/0078-the-site-owns-the-root-and-the-app-moves-to-app.md)) and how the
firmware reaches a board ([ADR 0079](../adr/0079-the-firmware-is-flashed-in-parts-from-the-browser.md)).

**Status:** S1–S9, R1–R6 and D1–D4 are built; **K7 0.5.0 is released** (`web/firmware.json`
and `web/public/firmware/0.5.0/`: `bootloader.bin` 21,120 B, `partition-table.bin` 3,072 B,
`sd_stream_server.bin` 2,073,008 B). The Site's own page is still its first shape — the hero
slot, the flash and the install are SI-03/SI-04 — and **no board has been flashed from a
browser**. The two ADRs are 0078 and 0079. The sizes in §3 are the released bytes; no timing in
§6 is measured yet.

**Vocabulary:** [`web/CONTEXT.md`](../../web/CONTEXT.md) — **Site**, **Flash**, **Install**. The
App is [`k7-app.md`](./k7-app.md); the deployment is [`k7-deploy.md`](./k7-deploy.md); the
device's own address and its 302 are [`front-door.md`](./front-door.md); the network the device
is given after a flash is [`wifi-provisioning.md`](./wifi-provisioning.md).

---

## 1. Two documents at one origin

**S1. The origin serves two documents, and only one of them is the App.**

| Path | Document | Gated | Who it is for |
|---|---|---|---|
| `/` | the **Site** | no | someone with a bare board, or with a K7 and no app |
| `/app` | the **App** | `local-network` | someone using K7 |

The Site is public in the same sense the App is public (L5 of the App spec): it holds no
credential. It makes no device request at all — the player is reached over USB, not the network.

**S2. `/app` is a rewrite, not a second origin.** Vercel rewrites `/app` to the App's document;
the browser's address bar keeps `/app`, and the App's hash routes follow it — `/app#/process`,
`/app#/playlists/:id`, `/app#/media/album/:album`. A route that exists only inside the App still
does (web/CONTEXT.md, **Route**); it now hangs off a path instead of the bare origin.

**S3. The manifest describes the App, and both documents link it.**

| Field | Value | Why |
|---|---|---|
| `id` | `/` | The App's identity to Chrome. It does not change, or every install is orphaned (A12). |
| `start_url` | `/app` | Installing from either document opens the App, not the Site. |
| `scope` | `/app` | The installed window owns the App; a link to `/` stays a website in a tab. |
| `name`, `short_name` | `K7` | unchanged |
| `display` | `standalone` | unchanged |
| icons | 192, 512, 512 maskable | unchanged (A9) |

**S4. The Site registers the worker.** A visitor who installs from `/` may never have loaded
`/app`, and an installed App with no worker is an App with no offline shell. `/sw.js` is
therefore registered by both documents; the install control does not wait on it.

**S5. The worker never stands in front of the Site.** It precaches the App's document and assets
and answers a navigation for `/app` and `/app.html`; any other navigation goes to the network.
The Site must never be served from a worker's stale copy — it is the page that carries a
version's flash and install.

**S6. The Site's install control and the App's must agree.** The Site reuses
`web/src/ui/install.ts` (`readInstallState`, `promptInstall`) and
`web/src/device/localNetwork.ts` (`queryLocalNetwork`); it does not grow a second browser
sniffer. The control is offered only when **both** can answer: installability from the install
state, and runnability from the permission being anything but `unsupported`.

**S7. The gate points at the Site.** The App's Local Network Access gate gains one line, *No
player yet? Flash one*, opening `https://k7.xperiments.dev/` in a new tab. A `target="_blank"`
link leaves the standalone window for the browser, which is the only way back to a page with an
address bar (S3).

**S8. The device's 302 follows the App.** `FRONT_DOOR_LAUNCH_URL` becomes
`https://k7.xperiments.dev/app#/player`; the Kconfig default, `front_door.c`'s fallback and the
host test's expected `Location` change with it. Every existing local `sdkconfig` keeps the old
string until it is reconfigured — a change of default does not rewrite a generated config, and a
build that is not reconfigured flashes a doorbell to the Site.

**S9. Headers are the App's, unchanged, with the new document added.** `/app.html` joins
`index.html`, `sw.js` and `manifest.webmanifest` on `no-cache`; `/assets/` stays `immutable`; the
CSP stays one policy for the origin (D4–D6 of [`k7-deploy.md`](./k7-deploy.md)).

---

## 2. What the Site says

**S10. Three sections, in this order, with anchors.** A hero that says what K7 is (`#top`), then
**flash** (`#flash`), then **install** (`#install`). Flash comes first because the person who
arrives with a board has no other route to firmware; the install is for the one who already has a
player.

**S11. The hero uses the product's own positioning.** The sentence is the campaign's
([`docs/research/marketing/campaign.md`](../research/marketing/campaign.md) §4.1), not a new one:

> For people who own their music and miss the object it came in, **K7** is a cassette-shaped
> player that turns a card into a tape and a room into a radio station.

The hero's media is `resources/cassette-drawer-800x480.png` — the panel's own drawing — held in
a **slot** so a photograph of the device can replace it without a redesign. There is no
photograph of the device in this repository, and the page does not wait for one.

**S12. The flash section names what it needs before it asks for a port.** A USB-C **data** cable,
a computer, and Chrome or Edge on it. A phone cannot flash, and the section says so before the
button rather than after a tap.

**S13. The install section is offered only where it can run.** With `local-network`
`unsupported` — Safari, Firefox, every iOS browser — the section states that K7 needs Chrome on a
computer, and offers no install control. A10's *Share → Add to Home Screen* sentence is retired:
an icon that opens a gate cannot do anything, and offering it is the dead button A10 exists to
prevent. An installed window sees *Open the app* instead of the control.

**S14. The footer is the licence trail, not a sitemap.** Links to the repository and
`THIRD_PARTY_NOTICES`, and no analytics, no cookie, no third-party script. The firmware is a
distribution of LGPL-2.0 `shine`, and the public source plus the notices are what keeps the
relink path real ([`THIRD_PARTY_NOTICES`](../../THIRD_PARTY_NOTICES)).

**S15. The Site is a ReUI page.** Components are installed, tokens are semantic, spacing is
`gap-*`, nothing is below `text-xs`, one entrance at most, and hover is `transition-colors`
([`AGENTS.md`](../../AGENTS.md), *The app's UI*).

---

## 3. The flash

**F1. The flash runs in the browser, over Web Serial, with `esptool-js` as the engine.** The page,
the port picker, the progress, the recovery copy and the design are ours; the protocol is
Espressif's. Web Serial is desktop Chromium only — Chrome and Edge, not Safari, not Firefox, not
any phone — and that is a fact the page states rather than routes around.

**F2. One variant is offered: `sunton`, the board that ships.** `backlight-test`, `dump`,
`capture`, `verify` and `headless` are development builds; a product page with a build matrix is
a support burden in a dropdown.

**F3. The flash writes three parts at their own offsets, and never the gaps:**

| Part | Offset | Size today |
|---|---:|---:|
| `bootloader.bin` | `0x0` | 21,120 B |
| `partition-table.bin` | `0x8000` | 3,072 B |
| `sd_stream_server.bin` | `0x10000` | 2,073,008 B |

`nvs` sits at `0x9000` and `phy_init` at `0xF000`; the app ends at `0x20A1B0`, far below the
Snapshot at `0x410000`. A **merged** image written from `0x0` pads those gaps with `0xFF` and so
erases the stored WiFi network — a factory reset disguised as an update. The Site therefore never
serves or writes a merged image, and the download path (F10) is the same three parts with the
same offsets.

**F4. The chip is checked before a byte is written.** `ESP32-S3` or refuse.

**F5. The flash size is checked before a byte is written.** 16 MB or refuse. The partition table
places `snapshot` at `0x410000` with 2 MB; a module smaller than 16 MB cannot hold this table,
and flashing it produces a board that does not boot.

**F6. Nothing is written before a confirmation naming the port, the chip, the flash size and the
firmware version.** The person sees exactly what will be written and where.

**F7. Erase is a separate, explicit choice.** *Erase and install fresh* wipes the whole chip: the
stored WiFi network, the setup password and the device's Snapshot, including the playlists in it.
The card's music is untouched, and the App can re-send its **Last image**. The choice is never
made for someone, and a device the page cannot read is a reason to say so, not a reason to wipe.

**F8. The flash is a state machine, and every failure names a likely cause.** `idle → port →
identified → confirmed → writing → verifying → done`, and a failure state that says one of: the
BOOT button (hold BOOT, tap RESET, release), a port held by another program, a Linux permission
(`dialout`), a cable that does not carry data, a chip that is not an S3, a flash that is not
16 MB. A failure never leaves the page saying only "error".

**F9. `firmware.json` is the only source of the version, the offsets and the hashes.** The page
has no constant for any of them. It reads the manifest at build time and fetches each part from
`/firmware/<version>/<file>`, checking the part's sha256 before writing it.

**F10. A browser without Web Serial gets the sentence and a manual path.** The manual path is
disclosed, not primary: the same three parts plus an `esptool` command generated from
`firmware.json`, so the command cannot drift from the files. It is a real route for a builder on
Firefox; it is not presented as the normal one.

**F11. A successful flash ends in the setup, not in "done".** Three steps: join the network
`K7.local`, open `http://192.168.4.1/setup`, then install K7 from this Site
([`wifi-provisioning.md`](./wifi-provisioning.md)). The Site does not provision over serial: the
setup network is the one channel a person already uses, and a second one would carry the same
password twice (ADR 0076).

**F12. No account, no upload, no analytics, no stored port.** The page writes the board and
forgets it.

---

## 4. The release

**R1. The firmware has a version, and it is a file.** `VERSION` at the repository root is
semver; CMake passes it as `PROJECT_VER`; the image carries it, the panel's **About** row already
reads it (`cassette_menu.c`), and `firmware.json` repeats it. The first release is `0.5.0`, after
the one tag the repository has, `v0.4.0`.

**R2. `./tools/build.sh sunton release` builds and publishes one release.** It

1. refuses a dirty tree (`git status --porcelain`) — a binary from uncommitted source is not a
   release;
2. refuses a `VERSION` that does not match a tag `v<VERSION>` pointing at `HEAD`;
3. builds `sunton` and copies the three parts from `build/sunton/` to
   `web/public/firmware/<version>/`;
4. writes `web/firmware.json` — version, commit, IDF version, and for each part its file, offset,
   size and sha256;
5. prints the version, the sizes and the hashes, and stops. It does not commit, tag or deploy.

**R3. `firmware.json` and the parts are committed.** A clone that has never seen ESP-IDF deploys
a Site whose flasher works. The cost is ~2.1 MB of history per release; the alternative is a
deployment that 404s the one button the page exists for.

**R4. CI proves the manifest against the files.** A check reads `web/firmware.json`, hashes every
part under `web/public/firmware/<version>/`, and fails on a size or hash mismatch, on a missing
file, and on a `VERSION` that disagrees with the manifest. The manifest shape check that already
guards the PWA keeps its job (D4a, A9).

**R5. The Site never publishes what it cannot verify.** The bytes the page writes are the bytes
whose hashes are in the manifest, and the manifest is the file CI checked.

**R6. The download path is generated, not written.** The `esptool` command and every link in the
manual path are rendered from `firmware.json`; a hand-written command is how offsets rot.

---

## 5. Delivery

**D1. Two Vite entries in one artifact.** `web/index.html` is the Site, `web/app.html` is the
App; `npm run build` emits both, and `npm test`, `npm run typecheck` and the artifact guard apply
to both. There is no second project, package or deploy.

**D2. One rewrite.** `/app` → `/app.html` on Vercel; the development server answers `/app` with
the same document through a middleware, so `k7-dev.xperiments.dev:8000/app` has the shape it
ships in (D11–D12 of [`k7-deploy.md`](./k7-deploy.md)).

**D3. The worker's shell list moves with the App.** `/` and `/index.html` leave it; `/app` and
`/app.html` replace them, and the navigation handler answers only those (S5). The Site is not
precached and is never served from a cache-first path.

**D4. A shipping build still refuses the development origin** (D14), and now also refuses a Site
that names a firmware version absent from `web/public/firmware/`.

---

## 6. Tests

Every size above is from one local `sunton` build. No release has been cut, no board has been
flashed from a browser, and the timings below are not yet measured.

**Host and web tests**

| # | Test | Tier |
|---|---|---|
| T1 | The build emits `/` and `/app`, and `/app` renders the App while `/` renders the Site | web |
| T2 | The manifest is `id: "/"`, `start_url: "/app"`, `scope: "/app"`, with the icons A9 requires | web/CI |
| T3 | The Site's install section renders the control only when `queryLocalNetwork()` is not `unsupported`, and *Open the app* when installed | web |
| T4 | Over a fake serial transport: a non-S3 chip is refused, an 8 MB flash is refused, nothing is written before confirmation, the happy path writes `0x0`/`0x8000`/`0x10000`, erase happens only when chosen, and every failure has its named cause | web |
| T5 | A part whose sha256 does not match `firmware.json` is refused before it is written | web |
| T6 | CI: every part matches the manifest's size and hash; `VERSION` matches the manifest; a missing part fails the build | CI |
| T7 | `./tools/build.sh`'s release refuses a dirty tree and a tag that does not match `VERSION` | host (shell) |
| T8 | The worker precaches `/app` and answers navigation there; a navigation to `/` goes to the network | web |
| T9 | The device's 302 names `https://k7.xperiments.dev/app#/player` | host (`http_tests.sh`) |
| T10 | The Site's flash section is absent-but-honest without `'serial' in navigator`, and the manual command carries the manifest's offsets | web |

**Board-only, by hand**

| # | Test |
|---|---|
| B1 | A bare Sunton: plug in, pick the port, confirm, flash from the deployed Site in Chrome; the device boots and raises `K7.local` with the default password |
| B2 | A working device: flash again; the stored WiFi record survives, the device rejoins its home network, and the Snapshot and its playlists are still there |
| B3 | *Erase and install fresh*: the device comes back unprovisioned, the hotspot returns, and the App can re-send its Last image |
| B4 | A non-ESP32-S3 board and an 8 MB module are refused before any write, with the reason on screen |
| B5 | A port held by another program, and a board that needs the BOOT-button sequence: the page names each cause and the flash succeeds on the retry |
| B6 | Safari and Firefox on the Site: the honest sentence, the manual path, and no dead flash control |
| B7 | Install from the Site in Chrome: the icon opens at `/app` in its own window with no address bar, and a link to `/` stays in a tab |

---

## 7. Open questions

1. **Which USB-UART bridge does the shipping board carry, and does it need a driver?** The
   repository records only "a separate USB-UART bridge" (`usbserial-834430`); the page's recovery
   copy should name the driver if one is needed on Windows or macOS.
2. **What baud rate does that bridge sustain?** The flash time is not measured; the page should
   not claim a speed the cable cannot carry.
3. **Can the installed firmware's version be read before writing?** `esp_app_desc_t` sits at
   `0x10000 + 0x20`, so a read of 256 bytes would name it. Not in v1; the confirmation names what
   will be written, not what is there.
4. **Does `scope: "/app"` change anything about installability from `/`?** The manifest is linked
   from a page outside its scope. Chrome's criteria do not mention scope, but this is the one
   thing to verify on the real origin before A13 is re-signed.
5. **A photograph of the device** for the hero slot, and who shoots it.

---

## 8. Out of scope

- **Provisioning over serial (Improv).** The setup network is the one channel (ADR 0076, F11).
- **The development variants** as downloads or buttons (F2).
- **OTA.** The flash is the update; the card's **Update file** carries the Snapshot, not the
  firmware.
- **A phone path.** Web Serial does not exist there, and a native app is a different product.
- **Accounts, analytics, telemetry.**
- **A setup-page change.** The device's page stays asset-free and firmware-resident; the person on
  the hotspot often has no internet, and a link to an unreachable address is the small lie this
  repository refuses.
