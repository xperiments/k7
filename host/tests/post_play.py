#!/usr/bin/env python3
"""Exercise POST /api/play through the real TCP parser.

The interesting cases are wire-level: a body sent in the same packet as the
header, a body sent after it, a missing length, a wrong type and an oversized
body. The route's acceptance is checked against the track names the fixture was
built with.
"""
import json
import socket
import sys
import time

HOST = sys.argv[1]
PORT = int(sys.argv[2])


def exchange(header, body=b"", split=False):
    s = socket.create_connection((HOST, PORT), timeout=5)
    s.sendall(header)
    if split:
        time.sleep(0.15)
    if body:
        s.sendall(body)
    data = b""
    while True:
        chunk = s.recv(4096)
        if not chunk:
            break
        data += chunk
    s.close()
    head, _, payload = data.partition(b"\r\n\r\n")
    lines = head.decode("latin1").split("\r\n")
    status = int(lines[0].split()[1])
    headers = {}
    for line in lines[1:]:
        k, _, v = line.partition(":")
        headers[k.lower()] = v.strip()
    return status, headers, payload


def chunked(header, chunks):
    s = socket.create_connection((HOST, PORT), timeout=5)
    s.sendall(header)
    for chunk in chunks:
        time.sleep(0.15)
        s.sendall(chunk)
    data = b""
    while True:
        part = s.recv(4096)
        if not part:
            break
        data += part
    s.close()
    head, _, payload = data.partition(b"\r\n\r\n")
    return int(head.decode("latin1").split()[1]), payload


def check(label, got, want):
    if got != want:
        print(f"FAIL {label}: got {got!r}, want {want!r}")
        return 1
    print(f"  ok    {label}")
    return 0


def main():
    bad = 0
    path = "two.wav"
    body = json.dumps({"path": path}).encode()

    # Header and body in one recv: the bytes after CRLFCRLF must be preserved.
    header = (b"POST /api/play HTTP/1.0\r\nContent-Type: application/json\r\n"
              b"Content-Length: " + str(len(body)).encode() + b"\r\n\r\n")
    status, headers, payload = exchange(header + body)
    bad += check("one-packet header+body is accepted", status, 200)
    bad += check("one-packet response is JSON", headers.get("content-type"), "application/json")
    bad += check("one-packet response says ok", payload, b'{"ok":true}')

    # Header first, body after: the server must wait for the declared length.
    status, _headers, payload = exchange(header, body, split=True)
    bad += check("split header/body is accepted", status, 200)
    bad += check("split response says ok", payload, b'{"ok":true}')

    # The body straddles the first read: bytes already in the header buffer must
    # be kept and the rest read from the socket.
    status, payload = chunked(header, [body[:4], body[4:]])
    bad += check("a body straddling the header read is accepted", status, 200)
    bad += check("straddled response says ok", payload, b'{"ok":true}')

    # Accept is ignored.
    accept = (b"POST /api/play HTTP/1.0\r\nAccept: text/html\r\n"
              b"Content-Type: application/json\r\nContent-Length: " + str(len(body)).encode() +
              b"\r\n\r\n")
    status, headers, payload = exchange(accept + body)
    bad += check("Accept does not change the result", status, 200)
    bad += check("Accept does not change the response type", headers.get("content-type"),
                 "application/json")

    # Unknown path: an error, but the process stays up.
    nope = b'{"path":"no/such.wav"}'
    header = (b"POST /api/play HTTP/1.0\r\nContent-Type: application/json\r\n"
              b"Content-Length: " + str(len(nope)).encode() + b"\r\n\r\n")
    status, _headers, payload = exchange(header + nope)
    bad += check("an unknown path is a 404", status, 404)
    bad += check("and reports not ok", payload, b'{"ok":false,"error":"no such track"}')

    # Missing Content-Length.
    status, _headers, _payload = exchange(
        b"POST /api/play HTTP/1.0\r\nContent-Type: application/json\r\n\r\n")
    bad += check("a missing Content-Length is 411", status, 411)

    # Wrong media type.
    status, _headers, _payload = exchange(
        b"POST /api/play HTTP/1.0\r\nContent-Type: text/plain\r\nContent-Length: 2\r\n\r\n{}")
    bad += check("a non-JSON body is 415", status, 415)

    # Exactly the cap is accepted; one byte over is 413. The trailing spaces are
    # legal JSON whitespace and make the payload lengths exact.
    exact = json.dumps({"path": "two.wav"}).encode()
    exact += b" " * (8192 - len(exact))
    status, _headers, payload = exchange(
        b"POST /api/play HTTP/1.0\r\nContent-Type: application/json\r\n"
        b"Content-Length: 8192\r\n\r\n" + exact)
    bad += check("8192 bytes is accepted", status, 200)
    bad += check("8192-byte response says ok", payload, b'{"ok":true}')

    over = json.dumps({"path": "two.wav"}).encode()
    over += b" " * (8193 - len(over))
    status, _headers, _payload = exchange(
        b"POST /api/play HTTP/1.0\r\nContent-Type: application/json\r\n"
        b"Content-Length: 8193\r\n\r\n" + over)
    bad += check("8193 bytes is 413", status, 413)

    # Too large with the body in the same packet: the server must drain it before
    # closing, or the client sees RST instead of the 413.
    status, _headers, _payload = exchange(
        b"POST /api/play HTTP/1.0\r\nContent-Type: application/json\r\n"
        b"Content-Length: 9000\r\n\r\n" + b" " * 9000)
    bad += check("an oversized body in the header packet is 413", status, 413)

    # A declared body that never arrives is a 400, not an accepted play.
    status, _payload = chunked(
        b"POST /api/play HTTP/1.0\r\nContent-Type: application/json\r\n"
        b"Content-Length: 100\r\n\r\n", [b'{"path":'])
    bad += check("a short body is 400", status, 400)

    # Malformed JSON is a 400, not an accepted play.
    badjson = b'{"path":42}'
    status, _headers, _payload = exchange(
        b"POST /api/play HTTP/1.0\r\nContent-Type: application/json\r\nContent-Length: " +
        str(len(badjson)).encode() + b"\r\n\r\n" + badjson)
    bad += check("malformed JSON is 400", status, 400)

    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
