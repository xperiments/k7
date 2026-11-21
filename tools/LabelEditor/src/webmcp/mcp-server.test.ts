import { describe, expect, it } from "vitest";
import type { Page } from "playwright";

import { PROTOCOL, SERVER_INFO, options, rpc, USAGE } from "./mcp-server";

/** A page that answers `evaluate` with a canned value, for the protocol tests. */
function stubPage(evaluate: () => unknown): Page {
  return { evaluate: async () => evaluate() } as unknown as Page;
}

const tools = [
  {
    name: "k7art-read",
    description: "Read the K7Art editor.",
    inputSchema: { type: "object", properties: {} },
  },
];

describe("the MCP server's protocol", () => {
  it("answers initialize with the tools capability and the editor's instructions", async () => {
    const reply = await rpc({ id: 1, method: "initialize", params: {} }, null);
    expect(reply?.jsonrpc).toBe("2.0");
    expect(reply?.id).toBe(1);
    const result = reply?.result as {
      protocolVersion: string;
      capabilities: { tools: unknown };
      serverInfo: unknown;
      instructions: string;
    };
    expect(result.protocolVersion).toBe(PROTOCOL);
    expect(result.capabilities.tools).toBeDefined();
    expect(result.serverInfo).toEqual(SERVER_INFO);
    expect(result.instructions).toContain("k7art-read");
  });

  it("keeps the client's protocol version when it names one", async () => {
    const reply = await rpc(
      { id: 2, method: "initialize", params: { protocolVersion: "2024-11-05" } },
      null,
    );
    expect((reply?.result as { protocolVersion: string }).protocolVersion).toBe("2024-11-05");
  });

  it("has no reply for a notification, and no error either", async () => {
    expect(await rpc({ method: "notifications/initialized" }, null)).toBeNull();
    expect(await rpc({ method: "notifications/cancelled" }, null)).toBeNull();
  });

  it("refuses a method it does not serve, and a message that is not a request", async () => {
    const unknown = await rpc({ id: 3, method: "resources/list" }, null);
    expect(unknown?.error?.code).toBe(-32601);
    const broken = await rpc({ id: 4 } as never, null);
    expect(broken?.error?.code).toBe(-32600);
  });

  it("lists the page's tools as they are", async () => {
    const reply = await rpc({ id: 5, method: "tools/list" }, stubPage(() => tools));
    expect((reply?.result as { tools: unknown[] }).tools).toEqual(tools);
  });

  it("says why when the editor is not reached, instead of an empty list", async () => {
    const list = await rpc({ id: 6, method: "tools/list" }, null);
    expect(list?.error?.code).toBe(-32000);
    expect(list?.error?.message).toContain("not open");
    const call = await rpc({ id: 7, method: "tools/call", params: { name: "k7art-read" } }, null);
    expect(call?.error?.code).toBe(-32000);
  });

  it("needs a tool name to call one", async () => {
    const reply = await rpc({ id: 8, method: "tools/call", params: {} }, stubPage(() => ({})));
    expect(reply?.error?.code).toBe(-32602);
  });

  it("returns a call's value as text, and a refusal as isError", async () => {
    const ok = await rpc(
      { id: 9, method: "tools/call", params: { name: "k7art-read", arguments: {} } },
      stubPage(() => ({ ok: true, surface: "label" })),
    );
    const content = (ok?.result as { content: Array<{ type: string; text: string }> }).content;
    expect(content[0]?.type).toBe("text");
    expect(JSON.parse(content[0]!.text)).toEqual({ ok: true, surface: "label" });

    const refused = await rpc(
      { id: 10, method: "tools/call", params: { name: "k7art-add-layer", arguments: {} } },
      stubPage(() => {
        throw new Error("surface must be label or jcard");
      }),
    );
    const bad = refused?.result as { content: Array<{ text: string }>; isError: boolean };
    expect(bad.isError).toBe(true);
    expect(bad.content[0]?.text).toContain("surface");
  });
});

describe("the MCP server's command line", () => {
  it("defaults to the editor and stdio", () => {
    const opts = options([]);
    expect(opts.url).toMatch(/^https?:\/\//);
    expect(opts.http).toBeNull();
    expect(opts.list).toBe(false);
  });

  it("reads the HTTP port, the editor url and an attached browser", () => {
    const opts = options([
      "--http",
      "9100",
      "--host",
      "0.0.0.0",
      "--url",
      "https://k7.xperiments.dev/k7art",
      "--attach",
      "http://127.0.0.1:9222",
    ]);
    expect(opts.http).toBe(9100);
    expect(opts.host).toBe("0.0.0.0");
    expect(opts.url).toBe("https://k7.xperiments.dev/k7art");
    expect(opts.attach).toBe("http://127.0.0.1:9222");
  });

  it("takes a call and its JSON arguments", () => {
    const opts = options(["--call", "k7art-read", '{"includeTemplate":true}']);
    expect(opts.call).toBe("k7art-read");
    expect(opts.callArgs).toEqual({ includeTemplate: true });
  });

  it("has a usage text for every mode", () => {
    for (const flag of ["--http", "--list", "--call", "--attach", "--url"]) {
      expect(USAGE).toContain(flag);
    }
  });
});
