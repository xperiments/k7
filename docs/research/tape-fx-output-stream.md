# Tape FX on the output stream

> **Retired.** This effect was built, measured and then removed from the product at the
> owner's request: it ran on the stream, but its character was too subtle to be worth the
> remaining headroom, and the settings never earned a place in the interface. What is below is
> the record of the investigation - the measurements, the two operating points, the null tests
> and the reasons a number came out the way it did - and it describes code that no longer
> exists in this tree. It is kept because the numbers are still the answers to the questions
> they were asked: what a per-sample effect costs on this chip, what a division costs, what
> the encoder costs, and how far a cassette emulation can go before it is audible.

Is a C99 tape_fx (saturation + 3 biquads per channel + a 1024-sample modulated delay + hiss,
two `sinf()` per sample) viable per-sample before the MP3 encoder, and what does it cost?

**Verdict: viable, and small next to the encoder — but the version as written pays for six
library calls per sample that it does not need.**

- **It plugs into a seam that already exists**: the 1024-frame PCM chunk before the encoder.
  One call and 8.4 KB of state; the encoder, the rings, the pacing clock, the ICY metadata and
  the HTTP surface do not change, because the effect never sees an encoded byte. No added
  latency. ([§5](#5-where-it-goes-the-pcm-seam-not-the-wire))
- **Cost at 44.1 kHz stereo: 11.0 % of one 240 MHz core, measured on the board** (9.0 %
  estimated from the instruction count first; the model's cycles-per-instruction was optimistic
  by 22 %). As written it would be ~21 %. The encoder on the same core, on the same material,
  measured 60.6 %, so the broadcast goes from **60.6 % to 70.8 %**, with zero starvation
  rebases. ([§6](#6-what-it-costs))
- **The filters are not the expense; the calls are.** The effect calls `sinf()` twice **per
  sample** (88 200 calls/s, not 77) and divides four times per sample inside `fast_tanh()`, and
  on this toolchain a float division is a 23-instruction libgcc routine rather than one
  instruction. Those six calls are more than half the cost.
  ([§2.4](#24-why-the-division-matters-and-why-no-flag-fixes-it-verified-locally))
- **It is not drop-in as-is.** The make-up gain is not unity (+0.8 to +4.6 dB depending on the
  knobs), the default head setting is 6.5 kHz dark, and "Dolby" is a noise-floor knob. ([§8](#8-what-changed-in-the-component-and-what-is-still-open))

Labels: **VERIFIED** = read out of the primary artefact linked in the same line;
**MEASURED** = produced here, on this machine or this toolchain, with the commands given;
**ESTIMATE** = derived from a measurement, with the assumption stated.

Verification date 2026-10-19. Primary sources are pinned at ESP-IDF v5.4.2 (newlib) plus the
ESP32-S3 datasheet and TRM v1.8, as installed at `~/.espressif/v5.4.2/esp-idf` (commit
`f5c3654a1c`) with its `xtensa-esp-elf` 14.2.0_20241119. The local measurements use the
toolchain this repo actually builds with, IDF 6.1 / `xtensa-esp-elf` 15.2.0_20251204
(picolibc). The two agree on the shape of the finding — `sinf` is a libm call of ~100
instructions with no double-precision work — but the object names differ
(`libm_a-sf_sin.o` against picolibc's `libm_math_sf_sin.c.o`).

## 1. Hardware facts (verified)

| Fact | Value | Source |
|---|---|---|
| CPU | Xtensa LX7, **dual-core**, 240 MHz max | [ESP32-S3 datasheet](https://documentation.espressif.com/esp32-s3_datasheet_en.pdf) p.3 ("Clock speed: up to 240 MHz"), p.36 |
| FPU | **single precision only** | datasheet p.3: "Single precision floating point unit (FPU)"; p.36: "Support for single-precision floating-point unit (FPU)" |
| FPU arithmetic options | `FP=1`, `FP_DIV=1`, `FP_RECIP=1`, `FP_SQRT=1`, `FP_RSQRT=1` | [`core-isa.h` L130-134](https://github.com/espressif/esp-idf/blob/v5.4/components/xtensa/esp32s3/include/xtensa/config/core-isa.h#L130-L134) |
| Double precision in hardware | **none** (`XCHAL_HAVE_DFP 0`; only a single-precision DFPU coprocessor) | [`core-isa.h` L135, L143](https://github.com/espressif/esp-idf/blob/v5.4/components/xtensa/esp32s3/include/xtensa/config/core-isa.h#L135-L143) |
| `double` cost | software-emulated, "very slow" | [ESP-IDF Speed Optimization, "Improving Overall Speed"](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-guides/performance/speed.html) (`speed.rst` L89) |
| FP register file | **16 × 32-bit** registers (`FR`) | [ESP32-S3 TRM](https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf) Table 1.5-1, p.45 |
| Zero-overhead loops | present (`XCHAL_HAVE_LOOPS 1`) | [`core-isa.h` L56](https://github.com/espressif/esp-idf/blob/v5.4/components/xtensa/esp32s3/include/xtensa/config/core-isa.h#L56) |
| Fused multiply-add | **present and used**: `madd.s` / `msub.s` | esp-dsp inner loop [`dsps_biquad_f32_aes3.S` L70-76](https://github.com/espressif/esp-dsp/blob/master/modules/iir/biquad/dsps_biquad_f32_aes3.S#L70-L76); also emitted in the toolchain's S3 `libm` (below) |

Two details worth having straight:

- **Load/store path to the FPU.** Scalars move with the native single-float loads/stores
  (`lsi` / `ssi`, one 32-bit FR each), and AR↔FR moves use `wfr` / `rfr`; both appear in
  Espressif's own `dsps_biquad_f32_aes3.S` (`lsi`/`ssi`) and in the toolchain's `libm`
  objects (`wfr f1,a2`, `rfr a2,f0`). The S3 **PIE** extension adds 64-/128-bit FP loads
  (`EE.LDF.64.*`, `EE.LDF.128.*`) which fill two or four FRs at once, and the TRM says
  these are "4 times more efficient than the 32-bit floating-point data R/W instructions
  that are native to the Xtensa processor" ([TRM §1.5.1.1, p.46](https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf)).
- **`madd.s` / `msub.s` are the FPU's multiply-accumulate instructions.** Espressif's
  hand-written biquad kernel uses four `madd.s` per sample in both the `_ae32` and `_aes3`
  variants, and the toolchain's S3 `libm` uses them throughout, so the core does have an FMA
  form and the compiler targets it. Espressif's public docs do not state its rounding
  behaviour; that detail is only in Cadence's LX7 manual.


## 2. Float cost: what the primary sources do and do not say

### 2.1 No ROM math on ESP32-S3

- The S3 ROM interface exports **no** libm symbols. `components/esp_rom/esp32s3/ld/` contains
  newlib, libgcc and peripheral fragments only — no `*libm*` fragment
  ([directory](https://github.com/espressif/esp-idf/tree/v5.4/components/esp_rom/esp32s3/ld)),
  and [`esp32s3.rom.newlib.ld`](https://github.com/espressif/esp-idf/blob/v5.4/components/esp_rom/esp32s3/ld/esp32s3.rom.newlib.ld)
  lists `memcpy`/`strlen`/`isalpha`-class functions, no `sinf`/`cosf`/`powf`. There is no
  `esp_rom/esp32s3/include/esp32s3/rom/math.h`, and a grep of `esp_rom/` for `sinf` returns
  nothing.
- Checked by symbol table, not by absence of a header: Espressif's ROM image
  `esp32s3_rev0_rom.elf` (esp-rom-elfs 20241011) exports 7304 symbols and **none** match
  `sinf|cosf|powf|sin|cos|pow` (local `nm` scan of
  `~/.espressif/tools/esp-rom-elfs/20241011/esp32s3_rev0_rom.elf`). The same is true of the
  ESP32 ROM image and of every `*.rom.libm.ld` URL across IDF v3.3/v4.4/v5.1/v5.4 — those
  files do not exist (only `*.rom.libgcc.ld` does).

**Consequence:** on this chip `sinf`/`cosf`/`powf` are normal newlib calls linked into flash.
They are not ROM calls, so there is no ROM table to look up and no ROM speed advantage; they
are subject to instruction-cache misses unless placed in IRAM.

### 2.2 `sinf`/`cosf`/`powf` are float-native in this toolchain (verified by disassembly)

Inspected the per-core archive
`~/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20241119/xtensa-esp-elf/xtensa-esp-elf/lib/esp32s3/libm.a`
(the multilib IDF links for `esp32s3`; note it is a *different* archive from the generic
`lib/libm.a`, which is soft-float):

| Function | Implementation | Evidence |
|---|---|---|
| `sinf` | `__ieee754_rem_pio2f` → `__kernel_sinf` / `__kernel_cosf` | relocations in `libm_a-sf_sin.o`; kernel bodies use `madd.s`, `msub.s`, `mul.s`, `trunc.s`, `mov.s` |
| `cosf` | same range reduction + kernels | `libm_a-sf_cos.o` |
| `powf` | `__ieee754_powf`, all single precision | `libm_a-wf_pow.o` → `libm_a-ef_pow.o`; 20 `madd.s`, 11 `msub.s`, 12 `add.s`, **no** `__adddf3`/`__muldf3`/`__divdf3` references |
| `pow` (double) | `__ieee754_pow` and it *does* pull soft-double | `libm_a-e_pow.o` references `__adddf3`, `__muldf3`, `__divdf3` |
| `powf` float divide | references `__divsf3` and `__ieee754_sqrtf` | even though the core declares `FP_DIV`/`FP_SQRT`, so check what your build actually emits — answered for `esp32s3` in [§2.4](#24-why-the-division-matters-and-why-no-flag-fixes-it-verified-locally) |

Static code size of the fast path (instruction counts, **not** cycles): `sinf` body 49
instructions, `__kernel_sinf` 61, `__kernel_cosf` 70, `__ieee754_rem_pio2f` 216 (the large-
argument path; small arguments take a short branch). A `sinf()` call is therefore a two- or
three-call chain on the order of 100 instructions, with no double-precision work.

**Published cycle counts: none found.** Espressif publishes cycle counts only for its own DSP
kernels (esp-dsp benchmarks, next section). We found no Espressif datasheet, TRM chapter,
`esp_rom` header or libm document that gives cycles per `sinf`/`cosf`/`powf`, and no primary
source for libm throughput on the S3. Numbers circulating on blogs/forums are third-party
microbenchmarks and are unverified here.

### 2.3 FPU latency is not documented in primary sources

**Not documented in primary sources:** FPU instruction latency/throughput, and therefore
whether any `float` op is single-cycle. The datasheet only names the unit; the TRM documents
the FR file and the PIE 128-bit loads but no FPU pipeline timings; Espressif's own
[Xtensa ISA summary](https://github.com/espressif/xtensa-isa-doc) explicitly puts
"coprocessors" out of scope and its instruction list does not cover the FPU extension.
Treat "float is single-cycle" as folklore until measured with `cpu_hal_get_cycle_count()`
(the method ESP-IDF itself recommends in the Speed guide, and it must be called from a
pinned task or an ISR because the counter is per-core).

### 2.4 Why the division matters, and why no flag fixes it (VERIFIED locally)

This closes the open question the primary sources could not: **`float a / float b` never
becomes one instruction on this toolchain**, even though the core declares `FP_DIV 1`.
Compiled for `esp32s3` and disassembled (`make asm`), `a / b` is `call8 __divsf3`, and libgcc
implements that as a 23-instruction Newton-Raphson chain — `div0.s`, `const.s`, ten
`maddn.s`, `nexp01.s`, `addexpm.s`, `divn.s` — with a serial dependency the scheduler cannot
hide. GCC fuses `a * b + c` into `madd.s` and emits `add.s`/`mul.s`/`sub.s` inline, but it
does not emit `div.s` or `sqrt.s`; `sqrtf()` is a libm call too. There is no `-mfpu` option to
change this, and `-ffast-math` does not change it either.

The practical consequence for tape_fx: its `fast_tanh()` is a rational function, so the two
calls per channel are two divisions per channel, four per frame. That is the single most
expensive thing in the effect and it is removable — see [§6](#6-what-it-costs).

## 3. Optimized building blocks that already exist (esp-dsp, ROM)

`esp-dsp` is the official library ([repo](https://github.com/espressif/esp-dsp)); its biquad
kernels live under `modules/iir/biquad/`:

| Symbol | Source | Notes |
|---|---|---|
| `dsps_biquad_f32_ansi` | [`dsps_biquad_f32_ansi.c`](https://github.com/espressif/esp-dsp/blob/master/modules/iir/biquad/dsps_biquad_f32_ansi.c) | reference C, direct-form-II transposed; 1 biquad, 1 channel |
| `dsps_biquad_f32_ae32` | [`dsps_biquad_f32_ae32.S`](https://github.com/espressif/esp-dsp/blob/master/modules/iir/biquad/dsps_biquad_f32_ae32.S) | LX6/LX7 assembly, zero-overhead `loopnez` |
| `dsps_biquad_f32_aes3` | [`dsps_biquad_f32_aes3.S`](https://github.com/espressif/esp-dsp/blob/master/modules/iir/biquad/dsps_biquad_f32_aes3.S) | the S3 variant; 12-instruction inner loop: 4× `madd.s`, `mul.s`, 2× `mov.s`, 2× `lsi`, `ssi`, 2× `addi` |
| `dsps_biquad_sf32_*` | same directory | stereo (interleaved L/R) versions, one call per biquad for both channels |
| `dsps_biquad_gen_f32` | [`dsps_biquad_gen_f32.c`](https://github.com/espressif/esp-dsp/blob/master/modules/iir/biquad/dsps_biquad_gen_f32.c) | coefficient generation (setup time, not per-sample) |

The optimized kernels exploit the FPU's FMA and the zero-overhead loop; the `_aes3` kernel is
compiled/enabled only when `CONFIG_IDF_TARGET_ESP32S3`
([`dsps_biquad_platform.h` L10-19](https://github.com/espressif/esp-dsp/blob/master/modules/iir/include/dsps_biquad_platform.h#L10-L19)).

**Dispatch gotcha:** in `dsps_biquad.h` the `_ae32` branch is tested *first*
([L81-93](https://github.com/espressif/esp-dsp/blob/master/modules/iir/include/dsps_biquad.h#L81-L93)),
and `dsps_biquad_f32_ae32_enabled` is set whenever `XCHAL_HAVE_FP && XCHAL_HAVE_LOOPS` — both
true on S3. So on ESP32-S3 `dsps_biquad_f32` resolves to **`dsps_biquad_f32_ae32`**, not
`_aes3`. Call `dsps_biquad_f32_aes3` explicitly if you want that kernel.

**Official benchmark** (CPU cycle counts, ESP32-S3 column, `-O2`), from
[`esp-dsp-benchmarks.rst` L67](https://github.com/espressif/esp-dsp/blob/master/docs/en/esp-dsp-benchmarks.rst#L67)
and the rendered [benchmark page](https://docs.espressif.com/projects/esp-dsp/en/latest/esp-dsp-benchmarks.html):

| Kernel | ESP32 | **ESP32-S3** | ANSI Xtensa | ESP32-S3 `-Os` |
|---|---|---|---|---|
| `dsps_biquad_f32`, 1024 samples | 17 442 | **17 552** | 26 651 | 17 441 |

That is **17.1 cycles/sample per biquad**, so:

| tape_fx stage | Cycles/sample | % of one core @44.1 kHz |
|---|---|---|
| 1 biquad, 1 channel | 17.1 | 0.32 % |
| 3 biquads × 2 channels (6 passes) | ~103 | **~1.9 %** |

What esp-dsp does **not** have: any `tanh`/soft-clip, any fractional-delay/wow-flutter kernel,
and any fast `sin`/`cos`. Those are hand-rolled and carry no published cost.

### Not in the library: an MP3 encoder

Espressif's official codec component, `esp_audio_codec` v2.6.2
([registry](https://components.espressif.com/components/espressif/esp_audio_codec)), ships
encoders for AAC, AMR-NB/WB, OPUS, SBC, ALAC, ADPCM, G722, LC3, G711 and PCM, and for MP3 only
a decoder (`include/decoder/impl/esp_mp3_dec.h`) — there is no MP3 encoder header or source in
the package. Consequently **there is no Espressif-published MP3 *encoding* cost** for any
target, S3 included. The encoder in this repo is third-party shine, vendored
([`components/portable/shine`](../../components/portable/shine)), so the only encode numbers
that exist are this project's own board measurements (quoted at the end of the next section
and in [`README.md`](../../README.md)).

## 4. Real-time audio guidance from ESP-IDF

| Topic | What the primary source says |
|---|---|
| I2S DMA sizing | `dma_buffer_size = dma_frame_num * slot_num * slot_bit_width / 8`, and the buffer must be **≤ 4092 bytes**; interrupt interval is `dma_frame_num / sample_rate`, `dma_desc_num > polling_cycle / interrupt_interval` ([`i2s.rst` L1103-1129](https://github.com/espressif/esp-idf/blob/v5.4/docs/en/api-reference/peripherals/i2s.rst#L1103-L1129)) |
| Default DMA config | `dma_desc_num = 6`, `dma_frame_num = 240` ([`i2s_common.h` L22-31](https://github.com/espressif/esp-idf/blob/v5.4/components/esp_driver_i2s/include/driver/i2s_common.h#L22-L31)) → at 16-bit stereo, 240 frames × 2 slots × 16 bit = 960 B per DMA buffer, 5.76 KB in flight |
| Float in the I2S callback | explicitly forbidden: "it is an interrupt callback, so do not add complex logic, **run floating operation**, or call non-reentrant functions in the callback" ([`i2s.rst` L265](https://github.com/espressif/esp-idf/blob/v5.4/docs/en/api-reference/peripherals/i2s.rst#L265)) |
| Float in ISRs generally | off by default: `CONFIG_FREERTOS_FPU_IN_ISR` "Use float in Level 1 ISR", default `n`, and even when enabled, "usage of float types in higher level interrupts is still not permitted" ([`Kconfig` L464-470](https://github.com/espressif/esp-idf/blob/v5.4/components/freertos/Kconfig#L464-L470)) |
| Where float DSP belongs | a task, not an ISR: the port keeps coprocessor (FPU) state per task and even pins unpinned tasks that use the FPU to a core ([`port.c` L66](https://github.com/espressif/esp-idf/blob/v5.4/components/freertos/FreeRTOS-Kernel/portable/xtensa/port.c#L66)) |
| IRAM placement | "Move frequently executed code to IRAM. By default, all code in the app is executed from flash cache … Functions which are copied into IRAM … always execute at full speed", with the caveat that "IRAM is a limited resource, and using more IRAM may reduce available DRAM" ([`speed.rst` L152-154](https://github.com/espressif/esp-idf/blob/v5.4/docs/en/api-guides/performance/speed.rst#L152-L154)) |
| Cache misses | microbenchmarks under 1-2 ms vary because "binary layout can cause different patterns of cache misses"; executing the function repeatedly or moving it to IRAM mitigates this ([`speed.rst` L61](https://github.com/espressif/esp-idf/blob/v5.4/docs/en/api-guides/performance/speed.rst#L61)) |
| PSRAM | when the flash cache is disabled, external RAM is inaccessible; DMA descriptors cannot live in PSRAM; for chunks "> 32 KB … the cache can be insufficient, and speeds will fall back to the access speed of the external RAM", and such access can "push out" cached flash; DMA bandwidth to PSRAM is "very limited" when the core also accesses it ([`external-ram.rst` L198-212](https://github.com/espressif/esp-idf/blob/v5.4/docs/en/api-guides/external-ram.rst#L198-L212)) |
| FreeRTOS tick | `CONFIG_FREERTOS_HZ` default **100 Hz** (10 ms), range 1-1000 ([`components/freertos/Kconfig` L33-40](https://github.com/espressif/esp-idf/blob/v5.4/components/freertos/Kconfig#L33-L40)) |
| Core pinning | `xTaskCreatePinnedToCore` exists ([FreeRTOS (IDF) docs](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/freertos_idf.html)); the Wi-Fi driver task is pinned to Core 0 by default, and the Speed guide recommends assigning latency-critical interrupts to Core 1 ([`speed.rst`](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-guides/performance/speed.html)) |
| Measuring | `cpu_hal_get_cycle_count()` for short intervals, from an ISR or a pinned task ([`speed.rst` L55](https://github.com/espressif/esp-idf/blob/v5.4/docs/en/api-guides/performance/speed.rst#L55)) |
| Audio examples | [`examples/peripherals/i2s/i2s_std`](https://github.com/espressif/esp-idf/tree/v5.4/examples/peripherals/i2s/i2s_basic/i2s_std) (I2S std TX/RX, 2048-byte buffers) and [`i2s_recorder`](https://github.com/espressif/esp-idf/tree/v5.4/examples/peripherals/i2s/i2s_recorder) (44.1 kHz/16-bit to SD, DMA `datalen=2048, dma_desc_num=64` in its boot log) |

Repo-local context that matters more than any of the above: this firmware already measured
shine at **13 817 µs/frame at 44.1 kHz/256 kbps = 52.9 % of one core**, and IRAM placement of
the encoder alone is worth ~17 % ([`README.md`](../../README.md),
[`components/portable/shine/CMakeLists.txt`](../../components/portable/shine/CMakeLists.txt)).
Measured here ([§6](#6-what-it-costs)), the effect is **10-18 % as written and ~8-10 % after
the two fixes in [§8](#8-what-changed-in-the-component-and-what-is-still-open)** - not the 2-4 %
that the esp-dsp biquad figure alone would suggest, because the biquads are the cheap part.

## 5. Where it goes: the PCM seam, not the wire

The station's audio task is a straight line, and every sample already passes through one
buffer:

```
decode (dr_libs) -> resample (mi_resample) -> chunk[1024 * 2] -> mi_enc_feed -> MP3 frames -> rings -> sockets
                                                 ^
                                                 MINI_ICECAST_CHUNK_FRAMES: 16-bit interleaved, always stereo
```

`emit_chunk()` is what hands that buffer to the encoder, and the buffer is exactly the layout
`tape_fx_process_s16()` takes: in place, interleaved, 16-bit. The integration is one guarded
call in two places - the whole chunk, and the partial one at the end of a track, which
`flush_pipeline()` also sends through the encoder:

```c
static void fx_apply(size_t frames)
{
    if (!S.fx_on) return;                       /* off costs nothing, and the stream is
                                                 * the decoder's output sample for sample */
    int64_t t0 = esp_timer_get_time();
    tape_fx_process_s16(&S.fx, S.chunk, frames, 2);
    S.fx_us += (uint64_t)(esp_timer_get_time() - t0);
    S.fx_frames += frames;
}
```

That is what `components/portable/tape_fx` is: `tape_fx_t` lives in the static `S`, not on the
audio task's stack, and `S.fx_on` comes from `mini_icecast_cfg_t.fx` (0 = off, 1 = on).

**The sound is tunable without reflashing.** Three layers, each optional:

| layer | what it is | when it applies |
|---|---|---|
| `TAPE_FX_DEFAULT_*` in `tape_fx.h` | the factory sound, one line each | compile time |
| `mini_icecast_cfg_t.fx_tune` | a `mini_icecast_fx_t`; zeroed means the factory sound | at start |
| `mini_icecast_set_fx(&tune)` | the same struct, from any task | at the next 1024-frame chunk (~23 ms) |
| `GET /fx` | a page with the five values, and `GET /fx?type=..&dolby=..&tape_q=..&head_q=..&motor_q=..` to set them | immediately, and saved |

The runtime path is a lock-free handoff: the caller writes the snapshot and bumps a
generation counter with release semantics, and the audio task reads it with acquire at the top
of `fx_apply()` and applies all five values at once. No partial retune, no lock on the audio
task, and the encoder never restarts, so a listener hears the knob move rather than a gap.
`set = false` keeps its meaning ("use the defaults") at every layer, which is what lets `0.0`
stay a *worn* setting instead of "unset".

**The page is the control surface.** `/fx` serves a form with the current values and takes them
back as a query string (`303 See Other` on save, which is why the HTTP server had to start
*keeping* the query instead of dropping it). Saving writes five keys to NVS under `tapefx`;
`mini_icecast_start()` reads them back and applies them over the compiled defaults, so the
sound survives a reboot. Verified on the board: set the five values, reset, read them back
unchanged. The store keeps the keys explicit rather than one struct blob, so the layout of a C
struct never becomes a compatibility promise between builds. With the
delay line at `TAPE_FX_DLY_LEN 512` the state is **4 292 B** in the built object, plus 1 028 B
of sine table in `.bss` and 2 819 B of code. The encoder already takes ~100 KB of internal RAM,
so this is a rounding error next to it.

**Why not on the wire.** Effecting the encoded bytes means decoding them back to PCM and
encoding them again - a decode pass plus a full second encode pass (the 52.9 % in the
verdict), for the same audible result. The PCM seam costs one pass over samples that are
already in RAM.

**No added latency.** The delay line is 4 ms (`center = 0.004 * sr` = 176 samples at 44.1 kHz)
and it sits inside a pipeline that already runs 1 500 ms ahead of real time (`lead_ms`
default). Nothing in the stream's timing changes.

**Verified end to end on the host** (`./host/build/bin/icecast_host --fx`, 6 s of `/stream`
captured and decoded): both captures are valid 44.1 kHz stereo MP3, and they have the same RMS,
-20.1 dBFS, with and without the effect - the unity-gain fix holding through the whole chain.
The spectral balance is the effect working: 1-5 kHz is 7.7 dB *up* (saturation and the head
bump), 5-10 kHz 7.9 dB up and 10-20 kHz 10.5 dB up - the last two because the 6.5 kHz low-pass
removes the music there and the hiss fills the gap, which is what tape does.

## 6. What it costs

Neither a host clock nor an instruction count is trustworthy alone: the host says nothing
about a 240 MHz single-issue core, and instruction counts say nothing about stalls. Both were
measured; the reproduction is `tools/bench/tape-fx/` (`make`, `make opt`, `make probe`,
`make asm`).

### 6.1 Host, per 1024-frame stereo chunk (MEASURED)

| variant | ns per 1024-frame chunk | ns per frame |
|---|---|---|
| as written | 19 065 | **18.6** |
| two `sinf()` -> 256-entry table | 14 209 | **13.9** |

The host can only see the `sinf` win, because on the host a float division is one
instruction. On the ESP32-S3 it is not.

### 6.2 ESP32-S3: static count from the real toolchain (MEASURED, then ESTIMATE)

Compiled with the project's own flags (`@toolchain/cflags` from `build_sunton/`), `-O2`, and
disassembled. Per stereo frame the compiler emits **432 instructions** before any call, and
the function makes **six calls per frame**:

| call | per frame | what it actually is |
|---|---|---|
| `sinf` | 2 | `_sinf` -> `__rem_pio2f` -> `__kernel_sinf`: ~100 instructions, 3 stack frames, all single precision |
| `__divsf3` | 4 | the two `fast_tanh()` calls per channel are **rational**, so each one divides |

| variant | instructions per frame | calls per frame | ESTIMATE cycles | ESTIMATE % of one 240 MHz core | null test vs original |
|---|---|---|---|---|---|
| as written, `-O2` | 432 + ~290 | 2 `sinf` + 4 `__divsf3` | ~950 | **~17 %** | reference |
| `sinf` -> table | 433 + ~92 | 4 `__divsf3` | ~800 | ~15 % | -90 to -99 dBFS |
| table + cubic saturator | 365 | **0** | ~430 | ~8 % | **-21 to -63 dBFS (audible)** |
| table + fitted polynomial | **408** | **0** | ~490 | **~9 %** (11.0 % measured, §6.7) | -75 to -92 dBFS |

The last column is a null test: the variant's output minus the original's, in dBFS, across
input levels from -3 to -20 dBFS (`make compare`). The effect's own hiss sits at -66 dBFS, so
the first, second and fourth rows are below the noise the effect adds and the third is not -
which is why the cubic was rejected in [§6.4](#64-the-two-fixes-in-code).

`-O3` folds the channel loop into the frame loop and lands at 389 instructions per frame;
`-Og` (the project default) breaks `bq_run()` out of line - six calls per frame - and lands
back where `-O2` started. Unlike shine, this file is not rescued by an optimisation flag: the
cost is in the calls, not in the loop code.

For comparison, Espressif's hand-written biquad kernel (`dsps_biquad_f32`, §3) is
17.1 cycles/sample; the six biquad passes this effect needs would be ~103 cycles/sample, 1.9 %
of a core, if they ran at esp-dsp's density. They do not: the GCC-built chain is ~2-3x that,
and it is still the cheapest part of the loop.

### 6.3 The cross-check, and why the two numbers disagree (ESTIMATE)

`tape_fx` is 5.8x cheaper than the encoder on the host (18.6 ns x 1 152 frames = 21 us,
against `mi_bench`'s 125 us per MP3 frame). The encoder costs 2 917 cycles per stereo frame on
the board (13.8 ms per 1152-frame frame, measured in `components/app/appliance/appliance.c`).
2 917 / 5.8 = **~500 cycles, 9 %**.

That is a *lower* bound, because shine is far more memory-bound than a biquad chain, so the
host-to-board penalty is larger for shine (110x on this host) than for the effect.

**Budget 10-18 % of one 240 MHz core as written, ~8-10 % after the two fixes.** The
documented 22050 Hz fallback (23.6 % for the encoder) would still leave ~30 % headroom if the
effect ever proves too expensive at 44.1 kHz.

### 6.4 The two fixes, in code

**The saturator: replace the division, not the curve.** The first attempt was a cubic soft clip
(`1.5 * (u - u^3/3)`, `u = 2x/3`). It is monotonic, has unity slope at the origin and costs five
instructions - and it is *audible*: its third-order term is -4/27 where the rational's is -1/3,
so above about -20 dBFS the harmonic content changes, and the null test against the original
reads -25 dBFS at -6 dBFS input, i.e. 40 dB **above** the effect's own hiss. "Same RMS at
-20 dBFS" is not the same thing as "same sound"; the RMS matched to 0.05 dB and the waveform did
not.

The curve itself is what has to survive, so fit the original function instead. It is a rational
whose only expensive part is the division, and on [-2, 2] a degree-13 odd polynomial (degree 6
in `t = x^2`) matches it to 2.7e-5, -92 dB, monotonic, with the float32 evaluation adding
nothing measurable:

```c
/* x*(27+x^2)/(27+9x^2), fitted: degree 13 in x (6 in t = x^2) on [-2, 2],
 * max error 2.7e-5 (-92 dB), monotonic. Beyond +-2 the rational is flat
 * (0.984 at 2, 1.000 at 3), so the tails clamp to its value at 2. */
static inline float fast_tanh(float x) {
    if (x > 2.f) return 0.984153f;
    if (x < -2.f) return -0.984153f;
    float t = x * x;
    return x * (0.99996933015f + t * (-0.29553466172f
        + t * (0.095505908738f + t * (-0.027326835159f
        + t * (0.0058886109434f + t * (-0.00078208114092f
        + t * 0.000046004772995f))))));
}
```

That is eight FP instructions where the division was a 23-instruction libgcc call, and the null
test against the original reads -75 to -92 dBFS: 9 to 26 dB *below* the hiss the effect itself
adds, at every input level tested. The one deliberate difference is the tail: the clamp at +-2
loses the last 1.6 % of the curve, which only appears at `drive = 3` with a full-scale input.

**The LFO: a table, with the wrap done in the right order.** The two oscillators move at 0.9 Hz
and 8 Hz, so a 256-entry table (1 KB) with linear interpolation reproduces the modulation to
-83 dBFS, at ~10 cycles instead of ~130. Two traps:

- the phase is advanced and wrapped with `if (ph > 2*pi) ph -= 2*pi`, so it can land on exactly
  `2*pi`, where the table index is 256. Reading `tab[257]` is a global-buffer-overflow (ASan
  catches it, `make compare`'s `corner` case): mask the index, and mask it **after** taking the
  fractional part, or the fraction becomes 256 and the interpolation extrapolates the table into
  nonsense;
- the table is `static float` (RAM, 1 KB) rather than `const` (flash) on purpose: it is read
  twice per frame and a DRAM read has no cache to miss.

Both fixes together: **408 instructions per frame, no calls, ~9 % of a core**, against 432
instructions *plus* six library calls (~950 cycles, ~17 %) as written.

### 6.5 The budget in one table

Everything the station would spend, at 44.1 kHz stereo on one 240 MHz core. The encoder row is
the board measurement in `components/app/appliance/appliance.c`; the effect rows are the
instruction counts above converted at ~1.2 cycles per instruction, so read them as +-30 %.

| block | cycles per frame | ms per 1024-frame chunk (23.2 ms of audio) | % of one core | seconds of CPU per minute of audio | vs the encoder |
|---|---|---|---|---|---|
| MP3 encoder, 44.1k/256 | 2 880 | 12.3 | **52.9 %** | 31.8 | 1.00x |
| tape_fx as written | ~950 | 4.1 | **17.5 %** | 10.5 | 0.33x |
| tape_fx, `sinf` -> table | ~800 | 3.4 | 14.7 % | 8.8 | 0.28x |
| tape_fx, table + fitted polynomial | ~490 | 2.1 | **9.0 %** (11.0 % measured, §6.7) | 5.4 | 0.17x |
| tape_fx, table + cubic | ~430 | 1.8 | 7.9 % | 4.7 | 0.15x |

What that means for the whole broadcast:

| scenario | encoder | effect | total | headroom on one core |
|---|---|---|---|---|
| 44.1 kHz, synthetic, as written | 52.9 % | 17.5 % | 70.4 % | 29.6 % |
| 44.1 kHz, synthetic, polynomial | 52.9 % | 9.0 % | **61.9 %** | 38.1 % |
| 44.1 kHz, tonal corpus, polynomial | ~40 % | 9.0 % | 49.0 % | 51.0 % |
| 22.05 kHz fallback, polynomial | 23.6 % | 4.5 % | 28.1 % | 71.9 % |

Three things this table says that a single number does not:

- **The effect is a sixth of the encoder** with the polynomial, a third as written. It is not
  the thing to optimise if the station ever runs out of headroom - but at 70 % as written it is
  the difference between "comfortable" and "one bad track away from starving".
- **The cost is per stream, not per listener.** It runs before the encoder, so four clients
  cost exactly the same as one. Fan-out is the encoder's problem, not the effect's.
- **Halving the station rate halves the effect**, because it is per sample. At the documented
  22.05 kHz fallback the effect is 4.5 %, not 17.5 %.

Memory, for completeness, as built: 4 292 B of state (4 096 of it the delay line), 1 028 B of
sine table in `.bss`, 2 819 B of code - 8.1 KB, against the encoder's ~100 KB.

### 6.6 The two operating points

The station's two configurations, both at 44.1 kHz / 256 kbps, both on core 1 (where the
broadcast task is pinned; Wi-Fi and the screen own core 0):

| | **normal** | **FX** |
|---|---|---|
| path | decode -> resample -> encode | decode -> resample -> **tape_fx** -> encode |
| core 1, synthetic material | 52.9 % (measured) | 61.9 % (52.9 + 9.0 estimated) |
| core 1, on the board (§6.7) | **60.6 %** | **70.8 %** (60.6 + 11.0 measured) |
| headroom on core 1 | 47.1 % | 38.1 % |
| latency | 0 | +4 ms, inside the 1500 ms `lead_ms` |
| loudness | reference | **+3.0 dB today** - see below |
| extra state | none | 8.4 KB + 1 KB table + ~1.2 KB code |

**They cannot run at the same time.** Two encoders on core 1 would be 52.9 + 61.9 = 114.8 % of
it, and the other core is Wi-Fi plus LVGL (`broadcast_task` is pinned to core 1 in
`mini_icecast.c:719`, the screen task to core 0 in `screen_display.c:302`). So "FX" is a mode of
the one stream, not a second endpoint: switching it does not restart the encoder and listeners
do not reconnect.

Two things have to be true before that switch is usable:

1. **Loudness must match.** The effect as written was +3.0 dB louder than its input at the
   defaults, and between +0.8 and +4.6 dB across the knobs, so toggling normal/FX would have
   been a volume jump rather than a change of character. The make-up gain is fixed in the
   component (§8) and the two modes now measure the same RMS.
2. **The switch must not click.** Enabling inserts a 4 ms delay whose line holds whatever was
   there before, so the first samples come from stale audio. Crossfade dry->wet over ~20 ms
   instead of jumping.

### 6.7 Measured on the board

Flashed, two runs each way (`build_sunton`, board `192.168.0.44`), same track, 44.1 kHz /
256 kbps, the telemetry line at 60 s and 120 s of a soak:

Four runs in total, the last one on the image now on the board:

| configuration | encoder | effect | total on core 1 | starvation rebases |
|---|---|---|---|---|
| `.fx = 0` (normal) | 60.6 % | - | **60.6 %** | 0 |
| `.fx = 1` (FX) | 59.5–60.0 % | **10.9–11.2 %** | **70.4–70.8 %** | 0 |

Three things to read out of it:

- **The effect is 11.0 %, not the 9.0 % the instruction count predicted.** 408 instructions per
  frame at ~590–600 cycles is ~1.45 cycles per instruction, not the 1.2 assumed; the estimate was
  inside its stated ±30 % band, on the pessimistic side. The measured cost is 2 548 us per
  1024-frame chunk, i.e. 2.49 us per frame.
- **The encoder's cost does not move when its input is saturated and band-limited** - 60.6 %
  without the effect against 59.5–60.0 % with it, which is the track moving on, not an effect. So the two costs add, and the
  FX mode is exactly normal plus the effect. (This also answers open question 3 below.)
- **Nothing starved**: `starvation rebases 0` across both soaks, and the encoder never fell
  behind. 70.8 % of core 1 leaves ~29 % for everything else that core does.

The encoder's 60.6 % here is higher than the 52.9 % the appliance's comment records, because
that figure was measured on synthetic full-scale noise and this is a dense live recording. The
effect's number is the one that matters for sizing, and it is material-independent: it is a
fixed number of instructions per sample.

## 7. What it does to the sound (MEASURED)

### 7.1 The transfer function of each type

Pink noise at -18 dBFS through the shipped component (`.scratch/fx-audible/`), measured
spectrum of the output over the measured spectrum of the input, normalised to 1 kHz. These are
the numbers with `dolby = K`, so the hiss is out of the way and what is left is the filter:

| Hz | 40 | 63 | 80 | 110 | 160 | 250 | 500 | 1k | 2k | 4k | 6k | 8k | 10k | 12k | 14k | 16k |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| NORMAL | +3.8 | **+5.5** | +5.3 | +4.3 | +2.9 | +2.2 | +1.8 | 0 | **-3.8** | +0.1 | -1.0 | -4.5 | -9.0 | **-15.5** | -20.9 | -27.7 |
| CHROME | -0.6 | +1.3 | +1.3 | +0.9 | +0.2 | -0.1 | 0.0 | 0 | -0.3 | +0.2 | -0.7 | -1.4 | -3.2 | -8.3 | -13.3 | -20.1 |
| METAL | -3.0 | -1.0 | -0.8 | -0.9 | -1.2 | -1.3 | -0.9 | 0 | **+1.4** | 0.0 | -1.5 | -1.9 | -1.9 | -4.0 | -5.7 | -10.4 |

The shape is one broad presence bell at 2 kHz (-6 dB / 0 / +3 dB), one low shelf at 110 Hz
(4.0 / 2.0 / 0.8 dB) and one low-pass (7 / 11 / 16 kHz corner at `head_q = 1`): NORMAL is a
worn ferric - fat bottom, scooped middle, dull top - and METAL is open and forward.

1. **Unity, at last.** The draft was +3.0 dB hot and moved between +0.8 and +4.6 dB with the
   knobs. The component is -0.1 dB at 1 kHz, and the encoded stream measures the same RMS with
   the effect on and off (-20.1 dBFS both ways, §5).
2. **The first cut of these tables was inaudible, and that is the interesting part.** With
   `lp_base` at 9/12/15 kHz and a 2.5/1.5/1.0 dB bump, every type sat within 1.5 dB of every
   other below 8 kHz and the three differed by at most 5 dB at 10 kHz. On a cassette that is
   plausible; on real music, which is already limited at both ends, it is nothing. The tables
   are now 7/11/16 kHz and 4.0/2.0/0.8 dB: NORMAL is 10 dB down at 10 kHz where METAL is flat,
   and the two differ by ~3 dB at 63 Hz. A difference you have to A/B is not a difference.
   The second pass went further and moved the character into the middle, where every system and
   most ears live: NORMAL is 5 dB down at 2 kHz and 6 dB up at 63 Hz against METAL. Everything
   above 8 kHz is now the icing rather than the cake.
3. **Measured end to end, through the board.** Two 10 s captures of the same broadcast, decoded
   and compared band by band against a dry capture (levels relative to the 1.3-2.6 kHz band, so
   the track's own tilt cancels):

   | capture | 8-11 kHz | 11-14 kHz | 14-18 kHz |
   |---|---|---|---|
   | dry | -3.0 dB | -5.7 dB | -7.3 dB |
   | NORMAL | **-14.8 dB** | **-25.2 dB** | -35.7 dB |
   | METAL | -1.7 dB | -4.4 dB | -10.0 dB |

   So the setting reaches the running effect, survives the encoder, the Wi-Fi link and the
   client's decoder, and is 10 dB wide in the band where cymbals live.
4. **"Dolby" is a noise-floor knob, and it was set too low to be one.** Silence in, RMS out:
   -55.6 dBFS (off), -65.8 (B), -76.2 (C), and nothing (K). Those are 10 dB steps, and they
   only move hiss: the hiss used to sit at -66 dBFS, below the noise of the room any station
   plays in, so turning Dolby on and off changed nothing anyone could hear. The base is now
   -54 instead of -64. It is still not Dolby B's high-band compander: it subtracts from the
   noise, it does not compress and expand the top end, and on loud material what it changes is
   what it is supposed to change - the noise, not the music.

## 8. What changed in the component, and what is still open

`components/portable/tape_fx` is the version that shipped, with the first three fixed:

| # | defect | status |
|---|---|---|
| 1 | make-up gain was `1/sqrt(drive)`, up to +4.8 dB hot | **fixed**: `1/drive`, unity for a quiet signal. Verified end to end: -20.1 dBFS in, -20.1 dBFS out of the encoded stream with the effect on |
| 2 | six library calls per frame (2 `sinf` + 4 `__divsf3`), more than half the cost | **fixed**: sine table + fitted polynomial. The built object's `tape_fx_process_s16` makes **zero** calls (checked with `objdump` on `build_sunton/`) |
| 3 | `-Og` would break the biquads out of line | **fixed**: the component compiles its one source at `-O2`, like `mini_icecast/` |
| 4 | aliasing: the saturators have no band-limit after them | **measured, and not fixed on purpose**. A 5 kHz tone at -6 dBFS folds its 5th harmonic to 19.1 kHz at -78 dB; at 0 dBFS its 7th folds to 9.1 kHz at -40 dB; the worst case found is an 8 kHz tone at -6 dBFS, whose 5th harmonic folds to 4.1 kHz at -30 dB. At the defaults those sit at or below the hiss the effect adds (-66 dBFS); they surface only with an isolated, loud, high-frequency tone. 2x oversampling the input saturator would cost about what the whole effect costs (11 %, taking the station to ~82 % of core 1) to bury something already under the noise floor. Revisit if cymbals ever sound gritty at high drive |
| 5 | hiss and wow are baked into the stream for every listener | **by design**. The VU on the screen now reads the processed level, because `measure_level()` runs after the effect |
| 6 | denormals in the biquad states on silence | **open**. The host bench showed no slowdown (`./bench 20 2 1`); the board test is in §10 |
| 7 | the delay line was twice the size it needed to be | **fixed**: `TAPE_FX_DLY_LEN 512` (4 KB). The worst case is 285 samples at 44.1 kHz and 311 at 48 kHz; 256 would clip at maximum wear |

The default head quality moved from 0.6 to 0.85 (`TAPE_FX_DEFAULT_HEAD_Q`). The type tables in
`recompute()` were widened after the first build turned out to be inaudible (§7.2), and the hiss
base went from -64 to -54 for the same reason (§7.4). The remaining choice is worth naming
because it is a sound decision rather than a defect: "Dolby" is a noise-floor knob rather than
Dolby B's high-band compander.

## 9. Open questions to measure on the board

Three of the questions this note set out with are answered above: what the compiler emits for
`x / y` ([§2.4](#24-why-the-division-matters-and-why-no-flag-fixes-it-verified-locally)), the
cost of a `sinf()` call and whether it is per-sample ([§6.2](#62-esp32-s3-static-count-from-the-real-toolchain-measured-then-estimate)),
and where the delay line lives (8 KB of internal RAM, not 4 KB, and not DMA memory). What
remains:

1. ~~**Cycles/sample for the whole chain**~~ - answered by §6.7: 597 cycles per frame, 11.0 %
   of one core, measured with `esp_timer_get_time()` around the chunk. IRAM was not tried: at
   11 % of a core, the ~17 % that IRAM buys shine does not justify the space.
2. **End-to-end headroom**: encoder + effect + ICY/TCP still leaves margin at 44.1 kHz/256?
   The station already tracks lag and underruns (`mini_icecast_lag_ms`), so this is a soak,
   not a new instrument.
3. ~~**Encoder cost with effect-shaped input**~~ - answered by §6.7: 60.6 % without the effect,
   59.8-60.0 % with it. Shine does not care, so the two costs add.
4. **Int16 ↔ float conversion versus Q15.** The Speed guide says integer is faster even with
   an FPU, and the effect converts 2048 samples per chunk each way. Worth one experiment
   before rewriting anything.
5. **IRAM budget.** The effect's code is ~1.2 KB at `-O2` (~2 KB with a table); shine's IRAM
   placement is already worth ~17 % of the encoder. Check `.iram0` headroom before copying it.

## 10. Pinning the number on the board

The firmware already has the pattern: `mi_enc_encode_us()` + `esp_timer_get_time()` + a
periodic log line. Six lines around the call give the real figure:

```c
int64_t t0 = esp_timer_get_time();
tape_fx_process_s16(&S.fx, S.chunk, CHUNK_FRAMES, 2);
S.fx_us += (uint64_t)(esp_timer_get_time() - t0);
S.fx_frames += CHUNK_FRAMES;

ESP_LOGI(TAG, "tape_fx: %.2f us/chunk = %.1f%% of one core",
         S.fx_us / (S.fx_frames / (double)CHUNK_FRAMES),
         100.0 * S.fx_us / (S.fx_frames * 1e6 / 44100.0));
```

That is the number that decides it, the same way the encoder's 13.8 ms per frame decided the
station format. An effect chunk above ~11 ms (50 % of one core) would mean it had eaten the
headroom the broadcast needs.

