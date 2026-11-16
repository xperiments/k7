# The K7 look: one near-black page, a primary and a secondary, line art

**Goal.** The App at `/app` and the Site at `/` are one instrument drawn as a technical
schematic: a thin line on near-black, an orange primary and a neutral ink, outline for what is
available and fill for what is current. The look is carried by the theme tokens and the installed
primitives, so a screen is not restyled one by one; and it is one look for both documents, because
`/` and `/app` share one stylesheet and one set of primitives. The device's one page, the Wi-Fi
setup at `/setup`, carries the same palette by hand in its inline stylesheet
(`components/portable/setup/setup_http.c`) — it has no stylesheet to share.

**Status.** Built: the tokens (LK-02), the shell (LK-03), the screens (LK-04, LK-05) and the Site
(LK-06); the palette was split into a primary and a secondary by ADR 0081, and the setup page
follows by hand. LK-07 is the verification. The calls this repository took against the guide that
preceded this file are recorded in [ADR 0080](../adr/0080-the-k7-look-is-the-products-one-skin.md)
and [ADR 0081](../adr/0081-the-look-has-a-primary-and-a-secondary.md), and marked **[ADR]** below.

**Reference.** The ten scene drawings in [`docs/images/screens/`](../images/screens/), all of them
in [`contact-sheet.png`](../images/screens/contact-sheet.png): scenes 0–4 and 6–8 are the K7 look;
scenes 5 and 9 are the grey "Real" takes and are not. The sheet is a device drawing, so its rail,
knobs and meters are the kit §5 says this App does not build; the ink, the frame, the filled logo
tile, the footer pill and the filled rows are what carry over.

**Vocabulary:** [`web/CONTEXT.md`](../../web/CONTEXT.md) — **App**, **Site**, **Area**,
**Route**. The App is specified in [`k7-app.md`](./k7-app.md), the Site in
[`k7-site.md`](./k7-site.md).

---

## 0. Read this first

**The order of work.** The tasks that carry it are named in brackets.

1. Audit the codebase. **[LK-02]**
2. Install the tokens. **[LK-02]**
3. Set the global CSS. **[LK-02]**
4. Restyle the primitives in `web/src/components/ui` and `web/src/components/reui`. **[LK-02]**
5. The shell. **[LK-03]**
6. The screens. **[LK-04, LK-05]**
7. Icons, images and assets. **[LK-04, LK-05, LK-06]**
8. Motion. **[LK-02, LK-03]**
9. Clean up and verify. **[LK-07]**

**Hard constraints.**

- One hue and one neutral: the orange accent (the **primary**) and a light neutral ink (the
  **secondary**), on a near-black page, plus alpha variants and one recessed well. The accent
  marks the brand, chips, the current button's border and text, and the scrollbar; the neutral
  carries text, control outlines, panel lines and the rail's current dome. No other colour
  anywhere, including error, success and warning states — except the device-unreachable badge
  (K22, [ADR 0085](../adr/0085-the-device-warning-is-the-one-extra-hue.md)). **[ADR 0081]**
- Dark theme only. Both documents carry `class="dark"` on `<html>`; there is no light palette and
  no theme control. **[ADR]**
- No shadows, gradients, blur, glow, photos, rounded photo thumbnails or multi-colour charts, in
  UI chrome. Two exceptions: the coverflow (§5.3), and content images (§5.4). **[ADR]**
- Change visuals only. Do not change props, behaviour, routing, data flow, or accessibility
  semantics. Keep every Base UI / Radix `data-*` and `aria-*` attribute the components already
  expose, because the styling hooks onto them.
- Installed components are owned code. Edit them in place in `web/src/components/ui` and
  `web/src/components/reui` rather than wrapping them. **[ADR]**
- When something in this guide does not fit a component, apply §1 and note the decision in the
  final report.

---

## 1. The visual rules

**K1. A control is an outline; a current mark is a fill.** A button and a chip are outlines — the
accent outline when it is the primary action, the current segment or a micro-label; the neutral
outline otherwise. A menu row, a list row and the rail's dome are neutral fills when current,
with dark text. This applies to navigation, tabs, filters, toggles, menu rows, chips, radio and
checkbox states.

**K2. Hierarchy comes only from outline vs fill, size and spacing.** No colour, weight, shadow or
elevation tricks.

**K3. Constant shell, swappable stage.** The frame, the sidebar and the footer bar never move.
Only the stage and the optional side panel change between screens.

**K4. Everything is a module in its own outlined cell.** Gutters are tight and even (5px).
Nothing floats without an outline unless it is plain text on the background.

**K5. No texture and no gradient, except the coverflow's own (§5.3).** The guide's speaker-grille
dot matrix is a device drawing; no screen in the App or the Site uses one, so nothing in this
repo does either.

