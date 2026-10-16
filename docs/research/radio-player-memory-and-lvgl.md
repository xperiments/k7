# Internal RAM on the ESP32-S3 stream server, and what an LVGL UI costs — verified from primary sources (2026-10-16)

Axis: **where this firmware's internal RAM actually goes, which levers can free some of it, and what adding an LVGL UI does to a board that already spends ~100 KB of internal RAM on an MP3 encoder.**

Everything below is read out of artefacts on this machine: the ESP-IDF trees at
`/Users/pedrocasaubon/esp/esp-idf-6.1` (v6.1.0, `fff9895c82d7`) and
`/Users/pedrocasaubon/esp/esp-idf` (v5.4.4, `296b6eab9445`), the two linker maps and ELFs in this
repository, LVGL v9.6.0 / v8.4.0 from GitHub, `esp_lvgl_port` 2.9.0 from `espressif/esp-bsp`, and the
IDF documentation shipped inside those trees. Where a number can only come from the board, it is
labelled as such and was measured by the board tier, not by me.

Three kinds of number appear in this note and they are never mixed:

- **MEASURED (board)** — printed by the firmware's own telemetry on the ESP32-S3 devkit (16 MB flash,
  8 MB octal PSRAM), supplied with timestamps and log lines.
- **MEASURED (link)** — read out of `build61/sd_stream_server.map`, `build61/sd_stream_server.elf` and
  the IDF size tool by me on this machine. Both maps were rebuilt *while this note was being written*,
  so the identity of what was measured is pinned here: `build61/sd_stream_server.map`, mtime
  2026-10-16 21:31:51, md5 `c2fd5afe15d90ade08b5e3490eda31f2` (IDF 6.1); `build/sd_stream_server.map`,
  mtime 19:01:09, md5 `ec0a0a924be7a125f8eb0ddda19b16c5` (IDF 5.4.4).
- **DOCUMENTED** — a Kconfig `help` string, a `.rst` page, or a header comment, always cited by file
  and line.

And one warning that applies to every cross-version number in this note: the tree in `build/` was
compiled at 19:01 from a source revision older than the current one (`main/main.c` changed at 21:31,
`components/mini_icecast/mini_icecast.c` at 21:27). **Its DIRAM figure is therefore not comparable to
the 6.1 build** — see §5.

---

## 1. What "DIRAM" is on this SoC, and why IRAM code comes out of the data pool

The IDF 6.1 size tool prints a row called **DIRAM** for the ESP32-S3. That name is not an IDF
invention; it is in the SoC linker script:

```
 * 40370000 <- IRAM/Icache -> 40378000 <- D/IRAM (I) -> 403E0000
 *                            3FC88000 <- D/IRAM (D) -> 3FCF0000 <- DRAM/DCache -> 3FD00000
```

(`components/esp_system/ld/esp32s3/memory.ld.in:17-19`.) The 512 KB of on-chip SRAM on an ESP32-S3 is
aliased into two windows that address the *same* cells — an instruction window from `0x40370000` and a
data window from `0x3FC88000` — and the address translation between them is a constant,
`SOC_I_D_OFFSET` (`components/soc/esp32s3/include/soc/soc.h:182-187`). Concretely, from the memory
configuration block of this build's own map:

| Region (this build) | Origin | Length | What lands there |
|---|---|---|---|
| `iram0_0_seg` | `0x40374000` | `0x57700` = 358 144 B | IRAM code: `.iram0.vectors`, `.iram0.text` |
| `dram0_0_seg` | `0x3fc88000` | `0x53700` = **341 760 B** | `.dram0.data`, `.dram0.bss`, and the data-side alias of the IRAM code |
| separate IRAM block | `0x40374000` | `0x4000` = 16 384 B | the 16 KB that is *not* shared with DRAM |

The last row is the "separate 16 KB IRAM region at 99.99 % used" in `idf.py size`. It exists because
`CONFIG_ESP32S3_INSTRUCTION_CACHE_16KB=y` (sdkconfig:1963) takes only half of the 32 KB
`IRAM/Icache` block, leaving 16 KB of true IRAM: `memory_layout.c:87-88` registers it as its own heap
region, and the size tool reports it separately. In the current 6.1 build it is completely full
(`IRAM 16384 / 16384, 100 %`, `.text` 15 356 B + `.vectors` 1 028 B).

Two consequences that decide most of this note:

1. **IRAM code and internal data compete for one pool.** The size tool counts `.text` placed in IRAM
   against the 341 760-byte DIRAM budget, because the linker emits the code into the I-side of the
   shared cells and a same-size `.dram0.dummy` placeholder into the D-side. In the current 6.1 build
   that placeholder is 97 024 B — i.e. **28 % of the data pool is consumed by instructions**, 96 787 B
   of it by code that spilled out of the full 16 KB block.
2. **The budget is the segment lengths, not "512 KB of SRAM".** The app's data window stops at
   `0x3FCDB700` (`dram0_0_seg` length 0x53700 = 341 760 B), the instruction window at `0x403CB700`,
   and the linker script states the rest of the story: "Startup code uses the IRAM from 0x403B9000 to
   0x403E0000, which is not available for static memory … D cache use the memory from high address …
   the region 0x3FCF000 ~ (3FD00000 - DATA_CACHE_SIZE) … is not used as static memory, leaving to the
   heap" (`components/esp_system/ld/esp32s3/memory.ld.in:20-27`). Everything above those windows is
   cache, ROM stacks and RVAs, not allocatable internal RAM.

What each heap region can serve is declared in the same file, and it is the authoritative answer to
"what must be internal":

```c
[SOC_MEMORY_TYPE_DIRAM]  = { "RAM",    { COMMON|INTERNAL|DMA|RETENTION|EXEC|SIMD, 0, 0 }},
[SOC_MEMORY_TYPE_DRAM]   = { "DRAM",   { 0, COMMON|INTERNAL|DMA|SIMD, 0 }},
[SOC_MEMORY_TYPE_IRAM]   = { "IRAM",   { EXEC, 32BIT|INTERNAL, 0 }},
[SOC_MEMORY_TYPE_SPIRAM] = { "SPIRAM", { SPIRAM, 0, COMMON|SIMD }},
```
(`memory_layout.c:56-70`, with `COMMON = MALLOC_CAP_DEFAULT|32BIT|8BIT`.)

So on this chip: **DMA and `MALLOC_CAP_INTERNAL` are the same pool** (DIRAM/DRAM), **EXEC is DIRAM or
the 16 KB IRAM block**, and **the PSRAM region carries neither DMA nor EXEC**. IDF states the
requirement in words as well: "Use the `MALLOC_CAP_DMA` flag to allocate memory which is suitable for
use with hardware DMA engines (for example SPI and I2S). This capability flag excludes any external
PSRAM" (`docs/en/api-reference/system/mem_alloc.rst:117`), and "DMA transaction descriptors cannot be
placed in PSRAM" on the S3 (`docs/en/api-guides/external-ram.rst:225`).

The heap's behaviour needs one qualification before budgeting a display: the *heap* will
happily hand back PSRAM for a `MALLOC_CAP_DMA|MALLOC_CAP_SPIRAM` request, because
`components/esp_hw_support/heap_align_hw.c:105-110` strips the DMA bit off any request that also asks
for PSRAM — "the heap allocator doesn't have regions marked `MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM` so we
need to request those without the DMA flag". The memory is *not* DMA-capable in the sense the header
means; whether a peripheral can reach it is the peripheral's problem (§8). And the IDF documentation
contradicts itself here: `mem_alloc.rst:121` tells you to use `MALLOC_CAP_SPIRAM|MALLOC_CAP_DMA` to
get "a DMA-capable external memory buffer". The code comment above is the truth.

## 2. Static internal RAM today, by library

From the current 6.1 map (`idf_size.py --format csv --archives`), DIRAM per archive, largest first:

