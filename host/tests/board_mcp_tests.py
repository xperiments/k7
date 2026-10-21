#!/usr/bin/env python3
"""Read-only MCP smoke test for a live board (no fixture-count oracle)."""
import json
import socket
import sys
import urllib.parse

URL = sys.argv[1]
MCP_HOST = sys.argv[2] if len(sys.argv) > 2 else None
u = urllib.parse.urlparse(URL)
HOST = u.hostname
PORT = u.port or 80
FAIL = 0


def check(label, got, want):
    global FAIL
    if got != want:
        print(f"FAIL {label}: got {got!r}, want {want!r}")
        FAIL += 1
    else:
        print(f"  ok    {label}")


def request(path, body=None, host=None):
    host = host or f"{HOST}:{PORT}"
    head = f"POST {path} HTTP/1.0\r\nHost: {host}\r\nContent-Type: application/json\r\n"
    if body is not None:
        head += f"Content-Length: {len(body)}\r\n"
    head += "\r\n"
    s = socket.create_connection((HOST, PORT), timeout=10)
    s.sendall(head.encode() + (body or b""))
    data = b""
    while True:
        part = s.recv(65536)
        if not part:
            break
        data += part
    s.close()
    h, _, payload = data.partition(b"\r\n\r\n")
    return int(h.split(b"\r\n")[0].split()[1]), h, json.loads(payload)


def main():
    status, headers, doc = request("/mcp", json.dumps({
        "jsonrpc": "2.0", "id": 1, "method": "initialize",
        "params": {"protocolVersion": "2025-11-25", "capabilities": {},
                   "clientInfo": {"name": "board-mcp", "version": "1"}}}).encode())
    check("initialize over the board is 200", status, 200)
    check("initialize echo", doc["result"]["protocolVersion"], "2025-11-25")
    check("MCP-Protocol-Version", b"MCP-Protocol-Version: 2025-11-25" in headers, True)
    status, _headers, doc = request("/mcp", json.dumps({
        "jsonrpc": "2.0", "id": 2, "method": "tools/list"}).encode())
    names = [t["name"] for t in doc["result"]["tools"]]
    check("the default board is read-only", names,
          ["library_stats", "library_list", "library_search", "library_track", "player_status"])
    status, _headers, doc = request("/mcp", json.dumps({
        "jsonrpc": "2.0", "id": 3, "method": "tools/call",
        "params": {"name": "player_status", "arguments": {}}}).encode())
    st = json.loads(doc["result"]["content"][0]["text"])
    check("player_status sees a library", st["library_count"] > 0, True)
    check("player_status has a path", bool(st["path"]), True)

    if MCP_HOST:
        status, headers, doc = request("/mcp", json.dumps({
            "jsonrpc": "2.0", "id": 4, "method": "ping"}).encode(), host=MCP_HOST)
        check("through xtape.local is 200", status, 200)
        check("and returns pong", doc["result"], {})
    if FAIL:
        print(f"{FAIL} failure(s)")
        return 1
    print("board MCP read-only: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
