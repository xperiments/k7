#!/bin/sh
# Board soak: T9b (download contention) and T11 (encoder cost and memory over time).
#
#   sh host/tests/board_soak.sh <base-url> <serial-port> <minutes> [stream|downloads]
#
# Set RESCAN=1 to POST /api/rescan once a minute during the soak. That is the
# ticket-13 path: a scan and a download sharing the card and the broadcast.
#
# It resets the board, captures its serial log for the whole run, attaches one
# client to the live stream, and (with `downloads`) keeps pulling the largest
# track off the card at the same time. Then it reads the device's own telemetry
# and decides:
#
#   * encoder cost below MAX_COST (default 35 %, the specification's F4 target)
#   * no starvation rebase                                      (T11, T9b)
#   * free memory in both classes stable across the run         (T11)
#   * no assert, stack overflow or reboot                       (T9b, T11)
#
# All judgement comes from the device's log, which is the only place these numbers
# exist: the host cannot measure a 240 MHz Xtensa with Wi-Fi running.
set -u

BASE=${1:?usage: board_soak.sh <base-url> <serial-port> <minutes> [stream|downloads]}
SERIAL_PORT=${2:?}
MINUTES=${3:-60}
LOAD=${4:-stream}
# The bound the encoder's cost is held to. The specification proposes 35% (F4); a
# configuration chosen for quality rather than headroom can be held to its own
# bound instead, and says so on the command line rather than being silently
# exempted.
MAX_COST=${MAX_COST:-35}
RESCAN=${RESCAN:-0}
HERE=$(dirname "$0")

# serial_log.py needs pyserial. The ESP-IDF python environment has it; a bare
# python3 usually does not. Find one that can import it rather than failing later.
PY=${PYTHON:-}
if [ -z "$PY" ]; then
    for cand in "$HOME"/.espressif/python_env/idf*/bin/python "$(command -v python3)"; do
        [ -x "$cand" ] || continue
        if "$cand" -c 'import serial' >/dev/null 2>&1; then PY=$cand; break; fi
    done
fi
if [ -z "$PY" ]; then
    echo "no python with pyserial found; set PYTHON=/path/to/python" >&2
    exit 2
fi
echo "using python: $PY"
SECS=$((MINUTES * 60))
LOG=$(mktemp "${TMPDIR:-/tmp}/board-soak.XXXXXX")
FAIL=0

echo "soaking $BASE for $MINUTES minute(s) with load: $LOAD (encoder bound ${MAX_COST}%)"
echo "serial: $SERIAL_PORT -> $LOG"

# Capture first: it resets the board, so the run covers a clean boot.
"$PY" "$HERE/serial_log.py" -p "$SERIAL_PORT" -s "$SECS" -o "$LOG" &
CAP=$!
# The capture resets the board, and boot plus Wi-Fi takes about five seconds.
# Asking for the library before that is a race, and it silently turns a download
# soak into a stream-only one.
n=0
while [ "$n" -lt 100 ]; do
    curl -s -o /dev/null --max-time 1 "$BASE/" && break
    n=$((n + 1))
    sleep 0.2
done
sleep 1

# One client on the live stream for the whole run: T11 asks for that, and T9b
# wants the broadcast busy while the card is under download pressure.
curl -s -o /dev/null --max-time "$SECS" "$BASE/stream" &
STREAM=$!

if [ "$RESCAN" = 1 ]; then
    echo "requesting a rescan once a minute during the soak"
    ( end=$(( $(date +%s) + SECS ))
      while [ "$(date +%s)" -lt "$end" ]; do
          curl -s -o /dev/null -X POST -H 'Content-Type: application/json' \
              --data '{}' "$BASE/api/rescan" || true
          sleep 60
      done ) &
    SCAN=$!
fi

if [ "$LOAD" = downloads ]; then
    name=$("$PY" "$HERE/biggest_track.py" "$BASE" 50000 | cut -d' ' -f1)
    if [ -z "$name" ]; then
        echo "no track large enough to download; using the stream load only"
    else
        echo "downloading $name repeatedly while the stream plays"
        end=$(( $(date +%s) + SECS ))
        n=0
        while [ "$(date +%s)" -lt "$end" ]; do
            curl -s -o /dev/null --max-time 60 "$BASE/music/$name" && n=$((n + 1))
        done &
        DL=$!
    fi
fi

wait $CAP
kill "$STREAM" 2>/dev/null
[ -n "${DL:-}" ] && kill "$DL" 2>/dev/null
[ -n "${SCAN:-}" ] && kill "$SCAN" 2>/dev/null
sleep 1

# ---------------------------------------------------------------- the verdict

lines=$(tr -d '\r' <"$LOG")