| Archive | DIRAM total | of which `.bss` | `.data` | IRAM `.text` (D-side alias) | Flash code |
|---|---|---|---|---|---|
| `libshine.a` (the encoder) | 22 353 | 0 | 8 562 | 13 791 | 0 |
| `libpp.a` (Wi-Fi blob) | 19 298 | 1 274 | 2 640 | 15 384 | 42 630 |
| `libspi_flash.a` | 17 692 | 64 | 7 247 | 10 381 | 2 298 |
| `libnet80211.a` | 12 786 | 7 973 | 946 | 3 867 | 122 983 |
| `libmini_icecast.a` (this project) | 12 664 | 12 584 | 80 | 0 | 74 784 |
| `libesp_hw_support.a` | 9 429 | 227 | 790 | 8 412 | 21 311 |
| `libfreertos.a` | 9 425 | 764 | 3 108 | 5 553 | 11 977 |
| `libphy.a` | 8 498 | 86 | 3 290 | 5 122 | 27 904 |
| `libheap.a` | 5 498 | 8 | 13 | 5 477 | 3 792 |
| `libesp_hal_mspi.a` | 5 266 | 0 | 36 | 5 230 | 624 |
| `liblwip.a` | 2 842 | 2 830 | 12 | 0 | 89 865 |
| `libwpa_supplicant.a` | 1 715 | 1 707 | 8 | 0 | 78 172 |

Totals for the build: **DIRAM 158 974 B used of 341 760 (46.5 %), 182 786 B free** (`.text` 96 787,
`.data` 31 211, `.bss` 30 976); Flash Code 739 954 B; Flash Data 158 088 B; the 16 KB IRAM block 100 %
used; RTC slow 36/8 192 and RTC fast 24/8 192.

Three things in that table matter for the rest of the note:

- The firmware's own static buffers are the fifth-largest consumer: `libmini_icecast.a` holds
  **12 584 B of internal `.bss`**, and it is almost exactly three 4 KB staging buffers:
  `S.chunk[CHUNK_FRAMES*2]` = 4 096 B, `tmp[CHUNK_BYTES]` = 4 096 B and `in[MI_READ_FRAMES*2]` =
  4 096 B, plus a 4×`client_t` table and the title buffer
  (`components/mini_icecast/mini_icecast.c:55-72,148,347`). These are *not* DMA buffers and not touched
  by an ISR — each is walked once per 26 ms frame.
- The encoder archive is 22 353 B of DIRAM plus 560 B in the 16 KB IRAM block. **The frequently quoted
  "~35 KB of encoder code in IRAM" is not what this map shows**: `libshine.a` is 22 913 B of internal
  RAM in total (`idf_size.py --archives`), of which code is 14 351 B and the rest is literals and
  rodata pulled in by the same `* (noflash)` fragment (`components/shine/linker.lf`). The archive's own
  `.text` totals 24 459 B, but `--gc-sections` discards unlinked functions; roughly 9.5 KB of archive
  text never reaches the image. Either the README's figure counts the pre-GC archive, or it is stale —
  **it should be re-measured rather than quoted.**
- `libpp.a` + `libnet80211.a` + `libwpa_supplicant.a` + `liblwip.a` + `libphy.a` are **45.1 KB of
  internal RAM** between them, mostly `.bss` and IRAM-mandated Wi-Fi code. This is what
  `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` and `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` exist for
  (§4).

## 3. The two heap policies, read from source rather than from their names

`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` (its help text: "Maximum malloc() size, in bytes, to always put
in internal memory … allocation strategy will prefer to allocate chunks less than this size in internal
memory, while allocations larger than this will be done from external RAM",
`components/esp_psram/Kconfig.spiram.common:97-107`; range 0–131072) is implemented in exactly one
place:

```c
void *heap_caps_malloc_default( size_t size ) {
    if (size <= (size_t)malloc_alwaysinternal_limit)
        r = heap_caps_malloc_base( size, MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL );
    else
        r = heap_caps_malloc_base( size, MALLOC_CAP_DEFAULT | MALLOC_CAP_SPIRAM );
    if (r==NULL && size > 0)      // "try again while being less picky"
        r = heap_caps_malloc_base( size, MALLOC_CAP_DEFAULT );
```
(`components/heap/heap_caps.c:107-134`; the limit is set from `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` at
boot, `components/esp_psram/system_layer/esp_psram.c:142`.)

Four facts follow, and they are the ones a budget has to be built on:

1. **The policy applies only to the default allocator.** `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`
   never consults it — it goes straight to `heap_caps_malloc_base` (`heap_caps.c:82-92`). An explicit
   capability request is therefore a *bypass* of the 131072 threshold, which is what makes the
   recommendations in §9 possible.
2. **`malloc`, `calloc`, `realloc` and the newlib `_r` variants all go through it.** In IDF 6.1 they
   are three-line wrappers in `components/esp_libc/src/heap.c:22-73` (`calloc` included, via
   `heap_caps_malloc_default` plus `bzero`). In 5.4.4 the same file is
   `components/newlib/heap.c:27-70`. Note that `components/heap/include/esp_heap_caps.h:181` claims
   "`calloc(p)` is equivalent to `heap_caps_calloc(p, MALLOC_CAP_8BIT)`" — **that is not what the code
   does**, and it is exactly the sentence that would mislead someone into thinking `calloc` bypasses
   the threshold (the encoder's state is `calloc`'d and does land internal in the shipped config).
3. **Exhausting internal RAM does not fail an allocation; it silently moves it to PSRAM.** Step 3
   above retries with plain `MALLOC_CAP_DEFAULT`, and the PSRAM region is registered with
   `MALLOC_CAP_DEFAULT` (`esp_psram.c:528`: `{MALLOC_CAP_SPIRAM|MALLOC_CAP_DEFAULT, 0, 8BIT|32BIT|SIMD}`).
   So for any `malloc()` smaller than the threshold, running internal out means *slower placement*, not
   a NULL. The allocations that genuinely hard-fail are the ones that ask for `MALLOC_CAP_INTERNAL` or
   `MALLOC_CAP_DMA` explicitly — because those have no PSRAM heap to fall back to.
4. **The 32 KB DMA reserve is not as reserved as it looks.** `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL`
   (=32768 here) carves a pool out of the internal heap at the end of `main_task` and re-registers it
   with caps `{0, MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL, MALLOC_CAP_DEFAULT|8BIT|32BIT}`
   (`esp_psram.c:647-673`, called from `components/freertos/app_startup.c:165-172`). Those three
   entries are *priorities*: explicit DMA requests are served from it first, and ordinary `malloc()`
   only reaches it "after other internal heaps with higher-priority `MALLOC_CAP_DEFAULT` are
   exhausted" (`docs/en/api-guides/external-ram.rst:117-122`). **IDF 6.1 changed the promise here** —
   5.4.4's help text said "the memory in this pool is not given out when a normal malloc() is called",
   while 6.1's says "a normal malloc() can fall back to the pool only after all other internal are
   exhausted" (`Kconfig.spiram.common:117-135` in 6.1 vs the same config in the 5.4.4 tree). So on 6.1 a
   large enough burst of `malloc()` traffic — a UI being built, for instance — can eat the pool the
   Wi-Fi DMA buffers were supposed to have.

### Is "11 KB free plus a 32 KB reserve" the honest number? No — and here is what it should be

The board census 20 s into playback in the shipped configuration (see §6) reports **internal free
35 327 B with a largest contiguous block of 18 432 B**, counting the reserve pool, because
`heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` matches the reserve region's OR-ed caps (`heap_private.h:59-65`
ORs the priority entries; the reserve pool's second entry is `MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL`). So:

- The honest "free internal for small default allocations before the reserve is touched" figure is
  `free_internal − reserved_pool`, i.e. ≈35 327 − 32 768 ≈ **2.6 KB** in the shipped config — unless
  malloc traffic has already spilled into the pool, in which case there is none.
- The honest "largest single internal allocation" figure is **18 432 B**, from
  `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)` — not the free total. That number, not the
  free total, is what decides whether a driver's buffer or an LVGL draw buffer can be internal.

Both of those are board measurements and should be sampled with the reserve pool in mind; the 11 KB
working minimum in the original brief is consistent with this picture (35 KB free at 20 s, dipping to
~19 664 B min-ever-free in that census, and lower during an hour of streaming).

