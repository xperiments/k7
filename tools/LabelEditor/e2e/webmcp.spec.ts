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
      (x) => x.name === "k7art-add-layer",
    )!;
    return t.execute({
      type: "text",
      text: "Hello agent",
      x: 120,
      y: 200,
      color: "#8a5d34",
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
      (x) => x.name === "k7art-update-layer",
    )!;
    return t.execute({ id, text: "Changed", x: 140 });
  }, added.id);
  expect(updated.layer.x).toBe(140);

  const exported = await page.evaluate(async () => {
    const t = (window as unknown as AnyWindow).__mcpTools!.find(
      (x) => x.name === "k7art-export-template",
    )!;
    return t.execute({});
  });
  expect(exported.ok).toBe(true);
  expect(
    exported.template.label.prims.some((p: { text?: string }) => p.text === "Changed"),
  ).toBe(true);

  const read = await page.evaluate(() =>
    (window as unknown as AnyWindow).__labelEditorMcp!.call("k7art-read"),
  );
  expect(read.active.layers.length).toBe(5);

  // A refused call changes nothing.
  const before = await page.evaluate(
    () => (window as unknown as AnyWindow).__editor!.objs.length,
  );
  const error = await page.evaluate(async () => {
    const t = (window as unknown as AnyWindow).__mcpTools!.find(
      (x) => x.name === "k7art-add-layer",
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
  await expect(dialog.getByText("k7art-read", { exact: true })).toBeVisible();
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
    (window as unknown as AnyWindow).__labelEditorMcp!.call("k7art-read"),
  );
  expect(read.ok).toBe(true);

  await page.getByRole("button", { name: "Agent tools (WebMCP)" }).click();
  await expect(page.getByRole("dialog")).toContainText("WebMCP unavailable");
});

test("hides a layer from the read, the canvas and the export", async ({ page }) => {
  await page.addInitScript(() => {
    Object.defineProperty(navigator, "modelContext", { value: undefined, configurable: true });
    Object.defineProperty(document, "modelContext", { value: undefined, configurable: true });
  });
  await page.goto("/");
  await page.waitForFunction(() => !!window.__labelEditorMcp);

  const out = await page.evaluate(async () => {
    const call = (name: string, args: Record<string, unknown> = {}) =>
      (window as unknown as AnyWindow).__labelEditorMcp!.call(name, args);
    const first = await call("k7art-read");
    const title = (first.jcard.layers as Array<{ id: number; field: string | null }>).find(
      (layer) => layer.field === "title",
    )!;
    const before = (await call("k7art-export-template")).template.jcard.prims.length;
    await call("k7art-update-layer", { surface: "jcard", id: title.id, hidden: true });
    const hidden = await call("k7art-read");
    const after = (await call("k7art-export-template")).template.jcard.prims.length;
    await call("k7art-update-layer", { surface: "jcard", id: title.id, hidden: false });
    const shown = (await call("k7art-export-template")).template.jcard.prims.length;
    return { before, after, shown, hidden: hidden.jcard.layers.find((l: { id: number }) => l.id === title.id) };
  });

  expect(out.before).toBe(3);
  expect(out.after).toBe(2);
  expect(out.shown).toBe(3);
  expect(out.hidden.hidden).toBe(true);
});

test("binds a J-card text to the label's fields", async ({ page }) => {
  await page.addInitScript(() => {
    Object.defineProperty(navigator, "modelContext", { value: undefined, configurable: true });
    Object.defineProperty(document, "modelContext", { value: undefined, configurable: true });
  });
  await page.goto("/");
  await page.waitForFunction(() => !!window.__labelEditorMcp);

  const out = await page.evaluate(async () => {
    const call = (name: string, args: Record<string, unknown> = {}) =>
      (window as unknown as AnyWindow).__labelEditorMcp!.call(name, args);
    const added = await call("k7art-add-layer", {
      surface: "jcard",
      type: "text",
      field: "bpm",
      x: 520,
      y: 300,
      color: "#151515",
      fontRole: "numeral",
    });
    const exported = await call("k7art-export-template");
    return {
      added,
      prims: exported.template.jcard.prims as Array<Record<string, unknown>>,
      sims: exported.template.jcard,
    };
  });

  expect(out.added.ok).toBe(true);
  // The label's own test value rides along as the file's preview.
  const bpm = out.prims.find((prim) => prim.slot === "bpm");
  expect(bpm).toMatchObject({ t: "text", slot: "bpm", preview: "124" });
  expect(out.added.layer.field).toBe("bpm");
});

