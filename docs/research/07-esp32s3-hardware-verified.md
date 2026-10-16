# ESP32-S3 hardware facts, verified from primary sources (2026-10-16)

Checked against the ESP-IDF tree installed at `~/.espressif/v5.4.2/esp-idf` and the
corresponding upstream headers, rather than from memory. These constants gate the
library's memory budget and rule out one entire class of audio output.

## No internal DAC — I2S is mandatory

This is the one hardware fact that eliminates a design option, so it was verified three
ways in IDF's own source:

1. `components/esp_driver_dac/` contains target directories for **`esp32/` and
   `esp32s2/` only** — there is no `esp32s3/` directory.
2. `components/esp_driver_dac/CMakeLists.txt` gates the whole component on the SoC
   capability:
   ```cmake
   if(CONFIG_SOC_DAC_SUPPORTED)
   ```
3. `components/soc/esp32/include/soc/soc_caps.h:66` defines
   `#define SOC_DAC_SUPPORTED 1`, while
   `components/soc/esp32s3/include/soc/soc_caps.h` contains **zero** occurrences of
   `SOC_DAC_SUPPORTED`.

**Consequence:** an ESP32-S3 player cannot use the classic ESP32's built-in 8-bit DAC
trick. Every output path is an external I2S device (amp or codec), which is why the
output layer needs a real codec/amp abstraction and not a "just toggle a pin" fallback.

## Confirmed capabilities (`soc_caps.h`, esp32s3)

| Macro | Value | Meaning |
|---|---|---|
| `SOC_CPU_CORES_NUM` | `2` | dual-core, so producer/consumer core pinning is meaningful |
| `SOC_CPU_HAS_FPU` | `1` | hardware float — relevant because `mi_resample.c` is float-based |
| `SOC_I2S_SUPPORTED` | `1` | I2S available |
| `SOC_I2S_NUM` | `(2U)` | two I2S peripherals |
| `SOC_SPIRAM_SUPPORTED` | `1` | external PSRAM possible (board-dependent) |
| `SOC_DAC_SUPPORTED` | *(absent)* | **no internal DAC** |

The presence of an FPU is worth noting for the resampler question: `mi_resample.c`'s
float dot-products are not being emulated, which makes its measured cost more plausible
on real hardware than a soft-float port would be.

## Memory map (`components/soc/esp32s3/include/soc/soc.h`)

Exact addresses, quoted rather than paraphrased:

```
#define SOC_IRAM_LOW    0x40370000
#define SOC_IRAM_HIGH   0x403E0000
#define SOC_DRAM_LOW    0x3FC88000
#define SOC_DRAM_HIGH   0x3FD00000
```

That is a 448 KB IRAM window and a 480 KB DRAM window. The commonly quoted "512 KB of
internal SRAM" for the ESP32-S3 refers to the total on-chip SRAM pool, which the linker
maps into those (largely aliased, D/IRAM) windows; the address windows above are the
authoritative numbers to reason about when budgeting, because **not all of it is
available to the application at once** — the ESP-IDF runtime, Wi-Fi/lwIP, and the
bootloader all consume part of it before the library sees anything.

The RTC memory regions (`0x600FE000`–`0x60100000`, 8 KB each for RTC fast/slow) are also
present in the same header and are too small to matter for audio buffering.

## What this means for the budget

- The library must not assume PSRAM. `SOC_SPIRAM_SUPPORTED 1` means the *silicon* can
  address it, not that a given board has it, and the decoder/stream buffers are the
  largest allocations in the system. The design therefore needs a PSRAM-preferred,
  internal-SRAM-fallback allocation strategy — exactly the idiom already used by
  `resources/mini_icecast.c:398-399`:
  `heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` falling back to `malloc`.
- Because DRAM is the scarce resource, the stream ring buffer (seconds of PCM) is the
  right thing to place in PSRAM when available, while decoder state and I2S DMA
  descriptors should stay in internal RAM (external-memory access during a flash-cache
  stall is the failure mode to avoid).
- Two I2S peripherals exist, so a future duplex (play + capture) feature is not blocked
  by the SoC — but nothing in the current scope needs the second one.

## Not verified here

- The exact free-heap figure after Wi-Fi + lwIP + I2S are initialised on a real S3
  board — no board was available, and this is the number that actually decides how much
  is left for a decoder. It should be measured on hardware before the library publishes
  a memory budget.
- Whether the specific development board in use has PSRAM fitted.
