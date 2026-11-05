#!/bin/sh

# This suite judges the device's HTTP surface with no page: `/` is a 404 and `/library`
# is the index.
# Host acceptance tests for the HTTP surface: T3, T4, T5, T6, T7, T8, T9, T12, T14, T15, T16 and T17,
# run against the real server on this machine with no board attached.
#
#   HOST_BIN=host/build/bin/icecast_host HOST_DIR=host sh host/tests/http_tests.sh
#
# Every assertion here is also meant to be run against the board (two-tier
# acceptance). The four things a host run cannot establish - encoder CPU at
# 240 MHz, memory fit, microSD contention and FATFS behaviour - are called out in
# the specification and are not claimed by anything in this file.
set -u

HOST_DIR=${HOST_DIR:?set HOST_DIR to the host/ directory}
# Only the host run needs the built server; a board run points at the device and
# never starts a local process.
HOST_BIN=${HOST_BIN:-$HOST_DIR/build/bin/icecast_host}
FIX="$HOST_DIR/fixtures"
BOARD_URL=${BOARD_URL:-}     # set to http://<device>:8000 for the board run
SERIAL_LOG=${SERIAL_LOG:-}   # optional: a serial capture from the device
PORT=${PORT:-18077}
PORT2=$((PORT + 1))
PY=${PYTHON:-python3}
BASE="http://127.0.0.1:$PORT"
if [ -n "$BOARD_URL" ]; then
    BASE=${BOARD_URL%/}
fi
SRV_HOST=$(printf '%s' "$BASE" | sed -e 's#^http://##' -e 's#:.*##')
SRV_PORT=$(printf '%s' "$BASE" | sed -e 's#^http://##' -e 's#^[^:]*:##' -e 's#/.*##')
[ -n "$SRV_PORT" ] || SRV_PORT=80
TMP=$(mktemp -d "${TMPDIR:-/tmp}/xtape-http-tests.XXXXXX")

PASS=0
FAIL=0
PIDS=""

