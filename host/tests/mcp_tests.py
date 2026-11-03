#!/usr/bin/env python3
"""Exercise the on-device MCP endpoint over raw HTTP.

This is the MCP Inspector path without the Inspector: initialize (three legacy
revisions and a fallback), notifications, tools/list, the five read-only tools,
and the protocol/transport error table. It also checks that no session header
is ever emitted and that Accept is ignored.
"""
import json
import socket
import sys

HOST = sys.argv[1]
PORT = int(sys.argv[2])
FAIL = 0


def check(label, got, want):
    global FAIL
    if got != want:
        print(f"FAIL {label}: got {got!r}, want {want!r}")
        FAIL += 1
    else:
        print(f"  ok    {label}")


def request(method, path, headers=None, body=None, omit_length=False, omit_host=False):
    headers = dict(headers or {})
    header = f"{method} {path} HTTP/1.0\r\n"
    if not omit_host and "Host" not in headers:
        header += f"Host: {HOST}:{PORT}\r\n"
    if body is not None and not omit_length:
        headers.setdefault("Content-Type", "application/json")
        header += f"Content-Length: {len(body)}\r\n"
    for k, v in headers.items():
        header += f"{k}: {v}\r\n"
    header += "\r\n"
    s = socket.create_connection((HOST, PORT), timeout=5)
    s.sendall(header.encode())
    if body:
        s.sendall(body)
    data = b""
    while True:
        part = s.recv(8192)
        if not part:
            break
        data += part
    s.close()
    head, _, payload = data.partition(b"\r\n\r\n")
    lines = head.decode("latin1").split("\r\n")
    status = int(lines[0].split()[1])
    hdrs = {}
    for line in lines[1:]:
        k, _, v = line.partition(":")
        hdrs[k.lower()] = v.strip()
    return status, hdrs, payload


def call(obj, headers=None):
    body = json.dumps(obj, separators=(",", ":")).encode()
    status, hdrs, payload = request("POST", "/mcp", headers, body)
    doc = json.loads(payload) if payload else None
    return status, hdrs, doc


