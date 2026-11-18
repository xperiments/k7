# 01 — The handover's frames and its key, in portable code

**What to build:** The bytes both firmwares have to agree on, as one module the host can test
alone: the three frame types (beacon, request, record), their encode and decode, the 250-byte
ceiling asserted where the record is written, and the derivation of the LMK from the shared
secret.

It is first because nothing else in the feature can be tested without it, and because it is the
only part of the handover that two independent implementations could silently disagree on.

**Blocked by:** None — can start immediately.

**Status:** done (host tier, plus a target-compiler check). Nothing has crossed a radio: the two
boards are PL-09's.

- [x] Beacon, request and record encode and decode, and a truncated frame, an unknown version and an unknown type are each refused.
- [x] The longest legal record is 235 B, and the encoder refuses anything over `ESP_NOW_MAX_DATA_LEN` (250) rather than trusting arithmetic.
- [x] Both sides derive the same 16-byte LMK from a fixed pair of keys; a different transcript derives a different one; an all-zero shared secret is refused.
- [x] The module compiles for the host and for the ESP32 with no target conditionals.

Spec: `docs/specs/network-handover.md` § 5, T1–T4.

## Comments

- The module is `components/portable/handover/`: `handover_frame.c` (the three frames and
  CRC-8/ATM) and `handover_key.c` (SHA-256, HMAC, HKDF and `ho_lmk`), with `handover.h` as the
  contract. The host build compiles the same two files as the firmware, and `t41_handover.c`
  is the host tier: **97 checks, 0 failures**.
- **The spec gained two details before the code did**, because two implementations that guessed
  differently would only fail on a board: the record's checksum is named as CRC-8/ATM (poly
  0x07, init 0x00) with its check value 0xF4, and H27 now states that the `info` is always 78
  bytes (14 for `k7-handover-v1`, then the two 32-byte keys) and that an absent salt is RFC
  5869's 32 zero bytes.
- **The curve is deliberately not here.** `ho_lmk()` takes the X25519 shared secret; both boards
  will call mbedTLS for it (PL-02 and PL-04). Writing a second X25519 in this repository would
  be a second implementation of a security primitive, and the host build links no crypto library
  to test one against. The hash *is* here, because the KDF is ours to get wrong and has to be
  testable where it is cheap to change.
- **The vectors are independent of the code.** SHA-256 against FIPS 180-4, HMAC against RFC
  4231, HKDF against RFC 5869's published OKMs, and the LMK against the same arithmetic in
  Python's `hashlib`/`hmac` — which reproduces all three RFC 5869 vectors before it is trusted.
- **Two mutations were planted to prove the test bites**: `k7-handover-v1` → `v2` failed the LMK
  vector only; the CRC polynomial 0x07 → 0x1D failed the checksum check only. Both were reverted
  and the suite is green.
- **The target compiler found a warning clang did not**: GCC 15 for the ESP32 refused the CRC's
  ternary under `-Werror` for a signedness change. Fixed with an explicit branch; both files now
  compile for `xtensa-esp32-elf` with `-Wall -Wextra -Werror`, at **3,238 B of text and no RAM**
  (1,528 frame + 1,710 key) — the flash cost of the whole handover's logic.
- **`make -C host check` is green except its `label` step**, which fails in this environment
  before it reaches any label assertion: headless Chrome's GPU process dies and the page renders
  empty (`label_boot_tests.py`). Unit tests, layering and integration all pass; the label target
  touches nothing this ticket changed.
