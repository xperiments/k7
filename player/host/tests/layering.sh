#!/bin/sh
# The K7Player's source-shape checks: the rules that keep two firmwares in one tree from
# drifting into one another. A violation fails here rather than in review.
#
#   ROOT=<repo> PLAYER=<repo>/player sh player/host/tests/layering.sh
set -u
ROOT=${ROOT:?set ROOT to the repository root}
PLAYER=${PLAYER:?set PLAYER to the player directory}
fail=0

ok() { printf '  ok    %s\n' "$1"; }
bad() { printf '  FAIL  %s\n' "$1"; fail=1; }

printf '\nP1  no target conditional compilation in portable code\n'
hits=$(grep -rn 'CONFIG_IDF_TARGET' "$PLAYER/components" "$ROOT/components/portable/handover" 2>/dev/null || true)
if [ -n "$hits" ]; then
    bad "a portable component branches on the target"
    printf '%s\n' "$hits" | sed 's/^/        /'
else
    ok "the player's components are target-agnostic"
fi

printf '\nP2  every portable player source is compiled by the host build\n'
for f in "$PLAYER"/components/player/*.c; do
    [ -e "$f" ] || continue
    base=$(basename "$f")
    if grep -q "$base" "$PLAYER/host/Makefile"; then
        ok "player/host compiles $base"
    else
        bad "player/host does not compile $base"
    fi
done

printf '\nP3  the board adapter is firmware-only\n'
if grep -q 'board_player_esp32\.c' "$PLAYER/host/Makefile"; then
    bad "the host build compiles the ESP32 adapter"
else
    ok "board_player_esp32.c stays out of the host build"
fi

printf '\nP4  no forked component source\n'
forks=$(find "$PLAYER/components" -name '*_host.c' 2>/dev/null || true)
if [ -n "$forks" ]; then
    bad "forked source files exist: $forks"
else
    ok "no *_host.c fork of a component source"
fi

printf '\nP5  the player requires nothing of the K7'"'"'s\n'
hits=$(grep -rnE 'components/app|cassette|mini_icecast|screen_display|shine|snapshot|label|app_board' \
       "$PLAYER/CMakeLists.txt" "$PLAYER/main/CMakeLists.txt" "$PLAYER"/components/*/CMakeLists.txt \
       2>/dev/null || true)
if [ -n "$hits" ]; then
    bad "the player's build names a K7 component"
    printf '%s\n' "$hits" | sed 's/^/        /'
else
    ok "the player's build names only its own components and the shared handover"
fi

printf '\n'
if [ "$fail" -ne 0 ]; then
    echo "layering: FAILED"
    exit 1
fi
echo "layering: all checks passed"
