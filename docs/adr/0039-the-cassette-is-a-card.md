# The cassette on the white page is a card

**Amends [ADR 0021](./0021-white-page-cover.md):** the glow is retired and the label is no
longer the top of a stack whose middle is a light wash. On the white page the cassette itself
is one tinted rectangle - the **Card** - the Cover is printed on it, and the tape shows through
a pill window that the Card and the print share.

**Amended by [ADR 0040](./0040-the-cassettes-are-a-table.md):** the Card's ink is not the
Cover's dominant hue any more. A cassette is a row of `k_col_cassette[]` - a plastic and the
ink of the label printed on it - and the Track's path only picks the row. Everything about the
Card, the window and the mask below stands; only where the colour comes from changed.

ADR 0021 gave the white page a picture: a print on the cassette's body, a glow washing it, and
the cinta below. That page is good and it stays - the print, the tape, the paper and
the pose are all unchanged. What it never had was a **cassette**: at the Controls pose the label
and the outer frame are the same rectangle (402x188 at 148,102), the frame's border is 1 px and
is lifted over the print, and the only other shell pixels are three chips in a 44 px band under
the frame. So the screen could print a Track's picture on a cassette but could not say what
colour the cassette was - and the owner's ask was a variety of cassettes, which needs a body to
colour.

The body has to live **outside** the print's rectangle, and the one box that already does is
the one the glow occupied: 426x272 at (136,82), the tape's own ink (402x232) grown by 12 px at
the sides and 20 px at the top and bottom. That is where the Card goes - a tinted rectangle the
print sits on, and the tape behind it, which makes a cassette worth tinting and costs no asset
at all.

## Decision

- **The Card is 426x272 at (136,82), radius 12, one ink.** It is created after the tape body,
  so it sits above the mechanics and below the print, and it takes the glow's slot in the
  build. It is shown with the white page and hidden with it, in one frame - it is a reading of
  the Page, like the Cover.
- **One A8 mask, 426x272, shared by the Card and the print's container.** The stadium is
  zeroed at (67,75): 291x87, radius 43 - the largest radius the rectangle allows, half its
  shorter side, so its ends are the hubs' circles. The mask is 255 everywhere else. Stride
  428, so 116,416 B.
- **The print's container becomes 426x272, and the print is clipped at +12,+20 inside it.**
  That is the whole reason it grows: LVGL aligns a bitmap mask to the *centre* of its object's
  area and clears everything outside the mask's rectangle (`lv_draw_sw_img.c:1019`, the
  `side_area` clears), so the mask must cover its object and two objects can share one buffer
  only if they share a centre. The Card and the print container both sit at (136,82), centre
  (349,218), so one buffer serves both. The Cover's own 402x188 mask - 75,952 B, the number
  ADR 0021 chose - is deleted.
- **The print is cut in and out** (amended: ADR 0039 first had it fade over 1 s). The Card
  arrives with the page flip and the print arrives with it, in the same frame, so there is no
  fade to land on either of them.
- **The window is at (55,55) in the print and (67,75) in the Card.** Both are the same 291x87
  stadium on the panel at (203,157), which is the window the frame already drew: frame[1]'s
  interior, `trunc(106x134/256) = 55` in from the print's corner. It is not centred in the
  print - 55 left, 55 top, 56 right, 46 bottom - and must not be centred by hand.
- **The labels with no cover are LVGL-drawn, in the print box, and their ink is the
  complement.** Nothing is blitted: `cassette_box`, `cassette_border_box` and `cassette_text`
  already draw everything a label needs, and a drawn label has no alpha, so the window is the
  mask's, exactly as the Cover's is. The first label is the Track's name in Jura 28 in the print's
  top band - the window is the print's middle, so 55 px is all the room there is - and it stays
  down while a picture is decoding, or every Track would flash its name before its cover.
- **The Card's ink is the Cover's own.** The dominant hue of the decoded 603x385 buffer, at a
  fixed saturation and value, computed once inside `cassette_cover_set_image()` and applied as
  a cut - a colour animation would repaint the panel on every step. With no cover, five table
  inks in `cassette_style`, indexed from the Track's path, and the drawn label takes the
  complement (`hue + 180`, same saturation and value) so it contrasts on purpose.