test("edits the J-card surface and publishes a pack", async ({ page }) => {
  await page.addInitScript(() => {
    Object.defineProperty(navigator, "modelContext", { value: undefined, configurable: true });
    Object.defineProperty(document, "modelContext", { value: undefined, configurable: true });
  });
  await page.goto("/");
  await page.waitForFunction(() => !!window.__labelEditorMcp);

  const out = await page.evaluate(async () => {
    const call = (name: string, args: Record<string, unknown> = {}) =>
      (window as unknown as AnyWindow).__labelEditorMcp!.call(name, args);
    const first = await call("k7art-read");
    const added = await call("k7art-add-layer", {
      surface: "jcard",
      type: "text",
      text: "Case",
      x: 600,
      y: 300,
      color: "#151515",
      fontRole: "title",
    });
    /* A second J-card layer, so the two can be a group: the grouping is the same feature
       the label has, on the sheet. */
    const rect = await call("k7art-add-layer", {
      surface: "jcard",
      type: "rect",
      x: 520,
      y: 240,
      width: 140,
      height: 60,
      color: "#a24e2c",
    });
    const grouped = await call("k7art-group-layers", {
      surface: "jcard",
      action: "group",
      ids: [added.id, rect.id],
    });
    const design = await call("k7art-set-design", {
      jcardFonts: { title: { size: 60 } },
      /* The sheet's own background, which is not the label's mask: one argument each. */
      jcardBackground: "#142538",
    });
    const field = await call("k7art-set-field-value", {
      surface: "jcard",
      field: "title",
      value: "Case Study",
    });
    const read = await call("k7art-read");
    const ungrouped = await call("k7art-group-layers", {
      surface: "jcard",
      action: "ungroup",
      ids: [added.id, rect.id],
    });
    const pub = await call("k7art-publish-pack", {
      packId: "neon-2026",
      name: "Neon Skyline",
      author: "The Analog Sons",
    });
    const bad = await call("k7art-publish-pack", {
      packId: "Neon 2026",
      name: "Neon Skyline",
      author: "The Analog Sons",
    });
    return {
      labelLayers: first.active.labelLayers.length,
      jcardLayers: first.jcard.layers.length,
      added,
      rect,
      grouped,
      ungrouped,
      design,
      field,
      read,
      pub,
      bad,
    };
  });

  expect(out.added.ok).toBe(true);
  expect(out.added.surface).toBe("jcard");
  expect(out.read.surface).toBe("jcard");
  expect(out.read.active.layers.length).toBe(out.jcardLayers + 2);
  expect(out.read.active.labelLayers.length).toBe(out.labelLayers);
  expect(
    (out.design.design.jcardFonts as Array<{ role: string; size: number }>).find(
      (f) => f.role === "title",
    )!.size,
  ).toBe(60);
  expect(out.field.value).toBe("Case Study");
  /* The J-card's background is readable and did not move the label's mask. */
  expect(out.read.jcard.background).toBe("#142538");
  /* The sheet's layers group like the label's, and ungroup again. */
  expect(out.grouped.groups.length).toBe(1);
  expect((out.grouped.groups[0] as { layers: number[] }).layers.length).toBe(2);
  expect(out.read.active.groups.length).toBe(1);
  expect((out.read.active.groups[0] as { layers: number[] }).layers.length).toBe(2);
  expect(out.ungrouped.groups.length).toBe(0);

  expect(out.bad.ok).toBe(false);
  expect((out.bad.errors as string[]).join("\n")).toContain("slug");
  expect(out.pub.ok).toBe(true);
  expect(out.pub.filename).toBe("neon-2026.k7artpack");
  expect(out.pub.sheetVersion).toBe(5);
  expect(out.pub.fontCount).toBe(6);
  const bytes = Buffer.from(out.pub.base64 as string, "base64");
  expect(bytes.subarray(0, 8).toString("latin1")).toBe("K7ART\u0000\u0000\u0000");
  expect(bytes.readUInt16LE(8)).toBe(1);
  expect(bytes.readUInt16LE(10)).toBe(128);
  expect(bytes.readUInt32LE(12)).toBe(bytes.length);
  expect(bytes.readUInt32LE(16)).toBe(out.pub.crc32);
  expect(bytes.readUInt16LE(20)).toBe(out.pub.designCount);
  expect(bytes.readUInt16LE(22)).toBe(5);
  expect(bytes.readUInt16LE(24)).toBe(6);
  expect(bytes.readUInt32LE(28)).toBe(128);
  expect(bytes.readUInt32LE(32)).toBe(out.pub.header.sheetSize);
  expect(bytes.readUInt32LE(36)).toBe(128 + out.pub.header.sheetSize);
  expect(bytes.readUInt32LE(40)).toBe(out.pub.header.jcardsSize);
  expect(bytes.subarray(128, 136).toString("latin1")).toBe("K7LBL\u0000\u0000\u0000");
  const jcards = JSON.parse(
    bytes.subarray(128 + out.pub.header.sheetSize).toString("utf8"),
  ) as { v: number; designs: Array<{ id: string; inks?: unknown; jcard: unknown }> };
  expect(jcards.v).toBe(2);
  expect(jcards.designs.map((d) => d.id)).toEqual(out.pub.designIds);
  expect(jcards.designs[0].inks).toBeUndefined();
  expect(jcards.designs[0].jcard).toBeTruthy();
});
