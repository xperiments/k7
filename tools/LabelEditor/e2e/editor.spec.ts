import { expect, test } from "@playwright/test";
import { fileURLToPath } from "node:url";

const sectionButton = (page: import("@playwright/test").Page, title: string) =>
  page
    .locator('aside[aria-label="Inspector"]')
    .getByRole("button", { name: title, exact: true })
    .and(page.locator("button[aria-expanded]"));

test("boots with the Design panel and one section open", async ({ page }) => {
  await page.goto("/");
  await expect(page.getByText("K7Art")).toBeVisible();
  await expect(sectionButton(page, "Template")).toHaveAttribute("aria-expanded", "true");
  await expect(sectionButton(page, "Canvas")).toHaveAttribute("aria-expanded", "false");
});

test("the print always has paper and its window, and no layer for either", async ({ page }) => {
  await page.goto("/");
  const layers = page.locator('aside[aria-label="Layers"]');
  await expect(layers).not.toContainText("LayerMask");
  await expect(layers.getByRole("button", { name: /LayerMask/ })).toHaveCount(0);

  // Paper is a design section, always there; its colour field is also named Paper, so the
  // header is found by its text rather than by its accessible name alone.
  const inspector = page.locator('aside[aria-label="Inspector"]');
  const paperHeader = inspector.locator("button[aria-expanded]").filter({ hasText: "Paper" });
  await expect(paperHeader).toHaveCount(1);
  await paperHeader.click();
  await expect(paperHeader).toHaveAttribute("aria-expanded", "true");
  await expect(inspector.getByLabel("Paper")).toBeVisible();
  await expect(inspector.getByText("Corner radius")).toBeVisible();

  // A new design already has paper, and a layer added anywhere prints on it.
  await page.getByRole("button", { name: /Rectangle \(R\)/ }).click();
  const box = await page.locator(".canvas-surface").boundingBox();
  await page.mouse.click(box!.x + 200, box!.y + 200);
  await expect(page.getByText("Aperture", { exact: false })).toHaveCount(0);

  // The Card's window is fixed and black, on top of the print: nothing under it is seen.
  const window = page.locator('[data-window="1"]');
  await expect(window).toBeVisible();
  const win = await window.evaluate((el) => {
    const s = getComputedStyle(el);
    return {
      left: s.left,
      top: s.top,
      width: s.width,
      height: s.height,
      radius: s.borderRadius,
      background: s.backgroundColor,
      pointerEvents: s.pointerEvents,
    };
  });
  expect(win).toEqual({
    left: "95px",
    top: "101px",
    width: "556px",
    height: "166px",
    radius: "83px",
    background: "rgb(0, 0, 0)",
    pointerEvents: "none",
  });

  const built = await page.evaluate(() => {
    const ed = (window as unknown as {
      __editor?: { buildTemplate(): { doc: unknown; errs: string[] }; objs: Array<Record<string, unknown>> };
    }).__editor;
    const r = ed!.buildTemplate();
    return {
      errs: r.errs,
      doc: r.doc as {
        label: {
          mask: unknown;
          aperture: unknown;
          prims: Array<Record<string, unknown>>;
        };
      },
      inMask: ed!.objs.some((o) => "m" in o),
      apertureLayer: ed!.objs.some((o) => "ap" in o),
    };
  });
  expect(built.errs).toEqual([]);
  expect(built.doc.label.mask).toEqual({ color: "#c7a867", r: 10 });
  expect(built.doc.label.aperture).toEqual({ x: 95, y: 101, w: 556, h: 166, r: 83 });
  expect(built.doc.label.prims.length).toBeGreaterThan(1);
  expect(built.inMask).toBe(false);
  expect(built.apertureLayer).toBe(false);
});

