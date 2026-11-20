#!/usr/bin/env python3
"""The art pack over the device's front door, end to end, against the host server.

A pack is validated before anything is written, stored in the `art` partition, read back
at boot and served to the App for its J-cards. On the host the partition is the file
XTAPE_ART names, so the same seam is exercised: a good pack lands byte for byte and
survives a restart, a bad one is refused with its reason and changes nothing, the reset
erases it, and an invalid pack left in the partition at boot reports itself while the
factory stays.

The sheet is the packer's own, built by tools/label_pack.py from assets/k7art/, and the
header is written here independently of the firmware's reader.

Usage: art_tests.py SERVER FIXTURES_DIR
"""
import json
import os
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from pathlib import Path

SERVER = sys.argv[1]
FIXTURES = sys.argv[2]
ROOT = Path(__file__).resolve().parents[2]
FAIL = 0

DEPLOYED = "https://k7.xperiments.dev"
PACK_ID = "neon-2026"
PACK_NAME = "Neon Pack"
PACK_AUTHOR = "Ada"
JCARDS = b'{"v":1,"designs":[]}'


def check(label, got, want):
    global FAIL
    if got != want:
        print(f"FAIL {label}: got {got!r}, want {want!r}")
        FAIL += 1
    else:
        print(f"  ok    {label}")


def free_port():
    import socket

    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


# ------------------------------------------------------------------ the pack


def factory_sheet(tmp):
    """The sheet label_pack.py packs from assets/k7art/, and the vocabulary's font count."""
    rc = subprocess.run(
        [sys.executable, str(ROOT / "tools/label_pack.py"), str(ROOT / "assets/k7art"), str(tmp)],
        capture_output=True,
        text=True,
    )
    if rc.returncode != 0:
        raise SystemExit("label_pack.py failed:\n" + rc.stdout + rc.stderr)
    sheet = (Path(tmp) / "label_sheet.bin").read_bytes()
    vocab = json.loads((ROOT / "assets/labels/vocabulary.json").read_text())
    return sheet, len(vocab["fonts"])


def build_pack(sheet, font_count, design_count=None, sheet_version=4, crc=None,
               total_delta=0, jcards=JCARDS):
    design_count = design_count if design_count is not None else struct.unpack_from("<H", sheet, 24)[0]
    payload = sheet + jcards
    total = 128 + len(payload) + total_delta
    header = struct.pack(
        "<8sHHIIHHHHIIII16s32s32sI",
        b"K7ART\x00\x00\x00",
        1,
        128,
        total,
        zlib.crc32(payload) & 0xFFFFFFFF if crc is None else crc,
        design_count,
        sheet_version,
        font_count,
        0,
        128,
        len(sheet),
        128 + len(sheet),
        len(jcards),
        PACK_ID.encode(),
        PACK_NAME.encode(),
        PACK_AUTHOR.encode(),
        0,
    )
    assert len(header) == 128
    return header + payload


def with_sheet_version(pack, sheet, version):
    """The pack with the sheet's version changed, and both CRCs recomputed."""
    body = bytearray(sheet)
    struct.pack_into("<H", body, 8, version)
    struct.pack_into("<I", body, 16, zlib.crc32(bytes(body[32:])) & 0xFFFFFFFF)
    jcards = pack[128 + len(sheet):]
    payload = bytes(body) + jcards
    out = bytearray(pack)
    out[128:128 + len(sheet)] = body
    struct.pack_into("<I", out, 16, zlib.crc32(payload) & 0xFFFFFFFF)
    return bytes(out)


# ------------------------------------------------------------------ HTTP


def request(port, method, path, headers=None, body=None):
    import socket

    headers = dict(headers or {})
    head = f"{method} {path} HTTP/1.0\r\nHost: 127.0.0.1:{port}\r\n"
    if body is not None:
        headers.setdefault("Content-Type", "application/octet-stream")
        head += f"Content-Length: {len(body)}\r\n"
    for name, value in headers.items():
        head += f"{name}: {value}\r\n"
    head += "\r\n"
    s = socket.create_connection(("127.0.0.1", port), timeout=20)
    try:
        s.sendall(head.encode())
    except OSError:
        pass
    if body:
        try:
            s.sendall(body)
        except OSError:
            pass
    data = b""
    while True:
        try:
            part = s.recv(65536)
        except OSError:
            break
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


