#!/usr/bin/env python3
"""Exercise rescan generations, leases and the broadcast switch.

The caller starts a server over a mutable music directory with `initial` valid
tracks. This script adds one, rescans, holds the old binary library view open while
the new generation is published, and checks that the broadcast eventually sees the
new count. A second pair of simultaneous rescans must produce exactly one 202 and
one 409.
"""
import json
import socket
import struct
import sys
import threading
import time

HOST, PORT, MUSIC, INITIAL = sys.argv[1], int(sys.argv[2]), sys.argv[3], int(sys.argv[4])
FAIL = 0

# The wire layout of GET /api/library.bin: a 64-byte header, then nodes, edges,
# the track index and the string table. Only the header is read here.
LIB_MAGIC = 0x424C5458
LIB_HEADER = struct.Struct("<IHH8IQ2IQ")
assert LIB_HEADER.size == 64


def check(label, got, want):
    global FAIL
    if got != want:
        print(f"FAIL {label}: got {got!r}, want {want!r}")
        FAIL += 1
    else:
        print(f"  ok    {label}")


def request(method, path, headers=None, body=None):
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
    h, _, body = data.partition(b"\r\n\r\n")
    return int(h.split(b"\r\n")[0].split()[1]), body


def library_view(body):
    """The header fields this suite reads: the generation and the track count."""
    fields = LIB_HEADER.unpack_from(body, 0)
    if fields[0] != LIB_MAGIC:
        raise ValueError(f"not a library blob: magic 0x{fields[0]:08x}")
    return {"generation": fields[9], "count": fields[6]}


def library_get():
    status, body = request("GET", "/api/library.bin")
    return status, library_view(body)


def rescan():
    return request("POST", "/api/rescan", body=b"{}")[0]


def player_status():
    status, body = request(
        "POST", "/mcp", body=json.dumps({
            "jsonrpc": "2.0", "id": 1, "method": "tools/call",
            "params": {"name": "player_status", "arguments": {}}}).encode())
    doc = json.loads(body)
    return json.loads(doc["result"]["content"][0]["text"])


def wait_count(want, timeout=8.0):
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        try:
            st = player_status()
            last = st.get("library_count")
            if last == want:
                return True
        except Exception:
            pass
        time.sleep(0.2)
    print(f"FAIL broadcast never reported {want} tracks (last {last!r})")
    global FAIL
    FAIL += 1
    return False


def main():
    status, doc = library_get()
    check("initial library is 200", status, 200)
    check("initial generation is 1", doc["generation"], 1)
    check("initial count", doc["count"], INITIAL)

    # Hold an old library view open without reading its body. Its lease must
    # survive the publish below.
    slow = socket.create_connection((HOST, PORT), timeout=10)
    slow.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
    slow.sendall(f"GET /api/library.bin HTTP/1.0\r\nHost: {HOST}:{PORT}\r\n\r\n".encode())
    head = b""
    while b"\r\n\r\n" not in head:
        head += slow.recv(1)
    check("slow reader got the old generation's headers", b'ETag: "gen-1"' in head, True)
    length = 0
    for line in head.decode("latin1").split("\r\n"):
        if line.lower().startswith("content-length:"):
            length = int(line.split(":", 1)[1])
    check("the old view is larger than socket buffers", length > 100000, True)

    with open(f"{MUSIC}/zz-new.wav", "wb") as f:
        f.write(open(f"{MUSIC}/t0000.wav", "rb").read())

    check("rescan starts", rescan(), 202)
    deadline = time.time() + 5
    current = None
    while time.time() < deadline:
        _, current = library_get()
        if current["generation"] == 2:
            break
        time.sleep(0.05)
    check("the new generation publishes", current["generation"], 2)
    check("the new generation sees the added track", current["count"], INITIAL + 1)

    # The old body is still valid and is still generation 1.
    body = b""
    while True:
        part = slow.recv(65536)
        if not part:
            break
        body += part
    slow.close()
    old = library_view(body)
    check("the slow reader completes on the old bytes", old["generation"], 1)
    check("and still sees the old count", old["count"], INITIAL)

    check("the broadcast sees the new count", wait_count(INITIAL + 1), True)

    # Two requests at once: one scan wins, the other is declined.
    results = []
    lock = threading.Lock()

    def race():
        code = rescan()
        with lock:
            results.append(code)

    threads = [threading.Thread(target=race) for _ in range(2)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    check("one of two simultaneous rescans is accepted", sorted(results)[0], 202)
    check("the other is declined", sorted(results)[1], 409)

    if FAIL:
        print(f"{FAIL} failure(s)")
        return 1
    print("rescan generations and leases: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
