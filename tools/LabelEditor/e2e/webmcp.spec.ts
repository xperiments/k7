import { expect, test } from "@playwright/test";

type AnyWindow = {
  __mcpTools?: Array<{
    name: string;
    execute: (args: Record<string, unknown>) => Promise<any>;
  }>;
  __editor?: { objs: Array<unknown> };
  __labelEditorMcp?: {
    tools(): Array<unknown>;
    call(name: string, args?: Record<string, unknown>): Promise<any>;
    status(): { available: boolean; registered: string[] };
  };
};

test("offers the editor to WebMCP, and its tools edit the design", async ({ page }) => {
  await page.addInitScript(() => {
    const w = window as unknown as AnyWindow & typeof globalThis;
    w.__mcpTools = [];
    const ctx = {
      registerTool: async (tool: NonNullable<AnyWindow["__mcpTools"]>[number]) => {
        w.__mcpTools!.push(tool);
      },
    };
    Object.defineProperty(document, "modelContext", { value: ctx, configurable: true });
  });
  await page.goto("/");

  await expect
    .poll(() => page.evaluate(() => ((window as unknown as AnyWindow).__mcpTools ?? []).length))
    .toBeGreaterThan(10);

  const count = await page.evaluate(
    () => (window as unknown as AnyWindow).__labelEditorMcp!.tools().length,
  );
  expect(
    await page.evaluate(() => (window as unknown as AnyWindow).__mcpTools!.length),
  ).toBe(count);

  const added = await page.evaluate(async () => {
    const t = (window as unknown as AnyWindow).__mcpTools!.find(
      (x) => x.name === "label-editor-add-layer",
    )!;
    return t.execute({
      type: "text",
      text: "Hello agent",
      x: 120,
      y: 200,
      role: "band.2",
      fontRole: "title",
    });
  });
  expect(added.ok).toBe(true);
  expect(added.layer.text).toBe("Hello agent");
  await expect(page.locator(".canvas-surface")).toContainText("Hello agent");
  // The row reads as the text, and the text stays on one line: the canvas measures its
  // ink without the chrome's letter-spacing.
  await expect(page.locator('aside[aria-label="Layers"]')).toContainText("Hello agent");
  const textLayer = page.locator("[data-obj-id]").filter({ hasText: "Hello agent" });
  const metrics = await textLayer.evaluate((el) => ({
    height: el.clientHeight,
    line: parseFloat(getComputedStyle(el).fontSize) * 1.2,
    spacing: getComputedStyle(el).letterSpacing,
  }));
  expect(metrics.height).toBeLessThan(metrics.line * 1.5);
  expect(["normal", "0px"]).toContain(metrics.spacing);

  const updated = await page.evaluate(async (id) => {
    const t = (window as unknown as AnyWindow).__mcpTools!.find(
      (x) => x.name === "label-editor-update-layer",
    )!;
    return t.execute({ id, text: "Changed", x: 140 });
  }, added.id);
  expect(updated.layer.x).toBe(140);

  const exported = await page.evaluate(async () => {
    const t = (window as unknown as AnyWindow).__mcpTools!.find(
      (x) => x.name === "label-editor-export-template",
    )!;
    return t.execute({});
  });
  expect(exported.ok).toBe(true);
  expect(
    exported.template.prims.some((p: { text?: string }) => p.text === "Changed"),
  ).toBe(true);

  const read = await page.evaluate(() =>
    (window as unknown as AnyWindow).__labelEditorMcp!.call("label-editor-read"),
  );
  expect(read.active.layers.length).toBe(5);

  // A refused call changes nothing.
  const before = await page.evaluate(
    () => (window as unknown as AnyWindow).__editor!.objs.length,
  );
  const error = await page.evaluate(async () => {
    const t = (window as unknown as AnyWindow).__mcpTools!.find(
      (x) => x.name === "label-editor-add-layer",
    )!;
    try {
      await t.execute({ type: "text", field: "nope" });
      return "accepted";
    } catch (e) {
      return (e as Error).message;
    }
  });
  expect(error).toContain("not one of");
  expect(
    await page.evaluate(() => (window as unknown as AnyWindow).__editor!.objs.length),
  ).toBe(before);

  // The dialog is the human's receipt of the surface.
  await page.getByRole("button", { name: "Agent tools (WebMCP)" }).click();
  const dialog = page.getByRole("dialog");
  await expect(dialog).toContainText("tools registered");
  await expect(dialog.getByText("label-editor-read", { exact: true })).toBeVisible();
});

test("keeps the bridge when the browser has no WebMCP", async ({ page }) => {
  await page.addInitScript(() => {
    Object.defineProperty(navigator, "modelContext", { value: undefined, configurable: true });
    Object.defineProperty(document, "modelContext", { value: undefined, configurable: true });
  });
  await page.goto("/");

  const status = await page.evaluate(() =>
    (window as unknown as AnyWindow).__labelEditorMcp!.status(),
  );
  expect(status.available).toBe(false);

  const read = await page.evaluate(() =>
    (window as unknown as AnyWindow).__labelEditorMcp!.call("label-editor-read"),
  );
  expect(read.ok).toBe(true);

  await page.getByRole("button", { name: "Agent tools (WebMCP)" }).click();
  await expect(page.getByRole("dialog")).toContainText("WebMCP unavailable");
});