---

## 2. The tokens

### 2.1 Values

The accent is the firmware's: `components/app/screen_display/` draws the panel with
`COL_PRIMARY 0xF15A22`, and the App uses it. The page is `#17181a`, the near-black of the
reference capture; the guide's `#121212` was its estimate. The neutral ink and the line have no
firmware value to borrow — they are the secondary the product was missing. **[ADR 0081]**

| Token | Value | Use |
| --- | --- | --- |
| `--k7-bg` | `#17181a` | Page background |
| `--k7-accent` | `#F15A22` | Brand, chips, a current button's border and text, scrollbar |
| `--k7-ink` | `#a6a8a8` | Text, control outlines, tree and menu rows |
| `--k7-ink-dim` | `#858787` | Secondary text, inactive icons |
| `--k7-line` | `#2e3030` | Panel edges, dividers |
| `--k7-surface` | `#222425` | Hover surface |
| `--k7-well` | `#121314` | Recessed fills (wells, inset panels) |
| `--k7-accent-dim` | accent at 42% | Decorative ticks, empty meters (never text) |
| `--k7-accent-faint` | accent at 18% | Tints |
| `--k7-on-accent` | `#111111` | Text and icons on the accent |
| `--k7-alert` | `#F2C94C` | The device-unreachable warning badge; the one hue outside the primary (K22, ADR 0085) |

**K6. The geometry is rem, not px.** The guide's pixel values are measurements of a 16px-root
document; this App sets `html { font-size: 130% }`, so the same rem value renders 30% larger and
every proportion holds. The panel radius is `0.4375rem` (7px at a 16px root, about 9px here); the
5px gutter is `gap-1` (about 5.2px here). Do not convert these to px to "correct" them.

| Metric | Value |
| --- | --- |
| Stroke | 1px everywhere, never thicker |
| Panel radius | `0.4375rem` |
| Pills, chips, inputs | fully round (`rounded-full`); the sidebar's dome buttons are 6px |
| Circle controls | icon buttons where a control is a circle; navigation stays rows |
| Gutter | 5px (`gap-1`) |
| Icon stroke | 1.25 (lucide `strokeWidth`) |

**K7. Contrast.** Measured on `#17181a`: the accent is 5.3:1, the neutral ink 7.4:1 and the dim
neutral 4.9:1 — all pass for text. Dark on the accent is 5.6:1. The 42% accent (1.9:1) is
decoration only and never informational text. The guide's 70% (3.3:1) and 85% (4.3:1) both fail;
the neutral replaces them.

**K8. One typeface, one weight.** Jura, self-hosted (`web/public/fonts/`), the same face the
device's LVGL build embeds. Weight 300 for everything; hierarchy comes from size only. Headings
are not bold. The only 500 is the micro-label chip. Numbers use `tabular-nums`.

**K9. Micro-labels are chips.** Uppercase, weight 500, on an outlined accent pill with accent
text, at `text-xs` — the ReUI floor, 12px at a 16px root and about 15.6px here. Nothing in the App is
below `text-xs`; the guide's 9–10px chips were device geometry. **[ADR]**

### 2.2 The stylesheet

This block replaces the `:root` and `.dark` palettes in [`web/src/index.css`](../../web/src/index.css).
The font faces above it and the existing `@theme inline` mappings of the semantic colours stay;
the lines below are added to them. Both documents (`web/index.html` and `web/app.html`) carry
`class="dark"` on `<html>`, so the `dark:` utilities in installed components resolve.

