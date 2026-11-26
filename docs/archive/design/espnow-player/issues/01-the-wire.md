# 01 — The wire

**What to build:** the bytes one MP3 frame travels in over ESP-NOW: the six-byte fragment header,
the fragmenter, the parser, and the status packet that carries the receiver's credit and the
sequences it missed. Portable, with no radio in it, so both firmwares share one implementation and
the host can prove it.

**Blocked by:** nothing.

**Status:** done and accepted on the board — host tier, `t43_espnow_stream`, 101 checks.

- [x] A frame of every legal length fragments and reassembles byte for byte; a 417-byte frame is two packets and a 1 441-byte one is six.
- [x] A frame that needs a seventh fragment is refused rather than truncated.
- [x] A packet with a wrong version, a wrong type, an index past its count, a zero or over-long count, or a short non-final payload is refused.
- [x] The status packet round-trips its credit, its highest sequence and its list of up to eight gaps; a truncated or wrong-version one is refused.

Spec: `docs/specs/espnow-stream.md` E1–E3; `docs/adr/0103`.

## Comments

- **The version byte is the handover's.** `0x01` beacon, `0x02` request, `0x03` record were already
  on this radio, so audio is `0x04` and status `0x05`, and one dispatcher can serve both without a
  length-prefix collision. `ES_MAX_NACK` is eight because the reorder window is wider than that.
- **250 bytes, not v2's 1 470.** The handover's frames are v1 and share the radio; a frame that
  needs more than six fragments is not an MP3 frame this encoder can make.
