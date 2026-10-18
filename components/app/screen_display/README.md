# screen_display

The screen: the panel (or the headless framebuffer), LVGL's PSRAM allocator and the
cassette view. The public interface is `include/screen_display.h` —
`screen_display_start()` and nothing else. Everything else is private:

- `screen_display.c` — start-up, the draw buffer, the serial dump and the task that keeps
  the cassette current.
- `lvgl_mem.c` — the `LV_USE_CUSTOM_MALLOC` backend, PSRAM first (ADR 0004).
- `cassette_view.c` — the LVGL objects and layout; the arithmetic is
  `components/portable/cassette`.
- `panel_rgb.c` — the Sunton panel adapter, compiled only for that board.

The panel and the headless path are two adapters behind the same interface, selected by
`CONFIG_APP_BOARD_SUNTON_8048S050C` in this component's CMake. A third panel promotes
that selection to a `display_port` seam without changing `screen_display_start()`.