test("the Card's window is black in the real preview", async ({ page }) => {
  await page.goto("/");
  await page.evaluate(() => {
    (window as unknown as { __editor: { setRun(v: boolean): void } }).__editor.setRun(true);
  });
  await page.waitForFunction(
    () => {
      const ed = (window as unknown as {
        __editor?: { lvgl?: { module?: unknown; canvas?: unknown } };
      }).__editor;
      return !!ed?.lvgl?.module && !!ed?.lvgl?.canvas;
    },
    undefined,
    { timeout: 15_000 },
  );
  const px = await page.evaluate(() => {
    const ed = (window as unknown as {
      __editor: { lvBuild(): void; lvgl: { canvas: HTMLCanvasElement } };
    }).__editor;
    ed.lvBuild();
    const ctx = ed.lvgl.canvas.getContext("2d")!;
    const at = (x: number, y: number) => [...ctx.getImageData(x, y, 1, 1).data];
    // The window's centre is (95 + 556/2, 101 + 166/2); the paper at (10,10) is not it.
    return { window: at(373, 184), paper: at(10, 10) };
  });
  expect(px.window).toEqual([0, 0, 0, 255]);
  expect(px.paper).not.toEqual([0, 0, 0, 255]);
});

test("opens one section at a time, and the open one cannot be closed", async ({ page }) => {
  await page.goto("/");
  // Clicking the open section keeps it open: the panel never becomes an empty list of headers.
  await sectionButton(page, "Template").click();
  await expect(sectionButton(page, "Template")).toHaveAttribute("aria-expanded", "true");

  await sectionButton(page, "Canvas").click();
  await expect(sectionButton(page, "Canvas")).toHaveAttribute("aria-expanded", "true");
  await expect(sectionButton(page, "Template")).toHaveAttribute("aria-expanded", "false");
});

test("collapses the inspector to a rail and remembers it", async ({ page }) => {
  await page.goto("/");
  await page.getByRole("button", { name: "Collapse the inspector" }).click();
  await expect(page.getByRole("button", { name: "Expand the inspector" })).toBeVisible();
  await expect(page.locator('aside[aria-label="Inspector"]')).toHaveCount(0);

  await page.reload();
  await expect(page.getByRole("button", { name: "Expand the inspector" })).toBeVisible();
  await page.getByRole("button", { name: "Expand the inspector" }).click();
  await expect(page.getByRole("button", { name: "Collapse the inspector" })).toBeVisible();
});

test("an empty static text says what it is", async ({ page }) => {
  await page.goto("/");
  await page.getByRole("button", { name: /Text \(T\)/ }).click();
  const box = await page.locator(".canvas-surface").boundingBox();
  await page.mouse.click(box!.x + 200, box!.y + 200);

  const field = page.locator('aside[aria-label="Inspector"] input[aria-label="Text"]');
  await expect(field).toHaveValue("Text");
  await field.fill("");

  await expect(field).toHaveAttribute("placeholder", "What this text prints");
  await expect(page.getByText(/Empty prints nothing/)).toBeVisible();
  await expect(page.locator('aside[aria-label="Layers"]')).toContainText("empty");
  await expect(page.getByText("empty text")).toBeVisible();

  await field.fill("Hello");
  await expect(page.locator(".canvas-surface")).toContainText("Hello");
  await expect(field).toHaveValue("Hello");
});

test("imports a factory K7Art", async ({ page }) => {
  await page.goto("/");
  const k7art = fileURLToPath(
    new URL("../../../assets/k7art/k7.neon.json", import.meta.url),
  );
  await page.locator('input[type="file"][accept*=".json"]').setInputFiles(k7art);
  await expect(page.getByRole("tab", { name: /neon/ })).toBeVisible();
  await expect(page.getByRole("banner").getByText("12 layers")).toBeVisible();

  // The open document is the K7Art it was read from: one container, both sections.
  const exported = await page.evaluate(() => {
    const ed = (window as unknown as { __editor?: { buildTemplate(): { doc: unknown } } })
      .__editor;
    return ed!.buildTemplate().doc as {
      format: string;
      id: string;
      label: { prims: unknown[] };
      jcard: { prims: unknown[] };
    };
  });
  expect(exported.format).toBe("k7art");
  expect(exported.id).toBe("k7.neon");
  expect(exported.jcard.prims).toHaveLength(3);
});

