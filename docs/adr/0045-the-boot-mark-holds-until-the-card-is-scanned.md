# The boot mark holds until the card is scanned

**Amended (a panel bring-up wait, not a delay to hide a frame):** stage one now holds for
`APP_UI_PANEL_SETTLE_MS` (250 ms by default) between `backlight_hold_off()` and the peripheral's
creation, because this panel has no reset line - the RGB driver's `reset()` and `init()` send it
no commands and the profile wires no display-enable pin - so its own power-on reset is the only
one it gets, and the first clock must not arrive while it is still starting up. This does not
contradict "the fix is the order, not a delay" below: that rejected a delay used to hide an
unpainted buffer, which no amount of waiting fixes because the buffer stays unpainted. This waits
for the panel's supply before the panel exists at all, with the light already off. The measured
boot times below move by the configured settle.

Boot opened on a white flash that turned black. Neither frame was intended, and the two had
different causes.

The black one was the cassette's page (`k_col_bg`, `#111111`) arriving - seconds late, because
the screen used to come up **after** `app_services_start()`, which joins the network and scans
the card first. The white one was the panel itself: the RGB peripheral starts scanning its two
PSRAM framebuffers the moment the driver is initialised, PSRAM powers up reading white, and
GPIO 2 - the backlight - is not ours until `backlight_start()` configures it, so on this board
the pin floats high and lights the panel over exactly that. The owner's ask was concrete: the
K7 logo from the `xtape` icon font at 128 px in the primary colour, centred on black, and held
until the card has been scanned.

## Decision

- **The screen comes up in two calls, and the first one runs before the services.**
  `screen_display_boot()` brings the panel and LVGL up and paints the mark;
  `screen_display_start()` builds the cassette over it and starts the task that keeps it
  current. The appliance calls the first before `app_services_start()` and the second after
  it - after the network, the card mount and the scan - so the mark is on the panel for
  exactly as long as there is nothing to show. `screen_display.h` states the order; there is
  no third entry point.
- **The panel is never lit over an unpainted buffer.** `backlight_hold_off()` gives the pin to
  LEDC at duty 0 before the panel exists (a plain GPIO drive would have made the channel's
  later pin routing report a conflict); `panel_rgb_start()` clears both framebuffers
  (`panel_rgb_clear()`) and returns with the light still off; the screen paints the mark into
  both with the same two forced refreshes the capture path uses, and only then calls
  `backlight_start()`. The fix is the order, not a delay: if the light cannot be on before a
  picture, it does not matter how long boot takes.
- **The mark is a glyph, not an asset.** It is `CASSETTE_ICON_K7LOGO` (U+F00F) in
  `xtape_logo_128`, `k_col_primary` on `k_col_bg`, centred by LVGL's own label. The icon font
  already carries the mark, the palette already carries the two colours, and the style colours
  a glyph. A second copy of the same design as a bitmap would need its own blob, its own
  tinting and its own conversion step to say the same thing.
- **The mark's font is the mark's alone, and its size is 128 px of ink, not a 128 px em.** The
  cut carries one glyph (`--range 0xF00F`). It is generated at `--size 314`, because an icon
  font's size is its em box and this emblem's ink fills about 42 % of it: a 64 px em drew a
  50x27 mark, which the owner's first look read as "too small", and 128 px of em would still
  be a 100x53 mark. 314 px is the em that puts the ink at 238x128 - the 128 px the owner asked
  for on the second look. The other nineteen glyphs at that em would cost far more flash for a
  size no part asks for; the range in `package.json` is where a second one goes when a part
  does.
- **LVGL's large-font format is on, and the reason is in the descriptor.** A standard
  `lv_font_fmt_txt_glyph_dsc_t` stores the advance in 12 bits - 255.9 px - and a 314 px em is
  5024/16, so the generated cut cannot compile without `CONFIG_LV_FONT_FMT_TXT_LARGE=y`. It is
  set in `sdkconfig.defaults`, which every defaults layer in the tree inherits; the generated
  font's own `#error` names the option, so a config that loses it fails at the file that needs
  it rather than at runtime. The option enlarges the glyph descriptors of **every** fmt_txt
  font, which is the real cost (below), not the mark.
- **The cut is mark to picture.** `screen_display_start()` creates the cassette first and
  takes the mark down second, so no frame between the two is empty.
