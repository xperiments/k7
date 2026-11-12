#!/usr/bin/env python3
"""Open the label canvas builder in headless Chrome and prove it booted.

The page is one self-contained HTML file with no module boundary and no test. On
905313d a `const` sat below the first `defaultTpl()` call that reads it, so the
page threw a TDZ ReferenceError while loading and painted nothing: the tabs were
empty, the layer list was empty and the stage had no objects. Every host check
stayed green because none of them opened the page.

This is that missing seam. It opens the page with a fresh Chrome profile, reads
the rendered DOM, and requires what the boot paints: the tabs, the layer list,
and the stage with its default slot values. A page that throws before the tail
of its own script leaves all three empty, so the check fails for the right
reason. When Chrome is not installed it skips loudly and succeeds, so a machine
without a browser is not a failed host check.

Usage: label_boot_tests.py [PAGE] [--timeout SECONDS]
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

FAIL = 0

DEFAULT_PAGE = Path(__file__).resolve().parents[2] / "resources" / "LVGL canvas builder.html"


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


def count_class(html, name):
    return len(re.findall(r'class="[^"]*\b' + re.escape(name) + r'\b[^"]*"', html))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("page", nargs="?", default=str(DEFAULT_PAGE))
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()

    page = Path(args.page).resolve()
    if not page.is_file():
        print(f"FAIL label page: {page} does not exist")
        return 1

    chrome = find_chrome()
    if chrome is None:
        print("SKIP label page: no Chrome found (set CHROME to a browser binary).", file=sys.stderr)
        print("SKIP label page: the canvas builder boot check did not run.")
        return 0

    profile = tempfile.mkdtemp(prefix="k7-label-boot-")
    try:
        command = [
            chrome,
            "--headless=new",
            "--disable-gpu",
            "--no-first-run",
            "--no-default-browser-check",
            "--allow-file-access-from-files",
            f"--user-data-dir={profile}",
            "--virtual-time-budget=8000",
            "--dump-dom",
            page.as_uri(),
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
            tail = (run.stderr or "").strip().splitlines()[-5:]
            print("FAIL label page: Chrome rendered an empty document")
            for line in tail:
                print(f"  {line}")
            return 1

        tabs = block(dom, r'<nav[^>]*\bid="tabs"[^>]*>', r"</nav>")
        stage = block(dom, r'<div[^>]*\bid="stage"[^>]*>', r"<canvas")
        layers = block(dom, r'<ul[^>]*\bid="list"[^>]*>', r"</ul>")

        tabs_text = strip_tags(tabs)
        stage_text = strip_tags(stage)
        layers_text = strip_tags(layers)
        objects = count_class(stage, "o")
        rows = len(re.findall(r"<li\b", layers, re.I))
        tabs_count = count_class(tabs, "tab")

        check("the tabs are painted", tabs_count >= 1, f"{tabs_count} tab(s): {tabs_text[:60]!r}")
        check("the layer list is painted", rows >= 3, f"{rows} row(s): {layers_text[:90]!r}")
        check("the stage has its objects", objects >= 3, f"{objects} object(s)")
        check(
            "the default slot values are on the stage",
            bool(re.search(r"[0-9A-Za-z]", stage_text)),
            repr(stage_text[:100]),
        )
        check(
            "the stage is the print, not an empty box",
            "width:" in stage and "height:" in stage,
            "the stage has its print geometry",
        )

        if FAIL:
            print("\nthe canvas builder did not boot; the stage/list/tabs are above.")
            return 1
        print(f"label page: ok (stage {objects} objects, {rows} layers, {tabs_count} tab(s))")
        return 0
    finally:
        shutil.rmtree(profile, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
