# Bluetooth A2DP as the K7Player's output: the feasibility box

**Status:** research · **Updated:** 2026-11-19

**The question.** A classic ESP32 (ESP32-D0WD, WROOM-32: 4 MB flash, 520 KB SRAM, no PSRAM) receives
a 128 kbps MP3 stream over ESP-NOW and plays it on an I2S line (PCM5102A). Could the same board
instead — or as well — be the **A2DP source** for a Bluetooth speaker (SBC encode, send)? As a
secondary: could it be an **A2DP sink** for a phone?

**The board and the firmware.** A single-app 1 MB partition, no PSRAM, an 891 712 B image, 38–40 MP3
frames/s over ESP-NOW, ~74 788 B internal heap free / 38 912 B largest while playing
([`player/build/k7player.bin`](../../player/build/k7player.bin); heap from the bench log quoted for
this note, with ADR 0102's census at 85 KB/43 KB: [ADR 0102](../adr/0102-the-players-ring-is-96-kb-and-clears-itself-rather-than-dropping-frames.md)
lines 37, 83). The firmware is ESP-NOW only ([ADR 0105](../adr/0105-the-player-is-esp-now-only.md)),
`WIFI_MODE_STA` with the channel set directly and **no association**
([`player/components/board_player/board_player_esp32.c`](../../player/components/board_player/board_player_esp32.c)
lines 283, 353).

**How to read this note.** Every claim carries one of three marks:

- **[measured]** — a number taken on the board, or read out of an artefact in this repository.
- **[read]** — read in the ESP-IDF 6.1 checkout at `~/esp/esp-idf-6.1` (paths below are relative to
  it) or in a published doc, with the file and line or the URL.
- **[inferred]** — my arithmetic or reasoning over the two above.

Two builds were made for this note: Espressif's own `a2dp_source` and `a2dp_sink_stream` examples,
copied to `/tmp` and built against the same IDF 6.1 checkout with `idf.py set-target esp32 && idf.py
build && idf.py size` (the repo was not touched; its `player/build` was read with the size tool
only). They are marked **[measured]** and named as such wherever they appear.

---

## 1. Supported at all: yes — and not on the S3 or the C3

**[read]** The ESP32's SoC caps declare Bluetooth Classic:

```
components/soc/esp32/include/soc/soc_caps.h:391
#define SOC_BT_CLASSIC_SUPPORTED        (1)    /*!< Support Bluetooth Classic hardware */
```

The ESP32-S3 and ESP32-C3 declare `SOC_BT_SUPPORTED` and `SOC_BLE_SUPPORTED` and **do not define
`SOC_BT_CLASSIC_SUPPORTED` at all** (`components/soc/esp32s3/include/soc/soc_caps.h:52,504`;
`components/soc/esp32c3/include/soc/soc_caps.h:34,411`). The Kconfig makes that structural rather
than cosmetic — the Classic option cannot even be offered on those chips:

```
components/bt/host/bluedroid/Kconfig.in:44-49
config BT_CLASSIC_ENABLED
    depends on BT_BLUEDROID_ENABLED && ((BT_CONTROLLER_ENABLED && SOC_BT_CLASSIC_SUPPORTED) || BT_CONTROLLER_DISABLED)
```

**[read]** The A2DP source API exists in IDF 6.1's Bluedroid, with the expected four calls:

| Call | Where |
|---|---|
| `esp_a2d_source_init()` | `components/bt/host/bluedroid/api/include/api/esp_a2dp_api.h:645` |
| `esp_a2d_source_connect(esp_bd_addr_t)` | same file, :735 |
| `esp_a2d_source_set_pref_mcc(conn_hdl, mcc)` (SBC capability, bitpool) | :662 |
| `esp_a2d_source_register_data_callback(cb)` — the stack **pulls PCM** from the app | `esp_a2dp_legacy_api.h:73`, callback type at :41 |

