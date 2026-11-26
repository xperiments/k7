# Can the ESP32-S3 also stream someone else's internet radio?

**Status:** research · **Updated:** 2026-11-13

Date: **2026-11-12**. Reconciles the 2026-10-16 player research (`03`, `04`, `05`, `07`,
`radio-player-*`, `server/s15`) with the firmware as it is today, re-verifies the load-bearing
claims at the IDF tag this repo builds (`/Users/pedrocasaubon/esp/esp-idf`,
`tools/cmake/version.cmake:1-3` → 5.4.4), and adds the simultaneous-use analysis, which is new.

Labels: **VERIFIED** = read in the source that owns it, or executed in this pass.
**INFERENCE** = derived from verified material. **UNVERIFIED** = not checkable here, and why.
IDF citations are `IDF5.4:path:line`; repo citations are `path:line`.

## Verdict

- **(a) The chip, at all: yes.** An S3 decodes MP3 and HE-AAC inside the budget (MP3 28 KB /
  8.17 % of one core, AAC 51.2 KB / 6.75 % on an S3R8) and has two I2S peripherals, PDM TX and TLS.
  **VERIFIED.** It cannot make sound without a pin or two on an external output: no DAC (`07`).
- **(b) This firmware, at the same time as it serves its own stream: unproven, and tight.** A
  decoder + its 20 KB stack + an HTTP client is ~51–56 KB of internal RAM. The only measured case
  with a listener is the **setup** network (AP up, a client streaming): **43,815 B free, largest
  block 31,744 B** (`README.md:549`), which cannot hold it. The device's *normal* mode —
  provisioned, no AP, a client streaming — is **not measured**; the nearest bracket is 123,183 B
  free / 49,152 B contiguous with the AP up and no listener (`README.md:548`), which would leave
  roughly 70 KB, above Wi-Fi's ~25 KB. So (b) is a measurement to take, not a settled no. The
  Sunton's encoder already holds **58.8–59.9 % of one core** (`README.md:594-595`), against the
  devkit's 41.4 %.
- **(c) The shape that does fit today is a relay/tuner, not a player.** Fetch and re-serve the
  station's compressed bytes to the K7 app: no decoder, no I2S, no pins, no SBR problem, a ring in
  PSRAM. **INFERENCE from §5**, and it is the cheapest path to a yes.
- **The hard wall on the Sunton is pins.** Four free GPIOs exist (`0, 17, 43, 44`), two are the
  console and one, 17, is already the LED strip. A 3-wire I2S DAC is impossible; one-pin PDM TX
  is the only local output, and it now costs the strip.

## 1. The chain a radio client needs

| Link | On the S3 | In this firmware | Where it is limited |
|---|---|---|---|
| URL → redirect → `.pls`/`.m3u` | works (`esp_http_client`: 30x, 10 hops; PLS is text) | **absent** — no outbound HTTP anywhere | firmware |
| HTTP/ICY status line + `icy-*` headers | works, `ICY` branch present (§2.1) | absent | firmware |
| ICY metadata demux (`metaint`, 16-byte blocks) | byte arithmetic | **half present**: `mi_icy.c` writes blocks, nothing reads them | firmware |
| ADTS / MP3 framing | inside the decoders | MP3 framing for files (`mi_decode.c`); no ADTS | firmware |
| decode | works (§2.3) | MP3/FLAC/WAV yes, **AAC no** (`components/portable/mini_icecast/include/mi_decode.h:3-4`) | firmware for AAC |
| rate conversion / clocking | `mi_resample.c`, measured (`audio-decode-cost.md`) | present and measured | CPU |
| I2S / PDM out | works; exact 44 100 Hz divider (`s15` §3.2) | **absent** — no `i2s_*` call but a comment (`strip.c:47-51`) | firmware |
| amp / speaker | needs a chip (no DAC, `07`) | absent | **board** |

Transport and codecs are S3-solved; the output is board-limited; the client, the AAC decoder and
the I2S sink are firmware-absent.

## 2. What is verified now

