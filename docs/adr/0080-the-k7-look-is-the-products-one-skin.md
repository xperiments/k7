# The K7 look is the product's one skin, and ReUI is its component source

The App at `/app` and the Site at `/` share one `web/src/index.css` and one set of installed
primitives, but they were carrying two ideas of themselves: a light, warm-paper palette carried
over from the hand-written stylesheet (the App is a library on a page, not the device's OLED), a
**Theme** toggle that let a browser choose the dark one, and four semantic state colours
(`success`, `warning`, `info`, `destructive`) that the screens use for meaning. A design guide for
the K7 look then arrived with the opposite premises: one near-black background, one orange accent,
no second hue for any state, dark only, and the installed components edited in place rather than
wrapped. Neither idea survives the other, and the shared stylesheet means "App only" is not a
cheaper option — it is a scoping exercise across every primitive.

Two facts settled it. The K7 system has no light expression: the accent on white is about 3.1:1,
so a light K7 would need a different hue, which its first rule forbids. And the guide's component
table (pill buttons, outlined transparent cards, filled menu rows, pill inputs) cannot be
delivered through tokens alone in the installed files.

## Decision

- **The App and the Site wear one skin, dark only.** [`k7-look.md`](../specs/k7-look.md) is the
  contract: near-black `#111111`, the firmware's accent `#F15A22`, outline for what is available
  and fill for what is current, Jura at weight 300, 12px micro-label chips, mechanical motion.
- **The Theme is retired.** No light palette, no toggle, no `theme.ts`; both documents carry
  `class="dark"` on `<html>`, and **Settings** remembers the Provider, the model, the Instance and
  the worker count.
- **One hue.** State is carried by fill/outline, an icon and a word; `success`, `warning`, `info`
  and `destructive` do not exist as colours, and a destructive confirm is the filled action in an
  `AlertDialog`.
- **ReUI is the component source, and the spec governs.** Components stay installed rather than
  hand-rolled, and the spec names where an installed file is edited in place; ReUI's spacing,
  text floor and accessibility still hold where the spec is silent.
- **The coverflow is the one preserved surface.** The album shelf keeps its drawing, its
  `hueOf` case colours, its 3D geometry and its 300ms curve; the stage around it is K7. Cover
  images and the Site's hero are content, not chrome, and keep their colour.
- **The firmware's values win where it has them** (`COL_BLACK 0x111111`, `COL_PRIMARY 0xF15A22`,
  Jura); the guide's `#121212` and `#ff5a1f` were estimates of the same things, and Quicksand was
  its fallback for a typeface this repository already has.

## Considered options

- **Keep the light palette and apply K7 only in dark.** Two brands, two primitive treatments, and
  the guide's rules false in one of them; orange on white cannot carry body text anyway.
- **Keep the four semantic hues.** It contradicts the one-hue rule the whole look rests on, and
  errors would read as a second brand colour rather than as the K7 language.
- **Keep ReUI as the governing skill and add a K7 layer.** The component table cannot be reached
  without editing the installed files; a wrapper layer would duplicate them and the look would be
  half-K7.
- **Scope the K7 tokens to the App and leave the Site light.** The stylesheet and primitives are
  shared, so this needs scoped variants of every primitive — more code to preserve a second
  identity on the front door.

## Consequences

- There is no light mode, and no setting that offers one; a person who needs a light page has the
  browser's own tools, not the App.
- Errors and confirmations must be written, not coloured: a failed action names itself and uses a
  filled panel or bar. Screens that relied on a red or green badge need new words.
- An installed file edited in place is a fork of that file's look: a later `shadcn add` over it
  overwrites the edit, so a ReUI update is a manual diff. This is the cost of the look, chosen
  over wrapping.
- The coverflow is the only surface exempt from the palette and the motion rules; every other
  gradient, shadow and raster in UI chrome goes.
- The Site follows the App's palette; [`k7-site.md`](../specs/k7-site.md) S15 now names the look.
- **Theme** leaves [`web/CONTEXT.md`](../../web/CONTEXT.md)'s glossary, and the light palette
  leaves **Settings**.
