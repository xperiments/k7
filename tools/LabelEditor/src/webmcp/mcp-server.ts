/**
 * The K7Art editor as an MCP server.
 *
 * The editor's tools live in the page: `window.__labelEditorMcp`, the same registry WebMCP
 * hands a browser. This puts them behind the Model Context Protocol, so a desktop client
 * with no WebMCP can drive a design - it opens the editor in Chrome, reads the tools from
 * the page, and forwards every call to it.
 *
 *   npm run mcp                    stdio (Claude Desktop, Claude Code, any stdio client)
 *   npm run mcp -- --http 8787     HTTP on 127.0.0.1:8787 (ChatGPT Desktop, mcp-remote)
 *   npm run mcp -- --list          print the editor's tools and exit
 *   npm run mcp -- --call k7art-read '{"includeTemplate":true}'   one call, then exit
 *
 * The protocol layer is `rpc()`: one JSON-RPC message in, one reply out (or null for a
 * notification). It holds no browser and no transport, which is what the tests exercise.
 */
import { createServer } from "node:http";
import { stdin, stdout } from "node:process";
import { pathToFileURL } from "node:url";

import { chromium, type Browser, type Page } from "playwright";

export const SERVER_INFO = { name: "k7art-editor", version: "1.0.0" };
/** The MCP revision this server answers with when a client does not name one. */
export const PROTOCOL = "2025-06-18";
export const EDITOR_DEFAULT = "http://localhost:5178";

/** The page's bridge, as the editor installs it (`src/webmcp/register.ts`). */
type Bridge = {
  tools(): Array<{ name: string; description: string; inputSchema: unknown }>;
  call(name: string, args: Record<string, unknown>): Promise<unknown>;
};

export interface McpTool {
  name: string;
  description: string;
  inputSchema: unknown;
}

export interface McpReply {
  jsonrpc: "2.0";
  id: unknown;
  result?: unknown;
  error?: { code: number; message: string };
}

export interface ToolResult {
  content: Array<{ type: "text"; text: string }>;
  isError?: boolean;
}

const UNREACHABLE =
  "the editor is not open in the browser this server drives; open it (npm run dev, or /k7art) and call again";

// ---- the page -------------------------------------------------------------

export async function pageTools(page: Page): Promise<McpTool[]> {
  return page.evaluate(() => {
    const w = window as unknown as { __labelEditorMcp: Bridge };
    return w.__labelEditorMcp.tools().map((tool) => ({
      name: tool.name,
      description: tool.description,
      inputSchema: tool.inputSchema ?? { type: "object", properties: {} },
    }));
  });
}

/** Run one tool and shape its result as `tools/call` wants it. A refusal is `isError`. */
export async function pageCall(
  page: Page,
  name: string,
  args?: Record<string, unknown>,
): Promise<ToolResult> {
  try {
    const value = await page.evaluate(
      ([tool, input]) => {
        const w = window as unknown as { __labelEditorMcp: Bridge };
        return w.__labelEditorMcp.call(tool as string, (input ?? {}) as Record<string, unknown>);
      },
      [name, args ?? {}] as const,
    );
    return { content: [{ type: "text", text: JSON.stringify(value, null, 2) }] };
  } catch (error) {
    /* Playwright prefixes a rejected `evaluate` with its own sentence; the client wants the
     * tool's, which already names the argument and the reason. */
    const text = (error instanceof Error ? error.message : String(error))
      .replace(/^page\.evaluate: /, "")
      .replace(/^Error: /, "");
    return { content: [{ type: "text", text }], isError: true };
  }
}

/**
 * One JSON-RPC message in, one answer out. `page` may be null while the editor is not
 * reachable: `initialize` and the notifications still answer, and anything that needs the
 * page says why instead of returning an empty list.
 */