- **The backlight-test build gets no mark.** `CONFIG_APP_UI_BACKLIGHT_TEST` owns the whole
  screen with its own white page and steps the light itself; a logo drawn over it would be a
  second thing to subtract from the comparison.

## Considered and rejected

- **Keep the screen last and only fix the flash.** That is the smaller diff, and it cannot
  satisfy the ask: the card is scanned before the screen exists, so the mark would never be
  visible while it ran.
- **A screen for the boot, a screen for the cassette.** Two active screens would leave the
  display's default page reachable and the capture aiming at whichever is active. The mark is
  a child of the one screen the cassette will use.
- **A boot hook inside `app_services_start()`.** It would put the screen in the composition's
  configuration, where the composition's job is the order of the services. The appliance is
  where policy lives; it now orders the two screen calls around the one services call.
- **An asynchronous first scan, so the screen could start later.** The boot generation is what
  `/api/library`, the file surface and the broadcast all start on; making it late would mean
  answering "no library" for a few seconds and deciding which generation the first Track comes
  from. That is a larger change than the one asked for, and it moves the problem rather than
  fixing the order.
- **A fade or a hold on the mark.** The request is that it stays until the scan is done, and
  the scan is the end condition. An animation would add a duration the boot does not have.

## Consequences

- **The mark is up for the network as well as the scan.** Wi-Fi association is started inside
  `app_services_start()`, before the card is touched, so the logo covers it too; on this board
  that is the larger part of the wait. A cold boot now reads as: black, the mark, the
  cassette.
- **Stage one runs before `esp_wifi_init()`, and that is a memory order change.** The LVGL
  port's 16 KB internal stack is the one block that moves, where the rule had been network
  before anything internal (ADR 0004). Measured on the board: internal free 230 303 B
  (largest 131 072 B) before the panel and 208 179 B (largest 118 784 B) after it, still far
  above the ~25 KB contiguous `esp_wifi_init()` asks for - Wi-Fi initialised and associated
  with no allocation warning after it. The boot screen logs that pair so the margin is read
  rather than assumed; if it ever comes close, the answer is the port's stack, not the order.
- **The card's scan lost no PSRAM to speak of.** The two 768 KB framebuffers are claimed
  before the arena now, but Wi-Fi's own allocations happen at the same time and the measured
  scan reports `psram=5149008 -> 5130064 B` free with an 82-track card (146 ms), essentially
  the 5.2 MB the earlier boot measured - comfortably above the 4 MiB `library_max_bytes`
  ceiling, and the same arrangement a rescan has always had.
- **The boot is `APP_UI_PANEL_SETTLE_MS` longer before anything is drawn.** The `panel up` line
  in the log below (1226 ms) moves by the configured value, 250 ms by default, and nothing after
  it changes. The knob exists because the panel's power-on reset is the only reset it gets; the
  failure it is aimed at is a cold start landing on a shifted picture, which a warm reboot never
  shows because the panel never lost power. If a cold start still lands displaced with a large
  value set, the supply is not the cause and the lever is the driver's frame restart instead.
- **The screen's HTTP diagnostics have a picture from boot.** `/api/screen.bmp` used to
  answer 503 until the cassette existed; it now serves the mark. Nothing else about the route
  changes.
- **Flash.** Measured: the image with the 64 px mark was 1 746 288 B and with the 128 px mark
  and the large-font format 1 768 448 B - 22 KB, of which the glyph is 15 KB (238x128 at 4 bpp)
  and the rest is the wider descriptor on every other font. One part's mark, not a whole icon
  set, and the 4 MB partition stays 58 % free.
- **The mark's colours are the cassette's.** `#111111` and `#F15A22` - the same two the
  controls header wears and the same black the cassette's page will paint a moment later, so
  the cut does not change shade.

**Status:** accepted, built for both boards and booted on the Sunton, 2026-10-30. The log shows
the intended order - `panel up` (1226 ms), `backlight` (1392), `boot screen ... the mark`
(1393), Wi-Fi init and association, `library ... scan=142 ms` (1819), the cassette (2295) - and
no LEDC pin conflict. The mark's size went from 50x27 (a 64 px em, read as too small) to
238x128 (a 314 px em, the large-font format on). The picture itself is the one thing a log
cannot check: the size and the absence of a white frame before the mark are confirmed by eye,
on the panel.
