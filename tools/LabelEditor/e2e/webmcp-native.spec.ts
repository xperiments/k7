import { expect, test } from "@playwright/test";

type ModelContext = {
  getTools(): Promise<Array<{ name: string }>>;
  executeTool(tool: unknown, args: Record<string, unknown>): Promise<unknown>;
};

/**
 * Chrome's own WebMCP, not a stub: the `webmcp-native` project launches Chrome with
 * `--enable-features=WebMCP`. On a Chrome that does not expose the API, the test skips.
 */
test("registers with Chrome's WebMCP and runs a tool", async ({ page }) => {
  await page.goto("/");

  const available = await page.evaluate(
    () =>
      typeof (document as unknown as { modelContext?: ModelContext }).modelContext
        ?.getTools === "function",
  );
  test.skip(!available, "This Chrome does not expose document.modelContext");

  await expect
    .poll(
      () =>
        page.evaluate(async () => {
          const mc = (document as unknown as { modelContext: ModelContext }).modelContext;
          const tools = await mc.getTools();
          return tools.filter((t) => t.name.startsWith("k7art-")).length;
        }),
      { timeout: 10_000 },
    )
    .toBe(16);

  // Chrome's testing API serialises a tool's return value to JSON text.
  const added = await page.evaluate(async () => {
    const mc = (document as unknown as { modelContext: ModelContext }).modelContext;
    const tools = await mc.getTools();
    const tool = tools.find((t) => t.name === "k7art-add-layer");
    const result = await mc.executeTool(tool, {
      type: "text",
      text: "Native agent",
      x: 80,
      y: 250,
      color: "#3b6e8f",
    });
    return typeof result === "string" ? JSON.parse(result) : result;
  });
  expect(added.ok).toBe(true);
  expect(added.layer.text).toBe("Native agent");

  await expect(page.locator(".canvas-surface")).toContainText("Native agent");
  const layers = await page.evaluate(
    () => (window as unknown as { __editor?: { objs: unknown[] } }).__editor!.objs.length,
  );
  expect(layers).toBe(5);
});