rebases=$(printf '%s\n' "$lines" | grep -c 'starved for' || true)
crashes=$(printf '%s\n' "$lines" | grep -cE 'assert failed|stack overflow|Rebooting' || true)
rescans=$(printf '%s\n' "$lines" | grep -c 'library rescan: generation' || true)
scan_faults=$(printf '%s\n' "$lines" | grep -cE 'library rescan:.*failed|stack overflow in task main' || true)
cost=$(printf '%s\n' "$lines" | sed -n 's/.*= \([0-9.]*\)% of one core.*/\1/p' | tail -1)
frames=$(printf '%s\n' "$lines" | sed -n 's/.*encoder: \([0-9]*\) frames.*/\1/p' | tail -1)
first_mem=$(printf '%s\n' "$lines" | grep -m1 'memory: internal' | sed 's/.*memory: //')
last_mem=$(printf '%s\n' "$lines" | grep 'memory: internal' | tail -1 | sed 's/.*memory: //')

echo
echo "telemetry from the device:"
printf '  encoder: %s frames, last reading %s%% of one core\n' "${frames:-none}" "${cost:-none}"
printf '  memory at the start: %s\n' "${first_mem:-none}"
printf '  memory at the end:   %s\n' "${last_mem:-none}"
printf '  starvation rebases: %s\n' "$rebases"
printf '  rescans published: %s\n' "$rescans"
printf '  asserts/overflows/reboots: %s\n' "$crashes"

[ "$crashes" = "0" ] || { echo "FAIL: the device reported a fault"; FAIL=1; }
[ "$rebases" = "0" ] || { echo "FAIL: $rebases starvation rebase(s)"; FAIL=1; }
if [ "$RESCAN" = 1 ]; then
    [ "$scan_faults" = "0" ] || { echo "FAIL: a rescan reported a fault"; FAIL=1; }
    [ "$rescans" -ge 2 ] || { echo "FAIL: fewer than two rescans published"; FAIL=1; }
fi
if [ -n "$cost" ]; then
    awk "BEGIN{exit !($cost < $MAX_COST)}" || {
        echo "FAIL: encoder cost ${cost}% is not below ${MAX_COST}%"
        if [ "$MAX_COST" = "35" ] && awk "BEGIN{exit !($cost < 70)}"; then
            echo "      (35% is the specification's target, which no format offers at"
            echo "       44.1 kHz or 32 kHz; this station is configured for quality, so"
            echo "       judge it with MAX_COST=70 — see the README's station format.)"
        fi
        FAIL=1
    }
else
    echo "FAIL: no encoder telemetry in the capture"; FAIL=1
fi

# Stability: the *minimum* free figure in the first third of the run against the
# minimum in the last third. Comparing two instantaneous samples would not work:
# a track's decoder working set comes and goes, so consecutive readings differ by
# more than any leak would, depending only on where in a track the sample landed.
STABILITY=na
for class in internal external; do
    # Only the periodic telemetry lines: the startup line ("heap at startup:
    # internal N B free") reports a much larger figure, because it is taken before
    # Wi-Fi, the encoder and the decoder's working set exist, and comparing against
    # it would report a leak on every run.
    vals=$(printf '%s\n' "$lines" | grep 'memory: internal' |
           sed -n "s/.*$class \([0-9]*\) B free.*/\1/p")
    n=$(printf '%s\n' "$vals" | grep -c . || true)
    if [ "$n" -lt 6 ]; then
        printf '  %s free: %s sample(s) - too few for a stability verdict\n' "$class" "$n"
        continue
    fi
    STABILITY=checked
    third=$((n / 3))
    a=$(printf '%s\n' "$vals" | head -"$third" | sort -n | head -1)
    b=$(printf '%s\n' "$vals" | tail -"$third" | sort -n | head -1)
    if [ -n "$a" ] && [ -n "$b" ]; then
        # A floor of 8 KB absorbs the normal jitter of small allocations; below
        # that, a real decline is reported.
        awk "BEGIN{d=$a-$b; exit !(d < 8192)}" ||
            { echo "FAIL: $class free memory fell from $a B to $b B over the run"; FAIL=1; }
        printf '  %s free (minimum of first/last third): %s B -> %s B\n' "$class" "$a" "$b"
    fi
done

if [ "$FAIL" -eq 0 ]; then
    echo
    if [ "$STABILITY" = "checked" ]; then
        echo "soak passed: no faults, no starvation, encoder under ${MAX_COST}%, memory stable"
    else
        echo "soak passed: no faults, no starvation, encoder under ${MAX_COST}%"
        echo "             (memory stability not assessed: too few telemetry samples)"
    fi
else
    echo
    echo "soak FAILED (log kept at $LOG)"
fi
exit "$FAIL"
