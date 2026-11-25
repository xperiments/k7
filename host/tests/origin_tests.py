#!/usr/bin/env python3
"""The two App origins, end to end, against the host server.

A shipping build names the deployed App's origin in its CORS reply and accepts it in the
MCP guard. A development build (`tools/build.sh dev`; ADR 0082) also trusts the App served
at https://k7-dev.xperiments.dev:8000, and the reply names whichever origin made the
request. This checks both builds, the preflight, and the MCP guard's answer to each.

Usage: origin_tests.py SERVER FIXTURES_DIR
"""
import json
import os
import socket
import subprocess
import sys
import time

SERVER = sys.argv[1]
FIXTURES = sys.argv[2]
FAIL = 0

DEPLOYED = "https://k7.xperiments.dev"
DEVELOPMENT = "https://k7-dev.xperiments.dev:8000"
EVIL = "https://evil.example"

MCP_BODY = json.dumps(
    {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params": {
            "protocolVersion": "2025-11-25",
            "capabilities": {},
            "clientInfo": {"name": "origin-tests", "version": "1.0.0"},
        },
    },
    separators=(",", ":"),
).encode()


def check(label, got, want):
    global FAIL
    if got != want:
        print(f"FAIL {label}: got {got!r}, want {want!r}")
        FAIL += 1
    else:
        print(f"  ok    {label}")


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def request(port, method, path, headers=None, body=None, host=None):
    headers = dict(headers or {})
    header = f"{method} {path} HTTP/1.0\r\nHost: {host or f'127.0.0.1:{port}'}\r\n"
    if body is not None:
        headers.setdefault("Content-Type", "application/json")
        header += f"Content-Length: {len(body)}\r\n"
    for name, value in headers.items():
        header += f"{name}: {value}\r\n"
    header += "\r\n"
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.sendall(header.encode())
    if body:
        s.sendall(body)
    data = b""
    while True:
        part = s.recv(65536)
        if not part:
            break
        data += part
    s.close()
    head, _, payload = data.partition(b"\r\n\r\n")
    lines = head.decode("latin1").split("\r\n")
    status = int(lines[0].split()[1])
    reply = {}
    for line in lines[1:]:
        name, _, value = line.partition(":")
        reply[name.strip().lower()] = value.strip()
    return status, reply, payload


def get(port, origin):
    headers = {"Origin": origin} if origin else {}
    return request(port, "GET", "/api/status", headers)


def start(music, port, dev_origin=None, env=None):
    args = [SERVER, "--music-dir", music, "--port", str(port)]
    if dev_origin:
        args += ["--app-origin-dev", dev_origin]
    proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
    deadline = time.time() + 15
    while time.time() < deadline:
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=0.5)
            s.sendall(b"GET /library HTTP/1.0\r\nHost: t\r\n\r\n")
            s.recv(64)
            s.close()
            return proc
        except OSError:
            time.sleep(0.1)
    proc.kill()
    raise SystemExit("the host server did not start")


def stop(proc):
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()


def shipping():
    port = free_port()
    proc = start(FIXTURES, port)
    try:
        status, headers, _ = get(port, None)
        check("shipping: a request without Origin is served", status, 200)
        check("shipping: the reply names the deployed origin",
              headers.get("access-control-allow-origin"), DEPLOYED)
        check("shipping: the reply does not vary by origin", "vary" in headers, False)

        status, headers, _ = get(port, DEPLOYED)
        check("shipping: the deployed App is named", status, 200)
        check("shipping: and reads the reply",
              headers.get("access-control-allow-origin"), DEPLOYED)

        status, headers, _ = get(port, DEVELOPMENT)
        check("shipping: the dev origin is answered", status, 200)
        check("shipping: but the reply still names the deployed origin",
              headers.get("access-control-allow-origin"), DEPLOYED)

        status, headers, _ = request(
            port, "OPTIONS", "/api/status",
            {"Origin": DEVELOPMENT, "Access-Control-Request-Method": "GET"},
        )
        check("shipping: the preflight is answered", status, 204)
        check("shipping: the preflight names the deployed origin",
              headers.get("access-control-allow-origin"), DEPLOYED)

        status, _, _ = request(
            port, "POST", "/mcp", {"Origin": DEVELOPMENT}, MCP_BODY
        )
        check("shipping: MCP refuses the dev origin", status, 403)
        status, _, _ = request(port, "POST", "/mcp", {"Origin": DEPLOYED}, MCP_BODY)
        check("shipping: MCP accepts the deployed origin", status, 200)
    finally:
        stop(proc)


def development():
    port = free_port()
    proc = start(FIXTURES, port, DEVELOPMENT)
    try:
        status, headers, _ = get(port, None)
        check("dev: a request without Origin is served", status, 200)
        check("dev: the reply names the deployed origin",
              headers.get("access-control-allow-origin"), DEPLOYED)
        check("dev: the reply varies by origin", "origin" in headers.get("vary", "").lower(), True)

        status, headers, _ = get(port, DEVELOPMENT)
        check("dev: the dev origin is answered", status, 200)
        check("dev: and the reply names it",
              headers.get("access-control-allow-origin"), DEVELOPMENT)

        status, headers, _ = get(port, DEPLOYED)
        check("dev: the deployed App is still answered", status, 200)
        check("dev: and its reply names its own origin",
              headers.get("access-control-allow-origin"), DEPLOYED)

        status, headers, _ = request(
            port, "OPTIONS", "/api/status",
            {"Origin": DEVELOPMENT, "Access-Control-Request-Method": "GET"},
        )
        check("dev: the preflight is answered", status, 204)
        check("dev: the preflight names the dev origin",
              headers.get("access-control-allow-origin"), DEVELOPMENT)

        status, headers, _ = request(port, "POST", "/mcp", {"Origin": DEVELOPMENT}, MCP_BODY)
        check("dev: MCP accepts the dev origin", status, 200)
        check("dev: the MCP reply names the dev origin",
              headers.get("access-control-allow-origin"), DEVELOPMENT)
        status, _, _ = request(port, "POST", "/mcp", {"Origin": DEPLOYED}, MCP_BODY)
        check("dev: MCP still accepts the deployed origin", status, 200)
        status, _, _ = request(port, "POST", "/mcp", {"Origin": EVIL}, MCP_BODY)
        check("dev: MCP refuses any other origin", status, 403)
    finally:
        stop(proc)


def named():
    """The guard's accepted name is the deck's one name (HA-03). The server starts with the
    name moved at the seam, and the origin it accepts moves with it: the old name is no
    longer the deck's own, so it is refused like any other."""
    env = dict(os.environ, K7_HOSTNAME="kitchen")
    port = free_port()
    proc = start(FIXTURES, port, env=env)
    try:
        # The page is served from the deck's name, so its origin and its Host are that name.
        status, _, _ = request(port, "POST", "/mcp", {"Origin": "http://kitchen.local"},
                               MCP_BODY, host="kitchen.local")
        check("named: MCP accepts the moved name's origin", status, 200)
        status, _, _ = request(port, "POST", "/mcp", {"Origin": "http://k7.local"}, MCP_BODY,
                               host="k7.local")
        check("named: and refuses the name it no longer answers", status, 403)
    finally:
        stop(proc)


def main():
    shipping()
    development()
    named()
    print(f"origins: {'FAILED' if FAIL else 'ok'} ({FAIL} failures)")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
