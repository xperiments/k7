# 20 — The card is not unmounted under an open file

**What to build:** make the card watcher's removal path safe for the files it shares with the
broadcast and the artwork loader — [`docs/specs/sd-stream-server.md`](../../../../specs/sd-stream-server.md) G2.

A failing card produced this on the board:

```text
W (563865) app: the card stopped answering (I/O error); the index will say so until it returns
E (563886) spi_masterGuru Meditation Error: Core 0 panic'ed (LoadProhibited)
PC: dir_find at .../fatfs/src/ff.c:2454
  follow_path -> f_open -> vfs_fat_open -> open -> fopen -> mi_dec_open -> src_task
```

The watcher called `app_board_card_forget()` — `esp_vfs_fat_sdcard_unmount` — while the decoder
was opening the next Track. FatFS had freed the volume; `dir_find` read `dp->dir` on it. The
artwork loader and the rescan share the same card, so the decoder is only the path that was
caught.

- **A card file has a lifetime.** `app_services_card_use_begin()` before a decoder or artwork
  file is opened, `end()` after it is closed. The decoder holds it for its whole open (not just
  `fopen`); the artwork loader holds it across its read/decode.
- **The watcher drains before it forgets the volume.** On removal it publishes the no-card
  generation, closes the gate, calls `mini_icecast_skip()` so the current decoder closes, and
  waits up to five seconds for every holder. Only then does it take the card lock and
  `app_board_card_forget()`. A timeout leaves the volume mounted and the retry path tries
  again; a mounted volume is recoverable, a freed one is not.
- **The retry path drains first too.** A card that returns is mounted only after the gate is
  closed and any volume a previous removal left is dropped.
- **A closed gate does not spin the broadcast.** A failed open now yields 50 ms, so a card
  that cannot open anything does not starve the watcher that is recovering it.

**Status:** done on the host; the board's panic path is the proof still owed.

- [x] `make -C host check` green, including the card watcher's ejection and reinsertion.
- [x] The decoder and the artwork loader take and release the gate on every path, including
      a failed `fopen`.
- [x] The watcher drains before `app_board_card_forget()` and resumes after a mount.
- [ ] A pulled card on the panel is reported as `no_card` without a panic, and the reinserted
      card comes back (board).

**Files:** `components/portable/app_services/app_services.c` and `.h`,
`components/portable/mini_icecast/mini_icecast.c` and `include/mini_icecast.h`,
`components/portable/mini_icecast/mi_decode.c` and `include/mi_decode.h`,
`components/app/screen_display/artwork_loader.c`.

**Tests:** `make -C host check` (the card watcher integration); the board is the only place a
real unmount happens.

**Source:** [`sd-stream-server.md`](../../../../specs/sd-stream-server.md) G2.
