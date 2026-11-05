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
usage: ./tools/build.sh <variant> [build|flash|monitor|reconfigure|fullclean ...]

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

actions=("$@")
if [ "${#actions[@]}" -eq 0 ]; then
  actions=(build)
fi

for action in "${actions[@]}"; do
  case "$action" in
    build | reconfigure | fullclean)
      idf.py "${args[@]}" "$action"
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
