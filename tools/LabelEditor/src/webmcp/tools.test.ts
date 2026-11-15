import { describe, expect, it } from "vitest";

import type { Editor } from "@/engine/editor";
import { labelEditorTools } from "./tools";

// The rules are pure metadata; execute is exercised in the browser tests.
const tools = labelEditorTools({} as Editor, { guide: "" });

describe("the WebMCP tool surface", () => {
  it("has unique, kebab-case names", () => {
    const names = tools.map((t) => t.name);
    expect(new Set(names).size).toBe(names.length);
    for (const name of names) expect(name).toMatch(/^label-editor-[a-z0-9-]+$/);
  });

  it("stays inside an agent's tool budget", () => {
    expect(tools.length).toBeGreaterThanOrEqual(10);
    expect(tools.length).toBeLessThanOrEqual(20);
  });

  it("describes every tool and every input property", () => {
    for (const tool of tools) {
      expect(tool.description.length).toBeGreaterThan(30);
      expect(tool.inputSchema.type).toBe("object");
      const walk = (properties: Record<string, unknown>, where: string) => {
        for (const [key, raw] of Object.entries(properties)) {
          const prop = raw as { description?: string; properties?: Record<string, unknown> };
          expect(prop.description, `${tool.name}.${where}${key}`).toBeTruthy();
          if (prop.properties) walk(prop.properties, `${where}${key}.`);
        }
      };
      walk(tool.inputSchema.properties, "");
    }
  });

  it("only requires properties it declares", () => {
    for (const tool of tools) {
      for (const key of tool.inputSchema.required ?? []) {
        expect(tool.inputSchema.properties, `${tool.name}.${key}`).toHaveProperty(key);
      }
    }
  });

  it("can read before it writes", () => {
    expect(tools.map((t) => t.name)).toContain("label-editor-read");
    expect(tools.map((t) => t.name)).toContain("label-editor-export-template");
  });
});