export async function rpc(
  message: { id?: unknown; method?: unknown; params?: Record<string, unknown> } | null,
  page: Page | null,
): Promise<McpReply | null> {
  const id = message?.id ?? null;
  const reply = (result: unknown): McpReply => ({ jsonrpc: "2.0", id, result });
  const fail = (code: number, text: string): McpReply => ({
    jsonrpc: "2.0",
    id,
    error: { code, message: text },
  });

  const method = message?.method;
  if (typeof method !== "string") return fail(-32600, "not a JSON-RPC request");
  const params = (message?.params ?? {}) as Record<string, unknown>;

  switch (method) {
    case "initialize":
      return reply({
        protocolVersion:
          typeof params.protocolVersion === "string" ? params.protocolVersion : PROTOCOL,
        capabilities: { tools: { listChanged: false } },
        serverInfo: SERVER_INFO,
        instructions:
          "The K7Art editor. Call k7art-read first: it returns both surfaces, the fields, " +
          "the colour and type roles and the open documents. Call k7art-guide for the " +
          "designing guide. A refused call throws, and the message names the argument.",
      });
    case "notifications/initialized":
    case "notifications/cancelled":
      return null;
    case "ping":
      return reply({});
    case "tools/list":
      if (!page) return fail(-32000, UNREACHABLE);
      return reply({ tools: await pageTools(page) });
    case "tools/call": {
      if (typeof params.name !== "string") return fail(-32602, "tools/call needs a tool name");
      if (!page) return fail(-32000, UNREACHABLE);
      return reply(
        await pageCall(page, params.name, (params.arguments ?? {}) as Record<string, unknown>),
      );
    }
    default:
      return fail(-32601, `unknown method "${method}"`);
  }
}

/**
 * The editor page: a Chrome already running with a debugging port when `attach` names one,
 * otherwise one of the server's own, shown so the person can watch the design change. The
 * wait is for the page's bridge, not for the load event, because that is when the tools exist.
 */
export async function openEditor(
  options: { url?: string; attach?: string | null; headless?: boolean } = {},
): Promise<{ browser: Browser; page: Page }> {
  const url = options.url ?? EDITOR_DEFAULT;
  const browser = options.attach
    ? await chromium.connectOverCDP(options.attach)
    : await chromium.launch({ channel: "chrome", headless: options.headless === true });
  const context = browser.contexts()[0] ?? (await browser.newContext());
  let page = context.pages().find((candidate) => candidate.url().startsWith(url));
  if (!page) {
    page = await context.newPage();
    await page.goto(url);
  }
  await page.bringToFront().catch(() => {});
  await page.waitForFunction(() => Boolean((window as unknown as { __labelEditorMcp?: unknown }).__labelEditorMcp), null, {
    timeout: 60_000,
  });
  return { browser, page };
}

// ---- the transports ---------------------------------------------------------

async function serveStdio(page: Page): Promise<void> {
  let buffer = "";
  stdin.setEncoding("utf8");
  const send = (message: McpReply | null) => {
    if (message) stdout.write(`${JSON.stringify(message)}\n`);
  };
  for await (const chunk of stdin) {
    buffer += chunk;
    let newline = buffer.indexOf("\n");
    while (newline >= 0) {
      const line = buffer.slice(0, newline).trim();
      buffer = buffer.slice(newline + 1);
      newline = buffer.indexOf("\n");
      if (!line) continue;
      let parsed: Parameters<typeof rpc>[0];
      try {
        parsed = JSON.parse(line);
      } catch {
        send({ jsonrpc: "2.0", id: null, error: { code: -32700, message: "not JSON" } });
        continue;
      }
      send(await rpc(parsed, page));
    }
  }
}

/**
 * The streamable HTTP transport, as far as this server needs it: one POST per message,
 * answered with JSON - or a one-message SSE stream when the client asked for one. No
 * sessions and no server-initiated messages: the editor is the only state.
 */
async function serveHttp(page: Page, port: number, host: string) {
  const headers = {
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Headers": "content-type, accept, mcp-session-id, mcp-protocol-version",
    "Access-Control-Allow-Methods": "GET, POST, DELETE, OPTIONS",
  };
  const server = createServer(async (req, res) => {
    const url = new URL(req.url ?? "/", `http://${host}:${port}`);
    if (req.method === "OPTIONS") {
      res.writeHead(204, headers).end();
      return;
    }
    if (url.pathname === "/health") {
      res
        .writeHead(200, { ...headers, "content-type": "application/json" })
        .end(JSON.stringify({ ok: true, editor: page.url() }));
      return;
    }
    if (url.pathname !== "/mcp") {
      res.writeHead(404, headers).end();
      return;
    }
    if (req.method === "GET") {
      /* No server-initiated stream to offer: say so rather than hold the socket. */
      res.writeHead(405, headers).end();
      return;
    }
    if (req.method === "DELETE") {
      res.writeHead(204, headers).end();
      return;
    }
    if (req.method !== "POST") {
      res.writeHead(405, headers).end();
      return;
    }

    let raw = "";
    for await (const chunk of req) raw += String(chunk);
    let parsed: Parameters<typeof rpc>[0];
    try {
      parsed = JSON.parse(raw);
    } catch {
      res
        .writeHead(400, { ...headers, "content-type": "application/json" })
        .end(
          JSON.stringify({ jsonrpc: "2.0", id: null, error: { code: -32700, message: "not JSON" } }),
        );
      return;
    }

    const answer = await rpc(parsed, page);
    if (!answer) {
      res.writeHead(202, headers).end();
      return;
    }
    if ((req.headers.accept ?? "").includes("text/event-stream")) {
      res.writeHead(200, {
        ...headers,
        "content-type": "text/event-stream",
        "cache-control": "no-cache",
      });
      res.end(`event: message\ndata: ${JSON.stringify(answer)}\n\n`);
      return;
    }
    res.writeHead(200, { ...headers, "content-type": "application/json" }).end(JSON.stringify(answer));
  });
  await new Promise<void>((resolve) => server.listen(port, host, () => resolve()));
  return server;
}

