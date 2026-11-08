#!/usr/bin/env python3
"""Extract the label faces the LVGL canvas builder embeds as base64.

The builder carries its Google Fonts inline so the web preview and the LVGL
image draw exactly the same glyphs.  This script writes those bytes out to
assets/fonts/ so lv_font_conv can compile them; it only ever reads the HTML.

Run it from the repository root:

    python3 tools/extract_builder_fonts.py

It is idempotent: a face whose bytes are already on disk is reported as up to
date and is not rewritten.
"""

from __future__ import annotations

import base64
import binascii
import hashlib
import os
import re
import sys
from pathlib import Path
from typing import NoReturn

ROOT = Path(__file__).resolve().parent.parent
BUILDER = ROOT / "resources" / "LVGL canvas builder.html"
OUT_DIR = ROOT / "assets" / "fonts"

# (name in the builder's BUILTIN_FONTS array, file under assets/fonts/)
FACES = (
    ("Archivo Narrow 600", "archivo-narrow-600.ttf"),
    ("Barlow Condensed 700", "barlow-condensed-700.ttf"),
    ("IBM Plex Mono 500", "ibm-plex-mono-500.ttf"),
)

MARKER = "const BUILTIN_FONTS="
ENTRY = re.compile(
    r"\{\s*name:\s*'(?P<name>[^']+)'\s*,\s*role:\s*'[^']*'\s*,"
    r"\s*b64:\s*'(?P<b64>[^']+)'\s*\}",
    re.DOTALL,
)
TTF_MAGIC = (b"\x00\x01\x00\x00", b"true", b"OTTO", b"ttcf")


def fail(message: str) -> NoReturn:
    print(f"extract_builder_fonts: {message}", file=sys.stderr)
    raise SystemExit(1)


def builtin_fonts(html: str) -> dict[str, str]:
    start = html.find(MARKER)
    if start == -1:
        fail(f"{BUILDER.relative_to(ROOT)} has no {MARKER} array")
    end = html.find("];", start)
    if end == -1:
        fail(f"{MARKER} in {BUILDER.relative_to(ROOT)} is not terminated")

    faces: dict[str, str] = {}
    for match in ENTRY.finditer(html[start:end]):
        faces[match.group("name")] = match.group("b64")
    if not faces:
        fail(f"{MARKER} in {BUILDER.relative_to(ROOT)} has no entries")
    return faces


def main() -> int:
    if not BUILDER.is_file():
        fail(f"missing builder HTML: {BUILDER}")
    html = BUILDER.read_text(encoding="utf-8")
    embedded = builtin_fonts(html)

    missing = [name for name, _ in FACES if name not in embedded]
    if missing:
        fail(f"builder has no embedded face named: {', '.join(missing)}")

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for name, filename in FACES:
        try:
            data = base64.b64decode("".join(embedded[name].split()), validate=True)
        except (binascii.Error, ValueError) as exc:
            fail(f"{name}: the builder's base64 does not decode ({exc})")
        if not data.startswith(TTF_MAGIC):
            fail(f"{name}: decoded bytes are not a TrueType/OpenType font")

        target = OUT_DIR / filename
        digest = hashlib.sha256(data).hexdigest()
        rel = target.relative_to(ROOT)
        if target.is_file() and target.read_bytes() == data:
            print(f"up to date  {rel}  {len(data)} bytes  sha256 {digest[:16]}")
            continue

        tmp = target.with_name(target.name + ".tmp")
        tmp.write_bytes(data)
        os.replace(tmp, target)
        print(f"wrote       {rel}  {len(data)} bytes  sha256 {digest[:16]}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
