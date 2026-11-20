import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";

import { describe, expect, it } from "vitest";

import type { Editor } from "@/engine/editor";
import { SHEET_FONTS, SLOTS } from "@/engine/vocab";
import { labelEditorTools } from "./tools";
import {
  SKILL_TOOLS_END,
  SKILL_TOOLS_START,
  readSkillSection,
  renderToolReference,
} from "./skill";

// The skill ships with the app: `src/webmcp` is two levels below `tools/LabelEditor`.
const skillPath = fileURLToPath(
  new URL("../../skills/k7-art/SKILL.md", import.meta.url),
);
const skill = readFileSync(skillPath, "utf8");
const tools = labelEditorTools({} as Editor, { guide: skill });

/** The hand-written part: everything outside the generated markers. */
const prose = (() => {
  const start = skill.indexOf(SKILL_TOOLS_START);
  const end = skill.indexOf(SKILL_TOOLS_END);
  return start < 0 || end < 0
    ? ""
    : skill.slice(0, start) + skill.slice(end + SKILL_TOOLS_END.length);
})();

describe("the K7 label skill", () => {
  it("has both generated markers", () => {
    expect(prose).not.toBe("");
  });

  it("documents the tools the page registers, exactly", () => {
    expect(readSkillSection(skill)).toBe(renderToolReference(tools));
  });

  it("names every field in its own words, not only in a schema", () => {
    for (const slot of SLOTS) expect(prose).toContain(`\`${slot.id}\``);
  });

  it("names every face and size the sheet carries", () => {
    for (const [face, px] of SHEET_FONTS) {
      expect(prose).toContain(face);
      expect(prose).toContain(`${px} px`);
    }
  });

  it("says what the print's box is, because every coordinate is in it", () => {
    expect(prose).toContain("748 × 359");
  });

  it("hands an agent the guide as a tool result, for a session that blocks routes", () => {
    const guide = tools.find((t) => t.name === "k7art-guide");
    expect(guide).toBeTruthy();
    const out = guide!.execute({}) as { ok: boolean; guide: string; url: string };
    expect(out.ok).toBe(true);
    expect(out.url).toBe("/skill.md");
    expect(out.guide.startsWith("# A K7Art for the K7")).toBe(true);
    expect(out.guide).toContain("748 × 359");
    expect(out.guide).toContain("Driving the visible controls");
    // The tool list is the caller's own; the generated block does not travel with the prose.
    expect(out.guide).not.toContain("generated: tools:start");
  });
});
