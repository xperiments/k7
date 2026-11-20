#!/usr/bin/env python3
"""Open the built label editor in headless Chrome and prove it booted.

The editor is the React app in `tools/LabelEditor`: `make -C host label` builds it into
`dist/`, and this opens that build, not its source. On 905313d its predecessor put a
`const` below the first `defaultTpl()` call that read it, so the page threw a TDZ
ReferenceError while loading and painted nothing: the tabs were empty, the layer list was
empty and the stage had no objects. Every host check stayed green because none of them
opened the editor.

This is that missing seam. It serves the build over HTTP (the app's modules and its faces
will not load from `file://`), opens it with a fresh Chrome profile, reads the rendered DOM,
and requires what the boot paints: the design's tab, the layer list, and the stage with its
default slot values. A page that throws before the tail of its own script leaves all three
empty, so the check fails for the right reason.

Two browser failures skip loudly and succeed, so a machine that cannot run the check is not a
failed host check: no Chrome at all, and a Chrome whose GPU process dies before the page's own
script (the fatal line is on Chrome's stderr and no DOM is dumped). The skip is keyed on that
line, never on an empty document alone: a page that really throws leaves a DOM and still fails.

Usage: label_boot_tests.py [DIST] [--timeout SECONDS]
"""
import argparse
import functools
import http.server
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

FAIL = 0

DEFAULT_DIST = Path(__file__).resolve().parents[2] / "tools" / "LabelEditor" / "dist"


def check(label, ok, detail=""):
    global FAIL
    if ok:
        print(f"  ok    {label}" + (f" — {detail}" if detail else ""))
    else:
        print(f"FAIL {label}" + (f" — {detail}" if detail else ""))
        FAIL += 1


def find_chrome():
    """The browser this check can drive, or None. CHROME overrides the search."""
    override = os.environ.get("CHROME")
    if override:
        return override if Path(override).exists() else None

    candidates = [
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
        "/Applications/Chromium.app/Contents/MacOS/Chromium",
        str(Path.home() / "Applications/Google Chrome.app/Contents/MacOS/Google Chrome"),
    ]
    for path in candidates:
        if Path(path).exists():
            return path
    for name in ("google-chrome", "google-chrome-stable", "chromium", "chromium-browser", "chrome"):
        found = shutil.which(name)
        if found:
            return found
    return None


# Chrome's own report that its GPU process cannot start: it exits before the page's script
# runs and dumps no DOM. The phrase is Chrome's, not the page's, and it is the one reason a
# found browser may skip; an empty document alone is still a failure.
GPU_UNUSABLE = re.compile(r"GPU process isn't usable\.(?: Goodbye\.)?", re.I)


def gpu_unusable(stderr):
    """The phrase Chrome prints when its GPU process cannot start, or None."""
    match = GPU_UNUSABLE.search(stderr or "")
    return match.group(0) if match else None


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    """A static server for the build that does not narrate every module and face."""

    def log_message(self, *args):
        pass


def serve(directory):
    """Serve the build on a free loopback port; the app cannot boot from file://."""
    handler = functools.partial(QuietHandler, directory=str(directory))
    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd, f"http://127.0.0.1:{httpd.server_address[1]}/"


def strip_tags(html):
    """The text a human sees in a block, with tags and entities roughly removed."""
    text = re.sub(r"<script\b.*?</script>", " ", html, flags=re.S | re.I)
    text = re.sub(r"<style\b.*?</style>", " ", text, flags=re.S | re.I)
    text = re.sub(r"<[^>]+>", " ", text)
    text = text.replace("&nbsp;", " ").replace("&amp;", "&").replace("&quot;", '"')
    text = text.replace("&lt;", "<").replace("&gt;", ">").replace("&#39;", "'")
    return re.sub(r"\s+", " ", text).strip()


