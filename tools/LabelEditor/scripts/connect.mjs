/**
 * Connect to a Chrome that is already running the editor, and report what it shows.
 *
 *   1. Quit nothing; start a Chrome of its own with a debugging port:
 *
 *      "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
 *        --remote-debugging-port=9222 \
 *        --user-data-dir=/tmp/k7-debug \
 *        http://localhost:5178
 *
 *   2. In another terminal:
 *
 *      npm run connect
 *
 *   It prints the inspector's section state, the document list and the viewport, and
 *   saves a screenshot to docs/connected.png. Set CDP_URL to use another port.
 */
import { chromium } from "@playwright/test";
import { mkdirSync } from "node:fs";

const endpoint = process.env.CDP_URL ?? "http://127.0.0.1:9222";
const browser = await chromium.connectOverCDP(endpoint);
const pages = browser.contexts().flatMap((c) => c.pages());
const page = pages.find((p) => /localhost:(5178|5179)/.test(p.url())) ?? pages[0];
if (!page) {
  console.error(`No page found at ${endpoint}. Is Chrome running with --remote-debugging-port?`);
  await browser.close();
  process.exit(1);
}

console.log(`Connected to ${page.url()}`);
const info = await page.evaluate(() => {
  const ed = window.__editor;
  const sections = [...document.querySelectorAll('aside[aria-label="Inspector"] section')].map(
    (s) => ({
      section: s.querySelector("button span")?.textContent?.trim(),
      open: s.querySelector("button")?.getAttribute("aria-expanded") === "true",
    }),
  );
  return {
    title: document.title,
    viewport: `${window.innerWidth} x ${window.innerHeight}`,
    rootFont: getComputedStyle(document.documentElement).fontSize,
    inspectorCollapsed: !!document.querySelector('aside[aria-label="Inspector, collapsed"]'),
    inspectorStored: localStorage.getItem("label-editor.inspector"),
    documents: ed?.docs.map((d) => d.name),
    active: ed?.active,
    zoom: ed?.zoom,
    sections,
  };
});
console.log(JSON.stringify(info, null, 2));

mkdirSync("docs", { recursive: true });
await page.screenshot({ path: "docs/connected.png" });
console.log("Screenshot: tools/LabelEditor/docs/connected.png");
await browser.close();