### 2.1 Transport, re-executed at v5.4.4

`03-transport-verified-locally.md` was anchored on v5.4.2/v5.5.5. I compiled `http_parser.c` from
the 5.4.4 tree standalone (`cc t.c http_parser.c`, `HTTP_PARSER_STRICT == 1`) and fed it the same
three responses. **VERIFIED, executed:**

| Response fed | Result |
|---|---|
| `ICY 200 OK` + `icy-name/genre/br/metaint` + `content-type` | `HPE_OK`, `status_code=200`, `http=0.0`, every `icy-*` header surfaced, `content_length=18446744073709551615` (`ULLONG_MAX`), `http_should_keep_alive()=0` |
| `HTTP/1.0 200 OK` + `icy-metaint: 8192` + `audio/aacp` | `HPE_OK`, `status_code=200`, `http=1.0`, `icy-metaint` surfaced |
| `HTTP/1.1 200 OK` + `Content-Length: 42` | `HPE_OK`, `status_code=200`, `content_length=42` |

The branch is there: `IDF5.4:components/http_parser/http_parser.c:781-782` (`case 'I'` →
`s_res_I`), `:796-804` (`s_res_I`/`s_res_IC` jumping to `s_res_http_minor`, so the space after
`ICY` is legal), reversed `HTTP/1.x 200` at `:748-751`. `esp_http_client_get_header()` still reads
**request** headers (`IDF5.4:components/esp_http_client/esp_http_client.c:369-376`);
response headers exist only in `HTTP_EVENT_ON_HEADER`, in wire case. The client component is
**compiled but not linked**: `libesp_http_client.a` is on the link line and **no
`esp_http_client_*` symbol is placed** in `build/headless/sd_stream_server.map` — new code, not a
free dependency (default rx buffer 512 B, `IDF5.4:…/include/esp_http_client.h:19`).

### 2.2 SoC facts (`IDF5.4:components/soc/esp32s3/include/soc/soc_caps.h`)

```
:59 SOC_I2S_SUPPORTED 1   :227 SOC_I2S_NUM (2U)   :233 SOC_I2S_SUPPORTS_PDM_TX (1)
:144 SOC_CPU_CORES_NUM 2   :237 SOC_I2S_SUPPORTS_TDM (1)   :168-169 GDMA 1 group x 5 pairs
```

`grep -c DAC` there is **0** — `07`'s no-DAC conclusion re-verified. Memory windows
(`IDF5.4:…/soc/soc.h:172-175`: 448 KB IRAM, 480 KB DRAM, partly aliased) as `07` said; console pads
(`IDF5.4:…/soc/uart_pins.h:23-24`) are `U0RXD 44`, `U0TXD 43`.

### 2.3 The decoder library (registry, fetched today)

Registry API, `espressif/esp_audio_codec`: latest **2.6.2** (published 2026-08-07; the October note
said 2.6.2 — still current), `idf >=4.4`, licence "Custom" = **Espressif Modified MIT**
("EXCLUSIVELY with Espressif Systems products"; redistribution for non-Espressif products
prohibited), SoC table lists **ESP32-S3 supported**. **VERIFIED.** The costs the repo quotes are
Espressif's own, re-confirmed verbatim at `…/2.6.2/readme.md:275-293`:

| Decoder | Rate | Heap | CPU (1 core) | Stack |
|---|---|---|---|---|
| AAC (tested as **AAC-LC**) | 48 000 | 51.2 KB | 6.75 % | ~20 K for all decoders |
| MP3 | 44 100 | 28 KB | 8.17 % | same |

"decoding AAC-Plus profile will have higher memory and CPU usage", **with no number** — the
HE-AAC cost stays UNVERIFIED, and 48/51 SomaFM channels' low tiers are HE-AAC (`04` §6).

### 2.4 The Sunton pin budget