- **The two packs' boxes clamp to the window while the Card is up.** `pack_draw_cb` draws its
  rings from the *hub's* screen centre, not from its own box, and `lv_obj_redraw()` truncates
  the layer to the object's box before `LV_EVENT_DRAW_MAIN_END` (`lv_refr.c:131`). So the pack's
  box is only an invalidation box: clamped to (203,157) 291x87 the rings do not move, they are
  clipped to the window, and the 25 Hz tick stops invalidating the Card outside it.
- **What the Card hides, it hides.** The two 1 px frame rings, the three chips, both rollers,
  the run and both strands are all inside 426x272 and under the Card. On the white page the
  cassette is the Card, the print, and the two rims turning behind the window. The Cover only
  ever shows on this page, so `cassette_tapes_raise_frames()` and
  `cassette_tapes_set_cover_print()` - the raise and the pill that kept the frames crisp over the
  print - have nothing left to do and go.
- **Nothing else moves.** The dark page, the Initial 256/256 cassette and the Peek keep the
  drawn line art, because Real only exists at the Controls pose.

## Consequences

- **+40,464 B of PSRAM.** One mask of 116,416 B replaces one of 75,952 B; the extra 40,464 B is
  the ring and the margins between the two rectangles, all 255. The ARGB layer that a masked
  object draws into is banded - `CONFIG_LV_DRAW_LAYER_SIMPLE_BUF_SIZE = 24576` (`sdkconfig`)
  gives about 14 rows at 426 px, so it is transient, not resident.
- **The reel tick gets cheaper, not dearer.** Today's tick invalidates the union of the two pack
  boxes, which at the Controls pose are 186x186 each (PACK_MAX_D 356, `L(356) = 186`) - a union
  of 388x186, 72,168 px - and both the tape and the masked print are re-rendered over it. With
  the packs clamped the union is the window, 25,317 px, and nothing outside it is touched.
- **The white page's cassette is a Card,** not a body seen end to end: a 12 px mat at the sides,
  20 px above, 64 px below the print, and two Ø86 rims behind a 291x87 window. A real cassette
  shows its tape that way too, and the drawn run, strands, rollers and chips were always the
  least cassette-like thing on the screen.
- **Five cassettes become five inks** (or, with a cover, one ink per Track), and the label's ink
  is chosen against it. The variety the owner wanted is a colour, not a set of pictures.

## Alternatives

- **Bake the glow into the Card.** It cannot be: the glow washes the *label* - `cassette_view.c`
  says so in as many words, "the glow washes the label but not the tape" - so it must sit above
  the print. Baked into a Card that sits below it, the print hides all but the 12 px rim. It
  would also show on the dark page, where the glow is hidden today.
- **Two masks.** The Card's 116,416 B beside the Cover's 75,952 B is 192,368 B, and two
  geometries to keep in step; one shared buffer is cheaper because the objects can be given one
  centre.
- **Clip the mechanics in a stadium container instead of masking the Card.** It costs nothing,
  but it does not buy what it looks like it buys: `lv_obj_invalidate()` uses the object's own
  coords and is clipped only to the display, not to the parent, so a Ø186 pack box would still
  invalidate outside the window and still re-render the Card there. It also re-bases every tape
  position and splits the frames out of the container.
- **The Card under the mechanics.** 0 B extra and no clamp, but a solid rect under the tape
  hides nothing, so there is no window and the tape path draws on the colour - the page this ADR
  is trying to get away from.
- **Prerender the layers as PNGs.** Fifteen labels at 402x188 are 1.13 MB of the 2.27 MB of free
  flash at one byte per pixel, and 3.4 MB at RGB565A8. And "grey levels for tint" cannot work:
  LVGL's recolor is a lerp toward the tint, not a multiply (`lv_draw_sw_img.c:640-652`), so at
  full opacity the grey is gone.
- **A photographic plate instead of a flat ink.** The label sits on frame[0]'s exact rectangle,
  so a plate under it has 1 px of ring and a 44 px band in which to be real. Making room is a
  bezel, and the owner kept the measured rectangles.
