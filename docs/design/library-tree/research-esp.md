# ESP32-S3 / ESP-IDF constraints for a boot-built music index

2026-10-19. Repo: `/Users/pedrocasaubon/Documents/xtape` (ESP32-S3 N16R8, 8 MB octal PSRAM,
SD over SPI, FATFS). Sources: local **IDF v6.1** (`~/esp/esp-idf-6.1`, the tree both build dirs
use), IDF **v5.4.4** for deltas, FatFs R0.16 upstream docs, repo. **[V]** verified from the
source/doc named; **[E]** estimate with inputs cited; **[U]** unverified. "Sector" = FatFs logical
sector.
## Bottom line

1. Runtime sector is **512 B**, not the 4096 in `sdkconfig`. `CONFIG_FATFS_SECTOR_4096` sizes only
   the partition generator (`fatfs/Kconfig:24-36`); at mount FatFs takes the card's
   `csd.sector_size` via `GET_SECTOR_SIZE` (`ff.c:3497-3512`, `diskio_sdmmc.c:102-103`). **[V]**
2. **No directory cache.** FatFs has one 512 B window (`fs->win`); VFS adds a one-entry FILINFO
   stat cache for readdir→stat. A full walk re-reads every directory sector. **[V]**
3. A walk costs ~**1 sector per 16 short names**, or per **~1.45 long (128-char) names**. At the
   repo's measured ~250 KB/s (1.9 ms/512 B), a 5,000-track index is **1.2-6.6 s per request**;
   memory serving removes that. **[E, inputs V]**
4. Hard path limit is the **VFS: ~1024 bytes**, silently truncated by `snprintf` before FatFs
   sees it (`vfs_fat.c:61-62,381-390`). `FILENAME_MAX` is **1024** in the real build (verified by
   preprocessing `vfs_fat.c` with the IDF 6.1 flags; the `255` fallback in `vfs_fat.c:29` is
   inactive because libc `stdio.h` defines it). The earlier 255 reading was `sys/config.h`, not
   the active definition. **[V, corrected 2026-10-19]**
5. **PSRAM** is the right home: largest free block measured 8,126,464 B. Unaligned 32-bit access
   is legal on LX7; packed structs are safe but compile to byte loads. Little-endian. **[V]**
6. **No bundled cJSON on IDF 6.1** — removed in 6.0, now `espressif/cjson`. Use a bounded writer
   plus jsmn for POST. **[V]**
## 1. What opendir / readdir / stat cost

**Geometry.** Dirent 32 B (`SZDIRE`, `ff.c:189`) → **16 entries per 512 B sector**; a 16 KB cluster
holds 512 entries. An LFN name uses `ceil(len/13)` 32 B entries + one SFN: **128 chars = 10+1 = 11
entries = 352 B**, so at most one such name per sector (1.45 average); 8.3 names are 16/sector. **[V]**

**Caches.** Only these exist:

| Cache | Holds | Consequence |
|---|---|---|
| FatFs `fs->win` (`ff.c:1081`) | one 512 B sector for FAT **and** dirs | `readdir` inside a sector is free; each new sector is one single-block `disk_read(...,1)` (CMD17); a cluster boundary also reads a FAT sector, evicting the dir sector. **[V]** |
| VFS stat cache (6.1 per open `DIR`: `vfs_fat.c:816-841,1063`; 5.4 one per-volume slot: `731-760,936-953`) | last `FILINFO` from `readdir`, keyed by path | `stat()` **immediately after** `readdir()` on the same path does zero I/O; a miss falls back to `f_stat`. **[V]** |
| `CONFIG_FATFS_PER_FILE_CACHE=y` (`ffconf.h:287`, `FF_FS_TINY=0`) | a private sector buffer per open `FIL` | file I/O only; no effect on directory reads. **[V]** |

No readdir/inode/name cache exists; the stat cache is one-shot (and in 5.4 a single global slot).

**Per call.**

