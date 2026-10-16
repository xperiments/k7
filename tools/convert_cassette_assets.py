#!/usr/bin/env python3
"""Turn the cassette artwork into what the firmware can blit, and measure it.

    python3 tools/convert_cassette_assets.py            # writes main/assets/*.bin
    python3 tools/convert_cassette_assets.py --check     # only re-measures, writes nothing

Why this exists rather than LVGL's own LVGLImage.py: that tool needs `pypng`, and the
formats wanted here are plain (RGB565 plus an alpha plane). What matters is that the
conversion is reproducible and that the layout numbers are *measured from the art*
rather than typed in by hand, which is what the geometry half does: the reel holes are
found in the alpha channel, by flood fill, so moving or resizing the artwork cannot
silently leave the wheels sitting in the wrong place.

Output format is LVGL's RGB565A8: every row of RGB565 (little-endian, stride = w * 2)
followed by every row of 8-bit alpha (stride = w). The blobs are embedded into the
firmware with EMBED_FILES and wrapped in an lv_image_dsc_t by main/ui_cassette.c.

Requires Pillow (`pip install pillow`), which the project's documentation lists for
the host tooling.
"""
import argparse
import struct
import sys
from collections import deque

try:
    from PIL import Image
except ImportError:
    sys.exit("needs Pillow: pip install pillow")

FRONT = "assets/cs_front.png"
WHEEL = "assets/cs_wheel.png"
PANEL_W, PANEL_H = 800, 480          # the Sunton 8048S050C's panel
MARGIN = 20                          # breathing room around the cassette
# The cassette is sized so a band is left below it for the file's name, its format and
# the progress bar - the tape is the picture, the band is the information.
ART_MAX_H = 400


def measure_holes(path):
    """The reel windows, found in the alpha channel rather than assumed.

    Returns (width, height, [(cx, cy, diameter), ...]) in the artwork's own pixels.
    """
    im = Image.open(path).convert("RGBA")
    w, h = im.size
    a = im.getchannel("A").load()
    transparent = [[a[x, y] < 16 for x in range(w)] for y in range(h)]
    seen = [[False] * w for _ in range(h)]
    holes = []
    # Only the middle band can hold a reel window: the outer shadow is transparent too.
    for y0 in range(int(h * 0.30), int(h * 0.70), 3):
        for x0 in range(0, w, 3):
            if not transparent[y0][x0] or seen[y0][x0]:
                continue
            q = deque([(y0, x0)])
            seen[y0][x0] = True
            xs, ys = [], []
            while q:
                y, x = q.popleft()
                if not transparent[y][x]:
                    continue
                xs.append(x)
                ys.append(y)
                for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    ny, nx = y + dy, x + dx
                    if 0 <= ny < h and 0 <= nx < w and not seen[ny][nx] and transparent[ny][nx]:
                        seen[ny][nx] = True
                        q.append((ny, nx))
            bw, bh = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
            # A reel window is round and reasonably large; the shadow is neither.
            if len(xs) > 500 and abs(bw - bh) <= 4:
                holes.append((sum(xs) / len(xs), sum(ys) / len(ys), (bw + bh) / 2.0))
    holes.sort()
    return w, h, holes


def rgb565a8(im):
    """LVGL's RGB565A8: RGB565 rows, then alpha rows, both at their own stride."""
    w, h = im.size
    px = im.convert("RGBA").load()
    colour = bytearray()
    alpha = bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            colour += struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
            alpha.append(a)
    return bytes(colour) + bytes(alpha)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="measure only, write nothing")
    ap.add_argument("--front", default=FRONT)
    ap.add_argument("--wheel", default=WHEEL)
    args = ap.parse_args()

    fw, fh, holes = measure_holes(args.front)
    if len(holes) != 2:
        sys.exit(f"expected two reel windows in {args.front}, found {len(holes)}")
    print(f"front {fw}x{fh}, reel windows at "
          + ", ".join(f"({cx:.1f},{cy:.1f}) {d:.1f} px" for cx, cy, d in holes))

    # One scale for both images, so the wheels keep their size relative to the shell.
    scale = min((PANEL_W - 2 * MARGIN) / fw, ART_MAX_H / fh)
    fwt, fht = round(fw * scale), round(fh * scale)
    wheel_px = round(holes[0][2] * 2 * scale)  # the wheel is twice the window's width
    wheel_px = max(wheel_px, 8)
    print(f"scale {scale:.4f}: front -> {fwt}x{fht}, wheel -> {wheel_px}x{wheel_px}")

    off_x, off_y = (PANEL_W - fwt) // 2, 14
    for cx, cy, _d in holes:
        print(f"  window centre on the panel: ({off_x + cx * scale:.1f}, {off_y + cy * scale:.1f})")

    print(f"#define UI_FRONT_W {fwt}")
    print(f"#define UI_FRONT_H {fht}")
    print(f"#define UI_FRONT_X {off_x}")
    print(f"#define UI_FRONT_Y {off_y}")
    print(f"#define UI_WHEEL_PX {wheel_px}")
    print("#define UI_WINDOW_COUNT 2")
    print("static const struct { uint16_t x, y; } k_window[UI_WINDOW_COUNT] = {")
    for cx, cy, _d in holes:
        print(f"    {{ {round(off_x + cx * scale)}, {round(off_y + cy * scale)} }},")
    print("};")

    if args.check:
        return

    front = Image.open(args.front).convert("RGBA").resize((fwt, fht), Image.LANCZOS)
    wheel = Image.open(args.wheel).convert("RGBA").resize((wheel_px, wheel_px), Image.LANCZOS)
    for name, im in (("cs_front", front), ("cs_wheel", wheel)):
        blob = rgb565a8(im)
        path = f"main/assets/{name}.bin"
        with open(path, "wb") as f:
            f.write(blob)
        print(f"wrote {path}: {im.size[0]}x{im.size[1]}, {len(blob)} B "
              f"({len(blob) // 1024} KB of {3 * 1024} KB per 1024 px)")


if __name__ == "__main__":
    main()