```css
:root {
  --k7-bg: #17181a;
  --k7-accent: #F15A22;
  --k7-ink: #a6a8a8;
  --k7-ink-dim: #858787;
  --k7-line: #2e3030;
  --k7-surface: #222425;
  --k7-well: #121314;
  --k7-on-accent: #111111;
  --k7-accent-dim: color-mix(in srgb, var(--k7-accent) 42%, transparent);
  --k7-accent-faint: color-mix(in srgb, var(--k7-accent) 18%, transparent);
  --k7-font: 'Jura', ui-sans-serif, system-ui, sans-serif;
}

:root, .dark {
  --background: var(--k7-bg);
  --foreground: var(--k7-ink);
  --card: var(--k7-bg);
  --card-foreground: var(--k7-ink);
  --popover: var(--k7-bg);
  --popover-foreground: var(--k7-ink);
  --primary: var(--k7-accent);
  --primary-foreground: var(--k7-on-accent);
  --secondary: var(--k7-ink-dim);
  --secondary-foreground: var(--k7-on-accent);
  --muted: var(--k7-well);
  --muted-foreground: var(--k7-ink-dim);
  --accent: var(--k7-surface);
  --accent-foreground: var(--k7-ink);
  --destructive: var(--k7-accent);
  --destructive-foreground: var(--k7-on-accent);
  --border: var(--k7-line);
  --input: var(--k7-line);
  --ring: var(--k7-accent);
  --radius: 0.4375rem;
  --chart-1: var(--k7-accent);
  --chart-2: var(--k7-accent-dim);
  --chart-3: var(--k7-accent-faint);
  --chart-4: var(--k7-accent);
  --chart-5: var(--k7-accent-dim);
  --sidebar: var(--k7-bg);
  --sidebar-foreground: var(--k7-ink);
  --sidebar-primary: var(--k7-accent);
  --sidebar-primary-foreground: var(--k7-on-accent);
  --sidebar-accent: var(--k7-surface);
  --sidebar-accent-foreground: var(--k7-ink);
  --sidebar-border: var(--k7-line);
  --sidebar-ring: var(--k7-accent);
  /* State is a word now: the names resolve to the neutral if a file still reads one. */
  --success: var(--k7-ink-dim);
  --success-foreground: var(--k7-ink);
  --info: var(--k7-ink-dim);
  --info-foreground: var(--k7-ink);
  --warning: var(--k7-ink-dim);
  --warning-foreground: var(--k7-ink);
  --invert: var(--k7-ink);
  --invert-foreground: var(--k7-bg);
  color-scheme: dark;
}

@theme inline {
  --color-k7-bg: var(--k7-bg);
  --color-k7-accent: var(--k7-accent);
  --color-k7-ink: var(--k7-ink);
  --color-k7-ink-dim: var(--k7-ink-dim);
  --color-k7-line: var(--k7-line);
  --color-k7-surface: var(--k7-surface);
  --color-k7-dim: var(--k7-accent-dim);
  --color-k7-faint: var(--k7-accent-faint);
  --color-k7-well: var(--k7-well);
  --font-sans: var(--k7-font);
  --radius-panel: 0.4375rem;
  /* Kill every built-in shadow utility. */
  --shadow-2xs: none; --shadow-xs: none; --shadow-sm: none; --shadow-md: none;
  --shadow-lg: none; --shadow-xl: none; --shadow-2xl: none;
}

@layer base {
  html, body {
    background: var(--k7-bg);
    color: var(--k7-ink);
    font-family: var(--k7-font);
    font-weight: 300;
    letter-spacing: 0.02em;
  }
  *, *::before, *::after { border-color: var(--k7-line); }
  :focus-visible { outline: 2px solid var(--k7-accent); outline-offset: 2px; }
}
```

**K10. Delete the light palette and the control that chose it.** `web/src/ui/theme.ts`, the
`startTheme()` call in `web/src/main.tsx`, the footer button in `web/src/ui/Shell.tsx`, the
palette sentence in `web/CONTEXT.md`'s **Settings** entry and the light `:root` block all go. The
`meta[name="theme-color"]` in both documents becomes `#111111`. **[ADR]**

**K11. Global rules.** Delete any `body` gradient or background image. Scrollbars are thin, with
an accent thumb on the background track. Selection is accent on dark
(`::selection { background: var(--k7-accent); color: var(--k7-bg); }`). Disabled is `opacity-40`
plus `pointer-events-none`, never grey.

---

## 3. The procedure

### 3.1 Audit

1. List every file in `web/src/components/ui` and `web/src/components/reui`, and every custom
   component under `web/src/ui`. Record which installed components the App and the Site use.
2. Run the greps in §9.1 and save the counts as a baseline.
3. Note where the theme is set: `web/index.html`, `web/app.html`, `web/src/main.tsx`,
   `web/src/ui/theme.ts`. Tailwind is v4, installed through `@tailwindcss/vite`; `components.json`
   is `base-nova` (Base UI), so selection attributes are `data-[pressed]`, `data-[selected]`,
   `data-[checked]` and `data-[state=…]`, not Radix's `data-[state=on]`.
4. List every route and map it to a pattern in §6. A route with no match is the shell plus the
   modules its content needs.

### 3.2 The rest

1. **Tokens** (§2): replace the palettes, force dark, delete the switcher.
2. **Global** (§2.2 K11): scrollbars, selection, disabled.
3. **Primitives** (§4): work top to bottom; after each, render it in every state (default, hover,
   focus-visible, active/selected, disabled) before moving on.
4. **The shell** (§5.1).
5. **The screens** (§6): one area at a time; the coverflow stays as it is.
6. **Icons, images, assets** (§7).
7. **Motion** (§8).
8. **Verify** (§9): no report until every box is checked.

---

## 4. The primitives

