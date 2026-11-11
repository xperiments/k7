#!/bin/sh
# SU-07: the board tier for provisioning, driven from the far side of the hotspot.
#
#   sh host/tests/board_setup.sh [--home-pass] [--iface en0]
#
# The hotspot must already be up: on a healthy device nothing on the network can raise it
# (ADR 0077), so raise it from Menu - Settings - Wi-Fi - Setup first, or run this against a
# device that has no network and raised its own.
#
# **This script changes the machine's Wi-Fi.** It joins the device's `K7.local` hotspot,
# runs the checks that can only be made from a client on that network (B1, B2, B7), offers
# B3 if you give it your network's password, and then puts the interface back by cycling
# its power, which makes macOS rejoin whichever network it prefers. Nothing in this
# repository or the App needs the internet, so nothing else is lost while it runs.
#
# What it cannot do: B6 (a reflash keeping the record) and B8 (the panel's rows), which
# need a cable and eyes. It prints them as SKIP rather than pretending.
#
# Requires macOS (networksetup). Sets up nothing else.
set -u

AP=${AP:-K7.local}
PASSWORD=${PASSWORD:-12345678}
IFACE=${IFACE:-}
BASE=${BASE:-http://192.168.4.1}
ASK_HOME_PASS=0
FAIL=0
SKIP=0

WANT_IFACE=0
for arg in "$@"; do
    case "$arg" in
    --home-pass) ASK_HOME_PASS=1 ;;
    --iface=*) IFACE=${arg#--iface=} ;;
    -h|--help) sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    --iface) WANT_IFACE=1 ;;
    *)
        if [ "$WANT_IFACE" -eq 1 ]; then IFACE=$arg; WANT_IFACE=0
        else echo "unknown argument: $arg" >&2; exit 2; fi
        ;;
    esac
done

command -v networksetup >/dev/null 2>&1 || { echo "networksetup not found: macOS only" >&2; exit 2; }

# The Wi-Fi port is not always en0: find the one macOS calls Wi-Fi (or AirPort, on an older
# system) rather than guessing.
if [ -z "$IFACE" ]; then
    IFACE=$(networksetup -listallhardwareports |
        awk '/Hardware Port: (Wi-Fi|AirPort)/{getline; if ($1 == "Device:") { print $2; exit }}')
fi
if [ -z "$IFACE" ]; then
    echo "no Wi-Fi interface found; pick one with --iface enN" >&2
    exit 2
fi

ok() { echo "  ok    $1"; }
bad() { echo "  FAIL  $1"; FAIL=1; }
skip() { echo "  skip  $1"; SKIP=$((SKIP + 1)); }
note() { echo "  note  $1"; }

send() { curl -s -o "$2" -w '%{http_code}' --max-time "${3:-10}" "$1"; }
post() { # url file body
    curl -s -o "$2" -w '%{http_code}' --max-time 30 -X POST -H 'Content-Type: application/json' \
        -d "$3" "$1"
}

# A Wi-Fi port answers for its power; a port that is merely unassociated says so in words,
# which is not the same thing as being an Ethernet port.
POWER=$(networksetup -getairportpower "$IFACE" 2>/dev/null)
case "$POWER" in
*"Wi-Fi Power"* | *"AirPort Power"*) ;;
*)
    bad "$IFACE is not a Wi-Fi interface; pick one with --iface enN (networksetup -listallhardwareports)"
    exit 1
    ;;
esac
START_NET=$(networksetup -getairportnetwork "$IFACE" 2>/dev/null | sed -n 's/^Current Wi-Fi Network: //p')
if [ "$START_NET" = "$AP" ]; then
    bad "this machine is already on $AP; run this from a network that is not the hotspot"
    exit 1
fi
if [ -z "$START_NET" ]; then
    echo "--- device: $AP at $BASE; $IFACE is not joined to anything (Ethernet stays up)"

else
    echo "--- device: $AP at $BASE; this machine: $START_NET on $IFACE"

fi

restore() {
    echo "--- putting $IFACE back"
    networksetup -setairportpower "$IFACE" off >/dev/null 2>&1
    sleep 2
    networksetup -setairportpower "$IFACE" on >/dev/null 2>&1
    i=0
    while [ $i -lt 45 ]; do
        now=$(networksetup -getairportnetwork "$IFACE" 2>/dev/null |
            sed -n 's/^Current Wi-Fi Network: //p' | grep -v 'not associated' || true)
        if [ -n "$now" ] && [ "$now" != "$AP" ]; then
            ok "rejoined ${now}"
            return 0
        fi
        sleep 1
        i=$((i + 1))
    done
    # An interface that was on nothing has nothing to rejoin - which is a pass, not a
    # failure: the wired connection this machine actually uses was never touched.
    ok "left $IFACE off the air (it was not on a network before)"
    return 0
}
trap 'restore; exit 1' INT TERM

