# The Site: the product page, the flash, and the install

**Status:** accepted · **Updated:** 2026-11-25

**Goal.** `https://k7.xperiments.dev/` is the **Site**: one public page that says what K7 is,
flashes the player's firmware from a browser, and installs the App. The App itself moves to
`/app` and is otherwise unchanged, behind its gate. The Site is the only place a person with no
ESP-IDF environment can put K7 on a board, and the only page that offers the install where the
App can actually run. The origin also serves the **K7Art Editor** at `/k7art`
([ADR 0112](../adr/0112-the-k7art-editor-is-the-sites-k7art-page.md)): the design tool for a
cassette's label and J-card, public and static, with no device access.

The origin does not move: `xperiments.dev` and its certificate are the ones already deployed
([ADR 0057](../adr/0057-the-app-lives-at-a-public-origin.md)). What moves is which document owns
the root ([ADR 0078](../adr/0078-the-site-owns-the-root-and-the-app-moves-to-app.md)) and how the
firmware reaches a board ([ADR 0079](../adr/0079-the-firmware-is-flashed-in-parts-from-the-browser.md)).

**State:** S1–S14, R1–R6, D1–D4 and F1–F13 are built; **K7 0.6.2 and K7Player 0.6.2 are
released** — `web/firmware.json` and `web/firmware-player.json`, with their parts under
`web/public/firmware/` and `web/public/firmware/player/`. The flash's rules are tested over a
fake board and a fake origin — **not on hardware** — and **no board has been flashed from a
browser**; SI-06 is the K7's board and browser pass and SI-08 the player's. The hero uses the panel's own render in
its slot until a photograph of the device exists. The two ADRs are 0078 and 0079. The sizes in
§3 are the released bytes, and §6's one timed flash is the **USB** write — 34.0 s at 488.4 kbit/s
over `esptool`; the browser's own write has not been timed. S15 now names the K7 look
([`k7-look.md`](./k7-look.md)), applied by LK-06.

**Vocabulary:** [`web/CONTEXT.md`](../../web/CONTEXT.md) — **Site**, **Flash**, **Install**. The
App is [`k7-app.md`](./k7-app.md); the deployment is [`k7-deploy.md`](./k7-deploy.md); the
device's own address and its 302 are [`front-door.md`](./front-door.md); the network the device
is given after a flash is [`wifi-provisioning.md`](./wifi-provisioning.md).

---

## 1. Three documents at one origin

**S1. The origin serves three documents, and only one of them is the App.**

| Path | Document | Gated | Who it is for |
|---|---|---|---|
| `/` | the **Site** | no | someone with a bare board, or with a K7 and no app |
| `/app` | the **App** | `local-network` | someone using K7 |
| `/k7art` | the **K7Art Editor** | no | someone making a cassette's label and J-card |

The Site is public in the same sense the App is public (L5 of the App spec): it holds no
credential. It makes no device request at all — the player is reached over USB, not the network.
The editor is public too: its documents live in the visitor's browser, its packs are downloads,
and it never reaches a device ([ADR 0112](../adr/0112-the-k7art-editor-is-the-sites-k7art-page.md)).

**S2. `/app` is a rewrite, not a second origin.** Vercel rewrites `/app` to the App's document;
the browser's address bar keeps `/app`, and the App's hash routes follow it — `/app#/prepare`,
`/app#/playlists/:id`, `/app#/media/album/:album`. A route that exists only inside the App still
does (web/CONTEXT.md, **Route**); it now hangs off a path instead of the bare origin. **`/k7art`
is a directory, not a rewrite**: it serves the editor's own `index.html` and its base-relative
assets — `/k7art/assets/`, `/k7art/fonts/`, `/k7art/lvgl.js` and `/k7art/skill.md` — and nothing
else is rewritten for it.

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
therefore registered by the Site and the App — not by the editor; the install control does not
wait on it.

