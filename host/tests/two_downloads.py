#!/usr/bin/env python3
"""T9 helper: a second simultaneous download is declined, and the first is unharmed.

    two_downloads.py <host> <port> <file-name> <local-path>

The first client reads slowly, on purpose, so the server is still inside the
download when the second request arrives. The first download must keep
delivering the file byte for byte; the second must be told 503 rather than queued
behind it.
"""
import os
import socket
import sys
import threading
import time

host, port, name, local_path = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]

first = {}


def slow_reader():
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8192)
    s.connect((host, port))
    s.sendall(("GET /music/%s HTTP/1.0\r\nHost: test\r\n\r\n" % name).encode())
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = s.recv(4096)
        if not chunk:
            break
        data += chunk
    head, body = data.split(b"\r\n\r\n", 1)
    first["status"] = head.split(b"\r\n")[0].decode()
    deadline = time.time() + 3.0
    while time.time() < deadline:
        s.settimeout(0.5)
        try:
            chunk = s.recv(2048)
        except socket.timeout:
            break
        if not chunk:
            break
        body += chunk
        time.sleep(0.05)  # slow, but faster than the server's stall timeout
    first["data"] = body
    s.close()


t = threading.Thread(target=slow_reader)
t.start()
time.sleep(0.6)  # the first download is now mid-flight

s2 = socket.socket()
s2.settimeout(5)
s2.connect((host, port))
s2.sendall(("GET /music/%s HTTP/1.0\r\nHost: test\r\n\r\n" % name).encode())
resp = b""
while b"\r\n" not in resp:
    chunk = s2.recv(4096)
    if not chunk:
        break
    resp += chunk
second = resp.split(b"\r\n")[0].decode() if resp else "NO RESPONSE"
s2.close()
t.join()

size = os.path.getsize(local_path)
expected = open(local_path, "rb").read(len(first["data"]))
problems = []
if not first["status"].startswith("HTTP/1.0 200"):
    problems.append("the first download got %r" % first["status"])
if second != "HTTP/1.0 503 Service Unavailable":
    problems.append("the second download got %r, expected 503" % second)
if first["data"] != expected:
    problems.append("the first download's bytes do not match the file (%d bytes read of %d)"
                    % (len(first["data"]), size))
if problems:
    print("\n".join(problems))
    sys.exit(1)
print("first download intact (%d bytes read of %d), second declined with 503"
      % (len(first["data"]), size))
