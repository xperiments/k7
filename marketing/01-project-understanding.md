# Phase 1 — Project Understanding Report

**Product:** K7 (the repository was `xtape` until 2026-11-03)
**Repository:** `github.com/xperiments/k7` · version `0.6.1` · 683 commits, 2026-10-16 → 2026-11-16
**Prepared:** 2026-11-16 · read-only review of the whole tree, the live site, and the public repo

---

## How to read this

- **FACT** — read in the repository, the live site, or a public API response. Cited `path:line`.
- **INFERENCE** — my reading of the evidence. No citation can settle it.
- **PROPOSAL** — a suggestion for later phases.

Two rules I applied, both because the repo already enforces them on itself:

1. **Code outranks prose.** Where a spec or the README contradicts the tree, the tree wins and I
   say so. There is a drift section (§2.6) because the drift is real and it matters for marketing.
2. **Host evidence is not board evidence.** The repo's own rule (`README.md:470-474`). A thing
   that compiles and passes on the host is not a thing that has run on hardware, and I mark it
   that way.

A note on scale: the repository is unusually large for a hardware project — 96 ADRs, 20 spec files
totalling ~10,400 lines, a firmware, a React app, a design tool, and a prior marketing plan. This
report is a compression, not a summary. Where I say something is "the strongest evidence in the
repo" I mean it.

---

## 1. What it is

### In one sentence

**K7 is a cassette-shaped music deck built on an ESP32-S3 that owns a music library on a microSD
card and broadcasts it over the local network as one continuous live MP3 stream, with a browser
app that measures your files and writes the tape.**

### In one paragraph

The object is a 5-inch touch panel showing a drawn compact cassette whose reels turn at real tape
speed; the card is the tape, and the broadcast is the deck. It has no account, no cloud, no
recommendations and no seek bar: it plays the tracks the card holds, in the order the card holds
them, or the list you wrote onto the card, and it re-encodes everything to 44.1 kHz / 256 kbps MP3
so that any player on the network — a phone, a laptop, a hi-fi — hears the same moment. Beside it,
a free browser app analyses the card's tracks for tempo, key, mood and energy (in the tab, nothing
uploaded), authors playlists as files on the card, browses the library as a shelf of cassettes, and
prints a cassette label onto the deck's screen. A public site at `k7.xperiments.dev` flashes the
firmware to a bare board from Chrome over USB and installs the app. Name, colour and voice all
come from one place: *K7* is what a cassette is called in French, and the thing is drawn like one
(`docs/adr/0062-xtape-becomes-k7.md:3`).

### Read this before the rest

Three sentences, in descending order of how much they change the marketing:

1. **The device makes no sound.** There is no I2S, no DAC, no amplifier and no speaker in the
   firmware — the only mention of I2S in the whole tree is a comment saying a DAC and the LED strip
   cannot share a pin (`components/app/strip/strip.c:47-50`). Audio leaves as HTTP and is decoded by
   somebody else's hardware. The research note says it plainly: an ESP32-S3 "whose only output today
   is an MP3 stream clocked by its own encoder"
   (`docs/research/server/s15-i2s-dac-output.md:3-5`; the SoC has no DAC,
   `docs/specs/sd-stream-server.md:94`).
2. **There is no physical design.** No STL, STEP, 3MF, KiCad or SCAD file exists anywhere in the
   tree or in git history. No case, no carrier board, no BOM with part numbers, no assembly guide.
   The "product" hardware today is a bought Sunton 8048S050C panel board, a microSD card and a hand-
   wired 8-pixel strip (`README.md:583`, `docs/specs/led-strip.md:3`).
3. **The software is real and it is live.** Three firmware releases (`web/firmware.json`), a
   deployed site and app (`https://k7.xperiments.dev/` returns 200, `last-modified` 2026-11-14), a
   public repo, a host test suite, and a two-tier acceptance discipline that is actually enforced.

INFERENCE: this is a **finished instrument and an unfinished appliance**. Everything that can be
proved in software has been proved with unusual care; everything that makes it a product you can
put on a shelf — sound, shell, price, certification — is unstarted. Any campaign that pretends
otherwise will be contradicted by the repository in public.

---

## 2. What works today, strictly

Status vocabulary: **VERIFIED IN CODE** (implemented and reachable) · **PARTIAL** (implemented but
one link unproven — usually the board) · **ASPIRATIONAL** (designed or researched, not built) ·
**ABSENT** (no evidence, and some of it deliberately).