**K12. Installed files are the look.** Edit `web/src/components/ui/*` and
`web/src/components/reui/*` in place. Keep each component's props, its `render` prop, and every
`data-*` and `aria-*` attribute; the styling hooks onto them. Never wrap an installed component to
change its look, and never hand-roll one it already provides. ReUI's rules still hold where this
spec is silent: `gap-*` spacing, nothing below `text-xs`, the accessibility the component ships.
An edited file is a fork of that file's look: a later `shadcn add` over it would overwrite it, so
an update is a manual diff. **[ADR]**

**K13. The general recipe.** `border border-primary bg-transparent text-primary shadow-none`.
Selected/active/on flips to `bg-primary text-primary-foreground`. Hover never changes hue:
`hover:bg-primary/15` on outline items, and no hover change on filled items. No `transition-colors`
above 100ms, and no easing (§8).

**K14. The mapping.**

The table is the whole guide; this App installs only the rows it uses — Button, Badge, Card,
Input, the menus, the overlay, Tooltip, Alert, Avatar, Skeleton, Separator, Switch, Breadcrumb,
Progress and Sidebar. A row that names a component not installed here is the treatment to apply
if one is.

| Component | K7 treatment |
| --- | --- |
| Button | Outlined pill in the neutral ink (`border-foreground/45 text-foreground`). `default` = outline. `solid` = accent outline, border and text (the primary action only; the name is historical). `ghost` = no border until hover, then a 1px neutral border. `destructive` = identical to default (no red). `link` = underline, neutral. Icon buttons are circles. The fill is reserved for chips, rows and the rail's dome |
| Badge | Pill outline: the accent variant is the micro-label chip (accent border and text, uppercase, weight 500, `text-xs`); the quiet variants a neutral border and text. No fill. The `alert` variant is the one yellow, used only for the device-unreachable badge (K22, ADR 0085) |
| Card | 1px neutral line (`border-border`), panel radius, transparent background, no shadow, `p-3`. Title weight 300 |
| Input / Textarea / Select trigger | Transparent, 1px neutral outline (`border-foreground/40`), `rounded-full` (textarea: panel radius), text in the neutral ink, placeholder `text-foreground/50`, focus is a 2px accent ring |
| Label / Form description | Neutral text; the description at the dim neutral (`text-muted-foreground`) |
| Checkbox | 16px square, 4px radius, neutral outline. Checked is an accent fill with a dark check |
| Radio | Neutral outlined circle. Checked is a filled accent inner dot |
| Switch | Pill track in the neutral outline. Checked is an accent track with the thumb in the background colour. Unchecked thumb is the neutral at 50% |
| Toggle / ToggleGroup | Pills, spaced with `gap-1.5` (not joined segments). On is an accent outline |
| Tabs | Trigger is a pill. Active is an accent outline. The list has no background or border; the underline indicator is removed |
| Slider | 1px track at neutral 40%, range in the accent, thumb a 14px outlined circle on the background (filled while dragging) |
| Progress | 4px pill, accent to the value, neutral after |
| Select / Dropdown / Context / Menubar / Command items | Row style: icon + label + right chevron or value. Highlighted or selected is a full-width filled neutral bar with dark text, 4px radius |
| Popover / Dropdown / Select / HoverCard content | Panel: background, 1px neutral line, panel radius, no shadow |
| Dialog / Sheet / AlertDialog | Panel as above. Overlay is `bg-background/80`, no `backdrop-blur`. Close is a circle icon button |
| Tooltip | Small outlined pill in the neutral, neutral text, no arrow fill |
| Table | No zebra, no filled header. Rows separated by `border-b border-border`. Header text `text-muted-foreground`. Selected row is a filled neutral bar |
| Accordion / Collapsible | Each item an outlined panel. An open item's header is an accent outline only when it is the current selection; otherwise it stays neutral. Chevron icon, no rotation easing |
| Alert | Outlined panel in the neutral line with a line icon. All variants identical (no colours) |
| Sonner / Toast | Outlined panel on the background, pill action button |
| Avatar | Outlined circle with initials or a line icon. A content portrait is §5.4 |
| Skeleton | `bg-foreground/10`, no pulse |
| Separator | 1px `bg-foreground/20` |
| Breadcrumb / Pagination | Neutral text, chevrons as line icons; the current page is the neutral ink. A filled pill only where it is a control |
| Calendar | Day cells are circles. Selected is an accent filled circle, today is a neutral outlined circle, outside days at 42% |
| Sidebar | The rail (§5.1): the logo tile in the accent, the rows and domes in the neutral, the current dome filled with the secondary, counts as accent numbers |
| Navigation Menu | Pill triggers, panel content |
| Scroll Area | Thin scrollbar, accent thumb |