cleanup() {
    for p in $PIDS; do kill "$p" 2>/dev/null; done
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

ok() { PASS=$((PASS + 1)); printf '  ok    %s\n' "$1"; }
bad() { FAIL=$((FAIL + 1)); printf '  FAIL  %s\n' "$1"; }
group() { printf '\n%s\n' "$1"; }

check_eq() { if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 (got '$2', want '$3')"; fi; }
check_match() { if printf '%s' "$2" | grep -qE "$3"; then ok "$1"; else bad "$1 (no /$3/ in: $2)"; fi; }
check_no_match() { if printf '%s' "$2" | grep -qE "$3"; then bad "$1 (found /$3/ in: $2)"; else ok "$1"; fi; }
check_lt() { if awk "BEGIN{exit !($2 < $3)}"; then ok "$1"; else bad "$1 ($2 is not < $3)"; fi; }

start_server() { # dir port log [extra args...]
    dir=$1; port=$2; log=$3; shift 3
    HOST_LOG_LEVEL=info "$HOST_BIN" --music-dir "$dir" --port "$port" "$@" >"$log" 2>&1 &
    pid=$!
    PIDS="$PIDS $pid"
    n=0
    while [ "$n" -lt 80 ]; do
        if curl -s -o /dev/null --max-time 1 "http://127.0.0.1:$port/" 2>/dev/null; then LAST_PID=$pid; return 0; fi
        n=$((n + 1))
        sleep 0.1
    done
    printf 'server on port %s did not come up; its log:\n' "$port"
    cat "$log"
    return 1
}

start_no_card_server() { # dir port log [extra args...]
    dir=$1; port=$2; log=$3; shift 3
    XTAPE_HOST_NO_CARD=1 HOST_LOG_LEVEL=info "$HOST_BIN" --music-dir "$dir" --port "$port" "$@" >"$log" 2>&1 &
    pid=$!
    PIDS="$PIDS $pid"
    n=0
    while [ "$n" -lt 80 ]; do
        if curl -s -o /dev/null --max-time 1 "http://127.0.0.1:$port/" 2>/dev/null; then LAST_PID=$pid; return 0; fi
        n=$((n + 1))
        sleep 0.1
    done
    printf 'no-card server on port %s did not come up; its log:\n' "$port"
    cat "$log"
    return 1
}

run_expect_refusal() { # description log port [args...]
    desc=$1; log=$2; port=$3; shift 3
    "$HOST_BIN" --port "$port" "$@" >"$log" 2>&1 &
    p=$!
    sleep 1
    if kill -0 "$p" 2>/dev/null; then
        bad "$desc (still running: a refusal must exit)"
        kill "$p" 2>/dev/null
        return 1
    fi
    wait "$p" 2>/dev/null
    rc=$?
    check_eq "$desc exits non-zero" "$([ "$rc" -ne 0 ] && echo yes || echo no)" "yes"
}

# --------------------------------------------------------------- start talking

group "starting the server against the fixture corpus"
if [ -n "$BOARD_URL" ]; then
    if ! curl -s -o /dev/null --max-time 8 "$BASE/library"; then
        printf 'the board at %s is not answering\n' "$BASE"
        exit 1
    fi
    ok "board answering on $BASE (no local server started)"
    if [ -n "$SERIAL_LOG" ]; then
        STARTUP_LOG=$SERIAL_LOG
        ok "reading the device's own startup log from $SERIAL_LOG"
    else
        STARTUP_LOG=/dev/null
        printf '  note  no SERIAL_LOG given: the startup-log assertions are skipped\n'
    fi
else
    if [ ! -x "$HOST_BIN" ]; then
        printf '%s is not built: make -C host (or set BOARD_URL for a board run)\n' "$HOST_BIN"
        exit 1
    fi
    start_server "$FIX" "$PORT" "$TMP/srv.log" || exit 1
    SRV_PID=$LAST_PID
    STARTUP_LOG=$TMP/srv.log
    ok "server up on $BASE"
fi

# The station format comes from the server's own startup log: the host build and
# the device are configured independently, and the interesting assertion is that
# the wire matches what was announced.
STATION_HZ=$(tr -d '\r' <"$STARTUP_LOG" | sed -n 's/.*station format: \([0-9]*\) Hz.*/\1/p' | head -1)
STATION_KBPS=$(tr -d '\r' <"$STARTUP_LOG" | sed -n 's/.*station format: [0-9]* Hz, \([0-9]*\) kbps.*/\1/p' | head -1)
if [ -z "$STATION_HZ" ] || [ -z "$STATION_KBPS" ]; then
    STATION_HZ=44100
    STATION_KBPS=128
    printf '  note  no station format in the startup log; assuming %s Hz / %s kbps\n' "$STATION_HZ" "$STATION_KBPS"
fi

group "T3  the live stream is continuous MPEG audio, not WAV"
curl -s --max-time 6 "$BASE/stream" >"$TMP/stream.mp3"
head4=$(head -c 4 "$TMP/stream.mp3" | od -An -tx1 | tr -d ' \n')
check_match "the capture starts on a frame sync (${head4})" "$head4" '^fff' 
probe=$(ffprobe -hide_banner -v error -show_entries stream=codec_name,sample_rate,channels,bit_rate -of default=nw=1 "$TMP/stream.mp3" 2>&1)
check_match "ffprobe sees MPEG audio" "$probe" '^codec_name=mp3$'
check_match "at the announced station rate ($STATION_HZ Hz)" "$probe" "^sample_rate=$STATION_HZ$"
check_match "in stereo" "$probe" '^channels=2$'
check_match "at the announced bit rate ($STATION_KBPS kbps)" "$probe" "^bit_rate=$((STATION_KBPS * 1000))$"
errors=$(ffmpeg -v error -i "$TMP/stream.mp3" -f null - 2>&1 | wc -l | tr -d ' ')
check_eq "the whole capture decodes without a single error" "$errors" "0"
if [ -s "$STARTUP_LOG" ]; then
    check_match "the station format is logged at startup (MPEG-1 joint stereo)" "$(tr -d '\r' <"$STARTUP_LOG")" "station format: $STATION_HZ Hz, $STATION_KBPS kbps CBR, joint stereo, MPEG-1"
    check_match "and names the encoder's state size" "$(tr -d '\r' <"$STARTUP_LOG")" '[0-9]+ B of state inside the vendored library'
    check_match "and reports which memory the ring took" "$(tr -d '\r' <"$STARTUP_LOG")" 'burst ring: .* in external memory'
    check_match "and which memory the encoder staging took" "$(tr -d '\r' <"$STARTUP_LOG")" 'staging buffers in external memory'
    check_match "the library scan is logged with counts, bytes and memory" "$(tr -d '\r' <"$STARTUP_LOG")" 'library: state=[a-z_]+ tracks=[0-9]+ dirs=[0-9]+ bytes=[0-9]+ scan=[0-9]+ ms arena=[0-9]+ B json=[0-9]+ B psram=[0-9]+ -> [0-9]+ B'
    if [ -n "$BOARD_URL" ]; then
        # These two come from the firmware's own bring-up, which the host build
        # does not compile: they only exist on the device.
        check_match "and the free memory in both classes (P2)" "$(tr -d '\r' <"$STARTUP_LOG")" 'heap at startup: internal [0-9]+ B free, external [0-9]+ B free'
        check_match "and that PSRAM really is octal and running" "$(tr -d '\r' <"$STARTUP_LOG")" 'psram: [0-9]+ KB \(octal mode'
    fi
fi

group "T5  ICY metadata lands at exactly 16000 encoded bytes"
curl -s --max-time 6 -D "$TMP/h5" -o "$TMP/b5" -H 'Icy-MetaData: 1' "$BASE/stream"
h5=$(tr -d '\r' <"$TMP/h5")
check_match "icy-metaint is advertised" "$h5" '^icy-metaint: 16000$'
check_match "Content-Type is audio/mpeg" "$h5" '^Content-Type: audio/mpeg$'
check_match "the station name is present" "$h5" '^icy-name:.+'
check_match "in canonical form: no space after the colon" "$h5" '^icy-name:[^ ]'
check_no_match "and not the non-canonical form" "$h5" '^icy-name: '
if $PY "$HOST_DIR/tests/check_metadata.py" "$TMP/b5" 16000 >"$TMP/meta.out" 2>&1; then
    ok "$(cat "$TMP/meta.out")"
else
    bad "metadata framing: $(cat "$TMP/meta.out")"
fi

group "T4  a client that says nothing cannot stall the next one (D1)"
$PY "$HOST_DIR/tests/silent_client.py" "$SRV_HOST" "$SRV_PORT" 4 &
silent=$!
sleep 0.6
code=$(curl -s -o /dev/null --max-time 5 -w '%{http_code}' "$BASE/library")
ttfb=$(curl -s -o /dev/null --max-time 5 -w '%{time_total}' "$BASE/library")
check_eq "the second client is served" "$code" "200"
check_lt "and served at once (${ttfb}s, not after a timeout)" "$ttfb" "2.0"
stream_ttfb=$(curl -s -o /dev/null --max-time 3 -w '%{time_starttransfer}' "$BASE/stream")
check_lt "the stream's headers also arrive at once (${stream_ttfb}s)" "$stream_ttfb" "2.0"
kill "$silent" 2>/dev/null

# On the board the card may hold the owner's own music, so the index is judged as a
# superset of the fixtures there and exactly on the host.
if [ -n "$BOARD_URL" ]; then INDEX_MODE=--superset; else INDEX_MODE=; fi
group "T6  the index lists the library"
curl -s "$BASE/library" >"$TMP/index.html"
if $PY "$HOST_DIR/tests/check_index.py" "$TMP/index.html" "$FIX" $INDEX_MODE >"$TMP/index.out" 2>&1; then
    ok "$(cat "$TMP/index.out")"
else
    bad "index: $(cat "$TMP/index.out")"
fi
check_match "the page says what is playing" "$(cat "$TMP/index.html")" 'Now playing:'
check_match "and links the live stream" "$(cat "$TMP/index.html")" 'href="/stream"'
check_match "and serves a content type a browser can render" "$(curl -s -D - -o /dev/null "$BASE/library")" 'Content-Type: text/html; charset=utf-8'

group "T14  the prebuilt library is served without the card"
curl -s -D "$TMP/libhdr" -o "$TMP/lib.json" "$BASE/api/library"
LIB_HDR=$(tr -d '\r' <"$TMP/libhdr")
LIB_BYTES=$(wc -c <"$TMP/lib.json" | tr -d ' ')
check_eq "GET /api/library is a 200" "$(printf '%s' "$LIB_HDR" | head -1 | awk '{print $2}')" "200"
check_match "and is typed application/json" "$LIB_HDR" '^Content-Type: application/json$'
check_match "and carries a real Content-Length ($LIB_BYTES B)" "$LIB_HDR" "^Content-Length: $LIB_BYTES$"
if $PY -m json.tool "$TMP/lib.json" >/dev/null 2>&1; then
    ok "the body is valid JSON"
else
    bad "the body is not valid JSON"
fi
LIB_GEN=$($PY -c 'import json,sys; print(json.load(open(sys.argv[1]))["generation"])' "$TMP/lib.json")
check_match "and names the generation ($LIB_GEN)" "$LIB_HDR" "^ETag: \"gen-$LIB_GEN\"$"
LIB_ETAG=$(printf '%s\n' "$LIB_HDR" | sed -n 's/^ETag: //p')

curl -s -D "$TMP/binhdr" -o "$TMP/lib.bin" "$BASE/api/library.bin"
BIN_HDR=$(tr -d '\r' <"$TMP/binhdr")
BIN_BYTES=$(wc -c <"$TMP/lib.bin" | tr -d ' ')
check_eq "GET /api/library.bin is a 200" "$(printf '%s' "$BIN_HDR" | head -1 | awk '{print $2}')" "200"
check_match "and is typed application/octet-stream" "$BIN_HDR" '^Content-Type: application/octet-stream$'
check_match "and carries a real Content-Length ($BIN_BYTES B)" "$BIN_HDR" "^Content-Length: $BIN_BYTES$"
check_eq "and shares the JSON generation" "$(printf '%s\n' "$BIN_HDR" | sed -n 's/^ETag: //p')" "$LIB_ETAG"
check_match "and is offered as an attachment" "$BIN_HDR" '^Content-Disposition: attachment; filename="library.bin"$'
if [ -n "$BOARD_URL" ]; then
    if $PY "$HOST_DIR/tests/check_library_bin.py" "$TMP/lib.bin" / --header-only >"$TMP/bin.out" 2>&1; then
        ok "$(cat "$TMP/bin.out")"
    else
        bad "board binary header: $(cat "$TMP/bin.out")"
    fi
elif $PY "$HOST_DIR/tests/check_library_bin.py" "$TMP/lib.bin" "$FIX" --state ok >"$TMP/bin.out" 2>&1; then
    ok "$(cat "$TMP/bin.out")"
else
    bad "binary view: $(cat "$TMP/bin.out")"
fi
BIN_GEN=$(sed -n 's/.*generation \([0-9]*\).*/\1/p' "$TMP/bin.out" | head -1)
check_eq "the binary header and the JSON body share generation" "$BIN_GEN" "$LIB_GEN"

if [ -z "$BOARD_URL" ]; then
    # A single flipped byte must fail the independent CRC check.
    cp "$TMP/lib.bin" "$TMP/lib-corrupt.bin"
    $PY -c 'import sys; p=sys.argv[1]; b=bytearray(open(p,"rb").read()); b[64+26]^=1; open(p,"wb").write(b)' "$TMP/lib-corrupt.bin"
    if $PY "$HOST_DIR/tests/check_library_bin.py" "$TMP/lib-corrupt.bin" "$FIX" --state ok >/dev/null 2>&1; then
        bad "a corrupted blob passed the CRC check"
    else
        ok "a corrupted blob fails the independent CRC check"
    fi
fi
if [ -n "$BOARD_URL" ]; then
    if $PY -c 'import json,sys; d=json.load(open(sys.argv[1])); assert all(k in d for k in ("root","generation","count","bytes","state","truncated","error","tree")); assert d["state"] in ("ok","empty","no_card","no_dir","error")' "$TMP/lib.json" 2>/dev/null; then
        ok "board library JSON has the expected shape"
    else
        bad "board library JSON shape"
    fi
elif $PY "$HOST_DIR/tests/check_library.py" "$TMP/lib.json" "$FIX" --state ok >"$TMP/lib.out" 2>&1; then
    ok "$(cat "$TMP/lib.out")"
else
    bad "library JSON: $(cat "$TMP/lib.out")"
fi

if [ -z "$BOARD_URL" ]; then
    # The endpoint must answer from the boot-built JSON even after the file is gone.
    mkdir -p "$TMP/frozen"
    cp -R "$FIX/." "$TMP/frozen/"
    if start_server "$TMP/frozen" 18101 "$TMP/frozen.log"; then
        curl -s -o "$TMP/frozen.bin.before" "http://127.0.0.1:18101/api/library.bin"
        rm -f "$TMP/frozen/tone-44100.flac"
        curl -s -D "$TMP/frozenhdr" -o "$TMP/frozen.json" "http://127.0.0.1:18101/api/library"
        curl -s -o "$TMP/frozen.bin.after" "http://127.0.0.1:18101/api/library.bin"
        if cmp -s "$TMP/frozen.bin.before" "$TMP/frozen.bin.after"; then
            ok "the binary view is byte-identical after a post-boot deletion"
        else
            bad "the binary view changed after a post-boot deletion"
        fi
        check_match "the JSON still lists a file deleted after boot" "$(cat "$TMP/frozen.json")" '"name":"tone-44100.flac"'
        check_eq "and its Content-Length still matches the body" \
            "$(tr -d '\r' <"$TMP/frozenhdr" | sed -n 's/^Content-Length: //p')" \
            "$(wc -c <"$TMP/frozen.json" | tr -d ' ')"
        curl -s -o "$TMP/frozen.html" "http://127.0.0.1:18101/library"
        check_match "the index still lists the deleted file from the tree" "$(cat "$TMP/frozen.html")" 'tone-44100\.flac'
    fi

    # A nested tree through the endpoint: paths and playlist order, not just
    # a flat directory.
    mkdir -p "$TMP/nested/Album/Deep"
    cp "$FIX/tone-44100.flac" "$TMP/nested/Album/01 - Song.flac"
    cp "$FIX/tone-44100.wav" "$TMP/nested/Album/Deep/02 - Song.wav"
    cp "$FIX/tone-44100.mp3" "$TMP/nested/loose.mp3"
    if start_server "$TMP/nested" 18106 "$TMP/nested.log"; then
        curl -s -o "$TMP/nested.json" "http://127.0.0.1:18106/api/library"
        if $PY "$HOST_DIR/tests/check_library.py" "$TMP/nested.json" "$TMP/nested" --state ok >"$TMP/nested.out" 2>&1; then
            ok "a nested tree matches os.walk() through /api/library"
        else
            bad "nested tree: $(cat "$TMP/nested.out")"
        fi
        check_match "and its paths use the tree, not the flat directory" "$(cat "$TMP/nested.json")" '"path":"Album/01 - Song.flac"'
        curl -s -o "$TMP/nested.html" "http://127.0.0.1:18106/library"
        if $PY "$HOST_DIR/tests/check_index.py" "$TMP/nested.html" "$TMP/nested" >"$TMP/nestedindex.out" 2>&1; then
            ok "$(cat "$TMP/nestedindex.out")"
        else
            bad "nested index: $(cat "$TMP/nestedindex.out")"
        fi
        curl -s -o "$TMP/mcpnested.json" -X POST -H 'Content-Type: application/json' \
            --data '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"library_track","arguments":{"path":"Album/Deep/02 - Song.wav"}}}' \
            "http://127.0.0.1:18106/mcp"
        check_match "MCP resolves a track three levels deep" "$(cat "$TMP/mcpnested.json")" 'Album/Deep/02 - Song.wav'
        curl -s -o "$TMP/nested.bin" "http://127.0.0.1:18106/api/library.bin"
        if $PY "$HOST_DIR/tests/check_library_bin.py" "$TMP/nested.bin" "$TMP/nested" --state ok >"$TMP/nestedbin.out" 2>&1; then
            ok "the binary view reconstructs the nested tree"
        else
            bad "nested binary: $(cat "$TMP/nestedbin.out")"
        fi
    fi

    # Boot-time states: no card, no Music directory, empty, truncated.
    if start_no_card_server "$FIX" 18102 "$TMP/nocard.log"; then
        code=$(curl -s -o "$TMP/nocard.json" -w '%{http_code}' "http://127.0.0.1:18102/api/library")
        check_eq "no card is a 200, not a 500" "$code" "200"
        if $PY "$HOST_DIR/tests/check_library.py" "$TMP/nocard.json" "$FIX" --state no_card >"$TMP/nocard.out" 2>&1; then
            ok "no card is a diagnosable 200 with state no_card"
        else
            bad "no card state: $(cat "$TMP/nocard.out")"
        fi
        check_match "and the boot log names it" "$(cat "$TMP/nocard.log")" 'library: state=no_card '
        curl -s -o "$TMP/nocard.html" "http://127.0.0.1:18102/library"
        check_match "the index says no card" "$(cat "$TMP/nocard.html")" 'No card mounted'
        curl -s -o "$TMP/nocard.bin" "http://127.0.0.1:18102/api/library.bin"
        if $PY "$HOST_DIR/tests/check_library_bin.py" "$TMP/nocard.bin" "$FIX" --state no_card >"$TMP/nocardbin.out" 2>&1; then
            ok "the no-card binary is an empty root"
        else
            bad "no-card binary: $(cat "$TMP/nocardbin.out")"
        fi
    fi
    mkdir -p "$TMP/no-music-parent"
    if start_server "$TMP/no-music-parent/missing" 18103 "$TMP/nodir.log"; then
        code=$(curl -s -o "$TMP/nodir.json" -w '%{http_code}' "http://127.0.0.1:18103/api/library")
        check_eq "missing directory is a 200, not a 500" "$code" "200"
        if $PY "$HOST_DIR/tests/check_library.py" "$TMP/nodir.json" "$TMP/no-music-parent/missing" --state no_dir >"$TMP/nodir.out" 2>&1; then
            ok "a missing Music directory is a diagnosable 200 with state no_dir"
        else
            bad "missing directory state: $(cat "$TMP/nodir.out")"
        fi
        curl -s -o "$TMP/nodir.html" "http://127.0.0.1:18103/library"
        check_match "the index says the music directory is missing" "$(cat "$TMP/nodir.html")" 'No music directory'
        curl -s -o "$TMP/nodir.bin" "http://127.0.0.1:18103/api/library.bin"
        if $PY "$HOST_DIR/tests/check_library_bin.py" "$TMP/nodir.bin" "$TMP/no-music-parent/missing" --state no_dir >"$TMP/nodirbin.out" 2>&1; then
            ok "the missing-directory binary is an empty root"
        else
            bad "missing-directory binary: $(cat "$TMP/nodirbin.out")"
        fi
    fi
    mkdir -p "$TMP/empty"
    if start_server "$TMP/empty" 18104 "$TMP/empty.log"; then
        code=$(curl -s -o "$TMP/empty.json" -w '%{http_code}' "http://127.0.0.1:18104/api/library")
        check_eq "an empty library is a 200" "$code" "200"
        if $PY "$HOST_DIR/tests/check_library.py" "$TMP/empty.json" "$TMP/empty" --state empty >"$TMP/empty.out" 2>&1; then
            ok "an empty Music directory is a diagnosable 200 with state empty"
        else
            bad "empty state: $(cat "$TMP/empty.out")"
        fi
        curl -s -o "$TMP/empty.html" "http://127.0.0.1:18104/library"
        check_match "the index says there are no tracks" "$(cat "$TMP/empty.html")" 'contains no tracks'
        curl -s -o "$TMP/empty.bin" "http://127.0.0.1:18104/api/library.bin"
        if $PY "$HOST_DIR/tests/check_library_bin.py" "$TMP/empty.bin" "$TMP/empty" --state empty >"$TMP/emptybin.out" 2>&1; then
            ok "the empty binary is an empty root"
        else
            bad "empty binary: $(cat "$TMP/emptybin.out")"
        fi
    fi
    if start_server "$FIX" 18105 "$TMP/trunc.log" --library-max-tracks 1; then
        code=$(curl -s -o "$TMP/trunc.json" -w '%{http_code}' "http://127.0.0.1:18105/api/library")
        check_eq "a truncated library is a 200" "$code" "200"
        if $PY "$HOST_DIR/tests/check_library.py" "$TMP/trunc.json" "$FIX" --state ok --truncated >"$TMP/trunc.out" 2>&1; then
            ok "a capped boot is a 200, state ok and truncated true"
        else
            bad "truncated view: $(cat "$TMP/trunc.out")"
        fi
        check_match "and the body says truncated" "$(cat "$TMP/trunc.json")" '"truncated":true'
        curl -s -o "$TMP/trunc.html" "http://127.0.0.1:18105/library"
        check_match "the index notes the truncation" "$(cat "$TMP/trunc.html")" 'truncated at 1 tracks'
        curl -s -o "$TMP/trunc.bin" "http://127.0.0.1:18105/api/library.bin"
        if $PY "$HOST_DIR/tests/check_library_bin.py" "$TMP/trunc.bin" "$FIX" --state ok --truncated --subset >"$TMP/truncbin.out" 2>&1; then
            ok "the truncated binary carries the same prefix"
        else
            bad "truncated binary: $(cat "$TMP/truncbin.out")"
        fi
    fi
fi

group "T15  POST /api/play selects a Track"
if [ -z "$BOARD_URL" ]; then
    mkdir -p "$TMP/play/Album/Deep"
    $PY - "$TMP/play" <<'PYX'
import os, sys, wave
root = sys.argv[1]
for rel in ("Album/Deep/three.wav", "Album/one.wav", "two.wav"):
    path = os.path.join(root, rel)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes(b"\0\0" * (44100 * 8))
PYX
    if start_server "$TMP/play" 18107 "$TMP/play.log"; then
        if $PY "$HOST_DIR/tests/post_play.py" 127.0.0.1 18107 >"$TMP/play.out" 2>&1; then
            cat "$TMP/play.out"
            ok "POST /api/play wire cases all passed"
        else
            cat "$TMP/play.out"
            bad "POST /api/play wire cases failed"
        fi
        curl -s -o "$TMP/playing.html" "http://127.0.0.1:18107/library"
        check_match "the index now names the selected Track" "$(cat "$TMP/playing.html")" 'Now playing: <strong>two</strong>'
        check_eq "a POST to a GET-only route is a 405" \
            "$(curl -s -o /dev/null -w '%{http_code}' -X POST -H 'Content-Type: application/json' --data '{}' "http://127.0.0.1:18107/api/library")" "405"
        check_eq "a POST to an unknown path is a 404" \
            "$(curl -s -o /dev/null -w '%{http_code}' -X POST -H 'Content-Type: application/json' --data '{}' "http://127.0.0.1:18107/nope")" "404"
        check_eq "the live stream still answers after an unknown play" \
            "$(curl -s -o /dev/null --max-time 3 -w '%{http_code}' "http://127.0.0.1:18107/stream")" "200"
    else
        bad "the POST /api/play fixture server did not start"
    fi
fi

group "T16  the read-only MCP endpoint"
if [ -n "$BOARD_URL" ]; then
    printf '  skip  MCP acceptance is host-only (the fixture count is the oracle)\n'
elif $PY "$HOST_DIR/tests/mcp_tests.py" "$SRV_HOST" "$SRV_PORT" >"$TMP/mcp.out" 2>&1; then
    cat "$TMP/mcp.out"
    ok "MCP initialize/tools/error table all passed"
else
    cat "$TMP/mcp.out"
    bad "MCP acceptance failed"
fi

group "T17  rescan generations, leases and the card-less state"
MUT="$TMP/rescan-music"
mkdir -p "$MUT"
$PY - "$FIX" "$MUT" <<'PYEOF'
import shutil, sys
src, dst = sys.argv[1], sys.argv[2]
for i in range(3000):
    shutil.copyfile(f"{src}/tiny-44100.wav", f"{dst}/t{i:04d}.wav")
PYEOF
start_server "$MUT" "$((PORT + 2))" "$TMP/rescan.log"
if $PY "$HOST_DIR/tests/rescan_tests.py" 127.0.0.1 "$((PORT + 2))" "$MUT" 3000 >"$TMP/rescan.out" 2>&1; then
    cat "$TMP/rescan.out"
    ok "rescan generations, the slow reader and the broadcast switch all passed"
else
    cat "$TMP/rescan.out"
    bad "rescan acceptance failed"
fi

EMPTY="$TMP/rescan-empty"
mkdir -p "$EMPTY"
start_no_card_server "$EMPTY" "$((PORT + 3))" "$TMP/rescan-nocard.log"
check_match "a card-less rescan stays no_card" \
    "$(curl -s "http://127.0.0.1:$((PORT + 3))/api/library")" '"state":"no_card"'
curl -s -o "$TMP/rescan-nocard.out" -X POST -H 'Content-Type: application/json' --data '{}' \
    "http://127.0.0.1:$((PORT + 3))/api/rescan"
sleep 1
check_match "and its new generation is still no_card" \
    "$(curl -s "http://127.0.0.1:$((PORT + 3))/api/library")" '"generation":2.*"state":"no_card"'
check_match "the card-less index stays diagnosable" \
    "$(curl -s "http://127.0.0.1:$((PORT + 3))/library")" "No card mounted"

group "the device serves no Track and accepts no Track upload"
# The global premise (docs/specs/CONTEXT.md): there is no per-Track route, and no
# method that writes one. The index still lists the Track; it just does not hand
# it out. The library view is the firmware's own summary, so it stays fetchable.
check_eq "GET /music/<path> is gone" \
    "$(curl -s -o /dev/null -w '%{http_code}' "$BASE/music/tone-44100.flac")" "404"
check_eq "so is a nested path" \
    "$(curl -s -o /dev/null -w '%{http_code}' --path-as-is "$BASE/music/Album/01-Song.flac")" "404"
check_eq "an upload to /music/ is refused" \
    "$(curl -s -o /dev/null -w '%{http_code}' -X PUT --data-binary x "$BASE/music/new.mp3")" "404"
check_eq "an upload anywhere is refused" \
    "$(curl -s -o /dev/null -w '%{http_code}' -X PUT --data-binary x "$BASE/upload/new.mp3")" "404"
check_eq "the index still lists the Track" \
    "$(curl -s "$BASE/library" | grep -c 'tone-44100\.flac')" "1"
# Not a Track: the computed library view keeps its attachment framing.
check_eq "the library view is still fetchable" \
    "$(curl -s -o /dev/null -w '%{http_code}' "$BASE/api/library.bin")" "200"
check_match "and still framed as an attachment" \
    "$(curl -s -D - -o /dev/null "$BASE/api/library.bin" | tr -d '\r')" \
    '^Content-Disposition: attachment; filename="library.bin"$'

group "routing and rejected setup"
check_eq "an unknown path is a 404, not the stream" "$(curl -s -o /dev/null -w '%{http_code}' --path-as-is "$BASE/nope")" "404"
check_eq "so is a file-ish path" "$(curl -s -o /dev/null -w '%{http_code}' "$BASE/index.html")" "404"
check_eq "a non-GET method is refused" "$(curl -s -o /dev/null -w '%{http_code}' -X POST -d x "$BASE/library")" "405"
check_eq "the index is served" "$(curl -s -o /dev/null -w '%{http_code}' "$BASE/library")" "200"

if [ -n "$BOARD_URL" ]; then
    printf '  skip  descriptor leak check on the board (160 rejected connections exercised only the host build)\n'
elif command -v lsof >/dev/null 2>&1; then
    pid=$SRV_PID
    if [ -n "$pid" ]; then
        before=$(lsof -p "$pid" 2>/dev/null | wc -l | tr -d ' ')
        i=0
        while [ "$i" -lt 80 ]; do
            curl -s -o /dev/null --path-as-is "$BASE/rejected-$i" 2>/dev/null
            curl -s -o /dev/null -X POST -d x "$BASE/nope.mp3" 2>/dev/null
            i=$((i + 1))
        done
        sleep 1
        after=$(lsof -p "$pid" 2>/dev/null | wc -l | tr -d ' ')
        if [ "$after" -le "$((before + 4))" ]; then
            ok "no descriptor leak over 160 rejected connections ($before -> $after)"
        else
            bad "descriptors grew over rejected connections ($before -> $after)"
        fi
    else
        bad "could not find the server process to check for descriptor leaks"
    fi
else
    printf '  skip  descriptor leak check (no lsof)\n'
fi

group "T12  the station format is validated before anything is served (F1, F2)"
if [ -n "$BOARD_URL" ]; then
    printf '  note  the refusal checks start host processes; on the board the same\n'
    printf '        requirement is checked by flashing a deliberately bad out_hz\n'
    printf '        (README, board status) and watching it refuse to serve\n'
fi
if [ -z "$BOARD_URL" ]; then
run_expect_refusal "out_hz=44101 is refused" "$TMP/bad1.log" 18091 --out-hz 44101
check_match "and the log names the rate" "$(cat "$TMP/bad1.log")" '44101'
run_expect_refusal "out_hz=11000 is refused" "$TMP/bad2.log" 18092 --out-hz 11000
run_expect_refusal "out_hz=8000 with bitrate=128 is refused" "$TMP/bad3.log" 18093 --out-hz 8000 --bitrate 128
check_match "and the log names both values" "$(cat "$TMP/bad3.log")" '8000.*128|128.*8000'
run_expect_refusal "max_clients=5 is refused, not silently coerced (D3)" "$TMP/bad4.log" 18094 --max-clients 5
check_match "and the log states the documented range" "$(cat "$TMP/bad4.log")" 'max_clients 5 is outside the documented 1\.\.4 range'

if start_server "$FIX" 18095 "$TMP/srv3.log" --out-hz 8000 --bitrate 64; then
    check_match "out_hz=8000/bitrate=64 starts and logs MPEG-2.5" "$(cat "$TMP/srv3.log")" 'station format: 8000 Hz, 64 kbps CBR, joint stereo, MPEG-2\.5'
    check_match "with the smaller encoder frame" "$(cat "$TMP/srv3.log")" '576 samples/frame'
    check_eq "and serves the stream" "$(curl -s -o /dev/null --max-time 3 -w '%{http_code}' "http://127.0.0.1:18095/stream")" "200"
    probe25=$(curl -s --max-time 5 "http://127.0.0.1:18095/stream" | ffprobe -hide_banner -v error -show_entries stream=codec_name,sample_rate -of default=nw=1 - 2>&1)
    check_match "and ffprobe agrees it is MPEG-2.5 audio at 8 kHz" "$probe25" '^sample_rate=8000$'
fi
if start_server "$FIX" 18096 "$TMP/srv4.log" --max-clients 4; then
    check_match "max_clients=4 is honoured as given" "$(cat "$TMP/srv4.log")" 'sends at most 4 clients'
fi
if start_server "$FIX" 18097 "$TMP/srv5.log" --max-clients 1; then
    check_match "max_clients=1 is honoured as given" "$(cat "$TMP/srv5.log")" 'sends at most 1 clients'
fi
if start_server "$FIX" 18098 "$TMP/srv6.log" --max-clients 0; then
    check_match "an unset client limit (0) is the documented default of 2" "$(cat "$TMP/srv6.log")" 'sends at most 2 clients'
fi
fi

group "summary"
if [ -n "$BOARD_URL" ]; then
    printf 'tier: board (%s)\n' "$BASE"
else
    printf 'tier: host build (macOS)\n'
fi
printf '%d passed, %d failed\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ] || exit 1
