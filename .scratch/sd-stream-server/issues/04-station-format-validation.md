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

**Status:** done (host tier). T12's board half has not been run.

- [ ] An unsupported station rate is refused at startup with a diagnostic naming the rate. The server does not reach the point of serving audio.
- [ ] A rate and bit rate the encoder cannot represent together is refused the same way, naming both values.
- [ ] A supported configuration at the **lowest** MPEG tier starts successfully and logs that tier. The validity check accepting a zero return value is asserted directly by a host test, not inferred.
- [ ] A station rate the encoder accepts is never rejected merely because the validity check returned the lowest tier.
- [ ] The chosen station format — rate, bit rate, channels and MPEG tier — is logged at startup.
- [ ] A client-limit value outside its documented range is rejected with a diagnostic. Values inside the documented range are honoured exactly as given, not silently reduced to a fixed number.
- [ ] The recommended default format remains valid and is what the firmware uses when nothing is configured.

Spec: `docs/specs/sd-stream-server.md` §4.3 (F1–F3), §4.1 E1, §5 D3, §9 T12.

## Comments

- [x] An unsupported station rate is refused at startup, naming it: `station rate 44101 Hz is not one the encoder accepts; use one of ...` and the process exits non-zero before serving anything (checked over HTTP by `host/tests/http_tests.sh`).
- [x] A pair the encoder cannot represent is refused naming both values: `bit rate 128 kbps is not one the encoder can use at 8000 Hz`.
- [x] A configuration at the **lowest** MPEG tier starts and logs that tier, and the zero return value is asserted directly by `host/tests/t04_format_validation.c` and by starting a real 8000/64 server on the host and probing its stream (ffprobe reports 8000 Hz).
- [x] A rate the encoder accepts is never rejected for returning the lowest tier (`shine_check_config(8000,64) >= 0`).
- [x] The chosen format is logged: `station format: 44100 Hz, 128 kbps CBR, joint stereo, MPEG-1`, plus the encoder's frame size and state size.
- [x] `max_clients` outside 1..4 is refused with `max_clients 5 is outside the documented 1..4 range`; values inside the range are honoured exactly (`--max-clients 4` logs `sends at most 4 clients`), not coerced to a fixed number.
- [x] The recommended default (44100/128) is what the firmware uses when nothing is configured: `main/main.c` sets only `music_dir`, `station_name`, `port` and `pcm_cb`, and the startup log confirms 44100/128/MPEG-1.

One extra: the encoder frame is **576** samples at MPEG-2/2.5 rates, not 1152, so the accumulator uses `shine_samples_per_pass()` rather than a constant. A hard-coded 1152 would have produced garbage at 8/11.025/12/16/22.05/24 kHz stations.

**Post-review note on the client limit.** `0` still means "leave it at the documented default" (2),
which is the convention every other field in `mini_icecast_cfg_t` follows (`out_hz`, `bitrate`,
`metaint`, `lead_ms`, `burst_ms` all treat 0 that way). The defect D3 describes was that *in-range*
values - 1, 3, 4 - were silently coerced to 2, and that is gone: the host suite now starts a server
with `--max-clients 1`, `4` and `0` and checks the log says 1, 4 and 2 respectively, and that `5` is
refused. The convention is stated in the header next to the field so it cannot be read as coercion.

**Board run of T12: passed, and the failure mode was improved because of it.** With
`out_hz = 44101` flashed, the device logs

```
E (1057) icecast: station format refused: station rate 44101 Hz is not one the encoder accepts; use one of 44100 48000 32000 22050 24000 16000 11025 12000 8000
E (1071) app: not serving: ESP_ERR_INVALID_ARG
E (1075) app: fix out_hz in main.c and reflash; the device is idle until then
```

and then nothing: no listener, no Wi-Fi, and no answer on port 8000. The first version of
this path used `ESP_ERROR_CHECK(err)`, which aborted - so the device rebooted, printed the
same message, and rebooted again. Loud, but a reboot loop hides everything else in the log
and thrashes the console. It now halts with the reason, and the station is validated
*before* the network comes up, so a refused format cannot leave the device sitting on the
network with nothing listening.
