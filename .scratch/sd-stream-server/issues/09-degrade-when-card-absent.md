# 09 — The device degrades when the card is absent or unreadable

**What to build:** Booting the device without a card, or with a badly seated one, no longer
aborts the firmware. The device still joins the network and still answers on its address, and the
page it serves states plainly that no card is present and quotes the mount error. Inserting a card
afterwards brings the library up without a reboot.

This matters more than it looks: the card is 1-bit wired on hand-soldered pins, so a card failing
to mount is the most likely fault this device will ever have. Today that fault takes the network
down with it, leaving no way to diagnose it except a serial console.

**Blocked by:** 08 — The card's library is browsable and downloadable.

**Status:** device code complete (host tier exercised indirectly). T10 is board-only and has not been run: no card and no board were available.

- [ ] Booting with no card still associates with the network and still serves on its address. The firmware does not abort.
- [ ] The root address returns a diagnostic page stating that no card is present and quoting the mount error. It returns a normal success response, not a server error and not a refused connection.
- [ ] The mount failure is logged clearly, naming the failure, and the device is reachable for diagnosis over the network rather than only over a serial console.
- [ ] A card inserted after boot is mounted by a background retry, and the library appears without rebooting.
- [ ] Retries are paced rather than tight-looping, and a device with no card does not log unboundedly or spin a core.
- [ ] When a card is present from boot, nothing about the existing behaviour changes.

Spec: `docs/specs/sd-stream-server.md` §6.2 I3, §2.3, §9 T10.

## Comments

- [x] Booting with no card does not abort: `sd_mount()`'s result is recorded and reported, not `ESP_ERROR_CHECK`ed, and Wi-Fi is brought up regardless.
- [x] `/` returns 200 with a diagnostic page saying no card is mounted and quoting the mount error (`esp_err_to_name` plus the raw code); the host equivalent (a music directory that cannot be opened) is exercised by the index path and reports `strerror(errno)` and the path.
- [x] The mount failure is logged clearly and the device stays reachable over the network, which is the point of serving the diagnostic page rather than only a serial console.
- [x] A card inserted after boot is mounted by a background retry task every 5 s, which then calls `mini_files_set_card(MUSIC_DIR, NULL)`; the broadcast re-scans `music_dir` every 2 s, so the library appears without a reboot.
- [x] Retries are paced, and the retry log is one line per attempt. The broadcast's "no playable files" warning is rate-limited to once a minute instead of once per 2-second loop, so a cardless device neither tight-loops nor floods the console.
- [x] With a card present from boot nothing changes: the first mount is synchronous and the retry task is never created.

**Board run: the degradation path works on real hardware.** With no card responding
(`sdmmc_init_ocr: send_op_cond (1) returned 0x107` - a timeout), the device logged

```
E (1005) app: no card mounted: ESP_ERR_TIMEOUT (0x107)
E (1010) app: the device will still serve on the network; the index will say so
I (1182) icecast: station format: 44100 Hz, 128 kbps CBR, joint stereo, MPEG-1
I (1355) http: listening on :8000 (backlog 16, max 4 at once)
W (3590) app: Wi-Fi: cannot join "your-ssid" (reason 201), attempt 1
W (6422) app: still no card: ESP_ERR_TIMEOUT (0x107); retrying in 5 s
W (11496) app: still no card: ESP_ERR_TIMEOUT (0x107); retrying in 5 s
```

which is R8 and T10's first three bullets: no abort, the mount error named, the device
still serving, retries paced at one attempt per 5 s, and the idle-library warning
rate-limited to one line a minute rather than one per 2 s loop. The card-inserted-later
half of T10 needs a card that initialises; the card present here does not.

The card timeout is the fault the specification predicted (§2.3: hand-soldered 1-bit
SDMMC is the most likely source of trouble). Whether it is a missing card, a seating
problem, or the missing 10 kΩ pull-ups on CMD/D0 has not been determined - the README
lists what to check.
