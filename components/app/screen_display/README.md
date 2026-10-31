# screen_display

The screen: the panel (or the headless framebuffer), LVGL's PSRAM allocator, the
cassette screen and, on the panel board, its touch controller and the card-side artwork
loader. The public interface is `include/screen_display.h` — two calls and nothing else:
`screen_display_boot()` puts the boot mark up before the services (the panel's framebuffers
are uninitialised PSRAM, so the light is only lit once something is painted), and
`screen_display_start()` replaces it with the cassette after them (the card has been
mounted and scanned by then). Everything else is private:

- `screen_display.c` — the two boot phases, the draw buffer, the serial dump and the task
  that keeps the cassette current.
- `splash.c` — the boot mark: the icon font's K7 glyph at 128 px tall in the palette's primary,
  centred on the palette's black. It is a glyph, not a bitmap, and it owns no geometry.
- `screen_frame.c` — the diagnostics over HTTP, with `CONFIG_APP_UI_SCREEN_HTTP` on (off by
  default, and compiled for both boards):
  `GET /api/screen.bmp` is the frame LVGL has drawn, as a 24-bit bottom-up BMP in one
  response, and `POST /api/scene?n=0..9` re-aims the scene driver the console dump uses, so
  one build photographs every scene. The route answers only once the scene's script has landed
  (`cassette_view_scene_ready()`: the bars have stopped, the Menu is open, the Real rung or the
  dial is up), because a tool that posted and photographed immediately caught the middle of the
  260 ms walk to it. The frame is not a copy of the panel's scan-out: this
  board runs the port in direct mode over the RGB panel's own two PSRAM framebuffers, so
  `esp_lcd_rgb_panel_get_frame_buffer()` hands over the picture. Two full refreshes before
  the read leave both buffers current - the headless boot does the same - and the 768 KB it
  copies under the LVGL lock is streamed after the lock is released, so a capture neither
  tears on the reels nor freezes the screen for the length of a Wi-Fi send.
  `screen_capture_frame()` is where the boards differ and is the only place they do: the
  panel's frame comes from the RGB panel's buffer, the headless one from the full-frame
  `s_fb` it keeps for the console dump, and the capture takes whichever lock the board has -
  the port's on one, a mutex the screen task takes around its render on the other, because a
  whole-frame render is not atomic and a torn frame looks exactly like a scene with its own
  motion.
  `screen_display_http_start()` registers the two routes and is called by the appliance
  **before** `http_server_start()`; the screen comes up later, and the handlers answer 503
  until it does. The console dump still exists for a board with no network, and it now copies
  the frame under that same lock and prints outside it - the print is tens of seconds, far
  longer than a frame, so it cannot hold the lock.
- `lvgl_mem.c` — the `LV_USE_CUSTOM_MALLOC` backend, PSRAM first (ADR 0004).
- `cassette_view.c` — the composition: the container under the screen root, the pose and
  its animation, and the applier of the Controls model's changed-mask (the page, the two
  bars, the peek, the cover, the Menu, the meter and the room). The page is the screen's
  backdrop - dark at rest, white while the controls are out - and the model's PAGE bit is
  what flips it. It owns no geometry and no peek state; the picture is built from the
  modules below. The cassette's drawing hangs off one full-bleed, unpainted container at
  0,0, so every position in the parts stays a screen coordinate, and the peek and the bars'
  cells are the container's siblings.
- `cassette_bars.h` / `cassette_bars.c` — the two edge bars: their cells, their entrance and
  their page ink. The bar builds one 96x96 carcase per cell at the layout's rect and hands
  each to the panel that is up; `set_panel(side, panel)` installs a panel value (a grid of
  cells, or one plate), `set_shown()` slides the cells over 260 ms, reverses from wherever
  they are and hides them at the end, and `set_page()` paints the Page. **Building and
  installing are separate calls, and the composition makes the second one.** A panel's
  `create()` builds its values - knobs, marks, names, a strip's state - and creates no
  object; `cassette_view_create()` installs the left bar's mode panel and then the right
  bar's group, and nothing else calls `set_panel()` except the Menu, which owns the right
  bar while it is up. A `set_panel()` before the bars exist is a `ESP_LOGE` and a refusal,
  not the silent drop it used to be (`cassette_bars.c`: a panel's `create()` installed
  itself, so a mistaken order left the bar empty for the life of the process), and
  `host/tests/layering.sh` A1 keeps the exception list closed.
  Swapping the panel is
  a cut, which is how the right bar changes group and how the Menu's Pad takes the bar and
  gives it back. There is no chrome cell: the Menu's door is the left bar's cassette button, so
  no panel can strand it. The left bar holds the **mode bar** (the Menu's toggle and the two
  group selectors) and the right bar one **group** - the Home keys, the EQ or the RGB panel -
  or the Menu's Pad. Every cell's carcase is clickable, and a panel's `gesture_bubble` alone
  decides whether a swipe that starts on one is still the ladder's. No bar keeps a slide of its
  own, and there is no container: the cells are the strip.
- `cassette_button.c` / `.h` — the shared button, built seven times: the mode bar's cassette,
  sliders-v and led, and the Home group's four keys. A carcase two cells wide says so with
  `disc_right` (its ink takes the outer column) and, for a key with no dome at all, `bare`: then
  it builds the letter alone, 16 px in from the right edge, and the pulse pivots on the glyph.
  It builds the two circles and the letter inside a carcase its host made and owns the one
  pressed state, the latched ink (`set_on()`: a k_col_primary disc with a k_col_bg glyph), the
  160 ms pulse and the tap guard. The cap is spec data and the click is a callback, so the module
  knows nothing about the Menu or a group; the spec's internal name (MENU/EQ/RGB/BACK) is carried
  but never drawn by the module.
- `cassette_buttons.c` / `.h` — the **mode bar**, the left bar's panel value: the cassette glyph
  that toggles the Menu, the sliders-v that selects the EQ group and the led that selects the
  RGB. The view owns which one is on and relatches them; the buttons only carry the glyph, the
  callback and the latch.
- `groups` (`components/portable/groups`) — which group the right bar is showing, as three rules
  and one fact: a group button toggles, and the group dies with the chrome. `t26_groups` pins it; the view installs the panel and relatches the bar.
- `cassette_home.c` / `.h` — the Home group: four keys on the bar's lattice, the top and bottom
  ones 190x96 carcases spanning both columns. The wide two are bare - plate and glyph, no dome,
  no shadow - and the middle pair are the shared button's ordinary domed cells. Their actions
  are not wired: a click is the pulse alone.
- `knob_cell.c` / `.h` — one dial at either size: the 44 px cell's cap and the 256 px 3x cap, the
  ring of marks and the value marker (one object, one draw callback), the 4 px line, a stepped
  knob's value pillow, and the plate/cap inks of the two pages. Both knob panels build their cells
  with it, and `knob_editor` builds the 3x dial with the same code.
- `cassette_pad.c` / `.h` — the right bar's Pad: one white plate at the layout's Pad rect with a
  dark inner and a dotted guide, the portable press machine (`pad.h`: a press becomes STEP, TAP or
  BACK, one axis per press) and a **target** the owner installs. The Menu ratchets its bands with
  it; a knob's editor drives the dial with it (ADR 0028).
