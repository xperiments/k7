# 04 — Station format is validated before anything is served

**What to build:** The station rate and bit rate are checked against the encoder at startup. An
unsupported rate, or a rate and bit rate the encoder cannot represent together, stops the server
with a diagnostic naming the offending value — instead of resampling audio into an encoder that
cannot accept it. A supported combination starts and logs the chosen format and its MPEG tier.

The lowest MPEG tier's value is zero, so the validity check must treat "not an error" as valid;
a truthiness check would reject every configuration at that tier while passing every test at the
recommended default. That is the single most important assertion in this ticket.

The same startup pass also stops silently second-guessing the documented client limit.

**Blocked by:** 03 — Vendor the encoder with its licence obligation, and pin its documented behaviour.

**Status:** ready-for-agent

- [ ] An unsupported station rate is refused at startup with a diagnostic naming the rate. The server does not reach the point of serving audio.
- [ ] A rate and bit rate the encoder cannot represent together is refused the same way, naming both values.
- [ ] A supported configuration at the **lowest** MPEG tier starts successfully and logs that tier. The validity check accepting a zero return value is asserted directly by a host test, not inferred.
- [ ] A station rate the encoder accepts is never rejected merely because the validity check returned the lowest tier.
- [ ] The chosen station format — rate, bit rate, channels and MPEG tier — is logged at startup.
- [ ] A client-limit value outside its documented range is rejected with a diagnostic. Values inside the documented range are honoured exactly as given, not silently reduced to a fixed number.
- [ ] The recommended default format remains valid and is what the firmware uses when nothing is configured.

Spec: `docs/specs/sd-stream-server.md` §4.3 (F1–F3), §4.1 E1, §5 D3, §9 T12.

## Comments