test("migrates a retired label-template v1 into a K7Art", async ({ page }) => {
  await page.goto("/");
  // The retired format: `v` and `prims` at the top level, and no container.
  const retired = {
    v: 1,
    id: "label.old",
    name: "Old Label",
    space: { w: 748, h: 359 },
    canvas: { backgrounds: ["#909090"], active: 0 },
    slots: {},
    inks: {
      paper: "#c7a867",
      ink: "#151515",
      rule: "#8e7e4a",
      band: ["#c68b2e", "#a24e2c", "#8a5d34", "#6b7a2f", "#3b6e8f", "#7a3b52", "#b9ac8e", "#2f4b3a"],
    },
    fonts: {
      title: { ff: "Archivo Narrow 600", px: 32 },
      numeral: { ff: "Barlow Condensed 700", px: 78 },
      caption: { ff: "IBM Plex Mono 500", px: 18 },
    },
    prims: [
      { t: "rect", x: 0, y: 0, w: 748, h: 359, r: 10, fill: "paper", still: true },
      { t: "text", text: "HELLO", x: 40, y: 40, w: 120, font: "title", color: "ink", align: "left" },
    ],
  };
  await page.locator('input[type="file"][accept*=".json"]').setInputFiles({
    name: "label.old.json",
    mimeType: "application/json",
    buffer: Buffer.from(JSON.stringify(retired)),
  });
  await expect(page.getByRole("tab", { name: /Old Label/ })).toBeVisible();

  const exported = await page.evaluate(() => {
    const ed = (window as unknown as { __editor?: { buildTemplate(): { doc: unknown; errs: string[] } } })
      .__editor;
    return ed!.buildTemplate();
  });
  const doc = exported.doc as {
    format: string;
    id: string;
    label: { prims: Array<{ text?: string }> };
    jcard: { prims: unknown[] };
  };
  expect(exported.errs).toEqual([]);
  expect(doc.format).toBe("k7art");
  expect(doc.id).toBe("k7.old");
  expect(doc.label.prims.some((p) => p.text === "HELLO")).toBe(true);
  expect(doc.jcard.prims).toHaveLength(3);
});

test("groups two selected layers from the panel's actions", async ({ page }) => {
  await page.goto("/");
  await page.getByText("circle 2").click();
  await page.getByText("rect 1").click({ modifiers: ["Meta"] });
  await page.getByRole("button", { name: "Group selected (⌘G)" }).click();
  await expect(page.getByText("Group 1")).toBeVisible();
});

test("puts the Layers actions at the bottom, and drag replaces Raise and Lower", async ({ page }) => {
  await page.goto("/");
  await expect(page.getByRole("button", { name: "Raise" })).toHaveCount(0);
  await expect(page.getByRole("button", { name: "Lower" })).toHaveCount(0);
  const tree = page.locator('aside[aria-label="Layers"] [role="tree"]');
  const footer = page.locator('[data-layers-actions="1"]');
  await expect(footer).toBeVisible();
  const f = await footer.boundingBox();
  const t = await tree.boundingBox();
  expect(f!.y).toBeGreaterThan(t!.y + t!.height - 1);

  // A row dragged above the top row becomes the top layer.
  await tree
    .getByText("rect 1", { exact: true })
    .dragTo(tree.getByText("bpm", { exact: true }), { targetPosition: { x: 20, y: 2 } });
  const order = await page.evaluate(() =>
    (window as unknown as {
      __editor: { objs: Array<{ id: number }> };
    }).__editor.objs.map((o) => o.id),
  );
  expect(order).toEqual([2, 3, 4, 1]);
});

test("shows every row's lock without hovering", async ({ page }) => {
  await page.goto("/");
  const layers = page.locator('aside[aria-label="Layers"]');
  await expect(layers.getByRole("button", { name: "Lock", exact: true })).toHaveCount(4);

  await layers.getByText("circle 2").click();
  await layers.getByText("rect 1").click({ modifiers: ["Meta"] });
  await layers.getByRole("button", { name: "Group selected (⌘G)" }).click();
  await expect(layers.getByRole("button", { name: "Lock the group" })).toBeVisible();
});

