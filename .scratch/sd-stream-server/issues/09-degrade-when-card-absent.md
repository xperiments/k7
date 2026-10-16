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