16 data + 4 sync (`panel_rgb.c:45-46,108-111`) + 4 touch (`touch_gt911.c:42-45`) + backlight
(`backlight.c:30`) + 4 card (`board_esp32.c:54-57`) + 12 flash/octal-PSRAM = **41 of 45**, leaving
`0, 17, 43, 44`. 43/44 are UART0 and there is no other console
(`sdkconfig.sunton:1917-1935`: `ESP_CONSOLE_UART_DEFAULT=y`, `UART_NUM=0`); the touch owns 19/20,
so USB-Serial-JTAG is not a console either. **VERIFIED.** Two corrections: the
`boards/sunton-8048s050c.json` that `s15:621` and `sdkconfig.defaults.sunton:5` cite **no longer
exists**, and GPIO 17 is already `STRIP_GPIO` (`strip.c:47-51`), which `s15` did not know: a
one-pin PDM TX means taking 17 from the strip or using GPIO 0 (BOOT strap, fights flashing). PDM TX
is mono, speaker-grade, no volume register — **VERIFIED** (`s15` §6.3).

### 2.5 Headroom, by mode

| Mode (measured) | internal free | largest block | Source |
|---|---|---|---|
| Boot, panel up, before the network starts | 199,171 B | 118,784 B | `.scratch/monitor-inboard.log:98` (2026-11-08; transient log, cross-check only — Wi-Fi init begins at line 100) |
| Setup AP up, no listener | 123,183 B | 49,152 B | `README.md:548` |
| **Setup AP up + a client streaming** (unprovisioned; the only measured listener case) | **43,815 B** | **31,744 B** | `README.md:549` |
| **Provisioned, no AP, a client on `/stream`** | **not measured** | **not measured** | — |
| Encoder state forced internal (pre-2026-10-16 config, not shipped) | 35,327 B | 18,432 B | `README.md:1220` |
| Hour soak, encoder-internal era | 18,751 B steady | min 11,067 B | `README.md:561`, `docs/design/sd-stream-server/issues/06-encoder-cost-and-memory-telemetry.md:128-132` |

The row that decides (b) is the missing one: nobody has logged free internal on a *provisioned*
device with the panel up and a client on `/stream` with no setup AP. The setup-network pair is the
worst case — it carries an AP, its DHCP server, mDNS on a second interface and the setup server,
none of which a normal device runs — not the normal one; measure the normal one before believing
either verdict.

**The 35,327 / 18,432 pair is history, not headroom.** `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` is
**4096** (`sdkconfig.defaults:57`, `sdkconfig:1895`): the lever `README.md:1236` calls an option
(+7.6 % CPU, 41.4 → 44.5 %, for +119 KB and largest 18 432 → 77 824) **has already been pulled**,
and `sdkconfig.defaults.sunton:55-62` states the result (~155 KB free, ~77 KB contiguous) and that
Wi-Fi's ~25–50 KB is what bought it. A current board logs `encoder: 4100 B of internal RAM taken`
(`.scratch/monitor-inboard.log:164`); the 102,408 B at `README.md:242,261,539` is a stale excerpt.

Encoder cost by board: **41.4 %** of one core on the devkit (44.1 kHz/256 kbps, flat over an hour,
0 rebases — `README.md:561,573-575`); **58.8 % idle / 59.9 % streaming on the Sunton**, 0 rebases
(`README.md:594-595`), the panel taking ~17 points by streaming ~32 MB/s out of the same PSRAM;
with the hotspot up, 64.0 % vs 52.9 % and still 0 rebases (`README.md:556`).

### 2.6 TLS: present and configured — "no TLS" is about the product surface

Client HTTPS on (`sdkconfig:1476`), engine `ESP_TLS_USING_MBEDTLS` (`:1154`), full certificate
bundle capped at 200 certs (`:3245-3261`), asymmetric record buffers 16384 in / 4096 out
(`:3196-3200`), dynamic buffers **off** (`sdkconfig.sunton:3176`), mbedTLS allocations in PSRAM on
the Sunton only (`sdkconfig.sunton:3166` vs `sdkconfig:3190` off). The HTTPS *server* Kconfig is on
for the Sunton (`sdkconfig.sunton:1520`) and off in the devkit (`sdkconfig:1535`), but nothing
places `httpd_ssl_start` — an unused option, not a live listener.

