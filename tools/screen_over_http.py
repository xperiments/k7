#!/usr/bin/env python3
"""Look at the panel board's screen over HTTP, and diff two captures of it.

    python3 tools/screen_over_http.py capture --host 192.168.1.50:8000 --out /tmp/base
    python3 tools/screen_over_http.py compare /tmp/base /tmp/after

The board has to be built with CONFIG_APP_UI_SCREEN_HTTP=y (screen_display's Kconfig, off by
default). That build serves two routes:

    GET  /api/screen.bmp    the 800x480 frame as a 24-bit bottom-up BMP, 1,152,054 bytes
    POST /api/scene?n=N     re-aims the diagnostic scene driver, N = 0..9, and answers once
                            the picture it asks for is on the screen (the route waits for the
                            scene's own script, so a capture never photographs the walk to it)

`capture` posts each scene and saves its frame; `compare` reports, per scene, how many pixels
differ between two capture directories. The intended use is a refactor that must not change the
picture: capture before, capture after, and expect 0.

Why a mask, and why one per scene. Parts of this screen move on their own, so a fixed
`sleep` either races the app's 260 ms animations or photographs a half-drawn scene, and a raw
pixel diff of two captures is dominated by them:

  * the tape turns: both packs and both hub spokes animate while anything is playing;
  * the VU columns follow the audio;
  * the footer rotates its three faces (times, artist, song) on a four-second timer, and its
    progress fill grows with the broadcast.

Those regions are excluded from both the settle test and the comparison, and **which ones move
depends on the scene**. The Menu hides the cassette and puts the Pad where the meter was; the 3x
dial dims the cassette. A single mask over the tape box - the shape this tool used to have -
therefore covered the Menu's page and the dial itself, so scenes 4, 6 and 7 were compared almost
blind. MOTION below is the table: each scene names the regions that move in it, and the
comparison sees everything else. The regions' derivations are in the MOTION comment.

Scenes 0 and 8 are photographed but not compared: 0 is the entry view, where the cassette
fills the screen and there is nothing outside the mask; 8 is the Peek, whose floating tags sit
over the reels and would need the reels comparing to see the tethers. Use them to look, not to
diff.

Exit codes: 0 all compared scenes identical, 1 a scene differs, 2 the tool could not do its
job (unreachable host, no diagnostic build, bad capture, bad arguments).

Pillow is required - for the BMP and the mask. It is the same optional dependency
tools/decode_screen_dump.py has, except that this tool cannot fall back to PPM, because the
mask needs real image arithmetic:

    python3 -m pip install pillow

The DSH bundled interpreter already has it:
~/.dsh/dsh-runtimes/dsh-primary-runtime/dependencies/python/bin/python3

A 1.15 MB frame takes a couple of seconds over Wi-Fi and much longer on a bad link, so the
per-request timeout defaults to 60 s (--timeout). One settle poll costs one whole frame.
"""
import argparse
import http.client
import io
import os
import re
import sys
import time
import urllib.error
import urllib.request


class Failure(Exception):
    """Something the tool cannot do; printed as one line, exit 2."""


class Busy(Failure):
    """The screen answered 503: its lock was held or PSRAM was momentarily short. Retryable."""


# Pillow is a hard requirement of both commands, and imported lazily so that `--help` still
# works on an interpreter without it - the failure then names the fix instead of a traceback.
_PIL = None


def pil():
    global _PIL
    if _PIL is None:
        try:
            from PIL import Image, ImageChops, ImageDraw
        except ImportError:
            raise Failure(
                "this tool needs Pillow, for the BMP and the mask:\n"
                "    python3 -m pip install pillow\n"
                "or run it with the bundled interpreter, which has it:\n"
                "    ~/.dsh/dsh-runtimes/dsh-primary-runtime/dependencies/python/bin/python3"
            ) from None
        _PIL = (Image, ImageChops, ImageDraw)
    return _PIL

# The panel, the frame and the highest scene the driver knows. CASSETTE_SCENE_MAX in
# components/app/screen_display/cassette_view.h and the route's own bound; keep the three in
# step - the device answers 400 to a scene above this.
WIDTH, HEIGHT = 800, 480
BMP_BYTES = 54 + WIDTH * HEIGHT * 3
MAX_SCENE = 9