### 2.1 The broadcast (firmware, `components/portable/mini_icecast`)

| Feature | Status | Evidence |
|---|---|---|
| Decodes MP3, FLAC, WAV; any sample rate ≤ 96 kHz; mono or stereo | VERIFIED IN CODE | `components/portable/audio_format/src/audio_format.c:12-17`; `components/portable/mini_icecast/include/mini_icecast.h:39-47` |
| Resamples everything to one station rate | VERIFIED IN CODE | `components/portable/mini_icecast/src/mi_resample.c` |
| Encodes to MP3 (vendored `shine`, fixed point) | VERIFIED IN CODE | `components/portable/shine/src/lib/layer3.c`; `mi_encode.c` |
| Station format 44 100 Hz / 256 kbps CBR / joint stereo, max 4 clients | VERIFIED IN CODE | `components/app/appliance/appliance.c:220-228` |
| Refuses a format the encoder cannot represent, names it and idles | VERIFIED IN CODE | `appliance.c:286-297`; log quoted at `README.md:394-398` |
| One continuous stream; every client hears the same moment; no pause or seek | VERIFIED IN CODE | `README.md:1272-1273`; `docs/specs/CONTEXT.md:43-45` |
| ICY metadata opt-in, block every 16 000 encoded bytes | VERIFIED IN CODE | `components/portable/mini_icecast/src/mi_icy.c:42-58` |
| Boots shuffled (xorshift32, seeded from `esp_random()`) | VERIFIED IN CODE | `appliance.c:239-243` |
| 3-band EQ + trim before the encoder (200 Hz shelf, 1 kHz bell, 4 kHz shelf) | VERIFIED IN CODE | `components/portable/tone_eq/include/tone_eq.h:29-37`; applied `mini_icecast.c:590-596` |
| Per-client send rings so a slow listener cannot starve the broadcast | VERIFIED IN CODE | `mini_icecast.c:1415-1418`; rationale `README.md:604-610` |
| Tape FX (wow, flutter, hiss, saturation) | ABSENT — the "tape" is a drawing | grep: no hits; `README.md:956` describes cassette as what the screen shows |
| Audio output of any kind | ABSENT — see §1 | §1 above |

### 2.2 The device's HTTP surface

| Feature | Status | Evidence |
|---|---|---|
| `GET /stream`, `GET /library`, `GET /api/library.bin`, `GET /api/status` | VERIFIED IN CODE | route registrations in `components/portable/app_services/app_services.c:615-715` |
| `POST /api/play`, `POST /api/rescan` | VERIFIED IN CODE | same |
| Playlist API: `GET /api/playlists`, `GET/PUT/DELETE /api/playlists/<id>`, `POST /api/playlists/select` | VERIFIED IN CODE | same; format `docs/specs/playlist-format.md` |
| MCP endpoint (`initialize`, `tools/list`, five read-only `library_*`/`player_status` tools) | VERIFIED IN CODE | `components/portable/mcp`; `README.md:297` |
| MCP control tools (`player_play`, `player_skip`, `player_prev`, `library_rescan`) | PARTIAL — behind build flag, off by default | `components/portable/mcp/src/mcp.c:19-22` |
| The device serves its own app / any page of it | ABSENT — deliberately, `302` to the Site | `components/portable/front_door/src/front_door.c:127-152` |
| `/setup` provisioning page, only while the hotspot is up | VERIFIED IN CODE | `docs/specs/wifi-provisioning.md`; ADR 0077 |
| Authentication, TLS, upload, per-track GET, range requests | ABSENT — deliberately, and stated in the first screen of the README | `README.md:23-39`; `docs/specs/CONTEXT.md:9-30` |

### 2.3 Storage and the card

