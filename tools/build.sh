#!/usr/bin/env bash
#
# Build, flash or monitor one firmware variant.
#
#   ./tools/build.sh <variant> [action...]
#
# Actions run in order and default to `build`. A variant builds into build/<variant>/ and
# keeps its generated config in sdkconfig.<variant> at the root (`default` uses sdkconfig).
# Only sdkconfig.defaults* is tracked, so a fresh checkout reconfigures from it.
#
#   sunton           the panel board (the one that ships)
#   backlight-test   the visual backlight test
#   dump             the screen dump build
#   capture          the screen capture build
#   verify           the panel with the capture on (screen over HTTP)
#   headless         the same chip with no panel driver, capture on
#   default          whatever the root sdkconfig says
#
# Environment:
#   IDF_EXPORT  path to export.sh   (default ~/esp/esp-idf-6.1/export.sh)
#   K7_PORT     serial device       (default /dev/cu.usbserial-8344440)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IDF_EXPORT="${IDF_EXPORT:-$HOME/esp/esp-idf-6.1/export.sh}"
PORT="${K7_PORT:-/dev/cu.usbserial-8344440}"

usage() {
  cat >&2 <<'USAGE'
usage: ./tools/build.sh <variant> [build|flash|monitor|reconfigure|fullclean|release ...]

variants: sunton, backlight-test, dump, capture, verify, headless, default
env:      IDF_EXPORT (default ~/esp/esp-idf-6.1/export.sh), K7_PORT
USAGE
  exit 2
}

variant="${1:-}"
shift || true
[ -n "$variant" ] || usage

case "$variant" in
  default)
    build_dir="build/default"
    sdkconfig=""
    defaults=""
    ;;
  sunton)
    build_dir="build/sunton"
    sdkconfig="sdkconfig.sunton"
    defaults="sdkconfig.defaults;sdkconfig.defaults.sunton"
    ;;
  backlight-test)
    build_dir="build/backlight-test"
    sdkconfig="sdkconfig.backlight-test"
    defaults="sdkconfig.defaults;sdkconfig.defaults.sunton;sdkconfig.defaults.backlight-test"
    ;;
  dump)
    build_dir="build/dump"
    sdkconfig="sdkconfig.dump"
    defaults="sdkconfig.defaults;sdkconfig.defaults.dump"
    ;;
  capture)
    build_dir="build/capture"
    sdkconfig="sdkconfig.capture"
    defaults="sdkconfig.defaults;sdkconfig.defaults.capture"
    ;;
  verify)
    build_dir="build/verify"
    sdkconfig="sdkconfig.verify"
    defaults="sdkconfig.defaults;sdkconfig.defaults.sunton;sdkconfig.defaults.capture"
    ;;
  headless)
    build_dir="build/headless"
    sdkconfig="sdkconfig.headless"
    defaults="sdkconfig.defaults;sdkconfig.defaults.capture"
    ;;
  *)
    echo "unknown variant: $variant" >&2
    usage
    ;;
esac

[ -f "$IDF_EXPORT" ] || {
  echo "no ESP-IDF export at $IDF_EXPORT (set IDF_EXPORT)" >&2
  exit 1
}

# Pin the interpreter. export.sh derives the virtualenv name from the `python3` on PATH, so a
# shell whose python3 is 3.12 while the build directory was configured with 3.14 makes idf.py
# refuse ("configured with ... / ... is currently active"). The caller's PATH must not decide
# this: pick one installed env, newest by name, and use it for both the interpreter and
# IDF_PYTHON_ENV_PATH so the build directory stays valid between shells.
idf_ver=$(basename "$(dirname "$IDF_EXPORT")" | sed -n 's/.*esp-idf-//p')
env_root="$HOME/.espressif/python_env"
chosen=$(ls -d "$env_root/idf${idf_ver}_py"*"_env" 2>/dev/null | sort -r | head -1 || true)
if [ -z "$chosen" ]; then
  echo "no ESP-IDF $idf_ver Python environment under $env_root." >&2
  echo "Create one with:" >&2
  echo "  python3 $(dirname "$IDF_EXPORT")/tools/idf_tools.py install-python-env" >&2
  exit 1
fi
export IDF_PYTHON_ENV_PATH="$chosen"
PATH="$chosen/bin:$PATH"
export PATH

cd "$ROOT"
# shellcheck disable=SC1090
source "$IDF_EXPORT"

args=(-B "$build_dir")
if [ -n "$sdkconfig" ]; then
  args+=(-D "SDKCONFIG=$sdkconfig")
