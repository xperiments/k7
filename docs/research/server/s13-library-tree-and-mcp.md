# A boot-built library tree, JSON and binary serving, recursive playback, and a mini MCP

**Status:** design and research; no source changed. Written 2026-10-19. The repository is at
`9b338e2` with unrelated art/UI changes in the working tree.

**Question:** the current "playlist" has no object in the code. What should replace it so that
the whole card tree is loaded once at boot, served as JSON (and optionally as a binary struct
dump) without touching the SD per request, any track in any subdirectory can be played, and a
small MCP server can expose the library and the transport?

**Verdict, in eight lines:**

1. **There is no playlist today.** The broadcast's order is whatever `readdir()` returns on
   the flat `music_dir`, re-derived on every pass; the HTTP index re-walks the same directory
   and `stat()`s every track on every request. Two independent walks, neither recursive
   ([`mini_icecast.c:684-767`](../../../components/portable/mini_icecast/mini_icecast.c#L684-L767),
   [`mini_files.c:198-263`](../../../components/portable/mini_files/mini_files.c#L198-L263)).
2. **Build one immutable library tree at boot, in one PSRAM arena, and make the playlist an
   index into it.** That removes the per-request SD work, makes recursion a property of the
   scan rather than of every consumer, and gives the broadcast a stable cursor.
3. **The arena is the binary format.** Pointer-free, fixed-width, little-endian, versioned,
   CRC'd; `send()` it verbatim. JSON is a second, prebuilt view of the same tree. Budget:
   ~85 B/track binary + ~200 B/track JSON, so 10 000 tracks ≈ 0.8 MB + 2.0 MB.
4. **Resolve HTTP paths by tree lookup, not by joining strings.** A request can only name a
   node that the scan found, which is a stronger guarantee than the current containment check
   and removes `%2e%2e`, `%2f`, NUL and symlink questions from the download route.
5. **Recursive playback falls out**: the broadcast iterates the flat track index in DFS order;
   `prev`/`skip` become index arithmetic; a new `play(path)` selects any node.
6. **The mini MCP is feasible on the device** as a stateless, JSON-only, tools-only Streamable
   HTTP endpoint at `/mcp`, speaking the legacy 2025-11-25 lifecycle and echoing 2025-06-18 /
   2025-03-26 when a client asks for them. The current revision, 2026-07-28, removed
   `initialize`, `ping` and sessions; the working reference `solnera/esp32-mcpserver` defaults to
   2025-11-25, MCP Inspector defaults to the legacy era, and Claude Code's v1 runtime speaks it,
   so legacy first is the compatibility choice, not a shortcut.
7. **What makes this non-trivial is not the tree, it is the boot scan.** ESP-IDF's FATFS VFS
   caches the `FILINFO` behind a `readdir()`, so the following `stat()` is free; the remaining
   cost is one sequential directory read per entry on a 20 MHz SPI card. Sync scan is the v1
   default; measure the real card and switch to the async `indexing` state if a typical card
   exceeds ~2 s, before inventing a new scan layer.
8. **Do not call the first version "the" MCP server.** Pin the protocol revision it speaks, and
   keep the JSON-RPC envelope separate from tool dispatch so the 2026-07-28 stateless era can be
   added without rewriting the tools.

Labels: **VERIFIED** = read in the repository or primary source cited on the line;
**ESTIMATE** = derivation, assumption stated; **MEASURED** = produced here, command given.
**UNVERIFIED** = could not confirm.

---

## 1. What the code does today

### 1.1 The playlist is an emergent property of `readdir()`

`broadcast_task()` opens `S.music_dir`, counts the names `is_audio()` accepts, then walks the
same directory with `readdir()`, builds `music_dir/name`, and streams it
([`mini_icecast.c:684-767`](../../../components/portable/mini_icecast/mini_icecast.c#L684-L767)).
`is_audio()` accepts `.mp3`, `.flac`, `.wav` case-insensitively and skips names beginning with
`.` ([`mini_icecast.c:665-669`](../../../components/portable/mini_icecast/mini_icecast.c#L665-L669)).
There is no sort, no shuffle, no subdirectory recursion, and no stored list: at the end of a
pass the directory is reopened and the order starts again. `prev` is a history of `telldir()`
positions, valid only while that `DIR` is open
([`mini_icecast.c:671-682`](../../../components/portable/mini_icecast/mini_icecast.c#L671-L682),
[`mini_icecast.c:715-748`](../../../components/portable/mini_icecast/mini_icecast.c#L715-L748)). The
display's `library_count` is recomputed by a second `readdir()` pass
([`mini_icecast.c:702-714`](../../../components/portable/mini_icecast/mini_icecast.c#L702-L714)).

Consequences:

- The playlist order is a filesystem implementation detail, documented in the README as
  "directory order" ([`README.md:100-107`](../../../README.md#L100-L107)). On FAT that is on-disk
  directory order, not alphabetical. Copying the same files in a different order changes what
  plays next.
- The list is rebuilt every pass, so nothing else can refer to a track by a stable identity.
- The card is open for reading for the whole pass, and `prev` depends on the directory handle
  staying open.

### 1.2 The index is a second, independent walk

`index_route()` re-walks the directory on every `GET /`, calls `music_dir_list()`, and that
function `opendir()`s, `readdir()`s and `stat()`s every accepted track before rendering one
`<li>` per track
([`mini_files.c:198-263`](../../../components/portable/mini_files/mini_files.c#L198-L263),
[`music_dir.c:67-93`](../../../components/portable/mini_files/music_dir.c#L67-L93)). The response has
no `Content-Length` and no ETag (`http_send_headers(..., (size_t)-1, ...)`,
[`mini_files.c:239`](../../../components/portable/mini_files/mini_files.c#L239)).

So the index costs O(files) directory reads and `stat()`s per request, and the page and the
broadcast can disagree if the card changes between their walks. There is no JSON surface at all.

### 1.3 The path rules assume exactly one segment

`music_resolve()` rejects any name containing `/` or `\`, any name containing `..`, and any
leading dot, then joins `dir/name` and re-checks the prefix
([`music_dir.c:15-65`](../../../components/portable/mini_files/music_dir.c#L15-L65)). The download
route decodes the rest of `/music/` into a `MUSIC_NAME_MAX + 1` (129-byte) buffer and passes it
to that function ([`mini_files.c:267-308`](../../../components/portable/mini_files/mini_files.c#L267-L308)).
Nested paths are therefore refused by construction, and `MUSIC_PATH_MAX` is 256
([`music_dir.h:21-23`](../../../components/portable/mini_files/include/music_dir.h#L21-L23)).

### 1.4 The card can arrive after boot

`app_services_start()` mounts the card before starting the file surface and the broadcast; a
missing card is a report, not a failure
([`app_services.c:78-98`](../../../components/portable/app_services/app_services.c#L78-L98)). The
appliance then retries every 5 s and, on success, calls only `mini_files_set_card()`
([`appliance.c:42-60`](../../../components/app/appliance/appliance.c#L42-L60)). The broadcast happens
to recover because it reopens the directory each pass. A boot-built library needs an explicit
reload at that point, or the device serves an empty library forever.

### 1.5 The HTTP server cannot receive an MCP request

`serve_connection()` reads at most 1024 bytes of request header and rejects anything that is not
`GET` before routing
([`http_server.c:186-220`](../../../components/portable/http_server/http_server.c#L186-L220),
[`http_server.h:33-45`](../../../components/portable/http_server/include/http_server.h#L33-L45)).
There is no request body, no `Content-Type`/`Origin` access, no `Content-Length` field
and no POST route. Responses are HTTP/1.0 with `Connection: close`
([`http_server.c:94-108`](../../../components/portable/http_server/http_server.c#L94-L108)). The
connection worker stack is 10 KB
([`http_server.c:30`](../../../components/portable/http_server/http_server.c#L30)).

MCP's Streamable HTTP transport is POST-based, so this is the first firmware change the MCP
needs — independent of the library.

### 1.6 The domain language says "no recursion"

`docs/specs/CONTEXT.md` used to define the **Music directory** as "the single directory ...
there is no database and no recursion into subdirectories"
([`CONTEXT.md:11-14`](../../../docs/specs/CONTEXT.md#L11-L14)), and the README tells users to put
tracks in a single flat directory ([`README.md:100-107`](../../../README.md#L100-L107)). This feature
changes that definition, so the document changes with the code. The vocabulary is in §3.0.

---

## 2. Requirements

**R1 — Build once.** One scan of the music directory at boot produces the complete tree: every
directory and every playable track, recursively.

**R2 — Serve without the card.** `GET /api/library` returns JSON and `GET /api/library.bin`
returns the binary blob with no filesystem access on the request path. Both have
`Content-Length`, a generation identifier and a media type.

**R3 — Play any track in any subdirectory.** The broadcast's playlist is the tree's tracks in a
defined order, and an explicit request can jump to any track by path.

**R4 — Keep downloads byte-identical.** `/music/<path>` returns the same original bytes, with
the same one-download-at-a-time and card-parking discipline, for nested paths.

**R5 — Keep the device diagnosable without a card.** No card, no music directory, an unreadable
subdirectory and an empty library remain distinct, visible states.

**R6 — Bounded memory and bounded failure.** The library has a fixed budget in PSRAM. Exceeding
it degrades in a stated way (truncate and flag, not abort), and never takes internal RAM from
Wi-Fi or the encoder.

**R7 — One authority for "what is a track".** The scan, the JSON, the binary, the index and
the download use the same accepted-name rule and the same source-format list.

**R8 — A mini MCP.** A standard MCP endpoint exposes read-only library queries and player
status, plus opt-in control (play/skip/prev), over the device's own HTTP port.

**Non-goals for this change:** no database, no ID3/FLAC tag parsing (file names only), no
on-card index cache, no Range requests, no TLS/auth, no incremental watch of the card. Those
are separate decisions.

---

## 3. Design

### 3.0 Vocabulary

`docs/specs/CONTEXT.md` used to say the music directory had "no recursion"
([`CONTEXT.md:11-14`](../../../docs/specs/CONTEXT.md#L11-L14)); it now defines it recursively.
**Music directory stays the canonical term** — it is still the directory the scan starts at —
and two new terms join it: **Library tree** and **Playlist**. Renaming it to "Library root"
would churn the README, the spec and the `music_dir` configuration for no gain.

| Term | Definition | Avoid |
|---|---|---|
| **Music directory** | The root directory on the card: `/sdcard/music`. Its contents, recursively, define the library; it is not itself the library. | library root, media path |
| **Library tree** | The directories and tracks under the music directory, as one ordered tree. The playlist and the JSON are views of it. | file list, index |
| **Track** | One regular file in the library tree whose name passes the source-format rule. | song, item, entry |
| **Playlist** | The ordered sequence of tracks the broadcast plays: depth-first, name-sorted. It is derived from the tree, not stored separately. | queue, play list |

Implementation and spec terms, deliberately **not** in `CONTEXT.md` (which is a glossary, not a
spec): **Library generation** (one immutable snapshot of the tree, its JSON, its blob and its
counts, published by a rescan), **Track id** (a node's index inside one generation) and **Scan**
(building a generation: walk, filter, sort, assemble, serialise). They are precise where they
are used, but they describe mechanism, not domain language.

The **now playing** title and the **station format** are unchanged; the cassette still reads
only the broadcast's status snapshot.

### 3.1 One module, one generation

Add `components/portable/library/` — a deep module that owns the scan, the arena, path lookup,
the JSON view and the binary blob. It knows nothing about HTTP, sockets, audio decoding or the
display. It does know the source-format authority, so that authority must stop living inside
`mini_icecast`.

Today `mi_fmt_t` and `mi_dec_format_of()` are in
`components/portable/mini_icecast/include/mi_decode.h`
([`mi_decode.h:14-36`](../../../components/portable/mini_icecast/include/mi_decode.h#L14-L36)). If
`library` includes that header and `mini_icecast` includes `library`, the two components become
mutually dependent. Extract the tiny format authority into
`components/portable/audio_format/` (`mi_fmt_t`, `mi_dec_format_of()`), have both depend on it,
and leave the decoders in `mini_icecast`. This is a prerequisite, not a nice-to-have.

Dependency direction after the change:

```
audio_format        (three extensions, one enum)
library       -> audio_format
mini_files    -> library, audio_format, http_server, mini_icecast
mini_icecast  -> library, audio_format, http_server
mcp           -> library, mini_icecast, http_server
app_services  -> all of the above
http_server   -> none
```

`http_server` still knows nothing about audio or files. `mini_files` already reads the
broadcast's now-playing snapshot through `mini_icecast_now_playing()`, and that one-way
dependency stays; what must not happen is the reverse. `mini_icecast` must not include
`mini_files.h`, render markup or route requests. The layering script gains two assertions: no
`library/*.c` includes `http_server.h`, `mini_icecast.h` or `mini_files.h`; and `mini_icecast`
does not include `mini_files.h`.

A **library generation** is an immutable `lib_t`: the arena, the JSON view, counts, CRC and a
generation number. Readers `lib_acquire()` a generation and `lib_release()` it; a rescan builds
a new generation off to the side and publishes it with a pointer swap. The old generation is
freed when its last reader releases it. This keeps a 2 MB JSON response from blocking a rescan,
and keeps the broadcast's current track valid while the index changes underneath it. The
implementation can be a mutex plus a reference count and a retired list; there is no need for
RCU, because rescans are rare and not in the audio path.

Proposed interface (names are placeholders; shapes are the decision):

```c
typedef struct lib lib_t;            /* one immutable generation */
typedef uint32_t lib_node_t;         /* arena index; LIB_NODE_NONE = UINT32_MAX */

typedef enum { LIB_DIR = 0, LIB_TRACK = 1 } lib_kind_t;

typedef struct {
    const char *root;        /* "/sdcard/music" */
    uint32_t    max_bytes;   /* arena + JSON budget, 0 = LIB_DEFAULT_MAX_BYTES */
    uint32_t    max_tracks;  /* 0 = LIB_DEFAULT_MAX_TRACKS */
    bool        build_json;  /* false = binary only, JSON streamed from arena */
} lib_cfg_t;

typedef enum {
    LIB_OK = 0, LIB_NO_CARD, LIB_NO_DIR, LIB_EMPTY, LIB_NO_MEM,
    LIB_TOO_LARGE, LIB_IO,
} lib_result_t;

/* Scans and builds, does not publish. A known "nothing to serve" outcome (no card, no
 * directory, empty tree) still returns a generation; lib_state() says which, so the index
 * and /api/library can stay diagnosable. Returns LIB_NO_MEM only when even that fails. */
lib_result_t lib_load(const lib_cfg_t *cfg, lib_t **out);
lib_t       *lib_acquire(void);                          /* current generation, refcounted */
void         lib_release(lib_t *lib);
lib_result_t lib_publish(lib_t *next);                   /* swap; old retired */

uint32_t     lib_node_count(const lib_t *lib);
uint32_t     lib_track_count(const lib_t *lib);
uint32_t     lib_generation(const lib_t *lib);
uint64_t     lib_total_bytes(const lib_t *lib);
bool         lib_truncated(const lib_t *lib);

typedef enum {
    LIB_STATE_OK = 0, LIB_STATE_NO_CARD, LIB_STATE_NO_DIR,
    LIB_STATE_EMPTY, LIB_STATE_TRUNCATED, LIB_STATE_ERROR,
} lib_state_t;
lib_state_t  lib_state(const lib_t *lib);   /* what the index page should say */
const char  *lib_error(const lib_t *lib);   /* sentence, "" when OK */

lib_node_t   lib_root(const lib_t *lib);
lib_node_t   lib_parent(const lib_t *lib, lib_node_t n);
uint32_t     lib_child_count(const lib_t *lib, lib_node_t n);
lib_node_t   lib_child(const lib_t *lib, lib_node_t n, uint32_t i);
const char  *lib_name(const lib_t *lib, lib_node_t n, uint16_t *len);
lib_kind_t   lib_kind(const lib_t *lib, lib_node_t n);
uint64_t     lib_size(const lib_t *lib, lib_node_t n);
mi_fmt_t     lib_format(const lib_t *lib, lib_node_t n);

/* Builds "artist/album/track.mp3" into buf; returns length, (size_t)-1 if it does not
 * fit, and 0 for the root (whose relative path is empty). */
size_t       lib_relpath(const lib_t *lib, lib_node_t n, char *buf, size_t buf_size);
/* Splits on '/', walks children; exact match first, ASCII case-insensitive fallback.
 * Returns LIB_NODE_NONE for an unknown path. No filesystem call. */
lib_node_t   lib_find(const lib_t *lib, const char *relpath);

lib_node_t   lib_track_at(const lib_t *lib, uint32_t playlist_index);
uint32_t     lib_track_index(const lib_t *lib, lib_node_t n); /* LIB_NODE_NONE if not a track */

/* The prebuilt views. Both are valid while the generation is acquired. */
const char  *lib_json(const lib_t *lib, size_t *len);
const void  *lib_blob(const lib_t *lib, size_t *len);
uint32_t     lib_crc32(const lib_t *lib);
```

`lib_relpath()` builds from parent links into a caller buffer. Full paths are **not** stored:
that would duplicate every parent name once per track (~1 MB for 10 000 tracks, from the model
in §6.1) to save a walk of at most a few levels.

### 3.2 The scan: one directory handle at a time

A recursive function that holds one `DIR *` per level is not bounded by `max_files`: in
ESP-IDF 5.4.4 and 6.1, `opendir()` allocates a `vfs_fat_dir_t` (including a 256-byte `FILINFO`)
from the heap and does **not** take a slot in the mount's `files[]` array; `max_files` bounds
open files, not open directory streams
([`vfs_fat.c:996-1018`](https://github.com/espressif/esp-idf/blob/v6.1/components/fatfs/vfs/vfs_fat.c#L996-L1018),
[`vfs_fat.c:67`](https://github.com/espressif/esp-idf/blob/v6.1/components/fatfs/vfs/vfs_fat.c#L67)).
It is still an unbounded per-depth heap cost, and a recursive walk would hold N directory
streams and N stat caches while it stats entries. The scan therefore uses an explicit queue of
node ids and opens exactly one `DIR *` at a time: flat memory, no depth cap, and the same code
on the host.

There is also good news in the same VFS: `readdir()` caches the `FILINFO` it just read, and a
`stat()` on that same path consumes the cache instead of issuing another card request
([`vfs_fat.c:1044-1063`](https://github.com/espressif/esp-idf/blob/v6.1/components/fatfs/vfs/vfs_fat.c#L1044-L1063),
[`vfs_fat.c:844`](https://github.com/espressif/esp-idf/blob/v6.1/components/fatfs/vfs/vfs_fat.c#L844)).
So the portable `readdir()` + `stat()` pair is one card access per entry, not two. That removes
most of the reason to bypass the VFS for `FILINFO`; §6.2 keeps the measurement, not the fear.

The scan therefore uses an explicit queue of node ids and opens exactly one `DIR *` at a time:

```
queue = [root]
while queue not empty and budget not exhausted:
    n = pop(queue)
    path = lib_relpath(n)                     # build from parent links
    d = opendir(root + "/" + path)
    if !d: record error; continue
    while (e = readdir(d)):
        skip "." and ".."
        skip names beginning with "."          # macOS "._foo.mp3", same rule as today
        skip names containing '/', '\\' or NUL, or longer than 128 bytes
        skip e->d_type == DT_LNK              # host only; FAT entries have no link type
        stat(root/path/name)                  # size and type; served from the readdir cache
        if directory: add dir node, push(queue)
        else if regular and audio extension: add track node with size
    closedir(d)
```

This keeps at most one directory object open, independent of depth. It also means a single
unreadable subdirectory is a logged skip, not a failed scan. The path buffer is
`FILENAME_MAX + 1` and the scan refuses a longer absolute path rather than letting the VFS
truncate it (§3.3).

The cost is one `opendir()`/`closedir()` per directory plus one `readdir()` per entry; the
`stat()` that follows each entry is served from the VFS's one-entry cache, not from the card.
`readdir()` itself still does not expose the size, which is why the `stat()` is in the loop at
all. **Boot time is still the one measurement that can change this design** (§6.2), but the
expected shape is now "one sequential directory read per entry", not "two card transactions per
entry". If that is still too slow, the FatFs `FILINFO` port remains the fallback; it should not
be built before the measurement.

Names are UTF-8 bytes from the VFS, and the library must pin
`CONFIG_FATFS_API_ENCODING_UTF_8`. The base `sdkconfig` has CP437 (`ANSI_OEM`) while
`sdkconfig.defaults` and `sdkconfig.sunton` select UTF-8; if the wrong one wins, an OEM name is
not valid UTF-8 and the JSON document is invalid. The scan validates each name as UTF-8 and
skips an invalid one with a one-line log, so a bad byte can never poison the prebuilt JSON.

The scan records entries in encounter order, then sorts each directory's children by name (ASCII
case-insensitive, then byte order) before assigning final node ids in DFS order. Sorting is a
deliberate behaviour change: the playlist becomes deterministic and independent of FAT
directory order, and the JSON and the HTML index can present the same order. It also makes
`lib_find()` a binary search over each directory's child edge range.

### 3.3 Arena and binary layout

One allocation for the arena, one for the JSON view; both in PSRAM via
`heap_caps_malloc(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`. No pointers, no padding surprises,
no host-endian ambiguity. The blob is exactly the arena from offset 0 to `strings_off +
string_bytes`, so serving it is a `send()` loop with no copy.

Header, 64 bytes:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 4 | `magic` | `"XTLB"` (0x424C5458 LE) |
| 4 | 2 | `version` | 1 |
| 6 | 2 | `header_size` | 64 |
| 8 | 4 | `flags` | bit 0: little-endian; bit 1: truncated |
| 12 | 4 | `node_count` | nodes, root = 0 |
| 16 | 4 | `edge_count` | child edges |
| 20 | 4 | `track_count` | playlist length |
| 24 | 4 | `string_bytes` | bytes in the name table |
| 28 | 4 | `crc32` | IEEE 802.3 over the payload (nodes..strings) |
| 32 | 4 | `generation` | increments on each publish |
| 36 | 4 | `root_name_off` | offset of the root's name (usually 0) |
| 40 | 8 | `library_bytes` | total size of tracks on the card |
| 48 | 8 | reserved | zero |
| 56 | 8 | reserved | zero |

Payload, in order:

| Section | Size | Contents |
|---|---:|---|
| nodes | `node_count * 32` | one 32-byte record per node, DFS order |
| edges | `edge_count * 4` | child node ids, each directory's children contiguous and sorted |
| tracks | `track_count * 4` | track node ids, DFS/playlist order |
| strings | `string_bytes` | raw UTF-8 name bytes, not NUL-terminated |

Node, 32 bytes (all fields naturally aligned; `_Static_assert` each offset):

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 8 | `size` | bytes for a track, 0 for a directory |
| 8 | 4 | `name_off` | into strings |
| 12 | 4 | `parent` | node id, `0xFFFFFFFF` for root |
| 16 | 4 | `child_edge` | first index into edges, `0xFFFFFFFF` if none |
| 20 | 2 | `child_count` | number of children |
| 22 | 2 | `name_len` | bytes, 1..128 |
| 24 | 1 | `kind` | 0 dir, 1 track |
| 25 | 1 | `format` | `mi_fmt_t`; 0xFF for dirs |
| 26 | 2 | `flags` | reserved, 0 |
| 28 | 4 | `track_index` | playlist ordinal for tracks, `0xFFFFFFFF` for dirs |

The order is chosen so `uint64_t size` is 8-byte aligned at offset 0 and the whole record is
32 bytes on both the 32-bit target and a 64-bit host. The wire format is the in-memory format:
no packing pragma, no serialisation step, and a consumer can parse it with the same table. The
`crc32` is computed once at build; a client may ignore it, but the device's own test parser
checks it.

Natural alignment is also a performance decision, not just a wire convenience. On the LX7 an
explicit unaligned 32-bit load is legal (`l32i`, no exception) but **not atomic**, and a
packed-struct `uint32_t` member compiles to four `l8ui` byte loads instead of one word load.
The arena is therefore naturally aligned and never packed; a client that wants a packed parser
can copy a record out with `memcpy`, but the canonical layout is the aligned one
([`core-isa.h:202-205`](https://github.com/espressif/esp-idf/blob/v6.1/components/xtensa/esp32s3/include/xtensa/config/core-isa.h#L202-L205)).

A client reconstructs a track's path by walking `parent` links and joining names with `/`; the
blob deliberately stores no full paths. The JSON view carries them because a human/LLM client
should not have to walk the tree, but the binary is the compact form and the device builds a
path only when it opens a file.

`lib_find()` splits the decoded path on `/` and, at each level, binary-searches that node's
contiguous edge range by name. No path string ever reaches `open()` unless every component
matched a node from the scan. That is the security model: **the tree is the allowlist**.

Two details worth stating because they are where implementations go wrong:

- **Case.** FATFS is configured case-insensitively and the board has been observed to serve
  `/music/TONE-44100.FLAC` for `tone-44100.flac` ([`issues/08`](../../../.scratch/sd-stream-server/issues/08-browsable-library-and-downloads.md),
  board run note). The tree stores the card's exact bytes. `lib_find()` matches exact first,
  then ASCII-case-insensitively; if the fallback matches more than one node (possible on the
  host's case-sensitive filesystem, impossible on FAT), it refuses as ambiguous rather than
  guessing.
- **Depth and length.** The VFS prepends `"0:"` to the path with `snprintf` into a
  `FILENAME_MAX+3` buffer
  ([`vfs_fat.c:61-62`](https://github.com/espressif/esp-idf/blob/v6.1/components/fatfs/vfs/vfs_fat.c#L61-L62),
  [`vfs_fat.c:381-390`](https://github.com/espressif/esp-idf/blob/v6.1/components/fatfs/vfs/vfs_fat.c#L381-L390)),
  so a path longer than the buffer is **silently truncated**. The size of that buffer is not
  255: preprocessing the actual 6.1 build (`xtensa-esp32s3-elf-gcc -E` on `vfs_fat.c` with the
  real compile flags) shows `FILENAME_MAX = 1024`, hence `tmp_path_buf[1027]` and an absolute
  path ceiling of **~1024 bytes**. The 255 fallback at `vfs_fat.c:29` applies only when the
  libc does not define `FILENAME_MAX`; picolibc and newlib both define it (`stdio.h`). The same
  toolchain family is used by 5.4.4, so the ceiling is ~1024 there too; the doc's earlier
  "255" was a misread of `sys/config.h` and is corrected here.
  The scan therefore enforces `LIB_PATH_MAX = FILENAME_MAX + 1` and skips/logs anything longer;
  `lib_relpath()` uses the same buffer. With `/sdcard/music/` taking 14 bytes, the tree has
  ~1010 bytes for relative paths, which is deep enough for any normal collection. `HTTP_PATH_MAX`
  is 512 **raw** bytes, so a URL is the practical limit for addressable paths (~511 decoded);
  the MCP `player_play` tool can name a longer path by id or via a POST body, because it does
  not go through a URL. If a future build ever leaves `FILENAME_MAX` undefined, the 255
  fallback returns and the scan's enforced cap must follow it; a `_Static_assert`/runtime log of
  `FILENAME_MAX` at boot makes that visible.

**So the fix for the path ceiling is not a workaround, it is to stop hard-coding a number.** Use
`FILENAME_MAX` as the budget (`LIB_PATH_MAX = FILENAME_MAX + 1`), check every joined path before
the VFS sees it, and log both the value and any skipped path at boot. On this build that is 1024
bytes, not 255; if a future libc or an IDF build with the 255 fallback lowers it, the same code
adapts and the log says so. Do **not** use `chdir` as a workaround: `FF_FS_RPATH=0` in this
configuration, so relative paths start at the root and there is no VFS `chdir`. Do **not** patch
`FILENAME_MAX` in IDF either; it is a libc constant used by more than the VFS.
If a product ever needs paths beyond `FILENAME_MAX`, the clean route is a filesystem port over
FatFs (`f_opendir`/`f_readdir`/`f_open`), because FatFs itself does not copy the whole path into a
fixed buffer. That port would also need a decoder I/O variant and a download read path, so it is a
separate, larger change — not something to do for a library that fits in 1 KB.

### 3.4 The JSON view

One object, one tree, one order. The `children` array interleaves directories and tracks in
playlist order, so a client that flattens depth-first gets exactly the playlist.

```json
{
  "root": "/sdcard/music",
  "generation": 7,
  "count": 2,
  "bytes": 8123456,
  "state": "ok",
  "truncated": false,
  "tree": {
    "id": 0, "type": "dir", "name": "music", "path": "",
    "children": [
      { "id": 1, "type": "dir", "name": "Artist", "path": "Artist",
        "children": [
          { "id": 2, "type": "track", "name": "01 - Song.mp3",
            "path": "Artist/01 - Song.mp3", "size": 5123456, "format": "mp3" }
        ] },
      { "id": 3, "type": "track", "name": "loose.flac",
        "path": "loose.flac", "size": 3000000, "format": "flac" }
    ]
  }
}
```

- `path` is the decoded, relative, UTF-8 path. It is not URL-encoded; a client that needs a URL
  encodes each segment. The server's own HTML index does that already.
- `id` is the arena node id, valid only for this `generation`. `player_play` prefers `path`
  and accepts `{id, generation}` as a fast path, validating the generation.
- No `mtime` in v1. FAT timestamps have no timezone and a 2-second resolution; adding them
  later is a header/version bump, not a schema break.
- `truncated` is true when the budget stopped the scan. `count` and `bytes` are what was
  indexed, not what is on the card.
- `state` is `ok`, `no_card`, `no_dir`, `empty` or `error`; `error` carries the sentence the
  index page would show. A card-less device still answers 200 with an empty tree and
  `state: "no_card"`, so a client gets a diagnosable document rather than a bare 503. A 503 is
  reserved for "the first scan has not finished yet" when the load is asynchronous.

JSON is escaped at build time (quotes, backslashes, control characters), so the bytes served
are valid UTF-8 without a per-request pass. The builder is the same hand-rolled writer style as
`mini_files.c`'s `writer_t` ([`mini_files.c:58-129`](../../../components/portable/mini_files/mini_files.c#L58-L129)),
not `snprintf` per field, because a 2 MB `snprintf` chain is both slow and stack-hungry.

The exact length is computed in a first pass (node ids, escaped name lengths, fixed key
overhead), the buffer is allocated once at that length, and a second pass fills it. That gives
`Content-Length` without a realloc or a chunked response, and a wrong estimate is a build-time
bug caught by `check_library.py`, not a truncated HTTP body. If the estimate is wrong, the build
fails loudly and publishes an empty generation; it never sends a short body with a larger
`Content-Length`.

### 3.5 Playback: a cursor, not a directory position

`mini_icecast` replaces the `DIR *` pass with a generation and a cursor:

```
view = lib_acquire()
if !view or lib_track_count(view) == 0: release, sleep 2 s, retry
node = pending ? pending : lib_track_at(view, cursor)
cursor = (cursor + 1) % lib_track_count(view)
path = root + "/" + lib_relpath(view, node)   # copied into a local buffer
set_track(name without extension)              # copies the name
release(view)                                  # the file is opened by path now
stream_file(path)
```

- `skip` advances the cursor; `prev` moves it back with the same 16-entry history the current
  code has, but the history holds `lib_node_t` values, not `telldir()` positions. A generation
  change resets the history (ids are generation-scoped).
- `mini_icecast_play(const char *relpath)` resolves through `lib_find()` and sets a pending
  node. At the next chunk boundary `stream_file()` returns, the pending node plays, and the
  current track is abandoned exactly as `skip` does today. A path that does not resolve is an
  error and does not interrupt playback. The pending request carries the generation it resolved
  against: if a rescan wins the race, the broadcast re-resolves the path in the new generation
  (the path is the stable key; the id is only a fast handle) or drops the request if the track
  is gone.
- `mini_icecast_status_t` gains `path[MUSIC_PATH_MAX]`, `generation` and `track_index`, and
  keeps `file` (basename), `title`, `library_count`, `duration_ms`, `elapsed_ms` and the levels
  unchanged. The cassette reads the same snapshot; it does not need the path.
- The playlist order is DFS: a directory's children are visited in sorted order, so
  `Artist/Album/01` plays before `Artist/Album/02` and before the next sibling directory. A
  track at the root sorts among directories by name, because there is one sorted children list.

The current `set_track()` uses the filename minus extension as the title
([`mini_icecast.c:371-388`](../../../components/portable/mini_icecast/mini_icecast.c#L371-L388)).
With nested paths, keep the basename as the title and put the relative path in the new status
field. Do not title a track `Artist/Album/song` unless the display is changed to show it; the
cassette's label width is a UI constraint, not this module's.

### 3.6 HTTP surface

The file surface grows four routes and changes two existing ones. Registration order matters
because matching is longest-prefix ([`http_parse.c:85-96`](../../../components/portable/http_server/http_parse.c#L85-L96)).

| Method and path | Response | Notes |
|---|---|---|
| `GET /` | HTML index | Rendered from the tree, no SD. Same diagnostic pages when no card/no directory. |
| `GET /api/library` | `application/json` | Prebuilt, `Content-Length`, `ETag: "gen-<n>"`. |
| `GET /api/library.bin` | `application/octet-stream` | Prebuilt arena, same ETag, `Content-Disposition: attachment`. |
| `GET /api/status` | `application/json` | Small snapshot, generated per request (it changes). |
| `GET /music/<path>` | original bytes | Tree lookup, then `stat`/open to serve the current file. |
| `POST /api/play` | `application/json` | Body `{"path":"..."}`; control surface for the UI/tests. |
| `POST /mcp` | `application/json` | The mini MCP (§4). |

`GET /api/library` and `.bin` are served while holding a generation lease; the JSON send is one
`http_send_all()` over the prebuilt buffer, so a 2 MB response does not allocate. The current
`http_send_timeout_ms` discipline applies (the route may wait longer than the 400 ms default,
as the download does at [`mini_files.c:344-350`](../../../components/portable/mini_files/mini_files.c#L344-L350)).

`GET /` changes from "list every track in one flat directory" to "list every track in the tree,
in playlist order, each link `/music/<url-encoded relative path>`". The existing
`check_index.py` regex still matches, but its expected set must come from `os.walk()`, not
`os.listdir()`; that is a test change, not a compatibility break for humans.

`/music/<path>` decodes the whole remainder with `http_url_decode()`, splits on `/`, and calls
`lib_find()`. It refuses a decoded path longer than the library's buffer, a NUL, an empty
segment, or `.`/`..` *before* lookup; then it looks up. If the node is not a track, 404. If the
file has disappeared since the scan, `stat()`/`open()` fails and the response is 404, not 500.
The `music_dir` module is then redundant: its only remaining caller is the scan's name
predicate, which moves into `library`. Retire it rather than keeping a second path authority.

### 3.7 Loading, rescanning and card arrival

`library_load()` runs after the card mount and before `mini_files_start()` /
`mini_icecast_start()`, so both consumers start with a generation. The natural place is between
steps 3 and 4 in `app_services_start()`
([`app_services.c:78-100`](../../../components/portable/app_services/app_services.c#L78-L100)),
because that is where the card state is known and where the order of allocation still matters.

- **No card at boot:** publish an empty generation with `no_card` state; the index and
  `/api/library` say so. The broadcast idles. The card retry task, on a successful mount, calls
  `library_load()` + `lib_publish()` and then `mini_files_set_card()`. The broadcast sees the
  new generation at its next loop and starts playing without a reboot.
- **Rescan while playing:** an explicit `POST /api/rescan` (or an MCP tool) loads a new
  generation off the side. The broadcast finishes the current track on its old lease, then
  releases it and picks up the new one. The HTML/JSON surfaces switch at the swap. The old
  generation's arena and JSON are freed when the last of its readers release. This is the case
  that makes the lease necessary; a simple global lock around a 2 MB JSON send would block the
  rescan for seconds. One caveat: on IDF 5.4.4 the VFS's readdir→stat cache is a single
  per-volume slot, so the broadcast can evict it between the scan's `readdir()` and `stat()`;
  on 6.1 the cache is per open `DIR`. A rescan while playing should therefore park the broadcast
  (the same mechanism a download uses) or accept slower scans on 5.4.4.
- **Card removed:** there is no hot-remove notification in this firmware. Reads start failing,
  the broadcast skips the track, and the library remains stale until a rescan/reboot. State
  that limitation; do not pretend otherwise.

### 3.8 Budgets and failure modes

Two counters cap the build: `max_bytes` for arena + JSON, `max_tracks` for playlist entries.
Recommended defaults: **4 MiB** and **20 000 tracks**. At ~80 B/track + ~200 B/track + name
bytes, 20 000 tracks is about 6 MiB — over the byte cap — so the byte cap binds first, around
13 000–15 000 average tracks. When a cap is hit:

1. Stop scanning (do not abort the boot, do not reboot).
2. Set `truncated` in the header and JSON, log one line with the reason and the counts.
3. Serve and play what was indexed. The HTML index says "library truncated at N tracks".
4. `/api/library` still returns 200; `GET /api/status` exposes `truncated`.

A failed allocation has the same shape: publish an empty generation with a `no_memory` state,
keep the device on the network, and tell the user in the index rather than failing the boot.
The existing no-card path already establishes that pattern.

All big allocations are PSRAM-preferred with an internal fallback only below a small threshold,
matching the project's rule and the UI decision
([`0004-ui-memory-is-external.md`](../../../docs/adr/0004-ui-memory-is-external.md)). The arena must
not land in internal RAM: the encoder and Wi-Fi need that pool
([`README.md:180-188`](../../../README.md#L180-L188)).

---

## 4. The mini MCP

### 4.1 Which MCP? Legacy 2025-11-25, stateless, JSON-only

The current revision is **2026-07-28**, a stateless rewrite: it removed the `initialize`
handshake, `ping` and protocol-level sessions, removed the GET endpoint, and added
`server/discover` plus mandatory `Mcp-Method`/`Mcp-Name` request headers
([changelog](https://modelcontextprotocol.io/specification/2026-07-28/changelog.md)). The
previous revisions are the "legacy" era: `2025-03-26` introduced Streamable HTTP, `2025-06-18`
added the `MCP-Protocol-Version` header, and `2025-11-25` made no breaking transport change
(only clarification and SSE polling). The full evidence trail is in
`.scratch/library-tree/research-mcp.md`.

A working reference implementation exists: [solnera/esp32-mcpserver](https://github.com/solnera/esp32-mcpserver)
(MIT, C++/Arduino, `ESPAsyncWebServer`). It is not code this firmware can reuse — different
build system, different HTTP stack, ArduinoJson — but it is a primary source for the protocol
decisions below. It defaults to **2025-11-25**, supports `2025-06-18` and `2025-03-26`, serves
tools only, is stateless, answers JSON only, validates `Origin`/`Host`, advertises
`_mcp._tcp` over mDNS, and caps the body at 8192 bytes.

Client reality matters more than the calendar:

- **MCP Inspector** defaults to the legacy era ("plain `initialize`, no probing at all").
- **Claude Code** has a v1 runtime on TypeScript SDK 1.x (legacy) and a v2 runtime that can
  negotiate 2026-07-28; it auto-negotiates down to legacy.
- **Claude Desktop**'s local configuration is stdio-only, so no HTTP MCP server — on the board
  or anywhere else — can attach without a bridge.
- The official TypeScript v2 SDK's `createMcpHandler` serves stateless 2025-era traffic by
  default and makes the modern era an explicit opt-in.

For a device, the smallest useful, maximally compatible target is therefore **legacy
2025-11-25**, stateless and JSON-only:

- **Transport:** Streamable HTTP, one endpoint (`/mcp`) on the existing port 8000, POST only.
  GET and DELETE return 405 with `Allow: POST`, which the legacy transport allows for a server
  that offers no standalone SSE stream
  ([transports, "Listening for Messages"](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports.md)).
- **Responses:** `Content-Type: application/json`, one JSON object per request. The server is
  allowed to choose JSON or SSE per request, and clients must support both
  ([transports, "Sending Messages"](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports.md)).
  SSE would only be mandatory if a tool emitted mid-call notifications; ours do not.
- **Sessions:** none. The server never assigns `Mcp-Session-Id`; session assignment is a MAY
  ([transports, "Session Management"](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports.md)).
- **Lifecycle:** `initialize` → result, `notifications/initialized` → 202, then requests. Echo
  the client's requested version if it is one of `2025-11-25`, `2025-06-18`, `2025-03-26`;
  otherwise return `2025-11-25`
  ([lifecycle, "Version Negotiation"](https://modelcontextprotocol.io/specification/2025-11-25/basic/lifecycle.md)).
- **Initialize is strict:** `protocolVersion`, `capabilities`, `clientInfo.name` and
  `clientInfo.version` are required; a missing field is `-32602` with a message naming it. This
  is what the spec requires and what the reference implementation enforces.
- **Capabilities:** `{"tools":{"listChanged":false}}` only. No resources, prompts, sampling,
  elicitation, logging or tasks. `initialize`, `notifications/initialized`, `ping`,
  `tools/list` and `tools/call` are the whole method surface.
- **Protocol header:** `MCP-Protocol-Version` is optional on a request; absent is accepted,
  unsupported is 400 with `-32602`. `MCP-Protocol-Version` is returned on every JSON-RPC
  response, as the reference does.

This is a deliberate subset, not a claim of full 2025-11-25 conformance. It is enough for MCP
Inspector, Claude Code and the official SDKs' legacy clients, and it avoids SSE, session state
and server-initiated requests entirely. The modern era is a later envelope change (§4.7), not a
different tool layer. `Accept` is **not** validated: the server always answers JSON, and a
client that only accepts SSE is non-conforming; rejecting it with 406 would add a failure mode
without helping a conforming client. This mirrors the reference implementation, which routes by
path and checks only `Content-Type`.

### 4.2 What the HTTP server must grow

The MCP work is mostly HTTP work. Minimal, in order:

1. **Method-aware routing.** `http_server_register_route()` gains a method mask (`HTTP_GET`,
   `HTTP_POST`), or the server stops rejecting non-GET globally and each route checks. A
   matched path with the wrong method returns 405 with `Allow: POST` on `/mcp`, and 404 for a
   path no route owns.
2. **Request body.** For POST, require `Content-Length` (no chunked; 411 otherwise), cap it at
   **8192 bytes** (413), read it after the header block, and preserve bytes already read past
   `\r\n\r\n`. Body memory is heap, not the 10 KB connection stack. 8192 is the reference
   implementation's cap and leaves room for a maximum-length path even if a client escapes
   every non-ASCII byte as `\uXXXX`.
3. **Header capture.** `http_request_t` needs the fields the MCP checks: `content_type`,
   `content_length`, `origin`, `host`, `mcp_protocol_version`. `Accept` is deliberately not
   captured or rejected (see §4.1); `Mcp-Method`/`Mcp-Name` are not required by the legacy
   revision. `http_parse.c` already finds headers by name; extend it to copy these into the
   struct instead of only testing `Icy-MetaData`
   ([`http_parse.c:13-35`](../../../components/portable/http_server/http_parse.c#L13-L35)).
4. **Status codes.** `202 Accepted` (notification), `400` (body/JSON/envelope), `403` (bad
   Origin), `404` (unknown path), `405` (GET/DELETE on `/mcp`, with `Allow`), `411` (no
   length), `413` (too large), `415` (wrong content type), `500` (out of memory). There is no
   406 (Accept is ignored) and no 505 (tools are synchronous, §4.4). The reason-phrase table is
   a switch ([`http_server.c:46-61`](../../../components/portable/http_server/http_server.c#L46-L61)).
5. **Origin, sessions, headers.** Validate `Origin` only when it is present, exactly as §4.5
   describes; ignore an incoming `Mcp-Session-Id` and never emit one. Return
   `MCP-Protocol-Version` on every JSON-RPC response, as the reference does.
6. **Response header budget.** `http_send_headers()` builds the whole header block in 512 bytes
   and fails with `ESP_ERR_INVALID_SIZE` past that
   ([`http_server.c:94-108`](../../../components/portable/http_server/http_server.c#L94-L108)).
   `MCP-Protocol-Version`, `Allow`, `Content-Type` and `Content-Length` share it; measure the
   longest response (the `.bin` route plus an `ETag`) and raise the buffer deliberately if
   needed rather than discovering a silent failure.
7. **Keep `Connection: close`.** One request per connection is fine for MCP; clients reconnect.
   Do not add keep-alive or chunked framing as part of this change.

The error contract is the reference implementation's table, minus the 505 and the queue-full
case because this design has neither a worker nor a queue:

| Condition | HTTP | JSON-RPC |
|---|---:|---|
| Body over 8192 | 413 | `-32600` |
| No `Content-Length` / chunked | 411 | `-32600` |
| `Content-Type` not `application/json` | 415 | `-32600` |
| `Origin` fails the guard | 403 | `-32600` |
| Fewer body bytes than declared | 400 | `-32600` |
| Empty or invalid JSON | 400 | `-32700` |
| Envelope invalid (`jsonrpc`, `id`, `params`) | 400 | `-32600` |
| Notification (no `id`) | 202 | no body |
| `notifications/initialized` with an `id` | 200 | `-32600` |
| Unknown method | 200 | `-32601` |
| Unknown tool | 200 | `-32602` |
| Tool/argument failure | 200 | result with `isError: true` |
| Out of memory while building the response | 500 | `-32603` |
| GET/DELETE on `/mcp` | 405 | not JSON-RPC; `Allow: POST` |
| Any other path | 404 | `-32600` |

Once a request parses as a valid envelope, protocol-level failures are HTTP 200 with a JSON-RPC
error object — that is what MCP SDKs expect. `notifications/initialized` is a notification, so a
copy carrying an `id` is a request for a method that must not be one: it is the 200/`-32600` row,
not a 202.

A 1024-byte request-header buffer is enough for this subset: `Host`, `Content-Type`,
`Content-Length`, `MCP-Protocol-Version`, `Origin`, `User-Agent` and the optional
`Mcp-Session-Id` are ~300–500 bytes. The legacy revision does not require `Mcp-Method` or
`Mcp-Name`; those belong to 2026-07-28. Raise the header buffer to 2048 only if a real client is
rejected; it sits on the 10 KB connection stack.

### 4.3 JSON-RPC core

Use **jsmn** for the **8192-byte** request body: a single MIT-licensed header, ~200 lines of
C89, no allocation, and a fixed array of 16-byte tokens pointing into the body. It does not
unescape strings, so a small bounded `json_unescape(dst, dst_size, src, src_len)` handles tool
arguments (a path can contain `\"`, `\\` or `\uXXXX`). Do **not** use cJSON: ESP-IDF 6.1 no
longer bundles it (it moved to the managed `espressif/cjson` component in 6.0), and a parse tree
costs ~300–400 B per tool-call object plus a second allocation to print it. This device parses
one small object and writes a small response; a tree is the wrong shape.

Responses are written by hand into a bounded buffer, because the shapes are few and known. The
static `tools/list` JSON is stored as a string constant and sent as-is.

Request handling:

| Method / condition | Result | HTTP |
|---|---|---|
| `initialize` (strict, below) | `protocolVersion`, `capabilities: {tools:{listChanged:false}}`, `serverInfo`, `instructions` | 200 JSON |
| `notifications/initialized`, no `id` | none | 202, no body |
| `notifications/initialized`, with `id` | `-32600` ("must be sent as a notification") | 200 JSON |
| `ping` | `{}` | 200 JSON |
| `tools/list` | static tool array | 200 JSON |
| `tools/call` | tool result (`isError` on failure) | 200 JSON |
| unknown method | `-32601` | 200 JSON |
| unknown tool | `-32602` | 200 JSON |
| malformed JSON | `-32700` | 400 |
| invalid envelope (`jsonrpc`, `id`, `params`) | `-32600` | 400 |
| body over 8192 | `-32600` | 413 |
| out of memory while building the response | `-32603` | 500 |

A body without an `id` member is a **notification**: it is answered 202 and never executed,
whatever its method. This is what the reference implementation does and it is the JSON-RPC 2.0
rule. `notifications/initialized` with an `id` is therefore a request, not a notification, and
is rejected as above. Batch arrays are rejected (`-32600`): the transport says the body is a
single JSON-RPC message.

`initialize` is strict about the spec's required fields:

```json
{
  "jsonrpc": "2.0", "id": 1, "method": "initialize",
  "params": {
    "protocolVersion": "2025-11-25",
    "capabilities": {},
    "clientInfo": { "name": "inspector", "version": "1.0.0" }
  }
}
```

`protocolVersion`, `capabilities`, `clientInfo.name` and `clientInfo.version` are required; a
missing field is `-32602` naming it. If `protocolVersion` is one of `2025-11-25`, `2025-06-18`
or `2025-03-26`, the result echoes it; otherwise the result returns `2025-11-25` (the server's
latest legacy version, which is also the reference implementation's default). Response:

```json
{
  "jsonrpc": "2.0", "id": 1,
  "result": {
    "protocolVersion": "2025-11-25",
    "capabilities": { "tools": { "listChanged": false } },
    "serverInfo": { "name": "xtape", "version": "1.0.0" },
    "instructions": "The xtape music library on the card. Use library_list/search to browse; player tools are available only when control is enabled."
  }
}
```

No `Mcp-Session-Id` header is returned. `MCP-Protocol-Version` on a request is optional; absent
is accepted, unsupported is 400 with `-32602`. Every JSON-RPC response carries
`MCP-Protocol-Version` with the version the build speaks, as the reference does.

### 4.4 Tools

Read-only tools are always available. **Control tools are off by default** and compiled in only
with `XTAPE_MCP_CONTROL=1`, because this device has no authentication and now advertises itself
over mDNS. The HTTP `/api/play` route and the BOOT button remain available regardless; the flag
only decides what an LLM can do through MCP. When control is off, `tools/list` omits the control
tools and a `tools/call` naming one is `-32602` (unknown tool).

| Tool | Arguments | Result | Class |
|---|---|---|---|
| `library_stats` | `{}` | counts, bytes, generation, truncated, scan time | read-only |
| `library_list` | `{path?, depth?=1, limit?=200}` | entries under a directory, in playlist order | read-only |
| `library_search` | `{query, limit?=50}` | tracks whose path contains `query`, case-insensitive | read-only |
| `library_track` | `{path}` | one track's metadata, or a tool error | read-only |
| `player_status` | `{}` | title, file, path, elapsed, duration, levels, library count | read-only |
| `player_play` | `{path}` or `{id, generation}` | accepted; interrupts at the next chunk | control |
| `player_skip` | `{}` | accepted | control |
| `player_prev` | `{}` | accepted | control |
| `library_rescan` | `{}` | started; generation changes asynchronously | control |

`player_play` interrupts the current track at the next chunk boundary (the same path `skip`
uses), rather than queueing it behind the current one: “play this now” is the useful semantic
for a live broadcast. A path that does not resolve is an `isError` and does not interrupt
playback.

`library_rescan` starts a task and returns immediately; it must never run the scan inside the
HTTP worker, where a 26-second scan would outlive the connection and the send timeout. Only one
rescan runs at a time; a second request returns `isError: true` with "a scan is already
running".

Tool definitions are a static JSON string, two constants selected at compile time: one with the
four control tools and one without. `tools/list` returns them in a deterministic order. Every
tool has `inputSchema` and an `annotations.readOnlyHint`; control tools also
`destructiveHint: false` (they change what plays, not what is stored).

Result shape: text content only, no `structuredContent` in v1. A `library_list` result is a
compact JSON array serialized into the text block; `isError` is true for a bad path, an
out-of-range limit, or a play request that does not resolve. That keeps the response writer
simple and works on every legacy client.

Limits are part of the interface: `library_list` caps at 200 entries, `library_search` at 50
tracks, and both report `"truncated":true` rather than returning a 2 MB payload to a model. A
model that needs more pages by `path`.

### 4.5 Security

The device binds `0.0.0.0:8000` and has no authentication by decision
([specification §7](../../../docs/specs/sd-stream-server.md)). MCP does not change that, but it
raises the stakes: a tool call can change the broadcast, and mDNS now advertises the endpoint to
the LAN. The mitigating choices are: read-only tools by default, a strict `Origin` guard for
browser traffic, and `Content-Type: application/json` (which forces a CORS preflight for a
cross-origin browser POST).

- **mDNS.** Fixed hostname **`xtape.local`**, service **`_mcp._tcp`**, port **8000**, TXT
  `path=/mcp`, `endpoint=http://xtape.local:8000/mcp`, `device_type=mcp:device`. Start it after
  Wi-Fi has an IP and after `/mcp` is registered and the HTTP server is listening; advertising a
  dead endpoint is worse than no discovery. The hostname is fixed, so **one device per LAN**;
  that is documented rather than solved with a MAC suffix. No `usn` record in v1. This is the
  same shape as the reference implementation, minus its generated `usn`. mDNS is app-only:
  `app_board_mdns_start("xtape", 8000, "/mcp")` in the board adapter, a no-op on the host, so
  the portable MCP module never links ESP-IDF's `mdns`.
- **Origin is validated only when present.** No `Origin` (curl, native MCP SDKs) is accepted.
  When `Origin` is present:
  1. `Host` must be present.
  2. `Origin` must start with `http://` (no TLS on this device) and its authority must contain no
     path, query or fragment.
  3. The `Origin` authority must equal `Host` case-insensitively.
  4. That authority must be the device's current local IP with `:8000`, or `xtape.local:8000`
     (bare only if the server ever listens on port 80).
  Failure is `403` before the body is parsed. Comparing `Origin` to `Host` alone would not stop
  DNS rebinding — after a rebind both still carry the attacker's domain — so the pin to the IP
  or the advertised mDNS name is the part that matters. This is the reference implementation's
  guard, and it is the spec's DNS-rebinding protection
  ([transports, "Security Warning"](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports.md)).
- **No `Host`-only rejection.** A request without `Origin` may carry any `Host`; rejecting it
  would break native clients that connect by IP while sending a different `Host`, and it adds
  nothing against a browser, which always sends `Origin`.
- **The spec says a local server SHOULD bind 127.0.0.1**, and this device binds `0.0.0.0`
  because serving the LAN is the product. That is an accepted, documented risk, not an
  oversight; the mitigation is the mDNS/IP pin above plus a private network. If the device is
  ever exposed beyond a trusted LAN, put the host bridge in front, or add a static bearer token
  and TLS termination — the firmware itself has neither.
- **Read-only by default.** Control tools are compiled only with `XTAPE_MCP_CONTROL=1`; the
  HTTP `/api/play` route and the BOOT button remain available. This is the safer default now
  that mDNS makes the endpoint discoverable, and it keeps the MCP surface useful for browsing.
- **No SSE** means no long-lived connection from an MCP client to hold a connection worker.
  Every call is bounded by the existing header/send timeouts.
- **Rate limiting** is not needed for one speaker on a LAN; if it becomes one, reject a second
  concurrent `tools/call` with 429 rather than adding a token bucket.

### 4.6 The host stdio bridge (the Claude Desktop path)

If the goal is only to let a desktop agent browse and control the device, a ~200-line Node
stdio server in `tools/xtape-mcp.mjs` is enough: line-delimited JSON-RPC on stdin/stdout,
implementing `initialize`, `tools/list`, `tools/call`, forwarding to the device's
`/api/library` and `/api/status` over `fetch`. It needs no firmware POST, no JSON parser on
the device and no new HTTP surface.

It is not the primary recommendation — the device already has an HTTP server and should not
need an always-on laptop — but it is **required for Claude Desktop**, whose local configuration
runs a stdio `command` and cannot attach to an HTTP MCP URL. It is also the right first test
harness for the tool layer and a useful fallback on a network where the board is not directly
reachable. Build it after the JSON API exists; it validates that the JSON shape is usable
outside C.

The on-device endpoint is `http://xtape.local:8000/mcp` or `http://<device-ip>:8000/mcp`.

| Client | On-device `/mcp` (legacy) | Host bridge |
|---|---|---|
| MCP Inspector | yes (default era is legacy) | yes |
| Claude Code | yes (v1 runtime auto-negotiates legacy) | yes |
| Cursor | yes in principle; revision support UNVERIFIED | yes |
| Claude Desktop | no (local config is stdio-only) | yes |

The bridge can also own the modern 2026-07-28 handshake later, translating to the device's
legacy endpoint, which keeps protocol churn out of the firmware if that becomes the cheaper
path.

### 4.7 The 2026-07-28 upgrade path

The modern era is a different envelope around the same tools, not a different tool layer:

- `initialize` is replaced by `server/discover` (mandatory) and per-request `_meta`
  (`io.modelcontextprotocol/protocolVersion`, `clientInfo`, `clientCapabilities`).
- Every request must carry `MCP-Protocol-Version`, `Mcp-Method` and (for `tools/call`) `Mcp-Name`
  headers whose values match the body; mismatches are 400 with `-32020`.
- Results carry `resultType: "complete"`; `tools/list` carries `ttlMs` and `cacheScope`;
  `ping` no longer exists.
- No sessions, no SSE GET, no `initialize`.

A dual-era server detects the era by the body (`initialize` vs `_meta`) and keeps one tool
dispatch. The extra code is a branch in the envelope, not a second implementation. Schedule it
after the legacy server is working and a client that needs it exists; the reference
implementation does not implement 2026-07-28 either. Shipping legacy-only in 2026 is a
compatibility decision backed by the SDKs' default behaviour and by the reference's choice, but
it should be revisited, not forgotten.

---

## 5. What changes, file by file

The architecture decision is recorded in
[ADR 0012](../../adr/0012-boot-built-library-arena.md).

| Area | Files | Change |
|---|---|---|
| Format authority | new `components/portable/audio_format/`; `mi_decode.c/.h`, `mini_icecast/*`, `mini_files/*` | Move `mi_fmt_t` + `mi_dec_format_of()`; update includes and host Makefile. |
| Library | new `components/portable/library/{library.c,include/library.h}` | Scan, arena, JSON, lookup, generations. |
| Composition | `app_services.c`, `appliance.c`, `app_services.h` | Load after mount; reload on card retry; expose state. |
| File surface | `mini_files.c`, `music_dir.c/.h` | `/api/library[.bin]`, tree-aware index/download; retire `music_dir`. |
| Broadcast | `mini_icecast.c/.h` | Playlist from library, cursor/history, `play(path)`, status path. |
| HTTP | `http_server.c/.h`, `http_parse.c/.h` | Method mask, 8192-byte body, `Origin`/`Host`/`MCP-Protocol-Version` fields, status table. |
| MCP | new `components/portable/mcp/{mcp.c,include/mcp.h}`, vendored `jsmn.h` | `/mcp`, JSON-RPC 2.0, tools; control behind `XTAPE_MCP_CONTROL`. |
| Discovery | `app_board.h`, `board_esp32.c`, `board_host.c`, `app_services.c` | mDNS `_mcp._tcp` behind an app-only seam; host no-op. |
| Build | `host/Makefile`, `components/*/CMakeLists.txt`, `main/CMakeLists.txt` | New sources and includes. |
| Layering | `host/tests/layering.sh` | Library dependency rules; `mini_icecast` not on `mini_files`. |
| Tests | `host/tests/t14_library.c`, `t15_mcp.c`, `check_library.py`, `check_index.py`, `http_tests.sh` | New units and integration. |
| Docs | `README.md`, `docs/specs/sd-stream-server.md`, ADR | URLs, recursion, memory, acceptance (CONTEXT already updated). |

### 5.1 Ticket sequence

The executable breakdown is the fourteen tickets under `.scratch/library-tree/issues/`
(`01` ADR through `14` final documentation), with their blocking edges. The sketch below is the
original sequencing and is kept for the reasoning; the issues are the source of truth for
implementation order.

1. **Extract `audio_format`.** No behaviour change. Both firmware builds and the host suite
   stay green. This unblocks `library` without a component cycle.
2. **`library`: scan + arena + JSON, host-only.** Unit test with a temp tree containing nested
   directories, UTF-8 names, dot-files, a symlink (skipped), a name at 128 bytes and one over,
   an unreadable directory, an empty root, an absolute path just under `FILENAME_MAX` and one over,
   and a tree larger than a tiny test budget. Assert counts, sorted order, `truncated`, lookup,
   path building, CRC and JSON validity (`python3 -m json.tool`). Validate UTF-8 at the scan
   boundary and make an invalid byte a skipped name, not a broken document.
3. **Serve the views.** `/api/library`, `/api/library.bin`, ETag/generation; switch `/` and
   `/music/` to the tree; delete `music_dir`'s second authority. Integration: compare JSON and
   blob against `os.walk()` of the fixture tree; download every nested fixture byte-for-byte;
   traversal names 400/404 without a filesystem call.
4. **Broadcast consumes the playlist.** Cursor/history; `play(path)`; status path. Test:
   playback order is the JSON order, `skip`/`prev` wrap, a nested track plays, a card insertion
   after boot starts playback.
5. **Rescan and generations.** Lease/refcount; publish a new generation while a slow reader
   holds the old JSON; assert the old response completes and the new view is served after.
6. **HTTP POST + body + headers.** Method mask, `202/400/403/404/405/411/413/415/500`, 8192-byte
   cap, `Content-Length` framing, no 406 and no 505. Unit-test the parser and body framing
   (including a body that arrives with the headers).
7. **mDNS and the Origin guard.** App-only `app_board_mdns_start("xtape", 8000, "/mcp")`, service
   `_mcp._tcp`, TXT `path`/`endpoint`/`device_type`; `Origin` validation only when present,
   pinned to the local IP or `xtape.local`; 403 before parsing. Host build: no-op.
8. **MCP read-only.** `initialize` strict, 2025-11-25 default with echo of the three legacy
   versions, `initialized`, `ping`, `tools/list`, `tools/call` for `library_*` and
   `player_status`; text-only results; `MCP-Protocol-Version` response header. Integration with
   a Python client and (if available) MCP Inspector; assert a 202 for a notification, no session
   header, and the error table.
9. **MCP control + `player_play`/`skip`/`prev`/`rescan`.** `XTAPE_MCP_CONTROL=1`; the default
   build omits the control tools. Invalid path is a tool error and does not interrupt the
   stream; rescan is asynchronous.
10. **Board tier.** A real nested card: log scan time, arena/JSON sizes, PSRAM free before/after,
    `truncated` at the cap; play a track three levels deep; 10-minute soak with a rescan and a
    download; confirm no starvation rebases caused by the scan (scan before the broadcast).
11. **Docs and ADR.** Update the README and the spec's URL/memory sections (`CONTEXT.md` is
    already updated by this document), and write an ADR for "the library is a boot-built PSRAM
    arena; requests resolve through it".

Tickets 2–3 are the critical path. 6–9 can proceed in parallel once 3 lands. 10 is the only tier
that can prove the boot-time cost.

### 5.2 Tests worth naming

- `t14_library`: scan/order/lookup/JSON/binary/CRC/generation. No sockets, no board.
- `check_library.py`: parses `/api/library.bin` independently (header, nodes, edges, strings,
  CRC), compares the tree and every path/size to `os.walk()`, and checks the JSON against the
  same tree. This is the "a non-C consumer can read the blob" proof.
- `check_index.py`: updated to `os.walk()`; still exact on the host and `--superset` on a card.
- `http_tests.sh`: nested download byte-identity, traversal table, `/api/library` headers,
  ETag gating if implemented.
- `t15_mcp`: the JSON-RPC table above: strict `initialize` (missing field → `-32602`), version
  echo for the three legacy revisions, fallback to `2025-11-25`, `notifications/initialized`
  with an `id` → `-32600`, notification → 202, unknown method/tool, malformed body/envelope,
  body over 8192 → 413, `Accept: text/event-stream` alone still served as JSON (no 406),
  `Origin`/`Host` guard (403 when bad, accepted when absent), control-off default, and
  `MCP-Protocol-Version` on every JSON-RPC response. Host-only, no model involved.
- `layering.sh`: `library` imports only `audio_format` + libc; `mini_icecast` does not import
  `mini_files`; mDNS is app-only (the host build links no ESP-IDF `mdns`); every new portable
  `.c` is in the host Makefile.

---

## 6. Costs

### 6.1 Arena and JSON size model

A synthetic tree with representative names (artist/album/NN - title.ext), computed by
`.scratch/library-tree/arena_model.py`; it is a model, not a card measurement.

| Library | Nodes | Binary | JSON | Notes |
|---|---:|---:|---:|---|
| 100 tracks | 121 | 0.01 MB (94 B/track) | 0.02 MB (215 B/track) | trivial |
| 1 000 tracks | 1 121 | 0.09 MB (88 B/track) | 0.21 MB (206 B/track) | typical card |
| 10 000 tracks | 10 441 | 0.83 MB (83 B/track) | 1.96 MB (196 B/track) | upper range |

The binary figure includes the 64-byte header, 32-byte nodes, one 4-byte edge per non-root
node, 4-byte track ids and the name bytes; JSON includes paths and keys. Storing a full path
per node instead of building it from parent links would add ~1 MB at 10 000 tracks, which is
why the design does not.

At 10 000 tracks both views fit in **2.8 MB of PSRAM**, leaving ~5 MB of the measured 7.98 MB
free after the screen
([`README.md:767-770`](../../../README.md#L767-L770)). A 4 MiB cap therefore binds around
13 000–15 000 average tracks, before the 20 000-track cap.

### 6.2 Boot scan: the measurement that decides

**ESTIMATE.** A walk of N entries does N `readdir()` calls; the following `stat()` is served
from the VFS cache, so the card cost is the directory reads, not two transactions per entry.
FatFs uses the card's **512-byte logical sector**, not the 4096 in `CONFIG_FATFS_SECTOR_4096`
(that option sizes the partition generator only), so 16 short 32-byte dirents fit a sector; a
128-character LFN name consumes 10 LFN entries plus one short name, about 1.45 names per
sector. At the repo's measured 250 KB/s (1.9 ms per 512 B) the model gives:

| Library | Name length | Sectors | Time |
|---|---:|---:|---:|
| 5 000 | 32 chars | ~1 250 | ~2.4 s |
| 5 000 | 128 chars | ~3 450 | ~6.6 s |
| 20 000 | 32 chars | ~5 000 | ~9.5 s |
| 20 000 | 128 chars | ~13 800 | ~26 s |

Add 20–50 % for path resolution, FAT bumps and scheduling. The range is still too wide to
design against, which is why this is ticket 13's first measurement, not a footnote. The
repo's own 250 KB/s is a download measurement, not a directory-scan measurement; the scan could
be either better (sequential, small reads) or worse (latency-bound), and no on-device number
exists yet.

What to log at boot, once:

```
library: 1 234 tracks, 156 dirs, 4.2 GB, scan 812 ms, arena 108 KB, json 254 KB
library: psram 7 981 000 B free before, 7 612 000 B after
```

If a typical 1 000-track card scans in under ~1 s, scan synchronously before the broadcast and
accept the boot delay. If it takes several seconds, the options are, in order: (a) scan on a
dedicated task after the HTTP server is up, with `/api/library` returning 503 until ready and
the broadcast idling; (b) an on-card cache with a volume-id + root-mtime key, which is a new
invalidation problem and is not recommended for v1; (c) the FatFs `FILINFO` scan port, which
removes the VFS layer but not the card reads, and is now the least promising option. Do not
start the broadcast on a partially built tree unless the playlist is explicitly allowed to
grow; that is a different consistency model.

### 6.3 Request cost, before and after

| Request | Today | After |
|---|---|---|
| `GET /` (1 000 tracks) | opendir + 1 000 `readdir`/cached `stat` + render, per request | walk 1 000 in-memory nodes + render, no SD |
| `GET /api/library` | does not exist | one `send` of ~200 KB, no SD |
| `GET /api/library.bin` | does not exist | one `send` of ~83 KB, no SD |
| `GET /music/<path>` | 1 `stat` + open + stream | 1 `stat` + open + stream (unchanged) |
| Boot | no scan | one scan (the new cost) |

The index becomes CPU-bound rather than SD-bound, and the JSON/blob are I/O-bound on Wi-Fi
only. This is the "serve it more easily without calculating it every request" the request asks
for.

### 6.4 MCP request cost

**ESTIMATE.** A `tools/call` body is < 1 KB, but the cap is 8192; jsmn tokenizes it in place
into ~16-byte tokens, with no parse tree and no allocation beyond the token array (a few hundred
bytes); the response is < 2 KB. Four concurrent connection workers worst case is ~4 × (8 KB
body + ~1 KB tokens + 8 KB response) ≈ 68 KB, all PSRAM. The connection stack stays at 10 KB:
jsmn is iterative, the body and response buffers are heap, and `tools/call` runs inline (§4.4).
The existing stack-headroom telemetry
([`http_server.c:284`](../../../components/portable/http_server/http_server.c#L284)) is the
check; if `t15_mcp` drives the headroom below ~2 KB, raise the connection stack — not a new
task.

`tools/list` and the read-only tools are static/short; no tool may return the full 2 MB JSON
through MCP. That is what `/api/library` is for.

---

## 7. Risks and open questions

| Risk | Why it matters | Mitigation |
|---|---|---|
| Boot scan time | A 10 000-track card could add 5–20 s before audio, with no HTTP page to explain it | Sync scan for v1; measure the real card; move to the async `indexing` state if a typical card exceeds ~2 s |
| PSRAM budget | 2 MB JSON + 0.8 MB arena is fine at 10 k, not at 50 k | 4 MiB cap, `truncated` flag, no internal fallback |
| Path ceiling | The VFS silently truncates an absolute path over ~1024 bytes (`FILENAME_MAX`); a pathological tree can list but fail to open | Enforce `FILENAME_MAX` at scan time, log the skip, assert `FILENAME_MAX` at boot |
| Directory depth | Recursive `DIR *` per level holds one `vfs_fat_dir_t` (heap, not `max_files`) per level | One-handle queue walk; no depth cap |
| Encoding drift | Base `sdkconfig` is CP437 while `sdkconfig.defaults`/`sdkconfig.sunton` select UTF-8; an OEM name is not valid UTF-8 | Pin `CONFIG_FATFS_API_ENCODING_UTF_8` in every build, check the built `sdkconfig.h`, reject invalid UTF-8 at the scan |
| MCP spec churn | 2026-07-28 removed `initialize`/`ping`/sessions; clients vary | Pin 2025-11-25 legacy, echo the older two, keep envelope separate, dual-era ticket |
| No auth/TLS | MCP on the LAN is open and mDNS advertises it | Read-only tools by default, `Origin` guard, JSON content type, document |
| mDNS collision | Fixed `xtape.local` collides if two devices share a LAN | Document one device per LAN; a MAC suffix is the upgrade if it becomes a problem |
| Ordering change | Sorted playlist is not the current FAT order | Intentional; document it; the index and JSON use the same order |
| Stale tree | Files changed after boot are not visible until rescan | Explicit rescan; 404 on a vanished file; document |
| JSON writer bugs | A quote/backslash/UTF-8 byte in a name can break the document | Build-time escaping + `check_library.py` parses the output with a real JSON parser |
| JSON parser bugs | jsmn does not unescape; a malformed `\u` could overrun a naive buffer | Bounded unescape into a fixed 256-byte buffer; fuzz the parser in `t15_mcp` |

**Open questions, with a default:**

1. **Is the boot delay acceptable?** Default for v1: yes, synchronous, no partial state. If
   ticket 13 measures more than ~2 s for a typical card, switch to the async `indexing` state.
2. **Prebuild JSON or stream it from the arena?** Default: prebuild, because the request asks
   for no per-request work and the memory is available. If the byte cap binds, `build_json =
   false` streams from the arena with no SD, which is still a win.
3. **Legacy MCP or dual-era?** Default: legacy 2025-11-25 stateless first (echoing 2025-06-18
   and 2025-03-26); add 2026-07-28 when a client needs it. The reference implementation stays
   legacy-only, which is evidence that this is not leaving users behind yet.
4. **Absolute file size vs `uint64_t`?** Default: `uint64_t` in the node. FAT files are
   < 4 GiB, but the wire is also for clients and 8 bytes is cheap.
5. **Should the HTML index show directories or only tracks?** Default: only tracks, flat with
   full relative paths, matching the current "every track with its size" contract and the
   existing test. The tree is the JSON's job.
6. **Download of a path longer than a URL.** `HTTP_PATH_MAX` is 512 raw bytes, while the VFS
   allows ~1024. Default: accept the gap for v1 — such a path is playable by MCP id but not
   downloadable by `GET /music/...`; add `GET /track/<generation>/<id>` only if a real card hits
   it. A path is the stable key across rescans; an id is not.

---

## 8. Sources

**Repository (primary, read directly):**

- Playlist and broadcast: [`components/portable/mini_icecast/mini_icecast.c`](../../../components/portable/mini_icecast/mini_icecast.c)
  (`is_audio` 665, `seek_track` 674, `broadcast_task` 684, `set_track` 371, status 950).
- Index and download: [`components/portable/mini_files/mini_files.c`](../../../components/portable/mini_files/mini_files.c)
  (index 198, download 267), [`music_dir.c`](../../../components/portable/mini_files/music_dir.c).
- HTTP: [`http_server.c`](../../../components/portable/http_server/http_server.c),
  [`http_parse.c`](../../../components/portable/http_server/http_parse.c),
  [`http_server.h`](../../../components/portable/http_server/include/http_server.h).
- Boot/card: [`app_services.c`](../../../components/portable/app_services/app_services.c),
  [`appliance.c`](../../../components/app/appliance/appliance.c),
  [`board_esp32.c`](../../../components/app/board_esp32/board_esp32.c).
- Hardware/memory/PSRAM: [`README.md`](../../../README.md) (boot log 176-188, UI memory 767-770,
  card/limits 100-107), [`docs/adr/0004-ui-memory-is-external.md`](../../../docs/adr/0004-ui-memory-is-external.md).
- Domain language: [`docs/specs/CONTEXT.md`](../../../docs/specs/CONTEXT.md) (music directory 11-14).
- HTTP contract: [`docs/specs/sd-stream-server.md`](../../../docs/specs/sd-stream-server.md) §6-§7.
- Existing card-index ticket: [`.scratch/sd-stream-server/issues/08-browsable-library-and-downloads.md`](../../../.scratch/sd-stream-server/issues/08-browsable-library-and-downloads.md).

**MCP (primary):**

- Working reference implementation: [solnera/esp32-mcpserver](https://github.com/solnera/esp32-mcpserver)
  (MIT, C++/Arduino) — [README](https://github.com/solnera/esp32-mcpserver/blob/main/README.md),
  [`src/MCPServer.cpp`](https://github.com/solnera/esp32-mcpserver/blob/main/src/MCPServer.cpp),
  [`include/MCPServer.h`](https://github.com/solnera/esp32-mcpserver/blob/main/include/MCPServer.h).
  It defaults to `2025-11-25`, supports `2025-06-18`/`2025-03-26`, is stateless, JSON-only,
  tools-only, caps the body at 8192, validates `Origin`/`Host`, and advertises `_mcp._tcp`.
  It is not reusable code (different build system and HTTP stack), but it is the protocol
  reference for this design.
- Full evidence trail for this section: [`.scratch/library-tree/research-mcp.md`](../../../.scratch/library-tree/research-mcp.md)
  (254 lines, spec-repo release-tag permalinks and SDK source).
- 2026-07-28 changelog: <https://modelcontextprotocol.io/specification/2026-07-28/changelog.md>
  (sessions removed, `initialize`/`ping` removed, `server/discover`, `resultType`, headers).
- 2026-07-28 Streamable HTTP: <https://modelcontextprotocol.io/specification/2026-07-28/basic/transports/streamable-http.md>.
- 2025-11-25 transports (legacy; same transport as 2025-06-18): <https://modelcontextprotocol.io/specification/2025-11-25/basic/transports.md>
  (POST JSON or SSE, sessions optional, GET may 405, Origin MUST, header on later requests).
- 2025-11-25 lifecycle: <https://modelcontextprotocol.io/specification/2025-11-25/basic/lifecycle.md>.
- 2025-06-18 lifecycle and transports (an older supported revision): <https://github.com/modelcontextprotocol/modelcontextprotocol/blob/f5ccad944fdf/docs/specification/2025-06-18/basic/lifecycle.mdx>
  and `.../basic/transports.mdx`.
- Tools: <https://modelcontextprotocol.io/specification/2026-07-28/server/tools.md> and the
  2025-11-25 equivalent.
- TypeScript SDK v2 migration: <https://ts.sdk.modelcontextprotocol.io/v2/migration/support-2026-07-28.md>
  (legacy served by default; modern is opt-in).
- MCP Inspector protocol eras: <https://github.com/modelcontextprotocol/modelcontextprotocol/blob/5f5440bb26a62e2cf3440b92da5a667efa03b267/docs/docs/2026-07-28/tools/inspector/protocol-eras.mdx>
  ("legacy — the default").

**ESP-IDF/FatFs (the scan, path and memory facts):**

- Full evidence trail, tagged [V]/[E]/[U] with line numbers: [`.scratch/library-tree/research-esp.md`](../../../.scratch/library-tree/research-esp.md).
- VFS path truncation and per-open-dir allocation:
  [`vfs_fat.c`](https://github.com/espressif/esp-idf/blob/v6.1/components/fatfs/vfs/vfs_fat.c#L61-L62)
  (`prepend_drive_to_path`, `FILENAME_MAX+3` = 1027-byte buffer here), `vfs_fat_opendir` and `vfs_fat_dir_alloc`
  (L776, L996), `vfs_fat_readdir`/stat cache (L1044-L1063, L844).
- FATFS sector geometry: [`ff.c`](https://github.com/espressif/esp-idf/blob/v6.1/components/fatfs/src/ff.c)
  `GET_SECTOR_SIZE` and `SZDIRE`; `CONFIG_FATFS_SECTOR_4096` is the partition generator only.
- Unaligned access flags: [`core-isa.h:202-205`](https://github.com/espressif/esp-idf/blob/v6.1/components/xtensa/esp32s3/include/xtensa/config/core-isa.h#L202-L205).
- ESP-IDF heap capabilities: <https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/system/mem_alloc.html>.
- ESP-IDF 6.0 migration note on cJSON moving to the managed component:
  [`protocols.rst`](https://github.com/espressif/esp-idf/blob/v6.1/docs/en/migration-guides/release-6.x/6.0/protocols.rst).
- FatFs `f_readdir` / `FILINFO`: <https://elm-chan.org/fsw/ff/doc/readdir.html> (size and
  attributes are in `FILINFO`; the VFS `readdir` does not expose them).
- jsmn: <https://github.com/zserge/jsmn> (single header, no allocation).

**Model:** `.scratch/library-tree/arena_model.py` (sizes in §6.1); run it with
`python3 .scratch/library-tree/arena_model.py`.
