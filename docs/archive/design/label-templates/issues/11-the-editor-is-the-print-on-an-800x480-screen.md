# 11 — The editor is the print on an 800x480 screen

**What to build:** the LVGL canvas builder edits the label at its one real size, and its
preview shows that label where it lands on the panel —
[`docs/specs/label-templates.md`](../../../../specs/label-templates.md) §1 and §2.

- **The print is 748x359, fixed.** The W/H fields and the `cw`/`ch` inputs go; `W()` and `H()`
  are the constants 748 and 359. The document no longer carries a size that can disagree with
  the packer's `space` check, and opening a template whose `space` is not 748x359 is refused.
- **The screen is 800x480 in both views.** One `#screen` div is the panel; the 748x359 `#stage`
  sits inside it at (25,19), so the edit view is the label on the real panel, not a 748x359
  canvas with no context. At 100% zoom the screen is 800x480 CSS pixels. The Run preview is the
  same div at real size (no zoom) with the 748x359 LVGL canvas at (25,19) and the stage
  hidden.
- **The screen background is editable.** The old "Canvas" colour becomes **Screen 800×480**:
  it paints the 800x480 preview behind the label (and the edit canvas's own backdrop), so the
  label can be judged against a dark, grey or white panel. It is editor state, not part of the
  template.

**Status:** done, proved in headless Chrome

- [x] The toolbar shows `748 × 359` and no size inputs remain.
- [x] A headless screenshot of the edit view is an 800x480 screen with the 748x359 label at
      (25,19).
- [x] A headless screenshot of Run is the same 800x480 screen at real size, with the label at
      (25,19) and a `#202020` background set through the Screen colour control.
- [x] A template whose `space` is not 748x359 is refused by Open.
- [x] `node --check` on the page's main script and `label_pack.py --check-editor` pass.

**Files:** `resources/LVGL canvas builder.html`.

**Tests:** headless Chrome, `node --check`, `label_pack.py --check-editor`; the panel is not
involved.

**Source:** [`label-templates.md`](../../../../specs/label-templates.md) §1 (the print's box and
its (25,19) on the panel) and §2 (`space` is 748x359).
