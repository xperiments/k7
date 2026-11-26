# 04 — The Site's page: hero, install, footer

**What to build:** the page in [`docs/specs/k7-site.md`](../../../../specs/k7-site.md) §2 — the ReUI
page that holds the flash section from ticket 03 and the install section of A10.

- **The hero** carries the product's positioning sentence verbatim (campaign.md §4.1) and
  `resources/cassette-drawer-800x480.png` in a **slot**: one image, one frame, replaceable by a
  photograph of the device without relaying the page. There is no photograph in the repository
  and the page does not wait for one.
- **The flash section** is ticket 03's component, with the prerequisites as its first block: a
  USB-C data cable, a computer, Chrome or Edge. A phone is told before it taps.
- **The install section** offers the control only when `readInstallState` can be `installable`
  **and** `queryLocalNetwork()` is anything but `unsupported`; an installed window sees *Open the
  app*; Safari, Firefox and every iOS browser get the sentence that names Chrome, and no control.
  A10's *Share → Add to Home Screen* sentence is retired here (k7-site.md S13).
- **The footer** links the repository and `THIRD_PARTY_NOTICES`. No analytics, no cookie, no
  third-party script.
- The page is ReUI: installed components, semantic tokens, `gap-*`, nothing below `text-xs`, at
  most one entrance, `transition-colors` for hover, `duration-200
  ease-[cubic-bezier(0.22,1,0.36,1)]` for the one reveal.

**Blocked by:** 01 (the Site's document), 02 (the version it shows), 03 (the flash section).

**Status:** pending

- [ ] `/` renders the hero, the flash and the install, in that order, with `#flash` and `#install`
      anchors.
- [ ] The install control appears only where the App can run; the copy for Safari, Firefox and
      iOS names Chrome and offers no install.
- [ ] An installed window sees *Open the app*, not the install control.
- [ ] The hero image is a slot fed by one file; replacing it with a photograph is a one-file
      change.
- [ ] The footer links the repository and the notices.
- [ ] No client-side route, no analytics, no third-party request.
- [ ] `npm run build` still refuses the development origin (D14).

**Files:** `web/src/site/Site.tsx` (new), `web/src/site/Install.tsx` (new),
`web/src/site/hero.*` (new), `web/public/site/` (the hero image), `web/index.html`, `web/src/ui/install.ts`
and `web/src/device/localNetwork.ts` (read only, reused).

**Tests:** k7-site.md T3, T10 (the section without Web Serial).

**Source:** ADR 0078, ADR 0079.

## Comments
