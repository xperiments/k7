#!/usr/bin/env python3
"""The setup page and its API, end to end, against the host server.

ADR 0076: the device serves exactly one page, at /setup, on every interface, and `/` still
redirects to the App. The page's five routes are the contract wifi-provisioning.md §4
writes down; this drives them the way the page's own script does, with the radio faked by
the environment (see host/board_host.c).

Usage: setup_tests.py SERVER FIXTURES_DIR
"""
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

SERVER = sys.argv[1]
FIXTURES = sys.argv[2]
FAIL = 0


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


def request(port, method, path, body=None, content_type="application/json"):
    s = socket.create_connection(("127.0.0.1", port), timeout=30)
    payload = b"" if body is None else (body.encode() if isinstance(body, str) else json.dumps(body).encode())
    head = f"{method} {path} HTTP/1.0\r\nHost: 127.0.0.1:{port}\r\n"
    if payload:
        head += f"Content-Type: {content_type}\r\nContent-Length: {len(payload)}\r\n"
    s.sendall((head + "\r\n").encode() + payload)
    data = b""
    while True:
        part = s.recv(65536)
        if not part:
            break
        data += part
    s.close()
    raw_head, _, raw_body = data.partition(b"\r\n\r\n")
    lines = raw_head.decode("latin1").split("\r\n")
    status = int(lines[0].split(" ")[1]) if lines and lines[0] else 0
    headers = {}
    for line in lines[1:]:
        name, _, value = line.partition(":")
        headers[name.strip().lower()] = value.strip()
    return status, headers, raw_body.decode("utf-8", "replace")


def get(port, path):
    return request(port, "GET", path)