**[read]** The data callback is `int32_t (*)(uint8_t *buf, int32_t len)`: "buffer to be filled with
PCM data stream from higher layer", `len == -1` means flush, and it "is invoked in the context of
A2DP source task" (`esp_a2dp_legacy_api.h:33-41,61-63`). The stack owns the SBC encoder
(`components/bt/host/bluedroid/external/sbc/encoder/`), so the application hands over PCM at 44.1 or
48 kHz and the stack encodes; the alternative, `BT_A2DP_USE_EXTERNAL_CODEC`, moves the encoder into
the application ("The internal codec in A2DP will be remove in the future, it is recommend to use
external codec for new design" — `Kconfig.in:100-107`).

**[read]** The profile's mandatory codec is SBC and the others are optional (A2DP specification, Table 4.1,
whose rows carry each codec's section: SBC §4.3, MPEG-1,2 §4.4, MPEG-2,4 AAC §4.5, ATRAC §4.6 —
[Advanced Audio Distribution Profile Specification](https://www.bluetooth.org/DocMan/handlers/DownloadDoc.ashx?doc_id=303201)).
Espressif's own example says the same in one line: "If the internal A2DP codec is used, only SBC is
supported" (`examples/bluetooth/bluedroid/classic_bt/a2dp_source/README.md:87`).

**[read]** One source at a time, and source and sink are mutually exclusive: "A2DP source can
support at most one connection with remote A2DP sink devices. Also, A2DP source cannot be used
together with A2DP sink at the same time" (same README, :89).

**[read]** The one other part in this IDF with the same capability is the newer ESP32-S31
(`components/soc/esp32s31/include/soc/soc_caps.h:643` defines `SOC_BT_CLASSIC_SUPPORTED (1)`; it is
the second target the A2DP examples list). **[inferred]** So the owner's board is a supported one,
the API is not in doubt, and everything below is memory, flash and the radio.

---

## 2. Flash: the stack does not fit the partition the player uses today

**[measured]** Three images, same IDF 6.1, same `esp32` target, default single-app 1 MB partition:

| Image | app `.bin` | free in 0x100000 | IRAM | static DRAM |
|---|---|---|---|---|
| The player's shipping line build (`player/build/k7player.bin`, read with `esp_idf_size`) | 891 712 B | 156 864 B (15 %) | 89 859 B (68.6 %) | 176 175 B (97.5 %) |
| Espressif `a2dp_source` example, built for this note | 728 864 B (0xB1F20) | 322 528 B (30 %) | 86 299 B (65.8 %) | 54 715 B |
| Espressif `a2dp_sink_stream` example, built for this note | 760 864 B (0xB9C20) | 287 712 B (27 %) | 90 443 B (69.0 %) | 54 471 B |

**[measured]** The player *with* the Bluetooth stack, on the bench, in the uncommitted tree that
built it: **1.42 MB**, and "the stack does not fit the 1 MB app partition"
([`docs/design/k7player/issues/08-the-player-speaker-output.md`](../archive/design/k7player/issues/08-the-player-speaker-output.md)
lines 52–56). The same ticket records the partition table that was needed to hold it on 4 MB flash:
**nvs 24 K, phy 4 K, factory 3 MB** — leaving ~1.6 MB of the 4 MB unused.

**[inferred]** The increment is about **0.53 MB** over the line build's 891 712 B (1.42 MB is the
ticket's rounded figure; the delta is 0.53–0.60 MB depending on whether it means MB or MiB). That is
consistent with the examples above: Espressif's *minimal* source example is already 729 KB — 82 % of
the player's whole line image — before a decoder, Wi-Fi, ESP-NOW or a ring is added. The Bluetooth
stack is not a peripheral; it is a second firmware.

**[read]** A 1.5 MB app slot (`CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE`, 0x180000 = 1 572 864 B) would
just hold 1.42 MB with ~150 KB to spare, but leaves no growth and no second slot; the repo chose the
3 MB factory partition instead. **[inferred]** A two-OTA layout (two 1.5 MB slots) would not fit a
1.42 MB image with any margin worth shipping.

---

## 3. RAM: the controller's reservation alone is 75 % of the free heap

### 3.1 What the stack takes, from the IDF's own options

**[read]** The ESP32's Bluetooth controller reserves DRAM whether or not it is ever started:

```
components/bt/controller/esp32/Kconfig.in:542-544
config BTDM_RESERVE_DRAM
    hex
    default 0xdb5c if BT_ENABLED
    default 0
```

and the linker script spends it by moving the start of the application's static DRAM and shortening
it by the same amount:

```
components/esp_system/ld/esp32/memory.ld.in:26-27,81-82
#ifndef CONFIG_BTDM_RESERVE_DRAM
#define CONFIG_BTDM_RESERVE_DRAM 0
  dram0_0_seg (RW) : org = 0x3FFB0000 + CONFIG_BTDM_RESERVE_DRAM,
                     len = DRAM0_0_SEG_LEN - CONFIG_BTDM_RESERVE_DRAM
```

**0xDB5C = 56 156 B**, and `DRAM0_0_SEG_LEN` is 0x2C200 = 180 736 B, so with `BT_ENABLED` the static
data region is 124 580 B. The whole application DRAM budget (static + heap, since the heap runs to
0x40000000) drops from 327 680 B to 271 524 B: **−56 156 B, before a single Bluetooth buffer.**

**[read]** On top of that:

| Item | Bytes | Source |
|---|---|---|
| BR/EDR ACL connection | 1.2 KB each, default 2 connections | `components/bt/controller/esp32/Kconfig.in:34-41` |
| BTC task stack (callbacks to the app) | 3 072 | `components/bt/host/bluedroid/Kconfig.in:1-6` |
| BTU task stack (host stack) | 4 352 | same file, :29-34 |
| Controller memory that can be handed back when BT is unused | "about 70 KB" | `components/bt/include/esp32/include/esp_bt.h:489-513` |

**[read]** The A2DP source adds its own buffers. Its transmit queue is depth-limited to 27 encoded
packets, and every packet is a fresh `osi_malloc` of `BTC_MEDIA_AA_BUF_SIZE` = 4096+16 bytes
regardless of how few bytes the SBC frame uses:

```
components/bt/host/bluedroid/btc/profile/std/a2dp/btc_a2dp_source.c:85
#define BTC_MEDIA_AA_BUF_SIZE (4096+16)
:117-118  #define MAX_OUTPUT_A2DP_SRC_FRAME_QUEUE_SZ (27) // 18 for 20ms tick
:113-115  "The typical runlevel of the tx queue size is ~1 buffer"
:1383     p_buf = osi_malloc(BTC_MEDIA_AA_BUF_SIZE);
```

**[inferred]** Worst case **27 × 4 112 = 111 024 B** of heap, typical ~4 KB. Add the source's static
read/upsample buffers (2 × 512 B, `btc_a2dp_source.c:1228-1231`), the SBC encoder state inside the
source context (1 536 B, `external/sbc/encoder/include/sbc_encoder.h:183,188`), and AVRCP, which
`BT_A2DP_ENABLE` selects automatically (`Kconfig.in:92-98`).

### 3.2 Against this board

**[measured]** The line build's static DRAM is **176 175 B of 180 736 (97.5 %)**, `.bss` alone
157 816 B — the 96 KB ring is in there (`PLAYER_RING_BYTES` in
[`player/components/player/include/player_ring.h`](../../player/components/player/include/player_ring.h)
line 38). Only 4 561 B of slack remains in the segment.

**[inferred]** With `BT_ENABLED` the same static data has a 124 580 B ceiling, so the current
`.bss` **overflows it by 33 236 B** before any Bluetooth allocation — the 96 KB ring cannot stay
where it is. This is the arithmetic behind the repo's own measurement: the link failed with
`region dram0_0_seg overflowed by 10 920 bytes` at a larger ring, the ring was cut, and with the
radio up the boot census read **112 664 B free, 98 304 B largest**
(PL-08 lines 52–56) — against a shipping build whose playing census is **74 788 B free / 38 912 B
largest** (bench log; ADR 0102's is 85 KB/43 KB).

**[inferred]** Put the numbers side by side and the box is tight:

- the controller reserve alone (56 156 B) is **75 %** of the 74 788 B the player has free while
  playing;
- the A2DP transmit queue's worst case (111 024 B) is **1.5×** that free heap;
- the typical queue (~4 KB) fits, but "typical" is exactly what a shared radio does not promise: the
  queue exists to absorb link congestion (`btc_a2dp_source.c:99-116`), and congestion is the one
  thing this coexistence produces.

**No PSRAM** (ADR 0097) and no second memory: every byte above comes out of the same 520 KB SRAM the
Wi-Fi driver, the decoder and the ring already share.

**[read]** It is worth saying what the source does when the application cannot supply PCM in time:
the media task zero-fills the unfilled part of the read buffer (`btc_a2dp_source.c:1325-1326`) — it
sends **silence**, not a stall. On this board, a starved callback therefore becomes quiet audio at
the speaker rather than a slowed stream.

---

## 4. What it costs the Wi-Fi link: the measurement is what the docs predict

### 4.1 The documented mechanism

**[read]** One radio, time-division, priority arbiter: "Each type of board has only one 2.4 GHz ISM
band RF module... a module cannot receive or transmit data while another module is engaged in data
transmission or reception. In such scenarios, ESP32 employs the time-division multiplexing method"
([RF Coexistence, ESP-IDF v6.1](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-guides/coexist.html),
`docs/en/api-guides/coexist.rst:9`). Each module requests the RF from the coexistence module, which
"decides who will use the RF resource based on their priority" (:136–160), and the period is split
into fixed Wi-Fi/BT/BLE slices whose proportions depend on **the Wi-Fi status** (:180):

- **IDLE status: "the coexistence of BT and BLE is controlled by Bluetooth module"** (:189);
- CONNECTED: period ≥ 100 ms, starting at TBTT (:191); SCAN/CONNECTING: longer Wi-Fi slices (:192–193);
- priorities are dynamic and a high-priority BT event may preempt a Wi-Fi slice (:211–225).

**[read]** The same page rates ESP-NOW RX with Classic BT as **"S"** — "supported and the performance
is stable in STA mode, otherwise not supported" (:67, :119). The player is `WIFI_MODE_STA`
(`board_player_esp32.c:353`) but **not associated**: it sets the channel directly (:283).

**[inferred]** That is precisely the "IDLE" row of the policy: with no association, there is no
connected-STA scheme to divide the period, and the doc says control passes to Bluetooth. The
measurement is not a contradiction of Espressif's model; it is the model's IDLE case.

### 4.2 The measured ladder

**[measured]** Same stream, same day, from PL-08 (lines 83–90), repeated in
[ADR 0100](../adr/0100-the-player-ships-with-the-line-output-and-the-speaker-waits.md) lines 11–17:

| Firmware state | Stream | Cost |
|---|---|---|
| No Bluetooth in the firmware | 38–40 frames/s | — |
| Bluetooth compiled in, controller never started | ~24 frames/s, degrading in bursts | −40 % |
| Bluetooth up but idle | 3–5 frames/s | −87…−92 % |
| Bluetooth in inquiry | 0 frames/s | −100 % |
| Coexistence arbitration disabled (radio off) | 23 frames/s — no better | — |
| Heap over two minutes | flat (59 776 → 96 412 B) | not a leak |

**[inferred]** Reconcile row by row:

1. **"Compiled in, never started" is not the radio.** Nothing transmits, so this cannot be
   coexistence. It is the DRAM: the 56 156 B reserve shortens the segment at link time whatever the
   runtime does (`memory.ld.in:81`), plus 7 424 B of host task stacks if Bluedroid is initialised.
   The repo records the effect but does not trace it; the heap census in that state is the
   measurement that would settle it, and it is not in the ticket. This row is the one claim in the
   ladder that the note can explain only by arithmetic, not by the coexistence document.
2. **"Radio up but idle" is the IDLE-STA case** of §4.1. A controller that is enabled and
   discoverable/connectable is running page/inquiry scan windows; with Wi-Fi unassociated there is
   no connected-STA period to bound them, and the policy hands the RF to Bluetooth. A 10× loss of
   ESP-NOW frames is the shape of that.
   *Caveat on the record:* PL-08's prose says "with the radio idle it is 40 frames a second"
   (line 46) while its table says 3–5 (line 87). The board log in the same ticket (lines 63–72)
   shows 38 frames/s only after the scan window closed, i.e. with the radio **shut down**; the
   table is the consistent record and ADR 0100 repeats it.
3. **Inquiry is the documented worst case.** Inquiry is continuous receive at the inquiry hopping
   rate; there is no scheme in which Wi-Fi keeps a slice of that. 0 frames/s is what "one radio"
   means when the other side is scanning.
4. **"Arbitration disabled" did not test what its name says.** `ESP_COEX_SW_COEXIST_ENABLE`'s help
   reads: "If enabled, WiFi & Bluetooth coexistence is controlled by **software rather than
   hardware**" (`components/esp_coex/Kconfig:9-21`). Clearing it does not remove the arbiter; it
   swaps the software implementation for the hardware one. The RF is still one, so "no better" is
   the expected result — the measurement is right, the label is wrong. The comment "it is not the
   arbiter" should be read as "it is not the *software* arbiter".
5. **"Heap flat" rules out a leak, not a budget.** A flat heap at 59–96 KB with a 56 KB static
   reservation is a smaller heap being used steadily, which is consistent with row 1.

**[inferred]** One more documented lever the measurement did not use: Espressif says to "run the
task of the Wi-Fi protocol stack, the task of the Bluetooth Controller and Host protocol stack on
different CPUs" (`coexist.rst:262`). The player pins Wi-Fi to core 0
(`CONFIG_ESP32_WIFI_TASK_PINNED_TO_CORE_0=y`, `player/sdkconfig:3696`), and both Bluetooth
choices default to core 0 (`BTDM_CTRL_PINNED_TO_CORE_0` is the first, hence default, entry of its
choice, `controller/esp32/Kconfig.in:198-216`; same shape at `host/bluedroid/Kconfig.in:8-22`).
A speaker build that did not move BT to core 1 would be measured against a worse configuration
than the platform allows — it would not, however, change the airtime arithmetic.

**[inferred]** The airtime arithmetic, for scale: 128 kbps is 16 000 B/s. ESP-NOW broadcast at the
default 1 Mbps PHY carries 244 B of payload per frame (`ES_MAX_FRAGS` is `ceil(1441 / 244)` in
`components/portable/espnow_stream/include/espnow_stream.h:32`), so the stream needs on the order of 15 % of
one radio's time. At 3–5 frames/s the player is receiving ~10 % of its packets — roughly 1.5 % of
the airtime. The coexistence does not halve the link, it closes it.

---

## 5. Latency and buffering: ~0.2 s on top of a stream that already holds 4.5 s

**[read]** The source is tick-driven: `BTC_MEDIA_TIME_TICK_MS (30)` (`btc_a2dp_source.c:78`). Every
tick the media task computes how many SBC frames the elapsed wall time owes, pulls that much PCM
from the application callback, encodes, and queues; the queue is capped at 27 packets
(`:1148-1207`). So:

- granularity **30 ms** per pull;
- worst case queued in the source **27 × 30 ms ≈ 810 ms** (`MAX_OUTPUT_A2DP_SRC_FRAME_QUEUE_SZ`, the
  queue's own "5 frames ... ~100 ms @ 44.1 kHz, 20 ms mediatick" comment at `:99-118`);
- plus AVDTP/RTP/L2CAP, the air, and the **sink's** jitter buffer.

**[read]** The delay model is the profile's: A2DP §13.2 says the SRC–SNK delay comes from "radio
signal processing, data buffering, and encode/decode of the stream data" and that "countering the
effects of such delays depends on implementation"; A2DP 1.3 interoperates with AVDTP 1.3 Delay
Reporting (A2DP specification, §13.2). In IDF the sink side of that is explicit: the sink reports a
delay "caused by buffering (including protocol stack and application layer), decoding and rendering",
**default and minimum 120 ms**, in units of 0.1 ms (`esp_a2dp_api.h:574-589`), and the source
receives it as `ESP_A2D_REPORT_SNK_DELAY_VALUE_EVT` (:388-392).

**[inferred]** Typical end-to-end therefore lands around **150–300 ms** (a 30 ms tick, one to a few
queued packets, the sink's ≥120 ms report, plus its buffer), and a congested link can push the
source's own queue toward 0.8 s. On a player that primes 4.5 s before it plays
([k7player spec](../specs/k7player.md) R28, lines 147–152), the added latency is not the problem.
The problem is the other property of that buffer: A2DP has no retransmission. A packet the shared
radio takes is a gap at the speaker, and the same coexistence that costs the stream its frames costs
the SBC stream its packets.

---

## 6. The verdict, as a box

```
NOT ON THIS CHIP — for the K7's stream.

Supported:      yes. SOC_BT_CLASSIC_SUPPORTED (1) on the classic ESP32; esp_a2d_source_init /
                connect / register_data_callback in IDF 6.1; SBC encoded inside the stack.
Fits, barely:   only as a second firmware variant. 1.42 MB image on a 3 MB factory partition
                (4 MB flash); the BT_ENABLED DRAM reserve alone is 56 156 B of the 74 788 B the
                player has free while playing, and the 96 KB ring cannot stay in .bss
                (static DRAM ceiling falls to 124 580 B against 176 175 B in use).
Decided by:     the radio, not the memory. 38-40 frames/s without Bluetooth, ~24 with the stack
                linked, 3-5 with the controller up and idle, 0 in inquiry. The stream needs
                ~38 frames/s to be audio at all.
Unproven:       no speaker on the bench ever answered the inquiry (PL-08; ADR 0100), so not one
                note has been played over A2DP from this board.
```

**[inferred]** "A second firmware variant" is feasible in the narrow sense that the image can be
built and flashed: partition table nvs 24 K / phy 4 K / factory 3 MB, the ring moved out of `.bss`
or shrunk, BT pinned to core 1. It is **not** feasible as *this product with a speaker added*,
because the thing that feeds it — the ESP-NOW stream — is the thing the Bluetooth radio takes away.
A variant would be a speaker player fed by something else, and this board has no something else: no
PSRAM, no Ethernet, no second radio.

**[inferred]** The one path the measurements leave open is a build that never associates but also
never runs the controller except when the stream is stopped — which is not "A2DP as the output", it
is "the line, with a speaker that takes over when the stream is not playing".

---

## 7. Secondary: the sink direction (brief)

**[read]** An ESP32 can be an A2DP **sink** in the same Bluedroid: `esp_a2d_sink_init()`,
`esp_a2d_sink_connect()`, and `esp_a2d_sink_register_data_callback()` which delivers "PCM data
stream decoded from SBC format" (`esp_a2dp_api.h:506,557`; `esp_a2dp_legacy_api.h:44-57`). The
`a2dp_sink_stream` example is exactly this board's shape — "This example involves the use of
Bluetooth legacy profile A2DP for audio stream reception and I2S for audio stream output", written
against "a PCM5102 chip" (`examples/bluetooth/bluedroid/classic_bt/a2dp_sink_stream/README.md`).

**[measured]** Its cost is the same stack, so the same order of memory: **760 864 B** image, 27 %
free of a 1 MB partition, IRAM 90 443 B (69 %), static DRAM 54 471 B — built for this note from the
same IDF, `esp32`, default partition. **[inferred]** A sink build of the player would therefore hit
the same flash and DRAM walls as the source, and the same coexistence wall: while a phone streams
to it, the ESP-NOW receiver gets the radio's leftovers, and during pairing/inquiry the K7's stream
stops entirely.

**[read]** It cannot be both at once: "A2DP source cannot be used together with A2DP sink at the
same time" (`a2dp_source/README.md:89`).

**[inferred]** The sink also changes the product's role: the player stops being the K7's client and
becomes a Bluetooth speaker for a phone, which contradicts ADR 0105's one-transport design. It is a
different device, not an output option.

---

## 8. What would have to change (for a variant, if one is ever built)

1. **Partition table.** `partitions.csv`: nvs 24 K, phy 4 K, factory 3 MB, as PL-08 measured — the
   default 1 MB single-app cannot hold a 1.42 MB image (PL-08:52–56).
2. **BT Kconfig set.** `CONFIG_BT_ENABLED`, `BT_BLUEDROID_ENABLED`, `BT_CLASSIC_ENABLED`,
   `BT_A2DP_ENABLE` (which selects AVRCP), `BTDM_CTRL_MODE_BR_EDR_ONLY`, `BT_BLE_ENABLED=n`
   (the exact set is in `examples/bluetooth/bluedroid/classic_bt/a2dp_source/sdkconfig.defaults:3-10`).
3. **The ring leaves `.bss`.** 96 KB static + BT's 124 580 B ceiling is 33 236 B of overflow before
   anything else; the ring must shrink or move to the heap, and then the 74 788 B free-heap figure
   becomes the budget the A2DP queue (worst case 111 KB) has to live in too.
4. **Pin Bluetooth away from Wi-Fi.** `BTDM_CTRL_PINNED_TO_CORE_1` and
   `BT_BLUEDROID_PINNED_TO_CORE_1`, since the player's Wi-Fi task is on core 0
   (`player/sdkconfig:3696`) and the coexistence guide asks for different CPUs (`coexist.rst:262`).
5. **A speaker that answers inquiry.** The bench speaker reported a `0x4B…` address, a random static
   BLE address, and refused a classic connection (PL-08:113–114); nothing here has ever played a
   note over A2DP, so no variant can claim B2/B3 of the spec.
6. **PL-10's ten-minute measurement**, which was never run: both radios live, dropouts counted, the
   ring's occupancy at its worst, and the K7's encoder frame cost beside it
   ([ticket 10](../archive/design/k7player/issues/10-the-speaker-cost-measured.md) lines 18–20).

---

## 9. What this note does not establish

- **A2DP never ran on this board.** No speaker answered inquiry; the connection was never made. Every
  RAM figure above is static analysis plus the repo's own measurements of a build that did exist but
  was never committed (see §10).
- **The "compiled in, never started" row is unexplained by measurement.** The DRAM arithmetic fits
  it; a heap census in that state would prove it, and one is not recorded.
- **No `idf.py size` of the player's BT variant survives**, because the tree that built it was never
  committed. The 1.42 MB and the DRAM overflow are the ticket's record, not a build artefact in the
  repo.
- **SBC encode CPU was not measured.** The encoder runs on the Bluetooth media task, and the
  measurements point at the radio, not the CPU, as the constraint; that is an inference from the
  ladder, not a profile.
- **No PSRAM, no second radio, and no association.** All three are properties of this board and this
  firmware ([ADR 0097](../adr/0097-the-player-is-an-esp32-classic-in-its-own-project.md),
  [ADR 0105](../adr/0105-the-player-is-esp-now-only.md)); a different board would change the answer.

---

## 10. The parked code is not in the history

**[measured]** ADR 0101 says the A2DP source, pairing rule and sink switch are "one `git show` away
on the branch that built it" ([ADR 0101](../adr/0101-the-speaker-code-waits-in-the-history-not-the-tree.md)
line 25); PL-08 repeats it ("The code is in the history of the branch that built it", line 128).
The claim does not hold. What was checked, on 2026-11-19:

- `git log --all --oneline -S"a2dp" -i` returns two commits, and neither adds code: `a43f3b1` (the
  commit that added the player, its ADRs and its tickets — it brings the *word* into the docs) and
  `3f67a4e` (a K7 doc table line "Bluetooth / A2DP / BLE | absent"). `-S"esp_a2dp"` and
  `-S"a2dp_source"` return nothing at all; `-S"PLAYER_OUTPUT_SPEAKER"` returns `a43f3b1` for the
  doc string only; `-S"esp_bt"` returns nothing.
- Every blob in the object database was scanned — 5 611 blobs, reachable **and** unreachable. The
  strings `esp_a2dp`, `esp_a2d`, `esp_bt_controller`, `btc_a2dp`, `esp_bluedroid` and `a2dp_sink`
  occur in **no object at all**. "a2dp" occurs in 14 blobs, every one of them Markdown or a comment:
  `CONTEXT-MAP.md`, `player/CONTEXT.md`, `docs/specs/k7player.md` (five revisions), ADR 0097, ADR
  0100, ADR 0101, PL-08, PL-04, `docs/research/service-surface-snapshot.md`, and the
  comment in `player/sdkconfig.defaults`. `PLAYER_OUTPUT_SPEAKER` occurs in three: PL-08 and the two
  ADRs.
- `git log --all --diff-filter=D -- "*speaker*"` is empty, and no path containing `speaker` or
  `a2dp` has ever been added except those four documents.
- `git fsck --unreachable --no-reflogs` lists **273** dangling objects (32 commits, 121 trees, 120
  blobs). Every unreachable commit is a stash/WIP entry of the K7 app (panel, artwork, evals, prompt
  runs, dated 19 Oct–16 Nov), and **not one of those commits contains a `player/` path at all** —
  checked by listing each commit's tree. The 120 dangling blobs are inside the 5 611-blob scan
  above, so they are covered by the no-`esp_a2dp` result.
- `git log --all --oneline -- player/` has nine commits; the oldest, `a43f3b1`, already has no
  Bluetooth, and `git branch -a` has no branch where it appears.

**[inferred]** The Bluetooth path was developed in an uncommitted working tree and deleted before
the first commit. Its measurements survive in PL-08 and ADR 0100; its code does not survive at all.
Rebuilding the speaker variant starts from the ticket's description, not from a `git show` — which
makes ADR 0101's cost argument ("a day's work") rest on prose only.

---

## 6. What the bench answered (19 November)

Sections 1–5 are desk work. This is the board: the K7Player (ESP32-D0WD, WROOM-32) and the
"BT5.0-Audio" sink at `4b:68:00:36:09:6d`, heard between −38 and −52 dBm.

**The controller mode was the first wall.** BR/EDR-only — `BTDM_CTRL_MODE_BR_EDR_ONLY` with
`esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)`, with and without
`esp_bt_controller_mem_release(ESP_BT_MODE_BLE)` — is **deaf on IDF 6.1**: six inquiries across
two independent firmwares (the player's own, and the ESP32-A2DP library built for the same board)
returned **0 devices**, while the same board in dual mode heard the same speaker and a phone at
−52. Every earlier "coexistence" reading on this bench was this, not the arbiter.

**Dual mode pages, with the radio quiet.** BTDM (`BTDM_CTRL_MODE_BTDM`,
`esp_bt_controller_enable(ESP_BT_MODE_BTDM)`, BLE host enabled — with it disabled the inquiry is
deaf too) and Wi-Fi up but idle: the inquiry finds the speaker at −40 dBm and
`esp_a2d_source_connect` completes — `hcif conn complete: hdl 0x80, st 0x0`, A2DP connected, and
the stack pulls PCM (845 data callbacks of silence).

**The stream starves the page.** With ESP-NOW receiving 38 frames a second, four page attempts in
a row ended `hcif conn complete: hdl 0xfff, st 0x4` (page timeout). Stopping the Wi-Fi driver for
the page and restarting it on the same channel landed one (`st 0x0`, 2.2 s) in about ten
attempts; the rest timed out even with the radio handed over.

**The SBC frame buffer does not fit.** `btc_media_aa_prep_sbc_2_send` allocates 4 112 B per frame
(`btc_a2dp_source.c:85,1383`). With Wi-Fi running, `MALLOC_CAP_DEFAULT` free on this board was
**3 104 B, largest block 2 816 B**, and every frame logged `no buffer TxCnt 2`. Note what the
player's own log said at the same moment: "internal 27 352 free, largest 24 576" — `MALLOC_CAP_INTERNAL`
counts IRAM that the allocator cannot hand out. Paused for the page the same line has 27 284 B
free; resuming Wi-Fi takes it back to ~3 KB.

**The right sequencing is the library's.** ESP32-A2DP 1.8.12 does
`esp_bt_gap_cancel_discovery()` → wait for `ESP_BT_GAP_DISCOVERY_STOPPED` → one `esp_a2d_connect`.
A connect issued while the inquiry is still winding down is refused `0x0c: Command Disallowed`,
which is what a hand-rolled scanner does when it pages from the inquiry callback.

**So the question is closed, and not by the code:** one radio delivering a stream cannot also
page and hold a link, and the RAM the A2DP source needs while Wi-Fi is up is not there. A speaker
for this player is a second radio — a bridge fed from the line, or an off-the-shelf transmitter
plugged into it — not another build of this firmware. [ADR 0108](../adr/0108-the-speaker-needs-a-second-radio-not-a-second-build.md)
records the decision.

---

## Sources

**The repo (read-only for this note; the only file written is this one).**

- [`docs/adr/0100-the-player-ships-with-the-line-output-and-the-speaker-waits.md`](../adr/0100-the-player-ships-with-the-line-output-and-the-speaker-waits.md) — the measured ladder, lines 9–23.
- [`docs/adr/0101-the-speaker-code-waits-in-the-history-not-the-tree.md`](../adr/0101-the-speaker-code-waits-in-the-history-not-the-tree.md) — the history claim, line 25.
- [`docs/adr/0102-the-players-ring-is-96-kb-and-clears-itself-rather-than-dropping-frames.md`](../adr/0102-the-players-ring-is-96-kb-and-clears-itself-rather-than-dropping-frames.md) — the 96 KB ring and the 85 KB/43 KB census, lines 33–45, 83.
- [`docs/adr/0097-the-player-is-an-esp32-classic-in-its-own-project.md`](../adr/0097-the-player-is-an-esp32-classic-in-its-own-project.md), [`0105`](../adr/0105-the-player-is-esp-now-only.md) — the chip and the one transport.
- [`docs/design/k7player/issues/08-the-player-speaker-output.md`](../archive/design/k7player/issues/08-the-player-speaker-output.md) — the 1.42 MB image, the 3 MB factory partition, the `dram0_0_seg overflowed by 10 920 bytes`, 112 664 B/98 304 B, the ladder, the bench speaker, lines 44–56, 63–90, 113–128.
- [`docs/design/k7player/issues/10-the-speaker-cost-measured.md`](../archive/design/k7player/issues/10-the-speaker-cost-measured.md) — the measurement that was never run.
- [`docs/specs/k7player.md`](../specs/k7player.md) — R28 and the prime, lines 139–152; R13, lines 71–73.
- [`player/build/k7player.bin`](../../player/build/k7player.bin) and `player/build/k7player.map`, read with `esp_idf_size` — 891 712 B; flash code 652 454, data 130 880; IRAM 89 859/131 072; DRAM 176 175/180 736, `.bss` 157 816.
- `player/sdkconfig:3696` (`WIFI_TASK_PINNED_TO_CORE_0`), `player/components/board_player/board_player_esp32.c:283,353`, `player/components/player/include/player_ring.h:38`.
- The bench heap figures (74 788 B free / 38 912 B largest while playing) are the board log quoted for this note; the repo's own census is ADR 0102's 85 KB/43 KB.

**ESP-IDF 6.1 (checkout at `~/esp/esp-idf-6.1`, `version.txt` = `v6.1`).**

- `components/soc/esp32/include/soc/soc_caps.h:76,389,391`; `components/soc/esp32s3/include/soc/soc_caps.h:52,504`; `components/soc/esp32c3/include/soc/soc_caps.h:34,411`; `components/soc/esp32s31/include/soc/soc_caps.h:643` (the one other part with Classic BT).
- `components/bt/host/bluedroid/api/include/api/esp_a2dp_api.h` — source API :645, :662, :720, :735; sink API :506, :557; delay reporting :574-602; source delay event :292, :388-392.
- `components/bt/host/bluedroid/api/include/api/esp_a2dp_legacy_api.h:33-73` — the PCM pull callback.
- `components/bt/host/bluedroid/Kconfig.in:1-6` (BTC stack 3 072), `:29-34` (BTU stack 4 352), `:44-49` (Classic depends on the SoC cap), `:92-107` (A2DP selects AVRCP; external-codec note), `:8-22` (Bluedroid core).
- `components/bt/controller/esp32/Kconfig.in:1-15` (BR/EDR-only mode), `:34-41` (1.2 KB per ACL), `:198-216` (controller core, default 0), `:542-544` (`BTDM_RESERVE_DRAM` 0xdb5c).
- `components/bt/include/esp32/include/esp_bt.h:489-513` — `esp_bt_controller_mem_release`, "about 70 KB".
- `components/esp_system/ld/esp32/memory.ld.in:26-27,38,81-82` — `DRAM0_0_SEG_LEN` 0x2c200 and the reserve.
- `components/bt/host/bluedroid/btc/profile/std/a2dp/btc_a2dp_source.c:78` (30 ms tick), `:85` (4 112 B buffer), `:99-118` (queue 27, "typical ~1"), `:1148-1207` (frames per tick), `:1228-1231` (static buffers), `:1325-1326` (silence fill), `:1383` (`osi_malloc`).
- `components/bt/host/bluedroid/external/sbc/encoder/include/sbc_encoder.h:45,77,183,188` — SBC encoder state.
- `components/esp_coex/Kconfig:9-21` — software vs hardware coexistence.
- `docs/en/api-guides/coexist.rst:9,38,67,119,136-160,180-193,211-225,262` — time-division, the four Wi-Fi statuses, the ESP-NOW "S" rating, core pinning.
- `examples/bluetooth/bluedroid/classic_bt/a2dp_source/README.md:4-9,87-89`, `sdkconfig.defaults:3-10`; `examples/bluetooth/bluedroid/classic_bt/a2dp_sink_stream/README.md` (I2S + PCM5102).
- The two `idf.py size` runs made for this note: `a2dp_source` 728 864 B / 0x4E0E0 free / IRAM 86 299 / DRAM 54 715; `a2dp_sink_stream` 760 864 B / 0x463E0 free / IRAM 90 443 / DRAM 54 471.

**Espressif published documentation (URLs, page titles).**

- [RF Coexistence](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-guides/coexist.html) — "RF Coexistence - ESP32 - ESP-IDF Programming Guide v6.1".
- [Bluetooth A2DP API](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/bluetooth/esp_a2dp.html) — "Bluetooth® A2DP API - ESP32 - ESP-IDF Programming Guide v6.1".

**Bluetooth SIG.**

- [Advanced Audio Distribution Profile Specification](https://www.bluetooth.org/DocMan/handlers/DownloadDoc.ashx?doc_id=303201) — Table 4.1 (codec requirements: SBC mandatory §4.3, MPEG-1,2 §4.4, AAC §4.5, ATRAC §4.6); §4.3.3 (media packet header, timestamp); §13.2 (Audio Streaming: SRC–SNK delay from radio, buffering and codec, implementation-defined; Delay Reporting from AVDTP 1.3). The SIG serves it as a PDF, so the section text here is quoted from the document's published copy rather than read through a text extractor.
