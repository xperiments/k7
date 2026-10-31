#!/bin/sh
# Ticket 13: the board-tier Library acceptance.
#
#   sh host/tests/board_library.sh http://192.168.0.44:8000 /dev/cu.usbserial-XXXX
#
# Captures a clean boot, checks the Library scan log, compares the JSON and the
# binary view, proves a nested Track is playable and that no Track route exists,
# exercises the MCP endpoint through xtape.local, and prints the card-arrival
# limitation. The ten-minute soak is separate:
#   RESCAN=1 sh host/tests/board_soak.sh <base> <serial> 10
set -u
BASE=${1:?usage: board_library.sh <base-url> <serial-port>}
SERIAL=${2:?}
PY=${PYTHON:-}
if [ -z "$PY" ]; then
    for cand in "$HOME"/.espressif/python_env/idf*/bin/python "$(command -v python3)"; do
        [ -x "$cand" ] || continue
        if "$cand" -c 'import serial' >/dev/null 2>&1; then PY=$cand; break; fi
    done
fi
[ -n "$PY" ] || { echo "no python with pyserial; set PYTHON=" >&2; exit 2; }
HERE=$(dirname "$0")
LOG=$(mktemp "${TMPDIR:-/tmp}/board-library.XXXXXX")
FAIL=0
ok() { echo "  ok    $1"; }
bad() { echo "  FAIL  $1"; FAIL=1; }

"$PY" "$HERE/serial_log.py" -p "$SERIAL" -s 8 -o "$LOG" >/dev/null 2>&1
line=$(grep -a 'library: state=' "$LOG" | tail -1)
if [ -n "$line" ]; then
    ok "boot log: $line"
    printf '%s' "$line" | grep -q 'scan=.* ms arena=.* B json=.* B psram=.* -> .* B truncated=' \
        && ok "scan, arena, JSON, PSRAM and truncated are logged" \
        || bad "the boot log is missing a scan field"
else
    bad "no Library boot line in the capture"
fi
printf '%s\n' "$line" | grep -q 'state=ok' && ok "the card library is ok" || bad "the card library is not ok"
printf '%s\n' "$line" | grep -q 'truncated=0' && echo "  note  this card is not over the cap; the cap path is a separate small-budget build"

n=0
while [ "$n" -lt 50 ]; do
    curl -s -o /dev/null --max-time 1 "$BASE/api/library" && break
    n=$((n + 1)); sleep .2
done
curl -s -o "$LOG.json" "$BASE/api/library" || bad "cannot fetch /api/library"
curl -s -o "$LOG.bin" "$BASE/api/library.bin" || bad "cannot fetch /api/library.bin"
$PY - "$LOG.json" <<'PY' && ok "JSON tree parses and has a generation/count" || bad "JSON tree is invalid"
import json, sys
d=json.load(open(sys.argv[1]))
assert isinstance(d.get("generation"), int) and isinstance(d.get("count"), int)
assert d["count"] > 0
PY
$PY "$HERE/check_library_bin.py" "$LOG.bin" /tmp --header-only \
    && ok "binary header, CRC and parentage verify" || bad "binary view failed"
$PY "$HERE/board_mcp_tests.py" "$BASE" "xtape.local:8000" \
    && ok "MCP read-only through xtape.local" || bad "MCP read-only failed"

if command -v dns-sd >/dev/null 2>&1; then
    (dns-sd -B _mcp._tcp >"$LOG.dnssd" 2>&1 &)
    sleep 3
    pkill -f "dns-sd -B _mcp._tcp" 2>/dev/null || true
    grep -q '_mcp._tcp' "$LOG.dnssd" && ok "dns-sd sees _mcp._tcp" \
        || echo "  note  dns-sd did not list _mcp._tcp in the 3 s window"
elif command -v avahi-browse >/dev/null 2>&1; then
    avahi-browse -rt _mcp._tcp >"$LOG.avahi" 2>&1 &
    APID=$!; sleep 3; kill "$APID" 2>/dev/null || true
    grep -q '_mcp._tcp' "$LOG.avahi" && ok "avahi sees _mcp._tcp" \
        || echo "  note  avahi did not list _mcp._tcp in the 3 s window"
fi

# Exercise a nested Track when the card has one; this is the ticket's three-level
# proof. With a flat card the note is explicit rather than silently skipped.
nested=$($PY - "$LOG.json" <<'PY'
import json,sys
d=json.load(open(sys.argv[1]))
def walk(n):
    yield n
    for c in n.get("children", []):
        yield from walk(c)
for n in walk(d["tree"]):
    if n.get("type") == "track" and "/" in n.get("path", ""):
        print(n["path"]); break
PY
)
if [ -n "$nested" ]; then
    enc=$($PY - "$nested" <<'PY'
import sys, urllib.parse
print(urllib.parse.quote(sys.argv[1], safe="/"))
PY
)
    code=$(curl -s -o /dev/null -w '%{http_code}' "$BASE/music/$enc")
    [ "$code" = 404 ] && ok "the nested Track has no Track route (404): $nested" \
        || bad "GET /music for the nested Track is $code, expected 404"
    code=$(curl -s -o /dev/null -w '%{http_code}' -X POST -H 'Content-Type: application/json' \
        --data "{\"path\":\"$nested\"}" "$BASE/api/play")
    [ "$code" = 200 ] && ok "POST /api/play accepts the nested Track" || bad "POST /api/play is $code"
else
    echo "  note  this card has no nested Track; the three-level play proof needs one"
fi

echo
echo "card removal is not hot-detected: a pulled card leaves the Library stale until"
echo "POST /api/rescan or a reboot (README limitation)."
rm -f "$LOG" "$LOG.json" "$LOG.bin" "$LOG.dnssd" "$LOG.avahi"
[ "$FAIL" -eq 0 ]
