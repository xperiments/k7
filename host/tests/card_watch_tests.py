#!/usr/bin/env python3
"""The card watcher: a card that leaves is not a one-way door.

The board has no card-detect line, so the only evidence a card is there is the volume
answering a directory read, and the only way to see one arrive is to try the mount again.
This drives the real watcher task in the host server through four states, using the host
adapter's scripted card:

  * boot with no card, then insert one - the Library comes back without a restart;
  * one failed probe is a hiccup, not a removal;
  * three in a row (a card that has gone) publishes `no_card`;
  * the card comes back after that, which is the case the old watcher could not do.

The host adapter keeps the board's invariant - a card is present only while it is
mounted - so the shim cannot hide the coupling that broke this on the device.

Usage: card_watch_tests.py SERVER FIXTURES_DIR
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

# The watcher ticks every 2 s, calls a removal after three consecutive failures, and
# retries a mount every 5 s. These deadlines are generous multiples of that.
WAIT = 15


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


def request(port, path):
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.sendall(f"GET {path} HTTP/1.0\r\nHost: 127.0.0.1:{port}\r\n\r\n".encode())
    data = b""
    while True:
        part = s.recv(65536)
        if not part:
            break
        data += part
    s.close()
    head, _, body = data.partition(b"\r\n\r\n")
    lines = head.decode("latin1").split("\r\n")
    status = int(lines[0].split(" ")[1])
    return status, body


def status(port):
    code, body = request(port, "/api/status")
    if code != 200:
        raise AssertionError(f"/api/status said {code}")
    return json.loads(body)


def wait_for(port, label, predicate, timeout=WAIT):
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        last = status(port)
        if predicate(last):
            print(f"  ok    {label}")
            return last
        time.sleep(0.2)
    raise AssertionError(f"{label}: last status {last}")


def write_card(path, word):
    # Whole-file replace: the watcher reads this file from another thread, and a
    # truncate-then-write window would be a torn "out" on the way to "in".
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        fh.write(word + "\n")
    os.replace(tmp, path)


def start(music, port, env):
    proc = subprocess.Popen(
        [SERVER, "--music-dir", music, "--port", str(port)],
        env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    deadline = time.time() + 15
    while time.time() < deadline:
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=0.5)
            s.sendall(b"GET /api/status HTTP/1.0\r\nHost: t\r\n\r\n")
            s.recv(64)
            s.close()
            return proc
        except OSError:
            if proc.poll() is not None:
                break
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
    global FAIL
    root = tempfile.mkdtemp(prefix="k7-card-watch-")
    card = os.path.join(root, "card")
    fail = os.path.join(root, "fail")
    write_card(card, "out")

    env = dict(os.environ)
    env.pop("XTAPE_HOST_NO_CARD", None)  # the script below is this test's card
    env["XTAPE_HOST_CARD_FILE"] = card
    env["XTAPE_HOST_CARD_FAIL_FILE"] = fail

    port = free_port()
    proc = start(FIXTURES, port, env)
    try:
        boot = status(port)
        check("a boot without a card still serves", boot.get("card"), False)
        check("and its Library says no_card", boot.get("library"), "no_card")
        check("with no tracks", boot.get("tracks"), 0)

        # G2: a card that arrives after boot is mounted and scanned, no reboot.
        write_card(card, "in")
        inserted = wait_for(port, "an inserted card comes up",
                            lambda s: s.get("card") is True and s.get("tracks", 0) > 0)
        check("its Library is the card's", inserted.get("library"), "ok")
        tracks = inserted.get("tracks")

        # One failed probe is a card hiccup, not a removal: the debounce keeps the
        # mount and the Library it already has.
        with open(fail, "w", encoding="utf-8"):
            pass
        deadline = time.time() + 6
        while os.path.exists(fail) and time.time() < deadline:
            time.sleep(0.1)
        check("the one-shot probe failure was taken", os.path.exists(fail), False)
        time.sleep(2.5)  # at least one clean probe after it
        still = status(port)
        check("one failed probe did not remove the card", still.get("card"), True)
        check("and the Library was left alone", still.get("tracks"), tracks)

        # A card that has gone: three consecutive failures publish no_card.
        write_card(card, "out")
        gone = wait_for(port, "a removed card is reported",
                        lambda s: s.get("card") is False and s.get("library") == "no_card")
        check("the removed card's Library is empty", gone.get("tracks"), 0)

        # The case the old watcher could not do: the card comes back.
        write_card(card, "in")
        back = wait_for(port, "the card comes back after a removal",
                        lambda s: s.get("card") is True and s.get("tracks", 0) > 0)
        check("and the Library is the card's again", back.get("library"), "ok")
        check("with the same tracks", back.get("tracks"), tracks)
    except AssertionError as e:
        print(f"FAIL {e}")
        FAIL += 1
    finally:
        stop(proc)
        if FAIL:
            print(proc.stdout.read().decode(errors="replace"))
        shutil.rmtree(root, ignore_errors=True)

    print(f"card watch: {'FAILED' if FAIL else 'ok'} ({FAIL} failures)")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