// ---- the command line -------------------------------------------------------

export interface Options {
  url: string;
  attach: string | null;
  headless: boolean;
  http: number | null;
  host: string;
  list: boolean;
  call: string | null;
  callArgs: Record<string, unknown> | null;
  help: boolean;
}

export function options(argv: string[]): Options {
  const opts: Options = {
    url: process.env.K7ART_URL ?? EDITOR_DEFAULT,
    attach: null,
    headless: false,
    http: null,
    host: "127.0.0.1",
    list: false,
    call: null,
    callArgs: null,
    help: false,
  };
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === "--url") opts.url = argv[++i] ?? opts.url;
    else if (arg === "--attach") opts.attach = argv[++i] ?? null;
    else if (arg === "--headless") opts.headless = true;
    else if (arg === "--http") opts.http = Number(argv[++i] ?? 8787);
    else if (arg === "--host") opts.host = argv[++i] ?? opts.host;
    else if (arg === "--list") opts.list = true;
    else if (arg === "--call") opts.call = argv[++i] ?? null;
    else if (arg === "--help" || arg === "-h") opts.help = true;
    else if (opts.call && opts.callArgs === null) {
      try {
        opts.callArgs = JSON.parse(arg) as Record<string, unknown>;
      } catch {
        opts.callArgs = { prompt: arg };
      }
    }
  }
  return opts;
}

export const USAGE = `The K7Art editor as an MCP server.

  npm run mcp                    stdio (Claude Desktop, Claude Code, any stdio client)
  npm run mcp -- --http 8787     HTTP on 127.0.0.1:8787 (ChatGPT Desktop, mcp-remote)
  npm run mcp -- --list          print the editor's tools and exit
  npm run mcp -- --call k7art-read '{ "includeTemplate": true }'

  --url <url>      the editor page (default K7ART_URL or ${EDITOR_DEFAULT})
  --attach <url>   drive a Chrome already on --remote-debugging-port (default: launch one)
  --headless       no Chrome window
  --host <h>       bind the HTTP server to <h> (default 127.0.0.1)
  --help           this text
`;

export async function main(argv: string[] = process.argv.slice(2)): Promise<void> {
  const opts = options(argv);
  if (opts.help) {
    stdout.write(USAGE);
    return;
  }
  const { browser, page } = await openEditor(opts);
  const shutdown = () => {
    void browser.close().catch(() => {});
    process.exit(0);
  };
  process.on("SIGINT", shutdown);
  process.on("SIGTERM", shutdown);

  try {
    if (opts.list) {
      const tools = await pageTools(page);
      stdout.write(`${tools.map((tool) => tool.name).join("\n")}\n`);
      return;
    }
    if (opts.call) {
      const result = await pageCall(page, opts.call, opts.callArgs ?? {});
      stdout.write(`${result.content.map((part) => part.text).join("\n")}\n`);
      process.exitCode = result.isError ? 1 : 0;
      return;
    }
    if (opts.http) {
      await serveHttp(page, opts.http, opts.host);
      stdout.write(`k7art mcp: ${opts.host}:${opts.http} (editor ${page.url()})\n`);
      stdout.write(`add it to a client as http://${opts.host}:${opts.http}/mcp\n`);
      await new Promise<void>(() => {});
      return;
    }
    await serveStdio(page);
    /* stdin closed: the client is gone, and with it the browser this server opened. */
    await browser.close().catch(() => {});
  } finally {
    if (opts.list || opts.call) await browser.close().catch(() => {});
  }
}

/* Only when run, not when imported by the tests. */
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  await main();
}