| Feature | Status | Evidence |
|---|---|---|
| microSD over SPI (CS 10 / CLK 12 / MOSI 11 / MISO 13), 20 MHz, FAT32 | VERIFIED IN CODE | `components/app/board_esp32/board_esp32.c:47-60`; `README.md:179-196` |
| Library tree built once into a 4 MiB / 20 000-track PSRAM arena | VERIFIED IN CODE | `components/portable/library/include/library.h:29-30,63-83` |
| Snapshot (measurements) in a 2 MB flash partition, read from a PSRAM copy | PARTIAL — code is format **v5**; the README still says v4; the flash window is recorded as board-pending | `components/portable/snapshot/include/snapshot.h:27`; `README.md:1200,1216` |
| Playlists as files on the card (`k7.playlists/<id>.k7list`) written by the device | VERIFIED IN CODE | `components/portable/playlist_file/include/playlist_file.h:28-31` |
| `k7.select` selection record written by the device | VERIFIED IN CODE | `README.md:1205-1207` |
| Card mark `k7.card` read at mount, id reported in `/api/status` | VERIFIED IN CODE | `docs/specs/card-mark.md:12-14`; `app_services.c:319` |
| Hot card removal detected, unmounted and remounted | VERIFIED IN CODE — **contradicts `README.md:1266`** | `app_services.c:341-420` |
| OTA firmware update | ABSENT — no OTA partition | `partitions.csv`; `README.md:1264` |

### 2.4 The physical device

| Feature | Status | Evidence |
|---|---|---|
| 800×480 RGB565 parallel panel, 16 MHz pixel clock | VERIFIED | `components/app/screen_display/panel_rgb.c:30-31,41,45-46,103` |
| GT911 capacitive touch (I2C0, 0x5D, SDA 19 / SCL 20 / INT 18 / RST 38), polled | PARTIAL — code verified, "the board tier has not yet recorded a finger" | `touch_gt911.c:41-53`; `README.md:732-733` |
| LVGL 9.6 with an explicit PSRAM allocator | VERIFIED | `components/app/screen_display/lvgl_mem.c`; `docs/adr/0004-ui-memory-is-external.md` |
| Drawn cassette, reels at real tape geometry (47.625 mm/s), 8 s reel-change rewind | VERIFIED | `components/portable/cassette/include/cassette.h:8-12,38-44,76-84` |
| Four vertical rungs Real / Art / Initial / Peek, dragged knobs, 3× edit dial | VERIFIED | `components/portable/controls/include/controls.h:49-59`; `knob.c`; `knob_editor.c` |
| Menu: Browse / Playlists / Settings / Shuffle, ratcheted on the Pad | VERIFIED | `components/portable/menu/include/menu.h:61-90` |
| LED strip: 8 × WS2812B on GPIO 17, 30 Hz, six effects, capped at 25 % power | PARTIAL — driver runs at boot; visual acceptance, level shifter and 4.5 V supply are open | `components/app/strip/strip.c:59-61`; `appliance.c:312`; `docs/specs/led-strip.md:113-131` |
| Backlight sleep, 100 → 60 → 30 → 0 % at 0/2/5/10 min | VERIFIED | `README.md:790-801` |
| Touchscreen label print (hold a cassette to the screen with two fingers) | PARTIAL — built and packed; the panel proof is a design ticket, not a recorded board run | `docs/specs/label-templates.md:3-8`; `docs/adr/0071` |
| Rotary encoder or any physical button | ABSENT — GPIO0 is the BOOT strap and has no runtime action | `README.md:200-203` |
| Battery, charger, power management, measured consumption | ABSENT | no hits in `components/`; power is the 5 V USB rail |
| Enclosure, carrier PCB, schematic, BOM, assembly guide | ABSENT | exhaustive search for `.stl .step .3mf .scad .kicad_* .sch .brd` returns nothing |

### 2.5 The Site and the App (`web/`)

