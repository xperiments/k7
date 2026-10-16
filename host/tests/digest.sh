#!/bin/sh
# The host/board digest procedure (T11, requirement H5).
#
#   sh host/tests/digest.sh <url-a> [url-b] [seconds]
#
# With two URLs it captures both streams at the same time, cuts the same window
# out of each, and says whether the two digests match. That is the comparison the
# two-tier rule asks for: run it as
#
#   sh host/tests/digest.sh http://127.0.0.1:18000 http://192.168.1.42:8000 12
#
# with the host build on one address and the board on the other.
#
# How the window is chosen, and why it has to be checked before the digests are
# believed: a capture starts wherever the client happened to connect, so the only
# intrinsic delimiters are track changes. Each ICY metadata block announces the
# current track, and a change of title marks a change of track. The window runs
# from the first announced change to the next one. Because the metadata interval
# is a fixed number of *encoded bytes* (16000 by default) and not a number of
# tracks, two captures can straddle different spans - so this script compares the
# reported spans first and refuses to compare digests when they do not match.
#
# Two things to hold on to when reading the result:
#   * when the source rate equals the station rate the resampler is a
#     pass-through, so decoder and encoder are the only stages involved; both are
#     fixed-point, so the digests must be identical.
#   * when the source rate differs, the resampler's floating-point kernel is in
#     the path. A mismatch there is not a defect: the weaker, correct assertion is
#     equal length and a bounded signal-to-noise difference.
#   * a digest taken on the host is a rehearsal. Only the board run is evidence
#     about the board.
set -u

A=${1:?usage: digest.sh <url-a> [url-b] [seconds]}
B=${2:-}
SECS=${3:-12}
HERE=$(dirname "$0")
OUT=$(mktemp -d "${TMPDIR:-/tmp}/xtape-digest.XXXXXX")
trap 'rm -rf "$OUT"' EXIT

python3 "$HERE/stream_digest.py" "$A/stream" "$OUT/a.mp3" "$SECS" >"$OUT/a.txt" 2>&1 &
pa=$!
if [ -n "$B" ]; then
    python3 "$HERE/stream_digest.py" "$B/stream" "$OUT/b.mp3" "$SECS" >"$OUT/b.txt" 2>&1 &
    pb=$!
fi
wait $pa || { echo "capture A failed:"; cat "$OUT/a.txt"; exit 1; }
printf 'A (%s): %s\n' "$A" "$(cat "$OUT/a.txt")"
printf 'A encoded bytes: %s\n' "$(wc -c <"$OUT/a.mp3" | tr -d ' ')"
printf 'A decoded md5:   '
ffmpeg -v error -i "$OUT/a.mp3" -map 0:a -f md5 - 2>/dev/null

[ -n "$B" ] || exit 0
wait $pb || { echo "capture B failed:"; cat "$OUT/b.txt"; exit 1; }
printf 'B (%s): %s\n' "$B" "$(cat "$OUT/b.txt")"
printf 'B encoded bytes: %s\n' "$(wc -c <"$OUT/b.mp3" | tr -d ' ')"
printf 'B decoded md5:   '
ffmpeg -v error -i "$OUT/b.mp3" -map 0:a -f md5 - 2>/dev/null

spanA=$(cat "$OUT/a.txt")
spanB=$(cat "$OUT/b.txt")
if [ "$spanA" != "$spanB" ]; then
    echo "NOT COMPARABLE: the two captures straddle different spans; re-run."
    exit 2
fi
if cmp -s "$OUT/a.mp3" "$OUT/b.mp3"; then
    echo "IDENTICAL: the same encoded bytes from both."
else
    echo "DIFFERENT: same span, different bytes. Fixed-point stages must agree;"
    echo "           if the source rate differs from the station rate, check the"
    echo "           resampler's SNR bound instead of comparing bytes."
    exit 3
fi
