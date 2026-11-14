import { expect, test } from "@playwright/test";
import { fileURLToPath } from "node:url";

const sectionButton = (page: import("@playwright/test").Page, title: string) =>
  page.locator('aside[aria-label="Inspector"]').getByRole("button", { name: title, exact: true });

test("boots with the Design panel and one section open", async ({ page }) => {
  await page.goto("/");
  await expect(page.getByText("Label Editor")).toBeVisible();
  await expect(sectionButton(page, "Template")).toHaveAttribute("aria-expanded", "true");
  await expect(sectionButton(page, "Colourway")).toHaveAttribute("aria-expanded", "false");
});

test("opens one section at a time, and the open one cannot be closed", async ({ page }) => {
  await page.goto("/");
  // Clicking the open section keeps it open: the panel never becomes an empty list of headers.
  await sectionButton(page, "Template").click();
  await expect(sectionButton(page, "Template")).toHaveAttribute("aria-expanded", "true");

  await sectionButton(page, "Colourway").click();
  await expect(sectionButton(page, "Colourway")).toHaveAttribute("aria-expanded", "true");
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

test("imports a preexisting label template", async ({ page }) => {
  await page.goto("/");
  const template = fileURLToPath(
    new URL("../../../assets/labels/templates/label.black-stripes.json", import.meta.url),
  );
  await page.locator('input[type="file"][accept*=".json"]').setInputFiles(template);
  await expect(page.getByRole("tab", { name: /Black Stripes/ })).toBeVisible();
  await expect(page.getByRole("banner").getByText("15 layers")).toBeVisible();
});

test("groups two selected layers from the panel header", async ({ page }) => {
  await page.goto("/");
  await page.getByText("circle 2").click();
  await page.getByText("rect 1").click({ modifiers: ["Meta"] });
  await page.getByRole("button", { name: "Group selected (⌘G)" }).click();
  await expect(page.getByText("Group 1")).toBeVisible();
});
