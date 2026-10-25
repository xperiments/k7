# The down swipe brings the controls in, and the controls page is white

The vertical swipes had one meaning each: the up swipe raised the peek and the down swipe
put it away, and a down swipe with the peek down did nothing at all. The owner wants the
down swipe to be the way into the transport drawer and the DJ block - the controls the
horizontal swipes already open - and wants that entry to read as a page of its own: the
whole backdrop white, so the orange line art sits on paper instead of on the screen's
near-black. The white page belongs to the **down swipe**, not to the controls: the
horizontal swipes keep showing the same drawer and block on the dark screen they had.

## Decision

- **The vertical pair is a ladder, and the view owns it.** `screen_input` reports the
  direction - `SCREEN_INTENT_DOWN` and `SCREEN_INTENT_UP` - and not a screen name, because
  a vertical swipe means two things depending on what is on screen. `PEEK_SHOW` and
  `PEEK_HIDE` are gone. The view's rule: **down** shows the controls, unless the peek is up
  - then it only puts the peek away - and does nothing while the controls are already out;
  **up** takes the controls away, and with none out it raises the peek (still only from the
  100% pose, and not while the peek is already up).
- **One overlay at a time.** Entering the controls by either route closes an open peek, so
  neither controls page carries the peek's black bar and tags. The peek was already unable
  to be raised while the controls were out; this is the other half of the same rule.
- **The page follows the gesture, not the controls.** A down swipe that brings the
  controls in paints the screen's backdrop `k_col_white` (0xFFFFFF) instead of `k_col_bg`
  (0x111111). The horizontal swipes keep their controls on the dark screen: the white page
  is the vertical ladder's, and a control shown from a side swipe is not the down swipe's
  page. A down swipe while the controls are already out (from a horizontal swipe) still
  asks for its page. Every close - up swipe or either horizontal swipe - returns the page
  to the dark screen. The flip is one frame, as the drawer's slide starts: a colour
  animation would repaint the whole 800x480 panel on every step, and the panel's frame
  budget is the tight resource (ADR 0004, `panel-rgb-lvgl-performance.md`).
- **The page-ink parts turn with the page.** The three chips along the bottom of the tape
  are windows - they are filled with whatever is behind the cassette - so
  `cassette_tapes_set_page()` repaints them `k_col_white` on the white page and `k_col_bg`
  at rest. That is the only fill in the body that carries the page's colour; the rest of
  the cassette is line art.
- **The horizontal swipes are unchanged.** Either one still toggles the controls, on the
  dark page as before. The view has one function for both ways in and out,
  `controls_set_shown(shown, white)`, and only the down swipe passes `white`.

## Considered options

- **Report a `CONTROLS_SHOW` intent on the down swipe and keep the peek's names.** It
  reads well until the peek is up: the same down swipe then means `PEEK_HIDE`, and the
  report would be a name that depends on state the reporter does not have. The direction is
  the part `screen_input` actually knows.
- **Give the down swipe the controls from every state, and let the peek stay up behind
  them.** The peek's bar and tags are `k_col_paper` (the near-black page) with the playing
  red's border; on white they read as two black cards, and the bar sits over the drawer's
  bottom button. Closing the peek is one line and keeps the page honest.
- **Cross-fade the backdrop over the drawer's 260 ms.** Every step of a fade is a
  full-screen background change, which invalidates the whole panel rather than the drawer
  and the cassette; the flip is one invalidation and the moving parts hide it.
- **Make only the drawer's and the DJ block's carcases white, on the dark page.** The owner
  asked for the page, not the buttons: the line art is what needs the contrast, and a white
  carcase fill would leave the orange stroke on orange-brown.
- **Wait for the peek's slide-out before flipping the page.** The page would then turn
  under a moving drawer. It flips at the same moment the chrome starts to move, so the
  picture is one page for the whole slide.

## Consequences

- `screen_input.h`'s enum is `BUTTONS_SHOW`, `BUTTONS_HIDE`, `DOWN`, `UP`, `VU_SHOW`,
  `VU_HIDE`; the header's contract now says the vertical axis is reported as the direction.
- `cassette_style` gains `k_col_white`, and `cassette_tapes` gains
  `cassette_tapes_set_page(bool)` - the three chips are the only page-ink fill in the body,
  and they are compiled only on the panel board, like the frames.
- The two hub rims are now created with the playing red instead of black. They used to keep
  a black border until the first reel change tinted them, which was invisible on the dark
  page and would have shown as a black ring on the white one; the tapes' ink cache already
  starts full, so the border now starts in the state the cache claims. `k_col_black` had no
  other reader and is gone.
- The controls can be entered two ways (a horizontal swipe either side, on the dark page,
  or a down swipe, on the white one) and left two ways (either horizontal swipe, or an up
  swipe). A down swipe while the controls are out leaves the drawer and the block where they
  are and only turns the page white, so the down swipe always means the white controls.
- `page_set()` holds the page in a flag, so a request for the colour already on screen does
  not invalidate the whole panel. The buttons' room callback no longer touches the page: it
  reports the drawer's slide, and the page is the intent's.
- The white page costs one whole-screen invalidation per flip, in addition to the drawer's
  and the DJ block's slides. It is not animated and does not repeat while the controls are
  out.
- The host tier does not compile the screen, so the page, the ladder and the chips need a
  finger on the panel; the Sunton and Devkit builds are the host-side check.
- [ADR 0016](./0016-transport-drawer-and-horizontal-swipes.md) is amended for the vertical
  swipes but keeps the drawer, its pose and the horizontal toggle; [ADR
  0019](./0019-remove-label-and-artwork-screens.md)'s "a down swipe has no meaning" is
  superseded.