def start(port, env_extra):
    env = dict(os.environ)
    env.update(env_extra)
    proc = subprocess.Popen(
        [SERVER, "--music-dir", FIXTURES, "--port", str(port)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env,
    )
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
    raise SystemExit("the server did not come up:\n" + proc.stdout.read().decode(errors="replace"))


def stop(proc):
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()


def main():
    tmp = tempfile.mkdtemp(prefix="k7setup-")
    record = os.path.join(tmp, "wifi.bin")
    ap_state = os.path.join(tmp, "ap.txt")
    scan = os.path.join(tmp, "scan.txt")
    with open(scan, "w") as f:
        # Deliberately out of order and with a weak first entry: the list is strongest-first.
        f.write("Cafe Guest|-66|0\nxperiments-2.4G|-41|1\nneighbour-5g|-80|1\n")
    env = {"XTAPE_HOST_WIFI": record, "XTAPE_HOST_AP_STATE": ap_state,
           "XTAPE_HOST_WIFI_SCAN": scan, "XTAPE_HOST_WIFI_JOIN": "ok"}

    port = free_port()
    proc = start(port, env)
    try:
        # --- the page, and the root it must not shadow
        status, headers, body = get(port, "/setup")
        check("/setup is a 200", status, 200)
        check("/setup is HTML", headers.get("content-type"), "text/html; charset=utf-8")
        for needle in ("Networks", "Another network", "2.4 GHz", "id='join'",
                       "The hotspot"):
            check(f"the page carries {needle!r}", needle in body, True)

        # The page's script is 6 KB of JavaScript living inside a C string, which no compiler
        # checks: a stray newline inside a quoted string is invisible until a browser refuses
        # the whole file. node --check reads it as JavaScript, so this catches that class.
        script = body.split("<script>", 1)[-1].split("</script>", 1)[0]
        node = shutil.which("node")
        if node:
            with open(os.path.join(tmp, "page.js"), "w") as f:
                f.write(script)
            done = subprocess.run([node, "--check", os.path.join(tmp, "page.js")],
                                  capture_output=True, text=True)
            check("the page's script parses as JavaScript", done.returncode, 0)
            if done.returncode:
                print(done.stderr)
        else:
            print("  note  no node: the page's script was not syntax-checked")
        status, headers, _ = get(port, "/")
        check("/ still redirects to the App", status, 302)
        check("/ names the App", headers.get("location"),
              "https://k7.xperiments.dev/#/player")

        # --- the state, before anything is chosen
        status, _, body = get(port, "/api/setup")
        st = json.loads(body)
        check("the state is a 200", status, 200)
        check("an empty record is unprovisioned", st["state"], "unprovisioned")
        check("and names no network", st["network"], None)
        check("the hotspot is up", st["hotspot"]["up"], True)
        check("the hotspot is K7.local", st["hotspot"]["ssid"], "K7.local")
        check("at 192.168.4.1", st["hotspot"]["address"], "192.168.4.1")
        check("nothing has been attempted", st["last_attempt"], None)
        check("no grace is running", st["closes_in_ms"], 0)
        check("no password is ever returned", "viernes13" in body or "12345678" in body, False)

        # --- the scan
        status, _, body = get(port, "/api/setup/scan")
        nets = json.loads(body)["networks"]
        check("the scan is a 200", status, 200)
        check("one row per network, strongest first",
              [n["ssid"] for n in nets],
              ["xperiments-2.4G", "Cafe Guest", "neighbour-5g"])
        check("an open network is flagged", [n["secure"] for n in nets], [True, False, True])
        check("a row carries its signal", nets[0]["rssi"], -41)

        # --- the refusals
        status, _, body = request(port, "POST", "/api/setup/join",
                                  {"ssid": "xperiments-2.4G", "password": "1234567"})
        check("a 7-character password is refused", status, 400)
        check("and the answer names the rule", "8 to 63" in body, True)
        status, _, _ = request(port, "POST", "/api/setup/join",
                               {"ssid": "x" * 33, "password": "12345678"})
        check("a 33-byte SSID is refused", status, 400)
        status, _, _ = request(port, "POST", "/api/setup/join",
                               "ssid=not-json", content_type="text/plain")
        check("a body that is not JSON is 415", status, 415)

        # --- a join that works
        status, _, body = request(port, "POST", "/api/setup/join",
                                  {"ssid": "xperiments-2.4G", "password": "viernes13"})
        check("the join is a 200", status, 200)
        check("and reports ok", json.loads(body)["ok"], True)
        check("the record was written", os.path.exists(record), True)
        status, _, body = get(port, "/api/setup")
        st = json.loads(body)
        check("the device now names its network", st["network"], "xperiments-2.4G")
        check("the last attempt is a success", st["last_attempt"]["ok"], True)
        check("a success has no reason text", st["last_attempt"]["why"], "")
        check("the hotspot is counting down", st["closes_in_ms"] > 0, True)

        # --- the setup password
        status, _, body = request(port, "POST", "/api/setup/ap-password", {"password": "short"})
        check("a 5-character hotspot password is refused", status, 400)
        status, _, _ = request(port, "POST", "/api/setup/ap-password", {"password": "newpass99"})
        check("a good one is accepted", status, 204)
        with open(record + ".ap") as f:
            check("and stored", f.read(), "newpass99")
        status, _, body = get(port, "/api/setup")
        check("but never returned", "newpass99" in body, False)

        # --- the hotspot's own switch
        status, _, _ = request(port, "POST", "/api/setup/ap", {"on": "yes"})
        check("a non-boolean on is a 400", status, 400)

        # --- forgetting the network: the record goes, the hotspot's password stays
        status, _, _ = request(port, "POST", "/api/setup/forget", {})
        check("forget is a 204", status, 204)
        check("the record is gone", os.path.exists(record), False)
        status, _, body = get(port, "/api/setup")
        st = json.loads(body)
        check("and the device is unprovisioned again", st["state"], "unprovisioned")
        check("naming no network", st["network"], None)
        check("with the hotspot still up", st["hotspot"]["up"], True)
        with open(record + ".ap") as f:
            check("and the hotspot password untouched", f.read(), "newpass99")
        status, _, _ = request(port, "POST", "/api/setup/ap", {"on": False})
        check("the hotspot can be stopped", status, 204)
        check("and the radio agrees", open(ap_state).read(), "off")

        # ADR 0077: with the hotspot down the whole surface is gone, from every interface.
        # A device sitting on its LAN serves no page that can rewrite its network.
        for path in ("/setup", "/api/setup", "/api/setup/scan"):
            code, _, _ = get(port, path)
            check(f"with the hotspot down {path} is a 404", code, 404)
        code, _, _ = request(port, "POST", "/api/setup/join",
                             {"ssid": "xperiments-2.4G", "password": "viernes13"})
        check("and a join with no hotspot is a 404 too", code, 404)
        code, _, _ = request(port, "POST", "/api/setup/ap", {"on": True})
        check("nor can the LAN raise the hotspot: that is the panel's job", code, 404)


    finally:
        stop(proc)

    # --- one scan at a time (P22): the second request is a 429, not a queued scan
    port = free_port()
    env_slow = dict(env)
    env_slow.update({"XTAPE_HOST_WIFI": os.path.join(tmp, "wifi3.bin"), "XTAPE_HOST_SCAN_MS": "700"})
    proc = start(port, env_slow)
    try:
        import threading
        results = []
        lock = threading.Lock()

        def one_scan():
            code, _, _ = get(port, "/api/setup/scan")
            with lock:
                results.append(code)

        threads = [threading.Thread(target=one_scan) for _ in range(2)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        results.sort()
        check("two scans at once: one runs, one is refused", results, [200, 429])
    finally:
        stop(proc)

    # --- a join that fails, on a device that had no network
    record2 = os.path.join(tmp, "wifi2.bin")
    port = free_port()
    env2 = dict(env)
    env2.update({"XTAPE_HOST_WIFI": record2, "XTAPE_HOST_WIFI_JOIN": "fail:15:5"})
    proc = start(port, env2)
    try:
        status, _, body = request(port, "POST", "/api/setup/join",
                                  {"ssid": "xperiments-2.4G", "password": "wrongpass"})
        answer = json.loads(body)
        check("a failed join is still a 200", status, 200)
        check("and reports the reason", answer["reason"], 15)
        check("in the panel's words", answer["why"], "wrong password")
        check("after five tries", answer["tries"], 5)
        check("the choice was still stored", os.path.exists(record2), True)
        _, _, body = get(port, "/api/setup")
        st = json.loads(body)
        check("the page can report it after a reload", st["last_attempt"]["why"], "wrong password")
        check("and the device says what it will try", st["network"], "xperiments-2.4G")
        check("no grace runs after a failure", st["closes_in_ms"], 0)
    finally:
        stop(proc)

    print(f"setup: {'ok' if FAIL == 0 else str(FAIL) + ' failures'} ({FAIL} failures)")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
