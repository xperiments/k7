#!/usr/bin/env python3
"""Capture the screen catalogue over HTTP and write the images the docs use.

    npm run screens                          # xtape.local:8000 -> docs/images/screens/
    npm run screens -- --host 192.168.1.69:8000
    python3 tools/capture_screens.py --out /tmp/screens --scenes 4,6,7

The catalogue is the T1 set: one capture per surface, plus the Menu at its root and no menu
depth. The scenes live on the device (`POST /api/scene?n=0..9`) and are described in
components/app/screen_display/cassette_view.h. Each one is photographed once the device says
the scene has landed (the route waits for its own script), then the frame is settled against
the scene's motion regions - see `capture_scene()` in tools/screen_over_http.py, which this
script shares with the BMP/diff workflow.

Two scenes are photographed and never compared: the entry view, where the reels fill the
screen, and the Peek, whose tags bob. They are timed instead of settled (LOOK_ONLY_SETTLE).

Needs a build with CONFIG_APP_UI_SCREEN_HTTP=y (sdkconfig.defaults.capture), the board on the
network, and Pillow:

    python3 -m pip install pillow

If the `python3` that runs this has no Pillow, the DSH runtime the screen tools document is
used instead when it is installed. The images are written as PNG:

    <out>/<scene>-<slug>.png        one per scene, 800x480
    <out>/contact-sheet.png         the same ten, half scale, captioned

`screen_over_http.py compare` is the other half of the pair: it diffs two directories of
`scene<N>.bmp` frames. This script writes what a reader opens; that one writes what a diff reads.
"""
import argparse
import os
import re
import sys
import time

import screen_over_http as wire

# The board advertises xtape.local over mDNS (board_esp32.c). A literal address works too.
DEFAULT_HOST = "xtape.local:8000"
DEFAULT_OUT = os.path.join("docs", "images", "screens")

# The contact sheet: half the panel per tile, two columns, a caption under each.
TILE_W, TILE_H = 400, 240
CAPTION_H = 24
COLUMNS = 2

# Pillow is the screen tools' optional dependency. This machine has it in the DSH runtime and
# not in the system python, so prefer the runtime when the current interpreter cannot import
# it; the script is otherwise portable.
BUNDLED_PYTHON = os.path.expanduser(
    "~/.dsh/dsh-runtimes/dsh-primary-runtime/dependencies/python/bin/python3")


def _use_an_interpreter_with_pillow():
    """Re-run under the bundled interpreter when this one has no Pillow, once."""
    try:
        import PIL  # noqa: F401
        return
    except ImportError:
        pass
    if (os.path.isfile(BUNDLED_PYTHON) and os.access(BUNDLED_PYTHON, os.X_OK)
            and os.path.realpath(sys.executable) != os.path.realpath(BUNDLED_PYTHON)
            and not os.environ.get("XTAPE_SCREENS_REEXEC")):
        os.environ["XTAPE_SCREENS_REEXEC"] = "1"
        os.execv(BUNDLED_PYTHON, [BUNDLED_PYTHON, os.path.abspath(__file__), *sys.argv[1:]])


def slug(name):
    """`Art/Empty` -> `art-empty`: a file name that survives every filesystem."""
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")


def capture(host, out, scenes, timeout, polls, pause):
    """Photograph `scenes` into `out` as PNGs, and return the images by scene number."""
    # One frame first: a wrong host, a normal build without the routes or a busy board should
    # fail before a directory is created or a scene is aimed.
    wire.fetch_frame(host, timeout)
    os.makedirs(out, exist_ok=True)

    images = {}
    for scene in scenes:
        data, used, settled = wire.capture_scene(host, scene, timeout, polls, pause)
        image = wire.as_image(data)
        path = os.path.join(out, f"{scene}-{slug(wire.SCENE_NAMES[scene])}.png")
        image.save(path)
        images[scene] = image
        name = wire.SCENE_NAMES[scene]
        if scene in wire.NOT_COMPARED:
            print(f"scene {scene} ({name}): photographed, look-only -> {path}")
        elif settled:
            print(f"scene {scene} ({name}): settled after {used} poll{'s' if used != 1 else ''} -> {path}")
        else:
            print(f"scene {scene} ({name}): still moving after {used} polls -> {path}")
    return images


def contact_sheet(images, path):
    """The catalogue as one picture: the docs' index of what the device can show."""
    from PIL import Image, ImageDraw, ImageFont

    scenes = sorted(images)
    rows = (len(scenes) + COLUMNS - 1) // COLUMNS
    sheet = Image.new("RGB", (TILE_W * COLUMNS, (TILE_H + CAPTION_H) * rows), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.load_default(size=15)
    except TypeError:                      # Pillow before 10.1 has no sized default font
        font = ImageFont.load_default()

    for i, scene in enumerate(scenes):
        x = (i % COLUMNS) * TILE_W
        y = (i // COLUMNS) * (TILE_H + CAPTION_H)
        sheet.paste(images[scene].resize((TILE_W, TILE_H), Image.LANCZOS), (x, y))
        draw.text((x + 8, y + TILE_H + 5), f"{scene}  {wire.SCENE_NAMES[scene]}",
                  font=font, fill=(240, 240, 240))
    sheet.save(path)
    return path


def main():
    parser = argparse.ArgumentParser(
        description="Capture the screen catalogue over HTTP and write the docs images.",
        epilog="Needs a build with CONFIG_APP_UI_SCREEN_HTTP=y. Writes PNGs, not BMPs; "
               "tools/screen_over_http.py capture/compare is the diff workflow.",
    )
    parser.add_argument("--host", default=DEFAULT_HOST, metavar="HOST:PORT",
                        help=f"the board's front door (default {DEFAULT_HOST})")
    parser.add_argument("--out", default=DEFAULT_OUT, metavar="DIR",
                        help=f"where to write the PNGs (default {DEFAULT_OUT})")
    parser.add_argument("--scenes", default=",".join(str(n) for n in range(wire.MAX_SCENE + 1)),
                        metavar="LIST", help=f"comma separated, 0..{wire.MAX_SCENE} (default: all)")
    parser.add_argument("--timeout", type=float, default=60.0, metavar="SECONDS",
                        help="per request (default 60: a frame is 1.15 MB)")
    parser.add_argument("--polls", type=int, default=8, metavar="N",
                        help="frames to try before giving up on a still one (default 8)")
    parser.add_argument("--pause", type=float, default=0.4, metavar="SECONDS",
                        help="between settle polls (default 0.4)")
    args = parser.parse_args()

    _use_an_interpreter_with_pillow()

    host = args.host.removeprefix("http://").rstrip("/")
    if args.timeout <= 0:
        raise wire.Failure("--timeout must be positive: it is seconds per request")
    if args.polls < 1:
        raise wire.Failure("--polls must be at least 1: it is how many frames to try")
    if args.pause < 0:
        raise wire.Failure("--pause cannot be negative")

    scenes = wire.parse_scenes(args.scenes)
    images = capture(host, args.out, scenes, args.timeout, args.polls, args.pause)
    sheet = contact_sheet(images, os.path.join(args.out, "contact-sheet.png"))

    print(f"\n{len(images)} scene(s) in {args.out}, contact sheet {sheet}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except wire.Failure as failure:
        print(f"capture_screens: {failure}", file=sys.stderr)
        sys.exit(2)
