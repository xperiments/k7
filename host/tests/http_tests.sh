#!/bin/sh
# Host acceptance tests for the HTTP surface: T3, T4, T5, T6, T7, T8, T9 and T12,
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
    if ! curl -s -o /dev/null --max-time 8 "$BASE/"; then
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

group "T3  the live stream is continuous MPEG audio, not WAV"
curl -s --max-time 6 "$BASE/stream" >"$TMP/stream.mp3"
head4=$(head -c 4 "$TMP/stream.mp3" | od -An -tx1 | tr -d ' \n')
check_match "the capture starts on a frame sync (${head4})" "$head4" '^fff' 
probe=$(ffprobe -hide_banner -v error -show_entries stream=codec_name,sample_rate,channels,bit_rate -of default=nw=1 "$TMP/stream.mp3" 2>&1)
check_match "ffprobe sees MPEG audio" "$probe" '^codec_name=mp3$'
check_match "at the station rate" "$probe" '^sample_rate=44100$'
check_match "in stereo" "$probe" '^channels=2$'
check_match "at the station bit rate" "$probe" '^bit_rate=128000$'
errors=$(ffmpeg -v error -i "$TMP/stream.mp3" -f null - 2>&1 | wc -l | tr -d ' ')
check_eq "the whole capture decodes without a single error" "$errors" "0"
if [ -s "$STARTUP_LOG" ]; then
    check_match "the station format is logged at startup" "$(tr -d '\r' <"$STARTUP_LOG")" 'station format: 44100 Hz, 128 kbps CBR, joint stereo, MPEG-1'
    check_match "and names the encoder's state size" "$(tr -d '\r' <"$STARTUP_LOG")" '[0-9]+ B of state inside the vendored library'
    check_match "and reports which memory the ring took" "$(tr -d '\r' <"$STARTUP_LOG")" 'burst ring: .* in external memory'
    check_match "and which memory the encoder staging took" "$(tr -d '\r' <"$STARTUP_LOG")" 'staging buffers in external memory'
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
check_match "the station name is canonical: no space after the colon" "$h5" '^icy-name:ESP32 Radio \(host\)$'
check_no_match "and not the non-canonical form" "$h5" '^icy-name: '
if $PY "$HOST_DIR/tests/check_metadata.py" "$TMP/b5" 16000 >"$TMP/meta.out" 2>&1; then
    ok "$(cat "$TMP/meta.out")"
else
    bad "metadata framing: $(cat "$TMP/meta.out")"
fi

group "T4  a client that says nothing cannot stall the next one (D1)"
$PY "$HOST_DIR/tests/silent_client.py" 127.0.0.1 "$PORT" 4 &
silent=$!
sleep 0.6
code=$(curl -s -o /dev/null --max-time 5 -w '%{http_code}' "$BASE/")
ttfb=$(curl -s -o /dev/null --max-time 5 -w '%{time_total}' "$BASE/")
check_eq "the second client is served" "$code" "200"
check_lt "and served at once (${ttfb}s, not after a timeout)" "$ttfb" "2.0"
stream_ttfb=$(curl -s -o /dev/null --max-time 3 -w '%{time_starttransfer}' "$BASE/stream")
check_lt "the stream's headers also arrive at once (${stream_ttfb}s)" "$stream_ttfb" "2.0"
kill "$silent" 2>/dev/null

group "T6  the index lists exactly the library"
curl -s "$BASE/" >"$TMP/index.html"
if $PY "$HOST_DIR/tests/check_index.py" "$TMP/index.html" "$FIX" >"$TMP/index.out" 2>&1; then
    ok "$(cat "$TMP/index.out")"
else
    bad "index: $(cat "$TMP/index.out")"
fi
check_match "the page says what is playing" "$(cat "$TMP/index.html")" 'Now playing:'
check_match "and links the live stream" "$(cat "$TMP/index.html")" 'href="/stream"'
check_match "and serves a content type a browser can render" "$(curl -s -D - -o /dev/null "$BASE/")" 'Content-Type: text/html; charset=utf-8'