mbedTLS objects **are placed in the image** (`mbedtls_ssl_setup`,
`build/headless/sd_stream_server.map:141021`): the engine is already in flash, so an `https://`
station adds **runtime** RAM, not code — ~20 KB of record buffers per connection plus the
handshake's transient peak (**INFERENCE**; the Kconfig arithmetic is verified, the peak is not). Those buffers land in PSRAM on the Sunton, internal on the devkit. The
README's "no TLS" (`README.md:1241`) means the device's own API is plain HTTP, as
`mini_icecast.c:1422-1425` says outright: "/stream is the one route on the plain listener: port 80,
no TLS, ever".

### 2.7 APSTA, and why bandwidth is not the question

`esp_wifi_set_mode(WIFI_MODE_APSTA)` is already used (`board_esp32.c:361`; the mode is at
`IDF5.4:…/esp_wifi_types_generic.h:27`) — **VERIFIED in this firmware**, not just documented.
Serving a client over its own AP while associated is measured: 132–146 KB in four seconds
(`README.md:551`), and eight minutes of AP + client streaming at 0 starvation rebases
(`README.md:556`). ~128 kbit/s in + ~256 kbit/s out ≈ 48 KB/s against a Mbps-class link: **the
constraint is internal RAM and the already-59 %-busy core, not the radio.**

### 2.8 What is actually absent (one correction)