**K15. Button reference.** The App's Button keeps its `render` prop and its cva; this is the class
string it should end up with.

```tsx
const buttonVariants = cva(
  'inline-flex shrink-0 items-center justify-center gap-2 whitespace-nowrap rounded-full border border-foreground/45 bg-transparent font-light tracking-wide text-foreground outline-none transition-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 focus-visible:ring-offset-background disabled:pointer-events-none disabled:opacity-40 aria-pressed:border-primary aria-pressed:text-primary data-[pressed]:border-primary data-[pressed]:text-primary data-[selected]:border-primary data-[selected]:text-primary [&_svg]:size-4 [&_svg]:shrink-0 [&_svg]:stroke-[1.25]',
  {
    variants: {
      variant: {
        default: 'hover:bg-foreground/10',
        solid: 'border-primary bg-transparent text-primary hover:bg-primary/10',
        ghost: 'border-transparent hover:border-foreground/45',
        destructive: 'hover:bg-foreground/10',
        link: 'rounded-none border-transparent underline underline-offset-4',
      },
      size: {
        sm: 'h-7 px-3 text-xs',
        default: 'h-9 px-4 text-sm',
        lg: 'h-11 px-6 text-base',
        icon: 'size-10 p-0',
      },
    },
    defaultVariants: { variant: 'default', size: 'default' },
  }
)
```

**K16. Row reference.** Menu rows, rail rows and table rows share it.

```tsx
const row = 'flex items-center gap-2.5 rounded-[4px] px-2.5 py-1.5 text-foreground outline-none data-[highlighted]:bg-foreground data-[highlighted]:text-background data-[state=checked]:bg-foreground data-[state=checked]:text-background aria-selected:bg-foreground aria-selected:text-background [&_svg]:size-4 [&_svg]:stroke-[1.25]'
```

Put a trailing value (for example `OFF`) or a chevron at the far right with `ml-auto`. Values are
small text in the same colour, not a badge.

### 5.1 The shell


**K17. The shell is one frame: a rail of dome buttons, the stage, the footer bar.** The logo at
the top of the rail toggles it above 768px; below 768px the rail is a drawer, opened by the
logo in the stage header and closed by the logo at its top; the destinations and Settings are **dome buttons** — a plaque with a circular dome holding the icon at its left and the label beside it,
6px corners, full width of the rail and stacked flush with a shared 1px edge (the one shape
exception to the pill rule); the install control is a dome button in the rail's footer. In the
default state the button is outlined and transparent, with a neutral icon and label; the current
one fills only its dome with the secondary and turns its icon dark, while the button, its border
and the label stay neutral. Above 768px the rail rests collapsed to its icons: the top logo
is the toggle, the labels, the group rules and the counts go `sr-only`, the rail narrows to
`w-16`, and the icons carry tooltips — the logo, every row, Install and Settings — after a
two-second hover. Expanding it is the remembered choice; the first visit is collapsed. Below
768px the rail is hidden; the stage header's logo opens it as the collapsed rail over the
stage, with a backdrop behind it and the mark at the drawer's top to close it. No `K7Shell`. **[ADR]**

- The frame is 1px neutral line, panel radius, 5px padding and 5px gutters; a 1px line divides
  the rail from the stage, the rail runs the frame's full height, and the footer bar is the
  content's footer inside the stage column.
- The stage header's right is the **card switcher**, a secondary control rather than a third
  action: the player's card and the SD are two ghost buttons, and the virtual cards are one
  button or a **Virtual cards** menu. The current unit is the accent outline, the menu's trigger
  names the current virtual card, and the header gains no strip. The Media Sources header names
  the area and the view; the card label and the back arrow are gone. Below 768px the two unit
  buttons drop their labels to their glyphs, the virtual control keeps the card's name, the other
  areas' breadcrumb drops its area label and separator, and the header's gutter narrows so the
  page name and the switcher both fit.
- The area is the sidebar's group and the page is its item: **Media Sources > Sources**, **Media
  Sources > Albums**, **Playlists**. The album, artist, Drive
  and Video pages are **Media Sources >** that page; the Player's page says what it shows
  (**Player > Now playing**); Settings has no area. The header and the sidebar read one
  `NAV_GROUPS` definition, so a renamed item cannot drift from its breadcrumb.
- The editor's page is the list's own name rather than a word for the screen, and that crumb is
  the field that renames the list: pressing the name opens a pill input of the same metrics,
  Enter or leaving it commits, Escape abandons it, and an empty name is refused by returning the
  one the list has. The name is typed where it is shown, so the editor's body starts with its two
  columns and carries no second title. The area beside it is the link back to the list of lists;
  the editor adds no back control of its own, so below 768px — where the area label is dropped —
  the way back is the rail.