- `knob_editor.c` / `.h` — the one 3x dial, the 260 ms delayed open, the cassette's
  cut-out and the edit ordering's effects, shared by the EQ and RGB panels through a **bank**: the
  knobs, the repaint, the publish and the shown gate. The
  dial's text is the knob's own - its label and its plaques - and the dial takes no touch: the Pad
  in the bar is what moves it.
- `cassette_layout` (`components/portable/cassette`) — the design grid the screen is
  written on: the 800x480 grid, the scale (256 = pixel for pixel), the centring, the
  offset, the even-diameter rounding and the animation's interpolation; and the Controls'
  fixed frame - the badge, the two bands, the window, the Pad and the block - with the two
  bars' 96 px cells on their 94 px pitch, the hit rule, the Menu's page and its five slots,
  and the three named poses. It has no LVGL include, so the host tier compiles it and
  `host/tests/t16_layout.c` pins the lot. The view keeps the current pose and passes it to
  every part on `place()`; the chrome never scales, so it asks for its rect once at create.
- `controls` (`components/portable/controls`) — the Controls as a value: the rung of the
  ladder the screen is on (Peek, Initial, Art or Real), the Menu's open state and the room the
  cassette asks for. The Page, the Cover, the Peek and the chrome are readings of the rung, not
  fields. One `controls_handle(intent, facts)` returns a changed-mask; the view applies it in
  the model's order. `t24_controls` pins the ladder.
- `knob_model` (`components/portable/knob_model`) — the knob's arithmetic: value 0..100, dB or a
  unit fraction, three sweeps on one 270 degree arc (bipolar with a twelve o'clock detent,
  unipolar, stepped), the angle of any value, the labelled positions and what the dial says
  (`knob_label`), the magnet and the drag, the per-knob neutral and the edit ordering, with the dB
  ranges read from `tone_eq`. `t22_knob` pins it; `knob_editor` keeps the timer.
- `rgb` (`components/portable/rgb`) — the addressable strip's state (three channels, a brightness
  applied last, one effect) and `rgb_frame()`, which fills a pixel buffer from
  `(state, now_ms, level, n)` for OFF/SOLID/BREATHE/RAINBOW/CHASE/LEVEL. `t25_rgb` pins every
  effect. `cassette_rgb.c` is the panel and the publish seam; the firmware has no strip driver
  yet.
- `pad.c` (`components/portable/menu`) — the Pad's press classification: samples to STEP,
  TAP or BACK; the ratchet itself stays the Menu model's. `t23_pad` pins the edges.
- `cassette_button_rule` (`components/portable/cassette`) — the one tap guard, the ring
  widths and the pulse curve the button module uses. `t21_button` pins it.
- `cassette_style.h` / `cassette_style.c` — the palette, one definition per colour, read
  by the view, the tapes, the meter, the peek, the mode bar and the groups.
- `cassette_widgets.h` / `cassette_widgets.c` — the LVGL object helpers (`box`,
  `border_box`, `text`, `image`) and `cassette_circle_face()`, which gives a circle the
  white page's dome - its face the colour the caller passes - or the dark page's line-art
  face; the drawer's buttons and the DJ block's knobs both use it. The face is a pair of
  objects - a transparent wrapper for the broad shadow and the disc for the contact one -
  because LVGL draws one shadow per object. `cassette_cap_t` is the pair of colours a button's
  white-page cap carries (see the drawer below).
  Every object they build is decorative, so a press
  reaches the screen rather than a painted box. The bars opt the cell carcases into
  hit-testing and set each panel's bubbling with it: the mode bar's cells bubble gestures back
  to the screen, the Home group's, the EQ's and the RGB's do not.
- `cassette_tapes.c` — the tape body: the two packs, the two strands, the rollers, the
  run, the two hubs and, on the panel board, the two border frames. Its interface is
  `create(parent)`, `place(pose)`, `set(frame)`. A pack is one object whose draw callback
  paints every ring its fill puts there, so a fill change invalidates two boxes rather
  than one per circle; each hub is a rim painted once plus two opposite spokes, so an
  invalidation stays in one quadrant; the run is its own line, so a fill change repaints
  two narrow sides. The tape leaves each pack on the outer tangent, wraps a 30 px roller
  whose bottom sits 16 px above the screen edge, and joins the run between the two
  rollers. The two panel-board frames are filled with `k_col_frame` (FF00FF for the
  current design) and drawn before every other part of the body. Its interface is
  `create(parent)`, `place(pose)` and `set(frame)`: the chips along the bottom are windows,
  filled with the background's `k_col_bg`, which is what is behind them on both pages.
- `cassette_meter.c` — the VU meter, `create(cell)` and `set(frame)`. Two tall dot columns in the
  EQ group's plate, L on the left and R on the right, each a 5x24 matrix lit from the bottom, with
  a small L/R pill under it. One object and one draw callback per column; the plate is its
  visibility and its lifetime.
- `cassette_card.c` / `.h` — the white page's cassette: one tinted 426x272 rectangle at
  (136,82), radius 12, and its 291x87 pill window at (203,157), with the one A8 mask the
  Card and the print's container share (ADR 0039). Its interface is `create(parent)`,
  `set_shown(shown)`, `set_ink(ink)`, `cassette_card_ink()`, `cassette_card_mask()` and
  `cassette_card_window()`. Below the print it carries the shell's front plate: a trapezoid filled
30 % darker than the plastic, drawn as two triangles from one transparent object. It is created
after the tape body and shown and hidden with the
  Page in one frame, so on Real the tape shows only through the window; `set_ink()` is a cut,
  never a colour animation. The mask is 426x272 and 116,416 B, built once at create - there
  is no per-pose mask.
