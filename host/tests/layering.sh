#!/bin/sh
# Ticket 07 (S1-S4), ticket 02 (S5) and requirement H2/H3: the architectural rules are checked,
# not just intended.
#
#   ROOT=. sh host/tests/layering.sh
#
# These are source-shape assertions, which is the only cheap way to keep a
# dependency direction from eroding: a component that starts parsing HTTP, or a
# forked copy of a source file, fails the build here rather than in review.
set -u
ROOT=${ROOT:?set ROOT to the repository root}
fail=0

absent() { # file pattern description
    if grep -qE "$2" "$1" 2>/dev/null; then
        printf '  FAIL  %s (/'"$2"'/ in %s)\n' "$3" "$1"
        fail=1
    else
        printf '  ok    %s\n' "$3"
    fi
}

printf '\nS1/S3  the broadcast component owns no socket, no parsing and no markup\n'
# It does write the stream's own response headers (the status line, Content-Type,
# icy-name, icy-metaint): those are properties of the broadcast, and the
# specification asks for the /stream hand-off to work exactly as it did before the
# extraction. What it must not do is listen, read or parse requests, route, or
# render pages.
for f in "$ROOT"/components/portable/mini_icecast/*.c "$ROOT"/components/portable/mini_icecast/include/*.h; do
    absent "$f" 'listen\(|accept\(|recv\(|socket\(' 'no sockets: '"$(basename "$f")"
    absent "$f" 'strcasestr|strstr\(buf|Icy-MetaData:' 'no request parsing: '"$(basename "$f")"
    absent "$f" '<html|<body|<h1>|href=' 'no markup: '"$(basename "$f")"
done

printf '\nS4  the HTTP component knows nothing about audio or files\n'
for f in "$ROOT"/components/portable/http_server/*.c "$ROOT"/components/portable/http_server/include/*.h; do
    absent "$f" '#include.*(mi_decode|mi_encode|mi_resample|mini_icecast|mini_files)' 'includes no audio or file header: '"$(basename "$f")"
    absent "$f" '(mini_icecast|mini_files|mi_encode|mi_decode|mi_resample)_' 'calls nothing in those components: '"$(basename "$f")"
    absent "$f" 'music_dir|\.mp3|\.flac|now_playing' 'no library concepts: '"$(basename "$f")"
done

printf '\nS4  nothing depends on mini_files except the application layer\n'
users=$(grep -rl 'mini_files\.h' "$ROOT/components" "$ROOT/main" 2>/dev/null | grep -v '/mini_files/' || true)
for f in $users; do
    case "$f" in
    */main/*) printf '  ok    %s may depend on mini_files (the appliance)\n' "$(basename "$f")" ;;
    */portable/app_services/*)
        printf '  ok    %s may depend on mini_files (the composition module is the application layer, moved)\n' \
               "$(basename "$f")" ;;
    */app/*)
        printf '  ok    %s may depend on mini_files (an app-only module)\n' "$(basename "$f")" ;;
    *)
        printf '  FAIL  %s depends on mini_files\n' "$f"
        fail=1
        ;;
    esac
done
[ -n "$users" ] || printf '  ok    (nothing at all)\n'

printf '\nS3  the broadcast keeps exactly its documented surface\n'
for sym in mini_icecast_check_station mini_icecast_start mini_icecast_status \
           mini_icecast_lag_ms mini_icecast_skip mini_icecast_now_playing; do
    if grep -q "$sym" "$ROOT/components/portable/mini_icecast/include/mini_icecast.h"; then
        printf '  ok    %s is public\n' "$sym"
    else
        printf '  FAIL  %s is missing from the public header\n' "$sym"
        fail=1
    fi
done
for sym in mini_icecast_register_client mini_icecast_register_result_t; do
    if grep -q "$sym" "$ROOT/components/portable/mini_icecast/include/mini_icecast.h"; then
        printf '  FAIL  %s is still public (the /stream route is the only caller)\n' "$sym"
        fail=1
    else
        printf '  ok    %s is private\n' "$sym"
    fi
done

printf '\nH2  no target conditional compilation in a portable component\n'
if grep -rn 'CONFIG_IDF_TARGET' "$ROOT/components/portable" >/dev/null 2>&1; then
    printf '  FAIL  a portable component branches on the target\n'
    grep -rn 'CONFIG_IDF_TARGET' "$ROOT/components/portable" | sed 's/^/        /'
    fail=1
else
    printf '  ok    portable components are target-agnostic; the app group carries the board/target difference\n'
fi

printf '\nH3  every portable component compiles on the host; no app-only source does\n'
for src in "$ROOT"/components/portable/*/*.c; do
    case "$src" in
    */shine/*) continue ;; # vendored: picked up by wildcard
    esac
    base=$(basename "$src")
    if grep -q "$base" "$ROOT/host/Makefile"; then
        printf '  ok    host/Makefile compiles %s\n' "$base"
    else
        printf '  FAIL  host/Makefile does not compile %s\n' "$base"
        fail=1
    fi
done
app_srcs=$(find "$ROOT/components/app" -name '*.c' 2>/dev/null || true)
if [ -n "$app_srcs" ]; then
    for src in $app_srcs; do
        base=$(basename "$src")
        if grep -q "$base" "$ROOT/host/Makefile"; then
            printf '  FAIL  app-only source %s is compiled by the host build\n' "$src"
            fail=1
        else
            printf '  ok    app-only %s stays out of the host build\n' "$base"
        fi
    done
else
    printf '  ok    (no app-only sources yet)\n'
fi
if grep -rn --include='*.c' --include='*.h' -E '__APPLE__|host/include|_host\.c' "$ROOT/components/app" 2>/dev/null; then
    printf '  FAIL  an app-only component carries a host fallback\n'
    fail=1
else
    printf '  ok    no app-only component carries a host fallback\n'
fi
forks=$(find "$ROOT/components" "$ROOT/main" -name '*_host.c' 2>/dev/null || true)
if [ -n "$forks" ]; then
    printf '  FAIL  forked source files exist: %s\n' "$forks"
    fail=1
else
    printf '  ok    no *_host.c fork of any component source\n'
fi

printf '\nS5  the source-format authority sits below the broadcast and the file surface\n'
# audio_format is a leaf: it must not learn about the components that consume it,
# in source or in its component dependency list. The library component lands in
# ticket 03; the rule below is the lower half of the library<->broadcast cycle
# guard and applies to it as soon as the directory exists.
for f in "$ROOT"/components/portable/audio_format/*.c "$ROOT"/components/portable/audio_format/include/*.h; do
    absent "$f" '#include.*(mini_icecast|mini_files|http_server|library)' \
           'audio_format includes no consumer: '"$(basename "$f")"
done
if grep -qE '(mini_icecast|mini_files|library)' "$ROOT/components/portable/audio_format/CMakeLists.txt"; then
    printf '  FAIL  audio_format requires a consumer in its CMakeLists\n'
    fail=1
else
    printf '  ok    audio_format requires no consumer\n'
fi
for header in mini_icecast/include/mini_icecast.h mini_icecast/include/mi_decode.h; do
    if grep -q '#include "audio_format.h"' "$ROOT/components/portable/$header"; then
        printf '  ok    %s takes the format authority from audio_format\n' "$header"
    else
        printf '  FAIL  %s does not include audio_format.h\n' "$header"
        fail=1
    fi
done
if [ -d "$ROOT/components/portable/library" ]; then
    for f in "$ROOT"/components/portable/library/*.c "$ROOT"/components/portable/library/include/*.h; do
        absent "$f" '#include.*(mini_icecast|mini_files|http_server)' \
               'library includes no consumer: '"$(basename "$f")"
    done
    if grep -qE '(mini_icecast|mini_files)' "$ROOT/components/portable/library/CMakeLists.txt" 2>/dev/null; then
        printf '  FAIL  library requires the broadcast or the file surface\n'
        fail=1
    else
        printf '  ok    library requires neither the broadcast nor the file surface\n'
    fi
else
    printf '  ok    (library component not present yet; its rule applies when ticket 03 adds it)\n'
fi
definitions=$(grep -rlE '^[[:space:]]*mi_fmt_t[[:space:]]+mi_dec_format_of[[:space:]]*\([^;]*\)[[:space:]]*$' \
                  "$ROOT/components/portable" --include='*.c' --include='*.h' 2>/dev/null | wc -l | tr -d ' ')
if [ "$definitions" = "1" ]; then
    printf '  ok    exactly one component defines the source-format rule\n'
else
    printf '  FAIL  %s components define the source-format rule\n' "$definitions"
    fail=1
fi

printf '\nL1  the vendored encoder is unmodified\n'
if [ -f "$ROOT/components/portable/shine/COPYING" ] && [ -f "$ROOT/LICENSES/LGPL-2.0.txt" ]; then
    if cmp -s "$ROOT/components/portable/shine/COPYING" "$ROOT/LICENSES/LGPL-2.0.txt"; then
        printf '  ok    the shipped licence text matches the vendored one\n'
    else
        printf '  FAIL  LICENSES/LGPL-2.0.txt differs from components/portable/shine/COPYING\n'
        fail=1
    fi
else
    printf '  FAIL  the LGPL text is not shipped\n'
    fail=1
fi
if [ -f "$ROOT/THIRD_PARTY_NOTICES" ] && grep -q 'shine' "$ROOT/THIRD_PARTY_NOTICES"; then
    printf '  ok    THIRD_PARTY_NOTICES names shine\n'
else
    printf '  FAIL  THIRD_PARTY_NOTICES does not name shine\n'
    fail=1
fi
if grep -rn --exclude=layering.sh 'fknrdcls\|mp3_shine_esp32' "$ROOT/components" "$ROOT/main" "$ROOT/host" >/dev/null 2>&1; then
    printf '  FAIL  the GPL-3.0 encoder port is referenced somewhere\n'
    fail=1
else
    printf '  ok    the GPL-3.0 encoder port appears nowhere\n'
fi

if [ "$fail" -eq 0 ]; then
    printf '\nlayering: all checks passed\n'
else
    printf '\nlayering: FAILURES above\n'
fi
exit "$fail"