echo "--- joining $AP"
# setairportnetwork answers before the interface has associated, and right after a power
# cycle it can miss a beacon entirely: three tries, a few seconds apart, then give up.
JOINED=0
for attempt in 1 2 3; do
    if networksetup -setairportnetwork "$IFACE" "$AP" "$PASSWORD" >/dev/null 2>&1; then
        if ifconfig "$IFACE" 2>/dev/null | grep -q 'inet 192\.168\.4\.'; then JOINED=1; break; fi
    fi
    echo "  note  join attempt $attempt did not take; waiting"
    sleep 5
done
if [ "$JOINED" -ne 1 ] && ! networksetup -getairportnetwork "$IFACE" 2>/dev/null | grep -q "$AP"; then
    bad "could not join $AP: raise it from Menu - Settings - Wi-Fi - Setup, or use a device with no network"
    restore
    exit 1
fi

i=0
UP=0
while [ $i -lt 60 ]; do
    # The lease arrives a few seconds after the association, and networksetup returns
    # before either: poll for the answer rather than for the command's exit code.
    if [ "$(send "$BASE/api/setup" /tmp/k7-setup.json 2)" = "200" ]; then UP=1; break; fi
    sleep 1
    i=$((i + 1))
done
if [ "$UP" -ne 1 ]; then
    bad "$BASE/api/setup never answered from the hotspot"
    restore
    exit 1
fi
ok "the device answers on the hotspot"

# ---------------------------------------------------------------- B1: the page
code=$(send "$BASE/setup" /tmp/k7-setup.html)
[ "$code" = "200" ] && ok "GET /setup is 200" || bad "GET /setup is $code"
grep -q 'Wi-Fi setup' /tmp/k7-setup.html 2>/dev/null && ok "the page is the setup page" \
    || bad "the page does not look like the setup page"
grep -q '2.4 GHz' /tmp/k7-setup.html 2>/dev/null && ok "it carries the 2.4 GHz line" \
    || bad "the 2.4 GHz note is missing"
code=$(send "http://k7.local/setup" /tmp/k7-setup-mdns.html)
if [ "$code" = "200" ]; then ok "http://k7.local/setup resolves on the hotspot (mDNS)"
else note "http://k7.local/setup answered $code: mDNS did not resolve here, which is why the panel prints an IP"; fi

# ------------------------------------------------------------ the state, the scan
code=$(send "$BASE/api/setup" /tmp/k7-state.json)
state=$(sed -n 's/.*"state":"\([a-z]*\)".*/\1/p' /tmp/k7-state.json)
if [ "$state" = "unprovisioned" ] || [ "$state" = "setup" ]; then ok "the state is $state"
else bad "the state is '$state', which is not a device waiting for a network"; fi
grep -q '"up":true' /tmp/k7-state.json && ok "the hotspot reports itself up" \
    || bad "the hotspot does not report itself up"
grep -q '"ssid":"K7.local"' /tmp/k7-state.json && ok "it is K7.local" || bad "the ssid is not K7.local"

code=$(send "$BASE/api/setup/scan" /tmp/k7-scan.json 15)
[ "$code" = "200" ] && ok "the scan answers" || bad "the scan is $code"
n=$(grep -o '"ssid":"[^"]*"' /tmp/k7-scan.json | wc -l | tr -d ' ')
[ "$n" -gt 0 ] && ok "the picker found $n network(s)" || bad "the scan found nothing"
if grep -o '"ssid":"[^"]*"' /tmp/k7-scan.json | sed 's/^"ssid":"//; s/"$//' | grep -qx "$AP"; then
    note "the scan lists the hotspot itself, as any radio would"
fi
FIRST_OTHER=$(grep -o '"ssid":"[^"]*"' /tmp/k7-scan.json | sed 's/^"ssid":"//; s/"$//' |
    grep -vx "$AP" | head -1)
if [ -n "$FIRST_OTHER" ]; then
    note "a join attempt will use '$FIRST_OTHER' with a deliberately wrong password"
else
    note "no other network is in range: the join checks are skipped"
fi

# ------------------------------------------------------------- B7: the stream, here
curl -s -D /tmp/k7-icy.txt -o /tmp/k7-stream.bin --max-time 4 "$BASE/stream" >/dev/null 2>&1
bytes=$(wc -c < /tmp/k7-stream.bin | tr -d ' ')
if [ "$bytes" -gt 4096 ]; then ok "the stream plays over the hotspot: $bytes B in four seconds"
else bad "the stream gave $bytes B in four seconds"; fi
grep -qi '^icy-name:' /tmp/k7-icy.txt && ok "with an ICY name header" || note "no ICY name in the first four seconds"

