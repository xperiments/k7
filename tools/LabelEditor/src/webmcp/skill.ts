import type { LabelTool } from "./types";

/**
 * The agent skill that ships with the editor (`.agents/skills/k7-labels/SKILL.md`).
 *
 * The prose is written by hand; the tool reference between these markers is rendered from
 * the same registry the page registers, so a new tool or argument cannot leave the skill
 * stale. `npm run skill` writes it, `npm run skill:check` (and a unit test) refuses a stale
 * one, and the dev server serves the whole file at `/skill.md`.
 */
export const SKILL_TOOLS_START = "<!-- generated: tools:start -->";
export const SKILL_TOOLS_END = "<!-- generated: tools:end -->";

type Schema = Record<string, unknown> & {
  type?: string;
  description?: string;
  enum?: unknown[];
  default?: unknown;
  minimum?: number;
  maximum?: number;
  items?: Record<string, unknown>;
  properties?: Record<string, Record<string, unknown>>;
};

/** A property's type, as an agent reads it: an enum is the list it may pass. */
function typeOf(p: Schema): string {
  if (Array.isArray(p.enum))
    return p.enum.map((v) => `\`${String(v)}\``).join(", ");
  if (p.type === "array") {
    const item = p.items?.type;
    return item ? `array of \`${String(item)}\`` : "array";
  }
  if (p.properties) return "object";
  return p.type ? `\`${p.type}\`` : "any";
}

/** The notes column: what it does, then the bounds a validator would enforce. */
function notesOf(p: Schema): string {
  const bits: string[] = [];
  const description = (p.description ?? "").replace(/\s+/g, " ").trim();
  // An enum is in the Type column; repeating its members in prose only makes the row longer.
  const prose = Array.isArray(p.enum)
    ? description.replace(/\s*One of:.*$/i, "")
    : description;
  if (prose) bits.push(prose);
  if (p.default !== undefined) {
    const value =
      typeof p.default === "string" ? p.default : JSON.stringify(p.default);
    bits.push(`Default \`${value}\`.`);
  }
  if (p.minimum !== undefined) bits.push(`Minimum ${p.minimum}.`);
  if (p.maximum !== undefined) bits.push(`Maximum ${p.maximum}.`);
  return bits.join(" ") || "—";
}

/** One row per property, with a nested object's own properties flattened as dotted paths. */
function rows(
  properties: Record<string, Record<string, unknown>>,
  required: string[],
  prefix = "",
  parentRequired = true,
): string[] {
  const out: string[] = [];
  for (const [key, raw] of Object.entries(properties)) {
    const p = raw as Schema;
    const name = `${prefix}${key}`;
    const optional = parentRequired ? (required.includes(key) ? "yes" : "") : "";
    out.push(`| \`${name}\` | ${typeOf(p)} | ${optional} | ${notesOf(p)} |`);
    if (p.properties)
      out.push(...rows(p.properties, [], `${name}.`, false));
  }
  return out;
}

/** The generated block's body: every tool, its sentence, and its arguments. */
export function renderToolReference(tools: LabelTool[]): string {
  const sections = tools.map((tool) => {
    const schema = tool.inputSchema as unknown as Schema;
    const properties = (schema.properties ?? {}) as Record<string, Record<string, unknown>>;
    const required = (tool.inputSchema.required ?? []) as string[];
    const table = Object.keys(properties).length
      ? [
          "| Argument | Type | Required | Notes |",
          "|---|---|---|---|",
          ...rows(properties, required),
        ].join("\n")
      : "Takes no arguments.";
    return [
      `### \`${tool.name}\``,
      "",
      tool.description,
      "",
      table,
    ].join("\n");
  });
  return sections.join("\n\n");
}

/** The whole generated section, markers included. */
export function renderSkillSection(tools: LabelTool[]): string {
  return [
    SKILL_TOOLS_START,
    renderToolReference(tools),
    SKILL_TOOLS_END,
  ].join("\n");
}

/**
 * The hand-written guide, with the generated tool list taken out. This is what the page hands
 * an agent through `label-editor-guide`, whose own tool list already carries the schemas.
 */
export function skillProse(skill: string): string {
  const start = skill.indexOf(SKILL_TOOLS_START);
  const end = skill.indexOf(SKILL_TOOLS_END);
  const withoutTools =
    start < 0 || end < 0 || end < start
      ? skill
      : skill.slice(0, start) + skill.slice(end + SKILL_TOOLS_END.length);
  // The YAML frontmatter is the skill loader's; a tool result needs the guide itself.
  const withoutFrontmatter = withoutTools.replace(/^---\n[\s\S]*?\n---\n/, "");
  return withoutFrontmatter.replace(/\n{3,}/g, "\n\n").trim();
}

/** Read a skill file and return the generated body, or null when the markers are missing. */
export function readSkillSection(skill: string): string | null {
  const start = skill.indexOf(SKILL_TOOLS_START);
  const end = skill.indexOf(SKILL_TOOLS_END);
  if (start < 0 || end < 0 || end < start) return null;
  return skill.slice(start + SKILL_TOOLS_START.length, end).trim();
}

/** Put a rendered section into a skill file, between its markers; returns the new text. */
export function withSkillSection(skill: string, tools: LabelTool[]): string {
  const start = skill.indexOf(SKILL_TOOLS_START);
  const end = skill.indexOf(SKILL_TOOLS_END);
  if (start < 0 || end < 0 || end < start)
    throw new Error(
      `the skill is missing its ${SKILL_TOOLS_START} / ${SKILL_TOOLS_END} markers`,
    );
  return (
    skill.slice(0, start) +
    SKILL_TOOLS_START +
    "\n" +
    renderToolReference(tools) +
    "\n" +
    skill.slice(end)
  );
}
