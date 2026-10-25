# The transport moves to a left-edge drawer, and the horizontal swipes drive it

**Amended by ADR 0019:** the vertical Level gestures this ADR weighed the horizontal
swipes against are gone. The up swipe is the peek and the down swipe does nothing; the
drawer's left/right swipes, the 134/256 control pose and its arbitration are unchanged.

The cassette screen's tap and one-finger left/right swipe were the transport: a tap skipped
to the next track and the swipes were next/previous. The product wants the transport
visible at the left edge - three buttons, shown and hidden by the horizontal gestures. That
removes the swipe transport: one left/right swipe can no longer both skip a track and open
a drawer.

## Decision

- The drawer is a 120 px strip at the screen's left edge, off-screen and hidden at rest. A
  one-finger right swipe slides it in, a left swipe slides it out. A tap stays next track,
  a down/up swipe stays the Levels and a two-finger swipe stays reserved for the menu.
- The drawer holds three 96x96 buttons stacked down the left edge, 12 px apart. The group
  is centred vertically, so the middle button, R, sits on the screen's centre line. Each is
  line art only: a rectangle with 8 px corners, a 64 px circle at its centre, and one
  letter - **P**, **R**, **W** - centred in the circle in Jura 36.
- Each button prebuilds two state objects, unpressed and pressed. A press swaps which one
  is visible, and release or `LV_EVENT_PRESS_LOST` restores the other. A click is a short
  scale pulse (256 -> 232 -> 256) and nothing else; the transport is not wired yet.
- While the drawer is out the cassette is scaled to 160/256. The 300 px-tall design box is
  then centred on y=240, the same line as R. The tape is 500 px wide, which leaves the
  120 px drawer clear, and its bottom edge clears the Label bar, so the Label's -60 px lift
  is not added on top. The view arbitrates the two room requests and the drawer wins while
  it is out.
- The drawer is chrome above every sheet. It is created after the Levels, and the view
  raises it after a vertical swipe because the Artwork sheet raises itself when it settles.

## Considered options

- **Keep left/right as next/previous and put the buttons behind another gesture.** The
  product wants the horizontal gestures to own the drawer, and there is no other natural
  two-direction gesture free: both vertical directions are already the Levels and the
  Label's peek.
- **Use an up/down swipe to show the drawer.** It would collide with the Levels at every
  position and with the Label's peek at Bare.
- **Leave the drawer permanently visible.** It would cover the left of the tape and the
  label all the time; the gestures exist to give it room only when it is wanted.
- **Wire the buttons to the transport now.** Their positions, names and semantics are the
  design question in flight. A placeholder that acts would become behaviour to unlearn; a
  click that only scales proves the hit target and the two states without committing to a
  command.
- **Scale only the tape, not the whole cassette.** The cassette is one picture and the pose
  already scales all of it; 160/256 is the same mechanism as the Label's 200/256.

## Consequences

- `screen_input` loses `SCREEN_INTENT_NEXT`/`PREV` and gains
  `SCREEN_INTENT_BUTTONS_SHOW`/`HIDE`. A left/right swipe no longer skips a track; the tap
  remains next track.
- A new `cassette_buttons` module (create, set_shown, is_shown, raise) is added to
  `screen_display`. It owns the drawer, its three buttons and their two states; the view
  owns the pose and the arbitration between the drawer and the Label.
- The Label's room request is now composed with the drawer's. While both are up the
  cassette stays at 160/256 and the Label's -60 px lift is dropped: the tape already
  clears the bar.
- A button bubbles gestures to the panel and the panel to the screen, so a swipe that
  starts on a button still opens or closes the drawer. The release of that swipe also
  arrives as a click; a button ignores a click that carries a gesture direction or a moved
  press, so it does not pulse as well.
- The host tier does not compile LVGL, so the drawer, the state swap and the pulse need a
  finger on the panel; the build is the host-side check.
- [ADR 0014](./0014-three-level-swipe-order-and-menu-gesture.md) still holds for the three
  Levels and the reserved two-finger gesture; only the horizontal transport is superseded.
- Amended after tuning on the panel: the drawer's pose is 134/256 with a -51 px dx, so the
  tape's 402 px box runs from x=148 to 550 rather than the 500 px at 160/256. Same
  decision - give the drawer room - with a smaller tape and a left-shifted one.
- Superseded in part by [ADR 0018](./0018-dj-eq-knobs-and-transport-arrows.md): the tap no
  longer skips a track (the DJ block's arrows do), and the drawer's control pose also shows
  the DJ block on the right. The drawer still asks for the 134/256 pose and still slides
  away to restore the full entry view.