- `opendir(path)` → `follow_path` (`ff.c:4763`): one `dir_find` per component, each scanning that
  dir from entry 0 → ≈ `Σ position/16` sectors. Opening `/sdcard/music` scans the root only. It
  allocates a `vfs_fat_dir_t` from the heap and does **not** consume a `max_files` slot
  (`vfs_fat.c:776,996-1015`). **[V]**
- `readdir` → 0 I/O within a sector, 1 sector per 16 entries (+FAT at cluster boundaries). No
  `stat` needed: size/attribute/time come from the dirent (`get_fileinfo`, `ff.c:2655`), and VFS
  fills `dirent.d_type` from the attribute (`vfs_fat.c:1090-1095`). **[V]**
- `stat(path)` → FatFs reads **directory metadata only**, never the FAT chain (upstream
  `stat.html`). VFS cache hit: 0 I/O. Miss: full `dir_find` from sector 0 → `O(position)` per
  call, `O(N²)` for N calls. **[V]**
- `closedir` → no I/O. **[V]**

`music_dir_list()` (`music_dir.c:67-95`) does readdir→stat on the just-returned name, exactly the
cached pattern: free on both 5.4.4/6.1 **unless another readdir interleaves** (5.4's global slot
can be evicted by the broadcast; 6.1 per-DIR cannot). On a miss the O(N²) behaviour is real. **[V]**

**Kconfig effects.**

- `CONFIG_FATFS_MAX_LFN=128` → `FF_MAX_LFN=128` (`ffconf.h:155`), 258 B heap working buffer per
  open object (`ff.c:551`). Names >128 UTF-16 units lose their LFN and surface as the **8.3 alias**
  (`pick_lfn`→0; `get_fileinfo`; upstream `readdir.html`) — `Café…flac` can list as `CAF_1234.FLA`.
  It does **not** shrink on-card LFN entries, so it does not reduce scan I/O. **[V]**
- `CONFIG_FATFS_USE_FASTSEEK` (off) accelerates `lseek` via a CLMT; irrelevant to walking
  (`ffconf.h:42`, upstream `config.html`). **[V]**
- `PER_FILE_CACHE` sizes `FIL` buffers only. `ALLOC_PREFER_EXTRAM=y` puts FatFs buffers in PSRAM,
  a documented hot-path cost. `SECTOR_*` is generator-only. **[V]**

**Walk model.** `sector_reads ≈ Σ ceil((ceil(L_i/13)+1)/16)` + FAT sectors. At the repo's measured
16 KB/62 ms = 250 KB/s (`mini_files.c:375-383`, README.md:376), ~1.9 ms per 512 B: **[E]**

| Library | Names | Dirents/file | Sectors | Time |
|---|---|---|---|---|
| 5,000 | 32 chars | 4 | 1,250 | ~2.4 s |
| 5,000 | 128 chars | 11 | 3,448 | ~6.6 s |
| 20,000 | 32 chars | 4 | 5,000 | ~9.5 s |
| 20,000 | 128 chars | 11 | 13,793 | ~26 s |

Add 20-50 % for path resolution, FAT, switches, pacing. Per-request walking is seconds, not ms.
## 2. Lengths, paths, depth

| Limit | Value | Source |
|---|---|---|
| `FF_MAX_LFN` | 12..255; **128** here | `fatfs/Kconfig:118-121`, `ffconf.h:155` |
| LFN working buffer | `(128+1)*2` = **258 B** heap/open object | `ff.c:551` |
| `FILINFO.fname[]` | **256 B** (`FF_LFN_BUF+1`) | `ffconf.h:191`, `ff.c:2655` |
| VFS temp path | **1027 B** (`FILENAME_MAX+3`); prepends `"0:"` | `vfs_fat.c:61-62,381-390`; preprocessed |
| `FILENAME_MAX` | **1024** (active); 255 only if undefined | libc `stdio.h`; `vfs_fat.c:29` fallback |
| HTTP path / query / header | **512 / 256 / 1024** | `http_server.h:33-35` |
| Today's name / joined path | 128 / **256** | `music_dir.h:21-22` |

