# The screen asks what it has

The screen display is one composition with two boards behind it, and until now the difference
was a preprocessor symbol threaded through every module. `CONFIG_APP_BOARD_SUNTON_8048S050C`
decided whether the white page's **Cover** existed, whether the **Peek** and the **Menu** were
built, whether the **Card** was mounted, and whether the cassette body was drawn on two border
frames. The modules that own those things were compiled on one board only, so the composition
could not be written once: `cassette_levels.c` was 157 lines of which seven functions were
`(void)x;` on the other board, and `cassette_view.c` carried sixteen conditionals around calls
that had nowhere to go.

Worse, one of them leaked. `cassette_card_window()` returned the Card's window rect on the
board that never mounts a Card, and the view clamps the tape's packs to that rect on the white
page - so the headless build clipped the tape to a hole with nothing behind it.

## Decision

- **A capability is a value the composition reads** (`screen_caps.h`): `cover` and
  `panel_borders`. The modules compile on both boards; a part that does not exist simply does
  not happen.
- **Only two things are left in the board's hands**: this file, which answers the question,
  and the adapters that are physically different - the RGB panel, its GT911, and the backlight
  pin. `host/tests/layering.sh` A2 keeps it that way: no other module in the tier may branch on
  the board symbol.
- **The board without hardware gets an adapter, not a branch**: `backlight_none.c` answers the
  same interface with "nothing is dimmed, nothing is held, nothing is fading", which is true,
  so the Menu's Backlight row and the screen's idle ladder compile everywhere.
- **The Peek, the Cover, the Menu and the artwork loader moved into the common source list.**
  TJpgDec's include path moved with the loader. The headless board now renders all of it; the
  parts that need a card or a finger simply have nothing to show.
- **A part decides whether it exists.** `cassette_cover_create()` asks `cover` itself rather
  than being called conditionally, because a print with no Card behind it is not a picture of
  anything - it drew a white slab and a red pill on the black cassette, which is how the
  decision was found.

## Considered and rejected

- **Keep the preprocessor.** It is what produced a module that erases itself and a
  composition written twice, and its failure mode was the silent one above.
- **A `display_port_t` of function pointers.** The panel, the touch controller and the
  backlight are already adapters selected at build time; a vtable would add indirection to
  calls that have exactly one implementation per build. The capability value is enough for the
  parts that genuinely vary, and `screen_display.c` stays the one file that selects hardware.
- **Compile the hardware drivers on both boards.** The RGB panel and the GT911 have no
  meaning without their hardware; they stay adapters.
- **Gate the view's cover calls on the capability.** Tried first; it leaves the module
  constructible in a state it cannot render. The part knowing its own capability is one rule
  instead of a rule at every call site.

## Consequences

- `cassette_levels.c` is real code on both boards rather than a stack of no-ops, and
  `cassette_view.c` has no board branches at all.
- The headless board now photographs scenes 4 (Menu), 5 (Real), 6 and 7 (the dials) and 8
  (the Peek) over HTTP, which it could not before - not because the hardware changed, but
  because the modules finally exist to be photographed. Scene 5 shows the white page with no
  print, which is exactly what "no Cover capability" means.
- The Card's 116 KB mask is still spent only where there is a white page.
- The panel's seven comparable scenes are unchanged: 0 px against the capture taken before
  this decision.
- A third board adds a `screen_caps_t` and its adapters, not a conditional:
  `screen_display.c` already asks the questions the composition needs.

**Status:** accepted and measured on the Sunton, both configs, 2026-10-30.
