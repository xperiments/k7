# The UI's memory is external, and the encoder's state is not

An LVGL interface is coming on top of this firmware, and the board had already shown
what happens when internal RAM runs out at the wrong moment: `esp_wifi_init()` needs
about 25 KB of *contiguous* DMA-capable internal memory, and when the encoder's 100 KB
of state was allocated first, Wi-Fi could not get it — `wifi:malloc buffer fail`, then
an abort and a reboot loop. Before the UI arrives, its memory has to be a decision
rather than a hope.

## Considered Options

- **Keep `SPIRAM_MALLOC_ALWAYSINTERNAL=131072` and give LVGL a pool.** Preserves the
  encoder's internal state (41.4 % of one core rather than 44.5 %) but leaves the
  largest single free internal block at 18 432 B forever. That block, not the free
  total, is what decides whether a contiguous request can be met — it is what broke
  Wi-Fi.
- **Set the threshold to 0 so every plain allocation prefers PSRAM.** Would keep LVGL
  out of internal RAM by policy, but it changes the placement of every small
  allocation in the system, including subsystems that quietly expect internal memory,
  for a reason that has nothing to do with them.
- **Lower the threshold to 4096 and give LVGL an explicit PSRAM allocator.** Chosen.
  The threshold moves the encoder's 95 KB of state to PSRAM and lifts the largest
  internal block to 81 920 B; the allocator keeps every LVGL allocation out of
  internal RAM without touching anything else's placement.

Note that lowering the threshold alone is *not* enough, and the reasoning is worth
recording because it is counter-intuitive: `heap_caps_malloc_default()`
(`components/heap/heap_caps.c`) prefers internal memory for anything **at or below**
the threshold. At 4096, LVGL's widgets — tens to hundreds of bytes each — would still
ask internal first and fall through to PSRAM only when internal ran out, silently.
That is the failure mode, not a fix for it. The custom allocator is what makes the UI's
placement deterministic.

## Consequences

`main/ui_fit.c` implements LVGL's custom backend over
`heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_INTERNAL)`, counts what
LVGL holds, and runs a startup fit check that creates a representative screen, reports
what it costs, and frees it. Measured on the board: 123 objects hold 54 276 B in 956
blocks (peak 56 312 B), the 25 600 B draw buffer is explicitly PSRAM, and internal free
memory is **byte-identical before, during and after** — 157 787 B free with an
81 920 B largest block. The encoder's cost is unchanged at 44.6 %, and the screen's
memory returns when it is freed (LVGL keeps 6 188 B of theme and style state, not the
screen's 54 KB).

Costs and constraints, all measured:

- **The encoder state is in PSRAM now, and that costs 7.6 %** — 41.4 % to 44.5 % of one
  core. Raising `SPIRAM_MALLOC_ALWAYSINTERNAL` back to 131072 restores it if encoder
  cost ever matters more than headroom.
- **The app partition is 4 MB** (`partitions.csv`) rather than the 1.5 MB single-app
  layout, because LVGL takes the image from 1.04 MB to 1.28 MB. 68 % is free.
- **The UI must stay off core 1.** The encoder runs there at priority 6; an interface
  task belongs on core 0 at a lower priority, as `docs/research/radio-player-memory-and-lvgl.md`
  sets out. The fit check runs in `app_main` on core 0 and does not disturb the
  encoder's measurements.
- **LVGL is a fetched dependency, not vendored** (`main/idf_component.yml`, pinned by
  the committed `dependencies.lock`), and it is an application-layer dependency: the
  host tier still compiles only the server components and never sees it.

`main/ui_fit.c` is groundwork and says so. When the real interface arrives it replaces
that file, keeping the allocator and the PSRAM draw-buffer rule, and the fit check goes
with it.