**Answer.** 128-byte names fit (FatFs `FILINFO` is 256 B). **The VFS ceiling is ~1024 bytes, not
255**: `vfs_fat_opendir/stat/open` build `"0:"+path` into `tmp_path_buf[FILENAME_MAX+3]`, and
`FILENAME_MAX` is 1024 in this toolchain, so >~1024 chars are silently truncated — wrong path or
`ENOENT`, no error. This is independent of `FF_MAX_LFN`. FatFs itself has no total-path or FAT32
depth limit. Nested URLs hit `HTTP_PATH_MAX` 512 on percent-encoded paths (414,
`http_parse.c:77`); the 1024 B header buffer (`http_server.c:186`) is the ceiling, so a path longer
than ~511 decoded bytes is playable by id but not addressable by a `GET /music/...` URL.

**Recommendation:** a `FILENAME_MAX + 1` scratch buffer, and enforce **absolute path
≤ `FILENAME_MAX` (1024)** in the scan (skip + log) so truncation can never happen. `/sdcard/music/`
eats 14, leaving ~1010 B, which is deep enough for normal collections. If a future libc leaves
`FILENAME_MAX` undefined, the 255 fallback returns; log/assert it at boot.
## 3. PSRAM

- **Octal, 80 MHz** (`CONFIG_SPIRAM_MODE_OCT`, `SPIRAM_SPEED_80M`; boot log README.md:177-181).
  Octal 120 MHz is experimental/temperature-sensitive (`esp_psram/esp32s3/Kconfig.spiram:71-83`). **[V]**
- Mapped through cache; working sets >32 KB lose the cache benefit and evict cached flash
  (`external-ram.rst:229`). Octal 8-bit DDR at 80 MHz is ~160 MB/s theoretical, quad ~half; real
  cache-miss throughput is lower. **[V doc, E bandwidth]**
- `heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`; blocks are **4-byte aligned**
  (TLSF `ALIGN` = `sizeof(void*)`, `multi_heap.c:81-82`), a few bytes overhead per block. Use
  `heap_caps_aligned_alloc` for cache-line alignment. **[V align, E overhead]**
- **Unaligned 32-bit access works**: `XCHAL_UNALIGNED_LOAD/STORE_HW=1`, no exception
  (`xtensa/esp32s3/.../core-isa.h:202-205`), and IDF's GCC guide says all ESP Xtensa/RISC-V chips
  allow it with no penalty (`release-5.x/5.0/gcc.rst:55`). Generic fatal-errors wording does not
  apply to S3. **[V]**
- **Packed caveat:** cross-compiling with the repo's `xtensa-esp32s3-elf-gcc` shows an explicit
  unaligned `uint32_t*` load → `l32i` (works, **not atomic**), while a packed-struct `uint32_t`
  member → four `l8ui` byte loads. Packed is safe but slower; keep the hot array naturally aligned,
  use packed only for the wire blob (read fields with `memcpy`). **[V]**
- **Little-endian** (`__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__`); declare the blob LE. **[V]**
- PSRAM is **not DMA-capable** and is inaccessible while the flash cache is off (flash write)
  (`external-ram.rst:219`). Never build the index during an NVS write. **[V]**

**Internal vs PSRAM.** Measured internal: 157,787 B free / 81,920 B largest; 35,327 B / 18,432 B
during playback (README.md:767-770,838; `docs/adr/0004-ui-memory-is-external.md:39-42`). PSRAM:
7.98-8.19 MB free, **8,126,464 B largest block**. A 1-4 MB index cannot live in internal RAM;
PSRAM costs cache-miss latency, not capacity, and sequential build/send is the friendly pattern.
**[V/E]**
## 4. Allocator

- `heap_caps_get_largest_free_block()` is the largest single allocation; tracking it against free
  detects fragmentation (`heap_debug.rst:21`). Measured 8,126,464 B external (README.md:838), so a
  multi-MB PSRAM arena is practical at boot. **[V]**
