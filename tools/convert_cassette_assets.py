#!/usr/bin/env python3
"""Turn the cassette art into what the firmware can blit.

    python3 tools/convert_cassette_assets.py            # writes hub_frames.{h,bin} and label_cover.{h,bin}
    python3 tools/convert_cassette_assets.py --check    # only validates and reports

The screen is drawn by components/app/screen_display/cassette_view.c. The hub art comes
from assets/hub/NNN.png: 168x168 keyframes of the whole hub, one revolution. The source
frames may be RGBA: the transparent pixels are the holes that show the static hub ring
behind the image. The tool composites them onto the ring colour (`--background`, default
e6e6e6) so the firmware still blits opaque RGB565; a per-pixel alpha blend at 25 Hz is
the cost this pipeline avoids.

What this exports: only the MASK_AT_168 = 96 centre of each frame. The frame around it
is the UI's business; no full 168 frame is stored. The number of frames is the angular
resolution: one frame per 360/count degrees. The playback speed is not in the art; the
firmware derives it from the tape geometry (see components/portable/cassette).

The frames are opaque, so the tool processes them as they are: no alpha mask, no
recolour and no compositing. It fails if a frame has transparent pixels - transparency
would drag the renderer back into a per-pixel alpha blend, the cost this pipeline exists
to avoid. The firmware blits only the 96x96 centre per frame, which is the area that
actually changes.

The label cover is assets/labels/k7logo.png: a full-bleed cover at the panel's 800x480.
A cover with transparency (a tape window) is exported as RGB565A8 (RGB565 plus an A8
plane), so the reels show through it; an opaque cover is exported as RGB565. This is a
static image drawn once when the cover is shown; it does not carry the hub path's 25 Hz
blend cost.

The blobs are the crops concatenated and the label's pixels. The headers wrap them in
lv_image_dsc_t descriptors and carry the sizes. Both are embedded by
components/app/screen_display/CMakeLists.txt and rebuilt by this tool; do not edit them
by hand.

The hub pixels are opaque RGB565: any source alpha is flattened here, at build time. The
label keeps its alpha when it has one: a transparent tape window is the point of that art.

Requires Pillow (`pip install pillow`), which the project's documentation lists for
the host tooling.
"""
import argparse
import glob
import os
import re
import struct
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("needs Pillow: pip install pillow")

HUB_DIR = "assets/hub"
OUT_BIN = "components/app/screen_display/assets/hub_frames.bin"
OUT_HDR = "components/app/screen_display/include/hub_frames.h"
MASK_AT_168 = 96    # the animated centre's size at the native hub size

LABEL_SRC = "assets/labels/k7logo.png"
LABEL_OUT_BIN = "components/app/screen_display/assets/label_cover.bin"
LABEL_OUT_HDR = "components/app/screen_display/include/label_cover.h"


def frame_number(path):
    m = re.search(r"(\d+)", os.path.basename(path))
    if not m:
        sys.exit(f"cannot read a frame number from {path}")
    return int(m.group(1))


def load_frames(background):
    paths = sorted(glob.glob(os.path.join(HUB_DIR, "*.png")), key=frame_number)
    if len(paths) < 2:
        sys.exit(f"need at least two frames in {HUB_DIR}/, found {len(paths)}")

    frames = []
    size = None
    for p in paths:
        im = Image.open(p).convert("RGBA")
        if im.size[0] != im.size[1]:
            sys.exit(f"{p} is {im.size[0]}x{im.size[1]}, expected a square frame")
        if size is None:
            size = im.size
        elif im.size != size:
            sys.exit(f"{p} is {im.size[0]}x{im.size[1]}, the other frames are "
                     f"{size[0]}x{size[1]}")
        # The transparent pixels are the hub holes; flatten them onto the ring so the
        # descriptor stays RGB565 and the firmware never blends alpha.
        canvas = Image.new("RGBA", im.size, background + (255,))
        canvas.alpha_composite(im)
        frames.append(canvas.convert("RGB"))
    return frames, size


