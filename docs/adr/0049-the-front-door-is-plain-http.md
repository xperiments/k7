# The front door is plain HTTP, and the snapshot is posted to the device

ADR 0048 decided the device would serve the analysis app, and assumed it would do so over
TLS with a shipped certificate. That was built, flashed, and it worked: `https://xtape.local`
served the app, `/api/status` answered, and `curl --cacert tls/ca.pem` verified the chain.

It was also the wrong trade, for a reason no amount of care on the device can fix. A browser
warns about a certificate it cannot chain to a trusted root, and the only ways past that
warning are an operating-system trust change once per machine — a dialog or a command, which
is exactly the ceremony the owner wanted to avoid — or a certificate from a public CA, which
means a domain, DNS for every device, and renewals. Everything else (an iframe from a trusted
origin, a cached public page, a `ws://` socket) is blocked by secure-context inheritance, mixed
content, or storage partitioning.

## Decision

- **The device speaks plain HTTP.** One listener on port 80 serves the app, `/api/*`, `/mcp`,
  `/library` and `/stream`. There is no certificate in the image, no trust step, and no
  `/setup` page, because there is nothing to install.
- **The card stays in the computer.** The browser reads it there, through a dropped folder or
  a file input; it never needs a directory handle, which is the only card operation that was
  secure-context-only.
- **`xtape.db` is posted to the device and flashed.** `POST /api/snapshot` takes the image,
  validates it in full before erasing anything, writes the snapshot partition, verifies the
  bytes, and restarts. The measurements are a few kilobytes; the network carries them, and
  the card never has to travel.
- **What the app loses, it replaces.** `crypto.subtle` is absent on an insecure origin, so a
  pure-JS SHA-256 does the same job (slower, and only the weights notice). The service worker
  cannot register, so there is no installable PWA and no precached shell. The picker is absent,
  which no longer matters because the snapshot is sent rather than written through a handle.

## What this costs

- A browser shows "Not secure" in the address bar. No interstitial, no warning page, and no
  dialog — but the chip is there, and it is honest: the device is on a LAN and serves a tool,
  not a bank.
- **Any page on the LAN can POST a snapshot**, because there is no origin the device could
  check that a hostile page cannot forge. The mitigation is the content type:
  `application/octet-stream` is not CORS-safelisted, so a cross-origin page cannot send it
  without a preflight the device never answers, and a form cannot produce it. What arrives is
  measurements, validated before use; the device still accepts no Track.
- Nothing about the protocol is encrypted. That was already true of `/stream` and of the
  library index, and it is now true of everything.

## What stays possible

`tls/` and `tools/make-cert.sh` remain in the tree, unused by the firmware, for a device that
serves TLS again — the transport is one `http_loop.c` and a listener configuration, and the
certificate path is a component away. A device that wants a green padlock needs a name in
public DNS pointing at its LAN address, not a better certificate.
