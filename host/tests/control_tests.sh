#!/bin/sh
# Opt-in MCP control build: the same server with XTAPE_MCP_CONTROL=1.
set -u
HERE=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "$HERE/.." && pwd)
BIN=${CONTROL_BIN:-$HERE/build/control/bin/icecast_host}
FIX="$HERE/fixtures"
PY=${PYTHON:-python3}
PORT=${CONTROL_PORT:-18090}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/xtape-control.XXXXXX")
PID=
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; rm -rf "$TMP"; }
trap cleanup EXIT INT TERM

if [ ! -x "$BIN" ]; then
    echo "missing $BIN; run: make -C host control"
    exit 1
fi
mkdir -p "$TMP/music"
$PY - "$FIX" "$TMP/music" <<'PY'
import shutil, sys
src, dst = sys.argv[1], sys.argv[2]
for i in range(600):
    shutil.copyfile(f"{src}/tone-44100.wav", f"{dst}/t{i:04d}.wav")
PY

HOST_LOG_LEVEL=error "$BIN" --music-dir "$TMP/music" --port "$PORT" >"$TMP/server.log" 2>&1 &
PID=$!
n=0
while [ "$n" -lt 80 ]; do
    if curl -s -o /dev/null --max-time 1 "http://127.0.0.1:$PORT/" 2>/dev/null; then break; fi
    n=$((n + 1)); sleep 0.1
done
if [ "$n" -ge 80 ]; then cat "$TMP/server.log"; exit 1; fi
$PY "$HERE/tests/mcp_control_tests.py" 127.0.0.1 "$PORT" 600