# ------------------------------------------------------- B2: a join that must fail
if [ -n "$FIRST_OTHER" ]; then
    code=$(post "$BASE/api/setup/join" /tmp/k7-joinbad.json \
        "{\"ssid\":\"$FIRST_OTHER\",\"password\":\"definitelywrong1\"}")
    if [ "$code" = "200" ] && grep -q '"ok":false' /tmp/k7-joinbad.json; then
        ok "a wrong password is refused by the radio: $(cat /tmp/k7-joinbad.json)"
    else
        bad "the wrong-password join answered $code: $(cat /tmp/k7-joinbad.json)"
    fi
    send "$BASE/api/setup" /tmp/k7-state2.json >/dev/null
    if grep -q '"last_attempt":{"ssid":"'"$FIRST_OTHER"'"' /tmp/k7-state2.json; then
        ok "the attempt is recorded for the page to report on reload: $(cat /tmp/k7-state2.json)"
    else
        bad "the attempt is not in /api/setup: $(cat /tmp/k7-state2.json)"
    fi
    code=$(post "$BASE/api/setup/join" /tmp/k7-joinbadshort.json \
        "{\"ssid\":\"$FIRST_OTHER\",\"password\":\"1234567\"}")
    [ "$code" = "400" ] && ok "a 7-character password is refused before it is tried" \
        || bad "a 7-character password answered $code"

    # The failed attempt was stored (P24), so the device would chase that network at every
    # boot for twenty seconds. Forget it, and prove the device is unprovisioned again.
    code=$(post "$BASE/api/setup/forget" /dev/null "{}")
    [ "$code" = "204" ] && ok "the wrong choice is forgotten again" \
        || bad "forget answered $code"
    send "$BASE/api/setup" /tmp/k7-state3.json >/dev/null
    grep -q '"state":"unprovisioned"' /tmp/k7-state3.json \
        && ok "and the device is unprovisioned, not chasing a wrong network" \
        || bad "the device is not unprovisioned after forget: $(cat /tmp/k7-state3.json)"
fi

# ------------------------------------------------------ B3: the join that works
if [ "$ASK_HOME_PASS" -eq 1 ] && [ -n "$FIRST_OTHER" ]; then
    printf 'password for %s: ' "$START_NET"
    stty -echo 2>/dev/null || true
    read -r HOME_PASS
    stty echo 2>/dev/null || true
    echo
    if [ -z "$START_NET" ]; then
        printf 'network name to join: '
        read -r START_NET
    fi
    HOME_SSID=${HOME_SSID:-$START_NET}
    code=$(post "$BASE/api/setup/join" /tmp/k7-joinok.json \
        "{\"ssid\":\"$HOME_SSID\",\"password\":\"$HOME_PASS\"}")
    if grep -q '"ok":true' /tmp/k7-joinok.json; then
        ok "the device joined $HOME_SSID"
        echo "  note  waiting up to 75 s for the hotspot to close; this machine will drop off it"
        i=0
        CLOSED=0
        while [ $i -lt 75 ]; do
            if [ "$(send "$BASE/api/setup" /dev/null 2)" != "200" ]; then CLOSED=1; break; fi
            sleep 3
            i=$((i + 3))
        done
        [ "$CLOSED" -eq 1 ] && ok "the hotspot closed on its own" || bad "the hotspot is still up after 75 s"
        restore
        i=0
        while [ $i -lt 20 ]; do
            if [ "$(send "http://k7.local/api/status" /dev/null 3)" = "200" ]; then
                ok "and the device answers on the LAN again"
                break
            fi
            sleep 1
            i=$((i + 1))
        done
        [ $i -lt 20 ] || note "http://k7.local/api/status did not answer from this machine: mDNS, not the device"
    else
        bad "the device did not join $HOME_SSID: $(cat /tmp/k7-joinok.json)"
        restore
    fi
else
    skip "B3 (a successful join closing the hotspot): re-run with --home-pass"
    restore
fi

# --------------------------------------------------------------- what needs you
skip "B6 (a reflash keeps the record): idf.py flash again and check /api/setup"
skip "B8 (the panel's Wi-Fi rows): Menu - Settings - Wi-Fi, by eye"

echo "--- board_setup: $FAIL failure(s), $SKIP skip(s)"
[ "$FAIL" -eq 0 ] || exit 1