def status(port, origin=None):
    headers = {"Origin": origin} if origin else {}
    code, reply, body = request(port, "GET", "/api/art", headers)
    return code, reply, body


def start(music, port, art, env_extra=None):
    env = dict(os.environ)
    env["XTAPE_ART"] = art
    if env_extra:
        env.update(env_extra)
    proc = subprocess.Popen(
        [SERVER, "--music-dir", music, "--port", str(port)],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    deadline = time.time() + 15
    import socket

    while time.time() < deadline:
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=0.5)
            s.sendall(b"GET /api/library HTTP/1.0\r\nHost: t\r\n\r\n")
            s.recv(64)
            s.close()
            return proc
        except OSError:
            time.sleep(0.1)
    proc.kill()
    raise SystemExit("the server did not come up:\n" + proc.stdout.read().decode(errors="replace"))


def stop(proc):
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()


def expect_refused(port, pack, needle):
    code, reply, body = request(
        port, "POST", "/api/art", {"Origin": DEPLOYED}, pack
    )
    check(f"a bad pack is a 422 ({needle})", code, 422)
    doc = json.loads(body)
    check(f"the refusal names the reason ({needle})", needle in doc.get("error", ""), True)
    check("the refusal says it did nothing", doc.get("ok"), False)


# ------------------------------------------------------------------ the run