test("holds a tooltip until the pointer rests", async ({ page }) => {
  await page.goto("/");
  const lock = page
    .locator('aside[aria-label="Layers"]')
    .getByRole("button", { name: "Lock", exact: true })
    .first();
  await lock.hover();
  const tip = page.locator('[data-slot="tooltip-content"]');
  await expect(tip).toHaveCount(0);
  await expect(tip).toBeVisible();
  await expect(tip).toContainText("Lock");
});

test("scales a group as one from the selection's corner", async ({ page }) => {
  await page.goto("/");
  await page.getByText("circle 2").click();
  await page.getByText("rect 1").click({ modifiers: ["Meta"] });

  const handle = page.locator('[data-handle="scale"]');
  await expect(handle).toHaveAttribute("aria-label", "Scale the selection");
  const before = await page.evaluate(() => {
    const ed = (window as unknown as {
      __editor: {
        zoom: number;
        selMembers: () => Array<{ id: number }>;
        selBounds: (mem: unknown[]) => { x0: number; y0: number; x1: number; y1: number };
      };
    }).__editor;
    const b = ed.selBounds(ed.selMembers());
    return { zoom: ed.zoom, b };
  });

  // Drag the corner out by half the selection's own size: one factor of 1.5 for every member.
  const box = (await handle.boundingBox())!;
  const from = { x: box.x + box.width / 2, y: box.y + box.height / 2 };
  const half = {
    x: ((before.b.x1 - before.b.x0) / 2) * before.zoom,
    y: ((before.b.y1 - before.b.y0) / 2) * before.zoom,
  };
  await page.mouse.move(from.x, from.y);
  await page.mouse.down();
  await page.mouse.move(from.x + half.x, from.y + half.y, { steps: 8 });
  await page.mouse.up();

  const after = await page.evaluate(() => {
    const ed = (window as unknown as {
      __editor: { objs: Array<{ id: number; x: number; y: number; w: number; h?: number }> };
    }).__editor;
    return ed.objs.map((o) => ({ id: o.id, x: o.x, y: o.y, w: o.w, h: o.h }));
  });
  const rect = after.find((o) => o.id === 1)!;
  const circle = after.find((o) => o.id === 2)!;
  // The default design's rect is 708x319 at (20, 20) and its circle 200x200 at (50, 60).
  const f = rect.w / 708;
  expect(f).toBeGreaterThan(1.4);
  expect(f).toBeLessThan(1.6);
  expect(rect.x).toBe(20);
  expect(rect.y).toBe(20);
  expect(Math.abs(rect.h! - 319 * f)).toBeLessThanOrEqual(1);
  // The member's position is scaled about the same anchor, and its box by the same factor.
  expect(Math.abs(circle.x - (20 + 30 * f))).toBeLessThanOrEqual(1);
  expect(Math.abs(circle.y - (20 + 40 * f))).toBeLessThanOrEqual(1);
  expect(Math.abs(circle.w - 200 * f)).toBeLessThanOrEqual(1);
  expect(Math.abs(circle.h! - 200 * f)).toBeLessThanOrEqual(1);
});