fi
if [ -n "$defaults" ]; then
  args+=(-D "SDKCONFIG_DEFAULTS=$defaults")
fi

die() { echo "$*" >&2; exit 1; }

# A release is cut from a commit, never from a working tree (ADR 0079). The binaries and the
# manifest are what a person flashes, so the source they came from has to be the tag: a build
# from a dirty tree is a different program from the one the version names.
release_prepare() {
  [ "$variant" = "sunton" ] || die "release is cut for the sunton variant only"

  [ -f "$ROOT/VERSION" ] || die "no VERSION at the repository root"
  local version tag tracked untracked
  version="$(tr -d '[:space:]' < "$ROOT/VERSION")"
  [[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] \
    || die "VERSION is \"$version\", not a semver number"

  tracked="$(git -C "$ROOT" status --porcelain --untracked-files=no)"
  untracked="$(git -C "$ROOT" ls-files --others --exclude-standard -- \
    components main host tools web/src CMakeLists.txt)"
  if [ -n "$tracked$untracked" ]; then
    { printf '%s\n' "$tracked"; printf '%s\n' "$untracked"; } >&2
    die "the tree has uncommitted source; a release is built from a commit"
  fi

  tag="$(git -C "$ROOT" describe --exact-match --tags HEAD 2>/dev/null || true)"
  [ "$tag" = "v$version" ] \
    || die "HEAD is ${tag:-untagged}, VERSION is $version: tag v$version at HEAD first"
}

# The three parts and the manifest. The offsets are read from the build's own
# `flasher_args.json` rather than written here twice, and a part list that is not the three
# the flasher knows (k7-site.md F3) is refused instead of guessed at.
release_publish() {
  local version commit idf
  version="$(tr -d '[:space:]' < "$ROOT/VERSION")"
  commit="$(git -C "$ROOT" rev-parse HEAD)"
  idf="$(idf.py --version 2>/dev/null | head -1 || true)"
  [ -n "$idf" ] || die "could not read the IDF version from idf.py --version"

  python3 - "$ROOT" "$version" "$commit" "$idf" <<'PY'
import datetime
import hashlib
import json
import os
import shutil
import sys

root, version, commit, idf = sys.argv[1:5]
build = os.path.join(root, "build", "sunton")
args = json.load(open(os.path.join(build, "flasher_args.json")))
flash = args["flash_files"]

expected = {"0x0", "0x8000", "0x10000"}
if set(flash) != expected:
    sys.exit(
        f"release: flasher_args.json carries {sorted(flash)}, "
        f"expected {sorted(expected)} (k7-site.md F3)"
    )

dest = os.path.join(root, "web", "public", "firmware", version)
os.makedirs(dest, exist_ok=True)

parts = []
for offset, relative in sorted(flash.items(), key=lambda item: int(item[0], 16)):
    source = os.path.join(build, relative)
    if not os.path.isfile(source):
        sys.exit(f"release: missing {source}; build the sunton variant first")
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

manifest = {
    "version": version,
    "commit": commit,
    "idf": idf,
    "chip": "esp32s3",
    "flash": {"mode": "dio", "size": "16MB", "freq": "80m"},
    "built_at": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    "parts": parts,
}
with open(os.path.join(root, "web", "firmware.json"), "w") as handle:
    json.dump(manifest, handle, indent=2)
    handle.write("\n")

print(f"release: K7 {version} ({commit[:12]})")
for part in parts:
    print(
        f"  {part['offset']:#07x}  {part['size']:>9} B  "
        f"{part['sha256'][:16]}...  {part['file']}"
    )
print(f"  wrote web/firmware.json and {len(parts)} parts in web/public/firmware/{version}/")
print("  nothing is committed: review and commit them with the change")
PY
}

actions=("$@")
if [ "${#actions[@]}" -eq 0 ]; then
  actions=(build)
fi

for action in "${actions[@]}"; do
  case "$action" in
    build | reconfigure | fullclean)
      idf.py "${args[@]}" "$action"
      ;;
    release)
      release_prepare
      idf.py "${args[@]}" build
      release_publish
      ;;
    flash | monitor)
      if pids="$(lsof -t "$PORT" 2>/dev/null)"; then
        # shellcheck disable=SC2086
        kill $pids 2>/dev/null || true
      fi
      idf.py "${args[@]}" -p "$PORT" "$action"
      ;;
    *)
      echo "unknown action: $action" >&2
      usage
      ;;
  esac
done
