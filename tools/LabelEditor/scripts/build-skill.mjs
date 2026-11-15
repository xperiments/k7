#!/usr/bin/env node
/**
 * Write the agent skill's tool reference from the page's own tool registry.
 *
 * The skill (`skills/k7-labels/SKILL.md`) is hand-written prose with one generated
 * section between its markers, so a tool or an argument cannot leave the guide stale.
 *
 *   node scripts/build-skill.mjs           # rewrite the section
 *   node scripts/build-skill.mjs --check   # refuse a stale one (exit 1)
 *
 * The registry is TypeScript (`src/webmcp/tools.ts`) with the app's `@/` alias, so it is
 * loaded through jiti rather than duplicated here.
 */
import { createJiti } from "jiti";
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, "..");
const skillPath = resolve(root, "skills/k7-labels/SKILL.md");

const jiti = createJiti(resolve(here, "build-skill.ts"), {
  alias: { "@": resolve(root, "src") },
});
const { renderSkillSection, readSkillSection } = await jiti.import(
  resolve(root, "src/webmcp/skill.ts"),
);
const { labelEditorTools } = await jiti.import(resolve(root, "src/webmcp/tools.ts"));

const tools = labelEditorTools({}, { guide: "" });
const skill = readFileSync(skillPath, "utf8");
const expected = readSkillSection(renderSkillSection(tools));

if (process.argv.includes("--check")) {
  const actual = readSkillSection(skill);
  if (actual === null) {
    console.error(`build-skill: ${skillPath} has no generated markers`);
    process.exit(1);
  }
  if (actual !== expected) {
    console.error(
      `build-skill: the tool reference in ${skillPath} is stale; run "npm run skill".`,
    );
    process.exit(1);
  }
  console.log("build-skill: the skill's tool reference matches the registry.");
  process.exit(0);
}

const start = skill.indexOf("<!-- generated: tools:start -->");
const end = skill.indexOf("<!-- generated: tools:end -->");
if (start < 0 || end < 0 || end < start) {
  console.error(`build-skill: ${skillPath} has no generated markers`);
  process.exit(1);
}
const next =
  skill.slice(0, start) +
  renderSkillSection(tools) +
  skill.slice(end + "<!-- generated: tools:end -->".length);
writeFileSync(skillPath, next);
console.log(`build-skill: wrote ${tools.length} tools into ${skillPath}`);
