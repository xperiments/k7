# Why the 800x480 RGB panel reports 15 FPS, and which lever moves it — verified from primary sources (2026-10-17)

Axis: **the board draws an 800x480 RGB565 panel through `esp_lvgl_port` in direct mode with
`avoid_tearing`, and LVGL's own performance monitor reports 15 FPS. This note establishes what that
number counts, where a frame's time actually goes, and which of the candidate levers can change it —
each traced to the line of source that decides it.**

Everything below is read out of artefacts on this machine: this repository's own configuration and
the LVGL 9.6.0 / `esp_lvgl_port` 2.9.0 sources vendored into `managed_components/`, the ESP-IDF
v6.1 tree at `/Users/pedrocasaubon/esp/esp-idf-6.1`, and two reference firmwares for the same board
(`~/Documents/SolidGL`, `~/Documents/Sunton5_IDF`). No `idf.py` was run and nothing was flashed
while writing it.

Four kinds of number appear here and they are never mixed:

- **MEASURED (board)** — printed by the firmware's own telemetry on the board. This note contains
  exactly one: the 15 FPS on LVGL's sysmon label, reported by the owner of the board on 2026-10-17
  and **not re-run by me**. It is treated as an observation to be explained, not as a result I
  reproduce.
- **MEASURED (link)** — read out of an artefact on this machine by me, with the artefact named:
  `build_sunton/sd_stream_server.map` (the Sunton build; `build_sunton/CMakeCache.txt` records
  `SDKCONFIG_DEFAULTS:STRING=sdkconfig.defaults;sdkconfig.defaults.sunton` and
  `build_sunton/config/sdkconfig.h` records `CONFIG_APP_BOARD_SUNTON_8048S050C 1`), and
  `components/app/screen_display/assets/hub_frames.bin`.
- **DOCUMENTED** — a Kconfig `help` string, a `.rst` page, a header comment, or a line of source,
  always cited by file and line.
- **ARITHMETIC** — a value computed here from DOCUMENTED inputs that are cited next to it. The
  citation covers the inputs; the multiplication is mine.

Two files this note cites are rewritten by every build: `sdkconfig.sunton` and
`build_sunton/config/sdkconfig.h`. They are cited by the `CONFIG_…` symbol they carry, not by line
number, because a rebuild moves those numbers; the hand-written sources are cited by `file:line`.

## Bottom line

1. **15 FPS is not a rendering speed.** It is the rate at which LVGL's refresh timer completes cycles,
   and that timer is hard-capped at 30 by `LV_DEF_REFR_PERIOD = 33` (§1). The second line of the same
   on-screen label already carries the split that matters: `total ms (render | flush)`.
2. **A 15 means a 62.5–66.7 ms cycle.** That sits on neither the 33 ms timer grid nor the 25.625 ms
   panel-frame grid (§2, §3), so something outside the renderer is stretching the cycle. The strongest
   candidate in the source is preemption: the refresh task runs at priority 4 on core 0, under Wi-Fi
   (pinned there) and under lwIP at priority 18 (§5, "Task placement").
3. **Three one-line experiments, in this order, one at a time:** `.task_affinity = 1` in `k_port`
   (priority unchanged); `lv_timer_set_period(lv_display_get_refr_timer(disp), 16)` after the port
   creates the display; and `CONFIG_COMPILER_OPTIMIZATION_PERF=y` (the build is `-Og`). Read the split
   before and after each. **Tried together on 2026-10-17 and reported worse; reverted the same day**
   (§10, "Result"). The combination is not evidence about any single lever, and the split was not read
   first — the next attempt changes one thing against the 33 ms / `-Og` baseline.
4. **If `flush` dominates,** the lever is the panel frame: raise `pclk` (16 → 20/24 MHz) or leave
   `avoid_tearing` for a partial arrangement (§4, §10 lever 10). If `render` dominates, the lever is
   what the UI invalidates each cycle — today, the scrolling title (§6).
5. **Two dead ends, so they are not tried twice:** `CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM` cannot
   change an object file in LVGL 9.6, and `CONFIG_LV_DRAW_SW_COMPLEX=n` deletes every rounded fill
   (§5, §6).

## Baseline (2026-10-17): 15 FPS

The configuration every lever in this note is measured against. It is the tree as it stands after both
failed attempts were reverted the same day (§10, "Result"); none of the levers below is in it.

| Item | Value | Source |
|---|---|---|
| Toolchain | LVGL 9.6.0, `esp_lvgl_port` 2.9.0, ESP-IDF 6.1, ESP32-S3 at 240 MHz | `dependencies.lock`; `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240` in `sdkconfig.sunton` |
| Panel | 800x480 RGB565, pclk **16 MHz** → 25.625 ms per frame, 39.02 fps | `components/app/screen_display/panel_rgb.c:31`, §2 |
| Framebuffers | **2**, in PSRAM, bounce buffer **0** | `components/app/screen_display/panel_rgb.c:73-74`, `:80` |
| LVGL render path | **direct mode**, `avoid_tearing`, no `bb_mode` | `components/app/screen_display/panel_rgb.c:119`, `:122` |
| Refresh task | **core 0**, priority 4, 16 KB stack, 500 ms sleep cap | `components/app/screen_display/panel_rgb.c:46-51` |
| Refresh period | **33 ms** (`LV_DEF_REFR_PERIOD`), a 30 fps cap on the reported number | `CONFIG_LV_DEF_REFR_PERIOD` in `build_sunton/config/sdkconfig.h`, §1 |
| Compiler | **`-Og`** (`CONFIG_COMPILER_OPTIMIZATION_DEBUG`) | `build_sunton/config/sdkconfig.h` |
| Memory clocks | octal PSRAM 80 MHz (`CONFIG_SPIRAM_MODE_OCT`, `CONFIG_SPIRAM_SPEED_80M`), flash DIO 80 MHz | `build_sunton/config/sdkconfig.h` |
| Monitor | LVGL's own perf label, top right, 300 ms report (`CONFIG_LV_USE_PERF_MONITOR`) | `build_sunton/config/sdkconfig.h`, §1 |
| Content | the cassette: two 96x96 hub crops at 100 ms, scrolling title, static packs | `components/app/screen_display/cassette_view.c` |
| Observation | **15 FPS** on the perf label, reported 2026-10-17 | MEASURED (board), §1 |
| Not yet recorded | the `render \| flush` split on the label's second line | the first reading any change should carry |

A change counts against this baseline only if it is the **single** variable changed, with the split read
before and after. The two attempts made so far did not meet that rule, which is why neither is
attributable: the first changed three variables at once, the second two.

---

## 1. What LVGL 9.6's "FPS" counts: refresh cycles, not draws

`lv_sysmon.c` builds the label and computes the number in `perf_dump_info()`:

```c
info->calculated.fps = time_since_last_report ? (1000 * info->measured.refr_cnt / time_since_last_report) : 0;
info->calculated.fps = LV_MIN(info->calculated.fps,
                              1000 / disp_refr_period);   /*Limit due to possible off-by-one error*/
```

(`managed_components/lvgl__lvgl/src/debugging/sysmon/lv_sysmon.c:303-305`.) Three separate facts are
packed into those two lines.

**`refr_cnt` counts completed refresh cycles, including cycles that drew nothing.** It is
incremented on `LV_EVENT_REFR_READY` (`lv_sysmon.c:252-255`). That event is sent at the end of
`lv_display_refr_timer()` **after** the early exit for an empty invalidation list:

```c
    if(disp_refr->inv_p == 0) goto refr_finish;
...
refr_finish:

#if LV_DRAW_SW_COMPLEX == 1
    lv_draw_sw_mask_cleanup();
#endif

    lv_display_send_event(disp_refr, LV_EVENT_REFR_READY, NULL);
```

(`src/core/lv_refr.c:433`, `:452-458`.) The event's own documentation says so twice:
`LV_EVENT_REFR_START` is "Sent even if there is nothing to redraw" and `LV_EVENT_REFR_READY` is
"Sent even if no redraw happened" (`include/lvgl/core/lv_event.h:113-114`). `refr_cnt` is therefore
a count of refresh **ticks**, not of frames that rendered.

**The tick keeps coming even when nothing changed.** `lv_display_refr_timer()` normally pauses its
own timer to save CPU, but not when the performance monitor is on:

```c
#if !LV_USE_PERF_MONITOR
    if(timer) lv_timer_pause(timer);
#endif
```

