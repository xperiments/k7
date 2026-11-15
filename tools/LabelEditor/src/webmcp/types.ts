/** The subset of JSON Schema the editor's tools use. WebMCP consumes it as-is. */
export type JsonSchema = {
  type: "object";
  description?: string;
  properties: Record<string, unknown>;
  required?: string[];
};

export interface ToolResult {
  ok: boolean;
  message?: string;
  [key: string]: unknown;
}

export interface LabelTool {
  /** Kebab-case; unique, and the name an agent calls. */
  name: string;
  /** One sentence for the agent: what it does, and the units or enums it expects. */
  description: string;
  inputSchema: JsonSchema;
  execute: (args: Record<string, unknown>, options?: { signal?: AbortSignal }) => unknown;
}

/** What the browser exposes at `document.modelContext` (WebMCP draft). */
export interface ModelContextLike {
  registerTool(
    tool: LabelTool,
    options?: { signal?: AbortSignal; exposedTo?: string[] },
  ): Promise<void> | void;
  getTools?(): Promise<unknown[]>;
}