def initialize(version="2025-11-25", client=True):
    params = {"protocolVersion": version, "capabilities": {}}
    if client:
        params["clientInfo"] = {"name": "mcp-tests", "version": "1.0.0"}
    return {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": params}


def main():
    # initialize across the three legacy revisions and a fallback.
    for version in ("2025-11-25", "2025-06-18", "2025-03-26"):
        status, hdrs, doc = call(initialize(version))
        check(f"initialize {version} is 200", status, 200)
        check(f"initialize {version} echoes", doc["result"]["protocolVersion"], version)
        check("initialize carries MCP-Protocol-Version",
              hdrs.get("mcp-protocol-version"), "2025-11-25")
        check("initialize never emits a session id", "mcp-session-id" in hdrs, False)
    status, _hdrs, doc = call(initialize("2026-07-28"))
    check("an unknown protocolVersion falls back", doc["result"]["protocolVersion"], "2025-11-25")

    bad = initialize()
    del bad["params"]["capabilities"]
    status, _hdrs, doc = call(bad)
    check("missing capabilities is 200", status, 200)
    check("and is -32602", doc["error"]["code"], -32602)
    check("and names the field", "capabilities" in doc["error"]["message"], True)

    bad = initialize()
    del bad["params"]["clientInfo"]["version"]
    status, _hdrs, doc = call(bad)
    check("missing clientInfo.version is -32602", doc["error"]["code"], -32602)
    check("and names the nested field", "clientInfo.version" in doc["error"]["message"], True)

    status, hdrs, payload = request(
        "POST", "/mcp", {"Content-Type": "application/json"},
        b'{"jsonrpc":"2.0","method":"notifications/initialized"}')
    check("a notification is 202", status, 202)
    check("and has no body", payload, b"")
    check("and still names the protocol", hdrs.get("mcp-protocol-version"), "2025-11-25")

    status, _hdrs, doc = call({"jsonrpc": "2.0", "id": 2, "method": "notifications/initialized"})
    check("initialized with an id is 200", status, 200)
    check("and is -32600", doc["error"]["code"], -32600)

    status, _hdrs, doc = call({"jsonrpc": "2.0", "id": 3, "method": "ping"})
    check("ping is 200", status, 200)
    check("ping result is empty", doc["result"], {})

    status, _hdrs, doc = call({"jsonrpc": "2.0", "id": 4, "method": "no/such"})
    check("unknown method is -32601", doc["error"]["code"], -32601)

    status, hdrs, doc = call({"jsonrpc": "2.0", "id": 5, "method": "tools/list"})
    check("tools/list is 200", status, 200)
    tools = doc["result"]["tools"]
    names = [t["name"] for t in tools]
    check("tools are exactly the read-only set", names,
          ["library_stats", "library_list", "library_search", "library_track", "player_status"])
    check("no control tool leaks", any(n.startswith("player_") and n != "player_status" for n in names), False)
    check("every tool has an inputSchema", all("inputSchema" in t for t in tools), True)
    check("every tool is read-only", all(t["annotations"]["readOnlyHint"] is True for t in tools), True)
    check("tools/list does not emit a session id", "mcp-session-id" in hdrs, False)

    def tool(name, arguments=None):
        status, _hdrs, doc = call({"jsonrpc": "2.0", "id": 9, "method": "tools/call",
                                  "params": {"name": name, "arguments": arguments or {}}})
        check(f"{name} is 200", status, 200)
        result = doc["result"]
        check(f"{name} is text-only", list(result["content"][0].keys()), ["type", "text"])
        check(f"{name} has no structuredContent", "structuredContent" in result, False)
        return result, json.loads(result["content"][0]["text"]) if not result["isError"] else None

    result, stats = tool("library_stats")
    check("stats is not an error", result["isError"], False)
    check("stats sees the fixture", stats["tracks"], 11)
    check("stats names the generation", stats["generation"], 1)

    result, listing = tool("library_list", {"limit": 3})
    check("library_list is not an error", result["isError"], False)
    check("library_list reports truncation at the cap", listing["truncated"], True)
    check("library_list has three entries", len(listing["entries"]), 3)

    result, search = tool("library_search", {"query": "tone-44", "limit": 2})
    check("search is not an error", result["isError"], False)
    check("search found the tones", len(search["tracks"]), 2)
    check("search reports the cap", search["truncated"], True)

    result, track = tool("library_track", {"path": "tone-44100.flac"})
    check("track is not an error", result["isError"], False)
    check("track has the path", track["path"], "tone-44100.flac")

    result, status_doc = tool("player_status")
    check("player_status is not an error", result["isError"], False)
    check("player_status sees the library", status_doc["library_count"], 11)

    result, _ = tool("library_track", {"path": "no/such.mp3"})
    check("an unknown track is a tool error", result["isError"], True)

    status, _hdrs, doc = call({"jsonrpc": "2.0", "id": 7, "method": "tools/call",
                              "params": {"name": "player_skip", "arguments": {}}})
    check("an unknown tool is -32602", doc["error"]["code"], -32602)

    status, hdrs, payload = request("POST", "/mcp", {"Content-Type": "application/json"},
                                    b'{"jsonrpc":"2.0",')
    check("malformed JSON is 400", status, 400)
    doc = json.loads(payload)
    check("malformed JSON is -32700", doc["error"]["code"], -32700)
    check("malformed JSON still names the protocol", hdrs.get("mcp-protocol-version"), "2025-11-25")

    status, _hdrs, doc = call({"jsonrpc": "1.0", "id": 8, "method": "ping"})
    check("a bad envelope is 400", status, 400)
    check("a bad envelope is -32600", doc["error"]["code"], -32600)

    status, _hdrs, doc = call({"jsonrpc": "2.0", "id": True, "method": "ping"})
    check("a boolean id is an invalid envelope", status, 400)
    check("a boolean id is -32600", doc["error"]["code"], -32600)

    status, _hdrs, doc = call([{"jsonrpc": "2.0", "id": 1, "method": "ping"}])
    check("a batch is an invalid envelope", status, 400)
    check("a batch is -32600", doc["error"]["code"], -32600)

    status, _hdrs, doc = call({"jsonrpc": "2.0", "id": 10, "method": "ping"},
                              {"MCP-Protocol-Version": "2026-07-28"})
    check("an unsupported request version is 400", status, 400)
    check("and is -32602", doc["error"]["code"], -32602)

    status, hdrs, doc = call(initialize(), {"Accept": "text/html"})
    check("Accept is ignored for the status", status, 200)
    check("Accept is ignored for the type", hdrs.get("content-type"), "application/json")

    # Transport-level body errors are JSON-RPC and carry the version too.
    status, hdrs, payload = request("POST", "/mcp", {"Content-Type": "application/json"},
                                    None, omit_length=True)
    check("missing Content-Length is 411", status, 411)
    check("missing Content-Length is -32600", json.loads(payload)["error"]["code"], -32600)
    check("missing Content-Length names the protocol", hdrs.get("mcp-protocol-version"), "2025-11-25")

    body = b'{"jsonrpc":"2.0","id":1,"method":"ping"}'
    status, _hdrs, payload = request("POST", "/mcp", {"Content-Type": "text/plain"}, body)
    check("wrong content type is 415", status, 415)
    check("wrong content type is -32600", json.loads(payload)["error"]["code"], -32600)

    big = b" " * 9000
    status, _hdrs, payload = request("POST", "/mcp", {"Content-Type": "application/json"}, big)
    check("oversized body is 413", status, 413)
    check("oversized body is -32600", json.loads(payload)["error"]["code"], -32600)

    status, hdrs, payload = request("GET", "/mcp")
    check("GET /mcp is 405", status, 405)
    check("and advertises POST", hdrs.get("allow"), "POST")

    status, hdrs, payload = request("POST", "/mcp/extra", {"Content-Type": "application/json"},
                                    b'{"jsonrpc":"2.0","id":1,"method":"ping"}')
    check("a path under /mcp is 404", status, 404)
    check("and is -32600", json.loads(payload)["error"]["code"], -32600)

    status, hdrs, payload = request("DELETE", "/mcp")
    check("DELETE /mcp is 405", status, 405)
    check("and advertises POST", hdrs.get("allow"), "POST")

    long_method = "x" * 200
    status, hdrs, payload = request(
        "POST", "/mcp", {"Content-Type": "application/json"},
        json.dumps({"jsonrpc": "2.0", "method": long_method}, separators=(",", ":")).encode())
    check("a long notification method is still 202", status, 202)
    check("and has no body", payload, b"")

    long_id = "i" * 2000
    status, hdrs, payload = request(
        "POST", "/mcp", {"Content-Type": "application/json"},
        json.dumps({"jsonrpc": "2.0", "id": long_id, "method": "no/such"},
                   separators=(",", ":")).encode())
    check("a long id error is 200", status, 200)
    doc = json.loads(payload)
    check("and keeps the id", doc["id"], long_id)
    check("and still names the protocol", hdrs.get("mcp-protocol-version"), "2025-11-25")

    # Origin is checked only when present and pinned to the device address or
    # k7.local, not merely echoed from Host.
    base = f"{HOST}:{PORT}"
    status, _hdrs, doc = call(initialize(), {"Host": base, "Origin": f"http://{base}"})
    check("Origin pinned to the local IP is accepted", status, 200)
    status, _hdrs, doc = call(initialize(), {"Host": f"k7.local:{PORT}",
                                            "Origin": f"http://k7.local:{PORT}"})
    check("Origin pinned to k7.local is accepted", status, 200)
    status, _hdrs, doc = call(initialize(), {"Host": "evil.example:9999",
                                            "Origin": "http://evil.example:9999"})
    check("a rebinding authority is 403", status, 403)
    check("and is -32600", doc["error"]["code"], -32600)
    status, _hdrs, doc = call(initialize(), {"Host": base,
                                            "Origin": "http://evil.example:9999"})
    check("a mismatched Origin is 403", status, 403)
    body = json.dumps(initialize(), separators=(",", ":")).encode()
    status, _hdrs, doc = request("POST", "/mcp", {"Origin": f"http://{base}"}, body,
                                 omit_host=True)
    check("Origin without Host is 403", status, 403)
    status, _hdrs, doc = call(initialize(), {"Host": "elsewhere.invalid",
                                            "Origin": f"http://{base}"})
    check("a mismatched Host is 403", status, 403)
    status, _hdrs, doc = call(initialize(), {"Host": "elsewhere.invalid"})
    check("Host alone is not rejected", status, 200)
    status, _hdrs, payload = request(
        "POST", "/mcp", {"Host": base, "Origin": "http://evil.example:9999"},
        b'{"jsonrpc":"2.0",')
    check("bad Origin is 403 before JSON parsing", status, 403)
    check("and is -32600, not -32700", json.loads(payload)["error"]["code"], -32600)

    if FAIL:
        print(f"{FAIL} failure(s)")
        return 1
    print("MCP read-only surface: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