test("scales two J-card layers as one", async ({ page }) => {
  await page.goto("/");
  await page.getByRole("button", { name: "J-card", exact: true }).click();
  const layers = page.locator('aside[aria-label="Layers"]');
  await layers.getByText("title", { exact: true }).click();
  await layers.getByText("artist", { exact: true }).click({ modifiers: ["Meta"] });

  const handle = page.locator('[data-handle="scale"]');
  await expect(handle).toBeVisible();
  // The J-card's stage draws the 1300-unit sheet at half the label's zoom.
  const before = await page.evaluate(() => {
    const ed = (window as unknown as {
      __editor: {
        zoom: number;
        selMembers: () => Array<{ id: number }>;
        selBounds: (mem: unknown[]) => { x0: number; y0: number; x1: number; y1: number };
      };
    }).__editor;
    const b = ed.selBounds(ed.selMembers());
    return { display: ed.zoom * 0.5, b };
  });

  // Pull the corner in by a fifth of the selection: one factor of 0.8 for both texts.
  const box = (await handle.boundingBox())!;
  const from = { x: box.x + box.width / 2, y: box.y + box.height / 2 };
  const shrink = {
    x: ((before.b.x1 - before.b.x0) * 0.2) * before.display,
    y: ((before.b.y1 - before.b.y0) * 0.2) * before.display,
  };
  await page.mouse.move(from.x, from.y);
  await page.mouse.down();
  await page.mouse.move(from.x - shrink.x, from.y - shrink.y, { steps: 8 });
  await page.mouse.up();

  const after = await page.evaluate(() =>
    (window as unknown as {
      __editor: {
        jcard: { prims: Array<{ name: string; x: number; y: number; w: number }> };
      };
    }).__editor.jcard.prims.map((o) => ({ name: o.name, x: o.x, y: o.y, w: o.w })),
  );
  const title = after.find((o) => o.name === "title")!;
  const artist = after.find((o) => o.name === "artist")!;
  // The starter's two texts are 700 wide at x 540, the title at y 900 and the artist at 1060.
  const f = title.w / 700;
  expect(f).toBeGreaterThan(0.78);
  expect(f).toBeLessThan(0.82);
  expect(title.x).toBe(540);
  expect(title.y).toBe(900);
  expect(Math.abs(artist.w - 700 * f)).toBeLessThanOrEqual(1);
  expect(Math.abs(artist.y - (900 + 160 * f))).toBeLessThanOrEqual(1);
});

test("hides a layer with the panel's eye, and shows it again", async ({ page }) => {
  await page.goto("/");
  await page.getByRole("button", { name: "J-card", exact: true }).click();
  const layers = page.locator('aside[aria-label="Layers"]');
  const canvas = page.locator(".canvas-surface");
  await expect(canvas).toContainText("Neon Skyline");

  const row = layers.getByRole("treeitem").filter({ hasText: "title" }).first();
  await row.getByRole("button", { name: "Hide" }).click();
  // Off the sheet, and off the export: the K7Art is what the canvas shows.
  await expect(canvas).not.toContainText("Neon Skyline");
  await expect(row.getByRole("button", { name: "Show" })).toBeVisible();

  await row.getByRole("button", { name: "Show" }).click();
  await expect(canvas).toContainText("Neon Skyline");
});

test("copies a group on the J-card as a group", async ({ page }) => {
  await page.goto("/");
  await page.getByRole("button", { name: "J-card", exact: true }).click();
  await page.waitForFunction(() => !!window.__labelEditorMcp);

  await page.evaluate(async () => {
    const call = (name: string, args: Record<string, unknown> = {}) =>
      (window as unknown as {
        __labelEditorMcp: { call: (n: string, a?: Record<string, unknown>) => Promise<any> };
      }).__labelEditorMcp.call(name, args);
    const text = await call("k7art-add-layer", {
      surface: "jcard",
      type: "text",
      text: "Note",
      x: 520,
      y: 300,
      role: "ink",
      fontRole: "caption",
    });
    const rect = await call("k7art-add-layer", {
      surface: "jcard",
      type: "rect",
      x: 500,
      y: 250,
      width: 120,
      height: 60,
      role: "band.1",
    });
    await call("k7art-group-layers", {
      surface: "jcard",
      action: "group",
      ids: [text.id, rect.id],
    });
  });

  // The group row selects its members; copy takes the group, paste makes another.
  await page.getByText("Group 1", { exact: true }).click();
  await page.keyboard.press("Meta+c");
  await page.keyboard.press("Meta+v");
  await expect(page.getByText("Group 2")).toBeVisible();

  const groups = await page.evaluate(async () => {
    const read = await (window as unknown as {
      __labelEditorMcp: { call: (n: string) => Promise<any> };
    }).__labelEditorMcp.call("k7art-read");
    return (read.active.groups as Array<{ layers: number[] }>).map((g) => g.layers.length);
  });
  expect(groups).toEqual([2, 2]);
});