## 4. Levers that free internal RAM, with their real prices

Each lever below states its source, its size, and what it costs. Sizes marked *(link-measured)* are
read from the current 6.1 map.

**L1 — Turn off the Wi-Fi IRAM optimisations: documented at >27 KB, 9.9 KB visible in this map.**
`CONFIG_ESP_WIFI_IRAM_OPT=y` "place frequently called Wi-Fi library functions in IRAM. When this
option is disabled, more than 10Kbytes of IRAM memory will be saved but Wi-Fi throughput will be
reduced" and `CONFIG_ESP_WIFI_RX_IRAM_OPT=y` "more than 17Kbytes of IRAM memory will be saved"
(`components/esp_wifi/Kconfig:273-300`). **I could not confirm those figures from the map**: the
sections the Wi-Fi linker fragments own (`.wifi0iram`, `.wifirxiram`, `.wifislpiram`,
`.wifiorslpiram`, `.wifislprxiram`, `.wifiextrairam`; `components/esp_wifi/linker.lf`) total **9 933 B**
in internal RAM in this build (`.wifislprxiram` 6 654 + `.wifirxiram` 3 279, *link-measured*), and the
rest of the optimised code is not separable by section name. So the honest size of this lever is "the
documentation says >27 KB; the map shows at least 9.9 KB" — settle it by toggling the two options and
diffing `idf.py size` (§13). It is still the largest *reversible* IRAM lever here, and IRAM code is
charged against the same DIRAM pool as data (§1). Cost: Wi-Fi throughput, which for a 256 kbps stream
and a handful of clients is not obviously needed. Note `CONFIG_ESP_WIFI_EXTRA_IRAM_OPT` is already off,
and `CONFIG_ESP_PHY_IRAM_OPT=y` is a no-op on this chip: "This config only affect esp32c2 now"
(`components/esp_phy/Kconfig:199-205`).

**L2 — Wi-Fi buffer counts: ~1.6 KB per buffer, but already PSRAM-preferred.**
`ESP_WIFI_STATIC_RX_BUFFER_NUM=16` ("Each buffer takes approximately 1.6KB of RAM",
`esp_wifi/Kconfig:25-40`), `ESP_WIFI_STATIC_TX_BUFFER_NUM=16` with static TX buffers forced by the
PSRAM policy (`esp_wifi/Kconfig:60-95`), `ESP_WIFI_CACHE_TX_BUFFER_NUM=32`,
`ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=32`, `ESP_WIFI_MGMT_SBUF_NUM=32`. With
`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`, every one of these goes through `wifi_malloc()` →
`heap_caps_malloc_prefer(size, 2, MALLOC_CAP_DEFAULT|MALLOC_CAP_SPIRAM, MALLOC_CAP_DEFAULT|MALLOC_CAP_INTERNAL)`
(`components/esp_wifi/esp32s3/esp_adapter.c:68-105`), so their *internal* cost is only paid on failure.
The Kconfig range for static RX is 2–25, so 16 is already near the ceiling; lowering it to 10 would
save internal RAM only in the fallback case, and it is exactly the wrong knob to touch given the
bring-up fault in §6. **Leave these alone.**

**L3 — `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`: ~14 KB, Kconfig-only.**
"BSS section of `lwip`, `net80211`, `pp`, `bt` libraries will be automatically placed in PSRAM"
(`Kconfig.spiram.common:137-148`). From the map, the `.bss` of exactly those libraries is: `lwip.a`
2 830 + `net80211.a` 7 973 + `pp.a` 1 274 + `wpa_supplicant.a` 1 707 = **13 784 B** *(link-measured)*,
plus whatever `esp_wifi` and `phy` keep. Cost: this is `.bss`, so the data still moves to PSRAM
where access is cached but not guaranteed during flash-cache-off windows; it is only safe for data
never touched with the cache disabled. The doc warns that on ESP32/S2 it removes the ability to ignore
a missing PSRAM (`external-ram.rst:238-241` shows the option is gated), so the "PSRAM is optional"
property this firmware keeps in `main.c` would be lost if it mattered.

**L4 — Move this project's own 12.5 KB of static buffers to PSRAM.**
`S.chunk`, `tmp[]` and `in[]` (`mini_icecast.c:55-72,148,347`) are read/written once per encoded frame
and are neither DMA buffers nor ISR data. `EXT_RAM_BSS_ATTR` on them (with L3 enabled) or making them
heap allocations with `MALLOC_CAP_SPIRAM` frees **12 584 B** *(link-measured)* and costs at most one
cache miss per 4 KB per 26 ms. This is the cheapest 12 KB in the system.

**L5 — Task stacks: ~52 KB internal in the shipped configuration, ~29 KB of it movable.**
The stacks this firmware owns are internal: broadcast **24 576**, telemetry 4 096, plus card retry
6 144 (created only when no card is mounted); the four HTTP connection workers (4×10 240) and the
accept task are already in PSRAM. IDF's own internal stacks are configured in `sdkconfig` too: main
6 144 (`CONFIG_ESP_MAIN_TASK_STACK_SIZE`), system event 2 304, IPC 2×1 280, esp_timer 3 584, two idle
tasks 2×1 536, FreeRTOS timer 2 048, lwIP tcpip 3 072 — 23 584 B that cannot be moved without
replacing IDF's task creation (`components/freertos/app_startup.c:80` uses plain
`xTaskCreatePinnedToCore` for the main task) — and the Wi-Fi task's stack lives inside the blob, not in
`sdkconfig` at all. `xTaskCreatePinnedToCoreWithCaps(..., uxMemoryCaps)` is the tool for the rest:
"it allows the memory allocated for the task's stack to have specific capabilities (e.g.,
`MALLOC_CAP_SPIRAM`)", but "the specified capabilities will NOT apply to the task's TCB as a TCB must
always be in internal RAM" (`components/freertos/esp_additions/include/freertos/idf_additions.h:256-291`).
The caveats are real: the board measurement that started the worker pool in the first place was
`no memory for a connection task`; a PSRAM stack is unreachable while the cache is off
(`external-ram.rst:219`); and a caps-allocated task "must only be deleted using `vTaskDeleteWithCaps()`"
(`idf_additions.h:305-306`), which on 6.1 spawns a temporary clean-up task when a task deletes itself
and `abort()`s if that allocation fails (`components/freertos/esp_additions/idf_additions.c:160-205`).
The board census shows the broadcast task's 24 KB stack has 13 596 B unused (≈10.7 KB used), so
**moving it as-is frees 24 KB of internal and wastes 13.6 KB of PSRAM**; the honest first move is to
shrink it to ~12 KB, or move 12 KB, rather than relocating 24 KB.

**L6 — IRAM-mandated code in flash-heavy subsystems (10.4 KB of IRAM code in `spi_flash`, 8.4 KB in
`esp_hw_support`, 5.6 KB in `freertos`, 5.5 KB in `heap` — the DIRAM `.text` column of §2).** This code is in IRAM because it must run
with the cache disabled or from an ISR. `CONFIG_SPIRAM_XIP_FROM_PSRAM`
(`=CONFIG_SPIRAM_FETCH_INSTRUCTIONS` + `CONFIG_SPIRAM_RODATA`) makes flash code and rodata reachable
while SPI1 is busy, and the doc's stated benefit is precisely "the cache will not be disabled during
an SPI1 flash operation … For ISRs, ISR callbacks and data which might be accessed during this period,
you do not need to place them in internal RAM, thus internal RAM usage can be optimized"
(`external-ram.rst:200-215`). It does **not** by itself free a byte: it removes the *reason* to place
code in IRAM, so the payoff only materialises if IDF's own `*_IRAM_OPT` options are then turned off. It
costs PSRAM bandwidth on the MSPI bus (shared with flash on this SoC) and needs VMA/XIP verification.
Treat it as an enabler, not a lever — and it is unmeasured here.

**L7 — Lower `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`. 119 KB, at a measured CPU price.**
This is the only lever that changes *heap* placement rather than static placement, and it is the one
measured on the board both ways (§6). Details and trade-off in §9.