| Feature | Status | Evidence |
|---|---|---|
| Site at `/` says what K7 is, flashes a board, installs the app | VERIFIED — deployed | `web/src/site/Site.tsx:36-73`; live 200 |
| Flash over USB from Chrome/Edge (Web Serial + esptool-js, three parts at `0x0/0x8000/0x10000`) | PARTIAL — tested over a fake board and a fake origin; **no board has been flashed from a browser** | `docs/specs/k7-site.md:14-20`; `web/src/site/flash/` |
| PWA install, offline shell, 41 MB of hash-pinned weights | VERIFIED | `docs/specs/k7-app.md:17-27`; `web/src/card/` `web/src/weights/` |
| Analyses tempo, key (Camelot), mood, energy in the tab, never uploads | VERIFIED — 890 tests in the web suite | `web/README.md:3-4,26-40` |
| Albums shelf / coverflow, Drive tree, artist and album pages | VERIFIED | `web/CONTEXT.md:54-78`; `web/src/ui/router.ts:14-25` |
| Playlist authoring, drafts, write to card | VERIFIED | `web/CONTEXT.md:331-355`; `web/src/playlists/` |
| Ask: a sentence becomes search **filters** run by the app, via Chrome's built-in model | PARTIAL — Chrome 148+ desktop, 22 GB free, 4 GB VRAM/16 GB RAM; 10/12 on a twelve-sentence spike | `web/README.md:186-203`; `docs/specs/k7-app.md:496-503` |
| Label Editor with real LVGL 9.6 in WASM, 15 WebMCP tools | VERIFIED | `tools/LabelEditor/`; `docs/specs/label-templates.md` |
| Card writer (writes tags, label JPEG and the card's files) | PARTIAL — the code exists (`web/src/card/write.ts:95-115`), the spec still lists "the writer is unbuilt" | `docs/specs/k7-app.md:516` |
| Chrome-only, stated rather than hidden | VERIFIED | `docs/specs/k7-site.md:111-115`; `Site.tsx` |

### 2.6 What the repo says about itself that is no longer true

This matters more than usual, because the README's "Known limitations" is exactly where a
marketer would go to be honest — and part of it is stale. Do not quote these without checking.

| README says | The tree says | Where |
|---|---|---|
| "there is no strip driver in the firmware yet" | the driver exists, runs at boot, six effects | `README.md:708-709` vs `components/app/strip/strip.c`, `appliance.c:312` |
| "Nobody has yet fetched `http://192.168.4.1/setup` from a client joined to that network" | the same README's own board table records the setup page, the join, the stream over the hotspot and the wrong-password path, proven | `README.md:1258-1263` vs `README.md:550-556` |
| "Card removal is not hot-detected" | a `card_watch` task unmounts, reports and remounts | `README.md:1266` vs `app_services.c:341-420` |
| Snapshot is "format v4" | the code is v5 | `README.md:1200,1219` vs `snapshot.h:27` |
| the Snapshot flash path is "board-tier pending — no board has run them yet" | a board boot is recorded logging `snapshot: 1502 rows` | `README.md:1216` vs `README.md:536` |
| the app image is 1.70 MB with 59 % of a 4 MB partition free | 1.70/4.0 is ~42 % used → ~57 % free; and the 0.5.0 image quoted elsewhere is 2,073,008 B | `README.md:810-811` vs `README.md:535` |
| device examples use `:8000` | the device listens on 80 | `README.md:411,558` vs `appliance.c:38` |
| drift: `front-door.md` claims the device serves the Shell | the page is retired; the code 302s | `docs/specs/front-door.md:31-37` vs `front_door.c:127-152` |

INFERENCE: none of this is dishonesty. It is a fast-moving tree whose prose lags by days. But it
means **the marketing copy cannot be derived from the README; it has to be derived from the tree**,
and the first asset a campaign needs is a corrected, short "what it does today" page.

---

## 3. Technical differentiators

What is genuinely unusual, in the order I would defend it to an engineer.

**1. An MP3 *encoder* on an ESP32-S3, at 256 kbps, in about half a core.**
Almost nobody encodes MP3 on this part; the usual project decodes. Getting there took a measured
sequence: `-O3`, the encoder's hot loop placed in IRAM by a linker fragment (17 % on its own), and
its 95 880 B of state forced into internal RAM instead of PSRAM (a further 8 %). Cost fell from
33.4 ms to 13.8 ms per frame (`README.md:1043-1052`). The insight that makes 44.1 kHz affordable is
counter-intuitive and measured: **bit rate is nearly free, sample rate is not** — 64 → 320 kbps costs
10 % more CPU for five times the bits (`README.md:333-352`).

**2. It found and repaired a bug in the vendored encoder's bitstream, at runtime, per format.**
At 32 kHz/96 kbps `shine` writes a padding bit for a slot it never fills — 432 bytes emitted under a
header claiming 433, which no decoder can parse. `mi_encode.c` *measures* the format once by
encoding silence, asks which reading of the padding bit makes the stream tile, and clears the bit
when it is wrong (`README.md:1022-1041`; `components/portable/mini_icecast/src/mi_encode.c:58-78,122-151`).
That is codec work, not configuration.

**3. A broadcast architecture where the audio task never touches a socket.**
Each client gets a 10-second PSRAM ring and its own permanent sender task. Before that, a
synchronous send through lwIP's 5.7 KB buffer dropped listeners within 10–30 seconds
(`README.md:604-610`; `mini_icecast.c:1415-1418`).

**4. Reel kinematics driven by tape physics, not by a spinner.**
Phase integrates 47.625 mm/s at each pack's own radius, so supply and take-up counter-rotate at
different speeds; the 8-second reel change is bounded by *both* the new track's elapsed time and the
wall clock, precisely because a starving broadcast once stretched 7 seconds of tape into 13.9
seconds of wall time (`cassette.h:38-44,65-84`).

**5. The picture and the broadcast share one core budget by construction.**
LVGL gets an explicit PSRAM allocator; the panel's stack is taken *before* Wi-Fi's DMA buffers; the
first scan-out waits until the boot's flash writes are done; the reel tick yields when the broadcast
lags. Bring-up order is the difference between booting and a 20-reset loop, and the repo records the
failure five different ways (`README.md:813-851`).

**6. A browser app that measures music with real models, offline.**
Essentia.js + ONNX in workers, weights hash-pinned, results keyed by content hash and analyser
version, and a principled rule that a tagged tempo/key/mood/energy is trusted and marked rather than
re-measured (`web/README.md:5-9`; `docs/adr/0009-tagged-fields.md`). Mood and energy models are
academic (MTG/UPF), which is also its licence problem (§5.3).

**7. An MCP surface, on a cassette player.**
The device exposes a read-only MCP endpoint over mDNS (`_mcp._tcp`), and the Label Editor exposes 15
WebMCP tools to a browser agent (`README.md:297`; `tools/LabelEditor/src/webmcp/tools.ts`). This is
ahead of the market rather than behind it, and it is the least "retro" thing in the product.

**8. The documentation discipline itself.**
96 ADRs that are superseded rather than rewritten, a rule that a phase is done only on host *and*
board, a measured claims table, and specs that say what is *not* established. This is rare enough to
be a marketable property of the *project*, not just of the object — see Phase 7.

INFERENCE: the technical story is not "old technology, new trick". It is **a small, hard real-time
system that was measured into existence**, and the measurements are the content.

---

## 4. The physical and sensory experience

### What exists (a screen, a light, and glass)

- **Sight.** A near-black page (`#17181a`) with one orange (`#F15A22`) and a neutral ink
  (`#a6a8a8`) (`docs/specs/k7-look.md:98-109`). A drawn compact cassette with two reels, a 291×87
  window and a 426×272 tinted "Card" that carries the track's cover art
  (`README.md:626-636`). Four rungs: **Real** (white page, cover printed on the cassette body),
  **Art** (dark page, orange line art), **Initial** (the bare cassette) and **Peek** (timeline and
  tags) (`README.md:711-716`). The reels turn at tape speed; on a track change the tape winds for
  eight seconds and lands on normal play. A K7 mark holds on black while the card is scanned
  (`docs/adr/0045`). The backlight falls to 60 %, 30 % and 0 % at 2, 5 and 10 minutes without a
  touch (`README.md:790-801`).
- **Touch.** A 5-inch capacitive panel (size INFERENCE: the repo records no panel dimension,
  `docs/research/player-menu-ipod-style.md:351-354`). No wheel, no knob, no button: the device is
  touch-only. Mode bar of three 96 px discs, a six-cell group block, knobs that are 44 px caps
  dragged 2 px a step with a magnet at neutral, a 3× dial after a 260 ms tap. The whole grammar is
  documented to the pixel in `README.md:657-745`.
- **Hearing.** **Nothing from the device.** The audio leaves as a network stream and is decoded
  elsewhere. What the device contributes to the sound is a 3-band EQ and a trim, before the encoder.
- **Light.** Eight addressable pixels, six effects (OFF, SOLID, BREATHE, RAINBOW, CHASE, LEVEL),
  deliberately dimmed to a quarter of the part's power because full white browned the board out
  (`README.md:274-282`; `docs/specs/led-strip.md`). It "starts dark" — the effect knob is neutral at
  OFF.

### What does not exist (and this is the campaign's central problem)

- **No object you can hold.** No case, no materials, no weight, no finish, no ports on a
  faceplate. The physical thing is a dev board with a screen and a card.
- **No sound in the room.** The pitch "turns a room into a radio station" requires a speaker that
  the product does not contain and does not ship with. It is true as a *network* claim and false as
  an *appliance* claim.
- **No photograph.** The repo has ten board screen captures and a contact sheet
  (`docs/images/screens/`), one panel PNG used as the site hero (`web/public/site/panel.png`), and
  no photograph of a device — stated twice as a known hole
  (`docs/specs/k7-site.md:19-20,315`).

INFERENCE: the sensory pitch that is *true today* is: **a screen that behaves like a machine** —
the reels follow the tape, the knobs resist, the tape winds, the light is a light. The pitch that is
*not* true today is anything about the object in your hand or the sound in the room.

---

## 5. Constraints and risks

### 5.1 Hardware

- **Power.** Eight WS2812B at full white draw ~0.5 A and browned the board out; the firmware caps
  every frame at 25 % (~120 mA) and the strip is "deliberately dimmer than the part can go until it
  has a supply of its own" (`README.md:274-282`). There is no battery and no charger.
- **Supply integrity on the strip.** Data is 3.3 V into a part whose VIH is 3.5 V; the fix is a
  4.5 V supply or a level shifter, and it is an open item (`docs/specs/led-strip.md:34-37,123`).
- **Pins.** The Sunton board leaves four free GPIOs — 0, 17, 43, 44 — two of which are the console's
  UART. A 3-wire I2S DAC cannot be added to this board without losing flashing
  (`docs/research/server/s15-i2s-dac-output.md:619-646`). **The board that has the screen is the
  board that cannot easily make sound.**
- **Memory.** ~11 KB of internal RAM is the working minimum during a track; the largest single free
  internal block is 18 432 B, and Wi-Fi wants ~25 KB contiguous (`README.md:1229-1242`). Every new
  subsystem is a budget decision.
- **CPU.** The encoder uses 41.4 % of one core on the devkit and 58.8–59.9 % on the panel board —
  above the spec's own 35 % target, admitted in three places (`README.md:373-386,561,594-595`).
- **One board.** Two boards have been exercised, both the author's (`README.md:527,583`). "One
  board, one build" (`docs/research/cassette-render-cost.md:571`).
- **Two devices on one LAN collide** — the hostname is fixed at `k7.local` (`README.md:1264-1265`).

### 5.2 Missing features that block a product

No OTA, no authentication, no TLS, no battery, no audio out, no case, no BOM, no assembly guide, no
certification, no support channel, no second unit, no price. The repo names OTA and provisioning as
"hard blockers" for selling to strangers (`docs/research/marketing/opportunities.md:447-448`);
provisioning has since been built (`docs/specs/wifi-provisioning.md`), OTA has not.

### 5.3 Legal and licensing — the sharpest edges

This is the part where the project is clean and the *commercial* story is not. Facts only; a lawyer
is needed for the calls.

| Issue | State | Evidence |
|---|---|---|
| Firmware licence | **MIT**, copyright "the SD stream server authors" — a collective, not an entity | `LICENSE:1-3` |
| Browser app licence | **AGPL-3.0-only** — and the Site says so in the footer | `web/LICENSE:1`; `web/package.json:6`; `web/src/site/Site.tsx:76` |
| Root `package.json` | declares **ISC** — a third, contradictory statement | `package.json:5` |
| Vendored MP3 encoder | **LGPL-2.0-only**, kept unmodified in its own component so it can be relinked; GPL-3.0 ports deliberately absent | `THIRD_PARTY_NOTICES:9-27`; `docs/adr/0002` |
| Analysis weights (ONNX) | **CC BY-NC** — the app's own NOTICE concludes "the combined application is non-commercial… Commercial use needs a proprietary licence from MTG" | `web/NOTICE:25-28`; `web/README.md` licence section |
| Fonts | Archivo/Barlow/IBM Plex are OFL-1.1 and named. **Jura is in the firmware but not in `THIRD_PARTY_NOTICES`**; `assets/fonts/xtape.ttf` (the icon font) has no licence statement anywhere | `THIRD_PARTY_NOTICES:71-92`; `LICENSES/OFL-1.1.txt` |
| "Cassette" as imagery | Generic line art, no brand marks in the SVGs; the trade dress risk is in **photography of the device next to real tapes**, not in the drawings | `resources/cassette-800x480.svg`; INFERENCE |
| Trade marks | No `®`/`™` anywhere; "K7" clearance is flagged as needed and not done | `docs/research/marketing/campaign.md:833` |
| User music | The global premise is that the device never serves or accepts a Track; all processing is outside it | `docs/specs/CONTEXT.md:9-30` |
| Public performance | A LAN stream played in a café is the customer's licence problem, not the device's — but a marketing image of a café is a claim | `docs/research/service-surface-and-opportunities.md:307`; INFERENCE |

The single most consequential line in the whole repo for a paid campaign:
**`web/NOTICE:28` — "the combined application is non-commercial."**
INFERENCE: the firmware is sellable (MIT); a bundle whose value depends on the analyser and Ask is
not, until the weights are licensed or replaced. Anything sold before that has to be sold as
*hardware + MIT firmware*, with the app offered separately and free.

### 5.4 Reputational risks specific to this product

- **The cassette purist.** `r/cassetteculture` will identify an SD card in two seconds. The honest
  line is already written in the repo and it is the good one: *"we did not bring back the tape; we
  brought back what you did with it"* (`docs/research/marketing/campaign.md:91`).
- **The privacy claim.** "Nothing leaves your machine" is true of the analyser and false of the
  device, which serves the LAN without authentication (`README.md:23-39`). The two must always be
  said in that order, or a journalist writes the correction.
- **The AI claim.** Ask writes filters, never results, and is measured at 10/12 on twelve sentences
  (`docs/specs/k7-app.md:496-500`). "AI DJ" is the fastest way to lose the audience that would
  otherwise trust the rest.
- **Credibility asymmetry.** This project's voice is evidence-first. One unmeasured number in a
  video will be found in the comments, because the README is public and says otherwise.

---

## 6. Existing brand signals

### Name

**K7.** Chosen because "that is what a cassette is called in French, and the thing is shaped like
one" (`docs/adr/0062-xtape-becomes-k7.md:3`). Every identifier was renamed with three read-both shims
for cards already in the world; the history was deliberately not rewritten. The rename tells you
something about the author: *"a record that edits itself is not a record"* (`0062:24`).

INFERENCE: *K7* is a genuinely good name for France and a hard one everywhere else — it is a
common noun, an ordinary search term, and it collides with unrelated products. The prior campaign
already reached this conclusion and requires every keyword to be paired: `K7 cassette`,
`K7 tape player`, `#K7tape` (`docs/research/marketing/campaign.md:296-306`). I agree with that and
would go further in Phase 2.

### Palette

| Token | Value | Use |
|---|---|---|
| background | `#17181a` (device page `#111111`) | the page, dark only |
| primary | `#F15A22` | the one accent; ink, not gloss |
| ink | `#a6a8a8` / dim `#858787` | text |
| line | `#2e3030` | 1 px strokes, never thicker |
| surface / well | `#222425` / `#121314` | panels, insets |
| alert | `#F2C94C` | the one extra hue, only for "device unreachable" |

Contrast is measured rather than assumed: accent 5.3:1, ink 7.4:1, dim 4.9:1
(`docs/specs/k7-look.md:98-128`). The favicon is the same idea in 32 px: a black rounded square with
two orange circles — two reels (`web/index.html:14`).

### Typography

**Jura, weight 300, for everything.** Hierarchy comes from size, not weight; the only 500 is a
micro-label chip; numbers are tabular. There is no second display face
(`docs/specs/k7-look.md:130-136`). The cassette labels are a separate, correct system: paper
`#F5F1E6`, ink `#242424`, rule `#8E7E4A`, with Archivo Narrow 600, Barlow Condensed 700 and IBM Plex
Mono 500 (`docs/specs/label-templates.md:49-84`).

INFERENCE: Jura at 300 is a *technical* face — geometric, quiet, slightly cold. It is the opposite
of a nostalgia-brand serif, and that is the point: the voice is "a technician who loves music", not
"remember the nineties".

### Motion

Mechanical, not smooth. Nothing is eased, bounced, blurred or faded in; overlays cut, or fade
opacity over ≤100 ms linear; `transition-all` and `zoom-in` were removed; route changes are cuts;
under `prefers-reduced-motion` motion is **none**, not gentler. The one exception is the coverflow's
300 ms turn (`docs/specs/k7-look.md:526-547`). On the device, the reel tick yields to the broadcast.

### Voice

Dry, precise, warm, never hyped; failure copy is a next step rather than an apology. Verbatim, the
best of it:

> "A USB-C cable that carries data, not just power." — `web/src/site/Flash.tsx:174-175`

> "That is all. Nothing is written until you confirm." — `Flash.tsx:182-183`

> "K7 needs Chrome on a computer … an installed icon would open a closed door."
> — `web/src/site/Install.tsx:59-63`

> "The live stream keeps playing while the page cannot ask what it is playing."
> — `web/src/ui/areas/Player.tsx:114-115`

> "Do not prescribe a mood the library does not have." — commit `9d68b55`

> "Never serve a Track, and delete the route that did." — commit `dede061`

> "a record that edits itself is not a record" — `docs/adr/0062:24`

> "That is the product's decision, not a defect to route around." — `docs/adr/0067:31`

> "It is **not** safe on an untrusted network." — `README.md:38`

That last one is the whole brand in six words: it would rather lose a sale than let you
misunderstand it.

### Existing copy and existing claims

- Live title: **"K7 — your music, on a cassette"** (`web/index.html:15`).
- Live hero: **"For people who own their music and miss the object it came in, K7 is a
  cassette-shaped player that turns a card into a tape and a room into a radio station."**
  (`web/src/site/Site.tsx:42-44`).
- CTAs: **"Flash a board"** / **"Install the app"** (`Site.tsx:47-50`).
- Footer: **"K7 is free software, AGPL-3.0-only. The player runs K7's firmware; the App runs in your
  browser."** (`Site.tsx:76`).
- The prior plan already contains a table of claims to **refuse** — "the cassette is back", "hi-fi",
  "pause/seek", "multi-room", "private and secure", "AI DJ", "available now" — each with the true
  sentence beside it (`docs/research/marketing/campaign.md:76-84`). I will not re-derive it; I will
  extend and tighten it in Phase 4.

### Signals from outside the repo (2026-11-16)

- **The site is live.** `https://k7.xperiments.dev/` → 200, Vercel, `last-modified` 2026-11-14, CSP
  with no analytics and no third-party script (`curl`).
- **The repository is public** — `github.com/xperiments/k7`, created 2026-10-31, **0 stars, 0 forks,
  no description, no topics**, `homepage` still points at `k7-chi.vercel.app` (GitHub API).
- **Three firmware releases** are published: 0.5.0, 0.6.0, 0.6.1 (`web/public/firmware/`,
  `web/firmware.json`).
- **The author** is Pedro Casaubon (`xperiments`), 145 followers, 115 public repositories — visible
  in git log and the GitHub API. No company, no entity, no studio name anywhere in the tree.

INFERENCE: the product has a *brand* and has had no *audience*. Zero stars on a public repo with
this much in it is not a quality signal — it is evidence that nothing has been announced yet. That
is the good news in this report: the campaign is not a relaunch, it is a first contact.

---

## 7. What I could not establish

Listed so it is not mistaken for silence.

1. **Any photograph of the device.** None exists. The site's hero is the panel's own drawing, in a
   slot built for a photo (`docs/specs/k7-site.md:103-106`).
2. **Any measurement of the device as an object**: weight, dimensions, materials, thermal behaviour,
   power draw from the wall.
3. **Whether a member of the public has ever built one.** No issue, no fork, no testimonial.
4. **Whether the touch UI has ever been used by a finger other than the author's** — the repo says
   the board tier has not recorded one (`README.md:732-733`), and the design tickets are closed,
   which is not the same thing.
5. **A true number for "sounds better than X".** The repo explicitly refuses to settle audio quality
   by measurement (`README.md:359-365`) and says `shine` is below LAME (`README.md:1274-1276`).
6. **A price.** Every figure in the repo is a planning estimate, labelled as one
   (`docs/research/marketing/opportunities.md:273-301`).
7. **Whether the Catalan reading of K7 is real.** The ADR and the prior campaign assert it; the
   campaign's own market file says "Not supported" (`docs/research/marketing/market-2026-11-03.md:70`).
   I treat K7 as French and will not build a market on the Catalan pun without a native check.

---

## 8. The honest one-paragraph summary I would give a journalist

> K7 is a cassette deck made of software. A five-inch touch panel shows a compact cassette whose
> reels turn at the speed the tape would; the tape is a microSD card; the deck is your network. It
> re-encodes whatever the card holds into one continuous MP3 stream, so every device in the room
> hears the same moment, and it has no account, no cloud, no recommendations and no seek bar — by
> design, not by omission. A free browser app measures your library, writes playlists onto the card,
> and prints a label onto the deck's screen. It is MIT firmware and AGPL app, it costs nothing to
> build if you have the board, and it does not yet exist as an object you can buy: there is no case,
> no speaker and no price. The author documented the gap more carefully than most products document
> their strengths, which is either the most trustworthy thing about it or the most frustrating,
> depending on whether you wanted a product or a project.
