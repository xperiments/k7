import type { Editor } from "@/engine/editor";
import { labelEditorTools } from "./tools";
import type { LabelTool, ModelContextLike } from "./types";

export interface WebMcpStatus {
  available: boolean;
  via: "document" | "navigator" | null;
  registered: string[];
  error: string | null;
  tools: number;
}

/** The page's registration state, read by the Agent tools dialog. */
export const webmcpStatus: WebMcpStatus = {
  available: false,
  via: null,
  registered: [],
  error: null,
  tools: 0,
};

/** The page-level bridge: the same tools, callable without native WebMCP. */
export interface LabelEditorBridge {
  tools(): Array<Pick<LabelTool, "name" | "description" | "inputSchema">>;
  call(name: string, args?: Record<string, unknown>): Promise<unknown>;
  status(): WebMcpStatus;
}

declare global {
  interface Window {
    __labelEditorMcp?: LabelEditorBridge;
  }
}

let controller: AbortController | null = null;
let registering = false;

/** `document.modelContext` is the draft API; older polyfills put it on navigator. */
export function modelContext(): ModelContextLike | null {
  const doc = document as unknown as { modelContext?: ModelContextLike };
  const nav = navigator as unknown as { modelContext?: ModelContextLike };
  const ctx = doc.modelContext ?? nav.modelContext;
  return ctx && typeof ctx.registerTool === "function" ? ctx : null;
}

/** Install `window.__labelEditorMcp` so a page script or extension can call the tools. */
export function installBridge(ed: Editor): LabelEditorBridge {
  const tools = labelEditorTools(ed);
  const byName = new Map(tools.map((t) => [t.name, t]));
  const bridge: LabelEditorBridge = {
    tools: () =>
      tools.map(({ name, description, inputSchema }) => ({
        name,
        description,
        inputSchema,
      })),
    call: async (name, args = {}) => {
      const tool = byName.get(name);
      if (!tool)
        throw new Error(
          `no tool "${name}"; the page exposes ${[...byName.keys()].join(", ")}`,
        );
      return tool.execute(args);
    },
    status: () => webmcpStatus,
  };
  window.__labelEditorMcp = bridge;
  return bridge;
}

/**
 * Register the editor's actions as WebMCP tools, and always leave the bridge installed.
 * Registration is all-or-nothing: if the browser refuses one tool, the rest are aborted
 * rather than leaving an agent with half a surface.
 */
export async function registerLabelEditorTools(ed: Editor): Promise<WebMcpStatus> {
  const tools = labelEditorTools(ed);
  installBridge(ed);
  webmcpStatus.tools = tools.length;
  if (registering) return webmcpStatus;
  registering = true;

  const ctx = modelContext();
  if (!ctx) {
    webmcpStatus.error = "This browser does not expose document.modelContext.";
    registering = false;
    return webmcpStatus;
  }

  controller = new AbortController();
  try {
    for (const tool of tools) {
      await ctx.registerTool(tool, { signal: controller.signal });
      webmcpStatus.registered.push(tool.name);
    }
    webmcpStatus.available = true;
    webmcpStatus.via =
      (document as unknown as { modelContext?: ModelContextLike }).modelContext !==
      undefined
        ? "document"
        : "navigator";
  } catch (err) {
    controller.abort();
    controller = null;
    webmcpStatus.available = false;
    webmcpStatus.registered = [];
    webmcpStatus.error = `Registration was refused: ${(err as Error)?.message || err}`;
  }
  registering = false;
  return webmcpStatus;
}

/** Take the tools back (HMR, or an app that unmounts the editor). */
export function unregisterLabelEditorTools(): void {
  controller?.abort();
  controller = null;
  webmcpStatus.available = false;
  webmcpStatus.registered = [];
}
