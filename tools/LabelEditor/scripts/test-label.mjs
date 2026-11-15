#!/usr/bin/env node
/**
 * Create a test label through the editor's own tool surface and save it as
 * `label-template v1`. It drives `window.__labelEditorMcp` — the same tools WebMCP
 * registers — so it exercises exactly what an agent would.
 *
 *   1. the editor is open (npm run dev), and
 *   2. node scripts/test-label.mjs
 *
 * Options:
 *   --url <url>    the editor (default http://localhost:5178)
 *   --text <s>     the printed string (default HOLA)
 *   --id <s>       the template id (default label.test-hola)
 *   --name <s>     the document name (default HOLA Test)
 *   --out <path>   where the JSON goes (default .scratch/<id>.json; "-" for stdout)
 *   --head         show the browser
 *   --no-shot      skip the preview PNG
 */
import { chromium } from "@playwright/test";
import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, "..");

function parse(argv) {
  const opts = {
    url: "http://localhost:5178",
    text: "HOLA",
    id: "label.test-hola",
    name: "HOLA Test",
    out: join(root, ".scratch", "label.test-hola.json"),
    head: false,
    shot: true,
  };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    const next = () => {
      const v = argv[++i];
      if (v === undefined) throw new Error(`${a} needs a value`);
      return v;
    };
    if (a === "--url") opts.url = next();
    else if (a === "--text") opts.text = next();
    else if (a === "--id") opts.id = next();
    else if (a === "--name") opts.name = next();
    else if (a === "--out") opts.out = next();
    else if (a === "--head") opts.head = true;
    else if (a === "--no-shot") opts.shot = false;
    else if (a === "--help" || a === "-h") {
      console.log(
        "node scripts/test-label.mjs [--url URL] [--text S] [--id ID] [--name S] [--out PATH] [--head] [--no-shot]",
      );
      process.exit(0);
    } else throw new Error(`unknown option ${a}`);
  }
  if (opts.out !== "-") opts.out = resolve(opts.out);
  return opts;
}

const opts = parse(process.argv.slice(2));
const browser = await chromium.launch({
  channel: "chrome",
  headless: !opts.head,
});
const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
const errors = [];
page.on("pageerror", (e) => errors.push(e.message));

try {
  try {
    await page.goto(opts.url, { waitUntil: "networkidle" });
  } catch {
    throw new Error(
      `could not open ${opts.url}. Start the editor with "npm run dev", or pass --url.`,
    );
  }
  await page.waitForFunction(() => !!window.__labelEditorMcp, null, {
    timeout: 10_000,
  });

  const call = (name, args = {}) =>
    page.evaluate(
      ([tool, input]) => window.__labelEditorMcp.call(tool, input),
      [name, args],
    );

  // A fresh document, then the design's identity and paper.
  await call("label-editor-document", { action: "new", name: opts.name });
  await call("label-editor-set-design", {
    templateId: opts.id,
    name: opts.name,
    background: "#ffffff",
    paper: "#c7a867",
    paperRadius: 12,
  });

  // A dark print with the word in the sheet's display face, centred.
  await call("label-editor-add-layer", {
    type: "rect",
    x: 0,
    y: 0,
    width: 748,
    height: 359,
    role: "band.7",
  });
  await call("label-editor-add-layer", {
    type: "text",
    text: opts.text,
    x: 274,
    y: 140,
    width: 200,
    role: "band.6",
    fontRole: "numeral",
    align: "center",
    layerName: opts.text,
  });

  const validation = await call("label-editor-validate");
  const exported = await call("label-editor-export-template");
  const template = exported.template;
  const problems = [...new Set([...(validation.documents ?? []).flatMap((d) => d.errs), ...exported.errors])];

  if (opts.out === "-") {
    process.stdout.write(JSON.stringify(template, null, 2) + "\n");
  } else {
    mkdirSync(dirname(opts.out), { recursive: true });
    writeFileSync(opts.out, JSON.stringify(template, null, 2) + "\n");
  }

  if (opts.shot && opts.out !== "-") {
    // A label-only image: deselect so no handles print, then capture the print itself.
    await page.keyboard.press("Escape");
    const shot = join(dirname(opts.out), `${opts.id.replace(/^label\./, "")}.png`);
    await page.locator(".canvas-surface").screenshot({ path: shot });
    opts.shotPath = shot;
  }

  console.log(`Created ${template.id} — "${opts.name}"`);
  console.log(`  ${template.prims.length} prims, ${Object.keys(template.slots).length} fields`);
  console.log(`  validation: ${problems.length ? `${problems.length} issue(s)` : "ok"}`);
  for (const p of problems) console.log(`    - ${p}`);
  if (opts.out !== "-") console.log(`  template: ${opts.out}`);
  if (opts.shotPath) console.log(`  label:    ${opts.shotPath}`);

  if (problems.length) process.exitCode = 1;
} catch (err) {
  console.error(`test-label: ${err.message}`);
  process.exitCode = 1;
} finally {
  if (errors.length) console.error("page errors:", errors.join("\n"));
  await browser.close();
}