**L8 — RTC memory as heap: 8 KB, marginal.** `CONFIG_ESP_SYSTEM_ALLOW_RTC_FAST_MEM_AS_HEAP` registers
the 8 KB RTC-fast region (`memory_layout.c:105-108`); the size tool shows 24 B used and 8 168 B free,
so the gain is under 8 KB and it comes with retention/access restrictions. Not worth it for LVGL.

**L9 — Dropping linked-but-unused subsystems saves flash, not internal RAM.** Bluetooth is off
(`sdkconfig:1057`), nothing from `bt` is in the map, and `esp_http_client`, `esp_https_ota` and
`esp_tls` appear in the map only as `LOAD` lines; the per-archive table shows **zero sections** from
them. A `LOAD` line means the archive was handed to the linker, not that any object was pulled in — the
per-archive contribution table is the thing to check. `libmbedtls.a` contributes 73 bytes. The one
large movable flash item is **`libtfpsacrypto.a` at 105 846 B of flash** (81 560 code + 23 634 rodata),
pulled in because `CONFIG_ESP_WIFI_ENABLE_WPA3_SAE=y` (`esp_wifi/Kconfig:300-306` `select
ESP_WIFI_MBEDTLS_CRYPTO`); its internal-RAM cost is only 636 B. Turning WPA3 off is a **flash** lever
(§10), not an internal-RAM lever.

## 5. IDF 6.1 versus 5.4.4: what is a version effect and what is not

The 5.4.4 map reports DIRAM 184 691 B used (54.0 %), Flash Code 702 942 B, `.bss` 50 152 B, IRAM block
16 383/16 384; the 6.1 map reports DIRAM 158 974 B (46.5 %), Flash Code 739 954 B, `.bss` 30 976 B.
Both are built from a byte-identical `config/sdkconfig.h` (they differ only in the version comment), so
the *configuration* is held constant. **The source is not.** `build/` predates edits to `main/main.c`
and `mini_icecast.c`; the per-archive diff shows the largest "savings" in the project's own archives
(`libmini_icecast.a` −16 136 B DIRAM, `libmain.a` −4 616 B), which cannot be an IDF property. So:

- **Do not quote "IDF 6.1 uses 25 KB less DIRAM" on the strength of these two trees.** Rebuild the
  current source with 5.4.4 and re-measure before attributing anything. That is a five-minute build and
  it is the first item in §12.
- Two things *are* verifiable version differences from source, independent of that:
  - **PSRAM DMA for SPI peripherals is new in 6.1.** `SPI_TRANS_DMA_USE_PSRAM`
    (`components/esp_driver_spi/include/driver/spi_master.h:120`) and
    `esp_lcd_panel_io_spi_config_t::psram_dma_direct` (`components/esp_lcd/include/esp_lcd_io_spi.h:42`,
    used in `esp_lcd/spi/esp_lcd_panel_io_spi.c:359-378`) do not exist anywhere in the 5.4.4 tree. The
    documented behaviour: "GPSPI Master with DMA transferring data from/to PSRAM directly without extra
    internal copy process, which saves memory … Note that this feature shares bandwidth … with MSPI bus,
    so GPSPI transfer bandwidth should be less than PSRAM bandwidth, otherwise transmission data may be
    lost" (`docs/en/api-reference/peripherals/spi_master.rst:356-363`).
  - **The DMA reserve pool's promise changed** (§3, item 4), in a direction that matters more for a UI
    that allocates in bursts than for the current firmware.
  - **`CONFIG_SPIRAM_USE_CAPS_ALLOC` was re-scoped and re-documented.** In 6.1 its label is "Add RAM to
    heap_caps allocator (malloc() stays internal by default)" and its help says so explicitly: "Standard
    `malloc()` does not allocate from SPI RAM by default in this mode"
    (`components/esp_psram/Kconfig.spiram.common:73-83`), whereas 5.4.4's label was "Make RAM allocatable
    using `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`" with no statement about `malloc()`. This firmware
    uses `SPIRAM_USE_MALLOC`, so it does not apply here — but it is the IDF 6.x option that gives a
    project a *deliberately* internal-only default allocator, which for an LVGL port is the opposite of
    what is wanted.

## 6. What the board says (all of this is measurement, not documentation)