- **One arena beats many small allocations**: TLSF metadata per block, fragmentation, and a flat
  array needs one pointer. 5,000 × 64 B records ≈ 320 KB + ~40 KB metadata and 5,000 free-list
  operations; the repo's policy is explicit placement (`ADR-0004:39-42`). **[E]**
- Allocate early — after mount, before the screen (`app_services.c:75-130`; screen last at
  `appliance.c:174`). Size from `heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)`, fall back
  smaller rather than failing boot. **[V placement, E sizing]**
- Not DMA, not a stack unless `CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM`; the HTTP workers already
  use explicit external-preferred stacks (`http_server.c:250-265`). **[V]**
## 5. JSON

**cJSON is not in IDF 6.1.** 5.4.4 bundles cJSON **1.7.19** (`components/json/cJSON/cJSON.h:82-84`);
6.0 removed `components/json` and points at managed `espressif/cjson: "^1.7.19"` with no code
change (`release-6.x/6.0/protocols.rst:8-56`). This repo is on 6.1.0 (`dependencies.lock`), so
cJSON means a new managed dependency + SBOM entry. **[V]**

**Cost.** `sizeof(cJSON) = 40 B`, align 8, on the xtensa ABI (cross-compiled; `cJSON.h:103-118`).
Every string node owns a malloc'd key and value → **~300-400 B/track** for a 5-member object, plus
~100 B/track for `cJSON_PrintUnformatted`, which allocates the whole text a second time: peak ≈
tree + text (~1.5-2.5 MB for 5,000 tracks). **[E, struct V]**

**Alternatives.** (a) The bounded writer pattern of `writer_t` (`mini_files.c:59-101`): 1 KB buffer,
`w_puts`/`w_printf`, `http_send_all` flush; for `Content-Length`, count in a first pass then fill an
exact-size block; peak = output only. (b) **jsmn** for a POST body: single header, ~200 lines, C89,
**no allocation**, 16 B tokens into the request buffer, no unescaping (`github.com/zserge/jsmn`). **[V]**

| Form | Fixed | Per track (avg / worst) | 5,000 tracks |
|---|---|---|---|
| Packed binary 16 B + NUL path | 16 | 56 / 272 | ~280 KB / 1.4 MB |
| JSON `{"n":…,"s":…,"d":…}` | ~40 | 80 / 296 | ~400 KB / 1.5 MB |
| cJSON tree (transient) | — | 300-400 / 600 | ~1.5-2 MB peak |

The path dominates, so JSON is only ~1.3-1.5× the binary; the real difference is cJSON's tree.
## 6. What the HTTP server supports today

Hand-written HTTP/1.0, not `esp_http_server`.

| Fact | Evidence |
|---|---|
| **GET only**; others → 405 "only GET is served" | `http_server.c:215-220` |
| Header buffer **1024 B**; read stops at first `\r\n\r\n`; bytes after it are **discarded** (no request body) | `http_server.h:35`, `http_server.c:186-204`, `http_parse.c:37-83` |
| **No keep-alive**: HTTP/1.0, always `Connection: close`, one request then `close(fd)` | `http_server.c:98,102,229` |
| **No chunked / Range / multipart**: body is Content-Length or close-delimited | `http_server.c:101-102`, `http_server.h:76-78` |
| Response header block **512 B**; overflow → `ESP_ERR_INVALID_SIZE` | `http_server.c:96,105` |
| `SO_SNDTIMEO` **400 ms** default, per-route override | `http_server.h:27`, `http_server.c:167,175-179` |
| Longest-prefix routing; path 512, query 256, raw/percent-encoded | `http_server.h:33-34`, `http_parse.c:69-78,85-95` |
| `find_header()` static; only `Icy-MetaData` parsed; `reason_phrase()` knows 200/400/404/405/414/500/503 | `http_parse.c:19-35,80-81`, `http_server.c:46-57` |
| 6 slots max, default 4; 10 KB external-preferred stack each | `http_server.h:36-37`, `http_server.c:30,250-265` |

