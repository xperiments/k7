# The look has a primary and a secondary, not one hue

[ADR 0080](./0080-the-k7-look-is-the-products-one-skin.md) made the look one hue: every text,
outline and control was the accent, and hierarchy came from size and fill alone. On a real screen
that left nothing quiet — a list of tracks, its labels and its numbers all competed with the
brand — and it forced secondary text to 90% of the accent (4.7:1) to stay legible, which is
almost the accent again. The reference capture has a different balance: the orange is rare (the
brand tile, the chips, the current segment, the scrollbar) and everything else is a light
neutral — text, control outlines, panel lines, and even the rail's current dome.

The neutral is also the only way to have a genuine second level of text at AA: the dim neutral
measures 4.9:1 where the guide's 70% and 85% accents measured 3.3 and 4.3.

## Decision

- **The orange is the primary.** It marks the brand tile, the chips, the current segment (the
  Carousel/Drive switch, a mood pill, a tab), a filled action, the scrollbar and the focus ring.
- **The neutral ink is the secondary.** It carries text, control outlines, tree and menu rows,
  panel lines, dividers and the rail's current dome; its dim tone carries secondary text.
- **The tokens:** `#17181a` page, `#F15A22` accent, `#a6a8a8` ink, `#858787` dim, `#2e3030` line,
  `#222425` hover surface, `#121314` well, `#111111` on the accent.
- **Contrast, measured:** accent 5.3:1, ink 7.4:1, dim 4.9:1, dark on accent 5.6:1. The 42%
  accent (1.9:1) stays decoration only.
- [`k7-look.md`](../specs/k7-look.md) K1, K7, K14, K17 and the token table carry the new split.

## Considered options

- **Keep the one hue.** It is simpler, and it was already built; but every control is the brand,
  the secondary level of text has to be nearly the accent to pass, and the eye has no quiet tone.
- **A second hue (the covers' teal).** The reference has no second hue, and colour would then
  carry two meanings at once. The neutral is a tone, not a hue.
- **The guide's 70% accent for secondary text.** 3.3:1 on this page, below AA.

## Consequences

- Text and controls are neutral; orange is scarce, so it now means “primary” rather than “ink”.
- The rail's current dome fills with the neutral, not the accent. That reverses the orange dome
  asked for during the button rounds, in favour of the reference.
- The coverflow keeps its colours; it is still the one preserved surface.
- ADR 0080's one-hue rule is superseded; its dark-only skin, coverflow exception and ReUI-as-
  component-source decisions stand.

## Supersedes

ADR 0080's one-hue rule.