- The logo is the accent wordmark on the page's dark ground — the inverse of the reference's filled
  tile, with no tile fill — and its wordmark is the only bold text in the system. It sits at the top of the rail above 768px — centred when the rail is collapsed — and at the
  top of the drawer below it; the stage header carries the mark only as the drawer's opener.
- Counts are accent numbers at the end of the row; each nav group's label is the uppercase word in
  the dim neutral followed by a 1px neutral rule to the rail's edge — no pill, no border.
- The PlayerBar is the content's footer: a strip at the stage column's bottom, under a 1px top
  rule, its scrub solid to the playhead and dim after.

### 5.2 The kit

**K18. The App builds no new K7 component kit.** The guide's §5 pieces are the device's; here the
installed primitives carry the look, and a composite is a variant in an installed file, not a new
component.

| Guide | In this repo |
| --- | --- |
| 5.1 Shell, 5.2 NavButton | The frame, the filled logo tile and the dome button (K17); no `K7Shell` |
| 5.3 FooterPill / ProgressPill | The PlayerBar (§5.1) |
| 5.4 MenuList / MenuRow | The row style (K16) in the sidebar, menus and tables |
| 5.5 ModePills | Single-select groups drawn as filled/outlined pills |
| 5.6 Knob, 5.7 VuMeter, 5.8 SpeakerGrille | Not built: no screen in the App or the Site has a knob, a meter or a grille |
| 5.9 StatePill | The Badge's filled variant, carrying the state's word |
| 5.10 CassetteArt | The coverflow's own case, preserved (§5.3) |

### 5.3 The coverflow

**K19. The album shelf is the one preserved surface.** `web/src/ui/album-shelf.tsx` and the
`.k7-shelf` / `.k7-case` / `.k7-mini` / `.k7-case-shelf` rules in `web/src/index.css` keep
their drawing, their `hueOf` case colours, their 3D geometry, their masks and their 300ms
`cubic-bezier(0.22, 1, 0.36, 1)` turn. They are not re-drawn as line art, not monochromed, and
not retimed. The stage around them is K7. **[ADR]**

The Player page keeps a case beside the device's own cassette screen (K29): the same face,
frame and sheen, not a second drawing. Where the two stand side by side the case is the screen's
height; below that breakpoint they stack, and the case keeps the shelf's width.

An album with no picture is still drawn: its face is one of the cassette case reference's five
covers — a gradient with a ring, colour bands with a cream box, an off-white with a colour
rule, a retro sunburst, a dark grid — seeded by its name, with its title, artist and cassette
meta. The case, its `hueOf` colour, geometry, masks and timing are unchanged; the drawn face is
cover art inside the preserved case, and its words hide when the face is too small to carry
them. The same face fills the album card and the player bar's mini case.
Every face of every case carries the sheen, so a spine has the same moulded gloss as a front.
A case under the pointer comes towards the reader — a side case 10rem, the active front
4rem — while the row steps back and turns towards the hovered side; the left and right turns
are set separately in the stylesheet. The perspective stays at 90rem. The pointer is read
from a flat hit layer at the cases' rest places, not from the moving case, so a turning case
cannot pull the hover out from under itself. A carousel drag can begin on any case, not only the
turned one, and the hover layer is off while the drag is in progress. The wheel is the other way
along the row — a notch, or a trackpad's travel adding up to one, turns one case — and it is
taken from the page rather than left to scroll it. The moves take the case's own 600ms curve, and
the shelf carries 1.5rem of extra height.

### 5.4 Content images

**K20. An image that is content keeps its colour; an image that is chrome does not exist.** Album
covers (the 422px AVIF cache) and the Site's hero (the panel drawing, or a future device
photograph in S11's slot) are content: they render in full colour inside an outlined frame, with
no shadow, no rounded thumbnail and no grayscale or multiply blend. UI chrome is 1px line art and
SVG in `currentColor`; there is no raster logo, icon or decorative photograph. **[ADR]**

---

## 6. The screens

**K21. Every area is the shell plus modules.** One outline per module, one micro-label chip at
the module's top-left as its title, and the current thing filled. The area is the sidebar's
group; the page is its item.