**(a) Large prebuilt JSON + Content-Length — already possible.** `http_send_headers(fd, 200,
"application/json", len, "ETag: …\r\n")` then `http_send_all(fd, json, len)`. Changes: raise the
send timeout (`http_send_timeout_ms(fd, 5000)`, as `mini_files.c:350` does — 400 ms is tuned for a
32 KB/s stream); keep extra headers under 512 B; pass the real length (today's index passes
`(size_t)-1`, `mini_files.c:239`); a multi-MB PSRAM buffer is fine to pass directly, but a slow
client holds one of four workers.

**(b) POST + JSON body (MCP).** Replace the GET check (`http_server.c:215`) with per-route methods.
Parse `Content-Length` by exposing `find_header()` (`http_parse.c:19`) or moving header lookup into
`http_parse_request`; add `content_length`/`body` to `http_request_t` (`http_server.h:39-45`). The
header read stops at the terminator (`http_server.c:193`) and the rest of the body is in `buf` but
currently dropped — continue `recv()` with `HTTP_HEADER_TIMEOUT_MS` and a hard cap (4-8 KB),
answering **413**. Reject `Transfer-Encoding: chunked` with 411/501, and add 304/411/413/415/501
to `reason_phrase` (`http_server.c:46-57`). Keep the body off the 10 KB connection stack.

**(c) ETag / If-None-Match.** Emit via the existing `extra_headers` (`http_server.c:93-107`); add
`if_none_match` to `http_request_t` and parse with `find_header`; return 304 by adding 304 to
`reason_phrase` and calling `http_send_headers(fd, 304, NULL, (size_t)-1, "ETag: …\r\n")` with no
body. `Cache-Control: no-cache` is hard-coded (`http_server.c:102`): it still permits storage but
forces revalidation, which is what ETag wants; parameterize it if freshness is desired.
## 7. Contention, startup, scan placement

- Measured: card alone ~250 KB/s (16 KB/62 ms); a download alongside the broadcast fell to
  55 KB/s and still missed stream deadlines, so downloads **park the broadcast**
  (`mini_files.c:318-326`, `mini_icecast.c:470-494`; README.md:376-383). The broadcast needs
  ~120 KB/s of the single SPI bus. **[V, repo measurement]**
- Boot order (`app_services.c:75-130`): Wi-Fi start is **async** → card mount → file surface →
  broadcast → HTTP listen; `app_board_wait_online()` runs later (`appliance.c:185`;
  `board_esp32.c:116-119`). A scan placed after mount **overlaps Wi-Fi association**. **[V]**
- The broadcast already does **two full readdir passes** over the top-level dir before the first
  track, to count it (`mini_icecast.c:691,704-713,736`). Replacing those with the boot list makes a
  flat-library scan close to free in boot-to-first-audio terms. **[V + E]**
- Estimated scan: 5,000 tracks ≈ **1.5-8 s** (32-char to 128-char names); 20,000 ≈ **6-30 s**; more
  if the decoder is running. **No on-device scan benchmark exists — these are [E]/[U], not measured.**
- **Placement:** scan after `app_board_card_mount()` (step 3), before `mini_icecast_start()` and
  `http_server_start()`. No listener, no decoder on the card, association overlaps, and the
  broadcast gets the finished list. If boot latency matters more, scan in a task and let `/` answer
  **503 + Retry-After** until a `ready` flag is set — never serve a half-built index.
- **No ESP-IDF guidance found** on ordering mount/scan vs Wi-Fi; the documented constraint is
  internal RAM (`esp_wifi_init()` needs ~25 KB contiguous DMA), which is why the network comes
  before the encoder (ADR-0004, README.md:767-770). The scan uses PSRAM, not internal DMA. **[V absence]**
- **Staleness:** the card is mounted once and never rescanned (`appliance.c:44-62` retries only a
  *failed* mount). Files removed at runtime leave dead paths in the index. Add a generation counter
  and an on-demand rescan or a directory-mtime check. **[V]**