No audio output: the only `i2s` mention in `main/` or `components/app/` is the comment at
`strip.c:47-51`, and no `i2s_*` symbol is placed. **VERIFIED.** The decode premise needs one
correction: the firmware **already decodes MP3**, not just FLAC —
`components/portable/mini_icecast/include/mi_decode.h:3-4` ("One decoder API for .mp3 / .flac /
.wav"), `mi_decode.c:12,23,26` (`dr_mp3.h`, `dr_flac.h`, `dr_wav.h`), and the card accepts them
(`README.md:1114-1116`). What is missing is an **AAC/ADTS** decoder and a **push** entry point:
`mi_dec_*` is file-shaped, the shape `radio-player-codecs.md:16-24` says must be replaced for a
stream. MP3 radio is a *shim* problem; AAC radio a *new dependency* problem.

## 3. Reconciliation with the 2026-10-16 research

| Prior claim | Status today | What to say instead |
|---|---|---|
| `07`/`03` anchored on v5.4.2 (`03` also 5.5.5) | the build is **v5.4.4** | re-verified this pass; nothing changed (§2.1, §2.2) |
| `http_parser` accepts `ICY`, surfaces `icy-*`, `status=200`, `content_length=ULLONG_MAX`, `keep_alive=0`; `get_header()` reads request headers | both hold, re-executed at 5.4.4 | keep |
| `04`: `HTTP/1.1`, `icy-metaint: 16000`, `ice-audio-info`; UA gating; `https` PLS mirrors | reproduced live today | keep |
| `05`: `aacp` is ADTS, LC profile, 22050 Hz, SBR implicit | not re-captured | keep; a decoder property |
| `esp_audio_codec` 2.6.2, `idf >= 4.4`, AAC 51.2 KB/6.75 %, MP3 28 KB/8.17 % | 2.6.2 still latest; numbers verbatim | keep |
| `s15`: four free GPIOs, no 3-wire DAC, PDM TX the one-pin path | holds — **but 17 is the strip's** | the only clean pin left for audio is 0 (BOOT), or take 17 from the strip / 43–44 from the console |
| `s15`: profile `boards/sunton-8048s050c.json` | **gone from the tree** | cite `sdkconfig.sunton:1917-1935` for the console |
| `README`: 35,327 B / 18,432 B as the internal-RAM picture | true only at `ALWAYSINTERNAL=131072`; the tree ships 4096 | today's numbers are §2.5; the 35 KB world costs a config change |
| `README`: "102408 B of internal RAM taken" | stale log from that world | current boards log 4,100 B |
| encoder ≈41.4 % | devkit only | **59 % on the Sunton**, the board this question is about |
| task premise "only FLAC decode" | **wrong** | MP3+FLAC+WAV are decoded; AAC is the gap |

## 4. Does a radio client fit at the same time? (new)

**Internal RAM, MP3 over HTTP:** decoder 28 KB + one contiguous ~20 KB task stack + a 2–8 KB client
rx buffer + a compressed ring in **PSRAM** (48–96 KB for 3–6 s at 128 kbit/s) = **~51–56 KB
internal**, ~71–76 KB if the station is `https://` on the devkit config; AAC instead is +23 KB.

| Mode | Free internal | Fits 51–56 KB? | Fits 71–76 KB (https)? |
|---|---|---|---|
| Boot, panel up, Wi-Fi not yet up | 199 KB / 118 KB block | yes, but Wi-Fi's ~25–50 KB has not been taken yet | marginal |
| Setup AP, no listener | 123 KB / 49 KB block | marginally (~70 KB left before Wi-Fi's share) | marginal |
| **Setup AP + client streaming** (worst measured) | **43.8 KB / 31.7 KB block** | **no** | **no** |
| Provisioned, no AP, client streaming | **not measured** | **unknown — the number that decides (b)** | unknown |
| Encoder forced internal | 35.3 KB / 18.4 KB block | no (18.4 < 20 KB stack) | no |

**CPU is not the blocker; RAM is.** Decode is 8.17 % of one core for MP3 at 44.1 kHz stereo
(Espressif, S3R8); the decode/HTTP/ICY tasks would sit on core 0, with Wi-Fi, lwIP and the panel,
while the encoder holds ~59 % of core 1 on the Sunton. Plausible, unproven: **the bar is the soak's
`starvation rebases 0`** (`README.md:561,595`), not a percentage.

**Verdict for (b): unproven, and the measured worst case is a no.** It fits by arithmetic before
Wi-Fi starts and in the setup-AP-no-listener bracket; it does not fit in the one measured case with
a listener (43.8 KB for a ~51 KB client). The mode the question actually asks about — provisioned,
serving a client, no setup AP — has never been logged, and could land on either side. Take that
measurement first; do not sell a lever before it exists. Levers, with measured prices:

| Lever | Price (measured) | Buys |
|---|---|---|
| encoder state → PSRAM | **already pulled**: +7.6 % (41.4→44.5 %), ~119 KB (35 327→154 643, block 18 432→77 824) | the ~155 KB world; nothing left to sell |
| lower station rate | 44.1/256 = 52.9 %, 32/96 = 37.7 %, 22.05/64 = **23.6 %** synthetic (`README.md:337-341`); the corpus runs ~12 points cheaper | ~15–29 points of one core |
| drop the panel | 58.8–59.9 % on the Sunton vs 41.4 % on the devkit → **~17–18 points** | the panel's PSRAM bandwidth and its 20 pins |
| PDM instead of a DAC | 1 pin, mono, no volume, an RC low-pass | the pin wall, minus quality |
| a second board | ≥24 free pins (`s15` §6.2) | everything else |

## 5. Four shapes, with cost and risk

| | Shape | What it needs | Cost / risk |
|---|---|---|---|
| **A** | **Separate player firmware on a bare N16R8 devkit + 3-wire I2S DAC** (the already-researched Radio player) | `esp_http_client` + ICY reader + `esp_audio_codec` + a `mi_resample`-style converter + an I2S sink; 24 free pins, PCM5102A/MAX98357A on 17/18/16 | the whole chip to itself: ~51 KB internal against 200+ KB. Risk: a second firmware — and A does not answer "also", it is a different device |
| **B** | **Radio client inside this firmware, output via one-pin PDM TX** | all of A, plus PDM TX (mono, 16-bit, RC filter, no volume) on **17 taken from the strip** or 0 fighting BOOT | pins and RAM together: only after the RAM reprioritisation, and the strip dies. Speaker-grade mono |
| **C** | **This firmware + a real I2S DAC on a board that has the pins** | a board change: a devkit with a panel, or a Sunton variant with 3 spare GPIOs | B's firmware work; the honest answer if the user wants sound *and* the panel |
| **D** | **Relay/tuner: fetch the station, re-serve the compressed bytes** | `esp_http_client` + ICY passthrough + a second route; ring in PSRAM; no decoder, no resampler, no I2S, no pins | cheapest by far: one task, ~6–8 KB stack, tens of KB of **PSRAM**. Loses: no local sound, the metadata is the station's, and a relay cannot re-time or seek another clock |

**D is the cheapest path to a yes and is nearly all reuse.** The app already plays the device's
stream through an `<audio>` element pointed at `http://k7.local/stream`
(`web/src/device/direct.ts:43,161-165`, `local.ts:78`), and the cross-origin hop from the HTTPS app
to `http://k7.local` is measured under Local Network Access §2.5 with the `local-network`
permission (`docs/specs/k7-app.md:50`, A2). A relay endpoint is consumed by that same element:
**VERIFIED by code and by A2 for the origin pair; INFERENCE that a media element, unlike `fetch`,
needs nothing extra** (no CORS for a plain `<audio src>`, and no `crossOrigin` is set).

A product consequence, not a transport one: `/stream` is **one** broadcast — one route on the plain
listener (`mini_icecast.c:1425-1426`), fanned out by per-client send rings — so a relay needs a
second endpoint with its own fan-out, or a source switch where the station replaces the card walk.
The 1500 ms pacer lead (`s15` §1, §2.2) is the device's own clock: a relay inherits the station's
buffering and metadata interval and cannot pace it — it forwards, so its jitter is the station's.
**HLS (`.m3u8`) is a separate lift** (BBC's HE-AAC tiers, `radio-player-codecs.md:43`): segment
fetching plus a discontinuity-aware queue, out of scope for a first pass.

### 5.1 "No reconversion": what is byte-for-byte, and what is not

The relay is **byte-for-byte**: the S3 opens the station and the compressed frames it receives go to
each client unchanged. No decode, no encode, no resample — the station's codec, bit rate and sample
rate *are* the stream's. That is why it fits: the cost is memcpy plus sockets, not a codec. A
HE-AAC station needs no SBR work because the browser does it; a 64 kbps station stays 64 kbps.

What is **not** free, and is therefore the actual work list:

| Condition | What it costs | Source |
|---|---|---|
| The "xxx.com" is a **stream URL or a `.pls`/`.m3u`**, not a website | resolve redirects and playlists; a station's home page is not a stream | §2.1, `04` §5 |
| `Content-Type` follows the station (`audio/mpeg`, `audio/aacp`, `audio/aac`) | the route hardcodes `audio/mpeg` (`mini_icecast.c:386`) — one parameter | this file |
| ICY metadata | pure audio only if the station is not asked; now-playing needs it asked, `StreamTitle` parsed, and blocks re-emitted at the device's own `metaint` — the writer exists, the reader does not (§1) | `04` §2, `mi_icy.h` |
| A station that gates on `User-Agent` | send a browser UA — some stations refuse the ESP default | `04` §4 |
| A station that stalls | the relay inherits the station's clock and its dropouts; reconnect with backoff, no pacer, no seek | `radio-player-streaming.md` §5 |
| `https://` station | TLS client; ~20 KB of record buffers per connection, in PSRAM on the Sunton | §2.6 |
| Two sources on one `/stream` | a second route with its own fan-out, or a source switch | §5 |

So "type a URL and hear it in the app" is one task, one route, no codec, plus the rows above.
"Hear it on the device's speaker" is shape B and still needs the decoder and the pin.

## 6. What must be measured on the board before any claim

1. **MP3 decode µs/frame and internal heap on this firmware** — a push shim over `drmp3dec_*`
   (already linked) or `esp_audio_codec`. Espressif's 8.17 %/28 KB is an S3R8 figure, not this board.
2. **HE-AAC decode cost**, for which Espressif publishes no number and which `05` shows is the
   normal case at 32–64 kbps.
3. **Does PDM TX coexist with the RGB panel's LCD_CAM DMA?** Two of five GDMA pairs are used
   (panel, SPI2), PDM TX takes a third; the risk is PSRAM/cache contention with a ~32 MB/s scan-out.
4. **Do a decode task and the encoder fit on two cores without starvation?** Pass condition: the
   soak's `starvation rebases 0` (`README.md:561,595`) for an hour with a client attached.
5. **The largest-block question**, the real killer: after Wi-Fi, a 20 KB stack and a 28 KB decoder,
   is 25 KB of contiguous DMA-capable internal still there for `esp_wifi_init`? `README.md:1224-1232`
   records the reboot loop this caused once. (For **D**: the client task's internal cost and bytes/s
   with four send rings live, and whether a real phone plays an ICY relay through the LNA permission.)

## 7. What would make it a yes / a no

**A yes needs** one of: shape D; shape A/C on a board with pins; or shape B with the strip removed
and the RAM reprioritised. Local sound also needs an AAC decoder if the HE-AAC tiers are wanted, and
a measured MP3 decode cost under the starvation bar.

**A no stays a no while** the Sunton must keep 43/44 for the console and 17 for the strip; or the
one measured mode with a listener (43.8 KB) is what the normal mode also measures, against a ~51 KB
client; or the encoder stays at ~59 % of a core and the bar stays "0 starvation rebases"; or the
requirement is HE-AAC sound out of *this* board, which has no output to give it without losing the
strip or the console.

## 8. Sources

- Executed here: `cc` + `http_parser.{c,h}` from `/Users/pedrocasaubon/esp/esp-idf`
  (`tools/cmake/version.cmake:1-3` → 5.4.4), three responses (§2.1). IDF files:
  `http_parser.c:748-751,781-804`; `esp_http_client.c:369-376`, `include/esp_http_client.h:19`;
  `soc_caps.h:59,144,168-169,227,233,237`; `soc.h:172-175`; `uart_pins.h:23-24`;
  `esp_wifi_types_generic.h:27`.
- Registry: [esp_audio_codec 2.6.2](https://components.espressif.com/components/espressif/esp_audio_codec),
  [readme](https://components-file.espressif.com/components/espressif/esp_audio_codec/2.6.2/readme.md)
  (275–293), [license](https://components-file.espressif.com/components/espressif/esp_audio_codec/2.6.2/license.txt).
- Repo: `README.md:337-341,548-556,561,573-575,594-595,1114-1116,1220,1224-1237,1241-1243`;
  `sdkconfig:1154,1476,1535,1895,3190-3200,3245-3261`; `sdkconfig.sunton:1462,1520,1917-1935,3166-3176`;
  `sdkconfig.defaults:57`; `sdkconfig.defaults.sunton:5,45-62`; `panel_rgb.c:45-46,108-111`,
  `touch_gt911.c:42-45`, `backlight.c:30`, `board_esp32.c:54-57,361`, `strip.c:47-51`,
  `components/portable/mini_icecast/include/mi_decode.h:3-4`, `mi_decode.c:12,23,26`, `mini_icecast.c:336,1425-1426`;
  `build/headless/sd_stream_server.map:141021` (the Sunton map agrees: 0 `esp_http_client_*`, 3 `mbedtls_ssl_setup`);
  `web/src/device/direct.ts:43,161-165`,
  `local.ts:78`; `docs/specs/k7-app.md:50`.
- Prior research (all here): `07`, `03`, `04`, `05`, `radio-player-{streaming,codecs,audio-out}.md`,
  `server/s15-i2s-dac-output.md`, `audio-decode-cost.md`. Live probes (2026-11-12, one request each):
  `http://stream.radioparadise.com/mp3-128` → `HTTP/1.1 200 OK`, `audio/mpeg`,
  `icy-metaint: 16000`, `ice-audio-info: channels=2;samplerate=44100;bitrate=128`;
  `https://api.somafm.com/groovesalad64.pls` → three `https://ice*.somafm.com` mirrors, `Length=-1`.