| Page | Pattern |
| --- | --- |
| Sources | A column of outlined panels: the drop zone, the virtual-card shelf, the queue rows, the run controls. The Device card shows the device's facts — the library, the Snapshot and Rescan — under the shelf, beside the K7 cassette drawing. The queue's running state is a filled row and a filled action |
| Albums | The coverflow exactly as it is (§5.3), with the album detail, artist and video routes as outlined panels around it |
| Playlists | The selected card's lists as rows, each row filled by how far its list has reached; New playlist is one dialog. The list's name is the stage header's page and is renamed there. The editor's body is two columns: the blocks, with the current block filled, on the left, and the card's tree with one search-or-ask bar at its foot on the right. The blocks' heading carries the edit's own state — writing, on the card, on the player, waiting, or the refusal's sentence — over the count and the rows the library is missing, and clearing the list is a trash icon whose press opens the filled confirm |
| IA Agent | Two views at the top of the editor's sources column, as round outlined icons — the card's folders and the results — the current one taking the accent, and the results dead until there is something in them. One bar at the tree's foot: a pill field, then Filters, the magnifier and Ask as icons, the primary filled. An answer's rows are the results view, drawn as the list's own rows a size smaller (a number, the file, its BPM and key, a plus) in the answer's order, with only the offer to add them below the tree, and the note that says dropped ranges are why when they were. A failure stands where the answer would and leaves the view where it is. When the model is not ready the bar is there for the search, with one sentence, and one solid button where the model can still be fetched (**Set up the assistant**), and the download's progress is a `Progress`. The Intent is behind **Filters**, a sheet of rows and controls. Errors are K22 |
| Settings | A column of outlined panels, one per group. Labels left, controls right, pill inputs |
| Now playing | The broadcast: what is on air, its case and the device's own cassette screen (K29), and the position, and what the device says when it cannot answer. The PlayerBar stays the control |
| Site | See [`k7-site.md`](./k7-site.md): the hero, the flash section and the install section are modules, and the footer is the licence trail |

**K22. States are shape and words, not hue.** Every data region has an empty state (with one
action), a loading state (a `Skeleton` in the real layout) and an error state (a retry, plus a
sentence naming the failure). An error or a warning is a filled bar or panel with a line icon and
its word — "Unmarked card", "The model did not answer" — never a second colour. The one exception
is the device-unreachable badge in the stage header: a yellow outline, a line alert icon and its
word ([ADR 0085](../adr/0085-the-device-warning-is-the-one-extra-hue.md)), because the link state
has to be seen before the card switcher; data errors stay neutral. A destructive confirm is a
panel whose action is the filled variant (the `AlertDialog` treatment, K14). **[ADR]**

---

## 7. Icons, images and assets

**K23. One icon library, one stroke.** Lucide, `strokeWidth={1.25}` everywhere (a global
`[&_svg]:stroke-[1.25]` or a wrapper), `currentColor`, line style only. No filled, duotone,
coloured or emoji icon. The rail glyphs of the device are not this App's icons; the sidebar's
icons are the ones the areas already use.

One icon is drawn in this repository instead: the rail's **Albums** glyph, a frame over three
thumbnails ([`gallery-glyph.tsx`](../../web/src/ui/gallery-glyph.tsx)), because the glyph the owner
chose has no Lucide twin. It keeps the rest of the rule — `currentColor`, line art, the 24-unit
grid — but draws its 1-unit walls by fill, because a 1.25 stroke at the rail's 16px closes the
three thumbnails into one bar. It is the only such glyph.

**K24. No charts, and the rule if one arrives.** The App has no charting dependency. A chart
added later follows the guide: `var(--chart-1)` strokes at 1px, no fills, no gradients, no dots
unless a point is selected, grid at 42% with `1 4` dash, series separated by dim vs solid or
dashes rather than hue, and axes at `text-primary/90` and `text-xs`.

**K25. Assets.** The logo and every glyph are SVG in `currentColor`, inlined where CSS variables
must reach them (the sidebar logo uses a mask for that reason). The Device card's cassette is the
same line art, painted through a mask rather than in the orange it is drawn with. Covers and the
Site's hero are content (§5.4). No external font or CSS: Jura is self-hosted and preloaded.

---

## 8. Motion

**K26. Mechanical.** Nothing eased, bouncy, blurred or faded. Overlays cut, or fade opacity 0→1
over at most 100ms linear; `zoom-in-*`, `slide-in-*` and `animate-in` are removed, as is
`backdrop-blur`. `Skeleton` does not pulse. `transition-all` and `transition-shadow` become
`transition-colors` or nothing. Hover is an instant ink swap, or at most 100ms linear. The
spinner stays: it is feedback, not decoration. The one exception is the coverflow's 300ms curve
(K19). **[ADR]**

**K27. Reduced motion means none.** Under `prefers-reduced-motion: reduce`, no transition and no
rotation; not a calmer fade. The Player's cassette screen draws one held frame (K29): the packs
show how much tape is on each side, and the hubs sit still.

**K28. Scene changes are cuts.** Changing route swaps the stage and keeps the shell; there is no
page transition and no entrance animation.

