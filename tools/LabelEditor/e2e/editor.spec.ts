import { expect, test } from "@playwright/test";
import { fileURLToPath } from "node:url";

const sectionButton = (page: import("@playwright/test").Page, title: string) =>
  page
    .locator('aside[aria-label="Inspector"]')
    .getByRole("button", { name: title, exact: true })
    .and(page.locator("button[aria-expanded]"));

test("boots with the Design panel and one section open", async ({ page }) => {
  await page.goto("/");
  await expect(page.getByText("Label Editor")).toBeVisible();
  await expect(sectionButton(page, "Template")).toHaveAttribute("aria-expanded", "true");
  await expect(sectionButton(page, "Colors")).toHaveAttribute("aria-expanded", "false");
});

test("opens one section at a time, and the open one cannot be closed", async ({ page }) => {
  await page.goto("/");
  // Clicking the open section keeps it open: the panel never becomes an empty list of headers.
  await sectionButton(page, "Template").click();
  await expect(sectionButton(page, "Template")).toHaveAttribute("aria-expanded", "true");

  await sectionButton(page, "Colors").click();
  await expect(sectionButton(page, "Colors")).toHaveAttribute("aria-expanded", "true");
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
  await expect(page.getByRole("banner").getByText("13 layers")).toBeVisible();

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
  expect(exported.jcard.prims).toHaveLength(4);
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
  expect(doc.jcard.prims).toHaveLength(4);
});

test("groups two selected layers from the panel header", async ({ page }) => {
  await page.goto("/");
  await page.getByText("circle 2").click();
  await page.getByText("rect 1").click({ modifiers: ["Meta"] });
  await page.getByRole("button", { name: "Group selected (⌘G)" }).click();
  await expect(page.getByText("Group 1")).toBeVisible();
});

test("switches to the J-card, edits it, and keeps one document history", async ({ page }) => {
  await page.goto("/");
  await page.getByRole("button", { name: "J-card", exact: true }).click();

  const canvas = page.getByRole("region", { name: "J-card canvas" });
  await expect(canvas).toBeVisible();
  await expect(canvas).toContainText("Sheet 1300 × 1280");
  await expect(canvas).toContainText("Safe 820 × 820");
  await expect(page.locator('aside[aria-label="Layers"]').getByText("4", { exact: true })).toBeVisible();

  // The J-card's inspector is its own: Sheet, Type and Fields, not the label's Canvas and Mask.
  await expect(sectionButton(page, "Sheet")).toHaveAttribute("aria-expanded", "true");
  await expect(sectionButton(page, "Canvas")).toHaveCount(0);
  await expect(sectionButton(page, "Paper")).toHaveCount(0);

  await sectionButton(page, "Fields").click();
  const title = page.locator(
    'aside[aria-label="Inspector"] input[aria-label="J-card Title test value"]',
  );
  await expect(title).toHaveValue("Neon Skyline");
  await title.fill("Case Study");
  await expect(page.locator(".canvas-surface")).toContainText("Case Study");

  // A layer added on the J-card is a J-card prim, and the same undo covers it.
  await page.getByRole("button", { name: /Text \(T\)/ }).click();
  const box = await page.locator(".canvas-surface").boundingBox();
  await page.mouse.click(box!.x + box!.width * 0.6, box!.y + box!.height * 0.5);
  await expect(page.locator('aside[aria-label="Layers"]')).toContainText("Text");

  await page.getByRole("button", { name: "Label", exact: true }).click();
  await expect(page.getByRole("region", { name: "Canvas" })).toBeVisible();
  await page.keyboard.press("Meta+z");
  await page.getByRole("button", { name: "J-card", exact: true }).click();
  await expect(page.locator('aside[aria-label="Layers"]').getByText("4", { exact: true })).toBeVisible();
  await expect(page.locator(".canvas-surface")).not.toContainText("Text");

  // Focus is the sheet or one part.
  await canvas.getByRole("button", { name: "Cover", exact: true }).click();
  await expect(canvas).toContainText("Cover · 820 × 1280");
  await canvas.getByRole("button", { name: "Sheet", exact: true }).click();
  await expect(canvas).toContainText("Whole sheet");
});

test("publishes a .k7artpack from the Export menu", async ({ page }) => {
  await page.goto("/");
  await page.getByRole("button", { name: "Export", exact: true }).click();
  await page.getByRole("menuitem", { name: /Publish a pack/ }).click();

  const dialog = page.getByRole("dialog");
  await expect(dialog).toContainText("Publish a K7Art pack");
  await expect(dialog.getByRole("button", { name: "Publish and download" })).toBeEnabled();

  const downloadPromise = page.waitForEvent("download");
  await dialog.getByRole("button", { name: "Publish and download" }).click();
  const download = await downloadPromise;
  expect(download.suggestedFilename()).toBe("untitled.k7artpack");
});