def rgb565(im):
    """Opaque RGB565 rows, little-endian, the stride the descriptors declare."""
    w, h = im.size
    px = im.load()
    data = bytearray(w * h * 2)
    i = 0
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            struct.pack_into("<H", data, i, ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
            i += 2
    return bytes(data)


def rgb565a8(im):
    """RGB565 rows followed by the A8 plane, the layout the engine's RGB565A8 expects."""
    w, h = im.size
    px = im.load()
    rgb = bytearray(w * h * 2)
    a8 = bytearray(w * h)
    p = 0
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            struct.pack_into("<H", rgb, p * 2,
                             ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
            a8[p] = a
            p += 1
    return bytes(rgb + a8)


def label_header_text(w, h, opaque):
    bpp = 2 if opaque else 3
    cf = "LV_COLOR_FORMAT_RGB565" if opaque else "LV_COLOR_FORMAT_RGB565A8"
    out = []
    out.append("/* Generated by tools/convert_cassette_assets.py from %s." % LABEL_SRC)
    out.append(" * Do not edit: re-run the tool after changing the art. */")
    out.append("#pragma once")
    out.append("")
    out.append("#include <stdint.h>")
    out.append("")
    out.append('#include "lvgl.h"')
    out.append("")
    out.append("#define LABEL_COVER_W %d" % w)
    out.append("#define LABEL_COVER_H %d" % h)
    out.append("#define LABEL_COVER_BYTES (LABEL_COVER_W * LABEL_COVER_H * %d)" % bpp)
    out.append("")
    if opaque:
        out.append("/* Opaque RGB565, with no alpha plane. EMBED_FILES puts the blob in the")
        out.append(" * image under this name. */")
    else:
        out.append("/* RGB565 followed by the A8 plane: the cover has transparent pixels, so")
        out.append(" * the reels show through them. EMBED_FILES puts the blob in the image")
        out.append(" * under this name. */")
    out.append('extern const uint8_t label_cover_bin_start[] asm("_binary_label_cover_bin_start");')
    out.append("")
    out.append("static const lv_image_dsc_t k_label_cover = {")
    out.append("    .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = %s," % cf)
    out.append("                .w = LABEL_COVER_W, .h = LABEL_COVER_H,")
    out.append("                .stride = LABEL_COVER_W * 2 },")
    out.append("    .data_size = LABEL_COVER_BYTES,")
    out.append("    .data = label_cover_bin_start,")
    out.append("};")
    out.append("")
    return "\n".join(out)


def is_opaque(im):
    """True when every pixel has alpha 255, so the A8 plane would be all 255s."""
    return im.getchannel("A").getextrema() == (255, 255)


def header_text(count, src_px, mask_px):
    crop_x = (src_px - mask_px) // 2
    crop_y = (src_px - mask_px) // 2
    out = []
    out.append("/* Generated by tools/convert_cassette_assets.py from the PNG frames")
    out.append(" * in %s/. Do not edit: re-run the tool after changing the art. */" % HUB_DIR)
    out.append("#pragma once")
    out.append("")
    out.append("#include <stdint.h>")
    out.append("")
    out.append('#include "lvgl.h"')
    out.append("")
    out.append("#define HUB_FRAME_COUNT %d" % count)
    out.append("#define HUB_FRAME_W %d" % src_px)
    out.append("#define HUB_FRAME_H %d" % src_px)
    out.append("#define HUB_CROP_W %d" % mask_px)
    out.append("#define HUB_CROP_H %d" % mask_px)
    out.append("#define HUB_CROP_X %d" % crop_x)
    out.append("#define HUB_CROP_Y %d" % crop_y)
    out.append("#define HUB_CROP_BYTES (HUB_CROP_W * HUB_CROP_H * 2)")
    out.append("")
    out.append("/* Only the %dx%d animated centre of each of the %d frames is exported."
               % (mask_px, mask_px, count))
    out.append(" * The frame around it is the UI's business: no full 168 frame is stored.")
    out.append(" * The frames are one revolution; the count is its angular resolution.")
    out.append(" * The source alpha is flattened onto the ring; the crops are blitted as")
    out.append(" * opaque RGB565. */")
    out.append("")
    out.append("/* EMBED_FILES puts components/app/screen_display/assets/hub_frames.bin in the image under this name. */")
    out.append('extern const uint8_t hub_frames_bin_start[] asm("_binary_hub_frames_bin_start");')
    out.append("")
    out.append("static const lv_image_dsc_t k_hub_crops[HUB_FRAME_COUNT] = {")
    for i in range(count):
        out.append("    {")
        out.append("        .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,")
        out.append("                    .w = HUB_CROP_W, .h = HUB_CROP_H,")
        out.append("                    .stride = HUB_CROP_W * 2 },")
        out.append("        .data_size = HUB_CROP_BYTES,")
        out.append("        .data = hub_frames_bin_start + %d * HUB_CROP_BYTES," % i)
        out.append("    },")
    out.append("};")
    out.append("")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="validate and report, write nothing")
    ap.add_argument("--background", default="e6e6e6",
                    help="RGB hex the transparent pixels are composited onto "
                         "(the hub ring; default e6e6e6)")
    args = ap.parse_args()

    bg = args.background.lstrip("#")
    if len(bg) != 6:
        sys.exit(f"--background needs six hex digits, got {args.background!r}")
    background = tuple(int(bg[i:i + 2], 16) for i in (0, 2, 4))

    frames, size = load_frames(background)
    count = len(frames)
    mask_px = max(16, round(size[0] * MASK_AT_168 / 168))
    if mask_px > size[0]:
        sys.exit(f"the {mask_px} px crop does not fit a {size[0]} px frame")
    left = (size[0] - mask_px) // 2
    top = (size[1] - mask_px) // 2
    blob = count * mask_px * mask_px * 2

    print(f"{count} frames in {HUB_DIR}/, {size[0]}x{size[1]}; exporting only the "
          f"{mask_px}x{mask_px} animated centre; one revolution at {360 / count:g} deg/frame; "
          f"alpha flattened onto #{bg}")
    print(f"blob {blob} B ({blob // 1024} KB)")

    if not os.path.exists(LABEL_SRC):
        sys.exit(f"missing label art: {LABEL_SRC}")
    label = Image.open(LABEL_SRC).convert("RGBA")
    label_opaque = is_opaque(label)
    label_blob = rgb565(label.convert("RGB")) if label_opaque else rgb565a8(label)
    print(f"label {label.size[0]}x{label.size[1]} from {LABEL_SRC}; "
          f"{'RGB565' if label_opaque else 'RGB565A8'} "
          f"{len(label_blob)} B ({len(label_blob) // 1024} KB)")

    if args.check:
        print("--check: nothing written")
        return

    os.makedirs(os.path.dirname(OUT_BIN), exist_ok=True)
    with open(OUT_BIN, "wb") as f:
        for im in frames:
            f.write(rgb565(im.crop((left, top, left + mask_px, top + mask_px))))
    print(f"wrote {OUT_BIN}")

    with open(OUT_HDR, "w") as f:
        f.write(header_text(count, size[0], mask_px))
    print(f"wrote {OUT_HDR}")

    with open(LABEL_OUT_BIN, "wb") as f:
        f.write(label_blob)
    print(f"wrote {LABEL_OUT_BIN}")

    with open(LABEL_OUT_HDR, "w") as f:
        f.write(label_header_text(*label.size, label_opaque))
    print(f"wrote {LABEL_OUT_HDR}")


if __name__ == "__main__":
    main()