**K29. The Player shows the device's screen.** The Player's right column is the panel's own entry
view, drawn as an SVG at its 800x480 grid in the panel's inks: the two frames and the three chips,
the two packs of concentric rings, the two hubs with their turning dividers, the guides, the
strands and the bottom run. The shelf's case stands beside it at the screen's height (K19), not
instead of it. The reels follow the broadcast: each turns at the tape speed at its own pack radius,
counter-clockwise, in the device's 36 frames; the packs fill from elapsed/duration with the
device's 80/1000 floor and square-root law; a track change plays the device's 8 s rewind. The
page's title, badges and progress sit under the screen.

---

## 9. Verification

### 9.1 Automated checks

All must return zero hits outside the exceptions named below.

```bash
# Hard-coded colours (the theme file and the coverflow rules are the exceptions).
grep -rnE '#[0-9a-fA-F]{3,8}\b|rgba?\(|hsla?\(|oklch\(' web/src --include=*.tsx --include=*.ts --include=*.css \
  | grep -v 'index.css'

# Tailwind palette colours.
grep -rnE '(bg|text|border|ring|fill|stroke|from|to|via|outline|divide|shadow)-(red|orange|amber|yellow|lime|green|emerald|teal|cyan|sky|blue|indigo|violet|purple|fuchsia|pink|rose|slate|gray|zinc|neutral|stone|white|black)(-[0-9]+)?' web/src

# Forbidden decoration.
grep -rnE 'shadow-|bg-gradient|from-|to-|via-|backdrop-blur|blur-|drop-shadow|ring-offset-[0-9]' web/src

# Wrong radii and weights.
grep -rnE 'rounded-(lg|xl|2xl|3xl)|font-(semibold|bold)' web/src

# Raster images in UI chrome.
grep -rnE '<img|next/image|\.(png|jpg|jpeg|webp)' web/src
```

**Allowed exceptions:** `shadow-none`; the coverflow's own rules in `index.css` and
`album-shelf.tsx` (its masks, shading, hues and weights); `font-medium` on the micro-label chip;
the focus ring offsets on the primitives; the content images — covers, video posters and the
Site's hero — which are `<img>` on purpose; the `.jpeg(` call in `src/audio/covers.ts`, which the
image grep reads as a file name; and `from-`/`to-`/`via-` inside prose and `auto-*`, which the
decoration grep reads as utilities.

### 9.2 Visual checklist

- [ ] Background is flat near-black; no gradients or images in chrome.
- [ ] Every visible stroke is 1px — the accent on the brand and primary controls, the neutral
      line on panels and rows.
- [ ] Text and icons are the neutral ink; orange appears only on the brand, chips, current
      segments, primary actions and the scrollbar — as borders and text, never as a button fill.
- [ ] In every list, tab group, nav and toggle set, exactly one item is filled and the rest are
      outlined — the accent for a segment or chip, the neutral for the rail's dome.
- [ ] Buttons, inputs, chips and toggles are pills. Panels use the panel radius. Circle controls
      are circles.
- [ ] The shell (frame, sidebar, footer bar) is identical on every screen.
- [ ] No shadows or blurs, including on dialogs, dropdowns and toasts.
- [ ] Typography is weight 300; the only bolder text is the chip and the logo wordmark.
- [ ] Charts, if any, have no colours beyond accent, dim and faint.
- [ ] Disabled items are dimmed, not grey.
- [ ] The coverflow is unchanged in its own surface, and the stage around it is K7.
- [ ] Content images are in colour and in an outlined frame.

### 9.3 Behavioural checklist

- [ ] Every keyboard interaction still works, and every focusable element shows the 2px accent
      focus ring.
- [ ] Screen-reader roles and labels are unchanged; composites expose the right roles.
- [ ] Informational text uses the neutral ink (7.4:1) or its dim (4.9:1), never the 42% accent.
- [ ] Reduced motion disables rotation and transitions.
- [ ] The App works at desktop and at 800x480, and the Site at both.
- [ ] The production build size did not grow meaningfully (fonts and SVG only).

### 9.4 Final report

1. The changed files, grouped by step.
2. Before/after counts for every grep in §9.1.
3. Components or screens that could not follow the rules, the reason and the closest compromise.
4. Any token tuned, and why.

---

## 10. Do and don't

**Do:** use outline and fill for state; keep gutters at 5px; put one micro-label chip on each
module; reuse the same three or four shapes; use `currentColor` for all SVG; keep text at weight
300; keep the orange for the brand and the current choice, and let everything else be the neutral
ink.

**Don't:** add a second hue for errors or success; add shadows, glow or blur; use filled cards;
use bold headings; mix radii; use photos in chrome; animate with easing; introduce a gradient
other than the coverflow's own.
