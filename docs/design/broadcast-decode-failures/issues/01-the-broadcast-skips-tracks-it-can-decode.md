# 01 — The broadcast skips tracks it can decode

**What to build:** A broadcast that keeps playing when it is under memory pressure: a track it
cannot open is a report about *this device's headroom*, not about the file, and a track change
does not cost the listeners a gap.

**Status:** done, verified on the board. The files were never the problem; the device's internal
RAM was.

**Blocked by:** None.

## What was seen

The K7's console, over 280 seconds while one player was listening:

```
E (1448590) sdmmc_cmd: allocate_dma_buf: not enough mem, err=0x101
E (1448590) diskio_sdmmc: sdmmc_read_blocks failed (0x101)
W (1448590) app: the card stopped answering (I/O error); the index will say so until it returns
W (1448825) icecast: cannot decode .../04 - Da Funk.mp3 (unsupported or corrupt): skipping
```

- **76 "cannot decode … skipping" lines in 280 s**, on ordinary MP3s — every track in the walk,
  four to seven times each.
- The internal free block dipped to **5 539 B (largest 2 944 B)** and recovered to 36 587 B.
- The SD driver's own error is the first line: **it could not allocate a DMA buffer**, so the
  card stopped answering, and every open after that failed. The "unsupported or corrupt"
  message was the *symptom* of a card the device had lost.

## The chain, measured

A census added to the lines that fire at those moments found the cost in one place:

| Moment | internal free | largest |
|---|---|---|
| a track opens | 77 679 B | 31 744 B |
| **a stream client connects** | **40 731 B** | **31 744 B** |
| steady state, one client | 5 539 B | 2 944 B |

`CONFIG_LWIP_TCP_SND_BUF_DEFAULT=32768`: every TCP connection took a **32 KB send buffer from
internal RAM**, on a device that has ~37 KB free once its panel, card, encoder and Wi-Fi are up.
The next allocation to ask for internal DMA memory was the SD driver's, and it failed — taking
the card, the library and every decode with it.

## The fix

1. **`CONFIG_LWIP_TCP_SND_BUF_DEFAULT` 32768 → 8192.** The burst never needed to sit in one socket
   write: a client's ring is in PSRAM and its own sender task drains it at the network's pace.
   8 KB is 250 ms of the station. `sdkconfig.defaults` carries the reasoning.
2. **`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` on the Sunton**, restoring the fallback the variant
   had disabled on the premise that 155 KB internal is free — true at boot, false at steady state.
3. **The four sender tasks' stacks moved to PSRAM** (internal fallback kept), the same rule the
   HTTP workers already follow: this task is blocked on a socket all its life, and four 4 KB
   internal stacks were 16 KB the card's DMA could not use.
4. **The decoder says why it refused.** `mi_dec_open()` now reports `format / memory / card /
   file / codec / scratch`, and the broadcast logs that instead of calling every failure
   "unsupported or corrupt".

## Verified on the board, 2026-11-18

```
I (9362)  client 0 connected (icy=0, burst=19226 B); internal 54479 B free, largest 31744 B
I (64266) memory: internal 56211 B free (largest block 31744 B), dma largest 31744 B,
          external 1539764 B free; starvation rebases 0, ring starved 1 ms
```

Over the following 110 s, with a player connected: **0 SD failures, 0 decode skips, 0 starvation
rebases**. Before the fix the same shape took ~90 s to lose the card.

## What this leaves

- The README's internal-RAM figures are now conservative (56 KB free where it recorded ~35 KB);
  they belong to the next board-tier run rather than to this ticket.
- The periodic census now carries the **DMA largest block**, which is the number that actually
  constrains this firmware: `esp_wifi_init()` needs roughly 25 KB of contiguous DMA-capable
  internal RAM, and a fragmented heap with plenty free can still fail to produce it.