SCENE_NAMES = {
    0: "entry view",
    1: "Art/Empty",
    2: "Art/EQ",
    3: "Art/RGB",
    4: "Menu",
    5: "Real/EQ",
    6: "EQ dial",
    7: "RGB dial",
    8: "Peek",
    9: "Real/RGB",
}

# Scenes that are photographed but not diffed, and why. Everything not named here is compared.
NOT_COMPARED = {
    0: "the cassette fills the screen, so there is nothing outside the mask",
    8: "the Peek's tags float over the reels, which the mask has to leave visible",
}

# The regions that move on their own, in screen pixels, and where each comes from:
#
#   REELS   The tape body and the 3x dial are one rect, because they are the same box. On the
#           design grid the tape is cassette_tape_box() = (16,16,768,360); at
#           cassette_pose_controls() - 134/256, dx -51, dy -20 - that lands at
#           (148,102)-(550,334). The dial's container is knob_editor's BIG_X/BIG_Y/BIG_W/BIG_H =
#           (120,76,456,284) = (120,76)-(576,360), which contains the tape's box, so the union is
#           the dial's container. The packs' rings are drawn from the hub centre and stay inside
#           the tape's frame; the dial, its pillow and its plaques are inside its container by
#           construction. It is a movable region only where the cassette is on screen: the Menu
#           hides it, and the dial's edit cuts it out, so those scenes do not mask it.
#   METER   The meter's plate: the right bar's inside column, top two cells. The bar's geometry
#           puts cell (0,0) at (594,76) with a 94 px pitch and 96 px cells, so the two-cell plate
#           is 96x190 at (594,76): (594,76)-(690,266). The dots follow the audio and the L/R pill
#           hangs under them. Only the EQ group draws it; the Menu puts the Pad in its place.
#   FOOTER  The bar the footer draws its faces in: CASSETTE_TIMELINE_X/Y/W/H =
#           (116,416,568,52): (116,416)-(684,468). The times, the artist and the song are all
#           inside it, and the fill grows with the broadcast.
#   PERF    LVGL's own performance monitor, in the top-right corner, on the builds that enable it
#           (the headless diagnostic config does, the panel's does not). It rewrites an FPS and a
#           CPU figure every frame, so no headless scene can ever settle while it is on screen -
#           masking it also keeps this tool from demanding a config change from a build whose
#           whole purpose is to measure that.
REELS = (120, 76, 576, 360)
METER = (594, 76, 690, 266)
FOOTER = (116, 416, 684, 468)
PERF = (660, 0, 800, 40)

# What moves in each scene, and therefore what neither the settle poll nor the comparison
# looks at. The chrome scenes keep the cassette (and its reels) behind them; the Menu and the
# dials do not. The Menu's page, its Pad and the dial's ring are static after the settle poll, so
# a diff that ignores only these regions sees every pixel of the thing under test. Scenes 0 and 8
# are photographed and not compared, so they are absent here: no mask can settle a picture whose
# subject *is* the motion, and they get a timed photograph instead (LOOK_ONLY_SETTLE).
MOTION = {
    1: (REELS, FOOTER, PERF),
    2: (REELS, METER, FOOTER, PERF),
    3: (REELS, FOOTER, PERF),
    4: (FOOTER, PERF),
    5: (REELS, METER, FOOTER, PERF),
    6: (FOOTER, PERF),
    7: (FOOTER, PERF),
    9: (REELS, FOOTER, PERF),
}

# Seconds to wait before photographing a look-only scene. The scene driver's longest move is the
# 260 ms slide plus a step or two of the 25 Hz screen task, and the entry view and the Peek are
# each one move from the entry pose; 1.2 s is that with room. It is not a settle test - there is
# nothing to settle, because the reels and the Peek's tags never stop.
LOOK_ONLY_SETTLE = 1.2

_MASKS = {}


def mask(scene):
    """255 where the scene is stable enough to compare, 0 inside the regions that move in it."""
    if scene not in _MASKS:
        Image, _, ImageDraw = pil()
        m = Image.new("L", (WIDTH, HEIGHT), 255)
        draw = ImageDraw.Draw(m)
        for x0, y0, x1, y1 in MOTION.get(scene, ()):
            draw.rectangle([x0, y0, x1, y1], fill=0)
        _MASKS[scene] = m
    return _MASKS[scene]


