#!/usr/bin/env python3
"""Turn a captured serial console log into the screen it contains.

    python3 tools/decode_screen_dump.py /tmp/dump.log -o /tmp/screen.png [--scale 0.5]

The firmware writes the framebuffer as one line per row:

    UIROW <row> <base64 of that row's RGB565 pixels>

when it is built with CONFIG_APP_UI_SERIAL_DUMP. Row indices are in the line, so log
output landing between rows cannot shift the image; incomplete rows are reported rather
than silently painted black.

Requires Pillow for the PNG (`pip install pillow`). Without it the raw pixels are
written next to the output path as a .ppm, which every image viewer can open.
"""
import argparse
import base64
import re
import sys

ROW = re.compile(rb"UIROW (\d+) (\d+) ([A-Za-z0-9+/=]+)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("-o", "--out", default="screen.png")
    ap.add_argument("--width", type=int, default=800)
    ap.add_argument("--height", type=int, default=480)
    ap.add_argument("--scale", type=float, default=1.0, help="shrink the saved image")
    args = ap.parse_args()

    # Rows arrive in chunks, and a chunk can be lost; reassemble what is there and say
    # plainly what is not, rather than painting it black and calling it a screen.
    chunks = {}
    for m in ROW.finditer(open(args.log, "rb").read()):
        row, chunk, data = int(m.group(1)), int(m.group(2)), m.group(3)
        chunks.setdefault(row, {})[chunk] = data

    rows, corrupt = {}, []
    for row, parts in chunks.items():
        blob = b"".join(parts[k] for k in sorted(parts))
        try:
            data = base64.b64decode(blob, validate=True)
        except Exception:
            corrupt.append(row)
            continue
        if row < args.height:
            rows[row] = data

    if not rows:
        sys.exit("no usable UIROW lines: was the build made with CONFIG_APP_UI_SERIAL_DUMP?")
    missing = [y for y in range(args.height) if y not in rows]
    short = [y for y, data in rows.items() if len(data) != args.width * 2]
    print(f"{len(rows)} of {args.height} rows usable, {len(missing)} missing, "
          f"{len(short)} short, {len(corrupt)} corrupt")
    for label, bad in (("missing", missing), ("short", short), ("corrupt", corrupt)):
        if bad:
            print(f"  first {label} rows: {bad[:8]}")

    # A row that never arrived, or arrived short, is painted a colour nothing else
    # uses: a dump with a hole in it should look like a dump with a hole in it.
    HOLE = (255, 0, 255)

    try:
        from PIL import Image
    except ImportError:
        path = args.out.rsplit(".", 1)[0] + ".ppm"
        with open(path, "wb") as f:
            f.write(b"P6\n%d %d\n255\n" % (args.width, args.height))
            for y in range(args.height):
                data = (rows.get(y) or b"").ljust(args.width * 2, b"\0")
                for x in range(args.width):
                    v = data[2 * x] | (data[2 * x + 1] << 8)
                    f.write(bytes((((v >> 11) & 31) * 255 // 31,
                                   ((v >> 5) & 63) * 255 // 63,
                                   (v & 31) * 255 // 31)))
        print(f"wrote {path} (no Pillow, so PPM)")
        return

    im = Image.new("RGB", (args.width, args.height))
    px = im.load()
    for y in range(args.height):
        data = rows.get(y)
        if data is None:
            for x in range(args.width):
                px[x, y] = HOLE
            continue
        data = data.ljust(args.width * 2, b"\0")
        for x in range(args.width):
            v = data[2 * x] | (data[2 * x + 1] << 8)
            px[x, y] = (((v >> 11) & 31) * 255 // 31,
                        ((v >> 5) & 63) * 255 // 63,
                        (v & 31) * 255 // 31)
    if args.scale != 1.0:
        im = im.resize((int(args.width * args.scale), int(args.height * args.scale)), Image.LANCZOS)
    im.save(args.out)
    print(f"wrote {args.out} ({im.size[0]}x{im.size[1]})")


if __name__ == "__main__":
    main()