def block(html, opening, closing):
    """One element's inner HTML, or "" when the element is not in the dump."""
    match = re.search(opening + r"(.*?)" + closing, html, re.S | re.I)
    return match.group(1) if match else ""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("dist", nargs="?", default=str(DEFAULT_DIST))
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()

    dist = Path(args.dist).resolve()
    page = dist / "index.html"
    if not page.is_file():
        print(f"FAIL label page: {page} does not exist — run `npm run build` in "
              f"tools/LabelEditor (`make -C host label` does it).")
        return 1

    chrome = find_chrome()
    if chrome is None:
        print("SKIP label page: no Chrome found (set CHROME to a browser binary).", file=sys.stderr)
        print("SKIP label page: the label editor boot check did not run.")
        return 0

    # The production build's base is /k7art/, so serve a parent with the build as k7art/.
    root = Path(tempfile.mkdtemp(prefix="k7-label-boot-root-"))
    (root / "k7art").symlink_to(dist, target_is_directory=True)
    httpd, url = serve(root)
    url += "k7art/"
    profile = tempfile.mkdtemp(prefix="k7-label-boot-")
    try:
        command = [
            chrome,
            "--headless=new",
            "--disable-gpu",
            "--no-first-run",
            "--no-default-browser-check",
            f"--user-data-dir={profile}",
            "--virtual-time-budget=15000",
            "--dump-dom",
            url,
        ]
        try:
            run = subprocess.run(command, capture_output=True, text=True, timeout=args.timeout)
        except subprocess.TimeoutExpired:
            print(f"FAIL label page: {os.path.basename(chrome)} did not finish within {args.timeout:g}s")
            return 1
        except OSError as error:
            print(f"FAIL label page: could not run {chrome}: {error}")
            return 1

        dom = run.stdout
        if not dom.strip():
            gpu = gpu_unusable(run.stderr)
            if gpu:
                print(f"SKIP label page: Chrome could not start its GPU process: {gpu}",
                      file=sys.stderr)
                print("SKIP label page: the label editor boot check did not run.")
                return 0
            tail = (run.stderr or "").strip().splitlines()[-5:]
            print("FAIL label page: Chrome rendered an empty document")
            for line in tail:
                print(f"  {line}")
            return 1

        tabs = block(dom, r'<div[^>]*role="tablist"[^>]*>', r"</div>")
        stage = block(dom, r'<section[^>]*\baria-label="Canvas"[^>]*>', r"</section>")
        layers = block(dom, r'<aside[^>]*\baria-label="Layers"[^>]*>', r"</aside>")

        tabs_text = strip_tags(tabs)
        stage_text = strip_tags(stage)
        layers_text = strip_tags(layers)
        objects = len(re.findall(r'data-obj-id="', stage))
        rows = len(re.findall(r'role="treeitem"', layers, re.I))
        tabs_count = len(re.findall(r'role="tab"', dom, re.I))
        surface = re.search(r'<div[^>]*class="[^"]*\bcanvas-surface\b[^"]*"[^>]*>', stage)

        check("the design's tab is painted", tabs_count >= 1 and "Untitled 1" in tabs_text,
              f"{tabs_count} tab(s): {tabs_text[:60]!r}")
        check("the layer list is painted", rows >= 4, f"{rows} row(s): {layers_text[:90]!r}")
        check("the stage has its objects", objects >= 3, f"{objects} object(s)")
        check(
            "the default slot values are on the stage",
            "Neon Skyline" in stage_text and "124" in stage_text,
            repr(stage_text[:100]),
        )
        check(
            "the stage is the print, not an empty box",
            bool(surface) and "width:" in surface.group(0) and "height:" in surface.group(0),
            "the stage has its print geometry",
        )

        if FAIL:
            print("\nthe label editor did not boot; the stage/list/tabs are above.")
            return 1
        print(f"label page: ok (stage {objects} objects, {rows} layers, {tabs_count} tab(s))")
        return 0
    finally:
        httpd.shutdown()
        httpd.server_close()
        shutil.rmtree(profile, ignore_errors=True)
        shutil.rmtree(root, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