# --------------------------------------------------------------------------- http


def request(host, path, data=None, timeout=60.0):
    """One request. `data` makes it a POST, which the front door requires to be JSON."""
    headers = {}
    if data is not None:
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(f"http://{host}{path}", data=data, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as response:
            return response.read()
    except urllib.error.HTTPError as err:
        if err.code == 404:
            raise Failure(
                f"404 from {host}{path}: this firmware has no screen diagnostics.\n"
                "  flash a build with CONFIG_APP_UI_SCREEN_HTTP=y (screen_display's Kconfig).\n"
                "  The production build leaves the routes out by design."
            ) from None
        if err.code == 405:
            raise Failure(f"{host}{path} answered 405: wrong method for this route") from None
        if err.code == 400:
            raise Failure(f"{host}{path} answered 400: bad request - a scene must be 0..{MAX_SCENE}"
                          ) from None
        if err.code == 503:
            raise Busy(f"{host}{path} answered 503: the screen is busy (its lock or PSRAM).") from None
        raise Failure(f"{host}{path} answered {err.code} {err.reason}") from None
    except urllib.error.URLError as err:
        raise Failure(f"cannot reach {host}: {err.reason} (is the board on this network, "
                      "and is the port right?)") from None
    except TimeoutError:
        raise Failure(f"{host}{path} timed out: a frame is 1.15 MB, so a weak link needs "
                      "--timeout above the default 60 s") from None
    except (http.client.HTTPException, OSError) as err:
        # A dropped socket or a truncated response: http.client raises its own type here, not a
        # URLError, and a half-read frame is exactly what a bad link produces.
        raise Failure(f"{host}{path} broke off mid-response: {err}. A frame is 1.15 MB, so "
                      "a weak link hits this; try again, or raise --timeout") from None


def retrying(call, retries=3, pause=0.5):
    """Run `call`, giving a momentary 503 a few tries before the run is declared failed.

    The screen answers 503 while its port holds LVGL - a whole-screen refresh after a scene
    change is the usual moment - and one busy frame should not throw away the scenes already
    photographed.
    """
    for attempt in range(retries):
        try:
            return call()
        except Busy:
            if attempt + 1 >= retries:
                raise
            time.sleep(pause)


def post_scene(host, scene, timeout):
    """Aim a scene and wait for its answer. The route blocks until the script has landed, so a
    200 means the picture is there; a 200 that says `not ready` missed the device's own 2 s
    budget and is retried like a busy screen."""
    def once():
        body = request(host, f"/api/scene?n={scene}", data=b"{}", timeout=timeout)
        if b"not ready" in body:
            raise Busy(f"scene {scene} did not land: the device answered {body!r}")
        return body
    retrying(once)


def fetch_frame(host, timeout):
    """One frame, checked: the right magic, the right length, the right size once decoded."""
    data = retrying(lambda: request(host, "/api/screen.bmp", timeout=timeout))
    if data[:2] != b"BM":
        raise Failure(f"/api/screen.bmp did not return a BMP: magic {data[:2]!r}, "
                      f"{len(data)} bytes. Is that port the board's HTTP front door?")
    if len(data) != BMP_BYTES:
        raise Failure(f"/api/screen.bmp returned {len(data)} bytes, expected {BMP_BYTES} "
                      f"({WIDTH}x{HEIGHT} at 24 bpp). A partial frame is a link or "
                      "memory problem on the board, not a capture.")
    Image, _, _ = pil()
    try:
        image = Image.open(io.BytesIO(data))
        image.load()
    except Exception as err:  # Pillow raises several types for a malformed BMP
        raise Failure(f"/api/screen.bmp is not a readable BMP: {err}") from None
    if image.size != (WIDTH, HEIGHT):
        raise Failure(f"/api/screen.bmp is {image.size}, not {(WIDTH, HEIGHT)}: "
                      "is this the 800x480 panel board?")
    return data


# ------------------------------------------------------------------------ images


def as_image(data_or_path):
    Image, _, _ = pil()
    try:
        if isinstance(data_or_path, bytes):
            return Image.open(io.BytesIO(data_or_path)).convert("RGB")
        return Image.open(data_or_path).convert("RGB")
    except Exception as err:  # Pillow's own types, plus OSError for a missing file
        where = "the response" if isinstance(data_or_path, bytes) else data_or_path
        raise Failure(f"{where} is not a readable BMP: {err}") from None


def static_diff(a, b, scene):
    """(count, bbox, image) of the differing pixels outside the scene's motion regions."""
    _, ImageChops, _ = pil()
    diff = ImageChops.difference(a, b).convert("L")
    static = ImageChops.multiply(diff, mask(scene))
    return sum(static.histogram()[1:]), static.getbbox(), static


# ----------------------------------------------------------------------- capture


def settled_frame(host, timeout, polls, pause, scene):
    """The frame once two captures in a row agree outside the scene's motion regions.

    Returns (data, polls_used, settled). `settled` is False when the budget ran out: scenes
    whose own motion reaches outside the mask - 0, where the reels fill the screen, and 8,
    where the tags float over them - never agree with themselves, and that is not an error.
    """
    previous = fetch_frame(host, timeout)
    for used in range(1, polls + 1):
        time.sleep(pause)
        current = fetch_frame(host, timeout)
        if static_diff(as_image(previous), as_image(current), scene)[0] == 0:
            return current, used, True
        previous = current
    return previous, polls, False


def parse_scenes(text):
    scenes = []
    for part in text.replace(" ", "").split(","):
        if not part:
            continue
        try:
            scene = int(part, 10)
        except ValueError:
            raise Failure(f"--scenes takes numbers 0..{MAX_SCENE}, comma separated; "
                          f"got {part!r}") from None
        if scene < 0 or scene > MAX_SCENE:
            raise Failure(f"scene {scene} is out of range: the driver knows 0..{MAX_SCENE} "
                          f"({', '.join(f'{n} {SCENE_NAMES[n]}' for n in range(MAX_SCENE + 1))})")
        if scene not in scenes:
            scenes.append(scene)
    if not scenes:
        raise Failure(f"--scenes is empty; give at least one number 0..{MAX_SCENE}")
    return scenes


def capture_scene(host, scene, timeout, polls=8, pause=0.4):
    """Aim one scene and return its frame, with the catalogue's timing rules applied.

    Returns (data, polls_used, settled). A look-only scene is photographed after a fixed pause
    and reports settled=True with 0 polls: its subject *is* the motion, so no pixel test can
    settle it. The shared implementation, so the BMP capture below and the docs catalogue in
    tools/capture_screens.py photograph a scene the same way.
    """
    post_scene(host, scene, timeout)
    if scene in NOT_COMPARED:
        time.sleep(LOOK_ONLY_SETTLE)
        return fetch_frame(host, timeout), 0, True
    return settled_frame(host, timeout, polls, pause, scene)


def cmd_capture(args):
    host = args.host.removeprefix("http://").rstrip("/")
    scenes = parse_scenes(args.scenes)
    pil()  # an environment problem before a network one
    if args.timeout <= 0:
        raise Failure("--timeout must be positive: it is seconds per request")
    if args.polls < 1:
        raise Failure("--polls must be at least 1: it is how many frames to try")
    if args.pause < 0:
        raise Failure("--pause cannot be negative")

    # One request first, so a wrong host or a build without the diagnostics fails before the
    # tool starts photographing scenes or creating a directory.
    fetch_frame(host, args.timeout)
    os.makedirs(args.out, exist_ok=True)

    unsettled = []
    for scene in scenes:
        data, polls, settled = capture_scene(host, scene, args.timeout, args.polls, args.pause)
        path = os.path.join(args.out, f"scene{scene}.bmp")
        with open(path, "wb") as handle:
            handle.write(data)
        label = f"scene {scene} ({SCENE_NAMES[scene]})"
        if scene in NOT_COMPARED:
            print(f"{label}: photographed after {LOOK_ONLY_SETTLE:g} s, not compared -> {path}")
        elif settled:
            print(f"{label}: settled after {polls} poll{'s' if polls != 1 else ''} -> {path}")
        else:
            unsettled.append(scene)
            print(f"{label}: still moving after {polls} polls -> {path}")
    if unsettled:
        print(f"\n{len(unsettled)} scene(s) never settled: "
              f"{', '.join(str(n) for n in unsettled)}. Any of those is a slow link (raise "
              "--polls) or something still moving that should not be.")
    print(f"\n{len(scenes)} scene(s) in {args.out}")
    return 0


# ----------------------------------------------------------------------- compare


SCENE_FILE = re.compile(r"^scene(\d+)\.bmp$")


def scenes_in(directory):
    found = {}
    for name in os.listdir(directory):
        match = SCENE_FILE.match(name)
        if match:
            found[int(match.group(1))] = os.path.join(directory, name)
    return found


def cmd_compare(args):
    pil()  # an environment problem before an I/O one
    for directory in (args.before, args.after):
        if not os.path.isdir(directory):
            raise Failure(f"{directory} is not a directory; capture with "
                          "`screen_over_http.py capture --out DIR` first")
    try:
        before, after = scenes_in(args.before), scenes_in(args.after)
    except OSError as err:
        raise Failure(f"cannot list a capture directory: {err}") from None
    if not before and not after:
        raise Failure(f"no scene<N>.bmp in {args.before} or {args.after}")

    compared = differing = 0
    print(f"{'scene':6} {'name':12} {'static diff':>12}  bbox")
    for scene in sorted(set(before) | set(after)):
        name = SCENE_NAMES.get(scene, "?")
        if scene not in before or scene not in after:
            side = args.after if scene not in before else args.before
            print(f"{scene:<6} {name:12} {'-':>12}  only in {side}")
            continue
        a, b = as_image(before[scene]), as_image(after[scene])
        if a.size != b.size:
            print(f"{scene:<6} {name:12} {'-':>12}  size {a.size} vs {b.size}")
            differing += 1
            continue
        if scene in NOT_COMPARED:
            print(f"{scene:<6} {name:12} {'-':>12}  not compared: {NOT_COMPARED[scene]}")
            continue

        count, bbox, static = static_diff(a, b, scene)
        compared += 1
        print(f"{scene:<6} {name:12} {count:12d}  {bbox}")
        if count:
            differing += 1
            # The evidence, amplified 8x: what differs and where.
            path = os.path.join(args.after, f"scene{scene}.diff.png")
            static.point(lambda v: min(255, v * 8)).save(path)
            print(f"{'':19} wrote {path}")

    if not compared and not differing:
        raise Failure("no comparable scenes: nothing in both directories outside 0 and 8")
    print(f"\n{compared} scene(s) compared, {differing} differ")
    return 1 if differing else 0


# -------------------------------------------------------------------------- main


def main():
    parser = argparse.ArgumentParser(
        description="Capture the panel board's screen over HTTP, and diff two captures.",
        epilog="Needs a build with CONFIG_APP_UI_SCREEN_HTTP=y. Exit codes: "
               "0 identical, 1 a scene differs, 2 the tool could not run.",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    capture = sub.add_parser(
        "capture",
        help="post each scene and save its frame as DIR/scene<N>.bmp",
        description="Post each scene, wait for the picture to stop moving, and save it.",
    )
    capture.add_argument("--host", required=True, metavar="HOST:PORT",
                         help="the board's front door, e.g. 192.168.1.69:8000")
    capture.add_argument("--out", required=True, metavar="DIR",
                         help="directory to write scene<N>.bmp into (created if missing)")
    capture.add_argument("--scenes", default=",".join(str(n) for n in range(MAX_SCENE + 1)),
                         metavar="LIST",
                         help=f"comma separated, 0..{MAX_SCENE} (default: all of them)")
    capture.add_argument("--timeout", type=float, default=60.0, metavar="SECONDS",
                         help="per request (default 60: a frame is 1.15 MB)")
    capture.add_argument("--polls", type=int, default=8, metavar="N",
                         help="how many frames to try before giving up on a still one (default 8)")
    capture.add_argument("--pause", type=float, default=0.4, metavar="SECONDS",
                         help="between settle polls (default 0.4)")
    capture.set_defaults(func=cmd_capture)

    compare = sub.add_parser(
        "compare",
        help="diff two capture directories outside the regions that move on their own",
        description="Per scene: the pixels that differ outside the mask. Exit 1 if any does.",
    )
    compare.add_argument("before", metavar="BEFORE_DIR")
    compare.add_argument("after", metavar="AFTER_DIR")
    compare.set_defaults(func=cmd_compare)

    args = parser.parse_args()
    try:
        return args.func(args)
    except Failure as failure:
        print(f"screen_over_http: {failure}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
