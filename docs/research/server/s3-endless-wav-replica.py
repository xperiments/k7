#!/usr/bin/env python3
"""Replica of xtape's mini_icecast wire output, for client-compatibility testing.

Bit-exact to resources/mini_icecast.c:
  - status line "HTTP/1.0 200 OK"
  - Content-Type: audio/wav, icy-name, Cache-Control: no-cache, Connection: close
  - optional icy-metaint: N when the request sends "Icy-MetaData: 1"
  - 44-byte WAV header with RIFF size = data size = 0xFFFFFFFF
  - endless PCM (440 Hz sine, stereo 16-bit 44100) with ICY metadata blocks
    injected exactly every metaint bytes of AUDIO (send_meta()/client_push()).
Usage: fake_server.py [port] [metaint] [--icy-title NAME]
"""
import socket, struct, sys, threading, math, time

port = int(sys.argv[1]) if len(sys.argv) > 1 else 8731
metaint = int(sys.argv[2]) if len(sys.argv) > 2 else 16000
HZ = 44100
CHUNK_FRAMES = 1024


def wav_header():
    unknown = 0xFFFFFFFF
    byte_rate = HZ * 4
    return (b"RIFF" + struct.pack("<I", unknown) + b"WAVE"
            + b"fmt " + struct.pack("<IHHIIHH", 16, 1, 2, HZ, byte_rate, 4, 16)
            + b"data" + struct.pack("<I", unknown))


def pcm_chunk(phase):
    """CHUNK_FRAMES stereo int16 frames of a 440Hz sine."""
    out = bytearray()
    for i in range(CHUNK_FRAMES):
        v = int(12000 * math.sin(2 * math.pi * 440 * (phase + i) / HZ))
        out += struct.pack("<hh", v, v)
    return bytes(out)


def meta_block(title):
    if title is None:
        return b"\x00"                      # send_meta(): no title -> single 0 byte
    tmp = ("StreamTitle='%s';" % title).encode()
    blocks = (len(tmp) + 15) // 16
    return bytes([blocks]) + tmp + b"\x00" * (blocks * 16 - len(tmp))


def handle(conn):
    req = b""
    while b"\r\n\r\n" not in req and len(req) < 4096:
        d = conn.recv(1024)
        if not d:
            return
        req += d
    meta = b"icy-metadata: 1" in req.lower()
    h = ("HTTP/1.0 200 OK\r\nContent-Type: audio/wav\r\n"
         "icy-name: Fake ESP32 Radio\r\nCache-Control: no-cache\r\nConnection: close\r\n")
    if meta:
        h += "icy-metaint: %d\r\n" % metaint
    h += "\r\n"
    conn.sendall(h.encode())
    conn.sendall(wav_header())
    since = 0
    phase = 0
    title = "Track One" if meta else None
    try:
        while True:
            data = pcm_chunk(phase)
            phase += CHUNK_FRAMES
            if not meta:
                conn.sendall(data)
                continue
            # client_push(): split so a metadata block lands every metaint bytes
            off = 0
            while off < len(data):
                k = min(metaint - since, len(data) - off)
                conn.sendall(data[off:off + k])
                off += k
                since += k
                if since == metaint:
                    since = 0
                    conn.sendall(meta_block(title))
            time.sleep(CHUNK_FRAMES / HZ)   # pacing: real-time rate
    except (BrokenPipeError, ConnectionResetError):
        pass
    finally:
        conn.close()


def main():
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", port))
    s.listen(8)
    print("listening on 127.0.0.1:%d metaint=%d" % (port, metaint), flush=True)
    while True:
        c, _ = s.accept()
        threading.Thread(target=handle, args=(c,), daemon=True).start()


main()
