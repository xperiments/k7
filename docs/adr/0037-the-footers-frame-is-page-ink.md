# The footer's timeline is framed in the page's ink

**Amends [ADR 0032](./0032-the-footer-shows-the-timeline.md).** That record made the footer's
timeline the Peek's bar whole, "with its paper fill and its primary border", on both pages.

The owner, on the white page's footer:

- "footer title etc on white should have a grey/black border not orange."

## Decision

- **The bar's frame is page ink, its contents are not.** On the dark page the timeline keeps the
  Peek's `k_col_primary` ring, because there it *is* the Peek's bar; on the white page the ring
  drops to `k_col_glass`, the palette's grey for the page's own lines, so the bar reads as a card
  printed on paper rather than as the brightest outline on the screen.
- **Only the frame turns.** The times, the title and the track's fill stay `k_col_primary` on both
  pages: the playing ink is the one colour the white page keeps, as the hubs, the knobs' marks and
  the meter's dots already do. The bar's own fill stays `k_col_paper`, so the card is dark on
  paper and the orange inside it is a signal, not furniture.
- **The band's border is untouched.** It stays the chrome's `k_col_button_rect` on both pages,
  like the bars' cells and the header's frame - the footer's *band* was never the orange the owner
  meant.
- **Nothing else changes**: the same rect, the same 260 ms slide, the same three faces and the
  same layout numbers.

## Considered options

- **Black (`k_col_bg`)**, which is the white page's line ink. Rejected for the frame: the card's
  fill is `k_col_paper` (`k_col_bg` too), so a black border on it stops being a border at all -
  the owner asked for one of the two, and grey is the one that still draws.
- **No border on the white page.** Rejected for the same reason, and it would change the bar's
  2 px geometry between pages.
- **Invert the card on paper** (white fill, black frame and text). Rejected: that is a second
  decision, the owner asked about the border, and the bar would stop being the Peek's bar - which
  is the point of ADR 0032.
- **Turn the Peek's bar too.** Rejected: the Peek only exists on the dark page, where primary is
  its ink, so there is nothing to turn.

## Consequences

- ADR 0032's "the same paper fill and primary border" is true on the dark page; on the white one
  the frame is `k_col_glass`. The copy is still the Peek's bar in its rect, its insets, its track
  and its faces.
- `k_col_glass` gets its second user - it was the white page's edge mark - and stays what its
  comment says: the page's line ink.
- The footer is the only place the timeline is painted on the white page, so no other module has
  a page-dependent version of it.