**A bring-up ordering fault that measures the Wi-Fi margin.** The firmware used to start the broadcast
(which allocates the encoder's ~100 KB internal) before `esp_wifi_init()`. It worked until a little
static RAM was added, and then:

```
I (1461) wifi:Init static rx buffer size: 1600
W (1465) wifi:malloc buffer fail
E (1475) wifi:Expected to init 16 rx buffer, actual is 14
E (1505) wifi_init: Failed to deinit Wi-Fi (0x3001)
abort() was called at PC 0x40381877 on core 0     → 20 resets in a 32 s capture
```

Changing only the order — validate the station format (arithmetic, allocates nothing), then
`net_start()`, then start the broadcast — gives **0 resets and 0 `malloc buffer fail`**. That is a hard
number: **Wi-Fi needs roughly 25 KB of *contiguous* internal RAM** (16 × ~1 600 B static RX plus
management buffers) before the encoder takes its share. It is the same constraint `MALLOC_CAP_DMA`
expresses in §1, and it is the constraint that makes "largest internal block" the metric that matters.

**Runtime census 20 s into playback, shipped configuration:**

```
internal: free 35327 B, largest block 18432 B, allocated 294980 B, min ever free 19664 B
external: free 8187420 B, largest block 8126464 B, allocated 198456 B, min ever free 8102160 B
largest DMA-capable block: 18432 B
tasks internal: main, IDLE0, IDLE1, tcpip, ice_telem, ice_broadcast, Tmr Svc, ipc0, ipc1,
                sys_evt, esp_timer, wifi
tasks PSRAM:    http_accept, 4 × http_conn (explicitly allocated)
ice_broadcast stack: 24576 B reserved, 13596 B unused (≈10.7 KB actually used)
```

One caveat on that census, because a budget built on a wrong total is worse than no budget: the free
and largest-block figures are self-consistent, but `allocated 294980` cannot be reconciled with the map.
The DIRAM pool is 341 760 B, of which 158 974 B is static, leaving 182 786 B; IDF adds two more internal
heap regions at run time — the 16 KB IRAM-only block (16 384 B) and the ROM-reserved D-side area between
`APP_USABLE_DRAM_END` (0x3FCE9710) and `SOC_DIRAM_DRAM_HIGH` (0x3FCF0000), 26 864 B, "recycled by heap
allocator in app_main task" (`memory_layout.c:84,96-97`, `soc.h:209-210`) — for a total of roughly
226 KB, not 330 KB. So `allocated` is either measured over a different set of pools or means something
other than live bytes; the exact call should be recorded. The recommendations below use the free and
largest-block figures, which are the ones that decide allocations.

**The encoder-state trade, by rebuilding with `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` lowered from
131072 to 4096:**

| | state internal (shipped) | state in PSRAM | delta |
|---|---|---|---|
| internal free | 35 327 B | **154 643 B** | +119 316 B |
| largest internal block | 18 432 B | **77 824 B** | ×4.2 |
| internal allocated | 294 980 B | 175 676 B | −119 304 B |
| external free | 8 187 420 B | 8 070 160 B | −117 260 B |
| encoder cost | 10 804 µs/frame = **41.4 %** of one core | 11 624 µs/frame = **44.5 %** | **+7.6 %** |

Task stacks did not move with the threshold (the broadcast stack stayed internal), so the CPU delta is
attributable to the encoder's state, not to a PSRAM stack.

## 7. LVGL: which version, and what its memory actually is

**Target v9.6.0.** LVGL's own release policy table gives every minor release one year of support and
lists **v8.4 as released 19 Mar 2024, support ended 20 Mar 2025, Active: No**
(`docs/src/introduction/policies.mdx`, "Version support"). v8.4 is not an LTS line in any sense the
project still honours; v9.4 (16 Oct 2025) and v9.5 (18 Feb 2026) are the versions marked active, and
v9.6.0 is the current release (GitHub release, 2026-09-16; `include/lvgl/lv_version.h`:
`LVGL_VERSION_MAJOR 9`, `MINOR 6`, `PATCH 0`). `espressif/esp_lvgl_port` 2.9.0 declares
`lvgl/lvgl: ">=8,<10"` (`idf_component.yml`), so v9 is squarely supported. The registry carries
`lvgl/lvgl` 9.6.0 and also a `9.6.0~1` revision whose contents I could not establish (see §13).

**What changed in memory management between v8 and v9.** In v8.4 the choice is
`LV_MEM_CUSTOM` with `LV_MEM_CUSTOM_INCLUDE`, `LV_MEM_CUSTOM_ALLOC/FREE/REALLOC`, a 48 KB
`LV_MEM_SIZE` default, and the pool hook `LV_MEM_POOL_INCLUDE`/`LV_MEM_POOL_ALLOC`
(`lv_conf_template.h:49-67` at v8.4.0). In v9.6 it becomes `LV_USE_STDLIB_MALLOC` with values
`LV_STDLIB_BUILTIN` (default), `LV_STDLIB_CLIB`, `LV_STDLIB_MICROPYTHON`, `LV_STDLIB_RTTHREAD`,
`LV_STDLIB_CUSTOM` (`lv_conf_template.h:29-37`; the Kconfig mirror is
`config LV_USE_BUILTIN_MALLOC`/`LV_USE_CLIB_MALLOC`/…/`LV_USE_CUSTOM_MALLOC` plus the derived
`LV_USE_STDLIB_MALLOC` int, `Kconfig:1-55`).

The three backends that matter here, read from source:

- **`LV_STDLIB_BUILTIN` (default): a fixed TLSF pool.** `LV_MEM_SIZE` defaults to **65536 B**
  ("Needs to be at least 2kB") and the pool is
  `static MEM_UNIT work_mem_int[LV_MEM_SIZE / sizeof(MEM_UNIT)] LV_ATTRIBUTE_LARGE_RAM_ARRAY;`
  (`src/stdlib/builtin/lv_mem_core_builtin.c:74-83`). As written that array lands in this firmware's
  `.bss` — 64 KB of DIRAM, which the board does not have. `lv_mem_add_pool()` can add more pools after
  init.
- **`LV_STDLIB_CLIB`: `lv_malloc` *is* `malloc`.** `lv_malloc_core` returns `malloc(size)`,
  `lv_free_core` returns `free(p)` (`src/stdlib/clib/lv_mem_core_clib.c:64-77`). Every LVGL widget,
  style, event list and label string therefore lands in ESP-IDF's default allocator — and inherits
  `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` verbatim.
- **`LV_STDLIB_CUSTOM`: "implemented externally".** v9.6.0 ships **no** `src/stdlib/custom/`
  backend — the tree has `builtin/`, `clib/`, `micropython/`, `rtthread/`, `uefi/` only — so choosing it
  means the application must provide the same core symbols the CLIB backend provides. That file is the
  specification: `lv_mem_init`, `lv_mem_deinit`, `lv_mem_add_pool`, `lv_mem_remove_pool`,
  `lv_malloc_core`, `lv_realloc_core`, `lv_free_core`, `lv_mem_monitor_core`, `lv_mem_test_core`
  (`lv_mem_core_clib.c` in full). The public wrappers `lv_malloc`, `lv_calloc`, `lv_zalloc`,
  `lv_malloc_zeroed`, `lv_free`, `lv_realloc`, `lv_reallocf` are in `src/stdlib/lv_mem.c:60-190` and
  add nothing but logging, junk-filling and zero-init.

**The allocator hook that needs no code: `LV_ATTRIBUTE_LARGE_RAM_ARRAY`.** LVGL's attribute macros are
overridable through one header, selected by Kconfig:
`LV_ATTRIBUTE_USE_CUSTOM_INCLUDE` + `LV_ATTRIBUTE_CUSTOM_INCLUDE`
(`Kconfig:3733-3796`), whose help lists `LV_ATTRIBUTE_LARGE_RAM_ARRAY` — "Applied to large arrays
allocated in RAM. e.g. `__attribute__((section(".ext_ram")))`", which is precisely
`EXT_RAM_BSS_ATTR` in IDF. This is how the builtin 64 KB pool can be moved to PSRAM with no shim code
at all, and it is the option that is missing from a naive reading of the docs. `LV_MEM_ADR` is the
other lever (`Kconfig:128-134`, "Place the pool at a fixed address instead of allocating it as a normal
array. 0: unused"), and `LV_MEM_POOL_ALLOC(LV_MEM_SIZE)` is honoured if it is defined, but it has no
Kconfig entry.

**What LVGL asks for.** From LVGL's own requirements page: "Flash/ROM: > 64 kB for the very essential
components (> 180 kB is recommended)"; "Static RAM usage: ~2 kB depending on the used features and
Widget types"; "Stack: > 2kB (> 8 kB recommended)"; "**Dynamic data (heap): > 2 KB (> 48 kB is
recommended if using many GUI Widgets)**"; "Display buffer: > *Horizontal resolution* pixels (> 10 ×
*Horizontal resolution* is recommended)" (`docs/src/introduction/requirements.mdx`).

**Per-object cost, estimated from the structures (not compiled — see §13).** `lv_obj_t` is
`class_p`/`parent`/`spec_attr`/`styles`/`user_data` pointers + `lv_area_t coords` (16 B) + one 32-bit
bitfield word + a 16-bit state/bitfield word (`src/core/lv_obj_private.h:58-160`), i.e. **≈60 B with
the shipped defaults**, plus 8 B if `LV_OBJ_STYLE_CACHE` (default **y**, `Kconfig:1307-1312`) and 4 B
if `LV_USE_OBJ_ID` (default **n**, `Kconfig:1317-1320`). `lv_label_t` adds a `char *text`, the dot
buffer, `size_cache`/`offset`/`text_size` points and flags — **≈110 B before the string**
(`src/widgets/label/lv_label_private.h:31-60`). Styles are `lv_style_t { void *values_and_props;
uint32_t has_group; uint8_t prop_cnt; }` = 12–16 B plus a heap array of `(prop, value)` pairs
(`include/lvgl/core/lv_style.h:370-383`), and `LV_USE_THEME_DEFAULT` is on by default
(`Kconfig:1482-1486`) with its styles in flash. A screen of twenty widgets is therefore **≈2–4 KB of
LVGL heap**, and the documented 48 KB recommendation covers a much busier UI.

## 8. Draw buffers, placement, and the SPI-LCD DMA question

Render modes and sizing, from LVGL's display setup page (`docs/src/main-modules/display/setup.mdx`):
`LV_DISPLAY_RENDER_MODE_PARTIAL` uses "buffers smaller than the size of the display. Use of buffers at
least 1/10 display size is recommended"; `DIRECT` and `FULL` require "the buffer size(s) must match the
size of the display". `lv_display_set_buffers(disp, buf1, buf2, buf_size, render_mode)` takes a byte
size, and the header repeats the rule for DIRECT/FULL: "the buffer size must be at least
`hor_res * ver_res * lv_color_format_get_size(...)`" (`include/lvgl/display/lv_display.h:259-270`).

For 16-bit colour (RGB565, 2 B/px), taking both documented rules of thumb:

| Panel | Full frame | 1/10 (flush-mode recommendation) | 10 × hres (requirements page) | Double-buffered full |
|---|---|---|---|---|
| 320×240 | 153 600 B | 15 360 B | 6 400 B | 307 200 B |
| 800×480 | 768 000 B | 76 800 B | 16 000 B | 1 536 000 B |

Against the board's **18 432 B largest internal block**, only the 320×240 1/10 and 10×hres buffers
could ever be internal, and only if nothing else is competing; every bigger option and every
double-buffered option must be in PSRAM. That is affordable — 8 187 420 B of PSRAM is free — and it
must be requested explicitly, not left to the default allocator (§9).

**Can an SPI LCD DMA out of PSRAM? On 6.1, yes — with a flag and a caveat.** The IDF 6.1 SPI-master
docs state it directly: "{IDF_TARGET_NAME} supports GPSPI Master with DMA transferring data from/to
PSRAM directly without extra internal copy process, which saves memory, by adding
`SPI_TRANS_DMA_USE_PSRAM` flag to the transaction. Note that this feature shares bandwidth (bus
frequency × bus bits width) with MSPI bus, so GPSPI transfer bandwidth should be less than PSRAM
bandwidth, **otherwise transmission data may be lost**"
(`docs/en/api-reference/peripherals/spi_master.rst:356-363`). The `esp_lcd` SPI panel IO exposes it as
`psram_dma_direct` in `esp_lcd_panel_io_spi_config_t` — "If color buffer is in PSRAM, use PSRAM for DMA
buffer directly, has speed limit, but no temp buffer and save memory"
(`components/esp_lcd/include/esp_lcd_io_spi.h:42`) — and sets the transaction flag itself when it
detects an external pointer (`esp_lcd/spi/esp_lcd_panel_io_spi.c:359-378`). None of this exists in
5.4.4.

**What Espressif's own port does instead, and why it matters for the internal budget.**
`esp_lvgl_port`'s display config allocates draw buffers with
`heap_caps_aligned_alloc(CONFIG_LV_DRAW_BUF_ALIGN, buffer_size * color_bytes, buff_caps)` where
`buff_caps` is built from `flags.buff_dma` / `flags.buff_spiram`, and only rejects the combination when
`SOC_PSRAM_DMA_CAPABLE == 0` (`src/lvgl9/esp_lvgl_port_disp.c:344-390`); its own comment repeats
LVGL's rule — "it's recommended to choose the size of the draw buffer(s) to be at least 1/10 screen
sized". Its README documents the no-`psram_dma_direct` recipe as "Using PSRAM canvas": `.buffer_size =
DISP_WIDTH*DISP_HEIGHT, // in PSRAM, not DMA-capable`, `.trans_size = size, // in SRAM, DMA-capable`,
`.flags.buff_spiram = true, .buff_dma = false` (`README.md`, "Using PSRAM canvas"). So the shipped
Espressif pattern spends an internal `trans_size` bounce buffer to feed DMA. On 6.1 that internal
bounce buffer can be avoided entirely by setting `psram_dma_direct` — and given the 18 432 B largest
internal block, budget the bounce buffer (or the flag) explicitly. Draw-buffer alignment is 4 B
(`Kconfig:371-375`, `LV_DRAW_BUF_ALIGN` default 4), which PSRAM allocations satisfy.

## 9. The interaction that decides the design

With `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=131072` and LVGL on `LV_STDLIB_CLIB` or `LV_STDLIB_BUILTIN`
(static pool in `.bss`), here is what actually happens, following §3's code path:

1. **Every LVGL widget, style and string smaller than 128 KB tries internal RAM first.** With 35 327 B
   free (and an 18 432 B largest block at 20 s), a UI built at boot consumes that margin in a few
   hundred widgets. The 64 KB builtin pool is worse: it is one `static` array, so it does not fail
   gracefully — if it links, it permanently removes 64 KB from the heap that Wi-Fi and every driver
   allocates from.
2. **When internal runs out, the allocation does not fail — it lands in PSRAM** via
   `heap_caps_malloc_default`'s third attempt. So the UI will "work", and the placement of each widget
   depends on allocation order rather than on any decision in the code. The damage is elsewhere: the
   internal margin that Wi-Fi's contiguous DMA buffers need (measured: ~25 KB at bring-up, §6) is gone,
   and the 32 KB DMA reserve is reachable by ordinary `malloc()` on 6.1 once the higher-priority
   internal heaps are exhausted. The visible failure is `wifi:malloc buffer fail` /
   `Expected to init 16 rx buffer, actual is 14` (then `abort()`) — exactly the fault the board
   already produced once, at boot, from a much smaller provocation.
3. **LVGL does not detect the failure mode for you.** `LV_USE_ASSERT_MALLOC` defaults to **0**
   (`include/lvgl/config/lv_conf_internal.h:4496-4502`; the Kconfig entry has no default), and with it
   off `LV_ASSERT_MALLOC(p)` expands to nothing (`include/lvgl/debugging/lv_assert.h:84-87`). `lv_malloc`
   logs and returns NULL (`src/stdlib/lv_mem.c:58-84`). A genuinely exhausted system therefore presents
   as a NULL dereference somewhere in LVGL, not as a diagnosable "out of memory".

The three ways out, with their prices (all three keep the encoder state internal unless stated):

- **(A) Keep the threshold; give LVGL explicit PSRAM.** Either
  `LV_ATTRIBUTE_CUSTOM_INCLUDE` + `LV_ATTRIBUTE_LARGE_RAM_ARRAY` = `EXT_RAM_BSS_ATTR` with
  `LV_MEM_SIZE` sized as needed (Kconfig-only, needs `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`),
  or `LV_USE_CUSTOM_MALLOC=y` plus the nine core symbols from §7 implemented over
  `heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_INTERNAL)`. Cost: the encoder keeps
  its 41.4 %, the UI's memory is in PSRAM by construction, and internal keeps its 35 KB / 18 432 B.
  Price: a build-time-fixed pool or a small shim to own and test, and `lv_mem_monitor()` needs a real
  `lv_mem_monitor_core` (implementable over `heap_caps_get_info`) or it reports nothing.
- **(B) Lower `SPIRAM_MALLOC_ALWAYSINTERNAL` to ≤4096 and let the default allocator do the work.**
  Measured: internal free 35 327 → **154 643 B**, largest block 18 432 → **77 824 B**, encoder
  41.4 % → **44.5 %** of one core (+7.6 %). Widgets then land in PSRAM by policy instead of by
  exhaustion; the whole 128 KB threshold behaviour simplifies to "only deliberately-internal
  allocations are internal".
- **(C) Both.** Lower the threshold *and* keep LVGL's draw buffers explicitly in PSRAM (which is
  required either way, because no draw buffer worth having fits in 18 KB).

**Recommendation: (C), with (B) as the primary move and the explicit PSRAM draw buffers as
non-negotiable.** The argument is arithmetic rather than preference. (A) preserves 3.1 percentage
points of one core but leaves the *largest internal block* at 18 432 B forever — and that, not the free
total, is what made Wi-Fi abort and what any future DMA-capable buffer has to fit inside. (B) quadruples
that block and restores a 154 KB internal margin for ~3 pp of one core, while the encoder still has
~55 % of its core unused. If 3 pp turns out to matter, (A) is the fallback and costs no encoder
performance — but then `lv_init()` must be given its PSRAM pool deliberately and the UI's size must be
bounded by that pool. Either way, the draw buffers and the LVGL metadata pool must be *explicitly*
PSRAM; relying on `malloc()`'s fallback path is relying on the failure that has already bitten this
firmware once.

## 10. CPU, IRAM and flash: what the UI adds

**IRAM.** LVGL requires none by default. Its IRAM knob is
`LV_ATTRIBUTE_FAST_MEM_USE_IRAM` — "Place `LV_ATTRIBUTE_FAST_MEM` functions in IRAM (**ESP32 only**)"
(`Kconfig:3799-3803`) — so it does not even offer the option on an S3. `esp_lvgl_port` has no IRAM
configuration at all (its only Kconfig entry is `LVGL_PORT_ENABLE_PPA`, for PPA rotation on P4). The
LCD path needs no IRAM either: `esp_lcd`'s SPI panel IO runs from a task and its DMA descriptors come
from internal RAM as usual. The IRAM budget therefore only *interacts* with LVGL through §1: any LVGL
flash code is fine, and the encoder's 14.4 KB of IRAM code (plus 4.3 KB of literals) is already paid
for. If more IRAM is later wanted for either side, L1 (the Wi-Fi IRAM options, >27 KB documented) is
the only large, reversible source.

**CPU, affinity, stack.** `esp_lvgl_port`'s defaults are
`task_priority = 4, task_stack = 7168, task_affinity = -1, task_max_sleep_ms = 500,
task_stack_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT, timer_period_ms = 5`
(`include/esp_lvgl_port.h:52-72`), and it creates the task with
`xTaskCreatePinnedToCoreWithCaps(..., cfg->task_affinity, caps)` (`src/lvgl9/esp_lvgl_port.c:84-93`) —
i.e. **the default stack is internal and must be counted against the 341 760 B pool**, though
`task_stack_caps` can be set to `MALLOC_CAP_SPIRAM` by the caller. This firmware runs the encoder on
core 1 at priority 6 (≈41 % of that core), the HTTP workers on core 1 at priority 5, and Wi-Fi plus
lwIP on core 0. Pin the LVGL task to **core 1 at priority ≤4** so that the encoder and the HTTP
serving path always preempt it: a dropped UI frame is cosmetic, a dropped audio frame is audible. An
unpinned LVGL task may migrate to core 0 and compete with the Wi-Fi task. LVGL's own CPU cost is not
documented; at 320×240 RGB565 the SPI *bus* is the more likely limit — one full frame is 153 600 B,
which is ≈19 ms at an 8 MB/s effective SPI clock (arithmetic, not measured), and partial refreshes
only send dirty areas.

**Flash.** The app partition is 1500 K = 1 536 000 B (`components/partition_table/partitions_singleapp_large.csv`
in the IDF tree, selected by `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y`), the current 6.1 image is
1 042 576 B, leaving **493 424 B free** (482 KB). LVGL's page asks for >180 KB recommended for the
core; the built-in fonts, measured from the v9.6.0 sources I fetched (bitmap bytes counted out of
`glyph_bitmap[]`, plus 158 `glyph_dsc` entries):

| Font | `glyph_bitmap` bytes | glyphs | generated C source |
|---|---|---|---|
| `lv_font_montserrat_14` (the default, `Kconfig:1679-1681`) | 8 832 | 158 | 93 639 B |
| `lv_font_montserrat_20` | 16 986 | 158 | 146 983 B |
| `lv_font_montserrat_28` | 32 484 | 158 | 247 857 B |
| `lv_font_montserrat_28_compressed` | (compressed) | 158 | 150 424 B |

So core + one 14 px font + a couple of images fits in 493 KB, and two or three larger fonts plus a
demo would not. If flash becomes the binding constraint before RAM does, the 16 MB of flash allows a
custom partition table far larger than 1500 K — and `libtfpsacrypto.a`'s 105 846 B is droppable by
disabling WPA3-Personal, which is a config decision about the access point, not a code change.

## 11. Budget table

Every cell is tagged **[M]** measured (board or link, as noted) or **[E]** estimated from documentation
or from structure fields. Nothing in the LVGL columns is measured on this board.

| Resource | Capacity | Now (shipped config) | LVGL's requirement | Verdict |
|---|---|---|---|---|
| Internal DIRAM pool (`dram0_0_seg`) | 341 760 B [M: map] | 158 974 B used — 96 787 B of it IRAM code, 182 786 B left for all heaps [M: map] | — | the pool everything competes for |
| ↳ static `.text` (IRAM code) | — | 96 787 B [M: map] | 0 B [E: Kconfig shows IRAM option is ESP32-only] | no change |
| ↳ static `.bss` + `.data` | — | 62 187 B [M: map] | ~2 kB static [E: LVGL requirements] | +2 kB |
| Internal heap free at runtime | — | 35 327 B, min-ever 19 664 B [M: board] | **>48 kB for a widget-heavy UI** [E: LVGL requirements] | **does not fit internal** |
| Largest single internal block | — | **18 432 B** [M: board] | 320×240 1/10 buffer 15 360 B; 800×480 1/10 76 800 B [E: arithmetic] | 320×240 marginal, 800×480 impossible |
| Largest DMA-capable block | — | 18 432 B [M: board] | `trans_size` bounce buffer if `psram_dma_direct` is not used [E] | keep it small or use `psram_direct` |
| DMA reserve pool | 32 768 B (of the free figure above) | — | reachable by `malloc()` on 6.1 [M: source] | see §9 |
| 16 KB IRAM-only block | 16 384 B | 16 384 B (100 %) [M: map] | 0 [E] | full; no headroom |
| IRAM code total (`.iram0.text`) | — | 112 143 B [M: ELF] | 0 [E] | encoder's 14.4 KB already inside |
| Wi-Fi IRAM opts | — | ≥9 933 B visible, >27 KB documented [M + doc] | — | droppable if IRAM is ever short |
| PSRAM | 8 MB nominal | 8 187 420 B free [M: board] | 320×240 double full = 307 200 B; 800×480 single full = 768 000 B; LVGL pool 48–128 kB [E] | **fits with megabytes to spare** |
| Flash app partition | 1 536 000 B [M: partition table] | image 1 042 576 B, free **493 424 B** [M: link + file size] | core >180 kB recommended [E] + ~12 kB for one font [E from counted bitmap] | fits, but not two more big fonts |
| RTC fast/slow | 8 192 B each | 8 168 / 8 156 B free [M: map] | not usable [E] | ignore |

## 12. What to change

> **Implemented (2026-10-16).** Option (C) is in the firmware; see §15 for the
> measured results and for one correction to the reasoning above. The text below is
> kept as the analysis that led to it.

1. **Decide the threshold, because every other number hangs off it.** Lower
   `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` to 4096: measured +7.6 % encoder CPU for +119 KB internal free
   and a 4× larger largest block. If that CPU is unacceptable, keep 131072 and take route (A) in §9 —
   but decide, because the difference is whether LVGL's placement is policy or accident.
2. **Allocate the draw buffers explicitly in PSRAM** (`esp_lvgl_port` `.flags.buff_spiram = true`),
   sized at 1/10 of the panel for PARTIAL mode. Never leave this to the default allocator.
3. **On 6.1 set `psram_dma_direct` on the SPI panel IO** and drop the internal `trans_size` bounce
   buffer. On 5.4.4, budget that bounce buffer against the 18 432 B largest internal block.
4. **Give LVGL its own allocator deliberately**: `LV_USE_CUSTOM_MALLOC` with the nine core symbols over
   `heap_caps_malloc_prefer(..., MALLOC_CAP_SPIRAM, MALLOC_CAP_INTERNAL)`, or the builtin pool
   relocated by `LV_ATTRIBUTE_CUSTOM_INCLUDE`/`LV_ATTRIBUTE_LARGE_RAM_ARRAY` +
   `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`. Keep `LV_MEM_SIZE` at what the UI needs.
5. **Keep the bring-up order fix** (validate → `net_start()` → broadcast): the ~25 KB contiguous
   internal requirement belongs to the Wi-Fi driver, not to this configuration.
6. **Pin the LVGL task to core 1 at priority ≤4** with `task_stack_caps` chosen deliberately (internal
   7 168 B by default).
7. **Reclaim the cheap static 12.5 KB**: `EXT_RAM_BSS_ATTR` for `mini_icecast`'s
   `S.chunk`/`tmp[]`/`in[]`; consider L3 for the ~13.8 KB of lwIP/net80211/pp/wpa_supplicant `.bss`.
   Shrink the broadcast stack rather than relocating it (10.7 KB of 24 KB is used).
8. **Pin `lvgl/lvgl` 9.6.0 + `espressif/esp_lvgl_port` 2.9.0**, and correct the README's "~35 KB of
   encoder IRAM code" — the map says 14.4 KB of code, 22.9 KB of internal RAM.

## 13. What to measure next (and what I could not establish)

- **Reconcile the census's `allocated` field** (§6): the free and largest-block numbers agree with the
  map, the allocated total does not. Record the exact `heap_caps_get_info`/`get_free_size` calls.
- **Rebuild the current source with IDF 5.4.4 and re-run `idf.py size`** — until then the DIRAM
  difference between the two maps here is confounded by source changes and must not be quoted.
- **`heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL / MALLOC_CAP_DMA)` with the UI alive.** The
  18 432 B figure, not the free total, decides the next failure.
- **LVGL's real heap high-water mark** via `lv_mem_monitor()` after a representative UI has been used.
- **The encoder's cost at 44.1 kHz/256 kbps on the tonal corpus after the threshold change** (44.5 %
  was measured on synthetic noise; the README's tonal corpus measures ≈40 %).
- **Whether PSRAM-resident widget metadata visibly slows rendering** — unknown, and the reason to
  prefer an explicit pool (one variable) over the fallback path (many).
- **LVGL core + font flash on this target** (`idf.py size` on a real build). My font figures are bitmap
  arrays counted from the generated sources, not compiled objects.
- **The real Wi-Fi IRAM lever**: build with `ESP_WIFI_IRAM_OPT`/`RX_IRAM_OPT` off and diff; the map
  cannot attribute the documented >27 KB.
- **`sizeof(lv_obj_t)`/`sizeof(lv_label_t)` compiled here** — my ≈60 B/≈110 B are read off structure
  fields, not compiled.
- **Whether `LV_STDLIB_CUSTOM` needs exactly the nine CLIB-backend symbols** — confirm by linking.
- **Two unexplained figures in the README**: `heap at startup: internal 305571 B free` versus
  `261087 B free` (most likely different builds, per §5).
- **What `lvgl/lvgl` `9.6.0~1` changes** relative to `9.6.0`; I did not diff them.

## 14. The external artefacts cited, by URL

- LVGL **v9.6.0**: code read from `https://raw.githubusercontent.com/lvgl/lvgl/v9.6.0/<path>` —
  `include/lvgl/lv_version.h`, `include/lvgl/config/lv_conf_internal.h`, `include/lvgl/stdlib/lv_mem.h`,
  `include/lvgl/display/lv_display.h`, `include/lvgl/core/lv_obj.h`, `include/lvgl/core/lv_style.h`,
  `include/lvgl/widgets/lv_label.h`, `include/lvgl/draw/lv_draw_buf.h`,
  `include/lvgl/debugging/lv_assert.h`, `src/stdlib/lv_mem.c`,
  `src/stdlib/builtin/lv_mem_core_builtin.c`, `src/stdlib/clib/lv_mem_core_clib.c`,
  `src/core/lv_obj_private.h`, `src/widgets/label/lv_label_private.h`, `Kconfig`,
  `lv_conf_template.h`, `src/font/lv_font_montserrat_{14,20,28}.c`.
- LVGL **v8.4.0**: `lv_conf_template.h` (for the `LV_MEM_CUSTOM` comparison).
- LVGL docs (**v9.6.0 branch of the same repository**, `docs/src/…`, which is what `docs.lvgl.io`
  renders): `introduction/policies.mdx`, `introduction/requirements.mdx`,
  `main-modules/display/setup.mdx`, `main-modules/fonts/built_in_fonts.mdx`,
  `integration/configuration.mdx`. The rendered page for the display setup text is
  `https://docs.lvgl.io/master/main-modules/display/setup.html`; `docs.lvgl.io/master/porting/display.html`
  now returns a 301 to `https://lvgl.io/docs/open/porting/display`.
- **esp_lvgl_port 2.9.0**: `https://raw.githubusercontent.com/espressif/esp-bsp/master/components/esp_lvgl_port/<path>`
  — `include/esp_lvgl_port.h`, `include/esp_lvgl_port_disp.h`, `src/lvgl9/esp_lvgl_port.c`,
  `src/lvgl9/esp_lvgl_port_disp.c`, `Kconfig`, `README.md`, `idf_component.yml`. There is **no**
  standalone `espressif/esp_lvgl_port` repository (`https://api.github.com/repos/espressif/esp_lvgl_port`
  returns 404); `esp-bsp` is the canonical home. Registry:
  `https://components.espressif.com/components/espressif/esp_lvgl_port` (2.9.0) and
  `https://components.espressif.com/components/lvgl/lvgl` (9.6.0 and 9.6.0~1).
- **ESP-IDF docs** were read from the local trees (`/Users/pedrocasaubon/esp/esp-idf-6.1/docs/en/…`);
  the same pages are published at
  `https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-guides/external-ram.html`,
  `…/api-reference/system/mem_alloc.html`, `…/api-reference/system/freertos.html`,
  `…/api-reference/peripherals/spi_master.html` and `…/api-reference/peripherals/lcd/spi_lcd.html`.

## 15. Implementation status: what (C) cost when it was built (2026-10-16)

Option (C) is implemented: `SPIRAM_MALLOC_ALWAYSINTERNAL=4096`, LVGL 9.6.0~1 with
`LV_USE_CUSTOM_MALLOC`, an explicit PSRAM draw buffer, `LV_USE_ASSERT_MALLOC=y`, and a
startup fit check (`main/ui_fit.c`) that builds a representative screen, reports what
it costs, and frees it. The decision record is `docs/adr/0004-ui-memory-is-external.md`.

**One correction to §9, found by implementing it.** Option (B) as written — "lower
`SPIRAM_MALLOC_ALWAYSINTERNAL` to ≤4096 and let the default allocator do the work …
widgets then land in PSRAM by policy" — is wrong, and the code says why:
`heap_caps_malloc_default()` prefers internal memory for anything **at or below** the
threshold (`if (size <= (size_t)malloc_alwaysinternal_limit)`). At 4096, LVGL's
widgets — tens to hundreds of bytes — would *still* ask internal first and fall through
to PSRAM only on exhaustion, which is the silent behaviour §9 is about. Making the
placement deterministic needs a threshold of **0**, which changes every small
allocation in the system, or a custom allocator for LVGL. The implementation takes the
allocator route, so (C) is (B)'s threshold change **plus** (A)'s mechanism for LVGL,
not (B) plus PSRAM draw buffers alone.

**What the board reports, with the broadcast running (measured):**

```
ui: before the UI:     internal 157787 B free (largest 81920 B), psram 7998472 B free
ui: display 320x240 with a 25600 B partial draw buffer in PSRAM
ui: LVGL: 123 objects hold 54276 B in 956 blocks (peak 56312 B), all of it PSRAM
ui: with the UI:       internal 157787 B free (largest 81920 B), psram 7980304 B free
ui: verdict: the UI's memory is external; internal RAM was the same before and after
ui: after freeing it: the screen is gone, LVGL keeps 6188 B of its own state in 94 blocks
```

- **A 320×240 screen of 123 objects costs 54 276 B, in 956 allocations** — about 57 B
  per allocation once block overhead is real. That is the figure §10 estimated from
  struct sizes; it is now measured.
- **Internal RAM does not move at all**: 157 787 B free and an 81 920 B largest block,
  identical before, during and after. The Wi-Fi margin is untouched by a UI.
- The screen's memory is returned when it is freed; the 6 188 B that remain are LVGL's
  theme and style state, not leakage.
- **The encoder's cost is unchanged at 44.6 % of one core** on the wire, against 44.5 %
  before LVGL existed. The fit check runs once, in `app_main` on core 0, and does not
  perturb the audio core. (Under simultaneous downloads the same measurement reads
  43-49 % across runs: that is SD-bus contention, not the UI.)
- **Flash**: the image grows from 1.04 MB to 1.28 MB, which is why the app partition is
  now 4 MB (`partitions.csv`, 68 % free) instead of the 1.5 MB single-app layout. The
  estimate in §10 that LVGL's flash cost had to be measured is resolved.
- **The custom backend is nine symbols, not the four the first attempt assumed**:
  `lv_mem_init`, `lv_mem_deinit`, `lv_mem_add_pool`, `lv_mem_remove_pool`,
  `lv_malloc_core`, `lv_realloc_core`, `lv_free_core`, `lv_mem_monitor_core`,
  `lv_mem_test_core`. `src/stdlib/clib/lv_mem_core_clib.c` is the shape to copy;
  omitting `lv_mem_init` produces `undefined reference to lv_mem_init` at link time.

**Still to do when the real interface lands** (§10, §13): pin the LVGL task to core 1's
opposite — core 0 — at a priority below the encoder's 6 and the HTTP workers' 5; decide
the panel and its DMA path (`SPI_TRANS_DMA_USE_PSRAM` on 6.1, so the draw buffer can
stay in PSRAM); and set a font budget deliberately, since fonts are the flash cost that
grows fastest.