(`src/core/lv_refr.c:394-397`; the comment above it says "However if the performance monitor is
enabled keep the timer running to count the FPS".) In this build `CONFIG_LV_USE_PERF_MONITOR=y`
(`sdkconfig.sunton`), so the refresh timer is never paused and an idle LVGL still completes a
cycle every period.

**The period is `LV_DEF_REFR_PERIOD`, 33 ms here, and that caps the display at 30.** The refresh
timer is created with `LV_DEF_REFR_PERIOD` (`src/display/lv_display.c:125`), the Kconfig describes it
as "Default display refresh, input device read and animation step period" with default 33
(`managed_components/lvgl__lvgl/Kconfig:353-357`), and the build has 33 (`sdkconfig.sunton`).
The sysmon cap reads the period back off the timer
(`lv_sysmon.c:300-301`) and applies `LV_MIN(fps, 1000/disp_refr_period)`, i.e. **30** — integer
division of 1000 by 33. The label can never print 31 in this configuration, whatever the renderer
does.

The report interval, and therefore the quantisation of the displayed number, is the sysmon timer:
`LV_SYSMON_REFR_PERIOD_DEF` = 300 ms (`lv_sysmon.c:34-36`), created as a normal `lv_timer`
(`lv_sysmon.c:145`), and `time_since_last_report = lv_tick_elaps(info->measured.last_report_timestamp)`
(`lv_sysmon.c:299`). So the displayed figure is `1000 × (cycles completed in ≈300 ms) / (measured
interval)`, truncated to an integer (`lv_sysmon.c:303`). **A displayed 15 therefore pins the mean
cycle to 62.5–66.7 ms**: `fps` is the floor of `1000 × refr_cnt / elapsed`, so 15 means
`62.5 < elapsed / refr_cnt ≤ 66.7` ms, independently of how long the report window actually ran
(ARITHMETIC from `lv_sysmon.c:303`).

What bounds the cycle is the timer, not the renderer: `lv_timer_exec()` stamps `last_run` at the
*start* of the callback (`src/misc/lv_timer.c:350`) and re-arms only once `elapsed >= period`
(`lv_timer.c:393-396`), so a cycle that takes `c` ms repeats every `max(33, c)` ms. The board's
handler is `lvgl_port_task()` calling `lv_timer_handler()` and sleeping for the returned time
(`managed_components/espressif__esp_lvgl_port/src/lvgl9/esp_lvgl_port.c:242`, `:220-221`), so nothing
downstream shortens it.

The label carries the split that matters, on its second line:

```c
    lv_label_set_text_fmt(
        label,
        "%" LV_PRIu32" FPS, %" LV_PRIu32 "%% CPU\n"
        "%" LV_PRIu32" ms (%" LV_PRIu32" | %" LV_PRIu32")",
        perf->calculated.fps, perf->calculated.cpu,
        perf->calculated.render_avg_time + perf->calculated.flush_avg_time,
        perf->calculated.render_avg_time, perf->calculated.flush_avg_time
    );
```

(`lv_sysmon.c:390-397`.) That is **total ms (render ms | flush ms)**, averaged over the cycles that
rendered (`lv_sysmon.c:311-320`). This is the measurement that separates a draw-cost problem from a
vsync-wait problem (§9), and it exists on the running board already.

Two properties of the same overlay are worth knowing before trusting it:

- **"CPU%" is not the chip's CPU.** With `CONFIG_LV_OS_NONE=y` and `CONFIG_LV_USE_OS=0` in
  `sdkconfig.sunton`, `LV_SYSMON_GET_IDLE` resolves to `lv_os_get_idle_percent()`,
  which in the no-OS backend returns `lv_timer_get_idle()`
  (`src/osal/lv_os_none.c:40-43`) — the duty cycle of LVGL's own timer handler. It says nothing
  about the encoder, Wi-Fi or the flush.
- **A per-cycle log is available instead of the label.** `CONFIG_LV_USE_PERF_MONITOR_LOG_MODE`
  (`Kconfig:3247-3252`) switches the same numbers to `LV_LOG` and prints `refr_cnt` and
  `render_cnt` separately (`lv_sysmon.c:358-375`), and hides the label
  (`lv_sysmon.c:149`). That is the only way to see, in one line, how many cycles completed versus
  how many actually drew.

**Verdict for §1.** 15 FPS is an invalidation-and-vsync cadence number, not a rendering-speed number:
the counter runs whether or not anything was drawn, its ceiling here is 30 by construction, and 15
means a cycle of 62.5–66.7 ms. Whether those 63 ms are LVGL drawing or the flush waiting on the panel is
not in the FPS figure — it is in the second line of the same label.

---

## 2. The panel's own frame: 25.625 ms at 16 MHz, 39.02 fps

The timing diagram in the driver header defines the two totals:

```
 *                                                 Total Width
 *                             <--------------------------------------------------->
 *                       HSYNC width HBP             Active Width                HFP
```

(`components/esp_lcd/rgb/include/esp_lcd_panel_rgb.h:27-29`), with the field semantics spelled out
underneath — `hsync_pulse_width` "unit: PCLK period", `hsync_back_porch` "number of PCLK between
hsync and start of line active data", `hsync_front_porch` "number of PCLK between the end of active
data and the next hsync", `vsync_pulse_width` "unit: number of lines", and the two vertical porches
"number of invalid lines" (`esp_lcd_panel_rgb.h:62-67`). So:

```
pclk per line  = 800 + 4 + 8 + 8   = 820
lines per frame = 480 + 4 + 8 + 8  = 500
pclk per frame  = 820 × 500        = 410 000
frame period    = 410 000 / pclk
```

The values are the board profile's — `pclkHz 16000000`, `hsyncPulseWidth 4`, `hsyncBackPorch 8`,
`hsyncFrontPorch 8`, `vsyncPulseWidth 4`, `vsyncBackPorch 8`, `vsyncFrontPorch 8`
(`boards/sunton-8048s050c.json`, "timings") — and are the ones the firmware programs
(`components/app/screen_display/panel_rgb.c:58-69`).

| pclk | line time (820/pclk) | frame (410 000/pclk) | refresh rate | active-pixel bytes/s | pclk data rate |
|---|---|---|---|---|---|
| **16 MHz (now)** | 51.25 µs | **25.625 ms** | **39.02 fps** | **29.98 MB/s** | 32.0 MB/s |
| 20 MHz | 41.00 µs | 20.500 ms | 48.78 fps | 37.46 MB/s | 40.0 MB/s |
| 24 MHz | 34.17 µs | 17.083 ms | 58.54 fps | 44.96 MB/s | 48.0 MB/s |

(ARITHMETIC from `panel_rgb.c:58-69` and `esp_lcd_panel_rgb.h:27-67`; the byte rates are
`800 × 480 × 2 = 768 000 B` per frame divided by the frame period, and the last column is
`pclk × 2 B`.) A 60 Hz refresh with these porches needs `410 000 × 60 = 24.6 MHz`, so 24 MHz is
58.5 fps, not 60.

Two consequences that matter for the rest of the note:

1. **The panel can outrun LVGL's 33 ms timer.** 25.625 ms < 33 ms, so at the current configuration
   the framework's period — not the panel — is the ceiling: 30 fps, not 39.02.
2. **The scan-out is a permanent consumer of PSRAM bandwidth.** 768 000 B per frame read by the
   GDMA straight out of PSRAM is 29.98 MB/s at 16 MHz, 44.96 MB/s at 24 MHz. The driver's own header
   states the premise: with `bounce_buffer_size_px` non-zero "DMA fetching from DRAM bounce buffer is
   much faster than PSRAM frame buffer" (`esp_lcd_panel_rgb.h:151-152`), and the guide says the
   bounce-buffer mode's "advantage is achieving a higher pixel clock frequency"
   (`docs/en/api-reference/peripherals/lcd/rgb_lcd.rst:243`). The page states no maximum pclk for a
   16-bit RGB panel on the ESP32-S3; what it does state is the bandwidth constraint and its
   consequences (§4b).

---

## 3. What `esp_lvgl_port` 2.9.0 does in direct mode with `avoid_tearing`

The configuration under test is `num_fbs = 2`, `bounce_buffer_size_px = 0`,
`flags.fb_in_psram = true` (`components/app/screen_display/panel_rgb.c:73-80`), `direct_mode = true` with
`buffer_size = PANEL_W * PANEL_H` and `double_buffer = true` (`panel_rgb.c:107-121`), and
`rgb_cfg.flags.avoid_tearing = true` (`panel_rgb.c:122`).

**Setup: LVGL is handed the panel's own two framebuffers.**

```c
    /* Use RGB internal buffers for avoid tearing effect */
    if (priv_cfg && priv_cfg->avoid_tearing) {
        if (priv_cfg->disp_type ==  LVGL_PORT_DISP_TYPE_RGB) {
            buffer_size = disp_cfg->hres * disp_cfg->vres;
            ESP_GOTO_ON_ERROR(esp_lcd_rgb_panel_get_frame_buffer(disp_cfg->panel_handle, 2, (void *)&buf1, (void *)&buf2), err, TAG,
                              "Get RGB buffers failed");
        }
...
        trans_sem = xSemaphoreCreateCounting(1, 0);
```

(`managed_components/espressif__esp_lvgl_port/src/lvgl9/esp_lvgl_port_disp.c:362-380`.) Two things
follow: LVGL allocates **no** draw buffer of its own in this mode, and the handshake semaphore is a
counting semaphore with **maximum count 1**.

```c
    } else if (disp_cfg->flags.direct_mode) {
        /* When using direct_mode, there must be used full bufer! */
        ESP_GOTO_ON_FALSE((disp_cfg->hres * disp_cfg->vres == buffer_size), ESP_ERR_INVALID_ARG, err, TAG,
                          "Direct mode must using full buffer!");

        disp_ctx->flags.direct_mode = 1;
        lv_display_set_buffers(disp, buf1, buf2, buffer_size * color_bytes, LV_DISPLAY_RENDER_MODE_DIRECT);
```

(`esp_lvgl_port_disp.c:427-432`.) Both buffers are passed, so LVGL is in **double-buffered direct
mode** — which is what triggers the synchronisation copies in §4d.

**The event the flush waits on is VSYNC, once per panel frame.** With `bb_mode` clear the port
registers the vsync callback set:

```c
        const esp_lcd_rgb_panel_event_callbacks_t vsync_cbs = {
            .on_vsync = lvgl_port_flush_rgb_vsync_ready_callback,
        };
...
            ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(disp_ctx->panel_handle, &vsync_cbs, disp_ctx));
```

(`esp_lvgl_port_disp.c:201-219`.) The driver invokes `on_vsync` from its ISR on
`LCD_LL_EVENT_VSYNC_END` (`components/esp_lcd/rgb/esp_lcd_panel_rgb.c:1371-1393`, `:1389`), and the
port's callback gives the semaphore (`esp_lvgl_port_disp.c:549-565`). One give per frame.

**The flush: one whole-screen `draw_bitmap`, then a discard-then-block pair of takes.**

```c
    if ((disp_ctx->disp_type == LVGL_PORT_DISP_TYPE_RGB || disp_ctx->disp_type == LVGL_PORT_DISP_TYPE_DSI)
            && (disp_ctx->flags.direct_mode || disp_ctx->flags.full_refresh)) {
        if (lv_disp_flush_is_last(drv)) {
            /* If the interface is I80 or SPI, this step cannot be used for drawing. */
            esp_lcd_panel_draw_bitmap(disp_ctx->panel_handle, 0, 0, lv_disp_get_hor_res(drv), lv_disp_get_ver_res(drv), color_map);
            /* Waiting for the last frame buffer to complete transmission */
            xSemaphoreTake(disp_ctx->trans_sem, 0);
            xSemaphoreTake(disp_ctx->trans_sem, portMAX_DELAY);
        }
    } else {
        esp_lcd_panel_draw_bitmap(disp_ctx->panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
    }
...
        lv_disp_flush_ready(drv);
```

(`esp_lvgl_port_disp.c:748-763`.) The flush happens **only on the last invalidated area**
(`:750`); earlier areas of the same cycle skip it and just report ready. The first take, with a zero
timeout, discards a give that is already pending; the second blocks for the **next** VSYNC after the
call. Because the semaphore cannot count above one (`:379`), a flush always waits for a vsync that
happens *after* it was entered, and that wait is `(0, 25.625]` ms at 16 MHz.

**The driver side does not copy a single pixel.** `rgb_panel_draw_bitmap()` first decides whether
the pointer it was handed lies inside one of its framebuffers:

```c
    for (int i = 0; i < rgb_panel->num_fbs; i++) {
        if (draw_buffer >= rgb_panel->fbs[i] && draw_buffer < rgb_panel->fbs[i] + fb_size) {
            draw_buf_fb_index = i;
            draw_buf_copy_to_fb = false;
            break;
        }
    }
```

(`esp_lcd_panel_rgb.c:724-730`.) In this configuration it does, so:

```c
    } else {
        ESP_LOGV(TAG, "draw buffer is part of the frame buffer");
        // the new frame buffer index is changed
        rgb_panel->cur_fb_index = draw_buf_fb_index;
        // when this function is called, the frame buffer already reflects the draw buffer changes
        // if the frame buffer is also mounted to the DMA, we need to do the sync between them
        if (!rgb_panel->bb_size && rgb_panel->flags.fb_behind_cache) {
            uint8_t *cache_sync_start = rgb_panel->fbs[draw_buf_fb_index] + (y_start * h_res) * bytes_per_pixel;
            size_t cache_sync_size = (y_end - y_start) * bytes_per_line;
            esp_cache_msync(cache_sync_start, cache_sync_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
        }
```

(`esp_lcd_panel_rgb.c:758-768`), followed by re-pointing every framebuffer's DMA link list at the
new index:

```c
    if (!rgb_panel->bb_size) {
        if (rgb_panel->flags.stream_mode) {
            for (int i = 0; i < rgb_panel->num_fbs; i++) {
                // Note, because of DMA prefetch, there's possibility that the old frame buffer might be sent out again
                // it's hard to know the time when the new frame buffer starts
                gdma_link_concat(rgb_panel->dma_fb_links[i], -1, rgb_panel->dma_fb_links[rgb_panel->cur_fb_index], 0);
            }
```

(`esp_lcd_panel_rgb.c:775-780`). `stream_mode` is on because `refresh_on_demand` is not set
(`esp_lcd_panel_rgb.c:436`). On this SoC the link-switch event path is compiled out —
`RGB_LCD_USE_GDMA_LINK_SWITCH_EVENT` is defined only under `SOC_HAS(AXI_GDMA)`
(`esp_lcd_panel_rgb.c:52-56`), and the ESP32-S3 has no AXI GDMA (`components/soc/esp32s3/include/soc/soc_caps.h`
contains no AXI_GDMA definition) — so the driver's own comment about not knowing when the new buffer
starts applies verbatim.

**So the pipeline is serialized behind the vsync handshake.** In order, per refresh cycle:

1. LVGL renders every invalidated area into `buf_act` (one of the panel's framebuffers), in PSRAM.
2. On the last area, `draw_buf_flush()` calls `wait_for_flushing()` and then the flush callback
   (`src/core/lv_refr.c:1401-1429`). With this port the wait is a no-op: the flush callback calls
   `lv_display_flush_ready()` before it returns, which clears `disp->flushing`
   (`src/display/lv_display.c:750-754`).
3. The port calls `esp_lcd_panel_draw_bitmap()` for the whole screen; the driver copies nothing,
   writes back the drawn band from the data cache, and re-points the DMA links.
4. The port blocks on the semaphore until the **next** VSYNC_END, then calls
   `lv_display_flush_ready()`.
5. Only then does `draw_buf_flush()` swap `buf_act` (`lv_refr.c:1432-1441`) and the cycle end, and
   only then can the next cycle start.

**How much waiting that adds.** Between zero and one panel frame per cycle — 0 to 25.625 ms at
16 MHz — and no amount of faster rendering removes it, because the cycle cannot end except at a
vsync. It is the price of `avoid_tearing`, and the port's own header says so: `avoid_tearing`
"requires over two LCD buffers and **may reduce the frame rate**"
(`managed_components/espressif__esp_lvgl_port/include/esp_lvgl_port_disp.h:82-83`). The two takes
mean exactly "discard a pending vsync, then wait for the following one": the zero-timeout take drains
a give that already happened, the blocking take waits for the next
(`esp_lvgl_port_disp.c:754-755`; one give per frame at `esp_lcd_panel_rgb.c:1389`; the counting
semaphore cannot hold more than one, `esp_lvgl_port_disp.c:379`). Two consequences follow
(ARITHMETIC from those three facts):

- **The cost is a step, not a slope.** If the render grows past one panel frame, a vsync lands inside
  the render, the zero-timeout take swallows it, and the cycle gains a whole 25.625 ms for a fraction
  of a millisecond of extra work.
- **The wait varies cycle to cycle even when nothing changes.** The 33 ms timer and the 25.625 ms
  frame are not multiples, so the phase advances by `33 mod 25.625 = 7.375 ms` per cycle and the wait
  sweeps 0–25.625 ms while `fps` sits at the timer's 30.

**What the non-`avoid_tearing` path would do instead.** Without `avoid_tearing` the port allocates
its own buffers from the heap (`esp_lvgl_port_disp.c:382-394`) and every flush becomes a CPU copy into
the framebuffer:

- the driver takes the `draw_buf_copy_to_fb` branch and copies the flushed area row by row into
  `fbs[cur_fb_index]` (`esp_lcd_panel_rgb.c:733-749`, where `COPY_PIXEL_CODE_BLOCK` is a per-row
  `memcpy` into `fb + (y_start * h_res + x_start) * bytes_per_pixel`, plus a `bytes_to_flush`
  update: `components/esp_lcd/rgb/rgb_lcd_rotation_sw.h:47-62`), then `esp_cache_msync`s the band
  (`esp_lcd_panel_rgb.c:751-753`);
- `cur_fb_index` is never updated in that branch, so with `num_fbs = 2` the second framebuffer is
  allocated and never scanned — one of the reasons the reference project gives for choosing
  `avoid_tearing` (`~/Documents/SolidGL/firmware/components/solidgl_platform/src/solidgl_platform.c:341-349`);
- in PARTIAL mode the copy is one buffer's worth per flush (the strip); in DIRECT mode the port's
  whole-screen call (`esp_lvgl_port_disp.c:752`) makes it 768 000 B per frame.

One hazard worth recording because it is one flag away: `trans_sem` is created **only** inside the
`avoid_tearing` branch (`esp_lvgl_port_disp.c:379-381`), but it is taken unconditionally whenever
`disp_type == RGB` and `direct_mode` or `full_refresh` is set (`:748-755`). Setting
`direct_mode = true` **without** `avoid_tearing` on RGB therefore calls `xSemaphoreTake(NULL, …)`.
That is read from source; this note did not run it.

---

## 4. The alternatives, each with its exact change and its price

### (a) Partial mode into internal RAM over one PSRAM framebuffer — what `Sunton5_IDF` does

The reference project on the same board takes a different route. Panel side: `num_fbs = 0`,
`bounce_buffer_size_px = 10 * 800`, `flags.fb_in_psram = true`, `psram_trans_align = 64`
(`~/Documents/Sunton5_IDF/components/Sunton5_IDF/src/sunton5_display.c:167-176`). LVGL side: partial
buffers of `CONFIG_S5_LVGL_RENDER_PARTIAL_BUFFER_LINES = 80` lines, ping-pong by default
(`sunton5_display.c:27-30`, `:247-282`), allocated **internal first** and falling back to PSRAM:

```c
        const uint32_t caps_int = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT;
        const uint32_t caps_psr = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
...
        lv_display_set_buffers(disp, pb1, pb2, part_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
```

(`sunton5_display.c:251-252`, `:274`), with the Kconfig label stating the preference:
"PARTIAL_2 (ping-pong partial; prefer INTERNAL)" (`components/Sunton5_IDF/Kconfig:53-57`). Its flush
is three lines, into the panel:

```c
        esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    }
    lv_display_flush_ready(disp);
```

(`sunton5_display.c:186-195`.) It does not use `esp_lvgl_port` at all.

The sizes (ARITHMETIC from those constants, 800 px wide, RGB565):

| Buffer | Pixels | Bytes | Where |
|---|---|---|---|
| partial draw buffer, 80 lines | 64 000 | **128 000** | internal preferred, PSRAM fallback |
| both ping-pong buffers | 128 000 | **256 000** | same |
| bounce buffer, 10 lines | 8 000 | **16 000**, ×2 buffers = 32 000 | internal, allocated by the driver |

`num_fbs = 0` does **not** mean "no framebuffer": the guide is explicit that "for backward
compatibility, setting this to `0` will allocate a single frame buffer. If you don't want to allocate
any frame buffer, use `no_fb` instead" (`docs/en/api-reference/peripherals/lcd/rgb_lcd.rst:16`), and
the header says the same (`esp_lcd_panel_rgb.h:149`). So this design is one PSRAM framebuffer **plus**
a bounce buffer.

What it costs, in copies per pixel:

1. LVGL renders the invalidated strip into internal RAM (no PSRAM write traffic).
2. `draw_bitmap` copies that strip into the PSRAM framebuffer, row by row, and skips the cache
   write-back because `bb_size != 0` (`esp_lcd_panel_rgb.c:751`).
3. The bounce ISR then copies the framebuffer into the bounce buffers — **the whole frame, every
   frame**, because `bounce_pos_px` advances by `bb_size` per DMA EOF and wraps at `fb_size`
   (`esp_lcd_panel_rgb.c:1042-1057`). At 39.02 fps that is 768 000 B × 39.02 = 29.98 MB/s of CPU
   `memcpy` out of PSRAM, permanently, independent of what changed on screen.

What it buys: the flush returns immediately (no vsync wait, so no 25.625 ms serialization), LVGL
writes into internal RAM instead of competing with the scan-out, and — per the guide — the bounce
buffer path is the one that tolerates a higher pclk (`rgb_lcd.rst:243`). Against it: 128–256 KB of
internal RAM, a full-frame copy per frame in an ISR, and the guide's warning that in bounce mode the
display "**CANNOT** function if the external memory cache is disabled" and that PSRAM bandwidth
contention between the two cores "causes the driver to fail in switching the bounce buffer promptly,
resulting in a screen shift" (`rgb_lcd.rst:243`, `:251`).

For this firmware the internal-RAM side is the binding constraint: the devkit census with the UI
built reported **157 787 B** free and a largest block of **81 920 B**
(`docs/research/radio-player-memory-and-lvgl.md:743-746`, MEASURED (board) there, on the devkit).
An 80-line buffer (128 000 B) does not fit in one 81 920 B block; a 40-line buffer (64 000 B) does,
once, with nothing else asking.

### (b) The bounce buffer: what it is for, when zero is legal, what it costs

The header states the purpose in one line: "If it's non-zero, the driver allocates two DRAM bounce
buffers for DMA use. DMA fetching from DRAM bounce buffer is much faster than PSRAM frame buffer"
(`esp_lcd_panel_rgb.h:151-152`). The guide describes the mechanism and the price:

> This mode allocates two "bounce buffers" from internal memory and a main frame buffer in PSRAM.
> … The LCD peripheral uses DMA to read data from one bounce buffer while an interrupt routine uses
> the CPU DCache to copy data from the main PSRAM frame buffer into the other bounce buffer. … The
> advantage of this mode is achieving a higher pixel clock frequency. … The downside is a significant
> increase in CPU usage, and the LCD **CANNOT** function if the external memory cache is disabled,
> such as during OTA or NVS writes to the main flash.

(`docs/en/api-reference/peripherals/lcd/rgb_lcd.rst:243`.)

**When `0` is legal:** the driver only *requires* a bounce buffer when it has no framebuffer to read
from — `ESP_RETURN_ON_FALSE(!(rgb_panel_config->flags.no_fb && rgb_panel_config->bounce_buffer_size_px == 0),
ESP_ERR_INVALID_ARG, …)` (`esp_lcd_panel_rgb.c:333-335`). With `num_fbs > 0` and
`fb_in_psram = true`, zero is the supported "let the GDMA read PSRAM directly" arrangement, which is
what this project does.

**What it costs in CPU and PSRAM bandwidth when the framebuffers are in PSRAM:** the ISR copies the
entire framebuffer into the bounce buffers once per frame (`esp_lcd_panel_rgb.c:1042-1057`), i.e.
29.98 MB/s at 16 MHz (ARITHMETIC from §2). With no bounce buffer that traffic disappears — the GDMA
reads PSRAM itself — but the *scan-out* still consumes the same 29.98 MB/s, and LVGL's own writes and
copies in PSRAM compete with it. The IDF guide quantifies the failure mode this contest produces:
"if your draw buffers are in PSRAM and their contents are copied to the internal frame buffer by CPU
Core 1, while CPU Core 0 is performing another memory copy in the DMA EOF ISR, both CPUs will be
accessing PSRAM via cache, sharing its bandwidth" (`rgb_lcd.rst:251`).

The driver also offers a mitigation for the mode this project uses: `esp_cache_msync` of only the
band that was drawn (`esp_lcd_panel_rgb.c:764-767`) rather than the whole frame.

### (c) `num_fbs` = 1, 2, 3 — and whether the port can use 3

| Value | What the driver does | What it means here |
|---|---|---|
| `0` | "By default (set to either 0 or 1) only one frame buffer will be used" (`esp_lcd_panel_rgb.h:149`) | same as 1: CPU copies into the single FB while the panel scans it |
| `1` | one framebuffer; the CPU copy in `esp_lcd_panel_rgb.c:733-749` writes the buffer being scanned | tearing unless the flush is synchronized to VBlank; no double-buffer sync copies |
| `2` | two framebuffers, `cur_fb_index` switches in the direct path (`:761`), DMA links re-pointed (`:775-780`) | the current configuration; enables `avoid_tearing` |
| `3` | allowed: `num_fbs <= ESP_RGB_LCD_PANEL_MAX_FB_NUM`, and `ESP_RGB_LCD_PANEL_MAX_FB_NUM` is `3` (`esp_lcd_panel_rgb.c:345`, `esp_lcd_panel_rgb.h:22`) | **not reachable through `esp_lvgl_port`** |

The port cannot use three framebuffers: it asks the panel for exactly two
(`esp_lcd_rgb_panel_get_frame_buffer(disp_cfg->panel_handle, 2, …)`,
`esp_lvgl_port_disp.c:367`) and never calls `lv_display_set_3rd_draw_buffer()`
(`include/lvgl/display/lv_display.h:298-302`), so LVGL's triple-buffer machinery
(`src/core/lv_refr.c:740-750`) stays idle. Using 3 would need a forked port or a hand-written flush
callback.

### (d) `PARTIAL` / `FULL` / `DIRECT`: where LVGL draws and what is copied

From the enum's own documentation (`include/lvgl/display/lv_display.h:41-60`):

- **`LV_DISPLAY_RENDER_MODE_PARTIAL`** — "Use the buffer(s) to render the screen is smaller parts …
  At least 1/10 screen size buffer(s) are recommended." LVGL renders the invalidated area in strips
  of `get_max_row()` rows and flushes each strip (`src/core/lv_refr.c:818-858`). Copied per strip: the
  whole strip, by the flush path (the driver's copy or your DMA).
- **`LV_DISPLAY_RENDER_MODE_DIRECT`** — "The buffer(s) has to be screen sized and LVGL will render
  into the correct location of the buffer … Only the changed ares will be updated. With 2 buffers the
  buffers' content are kept in sync automatically and in flush_cb only address change is required."
  LVGL renders each invalidated area in place and flushes once per area, with the panel handshake only
  on the last one (`lv_refr.c:860-866`, `:865`).
- **`LV_DISPLAY_RENDER_MODE_FULL`** — "Always redraw the whole screen even if only one pixel has been
  changed." On this UI that means 768 000 B painted per cycle that a partial refresh would not paint.

**The synchronisation the DIRECT documentation mentions is a real per-frame copy.** In
double-buffered direct mode LVGL keeps the two framebuffers identical by copying the previous cycle's
dirty areas into the buffer it is about to draw into:

```c
    /*Do not sync if not direct double buffered and no sync callback set*/
    const bool auto_sync = disp_refr->render_mode == LV_DISPLAY_RENDER_MODE_DIRECT &&
                           lv_display_is_double_buffered(disp_refr);
...
            lv_draw_buf_copy(off_screen, sync_area, on_screen, sync_area);
            if(off_screen2 != on_screen)
                lv_draw_buf_copy(off_screen2, sync_area, on_screen, sync_area);
```

(`src/core/lv_refr.c:677-684`, `:782-784`.) `lv_draw_buf_copy()` dispatches to the buffer's copy
handler (`src/draw/lv_draw_buf.c:402-415`), which for a same-format buffer is a per-line `lv_memcpy`
(`lv_draw_buf.c:628-684`). With two buffers `off_screen2 == on_screen`, so it is one copy per dirty
area, and those areas are the ones recorded in the previous cycle (`lv_refr.c:436-446`). So in this
mode **every pixel changed is written once by the renderer and copied once more**, both in PSRAM.

---

## 5. Build and system levers

### `CONFIG_COMPILER_OPTIMIZATION_PERF` — the build is at `-Og` today

IDF's choice calls the two levels by name: `COMPILER_OPTIMIZATION_DEBUG` is "Debug (-Og)" and
`COMPILER_OPTIMIZATION_PERF` is "Optimize for performance (-O2)" (IDF root `Kconfig:432-439`). The
Sunton build is **Debug**:

```
#define CONFIG_COMPILER_OPTIMIZATION_DEBUG 1
```

(`build_sunton/config/sdkconfig.h`; the same line is in the saved configuration at
`sdkconfig.sunton`.) Every line of LVGL, the RGB driver and the UI is compiled at `-Og`.

Espressif measured what that costs on this exact board class — 800x480 RGB, PSRAM, GDMA,
`ESP32-S3-LCD-EV-BOARD`, direct (avoid-tearing) mode, no double buffer, `LV_DEF_REFR_PERIOD` 30 ms:

| Average FPS (LVGL 9.2) | Changed settings |
|---|---|
| 9 | Default (Debug, 160 MHz, 80 MHz flash, DIO, PSRAM 80 MHz) |
| 9 | + Optimization: Performance (`CONFIG_COMPILER_OPTIMIZATION_PERF=y`) |
| 11 | + CPU frequency: 240 MHz |
| 11 | + Flash mode: QIO |
| 11 | + LVGL fast memory |
| 11 | + `CONFIG_LV_DEF_REFR_PERIOD=10` |

(`managed_components/espressif__esp_lvgl_port/docs/performance.md:88-113`.) Read that table twice:
for LVGL 9.2 on this board **the compiler level moved the number by zero and the refresh period by
zero**, while the CPU clock moved it by two. The LVGL 8 column of the same table moved 12 → 13 → 14 →
14 → 15 → 16 for the same steps. The two figures this note takes from it are (i) the CPU clock is
already at its maximum here (`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y`, `sdkconfig.sunton`), so
that lever is spent, and (ii) the doc's own summary is that the frame-buffer configuration, not the
compiler, is what dominates (`performance.md:19-21`).

### `CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM` — a no-op in LVGL 9.6

The Kconfig entry still exists and still promises the old behaviour:

```
config LV_ATTRIBUTE_FAST_MEM_USE_IRAM
	bool "Place LV_ATTRIBUTE_FAST_MEM functions in IRAM (ESP32 only)"
	help
	  Configure LV_ATTRIBUTE_FAST_MEM to use IRAM.
	  This option is only applicable to ESP32 targets.
```

(`managed_components/lvgl__lvgl/Kconfig:3799-3803`.) In 9.6.0 the macro it sets is **never read**.
The only trace of it anywhere in the tree is its own definition:

```c
#ifndef LV_ATTRIBUTE_FAST_MEM_USE_IRAM
    #ifdef CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM
        #define LV_ATTRIBUTE_FAST_MEM_USE_IRAM CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM
    #else
        #define LV_ATTRIBUTE_FAST_MEM_USE_IRAM 0
```

(`include/lvgl/config/lv_conf_internal.h:4642-4648`; a tree-wide grep for
`LV_ATTRIBUTE_FAST_MEM_USE_IRAM` in `*.c`/`*.h` returns those four lines and nothing else), while the
attribute itself is empty:

```c
#ifndef LV_ATTRIBUTE_FAST_MEM
#define LV_ATTRIBUTE_FAST_MEM
#endif
```

(`include/lvgl/config/lv_conf_internal.h:5216-5218`.) The ~30 functions that still carry the
annotation (`src/misc/lv_color_op.c:36`, `src/misc/lv_math.c:53,213,239,463`,
`src/draw/lv_draw_line.c:36,54`, …) are compiled with no attribute, and 118 further annotations in
`src/draw/sw/` are commented out (`/* LV_ATTRIBUTE_FAST_MEM */`). The option is off in this build
anyway (`sdkconfig.sunton`), and turning it on cannot change a single object file. The
`+1 FPS` attributed to it in `esp_lvgl_port`'s table (`docs/performance.md:56-60`, `:112`) was
measured on LVGL 8, where the macro was wired to `IRAM_ATTR`.

### `CONFIG_LV_DEF_REFR_PERIOD` — the cadence ceiling, and the only lever that moves the reported number directly

It sets the refresh timer's period (`src/display/lv_display.c:125`), it is the animation step
("Default display refresh, input device input and animation step period",
`Kconfig:353-357`), and it is the divisor in the sysmon's cap (`lv_sysmon.c:300-305`). The build has
33 (`sdkconfig.sunton`) → a 30 fps ceiling and a 30 fps *floor* for an idle screen. It can also
be changed at runtime without touching Kconfig, which is what the reference project on this board
does:

```c
  const uint32_t refr_ms = 16;
  lv_timer_t *refr = lv_display_get_refr_timer(lvgl_display);
  if (refr != NULL) {
    lv_timer_set_period(refr, refr_ms);
  }
```

(`~/Documents/SolidGL/firmware/components/solidgl_platform/src/solidgl_platform.c:393-397`), with the
comment above it reasoning from the same panel period this note computes: "LVGL's default is 30 ms
and the panel's own frame is about 25 ms at the profile's clock" (`:387-392`). SolidGL's board profile
is this project's board profile — `pclk 16 MHz`, `num_fbs 2`, `fb_in_psram`, `bounce 0`
(`~/Documents/SolidGL/build/generated/solidgl-profile-sunton-8048s050c.h:19,33-36`) — so this is the
same panel at the same clock with a different refresh period.

### Task placement: which core runs the refresh, and at what priority

The port creates its task pinned, at the configured priority and core:

```c
        res = xTaskCreatePinnedToCoreWithCaps(lvgl_port_task, "taskLVGL", cfg->task_stack, xTaskGetCurrentTaskHandle(),
                                              cfg->task_priority, &lvgl_port_ctx.lvgl_task, cfg->task_affinity, caps);
```

(`managed_components/espressif__esp_lvgl_port/src/lvgl9/esp_lvgl_port.c:90-93`.) This project points it
at **core 0, priority 4**:

```c
static const lvgl_port_cfg_t k_port = {
    .task_priority = 4,
    .task_stack = 16384,
    .task_affinity = 0,
```

(`components/app/screen_display/panel_rgb.c:46-52`.) Core 0 is the network core, and everything on it runs above priority 4:
Wi-Fi's task is pinned there (`CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_0=y`), the esp_timer task is on
CPU0 (`CONFIG_ESP_TIMER_TASK_AFFINITY=0x0`, `CONFIG_ESP_TIMER_TASK_AFFINITY_CPU0=y`), and lwIP's TCP/IP
task runs at **priority 18** with no affinity (`CONFIG_LWIP_TCPIP_TASK_PRIO=18`,
`CONFIG_LWIP_TCPIP_TASK_AFFINITY_NO_AFFINITY=y`, affinity `0x7FFFFFFF`) — all in `sdkconfig.sunton`.

Core 1 carries the encoder and the HTTP server: `ice_broadcast` at **priority 6** and `ice_telem` at
priority 3, both pinned to core 1 (`components/portable/mini_icecast/mini_icecast.c:597-598`), and the four
connection workers plus the accept task at **priority 5** on core 1
(`components/portable/http_server/http_server.c:426-431`). The encoder's measured share is 44.6 % of one core
(`docs/adr/0004-ui-memory-is-external.md:42`, and `radio-player-memory-and-lvgl.md:758-761`).

The arithmetic that follows is the point: a priority-4 task is preempted by anything at priority 5 or
above that runs on its core, and lwIP at 18 is **unpinned**
(`CONFIG_LWIP_TCPIP_TASK_AFFINITY_NO_AFFINITY=y`, `sdkconfig.sunton`), so it can preempt the
refresh on either core. What core 1 does not have is Wi-Fi's task, which is pinned to core 0
(`CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_0=y`) and cannot follow. On core 1 the refresh task sits below the encoder at 6 and below the HTTP
workers at 5; it cannot starve the encoder — 4 < 6 — so the encoder's 44.6 % is untouched, and the
refresh gets what the encoder leaves, 100 − 44.6 = **55.4 % of that core** (ARITHMETIC from
`docs/adr/0004-ui-memory-is-external.md:42`). On core 0 it gets what Wi-Fi, lwIP and the esp_timer task
leave, which is not a fixed share. The HTTP workers at priority 5 take the core only while they are
awake, and they block on sockets while serving.

The reference project on this board keeps its refresh task on core 1 at the same priority 4
(`~/Documents/SolidGL/firmware/components/solidgl_platform/src/solidgl_platform.c:303-320`,
`.task_affinity = 1` at `:312`), and this repository's own `components/app/screen_display/panel_rgb.c` did too until commit
`8c8d6e7` was followed by an uncommitted change: `git show 8c8d6e7:main/panel_rgb.c` has
`.task_affinity = 1` with the comment "core 1 so the refresh does not queue behind the application's
core", and the working tree has `.task_affinity = 0` with "the refresh lives on core 0 with Wi-Fi and
lwIP".

**This is the candidate for the render time that §9.4 finds unexplained.** Preemption produces no
cycle that is a multiple of anything; it inflates the wall time LVGL measures as `render`
(`lv_sysmon.c:256-264`), which is the field to compare against the draw work §6 accounts for. It is a
hypothesis with a mechanism, not a measurement.

### The S3 SIMD blenders in `esp_lvgl_port` are not compiled for this LVGL

The component ships hand-written ESP32-S3 assembly for the blend paths this UI uses — `fill` to
RGB565, RGB565→RGB565 image blend, and the RGB888/ARGB8888 variants — under `src/lvgl9/simd/`
(`lv_color_blend_to_rgb565_esp32s3.S`, `lv_rgb565_blend_normal_to_rgb565_esp32s3.S`, …). They are
selected by `CONFIG_LV_DRAW_SW_ASM_CUSTOM` and the header that overrides LVGL's blend macros
(`include/esp_lvgl_port_lv_blend.h:1-40`). But the component's own CMake compiles them for one LVGL
range only:

```cmake
# Include SIMD assembly source code for rendering, only for (9.1.0 <= LVG_version < 9.2.0) and only for esp32 and esp32s3
if((lvgl_ver VERSION_GREATER_EQUAL "9.1.0") AND (lvgl_ver VERSION_LESS "9.2.0"))
```

(`managed_components/espressif__esp_lvgl_port/CMakeLists.txt:110-118`.) This project pins LVGL 9.6.0
(`dependencies.lock`, `lvgl/lvgl: 9.6.0~1`), so those files are **not in the build**, and LVGL 9.6's own
`LV_DRAW_SW_ASM` choice has no Xtensa option
(`CONFIG_LV_DRAW_SW_ASM_NONE=y`, `CONFIG_LV_USE_DRAW_SW_ASM=0`, all in `sdkconfig.sunton`). There is no
accelerated blend path available to this firmware at its current LVGL version; the drawing is C on the
240 MHz core (a blend implementation, not a lever).

### `CONFIG_LV_USE_OS` and `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT` — two draw units are gated, not merely untested

The Kconfig makes the dependency explicit:

```
config LV_DRAW_SW_DRAW_UNIT_CNT
	int "Number of draw units"
	default 1
	range 1 255
	depends on !LV_OS_NONE
	help
	  Number of threads used to render a frame in parallel
```

(`Kconfig:526-533`), and the source refuses to compile the combination:

```c
#if LV_DRAW_SW_DRAW_UNIT_CNT > 1 && LV_USE_OS == LV_OS_NONE
    #error "OS support is required when more than one SW rendering units are enabled"
#endif
```

(`src/draw/sw/lv_draw_sw.c:31-33`.) This build is `CONFIG_LV_OS_NONE=y` / `CONFIG_LV_USE_OS=0`
(`sdkconfig.sunton`), so `LV_DRAW_SW_DRAW_UNIT_CNT` **does not exist in the configuration at
all** — `sdkconfig.sunton` contains no `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT` line, because its dependency
is false. Two software draw units therefore require switching LVGL to `LV_OS_FREERTOS`
(`Kconfig:168-190`) first.

Does `esp_lvgl_port` work with `LV_USE_OS=FREERTOS`? Nothing in the port decides it either way. Its
whole Kconfig is one entry, for PPA rotation (`managed_components/espressif__esp_lvgl_port/Kconfig:1-10`);
its README and its headers never mention `LV_USE_OS`; and it serialises LVGL with its own FreeRTOS
recursive mutex (`src/lvgl9/esp_lvgl_port.c:77`, `:142-152`), never with `lv_lock()`. So by
inspection the two locks would be independent and the port would keep working — but nothing in the
component tests or documents that combination, and this note did not build it. **Status: allowed by
LVGL's source, unused and untested by this port, unmeasured on this board.** The cost would be a
second renderer thread — another internal-RAM stack (`esp_lvgl_port.c:84-88` shows the port's own
stack defaults to `MALLOC_CAP_INTERNAL`) — competing for the two cores and for the PSRAM bandwidth
the scan-out is already using (§2).

### PSRAM at 120 MHz and flash at QIO/120: the project's comment is right for the wrong reason

The comment under test is in `sdkconfig.defaults.sunton:12-20`: the profile says 120 MHz for PSRAM
and flash, but "IDF 6.1 will not build that on this SoC … its MSPI clock rules assert that the flash
and PSRAM module clocks agree (`…/mspi_timing_tuning_configs.h:150`), and the same Kconfig that offers
120 MHz PSRAM sets the flash string to \"80m\"" — therefore PSRAM runs at 80 MHz.

The rules, read from source:

- **Which modules need tuning.** Flash in STR mode needs tuning above 80 MHz; PSRAM in DTR (octal)
  mode needs tuning above 40 MHz: `#define MSPI_TIMING_FLASH_NEEDS_TUNING (MSPI_TIMING_FLASH_MODULE_CLOCK > 80)`
  and `#define MSPI_TIMING_PSRAM_NEEDS_TUNING (MSPI_TIMING_PSRAM_MODULE_CLOCK > 40)`
  (`components/esp_hw_support/mspi/mspi_timing_tuning/port/esp32s3/mspi_timing_tuning_configs.h:58-61`, `:80-83`).
- **The assert only bites when both need tuning.** It is the third and fourth branch that compare the
  two expected core clocks; when only one module needs tuning the test is divisibility, not equality:
  `ESP_STATIC_ASSERT(MSPI_TIMING_PSRAM_EXPECTED_CORE_CLK_MHZ % MSPI_TIMING_FLASH_MODULE_CLOCK == 0, …)`
  (`mspi_timing_tuning_configs.h:159-162`).
- **The numbers for this project.** Flash is 80 MHz STR (`sdkconfig.sunton`; the profile's DIO
  is set at `sdkconfig.defaults.sunton:25-26`) → module clock 80 → **no tuning**
  (`mspi_timing_tuning_configs.h:47-56`). Octal PSRAM at 120 MHz is DTR → module clock 120 → tuning,
  expected core clock **240** (`mspi_timing_tuning_configs.h:136-139`). The applicable branch is
  `!FLASH_NEEDS && PSRAM_NEEDS`, whose test is `240 % 80 == 0` — **which passes.**

So **120 MHz octal PSRAM with 80 MHz DIO flash builds.** The flash-clock Kconfig line the comment
cites is about the *boot header*: `ESPTOOLPY_FLASHFREQ` defaults to `'80m' if ESPTOOLPY_FLASHFREQ_120M`
(`components/esptool_py/Kconfig.projbuild:104-108`) — the ROM bootloader's rate, not a runtime lock.
The constraint the comment describes is the one that runs the other way: **120 MHz flash cannot be
combined with 80 MHz octal PSRAM.** Flash at 120 MHz STR also needs tuning, with expected core clock
240 (`mspi_timing_tuning_configs.h:112-118`), and octal PSRAM at 80 MHz expects 160
(`mspi_timing_tuning_configs.h:122-125`); both need tuning, so the equality assert runs and
`240 == 160` fails with "FLASH and PSRAM Mode configuration are not supported"
(`mspi_timing_tuning_configs.h:149-150`). The reference project's example configuration takes the
consistent pair instead — QIO 120 MHz flash with octal 120 MHz PSRAM and
`CONFIG_IDF_EXPERIMENTAL_FEATURES=y`
(`~/Documents/Sunton5_IDF/examples/wifi_example/sdkconfig:439,445,889,897,2330`).

What actually blocks 120 MHz PSRAM here is the PSRAM option's own dependency and warning:

```
        config SPIRAM_SPEED_120M
            depends on SPIRAM_MODE_QUAD || IDF_EXPERIMENTAL_FEATURES
            bool "120MHz clock speed (READ DOCS FIRST)"

            help
                - Quad PSRAM 120 MHz is stable.

                - Octal PSRAM 120 MHz is an experimental feature, it works when
                  the temperature is stable.

                    Risks:
                        If your chip powers on at a certain temperature, then after the temperature
                        increases or decreases by approximately 20 Celsius degrees (depending on the
                        chip), the accesses to / from PSRAM will crash randomly.
```

(`components/esp_psram/esp32s3/Kconfig.spiram:89-102`.) This board is octal
(`CONFIG_SPIRAM_MODE_OCT=y`, `sdkconfig.sunton`), so the 120 MHz row needs
`CONFIG_IDF_EXPERIMENTAL_FEATURES=y` and accepts a documented random-crash window; there is even a
mitigation option for exactly that (`SPIRAM_TIMING_TUNING_POINT_VIA_TEMPERATURE_SENSOR`, "Adjust PSRAM
timing tuning point via on-chip temperature sensor in real-time",
`Kconfig.spiram:110-118`). The board profile still declares 120 MHz for both
(`boards/sunton-8048s050c.json`, "target"), and the project's comment should be corrected to say what
the source says: 80 MHz is a decision against an experimental mode with a thermal failure mode, not a
constraint imposed by the flash clock.

### Cache line size and data cache size

The S3's cache is configured in one file (`components/esp_system/port/soc/esp32s3/Kconfig.cache`).
The build takes the defaults for line size and data cache:

```
#define CONFIG_ESP32S3_DATA_CACHE_32KB 1
#define CONFIG_ESP32S3_DATA_CACHE_LINE_32B 1
```

(`CONFIG_ESP32S3_DATA_CACHE_32KB`, `CONFIG_ESP32S3_DATA_CACHE_LINE_32B`; the instruction cache is
16 KB / 32 B under `CONFIG_ESP32S3_INSTRUCTION_CACHE_16KB`, `CONFIG_ESP32S3_INSTRUCTION_CACHE_LINE_32B`,
all in `sdkconfig.sunton`).

**64 B lines are possible.** The choice offers 16, 32 and 64 bytes, and `ESP32S3_DATA_CACHE_LINE_64B`
has no dependency beyond the choice itself (`Kconfig.cache:106-119`); only the 16 B option is
restricted (`depends on ESP32S3_DATA_CACHE_16KB || ESP32S3_DATA_CACHE_32KB`, `:112-114`). The help
says nothing about performance in either direction — "Data cache line size to be set on application
startup" (`:109-110`) — so what a 64 B line changes for LVGL is a mechanism, not a documented claim:
the line is the unit of a cache fill, LVGL's flush writes single pixels and short rows into PSRAM
through this cache, and the driver writes the framebuffer back with
`esp_cache_msync(..., ESP_CACHE_MSYNC_FLAG_UNCOHERENT)`-style band syncs of `(y_end - y_start) ×
bytes_per_line` (`esp_lcd_panel_rgb.c:764-767`). A 64 B line halves the tag count per byte scanned
and doubles the fill granularity per touched line. **Unmeasured on this board.**

**64 KB of data cache costs 32 KB of internal RAM.** "If you use 32KB data cache rather than 64KB data
cache, the other 32KB will be added to the heap" (`Kconfig.cache:69-72`), and
`ESP32S3_DATA_CACHE_SIZE` is `0x8000` for the 32 KB choice and `0x10000` for 64 KB (`:82-87`). Given
the devkit's 157 787 B free and 81 920 B largest block (`radio-player-memory-and-lvgl.md:743-746`),
that is affordable but not free; it also cannot help the scan-out, whose 29.98 MB/s never passes
through the cache.

### Where the hub frames live (a link measurement, because it decides the draw cost)

`components/app/screen_display/assets/hub_frames.bin` is 368 640 B on disk, embedded with `EMBED_FILES`
(`components/app/screen_display/CMakeLists.txt:9-11`), and in the Sunton image it is not in flash:

```
 .rodata.embedded
                0x3c1276fc    0x5a004 esp-idf/main/libmain.a(hub_frames.bin.S.obj)
                0x3c1276fc                _binary_hub_frames_bin_start
...
                0x3c1816fc                _binary_hub_frames_bin_end
```

(`build_sunton/sd_stream_server.map:93653-93659`, MEASURED (link)). `0x3c1276fc` is inside the
external-RAM data window (`SOC_EXTRAM_DATA_LOW 0x3C000000`, `SOC_EXTRAM_DATA_HIGH 0x3E000000`,
`components/soc/esp32s3/include/soc/soc.h:173-174`), which is the consequence of
`CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y` / `CONFIG_SPIRAM_RODATA=y`
(`sdkconfig.defaults.sunton:39-40`, `sdkconfig.sunton`). So every hub blit reads its
pixels from PSRAM and shares bandwidth with the scan-out — 18 432 B per crop, 2 crops per 100 ms
(`components/app/screen_display/include/hub_frames.h:12,18`, `components/app/screen_display/cassette_view.c:147-153`), i.e. 368 640 B/s (ARITHMETIC):
0.001 % of the scan-out's 29.98 MB/s. The frames' location is not a performance lever; it is a fact
that rules the image path out as a cause.

---

## 6. What this UI asks the renderer to draw, and which switch gates it

The cassette is drawn, not shipped as a bitmap (`components/app/screen_display/cassette_view.c:4-15`). The draw paths it uses,
and the work each implies:

| Element | LVGL path | Work per invalidation | Source |
|---|---|---|---|
| Background, tape packs, label, red side square, 168 px ring | `lv_obj_create` + `lv_obj_set_style_radius` + `bg_opa = COVER` → `lv_draw_sw_fill` | a radius mask per fill, per row, via the circle cache | `cassette_view.c:95-111`, `src/draw/sw/lv_draw_sw_fill.c:59-99` |
| Two hub keyframes | `lv_image_create` + `lv_image_set_src`, RGB565, no alpha, no recolor, no transform | a straight `lv_draw_sw_blend` from the image buffer | `cassette_view.c:203-213`, `src/draw/sw/lv_draw_sw_img.c:274-283` |
| Title | `lv_label_set_long_mode(LV_LABEL_LONG_SCROLL)`, montserrat 22 | the label's box re-invalidated on every animation step | `cassette_view.c:228-231`, `src/widgets/label/lv_label.c:1476-1482` |
| "B" | montserrat 48 label over the red square | glyph blits | `cassette_view.c:221-224` |
| Progress bar | created, radius 5, **never updated** — the update block is commented out | none | `cassette_view.c:242-249`, `:356-361` |

**A radius fill requires `LV_DRAW_SW_COMPLEX`, and disabling it deletes the drawing.** In
`lv_draw_sw_fill()` the plain-rectangle case is taken only when there is no radius and no gradient;
everything else is the complex branch, and with the macro off the complex branch compiles to a warning
and nothing else:

```c
    /*Most simple case: just a plain rectangle*/
    if(dsc->radius == 0 && (grad_dir == LV_GRAD_DIR_NONE)) {
        blend_dsc.blend_area = &bg_coords;
        blend_dsc.opa = dsc->opa;
        lv_draw_sw_blend(t, &blend_dsc);
        return;
    }

    /*Complex case: there is gradient, mask, or radius*/
#if LV_DRAW_SW_COMPLEX == 0
    LV_LOG_WARN("Can't draw complex rectangle because LV_DRAW_SW_COMPLEX = 0");
#else
```

(`src/draw/sw/lv_draw_sw_fill.c:59-70`.) The whole mask engine is inside the same `#if`, from the top
of `lv_draw_sw_mask.c` (`#if LV_DRAW_SW_COMPLEX`, `src/draw/sw/lv_draw_sw_mask.c:11-12`) to its
`#endif` (`:1314`), including `lv_draw_sw_mask_radius_init()`
(`src/draw/sw/lv_draw_sw_mask.c:280-301`). So `CONFIG_LV_DRAW_SW_COMPLEX=n` (`sdkconfig.sunton`
has it on) would make every tape pack, every hub ring and the label's rounded corners vanish — the
Kconfig says as much: "Adds rounded corners, shadows, skewed lines and arcs. Without it only
rectangles with gradients, images, texts and straight lines can be drawn"
(`managed_components/lvgl__lvgl/Kconfig:546-551`). **It is not a lever; it is a requirement.**

What a radius costs: `lv_draw_sw_fill()` allocates one row of `lv_opa_t` per fill
(`mask_buf = lv_malloc(clipped_w)`, `lv_draw_sw_fill.c:85`) and builds the anti-aliased outline from
the circle cache (`acquire_mask_radius_from_cache()`, `src/draw/sw/lv_draw_sw_mask.c:1227-1280`),
which holds `CONFIG_LV_DRAW_SW_CIRCLE_CACHE_SIZE = 4` circles at "radius * 4 bytes per circle"
(`Kconfig:574-580`, `sdkconfig.sunton`). A pack is up to 400 px across, i.e. a radius of 200
(`cassette_view.c:36`, `PACK_MAX_D 400`), so one uncached radius is a 3 400-step circumference
computation. The packs only change size when the track's permille changes
(`cassette_view.c:158-163`), which is once per `duration_ms/1000` — rarely — so this is a per-track
cost, not a per-frame one.

The image path is the cheap one: with no transform, no radius, no recolor and no colorkey, LVGL takes
the "simplest case" branch and blends the buffer directly (`lv_draw_sw_img.c:274-283`). Text is
glyph blits; the 48 px "B" and the 22 px title are drawn only when the screen or the text changes,
except for the scroll:

```c
static void set_ofs_x_anim(void * obj, int32_t v)
{
    ...
    label->offset.x    = v;
    lv_obj_invalidate(obj);
}
```

(`src/widgets/label/lv_label.c:1476-1482`), driven by an infinite animation whose default speed is
`lv_anim_speed_clamped(40, 300, 10000)` (`lv_label.c:31`). The animation timer runs at
`LV_DEF_REFR_PERIOD` (`src/misc/lv_anim.c:67`), so **the scrolling title invalidates and repaints its
439 px-wide box on every refresh cycle** — the only element in the UI that does. `TITLE_W` is 439
(`cassette_view.c:57`, `:231`).

Put together, the steady-state invalidation per refresh cycle is one label box; per 100 ms it adds two
9 216-pixel image blits, i.e. 36 864 B (ARITHMETIC from `hub_frames.h:12-18` and
`cassette_view.c:147-153`). A full-screen invalidation instead costs 768 000 B painted **plus** 768 000 B
copied by the direct-mode sync (`lv_refr.c:782-784`, §4d) = 1 536 000 B of PSRAM traffic in one cycle
(ARITHMETIC), 25.6 MB/s if it happened every 60 ms, on top of the scan-out's 29.98 MB/s. Which of the
two regimes the board is in when it reports 15 FPS is exactly what the render/flush split and
`redraw_cnt` answer (§9).

---

## 7. What `esp_lvgl_port`'s own documentation says about RGB and PSRAM

- **The buffer configuration is the lever, not the compiler.** "**This is by far the most significant
  setting.** Users are encouraged to focus on correct frame buffer configuration before moving ahead
  with other optimizations"; and the takeaways — "Frame buffer size >25% of the screen does not bring
  relevant performance boost", "Frame buffer size <10% will have severe negative effect"
  (`docs/performance.md:19-32`).
- **PSRAM framebuffers are worse than internal ones, stated without qualification:** "The
  measurements are valid for frame buffer in internal SRAM. Placing the frame buffer into external
  PSRAM will yield worse results" (`docs/performance.md:33`).
- **`avoid_tearing` is documented as a frame-rate cost:** "Use internal RGB buffers as a LVGL draw
  buffers to avoid tearing effect, enabling this option requires over two LCD buffers and may reduce
  the frame rate" (`include/esp_lvgl_port_disp.h:82-83`).
- **The PSRAM-canvas recipe is the non-tearing alternative**: a full-size buffer in PSRAM with a small
  DMA-capable internal `trans_size` buffer beside it (`.buffer_size = DISP_WIDTH*DISP_HEIGHT, // in
  PSRAM, not DMA-capable`, `.trans_size = size, // in SRAM, DMA-capable`,
  `.flags.buff_spiram = true, .buff_dma = false`, `README.md:329-343`).
- **The performance monitor needs three config lines** — `CONFIG_LV_USE_OBSERVER`,
  `CONFIG_LV_USE_SYSMON`, `CONFIG_LV_USE_PERF_MONITOR` (`README.md:412-420`), all present here
  (`sdkconfig.sunton`).

---

## 8. Ranked levers

Effect is stated against the quantity the FPS number actually measures — the refresh cycle time
(§1) — and "confidence" is confidence that the *mechanism* is as described, which is what the
citations establish. No single lever in this table has been measured on the board; the last column is
what would settle each one. The reference configuration is the baseline above.

| # | Lever | Exact change | Expected effect | Confidence | What must be measured on the board |
|---|---|---|---|---|---|
| 1 | Refresh task placement | `.task_affinity = 1` in `k_port`, priority left at 4 (`components/app/screen_display/panel_rgb.c:46-52`) | **big if preemption is the missing term**: moves the task off the core Wi-Fi is pinned to (`sdkconfig.sunton`) onto the core whose encoder at 6 leaves 55.4 % free; it cannot starve the encoder (4 < 6), and lwIP at 18 is unpinned so it preempts on either core | high on the mechanism (`panel_rgb.c:46-52`, `sdkconfig.sunton`); **low** on the size until measured | encoder % unchanged and no stream underrun (telemetry), plus `refr_cnt`/`redraw_cnt` and `render \| flush` before/after |
| 2 | Refresh period | `CONFIG_LV_DEF_REFR_PERIOD=16` in `sdkconfig.defaults`, or `lv_timer_set_period(lv_display_get_refr_timer(disp), 16)` after `lvgl_port_add_disp_rgb()` | **big**: raises the sysmon cap from 30 to 62 (`lv_sysmon.c:303-305`) and lets the panel's 39.02 fps be reached; cannot shorten a cycle that already exceeds 16 ms | high — the period is the timer's period (`lv_display.c:125`) and the sysmon reads it back | FPS and the `render \| flush` split before/after; the same board/panel config runs 16 ms in SolidGL (`solidgl_platform.c:393-397`) |
| 3 | Render/flush split | `CONFIG_LV_USE_PERF_MONITOR_LOG_MODE=y` for one run | **diagnostic, not a gain** — it is what decides whether #4 or #5–#7 is the lever | high — the log prints `refr_cnt`, `redraw_cnt`, render and flush separately (`lv_sysmon.c:358-375`) | 20 consecutive `sysmon:` lines while the reels turn |
| 4 | Compiler level | `CONFIG_COMPILER_OPTIMIZATION_PERF=y`; delete the stale `CONFIG_COMPILER_OPTIMIZATION_DEBUG=y` | **medium**: -Og → -O2 over all of LVGL, the driver and the UI (`Kconfig:432-439`); the one published measurement on this board class moved LVGL 9.2 by 0 fps (`performance.md:107-109`) | high that the code changes; low on the size of the effect for *this* UI | FPS + render ms before/after |
| 5 | Panel pclk | `pclkHz` 16 → 20 or 24 MHz in the profile and `PCLK_HZ` in `panel_rgb.c:31` | **medium to big**: the panel frame drops to 20.500 / 17.083 ms, i.e. 48.78 / 58.54 fps instead of 39.02 (`esp_lcd_panel_rgb.h:27-67`), which also shortens the vsync wait of #6 | high on the arithmetic; **none** on whether this panel accepts a higher refresh — the profile's 16 MHz is the measured value | panel image stability, and FPS; PSRAM read rises to 37.46–44.96 MB/s (§2) |
| 6 | The vsync handshake | no config change: it is `avoid_tearing` (`panel_rgb.c:122`); removing it trades the wait for a CPU copy per flush (§3) | **big only if flush dominates**; it is the term that quantises a cycle to 25.625 ms steps and turns 1 ms of extra render into 25.6 ms of extra cycle | high — two takes at `esp_lvgl_port_disp.c:754-755`, one give per frame at `esp_lcd_panel_rgb.c:1389`, semaphore max 1 at `:379` | flush ms in the sysmon line: near 25.6 ms means the wait |
| 7 | Draw cost of the UI | shorten the scroll (`lv_anim_set_duration`/`lv_obj_set_style_anim_duration`, or a `LV_LABEL_LONG_DOT` title), and avoid full-screen invalidation | **medium**: the scrolling label is the only per-cycle invalidation (`lv_label.c:1476-1482`); a full-screen cycle costs 1 536 000 B of PSRAM traffic instead of ~0.1 MB | high on mechanism, medium on size | render ms in the sysmon line; `redraw_cnt` vs `refr_cnt` |
| 8 | PSRAM clock | `CONFIG_SPIRAM_SPEED_120M=y` + `CONFIG_IDF_EXPERIMENTAL_FEATURES=y` | **medium**: 160 → 240 MB/s of theoretical PSRAM bandwidth against a 29.98 MB/s scan-out; carries a documented ±20 °C random-crash mode on octal (`Kconfig.spiram:89-102`) | high that it builds with an 80 MHz DIO flash (§5) | hours of soak at temperature extremes; FPS |
| 9 | Data cache line | `CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y` | **small to medium, unknown**: allowed with no dependency (`Kconfig.cache:106-119`); changes fill granularity for LVGL's write-back traffic, and nothing for the DMA | high that it is permitted; **no** source states an effect | FPS + render ms; `esp_cache_msync` cost is not separately visible, so A/B only |
| 10 | Leave direct mode: partial draw buffers over one PSRAM FB | `num_fbs = 1`, `direct_mode = false`, `buffer_size = 800 * 40`, `double_buffer = true`, `flags.buff_dma = true`, `buff_spiram = false`; keep `bounce_buffer_size_px = 0` at 16 MHz | **medium**: removes the vsync wait and keeps LVGL's drawing out of the scanned FB, at the price of one per-strip copy into the FB (proportional to the dirty area) and 64 000 B of internal RAM per strip | high on the mechanism (§4a, §3); medium on the net gain | internal free/largest block on the board with the UI up; FPS and `render \| flush`; whether copy-induced tearing is visible on the moving hubs |
| 11 | Two software draw units | `CONFIG_LV_OS_FREERTOS=y` first, then `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2` | **unknown, probably small here**: the UI has few draw tasks and the S3's second core carries the encoder | **low** — the combination compiles only with an OS layer (`lv_draw_sw.c:31-33`) and nothing in `esp_lvgl_port` tests it | whether the port still owns LVGL correctly, plus a second stack's internal cost |
| 12 | `LV_ATTRIBUTE_FAST_MEM_USE_IRAM` | — | **zero**: the macro is empty in 9.6 and its Kconfig symbol has no consumer (`lv_conf_internal.h:4642-4648`, `:5216-5218`) | high — proved from source | nothing to measure; it cannot change an object file |

**Levers 1, 2 and 4 have now been tried together, and the result was worse than the baseline**
(§10, "Result"). Their expected effects above remain unproven on this board, and the combination says
nothing about any one of them. Test them alone, against the 33 ms / `-Og` baseline.

A bounce buffer is **not** part of lever 10 at 16 MHz. `bounce_buffer_size_px` exists to let the DMA read
from internal SRAM instead of PSRAM (§4b); it buys a higher pclk and costs a CPU `memcpy` of the whole
frame every frame, 29.98 MB/s at 16 MHz (`esp_lcd_panel_rgb.c:1042-1057`). It becomes relevant only if
lever 5 raises the pclk to a rate the direct-PSRAM path cannot hold. `esp_lvgl_port` has the matching
switch, `rgb_cfg.flags.bb_mode` (`include/esp_lvgl_port_disp.h:81`, selected at
`src/lvgl9/esp_lvgl_port_disp.c:216`), but it only changes which ISR callback the port waits on; the panel
config still has to set `bounce_buffer_size_px` itself. **Tried and reverted:** `bb_mode` with ten-line
buffers, on top of an 18 MHz pclk in the tree, was reported worse and removed the same day (§10,
"Result"); that run does not separate the mode from the clock.

---

## 9. What is proved from source, and what only the board can settle

### Proved, with the proof

| Claim | How it is proved |
|---|---|
| The FPS number counts refresh cycles, including idle ones | `refr_cnt++` on `LV_EVENT_REFR_READY` (`lv_sysmon.c:252-255`); the event is sent after the `inv_p == 0` early exit (`lv_refr.c:433`, `:452-458`); documented in `lv_event.h:113-114` |
| It cannot exceed 30 in this configuration | `LV_MIN(fps, 1000 / disp_refr_period)` (`lv_sysmon.c:303-305`) with period 33 (`sdkconfig.sunton`, `Kconfig:353-357`) |
| The timer is not paused while the monitor is on | `#if !LV_USE_PERF_MONITOR` around `lv_timer_pause` (`lv_refr.c:394-397`), `CONFIG_LV_USE_PERF_MONITOR=y` (`sdkconfig.sunton`) |
| A displayed 15 means a mean cycle of 62.5–66.7 ms | `fps = floor(1000 × refr_cnt / elapsed)` (`lv_sysmon.c:303`), so 15 implies `62.5 < elapsed / refr_cnt ≤ 66.7` ms whatever the window |
| The panel frame is 25.625 ms / 39.02 fps at 16 MHz | 820 × 500 = 410 000 pclk per frame (`esp_lcd_panel_rgb.h:27-67`) ÷ 16 MHz (`panel_rgb.c:31`, profile "timings") |
| The panel, not LVGL, is the faster of the two today | 25.625 ms < 33 ms |
| The refresh task runs at priority 4 on core 0, under the network stack | `.task_priority = 4`, `.task_affinity = 0` (`panel_rgb.c:46-52`), created pinned (`esp_lvgl_port.c:90-93`); Wi-Fi pinned to core 0, esp_timer on CPU0, lwIP at priority 18 with no affinity (all in `sdkconfig.sunton`) |
| The encoder is on core 1 at priority 6, at 44.6 % of one core | `mini_icecast.c:597-598`; HTTP workers and accept at priority 5 on core 1 (`http_server.c:426-431`); measured share `docs/adr/0004-ui-memory-is-external.md:42` |
| The current flush copies no pixels | the draw buffer is inside an FB, so the driver takes the `cur_fb_index` branch (`esp_lcd_panel_rgb.c:724-730`, `:758-786`) |
| The current flush waits for a vsync, every cycle | two takes around a max-count-1 counting semaphore (`esp_lvgl_port_disp.c:379`, `:754-755`) and one give per `LCD_LL_EVENT_VSYNC_END` (`esp_lcd_panel_rgb.c:1387-1393`) |
| The wait is 0–25.625 ms and quantises the cycle | one give per frame, one pending give maximum, so a flush always waits for a post-call vsync (§3) |
| Direct double-buffered mode copies every dirty area once more | `refr_sync_areas()` → `lv_draw_buf_copy()` per area (`lv_refr.c:677-684`, `:782-784`) → per-line `lv_memcpy` (`lv_draw_buf.c:628-684`) |
| `num_fbs = 3` is unreachable through this port | the port asks for 2 (`esp_lvgl_port_disp.c:367`) and never sets a third buffer (`lv_display.h:298-302`) |
| `num_fbs = 0` means one framebuffer, not none | `rgb_lcd.rst:16`, `esp_lcd_panel_rgb.h:149` |
| The bounce buffer re-copies the entire frame every frame | `bounce_pos_px` advances by `bb_size` and wraps at `fb_size` (`esp_lcd_panel_rgb.c:1042-1057`) |
| A radius fill needs `LV_DRAW_SW_COMPLEX` | `lv_draw_sw_fill.c:59-70`; the whole mask engine is inside the macro (`lv_draw_sw_mask.c:11-12`, `:1314`) |
| `LV_ATTRIBUTE_FAST_MEM_USE_IRAM` is dead in 9.6 | no consumer of the symbol; `LV_ATTRIBUTE_FAST_MEM` expands to nothing (`lv_conf_internal.h:4642-4648`, `:5216-5218`) |
| Two draw units require `LV_USE_OS != LV_OS_NONE` | `Kconfig:526-533`, `#error` at `lv_draw_sw.c:31-33`; the symbol is absent from `sdkconfig.sunton` |
| 120 MHz octal PSRAM builds with an 80 MHz DIO flash | 120 MHz octal PSRAM expects a 240 MHz core clock (`mspi_timing_tuning_configs.h:136-139`), an 80 MHz STR flash needs no tuning at all (`:47-56`, `:58-61`), so the divisibility test `240 % 80 == 0` is the one that runs (`:159-162`) |
| 120 MHz flash with 80 MHz octal PSRAM does not | both need tuning → equality assert `240 == 160` fails (`mspi_timing_tuning_configs.h:136-139`, `:122-125`, `:149-150`) |
| The hub frames are read from PSRAM at draw time | `.rodata.embedded` at `0x3c1276fc`, `0x5a004` B (`build_sunton/sd_stream_server.map:93653-93659`) inside `0x3C000000–0x3E000000` (`soc.h:173-174`) |

### Only the board can settle these

1. **In what state the 15 FPS was observed.** Streaming with a client attached, idle after boot, or
   during the serial-dump build are three different loads, and this note does not know which produced
   the reading. The split has to be read in the same state as the observation, or the comparison is
   between two different systems.
2. **Which term dominates the 62.5–66.7 ms cycle.** The FPS number cannot say. The sysmon's second line
   (`render | flush`, `lv_sysmon.c:390-397`) and `LV_USE_PERF_MONITOR_LOG_MODE` can, in one run.
3. **Whether `redraw_cnt` equals `refr_cnt`.** If cycles complete without drawing, the UI is
   invalidation-starved or the cycles are idle-limited; if they always draw, every cycle is paying
   render + wait. `redraw_cnt` is printed in log mode (`lv_sysmon.c:365`).
4. **Why 62.5–66.7 ms rather than 33 or 51.25.** A cycle cannot start before the timer's 33 ms and
   cannot end except at a vsync (§3), so the decompositions that land in the window are a render of
   ~37–41 ms plus one 25.625 ms wait (`37 + 25.6 = 62.6`, ARITHMETIC), or a render of ~62–66 ms with a
   wait near zero. The `render` field settles which: it is wall time between `LV_EVENT_RENDER_START`
   and `LV_EVENT_RENDER_READY` (`lv_sysmon.c:256-264`), so a render stretched by preemption — the
   refresh task sits at priority 4 on core 0, under Wi-Fi and under lwIP at 18 (§5) — is counted as
   render time, and shows up as a render larger than the visible draw work explains. The affinity move
   was tried, but together with the 16 ms period and `-O2`, and the combination was reverted as worse
   (§10, "Result"); it has not been tested alone. Twenty log lines in the baseline settle it.
5. **Whether the panel accepts 20 or 24 MHz.** Nothing in IDF documents a maximum for this panel; the
   profile records 16 MHz as measured. This is a datasheet/stability question, not a build question.
6. **The internal-RAM budget on the Sunton board with the UI up.** The only census in this
   repository is the devkit's (157 787 B free, 81 920 B largest,
   `radio-player-memory-and-lvgl.md:743-746`); the panel build has different statics and no census.
   Lever #10 depends on that number.
7. **Whether LV_USE_OS=FREERTOS coexists with `esp_lvgl_port`.** By inspection it should (the port uses
   its own mutex, `esp_lvgl_port.c:77`), but nothing in the component says so and this note did not
   build it.
8. **The effect of a 64 B data cache line.** No source states one; only an A/B on the board does.

---

## 10. What to try first

**Step 0 — read the split, before changing anything.** This costs one rebuild and no logic change.

```
# sdkconfig.defaults — measurement only: hides the on-screen label and prints one line per 300 ms
CONFIG_LV_USE_PERF_MONITOR_LOG_MODE=y
```

then read the `sysmon:` lines (`lv_sysmon.c:358-375`). Three readings point at different steps:

- `flush ≈ 25` (ms) with a small `render` → the vsync handshake is the cycle (§3): step 4 is the lever;
  steps 1 and 2 change when the cycle starts, not how long the wait is.
- `render` in the tens of ms with a small `flush` → draw cost: step 5.
- `refr_cnt` growing while `redraw_cnt` does not → the screen is idle and the number is the timer's
  33 ms cadence: step 2 is the direct fix.

No rebuild is needed for the first reading if the label is still on screen: its second line is already
`total ms (render | flush)` (`lv_sysmon.c:390-397`). The log mode adds `refr_cnt` and `redraw_cnt`
(`lv_sysmon.c:365`) and removes the label's own repaints from the measurement.

**Step 1 — move the refresh task to core 1, one line, priority unchanged.** In `components/app/screen_display/panel_rgb.c:49`:

```c
    .task_affinity = 1,   /* was 0; core 1's encoder runs at priority 6, this task at 4 */
```

This is what the same file had at `8c8d6e7` and what SolidGL keeps on this board
(`solidgl_platform.c:312`). Confirm three things with the telemetry that already exists: the encoder's
share is still 44.6 % of core 1 (`docs/adr/0004-ui-memory-is-external.md:42`), the stream does not
underrun, and the sysmon's `refr_cnt`/`redraw_cnt` and `render | flush` split have changed. The HTTP
workers at priority 5 will preempt the refresh briefly while they are awake; that is the intended order.

**Step 2 — the refresh period, a second one-line A/B, reversible on its own.** In
`components/app/screen_display/panel_rgb.c`, after `lvgl_port_add_disp_rgb()` succeeds (`panel_rgb.c:124-128`):

```c
    lv_timer_t *refr = lv_display_get_refr_timer(disp);
    if (refr) lv_timer_set_period(refr, 16);   /* SolidGL, same board: solidgl_platform.c:393-397 */
```

This is the same panel at the same 16 MHz with a 16 ms period in the reference firmware, and it lifts
the sysmon cap from 30 to 62 (`lv_sysmon.c:303-305`). It cannot help if the cycle is already longer
than 16 ms — that is what step 0 tells you.

**Step 3 — the compiler level, which is free of behavioural risk.**

```
# sdkconfig.defaults
CONFIG_COMPILER_OPTIMIZATION_PERF=y
```

`CONFIG_COMPILER_OPTIMIZATION_DEBUG=y` must be gone from the saved `sdkconfig.sunton`
or the choice will stay on Debug; confirm `CONFIG_COMPILER_OPTIMIZATION_DEBUG` in
`build_sunton/config/sdkconfig.h` after
reconfiguring.

**Step 4 — if flush dominates, shorten the wait by shortening the frame.** Either raise `pclkHz`
(20 MHz → 20.500 ms → 48.78 fps; 24 MHz → 17.083 ms → 58.54 fps, §2) after confirming the panel
accepts it, or leave `avoid_tearing` for the partial/internal arrangement of §4a — which removes the
wait but adds a full-frame bounce copy at 29.98 MB/s and needs 64 000 B of internal RAM per buffer.

**Step 5 — if render dominates, cut the per-cycle drawing.** The scrolling title is the only thing
that invalidates on every cycle (`lv_label.c:1476-1482`); a non-scrolling long mode removes a
439 px-wide repaint and its double-buffer copy from every cycle. A full-screen invalidation costs
1 536 000 B of PSRAM traffic per cycle (§6) — find and remove whatever causes one, if anything does.

**Do not bother with:** `CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM` (no-op, §5),
`CONFIG_LV_DRAW_SW_COMPLEX=n` (deletes every rounded shape, §6), and `CONFIG_LV_DRAW_SW_ASM_CUSTOM`
(the S3 SIMD blenders in `esp_lvgl_port` are compiled only for LVGL 9.1.x, §5).

### Result: tried on the board and reverted, 2026-10-17

Steps 1–3 were applied **together** and flashed once: `.task_affinity = 1`, the refresh timer at 16 ms,
and `-O2` (`CONFIG_COMPILER_OPTIMIZATION_PERF=y`). The owner reports the result as worse than the
baseline of 15 FPS — the count and the smoothness both — and the tree was reverted the same day to the
baseline configuration recorded above: refresh task on core 0 at the 33 ms period, compiled at `-Og`.

What this note can and cannot say:

- **It cannot attribute the regression.** The three changes were applied as one, and no reading of the
  `render | flush` split was taken in the new configuration before or after. The result is a fact about
  the combination, not about any one lever.
- **Two of the three plausibly act against the goal.** The 16 ms period asks for twice the refreshes on a
  display whose cycle was already ~66 ms, so every extra cycle pays the vsync handshake of §3 again. The
  affinity move puts the refresh task under the HTTP workers at priority 5 on core 1, which preempt the
  drawing before it reaches its vsync; and it shares that core with the encoder, which is the load the
  original comment in `panel_rgb.c` was avoiding.
- **The order in §10 was wrong in one respect.** It should have read the split first (step 0), then
  changed one thing at a time, keeping the 33 ms period and `-Og` as controls. That is the method this
  result argues for, and the one the next attempt should use.

**Second attempt, same day: bounce-buffer mode.** `bb_mode` was set with
`.bounce_buffer_size_px = 10 * PANEL_W` — two 16 000 B buffers in internal RAM — on top of an 18 MHz
pclk that was already in the working tree, so that run was again more than one variable. It was reported
worse as well, and reverted: `bounce_buffer_size_px = 0`, `bb_mode` off, pclk back to 16 MHz. The
mechanism predicted little: in this mode the same 30–36 MB/s of framebuffer data still moves from PSRAM,
but now through a CPU `memcpy` in the DMA EOF ISR rather than by the DMA, so the change adds work to the
CPU rather than removing it; the mode's documented purpose is to allow a higher pclk, not to raise the
frame rate at a fixed one (`rgb_lcd.rst:243`). Whether the pclk raise alone helps this panel is still
untested — it is the one variable from both attempts that was never run by itself, and the profile's
16 MHz is the only value the board has measured.

`CONFIG_LV_USE_PERF_MONITOR_LOG_MODE` was deliberately **not** set, because it hides the on-screen label
the owner reads. The `render | flush` split is on the label's second line, so the first measurement can
be made without a rebuild.

---

## 11. The artefacts cited

Local sources, by path:

- This project: `components/app/screen_display/panel_rgb.c`, `components/app/screen_display/screen_display.c`, `components/app/screen_display/cassette_view.c`, `components/app/screen_display/include/screen_display.h`,
  `components/app/screen_display/include/hub_frames.h`, `components/app/screen_display/CMakeLists.txt`, `components/app/screen_display/assets/hub_frames.bin`,
  `boards/sunton-8048s050c.json`, `sdkconfig.defaults`, `sdkconfig.defaults.sunton`,
  `sdkconfig.sunton`, `build_sunton/config/sdkconfig.h`, `build_sunton/CMakeCache.txt`,
  `build_sunton/sd_stream_server.map`, `docs/research/radio-player-memory-and-lvgl.md`.
- LVGL 9.6.0: `managed_components/lvgl__lvgl/` — `Kconfig`, `lv_conf_template.h`,
  `include/lvgl/config/lv_conf_internal.h`, `include/lvgl/display/lv_display.h`,
  `include/lvgl/core/lv_event.h`, `src/display/lv_display.c`, `src/core/lv_refr.c`,
  `src/misc/lv_timer.c`, `src/osal/lv_os_none.c`, `src/debugging/sysmon/lv_sysmon.c`,
  `src/draw/lv_draw_buf.c`, `src/draw/sw/lv_draw_sw.c`, `src/draw/sw/lv_draw_sw_fill.c`,
  `src/draw/sw/lv_draw_sw_mask.c`, `src/draw/sw/lv_draw_sw_img.c`,
  `src/widgets/label/lv_label.c`, `src/misc/lv_color_op.c`, `src/misc/lv_math.c`,
  `src/draw/lv_draw_line.c`. Upstream: `https://github.com/lvgl/lvgl/tree/v9.6.0`.
- `esp_lvgl_port` 2.9.0: `managed_components/espressif__esp_lvgl_port/` — `Kconfig`, `README.md`,
  `docs/performance.md`, `include/esp_lvgl_port.h`, `include/esp_lvgl_port_disp.h`,
  `src/lvgl9/esp_lvgl_port.c`, `src/lvgl9/esp_lvgl_port_disp.c`. Upstream home is
  `https://github.com/espressif/esp-bsp/tree/master/components/esp_lvgl_port`.
- ESP-IDF v6.1 (`/Users/pedrocasaubon/esp/esp-idf-6.1`): `Kconfig`,
  `components/esp_lcd/rgb/esp_lcd_panel_rgb.c`, `components/esp_lcd/rgb/rgb_lcd_rotation_sw.h`,
  `components/esp_lcd/rgb/include/esp_lcd_panel_rgb.h`,
  `docs/en/api-reference/peripherals/lcd/rgb_lcd.rst`,
  `components/esp_system/port/soc/esp32s3/Kconfig.cache`,
  `components/esp_psram/esp32s3/Kconfig.spiram`,
  `components/esp_hw_support/mspi/mspi_timing_tuning/port/esp32s3/mspi_timing_tuning_configs.h`,
  `components/esptool_py/Kconfig.projbuild`,
  `components/soc/esp32s3/include/soc/soc.h`.
  Rendered equivalents: `…/en/v6.1/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html`.
- Reference firmwares: `~/Documents/SolidGL/firmware/components/solidgl_platform/src/solidgl_platform.c`,
  `~/Documents/SolidGL/build/generated/solidgl-profile-sunton-8048s050c.h`,
  `~/Documents/SolidGL/boards/sunton-8048s050c.json`;
  `~/Documents/Sunton5_IDF/components/Sunton5_IDF/src/sunton5_display.c`,
  `~/Documents/Sunton5_IDF/components/Sunton5_IDF/Kconfig`,
  `~/Documents/Sunton5_IDF/examples/wifi_example/sdkconfig`.
