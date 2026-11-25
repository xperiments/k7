#!/usr/bin/env python3
"""Publish one board's release: the parts the Site serves and the manifest that describes them.

Both boards are published by this one script, from the build's own `flasher_args.json`, so the
offsets, the flash parameters and the app's file name cannot drift from the binaries that were
built (k7-site.md F9). The guard that makes a release a *release* — a clean tree, a tag at HEAD
— is the caller's (`tools/build.sh` and `player/build.sh` both check it before they call this):
this script is the mechanism, the action is the rule (ADR 0079).

The K7Player's image is signed here (ADR 0116): the player's release wraps `k7player.bin` in the
128-byte header of player-update.md U1 and signs its SHA-256 with the project's key, and the K7's
release verifies that image and carries it as its fourth part. That is why the player's release
runs first (`npm run release`).

    ./player/build.sh release              # the K7Player, and the image it signs
    ./tools/build.sh sunton release        # the K7, carrying that image

Nothing is committed: the manifest and the parts are reviewed with the change that made them.
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile

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
        # The two-slot table brings otadata, and IDF writes its initial image with the flash
        # so a fresh player boots ota_0 (ADR 0117).
        "offsets": {"0x1000", "0x8000", "0x390000", "0x10000"},
        "chip": "esp32",
    },
}

# The player's signed image (player-update.md U1-U3, ADR 0116): the header's shape, the K7's
# partition, and the one file both releases agree on.
PLAYER_IMAGE_NAME = "k7player.img"
PLAYER_HEADER_SIZE = 128
PLAYER_HEADER_VERSION = 1
PLAYER_BOARD_ESP32 = 1
PLAYER_SIGNATURE_MAX = 72
PLAYER_IMAGE_OFFSET = 0x710000
PLAYER_PARTITION_SIZE = 2 * 1024 * 1024
PLAYER_IMAGE_MAX = PLAYER_PARTITION_SIZE - PLAYER_HEADER_SIZE
PLAYER_PUBLIC_KEY = "player/release-key.pub.pem"
PLAYER_KEY_ENV = "K7_RELEASE_KEY"
PLAYER_KEY_DEFAULT = "~/.config/k7/release-key.pem"


def die(message: str) -> None:
    sys.exit(f"release: {message}")


def git(*args: str) -> str:
    return subprocess.run(
        ("git", "-C", ROOT, *args), check=True, capture_output=True, text=True
    ).stdout.strip()


# --------------------------------------------------------------- the signed image


def player_key_path() -> str:
    return os.path.expanduser(os.environ.get(PLAYER_KEY_ENV, PLAYER_KEY_DEFAULT))


def sign_image(image: bytes, key: str) -> bytes:
    """ECDSA P-256 over SHA-256(image), DER, as `openssl dgst` and mbedTLS both speak it."""
    try:
        done = subprocess.run(
            ("openssl", "dgst", "-sha256", "-sign", key, "-binary"),
            input=image,
            check=True,
            capture_output=True,
        )
    except FileNotFoundError:
        die("openssl is not on PATH; it signs the player's image (ADR 0116)")
    except subprocess.CalledProcessError as error:
        die(f"openssl could not sign with {key}: {error.stderr.decode().strip()}")
    signature = done.stdout
    if not 1 <= len(signature) <= PLAYER_SIGNATURE_MAX:
        die(f"the signature is {len(signature)} B, the header's field is {PLAYER_SIGNATURE_MAX}")
    return signature


def signed_player_image(image: bytes, version: str, key: str) -> bytes:
    """The image behind the 128-byte header of player-update.md U1."""
    if not image:
        die("the player's image is empty")
    if len(image) > PLAYER_IMAGE_MAX:
        die(f"the player's image is {len(image)} B, the partition holds {PLAYER_IMAGE_MAX}")
    major, minor, patch = (int(part) for part in version.split("."))
    for number in (major, minor, patch):
        if not 0 <= number <= 0xFFFF:
            die(f"version {version} does not fit the header's three u16 fields")

    signature = sign_image(image, key)
    header = bytearray(PLAYER_HEADER_SIZE)
    header[0:4] = b"K7PU"
    header[4] = PLAYER_HEADER_VERSION
    header[5] = PLAYER_BOARD_ESP32
    header[6] = len(signature)
    header[7] = 0
    header[8:12] = struct.pack(">I", len(image))
    header[12:18] = struct.pack(">HHH", major, minor, patch)
    header[18:20] = b"\0\0"
    header[20:52] = hashlib.sha256(image).digest()
    header[52 : 52 + len(signature)] = signature
    header[124:128] = b"\0\0\0\0"
    return bytes(header) + image


def check_player_image(blob: bytes, version: str, label: str) -> None:
    """Refuse an image the players would refuse: the header's own rules, its hash, its signature.

    The K7's release runs this before it copies the image, so a release cannot carry a blob that
    was signed with another key, edited after signing, or built from another version (U1, U18).
    """
    if len(blob) < PLAYER_HEADER_SIZE:
        die(f"{label}: shorter than the 128-byte header")
    if blob[0:4] != b"K7PU":
        die(f"{label}: the magic is not K7PU")
    if blob[4] != PLAYER_HEADER_VERSION:
        die(f"{label}: header version {blob[4]}, expected {PLAYER_HEADER_VERSION}")
    if blob[5] != PLAYER_BOARD_ESP32:
        die(f"{label}: board {blob[5]}, expected the classic ESP32")
    signature_length = blob[6]
    if not 1 <= signature_length <= PLAYER_SIGNATURE_MAX:
        die(f"{label}: signature length {signature_length}")
    if blob[7] != 0 or blob[18] != 0 or blob[19] != 0 or any(blob[124:128]):
        die(f"{label}: a reserved byte is not zero")
    image_length = struct.unpack(">I", blob[8:12])[0]
    if image_length + PLAYER_HEADER_SIZE != len(blob):
        die(
            f"{label}: the header says {image_length} B of image and the file is "
            f"{len(blob) - PLAYER_HEADER_SIZE} B"
        )
    if image_length > PLAYER_IMAGE_MAX:
        die(f"{label}: {image_length} B does not fit the partition")
    header_version = ".".join(str(part) for part in struct.unpack(">HHH", blob[12:18]))
    if header_version != version:
        die(f"{label}: the header says version {header_version}, the release is {version}")
    image = blob[PLAYER_HEADER_SIZE:]
    if hashlib.sha256(image).hexdigest() != blob[20:52].hex():
        die(f"{label}: the SHA-256 in the header does not match the image")

    public_key = os.path.join(ROOT, PLAYER_PUBLIC_KEY)
    if not os.path.isfile(public_key):
        die(f"missing {PLAYER_PUBLIC_KEY}; the player's firmware trusts that key (ADR 0116)")
    with tempfile.NamedTemporaryFile(suffix=".der") as handle:
        handle.write(blob[52 : 52 + signature_length])
        handle.flush()
        done = subprocess.run(
            ("openssl", "dgst", "-sha256", "-verify", public_key, "-signature", handle.name),
            input=image,
            capture_output=True,
        )
    if done.returncode != 0:
        die(
            f"{label}: the signature does not verify with {PLAYER_PUBLIC_KEY}\n"
            f"         {done.stdout.decode().strip()} {done.stderr.decode().strip()}"
        )


def check_player_partition(partition_table_path: str) -> None:
    """The K7's built table must actually carry the partition the fourth part is written to."""
    with open(partition_table_path, "rb") as handle:
        table = handle.read()
    for offset in range(0, len(table), 32):
        entry = table[offset : offset + 32]
        if entry[:2] != b"\xaa\x50":
            break
        _, _, address, size = struct.unpack("<BBII", entry[2:12])
        if address == PLAYER_IMAGE_OFFSET:
            if size != PLAYER_PARTITION_SIZE:
                die(
                    f"the player partition is {size} B, the release expects "
                    f"{PLAYER_PARTITION_SIZE} B"
                )
            return
    die(
        f"the K7's partition table has no partition at {PLAYER_IMAGE_OFFSET:#x}; "
        f"add it to partitions.csv (ADR 0116)"
    )


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

    if board == "player":
        key = player_key_path()
        if not os.path.isfile(key):
            die(
                f"no release key at {key}; the player's image is signed before it is "
                f"published (ADR 0116), and a release without it is refused"
            )
        app = next(part for part in parts if part["offset"] == 0x10000)
        with open(os.path.join(dest, app["file"]), "rb") as handle:
            image = handle.read()
        blob = signed_player_image(image, version, key)
        with open(os.path.join(dest, PLAYER_IMAGE_NAME), "wb") as handle:
            handle.write(blob)
        check_player_image(blob, version, PLAYER_IMAGE_NAME)
        manifest["image"] = {
            "file": PLAYER_IMAGE_NAME,
            "size": len(blob),
            "sha256": hashlib.sha256(blob).hexdigest(),
        }
        print(f"release: signed {PLAYER_IMAGE_NAME} with {key}")

    if board == "k7":
        check_player_partition(os.path.join(build, "partition_table", "partition-table.bin"))
        source = os.path.join(ROOT, "web", "public", "firmware", "player", version,
                              PLAYER_IMAGE_NAME)
        if not os.path.isfile(source):
            die(
                f"no player image at {os.path.relpath(source, ROOT)}; publish the player's "
                f"release for {version} first (k7-site.md R2)"
            )
        with open(source, "rb") as handle:
            blob = handle.read()
        check_player_image(blob, version, "the player's image")
        shutil.copyfile(source, os.path.join(dest, PLAYER_IMAGE_NAME))
        parts.append(
            {
                "file": PLAYER_IMAGE_NAME,
                "offset": PLAYER_IMAGE_OFFSET,
                "size": len(blob),
                "sha256": hashlib.sha256(blob).hexdigest(),
            }
        )
        parts.sort(key=lambda part: part["offset"])
        print(f"release: carries {PLAYER_IMAGE_NAME} at {PLAYER_IMAGE_OFFSET:#x}")

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