**S5. The worker never stands in front of the Site or the editor.** It precaches the App's
document and assets and answers a navigation for `/app` and `/app.html`; any other navigation —
`/`, `/k7art` and `/k7art/` included — goes to the network. The Site must never be served from a
worker's stale copy — it is the page that carries a version's flash and install — and the editor
must never be served from one either: it is a tool whose publish writes a pack.

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

**S9. Headers are the App's, unchanged, with the new documents added.** `/app.html` joins
`index.html`, `sw.js` and `manifest.webmanifest` on `no-cache`; `/k7art` and `/k7art/index.html`
join them. `/assets/` and `/k7art/assets/` (the editor's content-hashed Vite files) stay
`immutable`; the editor's `public` files — `/k7art/fonts/`, `/k7art/lvgl.js`, `/k7art/skill.md` —
are not hashed and carry a short `max-age` instead of `immutable`. The CSP stays one policy for
the origin, and it already carries `wasm-unsafe-eval`, which the editor's LVGL module needs
(D4–D6 of [`k7-deploy.md`](./k7-deploy.md)).

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

**S15. The Site wears the K7 look.** One near-black page, an orange primary and a neutral ink, outline/fill state, Jura,
micro-label chips at `text-xs`, mechanical motion — [`k7-look.md`](./k7-look.md) is the contract
and names the exceptions (content images). Components stay installed ReUI files, edited in place
where the spec says so; spacing is `gap-*` and hover is `transition-colors` where the spec does
not say otherwise ([`AGENTS.md`](../../AGENTS.md), *The app's UI*).

---

## 3. The flash

**F1. The flash runs in the browser, over Web Serial, with `esptool-js` as the engine.** The page,
the port picker, the progress, the recovery copy and the design are ours; the protocol is
Espressif's. Web Serial is desktop Chromium only — Chrome and Edge, not Safari, not Firefox, not
any phone — and that is a fact the page states rather than routes around.

**F2. Two boards are offered, and the person chooses one before a port is asked for:** **K7**
(`sunton`, the ESP32-S3 that ships) and **K7Player** (the classic ESP32 with no surface, ADR
0097). The choice comes first because the chip, the flash size, the parts and the steps
afterwards are each board's own; choosing the other board starts the section over rather than
carrying a port across. `backlight-test`, `dump`, `capture`, `verify` and `headless` are
development builds and are not offered on either board: a product page with a build matrix is a
support burden in a dropdown.

**F3. The flash writes the chosen board's parts at their own offsets, and never the gaps:**

| Board | Part | Offset | Size (0.6.2 unless noted) |
|---|---|---:|---:|
| K7 | `bootloader.bin` | `0x0` | 21,120 B |
| K7 | `partition-table.bin` | `0x8000` | 3,072 B |
| K7 | `sd_stream_server.bin` | `0x10000` | 2,111,536 B |
| K7 | `k7player.img` | `0x710000` | 891,840 B (from the first signed release) |
| K7Player | `bootloader.bin` | `0x1000` | 26,176 B |
| K7Player | `partition-table.bin` | `0x8000` | 3,072 B |
| K7Player | `ota_data_initial.bin` | `0x390000` | 8,192 B (from 0.7.0) |
| K7Player | `k7player.bin` | `0x10000` | 891,712 B |

The K7's table gains one partition, the player's signed image at `0x710000` (ADR 0116,
`player-update.md` U1–U3); everything before it keeps the offset it had. The K7 never writes that
partition itself — the flash is the only thing that does. The K7Player's bootloader sits at
**`0x1000`**, not `0x0`: that is where a classic ESP32's ROM looks, and the K7's offsets would leave the player not
booting. On the K7, `nvs` sits at `0x9000` and `phy_init` at `0xF000`; the app ends at `0x2138F0`,
far below the Snapshot at `0x410000`. A **merged** image written from `0x0` pads those gaps with
`0xFF` and so erases the stored WiFi network — a factory reset disguised as an update. The Site
therefore never serves or writes a merged image, and the download path (F10) is the same parts
with the same offsets.

**F4. The chip is checked before a byte is written, against the chosen board.** `ESP32-S3` for
K7, `ESP32` for the K7Player. A mismatch is refused with a sentence naming the board and the chip
it needs, and the port is closed.

**F5. The flash size is checked before a byte is written, against the board.** 16 MB for K7 — its
partition table places `snapshot` at `0x410000` with 2 MB, and a smaller module cannot hold it —
and 4 MB for the K7Player, whose table is two 1.75 MB OTA slots and an 8 KB `otadata`
(`player-update.md` U24). Either way, flashing a table a module cannot hold produces a board that
does not boot.

**F6. Nothing is written before a confirmation naming the port, the chip, the flash size, the
board and the firmware version.** The person sees exactly what will be written and where.

**F7. Erase is a separate, explicit choice, and its cost is named.** On K7, *Erase and install
fresh* wipes the stored WiFi network, the setup password and the device's Snapshot, including the
playlists in it; the card's music is untouched, and the App can re-send its **Last image**. On the
K7Player it wipes the network record the K7 handed over, so the share window must hand it over
again. The choice is never made for someone, and a device the page cannot read is a reason to say
so, not a reason to wipe.

**F8. The flash is a state machine, and every failure names a likely cause.** `idle → port →
identified → confirmed → writing → verifying → done`, and a failure state that says one of: the
BOOT button (hold BOOT, tap RESET, release), a port held by another program, a Linux permission
(`dialout`), a cable that does not carry data, a chip that is not an S3, a flash that is not
16 MB. A failure never leaves the page saying only "error".

**F9. Each board's manifest is the only source of its version, offsets, flash parameters and
hashes.** `web/firmware.json` is the K7's and `web/firmware-player.json` the K7Player's; the page
has no constant for any of them. It reads both at build time and fetches each part from the
board's own directory — `/firmware/<version>/<file>` for the K7, `/firmware/player/<version>/<file>`
for the player — checking the part's sha256 before writing it.

**F10. A browser without Web Serial gets the sentence and a manual path.** The manual path is
disclosed, not primary: the same parts plus an `esptool` command generated from the chosen
board's manifest — `--chip esp32s3` or `--chip esp32` — so the command cannot drift from the
files. It is a real route for a builder on Firefox; it is not presented as the normal one.

**F11. A successful flash ends in the board's own next step, not in "done".** For **K7**: join
the network `K7.local`, open `http://192.168.4.1/setup`, then install K7 from this Site
([`wifi-provisioning.md`](./wifi-provisioning.md)). For the **K7Player**: unplug it, put it
within range of the K7, and open the K7's **Menu → Wi-Fi → Share** window — the K7 hands it the
network over ESP-NOW and it starts playing
([`network-handover.md`](./network-handover.md)). The Site does not provision either over
serial: on the K7 the setup network is the one channel a person already uses (ADR 0076), and the
player has no credential to type at all.

**F12. No account, no upload, no analytics, no stored port.** The page writes the board and
forgets it.

**F13. The flash ends by resetting the board into the application, with an edge.** The write
leaves the enable line released, and esptool-js's `after('hard_reset')` only releases it again —
so the first browser flash left the board in the stub until the cable was pulled. The flasher
asserts EN (RTS) for 150 ms and releases it, with IO0 high: the same edge the boot button makes.

---

## 4. The release

**R1. The firmware has a version, and it is a file.** `VERSION` at the repository root is
semver; CMake passes it as `PROJECT_VER`; the image carries it, the panel's **About** row already
reads it (`cassette_menu.c`), and `firmware.json` repeats it. The first release is `0.5.0`, after
the one tag the repository has, `v0.4.0`.

**R2. Each board has a release action, and both publish through one script.**
`./player/build.sh release` publishes the K7Player and signs its image, and
`./tools/build.sh sunton release` publishes the K7, which carries that image; `npm run release`
runs the player's first and the K7's second, and stops at the first failure.
Each

1. refuses a dirty tree (`git status --porcelain`) over its own source — a binary from
   uncommitted source is not a release;
2. refuses a `VERSION` that does not match a tag `v<VERSION>` pointing at `HEAD`;
3. builds, and hands the build to `tools/release_publish.py`, which copies the parts from that
   build's own `flasher_args.json` — refusing a part table that is not the board's — to
   `web/public/firmware/<version>/` for the K7 and `web/public/firmware/player/<version>/` for the
   player;
4. writes the board's manifest — version, commit, IDF version, chip, flash parameters, and for
   each part its file, offset, size and sha256;
5. prints the version, the sizes and the hashes, and stops. It does not commit, tag or deploy.

For the K7Player the release also signs the image (ADR 0116): `tools/release_publish.py` signs
`k7player.bin` with the release key, writes the 128-byte header of `player-update.md` U1 around it,
and publishes `k7player.img`, the image behind its signed header; a player release with no key is
refused. The
K7's manifest then carries that image as its fourth part, so the K7's release cannot be cut without
the player's bytes.

**R3. Both manifests and both boards' parts are committed.** A clone that has never seen
ESP-IDF deploys a Site whose flasher works. The cost is ~3 MB of history per K7 release — the
player's signed image is about 0.9 MB of it, carried at `0x710000` (ADR 0116) — and ~1.9 MB per
player release, which publishes the image as well as the parts; the alternative is a deployment
that 404s the one button the page exists for.

**R4. CI proves each manifest against its files.** `web/scripts/check-firmware.mjs` reads both
manifests, hashes every part under their directories, and fails on a size or hash mismatch, on a
missing file, on a `VERSION` that disagrees with either manifest, and on a shape the page cannot
read. It also verifies the player's signature against the public key committed in the player's
source, so a published image that the players would refuse cannot pass the check. It runs in the web job of CI and as `npm run check:firmware`. The manifest shape check that already
guards the PWA keeps its job (D4a, A9).

**R5. The Site never publishes what it cannot verify.** The bytes the page writes are the bytes
whose hashes are in the manifest, and the manifest is the file CI checked.

**R6. The download path is generated, not written.** The `esptool` command and every link in the
manual path are rendered from the chosen board's manifest; a hand-written command is how offsets
rot.

---

## 5. Delivery

**D1. Three documents in one artifact.** `web/index.html` is the Site, `web/app.html` is the
App, and the **K7Art Editor** is `tools/LabelEditor/`, its own Vite build with `base: "/k7art/"`,
copied under `dist/k7art/`. `npm run build` emits the first two and builds and copies the editor;
`npm test`, `npm run typecheck` and the artifact guard apply to the Site and the App, and the
editor's own `npm test` and `npm run build` to it. One deploy, two packages: the editor is a
second Vite project, not a second origin or a second Vercel project.

**D2. One rewrite, and one directory.** `/app` → `/app.html` on Vercel; `/k7art/` is served from
its own directory (`/k7art/index.html` with the base-relative assets). The development server
answers `/app` with the same document through a middleware, so
`k7-dev.xperiments.dev:8000/app` has the shape it ships in (D11–D12 of
[`k7-deploy.md`](./k7-deploy.md)), and it answers `/k7art` from the editor's build
(`tools/LabelEditor/dist`, refreshed by `npm run build:editor`), so the editor's address is never
the Site's document; an editor that is not built answers with a message naming that build. The
editor is edited on its own `npm run dev` at `:5178`, with `base: "/"` (ADR 0112).

**D3. The worker's shell list moves with the App.** `/` and `/index.html` leave it; `/app` and
`/app.html` replace them, and the navigation handler answers only those (S5). The Site and the
editor are not precached and are never served from a cache-first path.

**D4. A shipping build still refuses the development origin** (D14), and now also refuses a Site
that names a firmware version absent from `web/public/firmware/`.

---

## 6. Tests

Every size above is from one local `sunton` build. No release has been cut, no board has been
flashed from a browser, and the timings below are not yet measured.

**Host and web tests**

| # | Test | Tier |
|---|---|---|
| T1 | The build emits `/`, `/app` and `/k7art`, and `/app` renders the App, `/` the Site and `/k7art` the editor | web |
| T2 | The manifest is `id: "/"`, `start_url: "/app"`, `scope: "/app"`, with the icons A9 requires | web/CI |
| T3 | The Site's install section renders the control only when `queryLocalNetwork()` is not `unsupported`, and *Open the app* when installed | web |
| T4 | Over a fake serial transport: a non-S3 chip is refused, an 8 MB flash is refused, nothing is written before confirmation, the happy path writes `0x0`/`0x8000`/`0x10000`, erase happens only when chosen, and every failure has its named cause | web |
| T5 | A part whose sha256 does not match `firmware.json` is refused before it is written | web |
| T6 | CI: every part matches the manifest's size and hash; `VERSION` matches the manifest; a missing part fails the build | CI |
| T7 | `./tools/build.sh`'s release refuses a dirty tree and a tag that does not match `VERSION` | host (shell) |
| T8 | The worker precaches `/app` and answers navigation there; a navigation to `/` or `/k7art` goes to the network | web |
| T9 | The device's 302 names `https://k7.xperiments.dev/app#/player` | host (`http_tests.sh`) |
| T10 | The Site's flash section is absent-but-honest without `'serial' in navigator`, and the manual command carries the manifest's offsets | web |
| T11 | The reset that ends a flash moves the enable line — `DTR false, RTS true`, a wait, `DTR false, RTS false` — instead of releasing a line that is already released | web |
| T12 | `/k7art` serves the editor and `/k7art/skill.md` the guide; every asset resolves under `/k7art/`; the editor's page boots from the built artifact | web |
| T13 | The development server answers `/k7art` with the editor's document, `/k7art/skill.md` with the guide and a build asset with its own type; a build file it does not carry, and a path outside it, are refused rather than answered with the Site; an unbuilt editor names `npm run build:editor` | web |

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

**Measured on the board, 2026-11-11** — the **released parts written over USB by `esptool`,
not by the Site** (the browser's port chooser needs a person at the machine), 460800 baud:

| # | Result |
|---|---|
| B2 | **proven** — 2,073,008 B at `0x10000` written in **34.0 s** (488.4 kbit/s, 1,290,565 B compressed), hash verified, hard reset. The board rejoined **`xperiments-2.4G`** from its stored record; at boot it logged `snapshot: 1502 rows` and `App version: 0.5.0`, and `/api/status` still answered with the same card mark and 1,495 tracks. |
| B1, B3–B7 | **not run** — a bare board (this one is provisioned), an erase (it would strand this board off its network), a non-S3 or 8 MB module, the browser's chooser, Safari/Firefox, and the deployed origin. |
| — | The Site and `/app` were rendered in headless Chrome: the flash card offered **Connect a board**, the install card offered **Install K7**, and `/app` showed its Local Network Access gate. |

The browser's own write is still unproven; the bytes are the same three parts at the same
three offsets, which is what B2 above exercised.

---

## 7. Open questions

1. **Which USB-UART bridge does the shipping board carry, and does it need a driver?**
   **Answered on 2026-11-11:** it is a **CH340** (`1a86:7523`), enumerated by macOS as
   `usbserial-8344440`. macOS had the driver; Windows may need WCH's, and the page's recovery
   copy names it.
2. **What baud rate does that bridge sustain?** **Measured 2026-11-11:** asked for 460800, the
   CH340 carried **488.4 kbit/s** effective; the 2,073,008 B app took 34.0 s.
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
