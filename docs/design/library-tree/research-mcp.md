# MCP for the ESP32-S3 stream server — "mini MCP" research

Researched 2026-10-19 against primary sources only: the spec repo at release tags, and official SDK
source. Quotes are from the tagged 2026-07-28 tree ([commit 5f5440b](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/5f5440bb26a62e2cf3440b92da5a667efa03b267/docs/specification/2026-07-28)).

**Headline:** the latest revision is **2026-07-28**; it deleted the `initialize` handshake, sessions and the
GET stream. This note therefore covers *two* eras: **modern** (2026-07-28) and **legacy** (≤ 2025-11-25,
still what many clients default to).

## 1. Revisions and transport changes

| Revision | Transport-layer change | Source |
|---|---|---|
| 2024-11-05 | First revision. HTTP+SSE: a GET SSE endpoint plus a POST endpoint. | [2026-07-28 transports, "HTTP+SSE"](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/5f5440bb26a62e2cf3440b92da5a667efa03b267/docs/specification/2026-07-28/basic/transports/streamable-http.mdx) says Streamable HTTP was "introduced in protocol version 2025-03-26 as a replacement for the HTTP+SSE transport from protocol version 2024-11-05" |
| 2025-03-26 | "Replaced the previous HTTP+SSE transport with a more flexible **Streamable HTTP transport**"; added OAuth 2.1 authorization framework; JSON-RPC batching | [2025-03-26 changelog](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/9236eb1cbfa0/docs/specification/2025-03-26/changelog.md) |
| 2025-06-18 | `MCP-Protocol-Version` HTTP header introduced (versions before this "did not define the `MCP-Protocol-Version` header"); sessions (`Mcp-Session-Id`), GET stream, DELETE, `Last-Event-ID` resumability all present | [2025-06-18 transports](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/f5ccad944fdf/docs/specification/2025-06-18/basic/transports.mdx); [2026-07-28 transports §Protocol Version Header](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/5f5440bb26a62e2cf3440b92da5a667efa03b267/docs/specification/2026-07-28/basic/transports/streamable-http.mdx) |
| 2025-11-25 | No breaking transport change. Clarified "servers must respond with HTTP 403 Forbidden for invalid Origin headers"; SSE polling (server may disconnect at will) | [2025-11-25 changelog](https://modelcontextprotocol.io/specification/2025-11-25/changelog) |
| **2026-07-28** (latest) | Sessions and `Mcp-Session-Id` removed; `initialize`/`notifications/initialized` removed; `ping` removed; GET endpoint removed; `Last-Event-ID` resumability removed; `server/discover` added; `Mcp-Method`/`Mcp-Name` required | [2026-07-28 changelog](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/5f5440bb26a62e2cf3440b92da5a667efa03b267/docs/specification/2026-07-28/changelog.mdx) |

The repo's release tags are `2024-10-07`, `2024-11-05`, `2025-03-26`, `2025-06-18`, `2025-11-25`, `2026-07-28`
([tags](https://api.github.com/repos/modelcontextprotocol/modelcontextprotocol/tags)) — so the last four in
~18 months are 2025-03-26 → 2026-07-28. Latest release: `2026-07-28`; the live draft is separate.

## 2. Minimal JSON-RPC message set

### 2a. Modern (2026-07-28) — 3 methods, no handshake

Servers **MUST** implement `server/discover` ([versioning](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/5f5440bb26a62e2cf3440b92da5a667efa03b267/docs/specification/2026-07-28/basic/versioning.mdx)); add `tools/list` and
`tools/call`. There is **no** `initialize`, `notifications/initialized` or `ping` (removed — [changelog](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/5f5440bb26a62e2cf3440b92da5a667efa03b267/docs/specification/2026-07-28/changelog.mdx) items 2, 5).

Every request **MUST** carry `_meta` ([basic §_meta](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/5f5440bb26a62e2cf3440b92da5a667efa03b267/docs/specification/2026-07-28/basic/index.mdx)): `…/protocolVersion` (**required**),
`…/clientCapabilities` (**required**), `…/clientInfo` (optional, SHOULD).

Missing required fields ⇒ JSON-RPC `-32602` **and HTTP 400**. Servers **SHOULD** put
`io.modelcontextprotocol/serverInfo` in every result's `_meta` ([basic §_meta](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/5f5440bb26a62e2cf3440b92da5a667efa03b267/docs/specification/2026-07-28/basic/index.mdx)).

**Every result MUST have `resultType`** — `"complete"` or `"input_required"`; clients **MUST** treat an
absent `resultType` as `"complete"` for old servers ([basic §ResultType](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/5f5440bb26a62e2cf3440b92da5a667efa03b267/docs/specification/2026-07-28/basic/index.mdx)).

`server/discover` result (shape from [discover](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/5f5440bb26a62e2cf3440b92da5a667efa03b267/docs/specification/2026-07-28/server/discover.mdx)):

```json
{"jsonrpc":"2.0","id":"d1","result":{
  "resultType":"complete",
  "supportedVersions":["2026-07-28"],
  "capabilities":{"tools":{}},
  "_meta":{"io.modelcontextprotocol/serverInfo":{"name":"xtape","version":"0.1.0"}},
  "instructions":"Read-only access to the music library on the SD card.",
  "ttlMs":3600000,"cacheScope":"public"}}
```

`ttlMs` and `cacheScope` are **MUSTs** on `server/discover` and `tools/list` results
([caching](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/5f5440bb26a62e2cf3440b92da5a667efa03b267/docs/specification/2026-07-28/server/utilities/caching.mdx): "Servers MUST include caching hints on results with
`resultType: "complete"` returned by the following operations" — the list is discover, tools/list,
prompts/list, resources/list, resources/templates/list, resources/read; **`tools/call` is not in it**).

`tools/list` / `tools/call` are familiar ([tools](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/5f5440bb26a62e2cf3440b92da5a667efa03b267/docs/specification/2026-07-28/server/tools.mdx)): tool defs need `name`, `description`,
`inputSchema` (no-arg tool: `{"type":"object","additionalProperties":false}`); results carry
`content:[{type:"text",text:"…"}]`, `isError:true` for execution errors, `-32602` for an unknown tool.

**Notifications.** The only core client→server notification is `notifications/cancelled`, and on
Streamable HTTP it is **not used** — closing the response stream *is* the cancellation
([transports §Sending Messages note](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/5f5440bb26a62e2cf3440b92da5a667efa03b267/docs/specification/2026-07-28/basic/transports/streamable-http.mdx)). `notifications/progress` is optional,
request-scoped, and requires a `progressToken` in `_meta` ([progress](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/5f5440bb26a62e2cf3440b92da5a667efa03b267/docs/specification/2026-07-28/basic/patterns/progress.mdx)).
A notification POST is answered `202 Accepted` with no body.

### 2b. Legacy (2025-06-18 / 2025-11-25) — 4 methods + 1 notification

Still what a default-configured MCP Inspector speaks (`protocolEra` defaults to `legacy`, see §6).
`initialize` request/response ([2025-06-18 lifecycle](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/f5ccad944fdf/docs/specification/2025-06-18/basic/lifecycle.mdx)):

```json
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18",
  "capabilities":{"roots":{},"sampling":{}},"clientInfo":{"name":"c","version":"1.0.0"}}}
{"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2025-06-18",
  "capabilities":{"tools":{"listChanged":false}},"serverInfo":{"name":"xtape","version":"0.1.0"},
  "instructions":"Optional instructions for the client"}}
```
- `protocolVersion`, `capabilities`, `serverInfo` are required result fields; `instructions` is optional.
  `capabilities.tools.listChanged` is optional — omit or `false` if you never send list-changed.
- Client then sends `{"jsonrpc":"2.0","method":"notifications/initialized"}` ⇒ `202`.
- `ping` is optional to *initiate* but "The receiver **MUST** respond promptly with an empty response"
  ([ping](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/f5ccad944fdf/docs/specification/2025-06-18/basic/utilities/ping.mdx)). Return `{"jsonrpc":"2.0","id":N,"result":{}}`.
- A session id is optional for the server ("A server … **MAY** assign a session ID"), so a stateless
  device can simply never emit `Mcp-Session-Id` ([2025-06-18 §Session Management](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/f5ccad944fdf/docs/specification/2025-06-18/basic/transports.mdx)).
- Legacy `tools/list` results have **no** `resultType` and need **no** `ttlMs`/`cacheScope`.

## 3. Streamable HTTP, 2026-07-28 — exact requirements

All quotes from [streamable-http.mdx](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/5f5440bb26a62e2cf3440b92da5a667efa03b267/docs/specification/2026-07-28/basic/transports/streamable-http.mdx) unless noted.
- **Endpoint + method.** "The server **MUST** provide a single HTTP endpoint path … that supports POST";
  "Every JSON-RPC message sent from the client **MUST** be a new HTTP POST request." GET and DELETE are
  gone: an old client's GET/DELETE "**SHOULD** respond with `405 Method Not Allowed`".
- **Accept (client).** "The client **MUST** include an `Accept` header listing both `application/json`
  and `text/event-stream` as supported content types."
- **Body / response.** "The body of the HTTP POST **MUST** be a single JSON-RPC *request* or
  *notification*. The client **MUST NOT** send JSON-RPC *responses*." For a request, "the server **MUST**
  return either `Content-Type: application/json` … or `Content-Type: text/event-stream` … The client
  **MUST** support both."
- **Request Content-Type** is *not* mandated in the spec prose, but both official SDKs require
  `application/json` (TS answers `415`, Python `400`; §6). A device may assume it.
- **Notifications.** Accepted ⇒ "`202 Accepted` with no body"; rejected ⇒ 4xx, optionally a JSON-RPC
  error with no `id`.
- **`MCP-Protocol-Version`** (case-insensitive header): "Every POST request to the MCP endpoint
  **MUST** include an `MCP-Protocol-Version` header"; "The header value **MUST** match the
  `io.modelcontextprotocol/protocolVersion` field carried in the request body's `_meta`." Mismatch ⇒
  `400` + `-32020` (`HeaderMismatch`). Unsupported version ⇒ `400` + `-32022`
  (`UnsupportedProtocolVersion`) listing `data.supported`.
- **`Mcp-Method` / `Mcp-Name`**: required for all requests / for `tools/call`, `resources/read`,
  `prompts/get` respectively; "These headers are **REQUIRED** for compliance." Servers "**MUST** reject"
  header/body mismatches with `400` + `-32020`. Non-ASCII names use `=?base64?…?=`.
- **Unknown method** ⇒ "`404 Not Found` and a JSON-RPC error with code `-32601`".
- **Sessions.** Gone. "Removal of protocol-level sessions"; an incoming `Mcp-Session-Id` "**ignore it**,
  and do not mint or echo session IDs"; `Last-Event-ID` "ignore it; streams are not resumable".
- **Origin.** "Servers **MUST** validate the `Origin` header on all incoming connections to prevent DNS
  rebinding attacks. If the `Origin` header is present and invalid, servers **MUST** respond with HTTP
  403 Forbidden."
- **Binding.** "When running locally, servers **SHOULD** bind only to localhost (127.0.0.1) rather than
  all network interfaces (0.0.0.0)." If you ever stream: `X-Accel-Buffering: no` **SHOULD** be set,
  keep-alives are `:` comment lines, and "Resumable SSE streams via `Last-Event-ID` are not supported."

## 4. Can it always be JSON? Can it be stateless?
- **Yes, JSON-always is compliant** for every request except `subscriptions/listen`, whose *response* is
  a long-lived notification stream; a device that never advertises `listChanged`/subscriptions can omit it
  (the Python SDK special-cases exactly this: [`_streamable_http_modern.py#L421`](https://github.com/modelcontextprotocol/python-sdk/blob/main/src/mcp/server/_streamable_http_modern.py)).
- **SSE is mandatory only** when you emit mid-call notifications (`notifications/progress`,
  `notifications/message`) before the result: "The server **MAY** send" them "before the final
  response", which requires a stream. Dropping them is legal.
- **Stateless, yes.** "MCP is a **stateless protocol**: all the information needed to process a request
  is contained in the request itself… Servers **MUST NOT** rely on prior requests over the same
  connection" ([basic §Statelessness](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/5f5440bb26a62e2cf3440b92da5a667efa03b267/docs/specification/2026-07-28/basic/index.mdx)). No session id exists to omit.
  (Legacy also permits omitting it — §2b.)
- **Clients MUST accept both** content types, so `application/json` alone satisfies the client contract.

## 5. Security requirements for a local/HTTP server
- `Origin` validation: **MUST**, `403` on a present-and-invalid value (§3). Note "if present and invalid"
  — an absent `Origin` (native client, curl) is allowed.
- Bind to loopback: **SHOULD** (127.0.0.1, not 0.0.0.0).
- Authentication: "Servers **SHOULD** implement proper authentication for all connections" (§3). For local
  servers: "Use the `stdio` transport to limit access to just the MCP client. Restrict access if using an
  HTTP transport, such as: Require an authorization token; Use unix domain sockets or other … IPC"; a local
  server "left running on localhost" is reachable "via DNS rebinding" ([security best practices](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/5f5440bb26a62e2cf3440b92da5a667efa03b267/docs/docs/2026-07-28/tutorials/security/security_best_practices.mdx)).
- Statelessness moves risk into handles: "MCP servers **MUST NOT** treat possession of a state handle as
  authentication" ([§State Handle Hijacking](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/5f5440bb26a62e2cf3440b92da5a667efa03b267/docs/docs/2026-07-28/tutorials/security/security_best_practices.mdx)).
- OAuth 2.1 is the HTTP auth framework ([basic §Auth](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/5f5440bb26a62e2cf3440b92da5a667efa03b267/docs/specification/2026-07-28/basic/index.mdx)) — out of scope per the brief, and
  unusable without TLS anyway. **Consequence for this device:** the only honest postures are
  (a) loopback-only behind a host bridge, or (b) LAN HTTP plus a static bearer token, an Origin allowlist
  and an IP allowlist — which is *not* spec-grade auth, only mitigation. Mark that as an accepted risk.
- The transport spec does not mandate TLS, but auth does: "Reject `http://` URLs except for loopback
  addresses" ([security best practices](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/5f5440bb26a62e2cf3440b92da5a667efa03b267/docs/docs/2026-07-28/tutorials/security/security_best_practices.mdx)).

## 6. How existing implementations do it

**TypeScript SDK v2 (modern).** `createMcpHandler` "serves the 2026-07-28 protocol revision, with
old-school stateless 2025-era serving as the default fallback"
([`createMcpHandler.ts#L2`](https://github.com/modelcontextprotocol/typescript-sdk/blob/main/packages/server/src/server/createMcpHandler.ts)). It does
**no** Origin/Host validation itself: "The entry performs no Origin/Host validation (mount the
origin/host …)" ([#L25](https://github.com/modelcontextprotocol/typescript-sdk/blob/main/packages/server/src/server/createMcpHandler.ts)); it exports
`originValidationResponse` / `hostHeaderValidationResponse` helpers instead. GET/DELETE ⇒ `405`
([#L338](https://github.com/modelcontextprotocol/typescript-sdk/blob/main/packages/server/src/server/createMcpHandler.ts)); POST not `application/json` ⇒ `415`
([#L956](https://github.com/modelcontextprotocol/typescript-sdk/blob/main/packages/server/src/server/createMcpHandler.ts)).

**TS per-request transport (the framing to copy).** One transport per inbound message; "a `202` with no
body for notifications, a single JSON body for requests whose handler produces no streamed output, or a
lazily-opened SSE stream when the handler emits related messages … before its result"
([`perRequestTransport.ts#L6-L13`](https://github.com/modelcontextprotocol/typescript-sdk/blob/main/packages/server/src/server/perRequestTransport.ts)). SSE frame is
`event: message\ndata: {json}\n\n` ([#L428](https://github.com/modelcontextprotocol/typescript-sdk/blob/main/packages/server/src/server/perRequestTransport.ts)); stream headers are
`text/event-stream`, `Cache-Control: no-cache, no-transform`, `Connection: keep-alive`,
`X-Accel-Buffering: no` ([#L395-L410](https://github.com/modelcontextprotocol/typescript-sdk/blob/main/packages/server/src/server/perRequestTransport.ts)). A `responseMode`
of `auto | json | sse` exists; `json` "drops mid-call notifications" ([#L715-L718](https://github.com/modelcontextprotocol/typescript-sdk/blob/main/packages/server/src/server/createMcpHandler.ts)).

**TS legacy transport (contrast).** `Accept` must contain **both** types else `406`
([#L786-L796](https://github.com/modelcontextprotocol/typescript-sdk/blob/main/packages/server/src/server/streamableHttp.ts)); `Content-Type: application/json` else `415`
([#L805](https://github.com/modelcontextprotocol/typescript-sdk/blob/main/packages/server/src/server/streamableHttp.ts)); bad session id ⇒ `404` ([#L684](https://github.com/modelcontextprotocol/typescript-sdk/blob/main/packages/server/src/server/streamableHttp.ts)).

**Python SDK.** DNS-rebinding middleware is **off by default** — "disable DNS rebinding protection by
default for backwards compatibility" ([`transport_security.py#L47`](https://github.com/modelcontextprotocol/python-sdk/blob/main/src/mcp/server/transport_security.py)); when on: bad
`Content-Type` on POST ⇒ `400`, bad `Host` ⇒ `421`, bad `Origin` ⇒ `403` ([#L107-L121](https://github.com/modelcontextprotocol/python-sdk/blob/main/src/mcp/server/transport_security.py)); body
cap 4 MiB ([#L15](https://github.com/modelcontextprotocol/python-sdk/blob/main/src/mcp/server/transport_security.py)). Modern handler: non-POST ⇒ `405` + `Allow: POST`
([`_streamable_http_modern.py#L390-L392`](https://github.com/modelcontextprotocol/python-sdk/blob/main/src/mcp/server/_streamable_http_modern.py)); unusable `Accept` ⇒ `406` ([#L395-L397](https://github.com/modelcontextprotocol/python-sdk/blob/main/src/mcp/server/_streamable_http_modern.py));
`json_response=True` forces one JSON body except for `subscriptions/listen` ([#L455](https://github.com/modelcontextprotocol/python-sdk/blob/main/src/mcp/server/_streamable_http_modern.py)); `Accept: */*`
satisfies both types ([`streamable_http.py#L87-L102`](https://github.com/modelcontextprotocol/python-sdk/blob/main/src/mcp/server/streamable_http.py)).

**Protocol-era support in clients (matters for the recommendation).**
- MCP Inspector: `protocolEra` is `legacy | auto | modern`, and "**`legacy`** — **The default.** Plain
  `initialize`, no probing at all." ([inspector/protocol-eras](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/5f5440bb26a62e2cf3440b92da5a667efa03b267/docs/docs/2026-07-28/tools/inspector/protocol-eras.mdx)).
- Claude Code: "The v1 runtime is built on MCP TypeScript SDK 1.x. The v2 runtime is the same code on MCP
  TypeScript SDK 2.0, which adds MCP protocol revision 2026-07-28"; `MCP_PROTOCOL_NEGOTIATION` is `auto` or
  `legacy` ([Claude Code MCP docs](https://code.claude.com/docs/en/mcp)).
- Claude Desktop: local config is a stdio `command` only ([connect-local-servers](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/5f5440bb26a62e2cf3440b92da5a667efa03b267/docs/docs/2026-07-28/develop/connect-local-servers.mdx));
  Cursor documents stdio/SSE/Streamable HTTP but not a revision ([Cursor MCP](https://cursor.com/docs/mcp)) — **UNVERIFIED**.

**Small C/C++ code.** No official C++ SDK exists at `modelcontextprotocol/cpp-sdk` (GitHub API returns
404 on 2026-10-19; UNVERIFIED whether one exists under another name). Closest usable references:

- [hkr04/cpp-mcp](https://github.com/hkr04/cpp-mcp) — MIT; 17 source files (~200 KB, `mcp_server.cpp` 60 KB); HTTP+SSE
  (the *old* transport) + stdio; "conforming to the 2025-03-26 basic protocol specification".
- [GopherSecurity/gopher-mcp](https://github.com/GopherSecurity/gopher-mcp) — Apache-2.0; 16 MB repo; stdio, HTTP+SSE,
  Streamable HTTP; revision UNVERIFIED. [radare2-mcp](https://github.com/radareorg/radare2-mcp) — MIT, C, stdio only,
  revision UNVERIFIED.

None is a drop-in for a no-TLS, no-stdio, 8 MB device; they are useful mainly as a shape reference.

## 7. Smallest compliant subset for this device

**Recommendation: ship legacy 2025-06-18 on the device; keep 2026-07-28 out of firmware for now.**

Legacy mini MCP (all JSON, one endpoint, no session, no SSE):
1. `POST /mcp` only; `GET`/`DELETE` ⇒ `405`. Require `Content-Type: application/json`; require `Accept` to
   contain `application/json` (accept `*/*`); read `Content-Length` body ≤ 4 KB into PSRAM.
2. `initialize` ⇒ echo `"protocolVersion":"2025-06-18"`, `capabilities:{"tools":{}}`, `serverInfo`,
   `instructions`; `notifications/initialized` (or any notification) ⇒ `202`, empty body.
3. `ping` ⇒ `{"result":{}}` (some clients send it).
4. `tools/list` ⇒ `{"tools":[…]}` — 4–6 read-only tools, no `resultType`, no `ttlMs`.
5. `tools/call` ⇒ `{"content":[{"type":"text","text":"…"}],"isError":false}`; unknown tool ⇒ `-32602`;
   bad args ⇒ `isError:true` with an explanatory string.
6. Errors: `-32700` parse, `-32600` invalid, `-32601` method not found, `-32602` params.

Why legacy: same 3-method surface plus `initialize`/`ping`, with **no** header↔body cross-validation, **no**
`resultType`, **no** `ttlMs`/`cacheScope`, and it is what Inspector defaults to and older Cursor/Claude builds
speak (Claude Code's v1 runtime is TS SDK 1.x). Modern is *conceptually* smaller but adds `MCP-Protocol-Version`/`Mcp-Method`/`Mcp-Name`
parsing and their `-32020` paths, `server/discover`, two mandatory caching fields, and `405`/`415`/`406`
discipline — more code for fewer clients; and modern clients negotiate down automatically anyway.

Guard rails regardless of era: strict `Origin` allowlist (or absent `Origin` only), reject a `Host` that
is not the device's own IP:port, single `/mcp` path, no `Access-Control-Allow-Origin`, plus a static bearer
token in `Authorization` if the device is reachable beyond a trusted LAN.

**Host-side stdio bridge (the alternative).** A ~150–250 line Python/Node process: read one JSON-RPC message
per line from stdin, POST it to `http://device:8000/mcp` with `Content-Type: application/json` and
`Accept: application/json, text/event-stream`, read the JSON reply, write it plus `\n` to stdout, log to
stderr. It needs an HTTP client, a `Content-Length` body loop, a line reader and a serialised queue (the
device is single-threaded). It can own the legacy `initialize`/`initialized` dance so the firmware need not,
and it is the only way to reach Claude Desktop, whose local config runs a stdio `command`.

**Tradeoffs.**

| | Firmware HTTP (legacy) | stdio bridge in front |
|---|---|---|
| Claude Desktop | ✗ (local config is stdio) | ✓ |
| Claude Code / Cursor / Inspector | ✓ with a URL | ✓ |
| TLS / auth exposure | plaintext on LAN; token + Origin allowlist only | device stays on loopback or a private link; bridge holds auth |
| Firmware complexity | +JSON parser, +body read, +HTTP methods/statuses | firmware unchanged except the JSON-RPC handler |
| RAM / failure modes | ~4 KB body + 4–8 KB response buffer; client times out on a stalled SD read | negligible on device; bridge retries, queues, adds timeouts |
| Future 2026-07-28 | requires new firmware | bridge can translate modern ⇄ legacy |

Sizing (estimate): 5 tools ≈ 1.5–3 KB of JSON, a 20-track result ≈ 1–2 KB — both fit in PSRAM, but replies
must be built there and sent with `Content-Length`, not through the 1 KB header buffer.

## 8. UNVERIFIED / open
- Cursor's supported MCP protocol revision (docs name transports, not revisions), and whether Claude
  Desktop can attach to an arbitrary remote `http://` MCP URL without a connector.
- Existence of an official C++ SDK under another name; protocol revision of `gopher-mcp`/`radare2-mcp`.
- Exact max header/body limits enforced by current clients (not specified anywhere I could cite).

## Addendum (2026-10-19): the embedded reference implementation

After this note was written, the project found [solnera/esp32-mcpserver](https://github.com/solnera/esp32-mcpserver)
(MIT, C++/Arduino, `ESPAsyncWebServer`, 13 stars). It is a working ESP32 MCP server and the
closest primary source for a mini MCP on this device. Its choices:

- Default **2025-11-25**, with `2025-06-18` and `2025-03-26` echoed when requested.
- `initialize`, `notifications/initialized`, `ping`, `tools/list`, `tools/call`; tools only.
- Stateless, JSON-only, no SSE; `POST /mcp`; `GET`/`DELETE` 405 + `Allow: POST`.
- `Accept` is not validated; only `Content-Type: application/json`.
- Strict `initialize` (protocolVersion, capabilities, clientInfo.name/version).
- Body cap 8192; 411/413/415/403/400/202/200/500 error contract.
- Dedicated `tools/call` worker with deferred chunked replies, 505 for HTTP/1.0, fast path 20 ms.
- mDNS `_mcp._tcp`; strict `Origin` pin to local IP or `<slug>.local`; no Host-only check.
- `structuredContent` for object results (optional via `MCP_OMIT_TEXT_WHEN_STRUCTURED`).

This supersedes the earlier "2025-06-18 default" recommendation in this note: the main document
now targets 2025-11-25 and keeps the other two for compatibility. The worker/deferred design was
not adopted because this device's tools are bounded memory reads; see §4.4 of the main document.
