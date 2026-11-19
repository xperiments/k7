#!/usr/bin/env python3
"""Publish one board's release: the parts the Site serves and the manifest that describes them.

Both boards are published by this one script, from the build's own `flasher_args.json`, so the
offsets, the flash parameters and the app's file name cannot drift from the binaries that were
built (k7-site.md F9). The guard that makes a release a *release* — a clean tree, a tag at HEAD
— is the caller's (`tools/build.sh` and `player/build.sh` both check it before they call this):
this script is the mechanism, the action is the rule (ADR 0079).

    ./tools/build.sh sunton release        # the K7
    ./player/build.sh release              # the K7Player

Nothing is committed: the manifest and the parts are reviewed with the change that made them.
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Per board: where its build wrote the binaries, the offsets the flasher must find (a bootloader
# at 0x0 on the ESP32-S3 and at 0x1000 on the classic ESP32), the chip esptool names it by, and
# where on the Site its parts and its manifest live. The K7 keeps the paths it has always had;
# the player lives under its own directory because the two versions are independent.
BOARDS = {
    "k7": {
        "build": "build/sunton",
        "dest": "web/public/firmware/{version}",
        "manifest": "web/firmware.json",
        "offsets": {"0x0", "0x8000", "0x10000"},
        "chip": "esp32s3",
    },
    "player": {
        "build": "player/build",
        "dest": "web/public/firmware/player/{version}",
        "manifest": "web/firmware-player.json",
        "offsets": {"0x1000", "0x8000", "0x10000"},
        "chip": "esp32",
    },
}


def die(message: str) -> None:
    sys.exit(f"release: {message}")


def git(*args: str) -> str:
    return subprocess.run(
        ("git", "-C", ROOT, *args), check=True, capture_output=True, text=True
    ).stdout.strip()


def publish(board: str, version: str, commit: str, idf: str) -> None:
    board_cfg = BOARDS[board]
    build = os.path.join(ROOT, board_cfg["build"])
    args_path = os.path.join(build, "flasher_args.json")
    if not os.path.isfile(args_path):
        die(f"missing {args_path}; build the {board} firmware first")
    with open(args_path) as handle:
        args = json.load(handle)

    flash = args["flash_files"]
    # The parts are the flasher's, never a list written twice here: a build whose table is not
    # the board's table is refused instead of guessed at.
    if set(flash) != board_cfg["offsets"]:
        die(
            f"{args_path} carries {sorted(flash)}, "
            f"expected {sorted(board_cfg['offsets'])} (k7-site.md F3)"
        )

    dest = os.path.join(ROOT, board_cfg["dest"].format(version=version))
    os.makedirs(dest, exist_ok=True)

    parts = []
    for offset, relative in sorted(flash.items(), key=lambda item: int(item[0], 16)):
        source = os.path.join(build, relative)
        if not os.path.isfile(source):
            die(f"missing {source}")
        name = os.path.basename(relative)
        shutil.copyfile(source, os.path.join(dest, name))
        with open(source, "rb") as handle:
            data = handle.read()
        parts.append(
            {
                "file": name,
                "offset": int(offset, 16),
                "size": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
        )

    settings = args["flash_settings"]
    manifest = {
        "version": version,
        "commit": commit,
        "idf": idf,
        "chip": board_cfg["chip"],
        "flash": {
            "mode": settings["flash_mode"],
            "size": settings["flash_size"],
            "freq": settings["flash_freq"],
        },
        "built_at": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "parts": parts,
    }
    manifest_path = os.path.join(ROOT, board_cfg["manifest"])
    with open(manifest_path, "w") as handle:
        json.dump(manifest, handle, indent=2)
        handle.write("\n")

    print(f"release: {board} {version} ({commit[:12]})")
    for part in parts:
        print(
            f"  {part['offset']:#07x}  {part['size']:>9} B  "
            f"{part['sha256'][:16]}...  {part['file']}"
        )
    print(f"  wrote {os.path.relpath(manifest_path, ROOT)} and {len(parts)} parts in "
          f"{os.path.relpath(dest, ROOT)}/")
    print("  nothing is committed: review and commit them with the change")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--board", required=True, choices=sorted(BOARDS))
    parser.add_argument("--version", help="default: VERSION at the repository root")
    parser.add_argument("--commit", help="default: HEAD")
    parser.add_argument("--idf", help="default: idf.py --version, when the IDF is on PATH")
    args = parser.parse_args()

    version = args.version
    if not version:
        version_path = os.path.join(ROOT, "VERSION")
        if not os.path.isfile(version_path):
            die("no VERSION at the repository root")
        with open(version_path) as handle:
            version = handle.read().strip()
    if not (len(version.split(".")) == 3 and all(part.isdigit() for part in version.split("."))):
        die(f'VERSION is "{version}", not a semver number')

    commit = args.commit or git("rev-parse", "HEAD")
    idf = args.idf
    if not idf:
        try:
            idf = subprocess.run(
                ("idf.py", "--version"), check=True, capture_output=True, text=True
            ).stdout.splitlines()[0]
        except (OSError, subprocess.CalledProcessError, IndexError):
            die("could not read the IDF version; pass --idf or source the IDF first")

    publish(args.board, version, commit, idf)


if __name__ == "__main__":
    main()