def main():
    sheet, fonts = factory_sheet(tempfile.mkdtemp(prefix="xtape-pack-"))
    designs = struct.unpack_from("<H", sheet, 24)[0]
    pack = build_pack(sheet, fonts)
    sheet_crc = zlib.crc32(sheet[32:]) & 0xFFFFFFFF
    art = os.path.join(tempfile.mkdtemp(prefix="xtape-art-"), "art.bin")
    port = free_port()
    proc = start(FIXTURES, port, art)
    try:
        # Nothing installed: the factory alone, and the status says so without a reason.
        code, reply, body = status(port, DEPLOYED)
        check("the art status is a 200", code, 200)
        check("it is json", reply.get("content-type"), "application/json")
        check("it names the App's origin", reply.get("access-control-allow-origin"), DEPLOYED)
        doc = json.loads(body)
        check("with no pack nothing is installed", doc.get("installed"), False)
        check("and nothing is invalid", doc.get("valid"), True)
        check("there is no name", doc.get("name"), "")
        check("no author", doc.get("author"), "")
        check("no designs", doc.get("design_count"), 0)
        check("no sheet CRC", doc.get("sheet_crc"), 0)
        check("the factory count is the screen's (the host has no screen)", doc.get("factory_count"), 0)
        check("and no reason", doc.get("why"), "")

        code, _, _ = request(port, "GET", "/api/art/pack")
        check("with no pack the bytes are a 404", code, 404)
        for path in ("/api/art/packx", "/api/art/"):
            code, _, _ = request(port, "GET", path)
            check(f"{path} is a 404", code, 404)
        code, _, _ = request(port, "POST", "/api/art/pack", {}, pack)
        check("POST /api/art/pack is a 404", code, 404)

        # The App's preflight: the same CORS gate the rest of /api/ has.
        code, reply, _ = request(
            port,
            "OPTIONS",
            "/api/art",
            {
                "Origin": DEPLOYED,
                "Access-Control-Request-Method": "POST",
                "Access-Control-Request-Private-Network": "true",
            },
        )
        check("the art preflight is answered", code, 204)
        check("and names the App's origin", reply.get("access-control-allow-origin"), DEPLOYED)
        check("and allows the methods it needs",
              "POST" in reply.get("access-control-allow-methods", ""), True)

        # Install.
        code, reply, body = request(port, "POST", "/api/art", {"Origin": DEPLOYED}, pack)
        check("a valid pack is a 200", code, 200)
        check("the upload reply names the App's origin",
              reply.get("access-control-allow-origin"), DEPLOYED)
        doc = json.loads(body)
        check("the new status is installed", doc.get("installed"), True)
        check("and valid", doc.get("valid"), True)
        check("and names the pack", doc.get("name"), PACK_NAME)
        check("and its author", doc.get("author"), PACK_AUTHOR)
        check("and its design count", doc.get("design_count"), designs)
        check("and the sheet's CRC", doc.get("sheet_crc"), sheet_crc)
        check("with no reason", doc.get("why"), "")
        check("the partition holds the pack byte for byte",
              Path(art).read_bytes() == pack, True)

        # The App's J-cards: the pack's own bytes, as it was uploaded.
        code, reply, body = request(port, "GET", "/api/art/pack", {"Origin": DEPLOYED})
        check("the pack is served", code, 200)
        check("as bytes", reply.get("content-type"), "application/octet-stream")
        check("with its length", int(reply.get("content-length", "-1")), len(pack))
        check("and the same bytes", body == pack, True)
        check("with the App's origin", reply.get("access-control-allow-origin"), DEPLOYED)

        # A boot re-opens it: the partition, not the request's memory, is the record.
        stop(proc)
        proc = start(FIXTURES, port, art)
        code, _, body = status(port)
        check("the pack survives a restart", json.loads(body).get("installed"), True)
        code, _, body = request(port, "GET", "/api/art/pack")
        check("and its bytes are still served", code == 200 and body == pack, True)

        # Every refusal leaves the old pack alone.
        before = Path(art).read_bytes()
        expect_refused(port, pack[:-1], "shorter than its total size")
        expect_refused(port, build_pack(sheet, fonts, crc=0x12345678), "checksum disagrees")
        expect_refused(port, build_pack(sheet, fonts, design_count=2), "design_count disagrees")
        expect_refused(port, pack + b"\x00", "bytes past its total size")
        expect_refused(port, with_sheet_version(pack, sheet, 3), "not sheet version 4")
        expect_refused(port, build_pack(sheet, fonts, total_delta=64), "shorter than its total size")
        check("a refused pack left the partition alone", Path(art).read_bytes() == before, True)
        code, _, body = status(port)
        check("and the active pack is untouched", json.loads(body).get("name"), PACK_NAME)

        # The route's type and size rules.
        code, _, _ = request(
            port, "POST", "/api/art", {"Content-Type": "application/json"}, pack
        )
        check("the wrong content type is a 415", code, 415)
        code, _, _ = request(port, "POST", "/api/art", {}, b"\x00" * (1024 * 1024 + 1))
        check("a body over 1 MB is a 413", code, 413)
        check("and it changed nothing", Path(art).read_bytes() == before, True)

        # Reset to factory.
        code, reply, body = request(port, "DELETE", "/api/art", {"Origin": DEPLOYED})
        check("the reset is a 200", code, 200)
        check("and names the App's origin", reply.get("access-control-allow-origin"), DEPLOYED)
        doc = json.loads(body)
        check("the factory is what is left", doc.get("installed"), False)
        check("with nothing invalid", doc.get("valid"), True)
        check("and nothing left on disk", os.path.exists(art), False)
        code, _, _ = request(port, "GET", "/api/art/pack")
        check("and no bytes to serve", code, 404)
        code, _, _ = request(port, "DELETE", "/api/art")
        check("a second reset is still a 200", code, 200)
    finally:
        stop(proc)

    # A pack whose CRC does not match, left in the partition (a power cut mid-upload, or a
    # hand-written file): the boot reports it by name and draws the factory.
    bad = bytearray(pack)
    bad[-1] ^= 0xFF
    Path(art).write_bytes(bytes(bad))
    proc = start(FIXTURES, port, art)
    try:
        code, _, body = status(port)
        check("an invalid pack is still a 200", code, 200)
        doc = json.loads(body)
        check("the boot says a pack is installed", doc.get("installed"), True)
        check("and that it is invalid", doc.get("valid"), False)
        check("with the reason", "checksum" in doc.get("why", ""), True)
        check("with no designs to draw", doc.get("design_count"), 0)
        code, _, _ = request(port, "GET", "/api/art/pack")
        check("an invalid pack is not served", code, 404)

        # A valid upload replaces it.
        code, _, body = request(port, "POST", "/api/art", {}, pack)
        check("a valid upload replaces an invalid pack", code, 200)
        check("and is what the status names now", json.loads(body).get("valid"), True)
    finally:
        stop(proc)

    print(f"art: {'FAILED' if FAIL else 'ok'} ({FAIL} failures)")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