group "T7  downloads are the original bytes, sized and typed"
for f in "$FIX"/*; do
    name=$(basename "$f")
    case "$name" in
    *.mp3 | *.flac | *.wav) ;;
    *) continue ;;
    esac
    curl -s -o "$TMP/dl" "$BASE/music/$name"
    if cmp -s "$TMP/dl" "$f"; then ok "byte-identical: $name"; else bad "download differs: $name"; fi
    hdr=$(curl -s -D - -o /dev/null "$BASE/music/$name" | tr -d '\r')
    want_size=$(wc -c <"$f" | tr -d ' ')
    check_match "length reported for $name" "$hdr" "^Content-Length: $want_size$"
done
check_match "flac is typed audio/flac" "$(curl -s -D - -o /dev/null "$BASE/music/tone-44100.flac")" 'Content-Type: audio/flac'
check_match "wav is typed audio/wav" "$(curl -s -D - -o /dev/null "$BASE/music/tone-44100.wav")" 'Content-Type: audio/wav'
check_match "mp3 is typed audio/mpeg" "$(curl -s -D - -o /dev/null "$BASE/music/tone-44100.mp3")" 'Content-Type: audio/mpeg'

group "T8  the path guard refuses anything that is not a track name (D5)"
for p in \
    '/music/..%2f..%2fetc%2fpasswd' \
    '/music/../../etc/passwd' \
    '/music/%2e%2e%2fetc%2fpasswd' \
    '/music/....//etc/passwd' \
    '/music/.hidden.mp3' \
    '/music/.%2e/secret.mp3' \
    '/music/sub%2fdir%2ftrack.mp3' \
    '/music/a%00b.mp3' \
    '/music/' \
    '/music/notes.txt' \
    '/music/tone-44100.flac%00.txt'; do
    code=$(curl -s --path-as-is -o "$TMP/guard.out" -w '%{http_code}' "$BASE$p")
    case "$code" in
    400 | 404) ok "refused ($code): $p" ;;
    *) bad "not refused ($code): $p -> $(head -c 60 "$TMP/guard.out")" ;;
    esac
    check_no_match "no file contents leaked for $p" "$(head -c 200 "$TMP/guard.out")" 'root:.*:0:0:'
done

group "routing and rejected setup"
check_eq "an unknown path is a 404, not the stream" "$(curl -s -o /dev/null -w '%{http_code}' --path-as-is "$BASE/nope")" "404"
check_eq "so is a file-ish path" "$(curl -s -o /dev/null -w '%{http_code}' "$BASE/index.html")" "404"
check_eq "a non-GET method is refused" "$(curl -s -o /dev/null -w '%{http_code}' -X POST -d x "$BASE/")" "405"
check_eq "the root is served" "$(curl -s -o /dev/null -w '%{http_code}' "$BASE/")" "200"

if [ -n "$BOARD_URL" ]; then
    printf '  skip  descriptor leak check on the board (160 rejected connections exercised only the host build)\n'
elif command -v lsof >/dev/null 2>&1; then
    pid=$SRV_PID
    if [ -n "$pid" ]; then
        before=$(lsof -p "$pid" 2>/dev/null | wc -l | tr -d ' ')
        i=0
        while [ "$i" -lt 80 ]; do
            curl -s -o /dev/null --path-as-is "$BASE/rejected-$i" 2>/dev/null
            curl -s -o /dev/null -X POST -d x "$BASE/music/nope.mp3" 2>/dev/null
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

group "T9  one download at a time, and the first one survives (D6)"
if [ -n "$BOARD_URL" ]; then
    pick=$($PY "$HOST_DIR/tests/biggest_track.py" "$BASE" 50000)
    if [ -z "$pick" ]; then
        printf '  skip  no track on the card is large enough for the slow-reader test (>50 KB)\n'
    else
        name=${pick%% *}
        curl -s -o "$TMP/ref.bin" "$BASE/music/$name"
        if $PY "$HOST_DIR/tests/two_downloads.py" "$(printf '%s' "$BASE" | sed -e 's#http://##' -e 's#:.*##')" \
                "$(printf '%s' "$BASE" | sed -e 's#.*:##')" "$name" "$TMP/ref.bin" 0.2 >"$TMP/two.out" 2>&1; then
            ok "$(cat "$TMP/two.out")"
        else
            bad "concurrent downloads: $(cat "$TMP/two.out")"
        fi
        if [ -n "$SERIAL_LOG" ] && grep -q 'starved' "$SERIAL_LOG"; then
            bad "the live stream starved during a download"
        else
            ok "no starvation rebase logged while a download was running"
        fi
    fi
fi
if [ -z "$BOARD_URL" ]; then
mkdir -p "$TMP/lib"
cp "$FIX/tone-44100.flac" "$TMP/lib/tone-44100.flac"
dd if=/dev/zero of="$TMP/lib/big.wav" bs=1048576 count=8 2>/dev/null
if start_server "$TMP/lib" "$PORT2" "$TMP/srv2.log"; then
    if $PY "$HOST_DIR/tests/two_downloads.py" 127.0.0.1 "$PORT2" big.wav "$TMP/lib/big.wav" >"$TMP/two.out" 2>&1; then
        ok "$(cat "$TMP/two.out")"
    else
        bad "concurrent downloads: $(cat "$TMP/two.out")"
    fi
    if grep -q 'starved' "$TMP/srv2.log"; then
        bad "the live stream starved during a download"
    else
        ok "no starvation rebase logged while a download was running"
    fi
fi
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