## 8. Recommended layout

One PSRAM arena, allocated once after mount, published read-only:

```
+0   header 32 B: "XTLB", u16 ver, u16 rec_size=16, u32 count, u32 str_off, u32 str_bytes,
                  u32 json_off, u32 json_len, u64 etag
+32  record[count], 16 B, naturally aligned, LE:
       u32 name_off (relative path into string arena); u32 size; u32 duration_ms;
       u16 flags (format, lossless, …); u16 reserved
+…   4-byte aligned arena of NUL-terminated relative paths
+…   optional JSON blob with exact Content-Length
```

- **Per track:** 16 B record + 40-60 B average path → **~64-80 B**, worst 272 B. A **4 MB** arena
  covers ~20k tracks worst-case; or size from `count` after a first pass.
- **Paths:** `FILENAME_MAX + 1` scratch, but enforce **absolute path ≤ `FILENAME_MAX`** (§2).
- **Sorting:** `qsort` the 16 B records by path, `strcasecmp` then `strcmp` as a deterministic
  tie-break (FAT is case-insensitive); ~10-30 ms for 10k tracks. Sort once after the scan, publish.
- **JSON:** bounded writer, count pass → exact allocate → fill → publish; ETag = FNV-1a of the blob.
- **On card:** no index file. Keep FAT32; allow nesting up to ~1024 B. An
  on-card cache needs boot writes, wear, atomic rename and invalidation for a few MB rebuilt in
  seconds — not worth it.
## 9. Top risks

1. **Silent ~1024 B path truncation** in the VFS on top of the 128-char LFN cap: a pathological
   tree can list but fail to open, or resolve a truncated sibling. Enforce `FILENAME_MAX` at scan
   time and log it at boot; it is not a practical limit for normal libraries.
2. **Encoding drift.** Base `sdkconfig` has `CONFIG_FATFS_API_ENCODING_ANSI_OEM=y` (CP437), while
   `sdkconfig.defaults` and `sdkconfig.sunton` select UTF-8. OEM names are not valid UTF-8 (invalid
   JSON); UTF-8 names can exceed `FF_LFN_BUF` on multibyte paths. Pin one and verify the built
   `sdkconfig.h`, then transcode or reject at the scan boundary.
3. **Scan vs. broadcast, and staleness.** A 5-30 s scan running with the decoder will starve it
   (downloads already park the broadcast); running before it delays first audio. Either way the
   index is a snapshot with no invalidation and no ETag change when the card changes.
## Sources

- IDF v6.1 (`~/esp/esp-idf-6.1`, tag `v6.1`), base
  <https://github.com/espressif/esp-idf/blob/v6.1>: `components/fatfs/{src/ff.c,src/ffconf.h,Kconfig,vfs/vfs_fat.c}`,
  `components/xtensa/esp32s3/include/xtensa/config/core-isa.h`,
  `docs/en/api-guides/external-ram.rst`, `docs/en/api-reference/system/{mem_alloc,heap_debug}.rst`,
  `docs/en/migration-guides/release-6.x/6.0/protocols.rst`,
  `docs/en/migration-guides/release-5.x/5.0/gcc.rst`.
- IDF v5.4.4 (`~/esp/esp-idf`): `components/json/cJSON/cJSON.h`, `components/fatfs/vfs/vfs_fat.c`.
- FatFs R0.16: [readdir](https://elm-chan.org/fsw/ff/doc/readdir.html),
  [stat](https://elm-chan.org/fsw/ff/doc/stat.html),
  [config](https://elm-chan.org/fsw/ff/doc/config.html),
  [paths](https://elm-chan.org/fsw/ff/doc/filename.html); jsmn: <https://github.com/zserge/jsmn>.
- Repo: `components/portable/{http_server,mini_files,app_services,mini_icecast}/*`,
  `components/app/board_esp32/board_esp32.c`, `README.md`,
  `docs/adr/0004-ui-memory-is-external.md`, `sdkconfig*`, `dependencies.lock`.