test("switches to the J-card, edits it, and keeps one document history", async ({ page }) => {
  await page.goto("/");
  await page.getByRole("button", { name: "J-card", exact: true }).click();

  const canvas = page.getByRole("region", { name: "J-card canvas" });
  await expect(canvas).toBeVisible();
  // The toolbar is the focus and the zoom; the sheet's numbers live in the Sheet panel.
  await expect(canvas.getByRole("button", { name: "Sheet", exact: true })).toBeVisible();
  await expect(canvas.getByLabel("Zoom")).toBeVisible();
  await expect(page.locator('aside[aria-label="Layers"]').getByText("3", { exact: true })).toBeVisible();

  // The J-card's inspector is its own: Sheet, Type and Template, not the label's Canvas and
  // Paper. Its test values are the label's, so there is no Fields panel here.
  await expect(sectionButton(page, "Sheet")).toHaveAttribute("aria-expanded", "true");
  await expect(sectionButton(page, "Canvas")).toHaveCount(0);
  await expect(sectionButton(page, "Paper")).toHaveCount(0);
  await expect(sectionButton(page, "Fields")).toHaveCount(0);

  // The label's own test values reach the J-card: one set for both surfaces.
  await page.locator('[aria-label="Surface"]').getByText("Label").click();
  // The Design panel is the one with no layer selected.
  await page.keyboard.press("Escape");
  await sectionButton(page, "Test values").click();
  const sim = page.locator('aside[aria-label="Inspector"] input[aria-label="Test value for Song Title"]');
  await sim.fill("Case Study");
  await expect(page.locator(".canvas-surface")).toContainText("Case Study");
  await page.locator('[aria-label="Surface"]').getByText("J-card").click();
  // The J-card's own three fields are not label fields: they keep the App's words, and the
  // panel that used to set them - Fields - is gone with the rest of the per-surface values.
  await expect(page.locator(".canvas-surface")).toContainText("Neon Skyline");

  // A layer added on the J-card is a J-card prim, and the same undo covers it.
  await page.getByRole("button", { name: /Text \(T\)/ }).click();
  const box = await page.locator(".canvas-surface").boundingBox();
  await page.mouse.click(box!.x + box!.width * 0.6, box!.y + box!.height * 0.5);
  await expect(page.locator('aside[aria-label="Layers"]')).toContainText("Text");

  await page.getByRole("button", { name: "Label", exact: true }).click();
  await expect(page.getByRole("region", { name: "Canvas" })).toBeVisible();
  await page.keyboard.press("Meta+z");
  await page.getByRole("button", { name: "J-card", exact: true }).click();
  await expect(page.locator('aside[aria-label="Layers"]').getByText("3", { exact: true })).toBeVisible();
  await expect(page.locator(".canvas-surface")).not.toContainText("Text");

  // Focus is the sheet or one part.
  await canvas.getByRole("button", { name: "Cover", exact: true }).click();
  await expect(canvas).toContainText("Cover · 820 × 1280");
  await canvas.getByRole("button", { name: "Sheet", exact: true }).click();
  await expect(canvas).toContainText("Whole sheet");
});

test("publishes a .k7artpack from the toolbar", async ({ page }) => {
  await page.goto("/");
  await page.getByRole("button", { name: "Publish a K7Art pack" }).click();

  const dialog = page.getByRole("dialog");
  await expect(dialog).toContainText("Publish a K7Art pack");
  await expect(dialog.getByRole("button", { name: "Publish and download" })).toBeEnabled();

  const downloadPromise = page.waitForEvent("download");
  await dialog.getByRole("button", { name: "Publish and download" }).click();
  const download = await downloadPromise;
  expect(download.suggestedFilename()).toBe("untitled.k7artpack");
});