- `cassette_cover.c` — the white page's print. `create(parent)` builds the container, the
  clip box and the label fill; a private `place()` applies the Card's shared mask and sizes
  the picture the first time the print is shown; `set_image(dsc)` takes the loader's one
  buffer; `set_inks()` gives the print the row's label fill and the rim its plastic; `set_shown()` / `is_shown()` are the page's wish. The
  print is the top layer: the stack is cinta < Card < label. The container is the Card's
  426x272 box at (136,82), so it shares the Card's one A8 mask, whose 291x87 window is a real
  transparent opening (a pill whose ends are the hubs' circles) - the tape shows through it
  and nowhere else. The picture sits at +12,+20 in a 402x188 clip box, frame 0 with its radius plus 2 px
  and `clip_corner`, scaled to frame 0's width and top-aligned, so its height follows from
  its aspect and the box clips the rest. A pose that is not the controls pose does not show
  it; the print is cut in and out, never faded, so it lands with the page flip.
- `cassette_menu.c` / `.h` — the Menu's view. `create(screen, cassette)` builds five row
  slots inside the window and the Pad's panel value; `open(white)`, `close()`,
  `update(status)` and `set_tags(group, song)` are the view's calls, and the right bar's M
  cell (through `cassette_bars_set_menu_cb()`) opens it. `open()` installs the Pad on the
  right bar and hides the cassette container; `close()` puts the EQ panel back through
  `cassette_eq_panel()`. The model is `components/portable/menu`;
  `menu.h` is the seam and `t18_menu` pins the tree, band, window and ratchet. The page is
  the window's inner 482x280, a 6 px inset framing rows of 53-54 px, the band inset 2 px, the Pad
  `(596,76,186,284)`. `cassette_icons.h` maps the row icons to the icon font; `xtape_24` is
  the rows' and the chevron's and `xtape_36` the drawer's, both generated by the `font:xtape`
  script from `assets/fonts/xtape.ttf`. A glyph the font does not carry is hidden, not drawn as
  .notdef. Row labels are Jura 28; the right column is the value in Jura 20 capitals or the
  `angle-right` glyph in `xtape_24`.
- `cassette_levels.c` — the peek's host and the Track request. Its interface is
  `create(screen)`, `apply(frame)`, `update(status)`, `set_peek(shown)`, `set_tags()` and
  `set_pose(pose)`. The Label cover and Artwork screens and the old Level machine are gone; the
  peek is the bottom rung of the four (ADR 0025). There is no room callback: the Controls
  model owns the room and the view applies its ROOM bit. The peek still needs room from the cassette (the view shrinks and
  lifts it so the bar clears the tape) and its tethers hang from a hub, so the view hands the
  peek the pose on every step and the anchor is `cassette_layout`'s arithmetic - not a pair of
  coordinates pushed in from the tape. On a new Track it hides the old picture and asks
  `artwork_loader` for the new tags and cover in one request slot.
- `cassette_peek.c` — the up swipe's overlay: the timeline bar and the two floating tags.
  Its interface is `create(screen)`, `apply(frame)`, `set_path()`, `set_tags()`,
  `set_pose()`, `show()` / `hide()` and `is_shown()`. The bar carries the
  Track's elapsed, a rounded fill and its total; the tags carry the song above the hub and
  the group below it, each tethered to it. The texts come from ID3/FLAC tags read
  by the loader, with the artist directory and the filename as the fallback. One
  animation bobs both tags; each tether is a static line whose lower end follows the hub.
- `cassette_buttons.c` — the **mode bar**, the left bar's panel value (one column of three):
  the table of the buttons' glyphs, names and callbacks, and the two states the view drives -
  the callback and the latch. The bar builds the three 96x96 carcases and slides them; the
  buttons themselves are the shared `cassette_button.c`, which owns the pressed state, the
  latched ink, the 160 ms pulse and the tap guard. Its interface is `create()`,
  `set_cb(mode, cb, ctx)` and `set_on(mode, on)`. A button's page ink: the carcase's fill
  (`k_col_white` on paper, `k_col_bg` on the dark page) and its ring (`k_col_button_rect` on
  both); on paper only, the disc's cap - `k_col_dome` when idle and `k_col_button_cap` (0xBBBBBB)
  when the mode is selected, with `k_col_dome` as its 4 px rim (ADR 0038).
- `cassette_home.c` — the Home group: the four keys' carcases (two of them two columns merged),
  their icon-font glyphs (`play`, `prev`, `next`, `mic`), the page ink, and the handles the
  actions will hang from.
- `cassette_eq.c` — the EQ group: all six cells, `[Back][HI] / [VU][MID] / [Gain][LOW]`. It
  builds the four knobs (`knob_cell.c`) and the meter into the carcases the bar hands it and owns
  its values and the audio; it creates no carcase and no root. Its interface is `create()` and
  `cassette_eq_panel()` - the value the view installs when the group is selected. The knob values and the meter's place outlive a swap;
  an open edit ends with the panel, as it does whenever the panel goes.
- `cassette_rgb.c` — the RGB group: the same six cells with Red, Green, Blue in the outer
  column and Effect (a 96x190 plate, its dial above and the effect's name below) and Brightness
  inside. Its interface is `create()`, `cassette_rgb_panel()` and `cassette_rgb_set_publish()` -
  where the strip's state goes.
- `cassette_window.c` — the window around the cassette on the controls screen: 486x284 at
  (110,76), its top and bottom edges on the bands' lines and its sides in the gaps either
  side of the scaled tape. Same 2 px border, 8 px corner and carcase ink as the bands. It
  does not slide - it is a window, not a band, and sweeping it over the tape would read as a
  mistake - so its interface `create(screen)` / `set_shown()` fades it 0..255 over the
  drawer's 260 ms instead. It is built as the screen's first child, before the cassette's
  container, so the tape draws over it and reveals the border as the pose shrinks the
  cassette into the window.
- `cassette_header.c` / `cassette_footer.c` — the two bands that close the controls screen
  across the top and the bottom: the header's composite frame is 768x72 at (16,6), its 2 px
  border landing on the drawer's and the DJ block's top line (76..77); the footer is 768 px
  wide at (16,358) and 116 px tall, 6 px short of the panel's bottom, its 2 px border landing
  on their bottom line. They are line art like the carcases they join - a 2 px border rounded by 8 px in
  `k_col_button_rect` - over an opaque fill in the page's colour: `k_col_bg` on the dark
  screen, `k_col_white` on the white page, repainted by `set_page()` as the view's page
  flips, so a band covers what it slides over instead of letting the picture show through
  it. Each interface is `create(screen)`, `set_shown()` and `set_page()`. They move with the
  rest of the chrome: a 260 ms ease-out slide, down from above the panel and up from below
  it, reversed from wherever it is if the controls change their mind, and hidden at the end
  of the slide out. The header also carries the K7 mark from `assets/labels/k7b.png` in a
  badge of its own: a 96x72 rounded plate at (16,6) that is the palette's primary on the
  dark page and the page's white with a `k_col_bg` border on the white one, with the 60x32
  A8 mask centred in it and tinted to suit - the cap's light ink on the primary plate,
  `k_col_bg` on the white one - so the mark sits at (34,26) on the screen.
- `screen_input.c` — the touch policy: the screen's one-finger vertical gesture becomes one
  `screen_intent_t`, DOWN or UP, and nothing else does. It names no screen and calls nothing
  above itself; the view's sink maps the two into the Controls model, which owns the rungs.
  The horizontal swipes are retired (ADR 0025): the drawer is Art, reached by a down swipe,
  and the only left swipe left is the Menu's Back, whose gesture the Pad keeps local. A
  two-finger swipe is nothing at all; there is no latch any more.
  A tap is not an intent: next and previous are the DJ block's own cells, and the view
  drops every intent while a knob is in edit mode.
- `artwork_loader.c` — the card side of the peek's tags and the white page's cover. It
  locates the Track's picture (an ID3 `APIC` or a FLAC `PICTURE`), decodes the JPEG with
  TJpgDec on its own task, and fits it cover-style into one 603x385 RGB565 buffer; the
  same task reads the text tags (TPE1/TIT2, ARTIST/TITLE). Results are handed to the view
  under the LVGL lock. The request is one slot, replaced rather than queued, and the read
  backs off while the broadcast is behind. Compiled and started only on the panel board.
- `panel_rgb.c` — the Sunton panel adapter, compiled only for that board. It holds the
  backlight off (`backlight_hold_off()`, which gives the pin to LEDC at duty 0) before the
  peripheral exists and leaves it off when it returns: the panel scans two uninitialised PSRAM
  framebuffers from the moment it is initialised, and those read as white. Then it waits
  `APP_UI_PANEL_SETTLE_MS` (250 ms) before creating the peripheral, because this panel has no
  reset line - the RGB driver sends it no commands and the profile wires no display-enable pin -
  so its own power-on reset is the only one it gets, and the first clock must not arrive while it
  is still starting up. `panel_rgb_clear()` fills both black, and the screen lights the panel
  only after the boot mark is in both buffers.
- `backlight.c` — the Sunton backlight on GPIO 2, now a 400 Hz, 8-bit LEDC duty rather than
  an on/off pin. It starts at `APP_UI_BACKLIGHT_AWAKE_PCT` (100% by default), then
  fades to 60% after 2 minutes, 30% after 5, and 0% after 10 with no touch, each step
  set by its own Kconfig pair. A touch restores the awake level in the input read
  itself, from wherever the light currently is. The ladder is measured on LVGL's
  inactivity clock, so any press resets it, and the fade is stepped by the screen
  task (`APP_UI_BACKLIGHT_DIM_FADE_MS`, 2500 ms; wake
  `APP_UI_BACKLIGHT_WAKE_FADE_MS`, 300 ms) so the steps do not snap. The first press
  after a dim only wakes the screen; the view sees no click or gesture from it. If
  LEDC refuses the pin the module falls back to holding it high and only dimming is
  lost. `APP_UI_BACKLIGHT_TEST` replaces the cassette with a white screen and steps
  the backlight through 100/80/60/40/20/0 every five seconds, printing the level, so
  the PWM range can be checked without waiting for the sleep timer.
- `touch_gt911.c` — the Sunton touch adapter: a GT911 on I2C0, polled every 10 ms and
  bound to LVGL as the display's pointer. It reports the first point and nothing else: the
  two-finger swipe and its midpoint detector are gone with the VU meter's old home
  (ADR 0025). Next and previous are the left drawer's `<` and `>`, never a screen gesture or
  a tap.
- `assets/labels/k7b.png` — the K7 mark in the controls header. It is greyscale with an
  alpha plane and only the alpha is exported, as `assets/header_logo.bin` (A8, 60x32),
  by `tools/convert_cassette_assets.py`; CMake embeds that blob and `cassette_header.c`
  tints it inside the header's badge.
- `assets/labels/cover.jpg` — the old Artwork Case, parked with the Label cover and
  Artwork screens and not embedded. The white page's cover does not use it; the cassette's
  frames are the frame. `tools/convert_cassette_assets.py` is still the way the old
  label-cover blob is made.

**The Menu.** A tap on the DJ block's M opens a mode of the controls: the cassette area
becomes the window's five-row page, the right bar swaps its EQ panel for the Pad, the drawer
becomes Back/Enter/Close, the header carries the list's title and the footer carries the broadcast's
**timeline** - elapsed, total and the Peek's own 6 px track and fill, no strings (ADR 0032) - and
its frame is page ink: primary on Art, `k_col_glass` on Real, while the times, the title and the
track keep the playing ink (ADR 0037).
The model is portable - `components/portable/menu`, host test `t18_menu` - and owns the tree,
the band, the window and the ratchet; `cassette_menu.c` draws five row slots, executes its
actions, installs the Pad on the right bar and hides the cassette container for the mode's
lifetime. The Pad is a white plate with a dark inner (inset 8 px) and a dotted vertical guide
inside it, over the block's place and white on both pages: a vertical drag ratchets one row per
54 px (1x/2x/4x by speed), a tap activates the band, a leftward swipe is the drawer's Back, and
the plate does not bubble gestures so a drag cannot close the Menu. Closing the Menu puts the
EQ panel back with its knob values and its edit layer as they were.
The root reaches Tracks, Browse, Shuffle (a mode of the broadcast, ADR 0023) and Settings
(Backlight, Station, Library, Rescan, About); Backlight pins the panel with a hold until
reboot. The icon font carries the whole set now - `books` for Browse and Library, `settings`,
the two `toggle` glyphs for Shuffle, `brightness`, `k7logo` for About, `track` for Track rows,
`micro-chip`/`tablet-android`/`disc` for About's rows and the `angle-right` chevron - and the
view hides a glyph the font does not carry. The tree and rules are in
[`docs/specs/menu.md`](../../../docs/specs/menu.md), with ADR
[0022](../../../docs/adr/0022-the-menu-is-a-controls-mode.md) and
[0023](../../../docs/adr/0023-shuffle-is-a-broadcast-mode.md).

### The rules the modules share

Four things this screen does were written out once per part, and each is now a value or a
table that the parts call (ADR 0043):

- **The timeline's arithmetic** - `components/portable/cassette/timeline.c`: the `m:ss` /
  `--:--` clock, the permille from elapsed and duration, the fill width from a permille. The
  model, the footer band and the Peek all read it; `t27_timeline` pins the edges. The two
  hosts still own their objects, their idle presentation and their caches - ADR 0032's
  rejection of shared objects stands.
- **The chrome's entrance** - `components/portable/cassette/entrance.c` for the rule (a
  repeat is not a restart, a reversal starts at the pixel reached, a piece is hideable only
  when it has left) and `chrome_entrance.c` for the animation. Every moving piece is one
  `chrome_entrance_t`, two small callbacks and an init line; `t28_entrance` pins the rule.
- **A knob Group is a table** - `knob_panel.c` owns the 2x3 shell: cell map, plate rect, five
  handlers, the tap guard, the editor's bank, the shown gate and the panel value. The EQ and
  RGB files are tables of names, ranges, marks and a publish hook. A third Group is a table.
- **The Menu's open state is the Controls model's** - closing is an intent
  (`CONTROLS_INTENT_MENU_CLOSE`), so no fact is adopted, no latch is patched, and the 40 ms
  poll that noticed a self-close is gone.

What is left of the board difference is the hardware: the RGB panel, its GT911, and the pin
the backlight hangs from, selected by `CONFIG_APP_BOARD_SUNTON_8048S050C` in this component's
CMake (with a `backlight_none.c` adapter on the board that has none). Everything above them -
the Peek, the white page's Cover, the Menu, the artwork loader, the Card, the body's border
frames - compiles on both boards and asks `screen_caps()` what this build has (ADR 0044).
`cover` is the loader, the print and the Card's mask; `panel_borders` is the two `k_col_frame`
rectangles the panel's RGB path uses to mask its overscan. A board with neither renders the
white page with no print, which is what "no Cover" means. The console dump stays the headless
board's and the HTTP diagnostics work on both. A third board adds a `screen_caps_t` and its
adapters, not a conditional: only `screen_display.c` and `screen_caps.c` name the board symbol,
and `host/tests/layering.sh` A2 keeps it that way.

## The four rungs and the peek

The screen is a stack of four, top to bottom: **Real** (the chrome on the white page, with the
Track's picture), **Art** (the chrome on the dark page), **Initial** (the cassette alone) and
**Peek** (the timeline bar and the two tags). It boots at Initial. A down swipe takes one rung
towards Real and an up swipe one towards the Peek; the ends stop. That is ADR 0025, and it is
the only way between them - the horizontal swipes and the two-finger VU are gone.

At the entry 100% pose (256/256) the up swipe raises the peek and a down swipe with the peek up
puts it away; an up swipe while it is up does nothing. The peek can only be raised from that
100% pose: while the cassette holds the 134/256 pose (or the peek's own 200/256 is still
settling) the up swipe does nothing. The down from Initial is **Art**: the drawer and the DJ
block slide in on the dark page. The next down is **Real**: the page turns white and the Track's
picture prints on the cassette, as one step; the up swipe undoes each rung in turn. The Label
cover and Artwork screens that used to sit below the cassette are gone, with their two down
gestures and the old Level machine.

The peek's own 260 ms slide is kept: it moves a 568x52 bar and two 504x44 tags, not the
panel. Its cassette shrink re-places every part in the same 260 ms — geometry only, no
bitmap and no re-sampled pixels — so what it costs is a redraw of the cassette, not of
the panel's picture. `cassette_view_set_scale(scale, 0, dy, 0)` is the instant form if
that ever reads as a stutter.

The Track's picture and text tags come from `artwork_loader.c`, which the panel board
starts after the view. A Track with no usable picture simply shows no cover; a Track with
no tags keeps the peek's filename fallback.

## The peek

The peek is the bottom rung of the four (ADR 0025), owned by `cassette_peek.c`. It has three
things: the bar, the tags and the tethers.

The bar is the old paper strip, 568x52 at (116,424) with a 12 px radius. It no
longer carries the title. Its left box is the Track's elapsed time, its right box the
total, and between them a 288x6 rounded track whose lit fill grows with the model's
`permille`. The model reports whole-second changes (`time_changed`) and permille changes
(`permille_changed`), so the times are rewritten once a second and the fill only when its
pixel width moves. With nothing playing the times and the track are hidden and the bar
shows the screen's idle sentence instead.

The tags are two 504x44 pills rounded by 22 px, black with the playing red's 2 px border
and 28 px Jura text, centred on the cassette at x=148..651 (61 px of cassette on each
side): the song at (148,12) above the left hub, the group at (148,248) below it, their
centres 118 px from the hub's centre on either side.
Sixteen px of inset leaves 472 px of text, enough for a long title; a longer one is cut
with dots, not scrolled. The peek uses a second Jura face at 28 px (`font_jura_28.c`);
there is no 36 px band left on the screen. Both tethers leave on the left hub's column
(251 px; 20.4 % of the centred 504 px pill rather than a round quarter, so the lines stay
vertical): the upper label's from its bottom to the top rim, the lower one's from its top
to the bottom rim.
The texts are the Track's ID3/FLAC tags, read by `artwork_loader`: TPE1 (artist, with TPE2
as its fallback), TIT2 in an MP3; ARTIST (with ALBUMARTIST as its fallback), TITLE in a
FLAC Vorbis comment. Until they land - and for a Track that carries none - the group is
the first directory of the Track's relative path and the song is the cleaned filename. A
label wider than its pill is truncated with dots, not scrolled: a marquee would animate
every frame the peek is up. A label with no text is hidden with its tether on its own - no
em dash stands in for it - so a Track with only one of the two shows only that pill.

Each tether is a two-point `lv_line` from 14 px inside its pill to the left hub's near
rim: the upper label from its bottom to the top rim, the lower one from its top to the
bottom rim.
The line's upper end is under the pill's opaque fill, so the tag's bob uncovers a little
more or less of it without a line redraw; the lower end is the left hub's rim at the current
pose, which the peek works out from `cassette_layout`'s `cassette_hub_anchor()` - the tape
body draws its rim from the same numbers - so the tethers follow the cassette while it
shrinks and lifts, and `t16_layout` pins the anchor at all three poses. The line object is
placed on its own bounding box, so an invalidation is the line and not the screen from 0,0.

The movement is one `lv_anim` over both tags: a five-second sine, +/-6 px, the two tags
in opposite phase. Its callback rounds the offset to a whole pixel and writes `y` only
when that pixel changes, which is around eight writes per tag per second at 25 Hz; each
write invalidates one 504x44 pill, not the cassette and not the screen. The tethers are
static between pose changes. The peek's room request is the view's 200/256 pose with the
-60 px lift; the tags appear with the first frame, the bar runs its 260 ms slide, and the
next up swipe puts everything away.

## The mode bar

The three buttons at the left edge are the shared `cassette_button.c`; `cassette_buttons.c`
is their table and the panel value the bar fills. They are the screen's **modes**, top to
bottom: the cassette glyph toggles the Menu, the sliders-v glyph selects the EQ group and the
led glyph the RGB group. The bar is three 96 px cells with no panel object around them, hidden
off-screen at rest; the left bar (`cassette_bars.c`) builds and slides them over 260 ms and the
Controls model decides when from the ladder. A down swipe from Initial slides it in as Art; an
up swipe slides it out. There is no sideways route to it any more (ADR 0025). The entry view is the untouched full-scale cassette; while the
drawer is out the model puts the cassette at 134/256 (`CASSETTE_SCALE_CONTROLS`), which
brings the tape's box to 402x232 px at x=148..550: the drawer's 96 px column at x=16 is clear
on the left and the 190 px DJ block at 594..784 has room on the right. The DJ block is shown
and hidden with the drawer by the same model bit, and the pose runs over the same 260 ms. The
three buttons start 76 px from the top, so the middle button `<` is on y=218; the -20 px dy
puts the cassette's centre near that line.
The drawer wins over the peek while it is out; when it slides away the peek's 200/256 and
-60 px lift come back.

Each button is 96x96, rounded by 8 px, with a 64 px circle at its centre and one icon font
glyph centred in the circle in `xtape_36`. The carcase is the left bar's - the same 96x96 box
as the groups' - filled opaque with the page's colour and bordered `k_col_button_rect`. Each
button carries **its own cap** - `cassette_cap_t`, the spec data, and the white page's alone:
the face of the disc there and the letter drawn on it - and the mode bar's three start in `k_col_dome`'s neutral grey with the letter `k_col_bg` (black).
The cap is spec data at create: the panel never re-inks one afterwards. The dark page is not
the caps': it is the palette's line art, the playing red ring and letter
(`k_col_button_circle`, `k_col_button_letter`), the same for every button, whatever a cap
says. What *is* the button's own ink is the **latch**: `set_on()` fills the disc
`k_col_primary` with a `k_col_bg` glyph on both pages, the idle look inverted, and the white
page keeps its 4 px `k_col_dome` rim so the coloured disc is still a disc let into the cap. The
view latches the cassette while the Menu is open and a group's button while its group is up.

On the white page the circle becomes a **dome**: a flat disc (one colour - no gradient) with
**two** outer shadows, because LVGL draws one per object. A transparent circle exactly under
the disc carries the broad ambient one - 24 px of blur at a (2, 5) offset, 60 % black - and
the disc itself carries the tight contact one - 8 px at (1, 2), 60 % black - so the edge is
dark and the shade falls off into the bezel, with no ring. Both are outer-only; the dome is
not inset anywhere. The contact one's whole blur sits inside the 96 px bezel (x=85, y=86);
the broad one reaches x=94, and its faintest tail a couple of px past the bezel's bottom.
Each has its own colour constant, so a tinted second shadow is a one-line change.
The dome's face - one flat colour, no gradient - is the button's own `face`, so a button whose
disc is not the playing red can carry that colour on this page too, and the letter on it is its
`letter`, `k_col_bg` (black) by default. The rim just inside the edge is always `k_col_dome`'s
original grey, `CASSETTE_CAP_RIM_W` (4 px), so a coloured cap reads as a disc let into the
original one; it is the disc's own border, which LVGL draws inward - an inset, not an outline -
and on a cap whose face is already that grey it is invisible.
The dome is the pressed state's too: on this page the disc is the cap, and its
`CASSETTE_CAP_RIM_W` rim is the same width pressed or not, so a press reads through the
click pulse - and, in the drawer, through the carcase's ring widening from 2 to 3 px.
`LV_EVENT_PRESSED` writes that ring and release or `LV_EVENT_PRESS_LOST` writes it back. A
click runs the 160 ms scale pulse (256 -> 232 -> 256) from the portable
`cassette_button_rule`.

All three buttons are that one module, and it is where the press, the pulse and the guard live: it builds the disc, the letter and the
page ink inside a carcase its host made, and hands a click to the host's callback. The mode bar
maps its three to the Menu's toggle and the two groups. The tap guard is one rule for all five: `!moved && !gesturing`. A click that carries a gesture direction or has moved past LVGL's
press limit is not a tap and does nothing at all - no pulse and no action. The guard used to
carry a third term for the two-finger gesture; that gesture is gone (ADR 0025).

The bands across the top and the bottom of the controls screen live in `cassette_header.c`
and `cassette_footer.c`: 768x72 at (16,6) and 768x116 at (16,358), a 2 px border rounded by
the buttons' 8 px in the drawer's carcase ink. The header's 2 px border lands on the first
button's and the first DJ cell's top line, the footer's on their bottom line, and both
bands' sides sit on the cassette frame's 16 px inset, so the drawer, the block and the two
bands read as one frame. Their fill is opaque and follows the page - `k_col_bg` on the dark
screen, `k_col_white` on the white one, which is what makes them read as part of the page
they close and lets them cover the tape while they slide over it. They arrive with the rest
of the chrome: the header drops from just above the panel and the footer rises from just
below it, both over the drawer's 260 ms ease-out, and both are hidden when their slide out
completes. A reversal starts from wherever the band is, like the drawer's own slide.

The header carries the K7 mark in a badge at its left. `assets/labels/k7b.png` is greyscale
art with transparency; the asset tool keeps only its alpha plane (A8, 60x32, 60 px stride)
and the firmware tints that mask with `lv_obj_set_style_image_recolor()`. The badge is a
96x72 rounded plate at (16,6), a screen child above the band so its border is the divider:
`k_col_bg` fill with a `k_col_primary` border and mask on the dark page; `k_col_logo_back`
with a `k_col_bg` border and mask on the white one. The mask is centred on the plate, so the 60x32 art sits at (34,26) on the
screen, and the badge rides the band's slide.

The third rectangle of the frame is the window around the picture, `cassette_window.c`:
486x284 at (110,76), its top and bottom edges on the bands' lines and its sides in the gaps
either side of the scaled tape. It has no edge to come in from - sweeping a window over the
tape would read as a mistake - so it fades in and out over the same 260 ms, from whatever
opacity it holds if it is reversed, and hides at the end of the fade out.

The mode bar is chrome above the peek. The bars are created after `cassette_levels`; the left
bar's three carcases and the buttons inside them bubble events, and the cells bubble gestures
where a panel asks them to, so a swipe that starts on the chrome still reaches
`screen_input`. A button press cannot fall through as a tap: a click on a child is the
child's, and the screen no longer gives a click any meaning.

## The white page (Real)

The white page is the **Real** rung, and it carries the Track's picture. A down swipe from
**Art** paints the screen's backdrop `k_col_white` (0x909090) instead of `k_col_bg`
(0x111111), so the orange line art reads as ink on paper, and prints the cover in the same
step; the up swipe flips the page back to dark and fades the picture out over it. The view
owns the flip (`page_set()`, held in a flag so a repeat request costs nothing), and it runs in
one frame as the step is taken - a colour animation would repaint the whole 800x480 panel on
every step. The flip writes the screen's backdrop, the opaque fills of the two bands, the
drawer's three buttons and the DJ block's six cells, the tape's window chips, and each
button's circle (line art or dome) and letter. The model's PAGE bit carries both halves, and
the applier flips the page before it shows the picture.

The white is a margin: the flip also reveals `s_screen_back_cover`, a square-cornered
768x448 `k_col_bg` box at (16,16) that is the screen's first child, so on the white page the
picture keeps its dark ground and the white reads as the 16 px ring around everything.

The same page carries the **print** - the Track's cover when it has one, the row's label fill
when it has not - on the **Card**: one tinted 426x272 rectangle at (136,82), radius 12, with a
291x87 pill window at (203,157) that the Card and the print's container share as one A8 mask,
so the tape shows through it and nowhere else. The mask is 426x272 and 116,416 B, built once
at create; there is no per-pose mask.
[ADR 0039](../../../docs/adr/0039-the-cassette-is-a-card.md) records why the glow is gone,
[ADR 0040](../../../docs/adr/0040-the-cassettes-are-a-table.md) why the colours are a table, and
[`docs/specs/cassette-card.md`](../../../docs/specs/cassette-card.md) has the rectangles. A
Track without a picture still has Real: the page turns white, the Card takes its row's plastic
and the print is the row's label fill. Every flip back to dark hides the Card and the
print with it.

The picture is the loader's one 603x385 buffer, drawn by `cassette_cover.c` into a 402x188
clip box at +12,+20 - frame 0's rect, with its radius plus 2 px and `clip_corner`, so the
print's corners sit a little inside the shell's border. Its width is frame 0's and it is
top-aligned, so its height follows from its aspect and the box clips the rest. The window is
zeroed in the same mask at (55,55): the largest radius the rectangle allows,
`min(width, height) / 2`, a pill whose ends are the hubs' circles.

The print's container is a child of the cassette container, created after the tape body and
after the Card - so nothing is raised at runtime - and the window's rim is created after it, so
the stack is **cinta < Card < label < rim**. The rim is 3 px of the Card's own plastic on a
291x88 box with the window's radius: the shell moulded around the window, not a line. It covers the body inside the outer
frame, and it hides the mechanics with it: the two frame rings, the three chips, both
rollers, the run and the two strands are all inside 426x272 and under the Card, and only the
hub rims turn behind the window - their lines white while a Cover is printed
(`k_col_hub_white`: the rim's border and the spokes; its face stays the playing red), the tape's
own inks when the print is the label fill. The print's box is fixed in screen pixels and does not
follow the pose, and its picture is scaled at draw time, so the buffer stays the loader's one
size; the controls pose scales it down, not up. The Card exists only at that pose, which is why the model refuses the down
from Art until it has landed - the same two downs can arrive faster than the shrink.

The print is cut in and out, never faded: the down brings it with the page flip and the up takes
it with the page, in one frame. A Track change cuts it too - the loader reuses the one cover
buffer, so there is nothing to fade over, and a Track with no picture cuts to the label fill
just as plainly.

The Card's ink is a cut, never an animation, and it is not read from anything: `k_col_cassette[]`
is the palette, one row per cassette - a plastic, the label's fill and the ink it would print
in - and the
Track's file name picks the row (`ink_seed % k_col_cassette_count`, logged as `cassette N of M`). Both inks land together, so a
cassette is one row and nothing is derived from the picture (ADR 0040). The table is where the
screen's colour is tuned: fill a row and a cassette exists.

The hub tick is the one thing that must not repaint the Card. While it is up `pack_place()`
clamps each pack's box to the window, (203,157) 291x87: the rings are drawn from the hub's own
screen centre, not from the box, so they do not move and are clipped to the window, and the
tick's invalidated area falls from the union of the two 186x186 boxes (72,168 px) to the
window's 25,317 px. The strands move only on a fill update, so they keep their full boxes.

The chips along the tape's bottom are windows on whatever is behind the cassette. That is
`k_col_bg` on both pages - the dark screen at rest, and the back cover on the white page - so
they are filled with `k_col_bg` and the page flip does not touch them. On the white page the
Card covers them, so they read only on the dark one.

## The right bar's groups

The six cells at the right edge are the right bar's (`cassette_bars.c`), which builds them,
slides them and paints them; one **group** fills them. There are three, and the view installs
whichever `components/portable/groups` says is up:

- **Home** — `cassette_home.c`:
  ```
  [      1      ]
  [  2  ] [  3  ]
  [      4      ]
  ```
  Four keys, and the ends are two of the bar's columns merged: the top and bottom keys are one
  190x96 carcase. A merged cell is not a bigger button - it is one key and a spare - and the wide
  pair carry **no dome**: `bare` gives them the plate and the glyph, the glyph 16 px in from the
  carcase's right edge, exactly where a domed key in the outer column would have put it, and the
  94 px left over is empty plate. So the wide keys have no ring on the Art page and no cap, rim
  or shadow on the Controls page; the middle pair are the ordinary domed buttons. The merge is the
  panel's own `cell_rect()`: the merged rect for the inside column and an empty one for the cell
  it covers, which is how the bar is told to build nothing there - `cassette_bars.c` needs no
  change for a span. The keys are a control surface, not a picture: no latch, no group of their
  own, a swipe that starts on one stops there, and their actions are not wired yet, so a click
  is the pulse alone. The domes carry `play` on the top key, `prev` and `next` on the middle pair
  and `mic` on the bottom, from the icon font's 36 px cut.
- **EQ** — `cassette_eq.c`: `[VU][HI] / [VU][MID] / [Gain][LOW]`, the meter one 96x190 plate.
- **RGB** — `cassette_rgb.c`: `[Effect][Red] / [Effect][Green] / [Bright][Blue]`, the effect one
  plate with its dial above and its name below.

Two touching 96 px columns at x=594..784 (the first column nudged 2 px right; the second stays at
688), three rows at y=76/170/264, so the block is 190x284 and its right edge leaves 16 px to the
screen. The 16 px is the inset of the 64 px circle inside each carcase, not a gap: the carcases
share their borders like the mode bar's. All six cells are carcases over the page's opaque fill,
so a group covers the tape it slides across: the bar's page fill, then the panel's own ink - the
knob cells are the knob plate on the white page only, `k_col_knob_rect` filling them and
`k_col_bg` for their border, so no carcase line runs round a knob. The meter's plate takes the same
ink, so the block reads as one surface; the columns' own inks do not turn.

The bar is hidden at rest, so the entry view is the full-scale cassette; the model shows it with
the mode bar, when the cassette is already at the 134/256 control pose. The right bar slides the
cells in from the right edge over the same 260 ms as the mode bar and back out when it leaves. A
group goes away when the bars go away - the groups model drops the selection on that edge - so
the controls always open on Home. The group's own button on the mode bar is the only way
back - a panel has no Back cell - and `groups_apply()` swaps the panel and relatches the bar.

While the Menu is up the same bar hosts the Pad instead - one white 186x284 plate over the
block's place - and the group is remembered across the mode and reinstalled when the Menu closes
(`cassette_menu_set_restore_cb()`).

Each knob is a 44 px cap - 30% smaller than the buttons' 64 px one - with a ring of short lines
outside it at r=32 and a line from the centre towards the rim (the owner's tuned 4 px width), built
by `knob_cell.c`. There are no dots: the ring prints the two ends and the detent, and one brighter,
thicker line marks the value (ADR 0027, ADR 0036). The cell names its knob in a 14 px round pillow inset 10 px
into its top-left corner, with the short label the panel passes - `G`/`H`/`M`/`L` for the EQ, a
letter or an abbreviation for the RGB group - in the meter's `lv_font_montserrat_14`:
`k_col_primary` with `k_col_bg` on the Art page and `k_col_bg` with `k_col_button_white` on Real.
The dial's box starts 10 px low (`KNOB_CELL_DIAL_Y`) so the pillow has its row (ADR 0035). Every
knob rides **one 270 degree arc** - the min end at
7:30, the max at 4:30, a dead gap at the bottom - so the EQ's four are bipolar with 0 dB at twelve
o'clock, **-26 dB to the left and +6 dB to the right** (a boost turns clockwise), and the RGB's
four are level knobs from 0 at 7:30 to full at 4:30, with its effect selector **stepped** along the
same arc, one slot per effect.

A knob's value moves by **dragging it up and down**: 6 px of dead zone, 2 px a step, a magnet
that holds the knob's neutral and its labelled positions (3 values of stick, 5 of release), and a
live publish on every step. A **tap** opens edit mode after a 260 ms delay: a 3x dial (256 px at
x=220, 90% of the 96+94+94 length, its top at y=90, a 2 px cap stroke and a centre line, with no
outer square - only the cap, the ring and the line, plus graduations every 22.5 degrees) appears
over the cassette, and the right bar gives its cells up to the **Pad**. **Above** the dial a rounded
`k_col_primary` pillow carries the knob's label (GAIN, HI, MID, LOW, or an effect's) in the light
cap ink, and takes the live value - `+3.5`, `0.0`, `73%`, or a selected effect's name - while the
Pad ratchets, going back to the label a second after the finger lifts. At each labelled position
the dial prints a **plaque** (-26 and +6 for the bands, -12 and +12 for the trim), an outline until
the knob reaches it and filled when it is there - a reading, not a target. The cassette cuts out
and the dial appears; on close the dial disappears, the cassette cuts back in and the group's cells
return.

**The dial takes no touch.** While it is up the Pad stands where the group was and does everything
(ADR 0028): its **ratchet** moves the value at 4 px a step, its **tap cycles the knob's stops**
(`knob_cycle()`: -12 -> 0 dB -> +12, -26 -> 0 dB -> +6, or a selector's slots one at a time) and its
**left swipe closes**. The mode bar's three buttons close it too and run nothing else - the dial
leaves them uncovered, and a tap there is the dismiss, so it cannot also open the Menu or drop the
group (ADR 0034) - and the view's sink drops every screen swipe, because editing is a fact of the
Controls model. A double tap on any small knob resets
it to its **neutral** and cancels the pending open, so the 3x dial never appears: 0 dB for a band,
full for a colour channel or the brightness, OFF for the effect. Nothing blinks (ADR 0029): the
dial's cap is a reading, and the Pad is what the finger is on. A stepped knob also keeps its
selected value in a small bottom pillow under its 1x cell (`knob_cell_add_value_pill()`), which is
how the effect's name reads on the RGB plate.

**A selector is named by its positions.** A stepped knob prints one label per slot **in a box** -
three stacked on the left, one above the dial, three stacked on the right, three to six values - and has
**no title pillow**: the filled label says which one is on, so a centre word would be the same
sentence twice. The columns hug the dial and share an edge; the values keep the arc's order across
the screen (ADR 0030). A range knob's two ends keep the ring, at 7:30 and 4:30. A continuous knob
keeps its pillow (it carries the knob's name, `GAIN`), because a number has no name of its own. The
dial also sits 20 px below the pillow's row, so the twelve o'clock marker is not read as part of
it.

The whole modal half is `knob_editor.c`: one dial, one timer per screen, shared by the two panels
through a **bank** (the knobs, the repaint, the publish and the shown gate). The panels keep their cells, their values and what a value publishes; the dial's
text is the knob's own, so the bank has no label hook. The editor borrows the bar through
`knob_editor_set_bar()` - the view's claim installs `cassette_pad_panel()` and its release calls
`groups_install()` - so a bank's hooks must tolerate their cells being gone: both panels guard on
their own `s_shown`.

The EQ's audio side is `components/portable/tone_eq`: a 200 Hz low shelf, a 1 kHz bell (Q 0.7)
and a 4 kHz high shelf, plus a trim applied before them. The knobs map to the DJ ranges:
GAIN -12..+12 dB (default 0) and the bands -26..+6 dB (default 0). The targets are
published by `mini_icecast_eq_set()` / `mini_icecast_gain_set()`, read by the broadcast
task once per 1024-frame chunk, and slewed at 2 dB per chunk before the coefficients are
rebuilt, so a moved knob glides rather than steps. The EQ runs in `emit_chunk()` and on the
partial tail in `flush_pipeline()` before `measure_level()`, so the VU is the level that
goes out and a track's last 23 ms is shaped too. The int16 clamp is the last stage; a hot
trim and a boosted band can reach it, like a mixer.

The RGB group's side is `components/portable/rgb`: three channels 0..100, a brightness 0..100
and one effect from OFF/SOLID/BREATHE/RAINBOW/CHASE/LEVEL. Brightness is applied **last**, to
whatever the effect produced, so turning the strip down keeps its hues. `rgb_frame()` fills a
pixel buffer from `(state, now_ms, level, n)` and is pure, so every effect is pinned by
`host/tests/t25_rgb`. The firmware has no strip driver yet - no pin, chipset or count is
decided - so `cassette_rgb_set_publish()` is the seam: the panel hands the whole state to
whatever the board installs, and a hook installed late is called once with the current values.

## The VU meter

The status carries the RMS of the 1024-frame chunk the encoder is about to take
(0..1, full scale), measured by `measure_level()` in `components/portable/mini_icecast`
after the tone EQ has run, so it is the level that goes on the wire.
`components/portable/cassette` turns it into a peak-hold: `vu = max(level, vu * 0.90)`
per 40 ms update, about -23 dB/s, so the peak follows a real drop instead of hanging.

`cassette_meter.c` draws it in the EQ group's `[V]` plate, in cell coordinates: the plate is
96x190 (the inside column's top two cells), and inside it sit two 30x148 columns 12 px apart,
with a 30x14 L/R pill 6 px under each. A column is a 5x24 matrix of 4 px dots on a 6 px pitch,
painted by one draw callback: the rows below the level in `k_col_meter_lit`, the rest in
`k_col_meter_dim`, so the column fills from the bottom up. The scale is -48..0 dB, as it always
was, and a change of level repaints only the column whose count moved. The pill is the channel's
label - `k_col_meter_lit` with the letter in `k_col_bg` - which is the badges' successor: with
vertical columns there is no left margin to hang an L/R badge from.

The meter's own screen is what makes it read on both pages: a `k_col_meter_window` (`k_col_bg`)
rectangle 3 px inside the plate, radius 4, behind the columns. On the white page the plate is the
knobs' mid grey (`k_col_knob_rect`), so that grey becomes a frame round a dark window instead of
the meter's background, and the dots keep the dark page's inks - the bright `k_col_meter_lit`, the
`k_col_meter_dim` track and the `k_col_meter_lit` pill with its `k_col_bg` letter. On the dark
page the window and the plate are the same ink, so it is simply not there.

The plate is the meter's life: the bar builds it with the EQ panel's cells and deletes it with
them, and `cassette_meter_set()` is a no-op whenever there is no meter on screen. The delete
event detaches the module, so a frame that arrives while another group is up cannot touch freed
objects; both lit counts start at zero when the cells are rebuilt. The two-finger gesture that
used to fade the meter in and out is gone (ADR 0025); the module has no show/hide and no pose any
more.

## The reel change

Eight seconds after a track boundary the model returns the rewind window: the packs
migrate back and the hubs turn in reverse (ADR 0011). The model also reports the window's
ink as `cassette_frame_t.tape_ink` (0 = the tape's own colour, 1 = the playing red, 1 when
not rewinding); `cassette_tapes.c` mixes each hub ring's border from `k_col_tape_dark` to
`k_col_tape` with it, and only writes the style when the value changes. Nothing else
fades: the meter, the title and the cassette are information, not tape. The model still
reports `rewind_permille`, and `t10_cassette` tests both the reel arithmetic and the
`tape_ink` ramp.

## The Track picture and the Case

The Track's picture is back, but not the old Artwork screen: the white page's cover uses
it inside the cassette's frames. The locator and fit (`components/portable/artwork`'s
`artwork_locate()`, `id3.c`, `flac.c`) and the TJpgDec decode in `artwork_loader.c` are
built on the panel board. The loader owns one 603x385 RGB565 cover buffer (464,310 B) and
starts it with the screen; the fit is still cover-style, centre-cropped, and the white
page scales the result to frame 0's width at draw time.

The old Case backdrop is still parked: `assets/labels/cover.jpg` (768,000 B decoded) and
the Label cover's source (`assets/labels/k7logo.png`) stay in the tree unembedded, with the
Label cover and Artwork screens. The cover needs neither: the cassette frames are the
frame. The host tier still builds and tests the locators and the fit
(`host/tests/t15_artwork.c`).

## Board-only properties

A host pass cannot establish the JPEG decode time or the card contention; those belong
to the artwork loader, and its board measurements are in the git history. The host tier
covers the locators and the fit (`host/tests/t15_artwork.c`) and the design
grid (`host/tests/t16_layout.c`).
