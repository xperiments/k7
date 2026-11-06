#!/usr/bin/env python3
"""Exercise the opt-in MCP control tools against a control build."""
import json
import socket
import struct
import sys
import threading
import time

HOST, PORT, INITIAL = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
FAIL = 0

# GET /api/library.bin: a 64-byte header, then 32-byte nodes, the u32 child-edge
# array, the u32 track index and the string table. Only what this test needs is
# decoded here: the generation and a track's node id (its index in that array).
LIB_MAGIC = 0x424C5458
LIB_HEADER = struct.Struct("<IHH8IQ2IQ")
LIB_NODE = struct.Struct("<QIIIHHBBHI")
assert LIB_HEADER.size == 64 and LIB_NODE.size == 32


def library_header(payload):
    fields = LIB_HEADER.unpack_from(payload, 0)
    if fields[0] != LIB_MAGIC:
        raise ValueError(f"not a library blob: magic 0x{fields[0]:08x}")
    return fields


def library_generation(payload):
    return library_header(payload)[9]


def library_node(payload, name):
    """(generation, node id) for the track called `name`, or None."""
    _, _, _, _, node_count, edge_count, track_count, _, _, generation, _, _, _, _, _ = \
        library_header(payload)
    nodes_off = LIB_HEADER.size
    strings_off = nodes_off + node_count * LIB_NODE.size + edge_count * 4 + track_count * 4
    strings = payload[strings_off:]
    for index in range(node_count):
        _, name_off, _, _, _, name_len, kind, _, _, _ = LIB_NODE.unpack_from(
            payload, nodes_off + index * LIB_NODE.size)
        if kind == 1 and strings[name_off:name_off + name_len].decode("utf-8") == name:
            return generation, index
    return None


def check(label, got, want):
    global FAIL
    if got != want:
        print(f"FAIL {label}: got {got!r}, want {want!r}")
        FAIL += 1
    else:
        print(f"  ok    {label}")


def request(method, path, body=None, headers=None):
    headers = dict(headers or {})
    head = f"{method} {path} HTTP/1.0\r\nHost: {HOST}:{PORT}\r\n"
    if body is not None:
        headers.setdefault("Content-Type", "application/json")
        head += f"Content-Length: {len(body)}\r\n"
    for k, v in headers.items():
        head += f"{k}: {v}\r\n"
    head += "\r\n"
    s = socket.create_connection((HOST, PORT), timeout=10)
    s.sendall(head.encode())
    if body:
        s.sendall(body)
    data = b""
    while True:
        part = s.recv(65536)
        if not part:
            break
        data += part
    s.close()
    h, _, payload = data.partition(b"\r\n\r\n")
    return int(h.split(b"\r\n")[0].split()[1]), payload


def call(obj):
    status, body = request("POST", "/mcp", json.dumps(obj, separators=(",", ":")).encode())
    return status, json.loads(body)


def tool(name, args=None):
    status, doc = call({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                        "params": {"name": name, "arguments": args or {}}})
    result = doc["result"]
    text = result["content"][0]["text"]
    return result["isError"], (json.loads(text) if not result["isError"] else text)


def stream_alive():
    s = socket.create_connection((HOST, PORT), timeout=5)
    s.sendall(f"GET /stream HTTP/1.0\r\nHost: {HOST}:{PORT}\r\n\r\n".encode())
    data = b""
    while b"\r\n\r\n" not in data:
        part = s.recv(4096)
        if not part:
            break
        data += part
    s.close()
    return data.startswith(b"HTTP/1.0 200")


def wait_path(want, timeout=5.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            err, st = tool("player_status")
            if not err and st["path"] == want:
                return True
        except Exception:
            pass
        time.sleep(0.1)
    return False


def main():
    status, doc = call({"jsonrpc": "2.0", "id": 1, "method": "initialize",
                        "params": {"protocolVersion": "2025-11-25", "capabilities": {},
                                   "clientInfo": {"name": "control-tests", "version": "1"}}})
    check("initialize is 200", status, 200)
    status, doc = call({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
    names = [t["name"] for t in doc["result"]["tools"]]
    check("tools/list has nine tools", len(names), 9)
    check("control tools are listed", names[5:],
          ["player_play", "player_skip", "player_prev", "library_rescan"])
    control = {t["name"]: t for t in doc["result"]["tools"] if not t["annotations"]["readOnlyHint"]}
    check("control tools are not read-only", sorted(control), ["library_rescan", "player_play",
                                                               "player_prev", "player_skip"])
    check("control tools are non-destructive", all(t["annotations"].get("destructiveHint") is False
                                                   for t in control.values()), True)

    err, text = tool("player_play", {"path": "no/such.wav"})
    check("a bad path is a tool error", err, True)
    check("the bad path says why", "no such track" in text, True)
    check("the stream survives a bad path", stream_alive(), True)

    check("a valid path is accepted", tool("player_play", {"path": "t0000.wav"})[0], False)
    check("the valid path reaches the broadcast", wait_path("t0000.wav"), True)

    before = tool("player_status")[1]["path"]
    err, _ = tool("player_skip")
    check("skip is accepted", err, False)
    check("skip changes the Track at the next chunk", wait_path(
        "t0001.wav" if before == "t0000.wav" else before, 5), True)
    err, _ = tool("player_prev")
    check("prev is accepted", err, False)

    # id + generation is the fast path; use the id the blob reports.
    _, payload = request("GET", "/api/library.bin")
    found = library_node(payload, "t0002.wav")
    check("the tree has an id for t0002.wav", found is not None, True)
    if found:
        generation, node_id = found
        err, _ = tool("player_play", {"id": node_id, "generation": generation})
        check("id + generation is accepted", err, False)
        check("id + generation reaches the Track", wait_path("t0002.wav"), True)

    results = []
    lock = threading.Lock()
    def rescan():
        code, body = request("POST", "/mcp", json.dumps({
            "jsonrpc": "2.0", "id": 3, "method": "tools/call",
            "params": {"name": "library_rescan", "arguments": {}}}).encode())
        with lock:
            results.append((code, json.loads(body)))
    threads = [threading.Thread(target=rescan) for _ in range(2)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    firsts = [r[1]["result"]["isError"] for r in results]
    check("one rescan starts", firsts.count(False), 1)
    check("the second rescan is a tool error", firsts.count(True), 1)
    busy = [r[1]["result"]["content"][0]["text"] for r in results if r[1]["result"]["isError"]]
    check("the second says a scan is running", "already running" in busy[0], True)

    deadline = time.time() + 5
    gen = None
    while time.time() < deadline:
        _, payload = request("GET", "/api/library.bin")
        gen = library_generation(payload)
        if gen == 2:
            break
        time.sleep(0.1)
    check("the rescan published a new generation", gen, 2)

    if FAIL:
        print(f"{FAIL} failure(s)")
        return 1
    print("MCP control tools: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
