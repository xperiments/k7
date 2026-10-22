# The one-finger swipe cycles three Levels, and the two-finger swipe waits for the menu

The cassette screen had four Levels - Bare, Label, Label cover and Artwork - and the
one-finger down/up swipe moved through all of them. The Label level was a cream bar with
the **Now playing** title, sitting between Bare and the K7 sticker. The product wants
three screens in the one-finger cycle instead: the player with nothing over it, the K7
sticker and Media/Covers; the label bar is not one of them. The two-finger swipe is the
gesture the menu (on/off) will take, so it can no longer be the K7 cover's shortcut.

## Decision

The screen has three Levels, in this order: **Bare** (the cassette alone, no labels until
the up swipe peeks the bar), **Label cover** (`k7logo` full-bleed) and **Artwork** (the
Case with the Track's picture, the Media/Covers screen). A one-finger down/up swipe moves
one Level - down from Bare to Label cover to Artwork, up the other way - and the ends do
not wrap. The Level changes in one frame, as before.

The Label level is removed as a Level: its enum value, its slot in the order and its
sheet are gone. The cream bar comes back as an **overlay on Bare only**. At Bare the up
swipe has no Level above to move to, so it peeks the bar instead: the first up swipe
slides it in, while it is up either vertical swipe slides it out, and any Level change
puts it away at once. A down swipe at Bare hides the bar before it is allowed to move to
the Label cover, so the first down swipe dismisses it and the second one navigates. It
carries the same cleaned title as the K7 cover and the Artwork band, and it is not a sheet
in the Level machine: no other Level can show it, so the order stays three.

The two-finger swipe is reserved for the menu. It moves no Level: the view's callback
latches the gesture and logs it, so the same touch cannot also reach the one-finger
handler or the click (both would otherwise act on the first contact), and the menu will
take the callback later without a change in `touch_gt911.c`. Until the menu is designed,
two fingers do nothing visible.

## Considered options

- **Keep four Levels and skip Label in the navigation.** A smaller diff, but it leaves an
  unreachable sheet, an enum value that means nothing, and a direct-jump path no gesture
  uses. The state machine would say four and behave as three.
- **Keep the cream bar as the first screen and make Bare unreachable.** Not the requested
  order: the first screen is the player with no labels.
- **Leave the title only on the K7 cover and the Artwork band.** The smallest screen, but
  the product wants the old small label readable from the first screen without changing
  Level; the overlay gives it to the up swipe that otherwise does nothing there.
- **Let a tap peek the label instead of the up swipe.** A tap is the transport (next
  track) on every screen; taking it away on Bare would make one screen behave differently
  from the other two. The up swipe at Bare is the gesture with nothing to do.
- **Make the two-finger swipe open the menu now.** The menu is still to be designed; a
  placeholder that toggles something would become a behaviour to unlearn. The gesture is
  held, and the detector stays wired.
- **Leave the two-finger swipe on the K7 cover and give the menu another gesture.** The
  menu is the one screen that should be reachable from everywhere, and two fingers is the
  natural chrome gesture; the K7 cover is now one swipe away by itself.

## Consequences

- `LEVEL_LABEL` and the label's place in the order are gone from `cassette_view.c`.
  `LEVEL_MAX` is `LEVEL_ART` on the panel board and `LEVEL_BARE` on the headless build.
- `s_paper`, `s_title` and the label constants are back, board-only, as a Bare overlay:
  the up swipe at Bare peeks `paper_set_shown()`, a down swipe hides it before the Level
  moves, its 260 ms slide is kept, and `set_level()` calls `paper_hide_now()` so it never
  rides over the K7 cover or the Artwork screen. The panel board's VU is moved below the
  paper again, so the arcs cannot paint over the cream bar.
- The Label cover, the Artwork band and the Bare overlay all show the cleaned title from
  `components/portable/cassette`.
- The two-finger callback in the view is a stub: it sets the latch and logs "reserved for
  the menu". `touch_gt911.c` still detects the gesture from the two contacts, and the
  wake-only path still keeps it out of a press that only wakes the backlight.
- The vocabulary calls the bar a **Label** and says it is an overlay on Bare, not a
  Level. [ADR 0013](./0013-artwork-level-and-case-window.md) is superseded in part (three
  Levels; two fingers reserved).
- The board pass is owed: the new order, the up-swipe peek and the two-finger no-op need
  a finger, because the host tier does not compile the LVGL view.
