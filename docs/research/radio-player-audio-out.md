# Internet-radio player on ESP32-S3 — audio output & concurrency architecture

Axis: **getting decoded PCM out of the chip reliably**, plus the task/buffering structure around it.
Target: an ESP-IDF v5.x component reusable by other projects.

Reading conventions used below:

- **VERIFIED** = claim read out of the primary artefact linked in the same bullet (IDF header/source at a pinned tag, IDF docs, or a vendor datasheet).
- **LIKELY** = inference from my reading of verified material or established engineering practice; no primary source found.
- Nothing here is quoted from a blog. Where a datasheet could not be fetched from this environment, the mirror/link is marked and the claim is demoted.

All ESP-IDF citations are pinned at tag `v5.4` (`esp_driver_i2s` component layout); version differences are called out where they matter.

---

## 1. The I2S output driver in ESP-IDF v5.x

### 1.1 New standard-mode API — exact names (VERIFIED)

| Thing | Definition |
|---|---|
| Header | [`driver/i2s_std.h`](https://github.com/espressif/esp-idf/blob/v5.4/components/esp_driver_i2s/include/driver/i2s_std.h) (plus `driver/i2s_common.h`, `driver/i2s_types.h`) |
| `esp_err_t i2s_new_channel(const i2s_chan_config_t *chan_cfg, i2s_chan_handle_t *ret_tx_handle, i2s_chan_handle_t *ret_rx_handle)` | [i2s_common.h](https://github.com/espressif/esp-idf/blob/v5.4/components/esp_driver_i2s/include/driver/i2s_common.h) |
| `esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t, const i2s_std_config_t *)` | i2s_std.h |
| `esp_err_t i2s_channel_enable(i2s_chan_handle_t)` / `i2s_channel_disable(i2s_chan_handle_t)` | i2s_common.h |
| `esp_err_t i2s_channel_write(i2s_chan_handle_t, const void *src, size_t size, size_t *bytes_written, uint32_t timeout_ms)` | i2s_common.h |
| `esp_err_t i2s_channel_reconfig_std_clock(i2s_chan_handle_t, const i2s_std_clk_config_t *)` | i2s_std.h |
| `esp_err_t i2s_channel_reconfig_std_slot(...)` / `..._reconfig_std_gpio(...)` | i2s_std.h |
| `esp_err_t i2s_channel_preload_data(i2s_chan_handle_t tx, const void *src, size_t size, size_t *bytes_loaded)` | i2s_common.h — **v5.1+**, absent in v5.0 |
| `esp_err_t i2s_channel_register_event_callback(...)` (`on_sent`, `on_send_q_ovf`) | i2s_common.h |
| `esp_err_t i2s_del_channel(i2s_chan_handle_t)` | i2s_common.h |

Legacy `driver/i2s.h` still exists in v5.4 at [`components/driver/deprecated/driver/i2s.h`](https://github.com/espressif/esp-idf/blob/v5.4/components/driver/deprecated/driver/i2s.h) and emits `#warning "This set of I2S APIs has been deprecated..."` unless `CONFIG_I2S_SUPPRESS_DEPRECATE_WARN` is set. Its own header text says "only one set of I2S APIs is allowed to be used at the same time". The v5.0 migration guide states "ADC and DAC modes are removed [from the new I2S driver]. They are only supported in their own drivers and the legacy I2S driver" ([migration guide 5.0 › I2S](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/migration-guides/release-5.x/5.0/peripherals.html)).

### 1.2 Where the new API lives, per IDF version (VERIFIED by HTTP-probing each tag)

| IDF | Header path |
|---|---|
| v4.4 | does not exist (legacy `driver/i2s.h` only) |
| **v5.0** | `components/driver/include/driver/i2s_std.h` |
| **v5.1, v5.2** | `components/driver/i2s/include/driver/i2s_std.h` |
| **v5.3, v5.4, v5.5** | `components/esp_driver_i2s/include/driver/i2s_std.h` (driver split into a standalone component) |

API deltas confirmed by diffing the headers: `i2s_channel_preload_data` does not exist in v5.0 (added v5.1); v5.0's `i2s_chan_config_t` had only `id, role, dma_desc_num, dma_frame_num, auto_clear`, while v5.4 has the union alias `auto_clear`/`auto_clear_after_cb` plus `auto_clear_before_cb`, `allow_pd`, `intr_priority`; `i2s_chan_info_t::total_dma_buf_size` is v5.4-only. **⇒ if the component must span v5.x, target the v5.0 API subset and `#if ESP_IDF_VERSION`-gate preload/PD/perf niceties.** Minimum useful floor: v5.1 (preload) and v5.3 if you want the component-manager dependency to resolve `espressif/esp_driver_i2s`.

### 1.3 Config structs and fields (VERIFIED, v5.4)

`i2s_std_config_t` = three nested structs:

```c
typedef struct {
    i2s_std_clk_config_t  clk_cfg;
    i2s_std_slot_config_t slot_cfg;
    i2s_std_gpio_config_t gpio_cfg;
} i2s_std_config_t;
```

`i2s_std_clk_config_t`: `uint32_t sample_rate_hz;` `i2s_clock_src_t clk_src;` `uint32_t ext_clk_freq_hz;` (only `SOC_I2S_HW_VERSION_2`, i.e. S3/C3+, and only when `clk_src == I2S_CLK_SRC_EXTERNAL`; "must be equal or greater than BCLK, i.e. `sample_rate_hz * slot_bits * 2`") `i2s_mclk_multiple_t mclk_multiple;`
Helper macro `I2S_STD_CLK_DEFAULT_CONFIG(rate)` = `{ .sample_rate_hz = rate, .clk_src = I2S_CLK_SRC_DEFAULT, .mclk_multiple = I2S_MCLK_MULTIPLE_256 }`.

`i2s_std_slot_config_t`: `data_bit_width`, `slot_bit_width`, `slot_mode`, `slot_mask`, `ws_width`, `ws_pol`, `bit_shift`, and on HW v2 (`SOC_I2S_HW_VERSION_2`, which includes S3) `left_align`, `big_endian`, `bit_order_lsb`. Helpers: `I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits, mono_or_stereo)`, `I2S_STD_MSB_SLOT_DEFAULT_CONFIG`, `I2S_STD_PCM_SLOT_DEFAULT_CONFIG`. For a plain I2S DAC use the **Philips** macro — on non-ESP32/S2 targets it sets `slot_mask = I2S_STD_SLOT_BOTH`, `ws_width = bits`, `bit_shift = true`, `left_align = true`, `big_endian = false`, `bit_order_lsb = false`.

`i2s_std_gpio_config_t`: `mclk`, `bclk`, `ws`, `dout`, `din` (all `gpio_num_t`) + bitfields `invert_flags.mclk_inv/bclk_inv/ws_inv`. Use `I2S_GPIO_UNUSED` for unused pins.

`i2s_chan_config_t` (`I2S_CHANNEL_DEFAULT_CONFIG(port, role)`): `id`, `role`, **`dma_desc_num = 6`**, **`dma_frame_num = 240`**, `auto_clear_after_cb = false`, `auto_clear_before_cb = false`, `allow_pd = false`, `intr_priority = 0`. `dma_frame_num` is documented as "frame number in one DMA buffer. One frame means one-time sample data in all slots" and **must be a multiple of 3 for 24-bit data** (enforced for the write buffer too — [I2S docs, Data Transport / 24-bit note](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/i2s.html)).

On ESP32-S3: `I2S_CLK_SRC_DEFAULT == SOC_MOD_CLK_PLL_F160M` (160 MHz), `I2S_CLK_SRC_XTAL` also available ([soc/esp32s3/include/soc/clk_tree_defs.h](https://github.com/espressif/esp-idf/blob/v5.4/components/soc/esp32s3/include/soc/clk_tree_defs.h)); the S3 `soc_caps.h` lists `SOC_I2S_SUPPORTS_XTAL`, `SOC_I2S_SUPPORTS_PLL_F160M` and **no APLL** — so no audio-optimised APLL option on S3 (the classic ESP32 has one). The clock divider is **fractional** on S3: `i2s_std_set_clock` calls `i2s_hal_calc_mclk_precise_division()` and `i2s_ll_tx_set_mclk()` programs numerator/denominator (`i2s_ll_tx_set_raw_clk_div`), so BCLK/MCLK are not limited to integer division of 160 MHz ([hal/i2s_hal.c](https://github.com/espressif/esp-idf/blob/v5.4/components/hal/i2s_hal.c), [hal/esp32s3/include/hal/i2s_ll.h](https://github.com/espressif/esp-idf/blob/v5.4/components/hal/esp32s3/include/hal/i2s_ll.h)). **⇒ exact 44 100/48 000 Hz sample rates are achievable on S3 without APLL.**

### 1.4 `i2s_channel_write()` semantics (VERIFIED in source + docs)

Docs: "Both `i2s_channel_write()` and `i2s_channel_read()` are blocking functions. They keep waiting until the whole source buffer is sent ... unless they exceed the max blocking time, where the error code `ESP_ERR_TIMEOUT` returns." ([I2S docs › Data Transport](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/i2s.html))

Source reading of [`i2s_common.c`](https://github.com/espressif/esp-idf/blob/v5.4/components/esp_driver_i2s/i2s_common.c):

- The function takes a **binary semaphore** (`handle->binary`) for the whole call with `pdMS_TO_TICKS(timeout_ms)`; if it fails it returns **`ESP_ERR_INVALID_STATE`** with the message "The channel is not enabled". **⇒ a second concurrent writer does not block, it errors** — one writer task per channel, by design.
- It then loops: if the current DMA buffer is full (or invalid), it **blocks on `xQueueReceive(handle->msg_queue, ...)`** — the ISR posts a descriptor back when its transfer completed. That queue receive is the actual "wait for the DMA to drain" point, with the same `timeout_ms`.
- Data is **`memcpy`'d from your buffer into the DMA buffer**. It is *not* zero-copy, and the DMA buffers are allocated with `I2S_DMA_ALLOC_CAPS = (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT)` ([i2s_private.h](https://github.com/espressif/esp-idf/blob/v5.4/components/esp_driver_i2s/i2s_private.h)). **⇒ your PCM source buffer may live in PSRAM** — the CPU copy is the only cost. Note `esp_cache_msync(..., C2M)` is emitted in that path on targets with `SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE`.
- On timeout the function returns `ESP_ERR_TIMEOUT` with `*bytes_written` holding the partial count — **partial writes must be handled** by looping with the remainder if you use a finite timeout.
- All public I2S APIs are mutex-protected and are **not ISR-safe** (docs, Thread Safety).
- `i2s_channel_disable()` sets state to READY, takes the binary semaphore, **resets `dma.curr_ptr/curr_desc/rw_pos`** and calls `stop()`. `i2s_channel_enable()` calls `start()` → `i2s_tx_channel_start()` → `i2s_hal_tx_reset()`, `gdma_reset()`, `i2s_hal_tx_reset_fifo()`, then `gdma_start(desc[0])`, and finally **`xQueueReset(handle->msg_queue)`**. **LIKELY:** since the DMA descriptors are re-linked from `desc[0]` and the buffer memory is not zeroed, the *previously written* contents of all `dma_desc_num` buffers are transmitted again after a disable/enable cycle; the docs also note (`auto_clear_*`) that the driver only auto-zeros TX data when no new data is supplied. Treat any stop/start as: expect a repeat of up to `dma_desc_num × dma_frame_num` frames, then a gap. Practical consequence: **do not use disable/enable as a mid-stream rate-change mechanism without flushing/discarding the ring around it.**

### 1.5 DMA depth vs glitch resistance (arithmetic)

Documented relations: `dma_buffer_size = dma_frame_num * slot_num * slot_bit_width / 8`, and the DMA buffer size must stay `<= 4092` bytes ([I2S docs](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/i2s.html), "Please follow these steps to prevent data lost"). Interrupt interval = `dma_frame_num / sample_rate`.

Defaults (`dma_desc_num = 6`, `dma_frame_num = 240`, 16-bit stereo ⇒ 4 B/frame):

- per buffer: 240 × 4 = **960 B** = 5.00 ms @48 k, 5.44 ms @44.1 k
- total DMA depth: 6 × 960 = **5 760 B** = **30.0 ms @48 k** / **32.6 ms @44.1 k**
- interrupt every 5.0 ms

Maximum useful depth: 4 092 B/buffer = 1 023 frames = 21.3 ms; with 8 descriptors = 32 736 B = **170 ms** of internal RAM for DMA. Depths beyond that buy nothing (the buffer is internal RAM you'd rather spend on the ring), so **the DMA is jitter absorption of tens of ms; the ring buffer is what makes the stream glitch-resistant.** Rule of thumb: keep DMA depth ≈ 20–50 ms and put 1–3 s in PSRAM.

`i2s_channel_preload_data()` fills DMA buffers while the channel is still in READY state so the first BCLK/WS edges already carry audio — use it to avoid the initial silence/pop when you enable the channel.

---

## 2. Output paths and boards

### 2.1 Internal 8-bit DAC — **the ESP32-S3 does not have one** (VERIFIED, three independent primary sources)

- [`soc/esp32s3/include/soc/soc_caps.h`](https://github.com/espressif/esp-idf/blob/v5.4/components/soc/esp32s3/include/soc/soc_caps.h) contains **no occurrence of the string `DAC` at all**; [`soc/esp32/include/soc/soc_caps.h`](https://github.com/espressif/esp-idf/blob/v5.4/components/soc/esp32/include/soc/soc_caps.h) defines `SOC_DAC_SUPPORTED 1` and `SOC_DAC_CHAN_NUM 2`. (The S3 caps likewise have no `SOC_I2S_SUPPORTS_ADC`.)
- ESP32-S3 datasheet: the analog chapter (4.2.2) lists only **SAR ADC**, Temperature Sensor, Touch Sensor — there is no DAC section, and the word "DAC" does not occur ([ESP32-S3 Series Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)). The classic ESP32 datasheet has "**Two 8-bit DAC**" in §4.9.2 ([ESP32 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf)).
- The ESP-IDF DAC API page **does not exist for `esp32s3`** — it returns "Page Not Found ... maybe because the current chip or version you choose does not support the feature you are looking for" ([dac.html @ esp32s3](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/dac.html) vs [the same page @ esp32](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/api-reference/peripherals/dac.html)).

**⇒ On S3 every output path is an external I2S (or PDM) device; there is no pin you can drive with 8-bit samples.** Corollary: the cheap "I2S built-in DAC mode" code snippets found in ESP32 tutorials do not port.

Lowest-cost no-chip option, if wanted: **I2S PDM TX** (`driver/i2s_pdm.h`, `i2s_channel_init_pdm_tx_mode`), supported on `I2S_NUM_0` on S3 (`SOC_I2S_SUPPORTS_PDM_TX`, max 2 TX lines), 16-bit only, needs an external RC/LC low-pass — a legitimate "amp-less" path for a small speaker, at the cost of audio quality and no volume control.

### 2.2 Plain I2S DAC / amp chips (no control bus)

| Device | What the driver must do beyond I2S | Verification |
|---|---|---|
| **MAX98357A/B** (Class-D amp, I2S in) | Nothing but `i2s_channel_init_std_mode` + Philips slot + write. No MCLK, no I2C/SPI. Gain and L/R channel are strapped by resistors on `GAIN_SLOT`/`SD_MODE`; shutdown/mute via the `SD_MODE` pin (drive it from a GPIO for pop-free mute). | **UNVERIFIED content** — analog.com/maxim domains were unreachable from this environment; datasheet link for reference: [MAX98357A/MAX98357B](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A-MAX98357B.pdf). Widely-known behaviour, treat as LIKELY. |
| **PCM5102A** (stereo DAC, line out) | **MCLK is optional**: "The integrated PLL on the device removes the requirement for a system clock (commonly known as master clock), allowing a 3-wire I2S connection". If SCK *is* supplied it must be one of the listed ratios; no I2C/SPI on the A2 part, so no hardware volume. | **VERIFIED** ([PCM5102A datasheet](https://www.ti.com/lit/ds/symlink/pcm5102a.pdf), features + §"PLL Clock" / "The serial audio interface typically has 4 connections…"). |
| **UDA1334A/ATS** | Typically needs a system clock (or its own PLL variant) and has no control bus; volume via pins/attenuation. | **UNVERIFIED** — NXP returned HTTP 403 and mirror downloads failed. Do not rely on the MCLK-specific claim; check the vendor datasheet before designing it in. |

Practical note for a board-agnostic component: these parts have **no volume register**, so the library must own digital volume for them (see §6).

### 2.3 Codecs needing I2C control and (usually) MCLK

| Device | Extra work beyond I2S | Verification |
|---|---|---|
| **ES8311** (mono DAC + ADC, I2C) | I2C register init through `audio_codec_ctrl_if_t`. I2C address `0x30` (7-bit 0x18 in most schematics) via `ES8311_CODEC_DEFAULT_ADDR 0x30`. MCLK: `mclk_div` = MCLK/LRCK, **default 256**; `use_mclk=false` switches to deriving the internal clock from LRCK ("DIG_MCLK = LRCK * 256 = BCLK * 4"; for 8 kHz BCLK must be ≥512 k ⇒ 32-bit slots). The driver has a `coeff_div[]` table of supported (MCLK, rate) pairs spanning 8 000, 11 025, 12 000, 16 000, 22 050, 24 000, 32 000, 44 100, 48 000, 64 000, 88 200, 96 000 Hz; a rate/MCLK pair not in the table is rejected with "Unable to configure sample rate". Volume = `ES8311_DAC_REG32` (0x32), written by `es8311_set_vol()` from a dB value. | **VERIFIED** from Espressif's own driver: [es8311.c](https://github.com/espressif/esp-adf/blob/master/components/esp_codec_dev/device/es8311/es8311.c), [es8311_reg.h](https://github.com/espressif/esp-adf/blob/master/components/esp_codec_dev/device/es8311/es8311_reg.h), [es8311_codec.h](https://github.com/espressif/esp-adf/blob/master/components/esp_codec_dev/device/include/es8311_codec.h). Everest Semi's own PDF could not be fetched from this environment (site has no direct PDF paths and mirrors returned HTML) — chip-level details beyond this driver are **UNVERIFIED**. |
| **ES8388** (stereo codec, I2C **or** SPI) | Control via SPI or 2-wire interface ("The device supports standard SPI and 2-wire micro-controller configuration interface", same 3 pins muxed). Slave mode: "LRCK and SCLK must be synchronously derived from the system clock with specific rates. The device can auto detect MCLK/LRCK ratio ... The device only supports the MCLK/LRCK ratios listed in Table 1" — single-speed 8–50 kHz: **256, 384, 512, 768, 1024**; double-speed: 128, 192, 256, 384, 512. LRCK/SCLK is normally 64. Two volume stages: digital `LDACVOL/RDACVOL` (0.5 dB steps, 0 to −96 dB, soft-ramp) and the output-driver stage `LOUT1VOL/ROUT1VOL` (`000000 = −30 dB`, 1 dB steps upward). | **VERIFIED** ([ES8388 datasheet Rev 4.0](http://www.xhxinchip.com/uploads/soft/20220907/1-220ZG54T5941.pdf) — vendor mirror; Everest's own site served no PDF). |
| **ES7210** (4-ch ADC — **no playback**) | I2C control, MCLK. Only useful as a microphone companion to an ES8311 on the same I2S bus. | **VERIFIED** that esp_codec_dev lists it as playback `N` / record `Y` ([README table](https://github.com/espressif/esp-adf/blob/master/components/esp_codec_dev/README.md)); datasheet **UNVERIFIED**. |
| **PCM3060** (I2S-input stereo codec + DAC) | Separate system clocks `SCKI1`/`SCKI2` ("24-bit, 96/192-kHz **asynchronous** stereo audio codec and DAC"), SPI-style control port or hardware mode. "Asynchronous" is the notable property: the DAC side can run off a different clock domain than the ADC side. | **PARTIALLY VERIFIED** — TI datasheet text extracted ([PCM3060](https://www.ti.com/lit/ds/symlink/pcm3060.pdf)) confirms the title/feature wording and the `SCKI1`/`SCKI2` pins; I did **not** verify a specific MCLK/LRCK ratio table (the extracted text is column-mangled). Mark ratio requirements UNVERIFIED. |

**Rule for the component:** the HAL must express three things beyond "write bytes" — (1) whether an external control bus must be initialised, (2) whether MCLK is needed and at what multiple of the sample rate, (3) whether the chip can change rate while stopped. Everything else is board glue.

---

## 3. `espressif/esp_codec_dev`

- **What it is:** "a driver component for audio codec devices ... unified abstract interface to operate on codec device; support customized codec realization; easy-to-use high-level API for playback and recording; volume adjustment in software when not supported in hardware; easy to port to other platforms after replacing codes under `platform`" ([README](https://github.com/espressif/esp-adf/blob/master/components/esp_codec_dev/README.md)). Registry page: [components.espressif.com › espressif/esp_codec_dev](https://components.espressif.com/components/espressif/esp_codec_dev) (registry redirects to the current version, 1.6.2 at the time of writing: [readme](https://components.espressif.com/components/espressif/esp_codec_dev/versions/1.6.2/readme)). Source of truth is the ESP-ADF tree: [components/esp_codec_dev](https://github.com/espressif/esp-adf/tree/master/components/esp_codec_dev) — there is **no** standalone `espressif/esp_codec_dev` GitHub repo (the URL 404s).
- **Supported devices (VERIFIED, README table):** playback — ES8311, ES8388, ES8374, ZL38063, TAS5805M, AW88298, ES8389, ES8156, CJC8910; record — ES8311, ES8388, ES8374, ZL38063, ES8389, ES7210, ES7243, ES7243E, CJC8910; plus a `dummy` codec for "PA-only speaker designs (dummy_codec_new), enabling automatic PA control via `esp_codec_dev_open/close`".
- **Abstraction layers:** `audio_codec_ctrl_if_t` (I2C/SPI register access), `audio_codec_data_if_t` (I2S/PDM byte stream: `open/is_open/enable/set_fmt/read/write/close`), `audio_codec_gpio_if_t` (PA/reset pins), `audio_codec_vol_if_t` (software volume), and the top-level `esp_codec_dev_*`.
- **Public API (VERIFIED, [esp_codec_dev.h](https://github.com/espressif/esp-adf/blob/master/components/esp_codec_dev/include/esp_codec_dev.h)):** `esp_codec_dev_new()`, `esp_codec_dev_open(handle, esp_codec_dev_sample_info_t *fs)`, `esp_codec_dev_write(handle, void *data, int len)`, `esp_codec_dev_read()`, `esp_codec_dev_read_reg()/write_reg()/dump_reg()`, `esp_codec_dev_set_out_vol()/get_out_vol()`, `esp_codec_dev_set_out_mute()`, `esp_codec_dev_set_vol_curve()`, `esp_codec_dev_set_vol_handler()`, `esp_codec_dev_set_in_gain()/set_in_channel_gain()`, `esp_codec_dev_close()`, `esp_codec_dev_delete()`. `esp_codec_dev_sample_info_t` = `{ uint8_t bits_per_sample; uint8_t channel; uint16_t channel_mask; uint32_t sample_rate; int mclk_multiple; }` with "If value is 0, mclk = sample_rate * 256".
- **It is the right integration layer for I2C codecs** (it is what Espressif's own `i2s_es8311` example uses), but three source-level caveats matter for a radio player (VERIFIED in [platform/audio_codec_data_i2s.c](https://github.com/espressif/esp-adf/blob/master/components/esp_codec_dev/platform/audio_codec_data_i2s.c)):
  1. `_i2s_data_write()` calls `i2s_channel_write(..., DEFAULT_WAIT_TIMEOUT)` with `#define DEFAULT_WAIT_TIMEOUT (1000)` ms, and **collapses every error into `ESP_CODEC_DEV_DRV_ERR`, discarding `bytes_written`** — a partial write is indistinguishable from a hard failure.
  2. When a format reconfig is in flight (`out_reconfig`), `_i2s_data_write()` does `esp_codec_dev_sleep(10); return ESP_CODEC_DEV_OK;` — i.e. **it silently accepts and drops the PCM**.
  3. `set_fs()` implements runtime rate changes by `i2s_channel_disable()` → `i2s_channel_reconfig_std_slot()`/`..._clock()` → `i2s_channel_enable()`, i.e. it inherits the stop/start consequences in §1.4.
- **What it does *not* have: no resampling, no drift compensation, no buffer occupancy feedback.** `esp_codec_dev_write` is a straight pass-through to `data_if->write` (plus optional in-place software volume). VERIFIED by absence in the header/source.

---

## 4. The central problem — unknown stream rate vs fixed I2S clock

### 4.1 The constraint

Live radio arrives at whatever rate the encoder used (44 100, 48 000, 32 000, 22 050, 24 000 …), and with HE-AAC/SBR the decoder's *output* rate is typically **twice** the AAC core rate, so the rate you must play is known only after the decoder has parsed the stream and can change between stations/tracks. Meanwhile `i2s_std_clk_config_t::sample_rate_hz` is fixed at `i2s_channel_init_std_mode()` time and `i2s_channel_reconfig_std_clock()` is only legal in the READY state. **⇒ The rate must be resolved once, before the I2S channel is configured (or the channel must be stop/start-cycled).** Design rule: take the rate from the **decoder's current output format**, never from the ICY header or a fixed assumption; rebuild/trim the conversion stage whenever that value changes.

### 4.2 Option A — fix the output rate, resample the stream (recommended default)

- Needs: a converter that accepts arbitrary in→out ratios, plus knowledge of `in_hz` (from the decoder).
- Verified enablers: the S3's fractional I2S divider makes the *output* rate accurate (§1.3), and the stream's nominal rate is exact, so **the whole system is clocked by the ESP32's crystal**; the network becomes the slave.
- Quality cost: a windowed-sinc converter at the ratios actually needed (44.1↔48 k ⇒ 18–20 taps) is cheap and inaudible at 16-bit; resampling 44 100→48 000 rejects at ~0.95·22 050 Hz so nothing folds back.
- Cost of wrong choice: if you fix 48 kHz, every 44.1 kHz stream pays a filter; if you fix 44.1 kHz, every 48 kHz stream pays one. Make it configurable, default 48 000 (most codec PLL tables are centred on 256×48 k = 12.288 MHz, and it avoids the filter for 48 k streams).

### 4.3 Option B — reconfigure the I2S clock per stream

- **VERIFIED:** `i2s_channel_reconfig_std_clock()` "is only allowed to be called when the channel state is READY ... `i2s_channel_disable()` should be called before calling this function if I2S has started" (header comment and `ESP_GOTO_ON_FALSE(handle->state == I2S_CHAN_STATE_READY, ...)` in [i2s_std.c](https://github.com/espressif/esp-idf/blob/v5.4/components/esp_driver_i2s/i2s_std.c)); the docs repeat "users have to first call `i2s_channel_disable()` ... and then call corresponding reconfig functions".
- Consequences (VERIFIED source, §1.4 + LIKELY inference): BCLK/WS stop and restart (audible click/glitch), DMA + FIFO reset, `msg_queue` reset, and the stale DMA content is re-sent after re-enable. So this is only acceptable **at a content boundary** (new station/track, where a few ms of silence is already expected), never per-packet.
- Codec tolerance: because the reconfig happens while stopped, both classes work. ES8311 rewrites its clock-manager registers for the new rate/MCLK pair (`es8311_config_sample`, 8 k–96 k table — VERIFIED); ES8388 in slave mode auto-detects MCLK/LRCK from Table 1 but **only the listed ratios** (VERIFIED); PCM5102A regenerates SCK from its PLL (VERIFIED). Careful: MCLK frequency changes with `sample_rate × mclk_multiple`, so any codec whose PLL is configured by I2C must be re-programmed *before* the new rate starts, and mute/PA should be pulled low around the switch.
- `espressif/esp_codec_dev` already implements exactly this path via `esp_codec_dev_open()` with a new `sample_rate`, including the paired-TX/RX reconfig dance (§3). **That is the cheapest way to get Option B.**

### 4.4 Option C — "let the codec PLL follow"

- What is verified: ES8311 can drop the MCLK wire entirely (`use_mclk=false`, DIG_MCLK = LRCK × 256 — VERIFIED code comment); PCM5102A has an internal PLL that removes the system clock (VERIFIED datasheet). That removes *wiring*, not the rate question: the DAC still converts at LRCK.
- **LIKELY:** none of ES8311/ES8388/PCM5102A contains an asynchronous sample-rate converter that would let a 44.1 kHz stream play correctly on a 48 kHz I2S clock. So "let the codec follow" is *not* an alternative to Option A/B — it only means the library can omit the MCLK pin on some boards. Also note that changing the I2S MCLK frequency itself can reset some codecs: the driver comments "Enable clock to start outputting mclk signal. **Some codecs will reset once mclk stop**" (i2s_std.c).

### 4.5 Producer/consumer drift — the part nobody's API solves

Two independent clocks: the sender's ADC clock + its encoder, and the ESP32's 40 MHz crystal. The ESP32-S3-WROOM-1 module specifies the 40 MHz crystal at **±10 ppm** ([WROOM-1 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf) — "40MHz(±10ppm)"). A sender may be off by tens of ppm of its own.

Arithmetic (stereo int16, 44 100 Hz, 4 B/frame):

- 20 ppm relative mismatch ⇒ 44 100 × 20e-6 = **0.88 frames/s**
- a 2 s buffer (88 200 frames) lasts 88 200 / 0.88 ≈ **100 000 s ≈ 28 h**; 100 ppm ⇒ 4.41 frames/s ⇒ **~5.5 h**

**⇒ drift is a slow problem: a 1–2 s buffer buys hours, and the buffer either drains or fills only on that timescale.** The failure mode is still real (an always-on radio must not die at hour 6), but it can be fixed cheaply and lazily.

Standard mitigations, and what IDF/esp_codec_dev provides:

1. **Occupancy feedback + drop/duplicate** — target 50 % fill; when occupancy < low watermark, repeat the last frame (or hold); when > high watermark, drop one frame. One frame = 23 µs @44.1 k.
2. **Adaptive resampling (preferred)** — trim the converter ratio by ±10–200 ppm with a slow PI controller on buffer occupancy. In a 32.32 fixed-point phase accumulator this is a one-integer change: `step = (in_hz << 32) / out_hz`, and ±1 LSB of `step` is a relative change of `out_hz / (in_hz × 2^32)` ≈ **0.25 ppm** at 44.1 k→48 k. So a ±400 LSB trim spans ±100 ppm with sub-ppm granularity, at zero extra CPU. (See §8 — `mi_resample.c` already has the right accumulator but no API to move it.)
3. **Concealment on underrun** — fade to silence rather than repeat indefinitely; count underruns as a health metric.
4. **What IDF/esp_codec_dev give you: nothing.** The I2S driver has no rate tracking, and `esp_codec_dev` explicitly has no resampler (§3). This controller is the library's job.

---

## 5. Tasks, ring buffers, cores, memory

### 5.1 Pipeline

```
Wi-Fi/lwIP (core 0)
   └─ net task      : blocking recv() from HTTP socket, ICY de-chunking, jitter/metadata strip
        └─ decode task: MP3/AAC/FLAC/Opus -> stereo int16 at stream rate, reported per call
             └─ PCM ring buffer (PSRAM, 1–3 s)
                  └─ out task (core 1): resample to the fixed output rate -> i2s_channel_write()
                       └─ I2S DMA (internal RAM, 20–50 ms) -> codec/amp
```

Why four and not three: the resampler must run in the *consumer* so that occupancy feedback can trim its ratio, and so the ring stores rate-homogeneous data (post-decode) that the output stage can splice/drop at frame granularity. Decode stays in its own task because it is bursty (a 32 kB HTTP read yields hundreds of ms of audio) and must not stall the I2S feeder.

### 5.2 Ring buffer sizing (arithmetic)

stereo int16 ⇒ **4 B/frame**.

| rate | bytes/s | 0.5 s | 1 s | 2 s | 4 s |
|---|---|---|---|---|---|
| 44 100 | 176 400 | 88 200 | 176 400 | 352 800 | 705 600 |
| 48 000 | 192 000 | 96 000 | 192 000 | 384 000 | 768 000 |

Recommendation: **1.5–2 s (≈ 288–384 kB @48 k)** for the decode→output ring; that covers typical Icecast/Shoutcast bufferbloat and Wi-Fi retry bursts. Note that the *codec* ring is sized at the *stream* rate, not the output rate — if you convert before buffering, size at the output rate. Add to that: DMA ≈ 30–50 ms internal, resampler state ≈ 10–55 kB (see §8), and a startup pre-roll of 0.5–2 s (must exceed DMA depth + network jitter; below it you will underrun on the first TCP hiccup).

ESP32-S3 has **512 KB on-chip SRAM** total ([S3 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf), "512 KB on-chip SRAM"), of which Wi-Fi/lwIP/wpa_supplicant take a large share. **⇒ a multi-second PCM ring does not fit in internal RAM; put it in PSRAM** on any module that has it, and degrade to ~0.5–1 s internal-only on modules without.

### 5.3 Where the buffers live

- **DMA buffers/descriptors: internal RAM, always.** `I2S_DMA_ALLOC_CAPS = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT` (VERIFIED, i2s_private.h) and the docs: "DMA transaction descriptors cannot be placed in PSRAM" ([Support for External RAM](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-guides/external-ram.html)).
- **The `i2s_channel_write()` source buffer: PSRAM is fine**, because the driver memcpys it into internal DMA buffers (§1.4). Budget the copy: at 48 kHz stereo that is 384 kB/s read + 384 kB/s write — trivial for either memory, but it does run on the feeder core, so it competes with the resampler for cycles.
- **Verified PSRAM caveats** (same page): "The bandwidth that DMA accesses external RAM is very limited, especially when the core is trying to access the external RAM at the same time"; external RAM shares the cache with flash, so "when accessing large chunks of data (> 32 KB), the cache can be insufficient ... accessing large chunks of data can 'push out' cached flash". ⇒ Keep PSRAM accesses sequential and in ≥ 1 kB blocks (which a ring buffer naturally does), avoid PSRAM for hot per-sample tables (the resampler coefficient table is read every sample — prefer internal RAM or flash `const`), and consider `CONFIG_SPIRAM_SPEED_120M` with octal PSRAM.

### 5.4 FreeRTOS ring buffer vs hand-rolled

VERIFIED from [freertos/ringbuf.h](https://github.com/espressif/esp-idf/blob/v5.4/components/esp_ringbuf/include/freertos/ringbuf.h) and the [IDF ring buffer docs](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/freertos_additions.html):

- Three types: `RINGBUF_TYPE_NOSPLIT`, `RINGBUF_TYPE_ALLOWSPLIT`, `RINGBUF_TYPE_BYTEBUF`. NOSPLIT/ALLOWSPLIT add an **8-byte header per item** and round sizes to 4 B; BYTEBUF has no item headers and no per-item overhead — **use BYTEBUF for a PCM byte stream**.
- Send: `xRingbufferSend(handle, data, size, ticks)`; receive: `xRingbufferReceiveUpTo(handle, &size, ticks, max)` (byte buffers only; a 32-bit-aligned pointer; you must call `vRingbufferReturnItem()` before the next receive). Occupancy is queryable non-blockingly with `xRingbufferGetCurFreeSize()` / `xRingbufferGetMaxItemSize()`.
- External RAM is supported: `xRingbufferCreateWithCaps(size, RINGBUF_TYPE_BYTEBUF, MALLOC_CAP_SPIRAM)` (and delete with `vRingbufferDeleteWithCaps`), or `xRingbufferCreateStatic()` with `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` blocks.
- **There is no watermark/event API.** You must poll `xRingbufferGetCurFreeSize()` from the writer (or maintain your own counters) and signal with a task notification / event group. Cheap because the getter takes the ring's own lock.
- "Drop oldest" is possible with a byte buffer: `xRingbufferReceiveUpTo()` returns a pointer to the oldest bytes; call `vRingbufferReturnItem()` on it to discard without copying. **⇒ a jitter buffer that must discard stale audio on overflow (rather than refusing new data) can be built on the IDF ring buffer.**

**Recommendation:** use `xRingbufferCreateWithCaps(..., RINGBUF_TYPE_BYTEBUF, MALLOC_CAP_SPIRAM)` for the decode→output hand-off and keep the "drop oldest on high watermark" policy in the out task. A hand-rolled single-producer/single-consumer ring with atomic `head`/`tail` is only justified if you need (a) zero-copy writes directly into the ring (`xRingbufferSendAcquire` already gives you that for NOSPLIT, at header cost), or (b) occupancy reads on a hot path without the ring's lock. Neither is needed here. Do **not** put variable-length multi-item structures (HTTP headers, ICY metadata frames) in the PCM ring — give metadata its own small message queue.

### 5.5 Core pinning and priorities (S3 is dual-core)

VERIFIED from the IDF FreeRTOS guide ([api-reference/system/freertos_idf.html](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/freertos_idf.html)) and [Performance › Speed](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-guides/performance/speed.html):

- "Core 0 and Core 1 are sometimes referred to as PRO_CPU and APP_CPU ... Typically, the tasks responsible for handling protocol related processing such as Wi-Fi or Bluetooth are pinned to Core 0 (thus the name PRO_CPU), whereas the tasks handling the remainder of the application are pinned to Core 1 (thus the name APP_CPU)."
- "**Wi-Fi Driver task has high priority (23) and is pinned to Core 0 by default (configurable)**"; "lwIP TCP/IP task has high priority (18 ...) and is not pinned to any core (configurable)"; esp_timer (22) and the event loop (20) are on Core 0.
- "Assign the interrupt on a CPU where built-in Wi-Fi/Bluetooth tasks are not configured to run, which means assigning the **interrupt on Core 1 by default**" — interrupts are allocated on the core that calls `esp_intr_alloc()`.
- Tasks are pinned with `xTaskCreatePinnedToCore()`; unpinned tasks may migrate between cores.

**⇒ The classic guidance still holds in v5.x and is what IDF's own docs say:** keep Wi-Fi/lwIP on core 0, pin the audio pipeline (`xTaskCreatePinnedToCore(..., 1)`) to core 1. Concretely, priorities (an application choice, not a documented requirement):

| task | core | priority | notes |
|---|---|---|---|
| out / I2S feeder | 1 | 18–22 | highest app task on core 1; blocks in `i2s_channel_write` on DMA completion, so it does not burn CPU while waiting |
| decode | 1 | 8–12 | below the feeder; bursty |
| net recv | 1 or unpinned | 5–8 | blocks in `recv()`; the real work happens in the lwIP task on core 0 |
| resampler | inside the feeder task, or a task at 14–16 on core 1 | | must sit below the feeder or run inside it |

Since core 1 also carries the I2S interrupt, keep the feeder's critical sections short and consider `CONFIG_I2S_ISR_IRAM_SAFE` so the EOF interrupt is not deferred during flash writes (docs: "By default, the I2S interrupt will be deferred when the cache is disabled ... you can enable `CONFIG_I2S_ISR_IRAM_SAFE`"). With `CONFIG_FREERTOS_HZ=1000` (already set in this project's `resources/sdkconfig.defaults`), 1 ms is the finest `vTaskDelay` granularity — but the audio path should never need `vTaskDelay`: it is fully event-driven by `xRingbufferReceiveUpTo()` and `i2s_channel_write()`.

---

## 6. Volume control

Two places to scale, with different costs:

1. **Digital scaling in the PCM path.** Costs SNR: each 6.02 dB of attenuation uses one bit of headroom, so a 16-bit stream attenuated by 30 dB ends up with roughly 11 bits of usable resolution (≈ 66 dB SNR) unless you widen to 24/32-bit before scaling. This is textbook quantization arithmetic, **LIKELY** (no single datasheet states it in that form) but it is also why codecs put a *separate analogue* stage after the DAC.
   - Do it above −20…−30 dB only if you must, and dither when you go deep.
   - Never scale below the noise floor of the *source* (a 128 kbps MP3 is already ~14 bits effective).
2. **Codec hardware volume.** Note that several "hardware volume" registers are themselves digital inside the codec — ES8388 `LDACVOL/RDACVOL` are "Digital volume control attenuates the signal in 0.5 dB incremental from 0 to −96 dB" (VERIFIED, ES8388 datasheet) — so they carry the same SNR caveat, *unless* you use the analogue-style output-driver stage: ES8388 `LOUT1VOL/ROUT1VOL` run from `000000 = −30 dB` upward in 1 dB steps (VERIFIED). ES8311 `ES8311_DAC_REG32` is the DAC volume register (VERIFIED from `es8311_set_vol`).

**What `esp_codec_dev` provides (VERIFIED):** `esp_codec_dev_set_out_vol(codec, volume)` maps 0–100 onto the codec's own curve (`set_vol` interface — ES8311 writes REG32), with a documented default curve "1 → −49.5 dB, 100 → 0 dB"; `esp_codec_dev_set_vol_curve()` replaces it; `esp_codec_dev_set_out_mute()`; and for hardware without volume it falls back to a software volume processor (`esp_codec_dev_set_vol_handler`, `audio_codec_new_sw_vol()` — "currently only support 16bits input"). **Gotcha:** `esp_codec_dev_write()`'s doc says "when enable software volume, it will change input data level directly without copy. Make sure that input data is writable" — so a `const` PCM buffer passed to the software-volume path is a bug.

**Recommendation for a board-agnostic component:** expose `volume 0..100` in the HAL; delegate to the codec when it has a volume stage, otherwise apply digital scaling (with an optional dither) inside the library, and keep the curve dB-based so the control feels the same on both paths. For MCLK-less amps (MAX98357A, PCM5102A, UDA1334) digital volume is the only option — make sure it happens **after** the resampler (so the resampler always sees full-scale data and the trim math is unchanged) and **before** the DMA write.

---

## 7. The board-abstraction seam

**Where the boundary belongs:** below the resampler, above the bytes. The library owns decode → ring → resample → volume and calls a *sink* that consumes **interleaved int16 PCM at the configured output rate**. The sink is responsible for everything physical (I2S peripheral, codec I2C init, MCLK, PA pin, mute). Optional `set_rate()` exists only for sinks that can genuinely re-clock (I2S + codec), which keeps the Option A/B choice inside the HAL rather than in the pipeline.

```c
/* radio_out.h — the HAL seam. One vtable, one opaque ctx. */
typedef struct radio_out radio_out_t;

typedef struct {
    /* All functions return ESP_OK / ESP_ERR_* (esp_codec_dev ints map 1:1 via ESP_CODEC_DEV_OK == 0). */

    /* Configure/claim the output. Called once before streaming, and again on a rate change
     * only if set_rate is NULL (i.e. the sink must be re-opened), otherwise on every stream. */
    esp_err_t (*open)(radio_out_t *self, uint32_t sample_rate, uint8_t bits, uint8_t channels);

    /* Blocking write of interleaved PCM; must honour timeout_ms and report partial progress.
     * Maps to i2s_channel_write() or esp_codec_dev_write(). */
    esp_err_t (*write)(radio_out_t *self, const void *pcm, size_t bytes,
                       size_t *written, uint32_t timeout_ms);

    /* Optional: change the output rate while the sink stays open (I2S disable/reconfig/enable).
     * NULL => the pipeline must resample to the rate fixed at open(). */
    esp_err_t (*set_rate)(radio_out_t *self, uint32_t sample_rate);

    /* 0..100; NULL => the library applies digital volume itself. */
    esp_err_t (*set_volume)(radio_out_t *self, int volume_0_100);
    esp_err_t (*set_mute)(radio_out_t *self, bool mute);

    /* Diagnostics the pipeline needs for drift control: how many PCM bytes the sink can still
     * accept (DMA free space / bytes consumed), and counters for underrun/overflow. */
    size_t    (*free_bytes)(radio_out_t *self);
    uint32_t  (*underrun_count)(radio_out_t *self);

    esp_err_t (*close)(radio_out_t *self);
    void      *ctx;      /* e.g. i2s_chan_handle_t, or the esp_codec_dev_handle_t */
} radio_out_ops_t;

struct radio_out { const radio_out_ops_t *ops; void *impl; };
```

**Why a function-pointer struct rather than weak symbols:**

- **Runtime selection.** Weak symbols can only be overridden once, at link time. Boards differ at runtime (the same firmware can detect an ES8311 vs a PCM5102A on a BSP, or fall back to a network sink); a vtable is just a pointer you choose at `radio_player_start()`.
- **Multiple sinks / fan-out.** A "network sink" (ICE cast-style re-stream, or the existing `mini_icecast_pcm_cb_t` hook in `resources/mini_icecast.h`) can be composed alongside I2S only with an interface, not with a link-time override.
- **Testability.** A fake sink that counts frames and injects underruns is how you unit-test the ring/watermark/drift logic on the host — the same reason `resources/mi_resample.c` ships with `resources/host_convert.c`. Weak symbols would force the whole firmware into the test.
- **Precedent in-tree.** ESP-IDF's own HAL (`i2s_hal_*`), FreeRTOS, lwIP and `esp_codec_dev` (`audio_codec_data_if_t`, `audio_codec_ctrl_if_t`, `audio_codec_vol_if_t`) all use exactly this shape, so wrapping `esp_codec_dev` behind it is a handful of one-line adapters (`esp_codec_dev_write` → `write`, `esp_codec_dev_set_out_vol` → `set_volume`, `esp_codec_dev_open` with a new `sample_rate` → `set_rate`). Provide one concrete `radio_out_i2s` (direct `i2s_channel_*`, for amp-only boards) and one `radio_out_codec_dev` (wraps `esp_codec_dev`) plus a weak default symbol so a naive build links without a board file.
- **Keep the seam narrow:** don't put the ring buffer or the resampler in the HAL, and don't let the HAL see the decoder. The one thing that *must* cross the seam is the sink's free space / consumption rate — without it the library cannot implement drift control (§4.5).

---

## 8. Verdict on `resources/mi_resample.c`

### 8.1 What it is (verified by reading the source)

A streaming windowed-sinc resampler, stereo int16 → stereo int16, self-contained (~110 lines of `.c`), with:
`NPH 128` phase rows + linear interpolation between rows; `ZERO_X 8` sinc zero-crossings per side; Blackman window (`0.42 + 0.5cos + 0.08cos2`); cutoff `fc = 0.95 * min(1, out/in)` (anti-aliasing on downsample); per-phase-row DC normalisation (`row[k] /= sum`); a 32.32 fixed-point phase accumulator (`step = (uint64_t)in_hz << 32) / out_hz`, `pos` in input frames relative to `buf[0]`); float coefficient table `(NPH+1) × T` and a float history/input buffer of `2 × (T + MAXIN + 8)`; `BATCH 256` output frames per callback; streaming history carry-over plus `memmove` to drop consumed input; and **bypass** (direct passthrough) when `in_hz == out_hz`. `T = 2*ceil(ZERO_X/fc)` taps.

Representative tap counts from that formula:

| conversion | fc | T |
|---|---|---|
| 22 050 → 48 000 (HE-AAC 2× case) | 0.95 | 18 |
| 44 100 → 48 000 | 0.95 | 18 |
| 48 000 → 44 100 | 0.873 | 20 |
| 44 100 → 22 050 | 0.475 | 34 |
| 48 000 → 8 000 | 0.158 | 102 |
| 96 000 → 8 000 | 0.079 | 204 |

### 8.2 Verdict: **REUSABLE WITH CAVEATS.** Keep the kernel; wrap it and make five changes.

The DSP core is the right shape for a radio player — a proper sinc with a Blackman window and a cutoff that tracks `min(in,out)`, DC-normalised phases, and a fixed-point accumulator that cannot drift. The caveats are all packaging/interface, not kernel quality:

1. **No way to trim the ratio ⇒ cannot do adaptive drift compensation.** `r->step` is written once in `mi_rs_new()` and is private; `mi_rs_new()` also *replaces* the converter when rates differ. The fixed-point scheme makes the fix trivial and unusually precise: `step = (in_hz << 32) / out_hz`, so **±1 LSB of `step` is `out_hz / (in_hz × 2^32)` ≈ 0.25 ppm at 44.1 k→48 k** — a ±400 LSB trim spans ±100 ppm with sub-ppm granularity and *zero* extra CPU. Required: `void mi_rs_trim_ppm(mi_rs_t *r, int ppm)` (and have it clamp to avoid overflow / underflow of `pos`). This is the single most important change; without it the library cannot solve §4.5.
2. **Unbounded `T` ⇒ stack buffer overflow in the constructor.** `mi_rs_new()` declares `double row[256]` on the stack and writes `row[k]` for `k < T` with no bound check (mi_resample.c:47–49). `T > 256` when `fc < 0.0631`, i.e. `out/in < 0.0664` — e.g. 48 000 → 3 000 Hz, or 96 000 → 6 000 Hz. Not reachable for the 8 k–96 kHz output set, but the constructor validates nothing and a caller passing a silly pair corrupts the stack. Required: reject/clamp `T` (or heap-allocate the row), and validate `in_hz`/`out_hz` non-zero.
3. **Allocation strategy is unmanaged.** `malloc`/`calloc` with no capability flags, so placement depends on `CONFIG_SPIRAM_USE_MALLOC` (which is *commented out* in `resources/sdkconfig.defaults`, so today's tables land in internal RAM). Sizes: table = `(NPH+1) × T × 4 B` = 9.3 kB at T=18, **52.6 kB at T=102**, 105 kB at T=204; plus the float history `2 × (T + 1024 + 8) × 4 B` ≈ 8.3 kB at T=18 rising to 10 kB. For a component you want (a) `heap_caps_malloc(..., MALLOC_CAP_INTERNAL)` for the history (touched every sample) and PSRAM-or-flash for the table, and (b) an API to change rates **without** re-allocating: add `mi_rs_reset(mi_rs_t *, in_hz, out_hz)` that regenerates the table in place when `T` is unchanged, instead of freeing/allocating ~10–50 kB on the live audio path on every track change.
4. **Output block size is not fixed.** `mi_rs_process()` flushes the partial batch at the end of every call (mi_resample.c:103: `if (r->out_n) { emit(...); r->out_n = 0; }`), so `emit()` sees 1..`BATCH` frames depending on how the caller chunks its input. Harmless for a byte-stream sink, but it defeats fixed-size `i2s_channel_write()` buffers and makes the output-time arithmetic in a drift controller wobbly. Prefer an explicit `mi_rs_flush()` and hold the remainder.
5. **Quality/cost notes (partly LIKELY).** Float accumulation is fine for 16-bit output: for `T` up to ~100 the worst-case accumulation error is ~1e-5 relative (≈ −100 dB), well under a 16-bit LSB. **UNVERIFIED estimate:** `NPH = 128` with linear interpolation between phase rows caps the kernel reconstruction error at roughly −80 dB (the `(1/NPH)^2` interpolation term), which is below the ~96 dB of 16-bit but inaudible for radio — raising `NPH` to 256 would double the table and halve that error if you ever care. CPU scales as ~`T × 2 channels` MACs per output frame (`T` = 18 dominates the common cases ⇒ a few M MAC/s on a 240 MHz core, single-digit % of one core). Note also that the API is not thread-safe by design (single instance, single caller) — call it from the out task only — and that `bypass` mode makes a 1:1 stream free, which is exactly the right fast path if you choose the fixed output rate to match your most common stream.

**On HE-AAC 2×:** the converter handles it cleanly — 22 050 → 48 000 or 22 050 → 44 100 is a pure 2.18×/2× upsample with `fc = 0.95`, `T = 18`, no aliasing risk, and the same fixed-point path. What it does *not* handle is a **rate change mid-stream**: with no reset/trim API you must `mi_rs_free()` + `mi_rs_new()` (losing ~10 kB and a few thousand `sin()` evaluations — sub-millisecond, but a heap churn on the audio path). That is caveat 1 + caveat 3 again, not a numerical problem with the 2× case.

**Keep `resources/host_convert.c`.** The fact that the resampler is host-testable with plain gcc is a genuine asset for a reusable component: the ratio-trim API from caveat 1 can be regression-tested off-device (feed a known sine, check the output rate, check the drop/duplicate controller converges), and the same harness can lock in the 8 k/16 k/22.05 k/32 k/44.1 k/48 k/96 k matrix.

---

## 9. What I could NOT verify (and what to check before committing)

1. **ES8311 vendor datasheet** — Everest Semi's site exposes no direct PDF path and both mirrors I tried returned HTML/truncated files. All ES8311 chip-level claims above come from Espressif's `esp_codec_dev` driver (`coeff_div` table, `ES8311_DAC_REG32` volume, `mclk_div` default 256, `use_mclk == false ⇒ DIG_MCLK = LRCK*256`). Verify against the real datasheet: max MCLK/LRCK ratio at 96 kHz, whether REG32 is digital-only, and whether the I2C address is 0x30 (7-bit) or 0x18 (8-bit notation).
2. **MAX98357A/MAX98357B datasheet** — analog.com and datasheets.maximintegrated.com were unreachable from this environment (timeouts); Mouser returned an access-denied page. The "no MCLK, no control bus, gain by pin strapping" description is LIKELY, not verified here.
3. **UDA1334ATS datasheet** — NXP returned HTTP 403; mirrors 403/404. Its clock requirements are unverified; check before designing it in.
4. **PCM3060 MCLK/LRCK ratio table** — the datasheet's text extraction from TI's PDF is column-mangled; only the "asynchronous stereo audio codec and DAC" title/feature wording and the `SCKI1`/`SCKI2` pins are verified.
5. **ES7210 datasheet** — unverified; only esp_codec_dev's support table (record-only) was verified.
6. **The "stale DMA content is re-sent after disable/enable" behaviour** — inferred from reading `i2s_channel_disable()`/`i2s_enable→i2s_tx_channel_start()` (DMA reset + re-link from `desc[0]`, `xQueueReset`, buffers not zeroed). I did not run it on hardware. Confirm with a logic analyser before relying on it for a rate-switch design; if true, flush/zero the ring around any Option B switch.
7. **HE-AAC SBR doubling the output rate** — treated as established domain knowledge; I did not fetch ISO/IEC 14496-3. The design rule ("always read the rate from the decoder's current output format") is safe regardless.
8. **The `NPH = 128` + linear-interpolation error floor (~−80 dB)** — my own estimate, not measured. A host FFT test would settle it.
9. **ESP-ADF's `i2s_stream`/pipeline drift behaviour** — I did not read it; the claim in §4.5 that Espressif ships no drift compensation is based on the I2S driver and `esp_codec_dev` sources only.
