#!/usr/bin/env bash
#
# Build, flash or monitor the K7Player's firmware.
#
#   ./player/build.sh [build|flash|monitor|fullclean]
#
# The project is its own ESP-IDF project on the classic ESP32 (ADR 0097), so it has its own
# sdkconfig and its own build directory; the target is set once, on the first build.
#
# Environment:
#   IDF_EXPORT   path to export.sh   (default ~/esp/esp-idf-6.1/export.sh)
#   PLAYER_PORT  serial device       (default /dev/cu.usbserial-8344420)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IDF_EXPORT="${IDF_EXPORT:-$HOME/esp/esp-idf-6.1/export.sh}"
PORT="${PLAYER_PORT:-/dev/cu.usbserial-8344420}"

[ -f "$IDF_EXPORT" ] || {
  echo "no ESP-IDF export at $IDF_EXPORT (set IDF_EXPORT)" >&2
  exit 1
}

action="${1:-build}"

# Pin the interpreter, for the reason tools/build.sh records: export.sh derives the virtualenv
# name from the `python3` on PATH, so a shell holding another IDF's export (or the system's
# 3.12 while the build directory was configured with 3.14) makes idf.py name a venv that was
# never installed and refuse to start. The caller's PATH must not decide this: pick one
# installed env, newest by name, and use it for both the interpreter and IDF_PYTHON_ENV_PATH.
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

# shellcheck disable=SC1090
. "$IDF_EXPORT" >/dev/null

cd "$ROOT"
[ -f sdkconfig ] || idf.py set-target esp32 >/dev/null

case "$action" in
  build) idf.py build ;;
  flash) idf.py -p "$PORT" flash ;;
  # --no-reset: a monitor attached to a playing player should watch it, not reboot it. A
  # flash have already reset the board; attaching a second reset used to cut the audio for
  # several seconds every time a console was opened.
  monitor) idf.py -p "$PORT" monitor --no-reset ;;
  fullclean) idf.py fullclean ;;
  *)
    echo "usage: $0 [build|flash|monitor|fullclean]" >&2
    exit 2
    ;;
esac
