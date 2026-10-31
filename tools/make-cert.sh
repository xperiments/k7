#!/bin/sh
# The certificate every image ships (ADR 0048).
#
# One leaf for the name every player answers to, signed by a CA that stays offline. The
# leaf's key is public by construction - it is compiled into every firmware - so this file
# exists to make that explicit rather than to protect anything. The CA key must never be
# committed: it signs this one name and nothing else.
#
#   tools/make-cert.sh          # create tls/ if it is not there
#   tools/make-cert.sh --force  # replace it, which re-flashes every device
#
# The trust step, once per client:  security add-trusted-cert -d -r trustRoot \
#   -k "$HOME/Library/Keychains/login.keychain-db" tls/ca.pem
set -eu

cd "$(dirname "$0")/.."
OPENSSL=${OPENSSL:-/opt/homebrew/bin/openssl}
[ -x "$OPENSSL" ] || OPENSSL=$(command -v openssl)

if [ -f tls/xtape.local.pem ] && [ "${1:-}" != "--force" ]; then
    echo "tls/ already exists; --force replaces it"
    exit 0
fi

mkdir -p tls
# A local CA is exempt from the 398-day cap, so both halves are long-lived by design.
"$OPENSSL" req -x509 -newkey rsa:2048 -sha256 -days 3650 -nodes \
    -keyout tls/ca.key -out tls/ca.pem \
    -subj "/O=xtape/CN=xtape local CA" \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,keyCertSign,cRLSign" >/dev/null 2>&1

"$OPENSSL" req -new -newkey rsa:2048 -nodes \
    -keyout tls/xtape.local.key -out tls/xtape.local.csr \
    -subj "/CN=xtape.local" >/dev/null 2>&1

cat > tls/leaf.ext <<EXT
basicConstraints=CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=DNS:xtape.local,DNS:localhost,IP:127.0.0.1
EXT

"$OPENSSL" x509 -req -in tls/xtape.local.csr -CA tls/ca.pem -CAkey tls/ca.key \
    -CAcreateserial -days 1825 -sha256 -extfile tls/leaf.ext \
    -out tls/xtape.local.pem >/dev/null 2>&1

rm -f tls/xtape.local.csr tls/leaf.ext tls/ca.srl
echo "wrote tls/ca.pem, tls/xtape.local.pem, tls/xtape.local.key"
"$OPENSSL" x509 -in tls/xtape.local.pem -noout -subject -dates -ext subjectAltName 2>/dev/null | sed 's/^/  /'
