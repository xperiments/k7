# Current service surface (code-verified)

Scope: the firmware in this checkout as it is implemented today, plus the host tier and the
research/ADR trail where it explains a choice. Every claim cites `path:line`. Labels used
throughout: **IMPLEMENTED** (compiled into the default firmware), **OPT-IN** (build flag),
**HOST-ONLY** (only the `host/` build), **RESEARCHED-ONLY** (a doc exists, no code),
**ABSENT** (nothing).

**Snapshot.** The tree is dirty: commit `12c83c5` plus uncommitted work. None of the
service-surface sources cited here (`http_server`, `mini_files`, `library`, `library_http`,
`mcp`, `app_services`, `mini_icecast`, `board_esp32`, `appliance`) differ from that commit; the
uncommitted changes are in `screen_display/`, `cassette/`, `groups/`, docs and tools. Research
docs are being renamed *while this is being written* — the WebDAV study is now
`docs/research/server/s15-webdav.md` (it was `s14-webdav.md` when the brief was written), and a
new I2S-DAC study appeared as `s14-i2s-dac-output.md` and is now `s15-i2s-dac-output.md`. If a
research path 404s, search for the basename; the file:line references into `components/`,
`host/`, `main/` and `README.md` are stable (those files are unmodified).

Three framing facts that colour everything below:

* There is **no authentication of any kind** — README states it at `README.md:18-31`
  ("There is no authentication anywhere: not on the index, not on the downloads, not on the
  stream, and not on `/api/play` or `/mcp`"), and the glossary encodes it as a property, not a
  gap (`docs/specs/CONTEXT.md:417-421`).
* The server always answers **`HTTP/1.0`** and always closes the connection
  (`components/portable/http_server/http_server.c:102,106`).
* The request's HTTP version token is **never parsed or validated**. `http_parse_request()`
  splits `METHOD SP TARGET[ SP VERSION]` and simply drops everything after the second space
  (`components/portable/http_server/http_parse.c:46-51`). `GET / HTTP/9.9`, `GET /`, and
  absolute-form `GET http://h:8000/stream` are all accepted (`http_parse.c:56-65`).

---

## 1. At a glance

| Surface | Transport + method | Auth | Default / opt-in | Implementation |
|---|---|---|---|---|
| `GET /` | HTTP/1.0 GET, TCP :8000 | none | **IMPLEMENTED**, default | `components/portable/mini_files/mini_files.c:257-271` |
| `GET /stream` | HTTP GET upgraded to a continuous `audio/mpeg` body | none | **IMPLEMENTED**, default | `components/portable/mini_icecast/mini_icecast.c:417-430`, headers `:466-472` |
| `GET /music/<rel>` | HTTP GET, original bytes | none | **IMPLEMENTED**, default | `components/portable/mini_files/mini_files.c:310-432` |
| `GET /api/library` | HTTP GET, JSON | none | **IMPLEMENTED**, default | `components/portable/library_http/library_http.c:80-83` |
| `GET /api/library.bin` | HTTP GET, octet-stream arena | none | **IMPLEMENTED**, default | `components/portable/library_http/library_http.c:85-88` |
| `POST /api/play` | HTTP POST, `application/json` | none | **IMPLEMENTED**, default | `components/portable/mini_icecast/mini_icecast.c:401-415` |
| `POST /api/rescan` | HTTP POST, `application/json` | none | **IMPLEMENTED**, default | `components/portable/app_services/app_services.c:108-132`, registered `:302` |
| `POST /mcp` (+ GET/DELETE → 405) | HTTP POST, JSON-RPC 2.0, stateless | none; `Origin`/`Host` guard when `Origin` present | **IMPLEMENTED**, default (read-only tool set) | `components/portable/mcp/mcp.c:965-1169`, registered `:1196-1210` |
| MCP control tools (`player_play`, `player_skip`, `player_prev`, `library_rescan`) | same route | none | **OPT-IN** `XTAPE_MCP_CONTROL=1` | `components/portable/mcp/mcp.c:585-662`, `:781-815`; build flag `components/portable/mcp/CMakeLists.txt:5-12`, `host/Makefile:28-30` |
| mDNS `_mcp._tcp` + `xtape.local` | UDP mDNS 5353 | none | **IMPLEMENTED** on board; **HOST-ONLY no-op** | `components/app/board_esp32/board_esp32.c:137-162`; `host/board_host.c:40-46`; started `components/app/appliance/appliance.c:231-233` |
| `GET /api/screen.bmp` | HTTP GET, 1,152,054-byte BMP | none | **OPT-IN** `CONFIG_APP_UI_SCREEN_HTTP` | `components/app/screen_display/screen_frame.c:113-154`, registered `:199-208` |
| `POST /api/scene?n=0..9` | HTTP POST, query param | none | **OPT-IN** `CONFIG_APP_UI_SCREEN_HTTP` | `components/app/screen_display/screen_frame.c:160-195` |
| Host server binary (`icecast_host`) | TCP, default port **18000** | none | **HOST-ONLY** | `host/icecast_host.c:45-113` |
| Wi-Fi station | 802.11 STA, hardcoded SSID/PSK | WPA (link layer only) | **IMPLEMENTED**, board only | `components/app/board_esp32/board_esp32.c:98-118` |

Route-table budget: `HTTP_MAX_ROUTES 12`, documented as "the appliance's ten, plus the
screen's two diagnostics" (`components/portable/http_server/include/http_server.h:37`). The
ten are `/music/`, `/`, `/api/library.bin`, `/api/library`, `/stream`, `POST /api/play`,
`/mcp` POST, `/mcp` GET, `/mcp` DELETE, `POST /api/rescan`. With
`CONFIG_APP_UI_SCREEN_HTTP=y` the table is **exactly full**; a thirteenth
`http_server_register_route()` returns `ESP_ERR_NO_MEM`
(`http_server.c:159`), which is why the WebDAV research calls the table full
(`docs/research/server/s15-webdav.md:30,88-113`, "all 12 route slots are taken").

---

## 2. HTTP server core

`components/portable/http_server/` (IMPLEMENTED). It knows nothing about audio, files or
markup; every surface arrives as a registered route
(`http_server.h:10-13`; enforced by `host/tests/layering.sh:35-46`).

### 2.1 Listener, port, backlog

| Property | Value | Source |
|---|---|---|
| Listen port | `8000` set in the appliance; `http_server_start` defaults to 8000 if 0 | `components/app/appliance/appliance.c:35,149`; `http_server.c:452` |
| Bind address | `INADDR_ANY`, `SO_REUSEADDR` | `http_server.c:410-416` |
| Listen backlog | `HTTP_LISTEN_BACKLOG 16` | `http_server.h:38`; `http_server.c:417` |
| Socket options | `SO_RCVTIMEO` = `HTTP_HEADER_TIMEOUT_MS` 2000 ms, `SO_SNDTIMEO` = `HTTP_CLIENT_STALL_MS` 400 ms, `TCP_NODELAY` | `http_server.c:183-193`; `http_server.h:27,31` |
| `Server:` header | `sd-stream/1.0` default, overridable (`mini_icecast/host` for the host build) | `http_server.c:103`; `host/icecast_host.c:93` |
| TLS | **absent** — no mbedtls/esp_tls include anywhere in `components/portable` or `components/app` | grep over those trees; `CONFIG_ESP_HTTPS_SERVER_ENABLE` not set in `sdkconfig:1535` |

### 2.2 Methods and routing

* Routes are registered with a **prefix** and a **method**; matching is longest-prefix-wins
  among routes whose method matches case-insensitively
  (`components/portable/http_server/http_parse.c:160-175`).
* Supported methods are whatever a route registers: `GET` (default) and `POST` are the only
  two used. The *parser* accepts any method up to 7 characters (`HEAD`, `PUT`, `OPTIONS`,
  `DELETE`, `MKCOL` …), but no route is registered for them, so they land in the 404/405
  fallback — there is no HEAD support, no OPTIONS/CORS preflight, no PUT. `DELETE` appears
  only as a `/mcp` route that answers 405 (`mcp.c:1207,1171-1181`).
* Method buffer is `char method[8]` and the parser rejects `strlen(method) >= 8`
  (`http_server.h:42`; `http_parse.c:53`) — so an 8-character method such as `PROPFIND`
  is a **400 before routing** (independently confirmed in
  `docs/research/server/s15-webdav.md:59-87`).
* Path/query limits: `HTTP_PATH_MAX 512` raw percent-encoded target, `HTTP_QUERY_MAX 256`;
  over-long is **414** (`http_server.h:33-35`; `http_parse.c:73,78`).
* 405 vs 404: a path owned by a route with a different method is **405**, anything else
  **404**; the `/` catch-all only counts as "path exists" for the literal `/`
  (`http_server.c:264-290`).
* Prefix matching is literal, so `GET /stream/anything` **starts a stream** (the stream
  handler never compares the path, `mini_icecast.c:417-430`) and `POST /api/playXYZ` **plays**
  (`mini_icecast.c:401-415`). `/api/library`, `/api/library.bin`, `/api/rescan`, `/` and
  `/api/screen.bmp` do check `req->path` for exact equality (`library_http.c:17,65`;
  `app_services.c:111`; `mini_files.c:181`; `screen_frame.c:116`); `/api/scene` checks only its
  query, so `POST /api/sceneX` still re-aims the scene (`screen_frame.c:160-167`).

### 2.3 Request parsing limits

| Limit | Value | Behaviour on breach | Source |
|---|---|---|---|
| Request header block | `HTTP_HEADER_MAX 1024` bytes | 400 "request header too large or incomplete" | `http_server.h:36`; `http_server.c:231-250` |
| Header read timeout | 2000 ms | short/partial header → 400; zero bytes → silent close | `http_server.h:31`; `http_server.c:240-243` |
| POST body | `HTTP_BODY_MAX 8192` | 413, then up to 64 KB of the body is drained so the error is not destroyed by an RST | `http_server.h:34`; `http_server.c:301-307,214-224` |
| POST without length | — | 411 | `http_server.c:295-299` |
| POST `Content-Type` | must be exactly `application/json` (lower-cased, parameters after `;` stripped) | 415 | `http_server.c:308-314`; `http_parse.c:132-143` |
| Chunked `Transfer-Encoding` | unsupported | a request without `Content-Length` is 411 | `http_server.c:295` |
| Body allocation | `req->content_length + 1`, PSRAM first, `malloc` fallback, NUL-terminated | 500 on OOM; body freed after the route | `http_server.c:316-341,344` |

Captured headers: `Icy-MetaData` (only `1` counts, `http_parse.c:81-82`), `Content-Length`,
`Content-Type`, `MCP-Protocol-Version` (`:99-109`), `Host`, `Origin`. `Accept`, `Range`,
`If-None-Match`, `User-Agent`, `Cookie` are **not captured** (`http_server.h:41-56`).

Body bytes that arrived in the same TCP segment as the header are preserved
(`http_server.c:251-252,323-325`), which the split/one-packet POST tests rely on
(`host/tests/post_play.py:18-21,74-83`).

### 2.4 Keep-alive vs close, HTTP version

* Every response is `HTTP/1.0` with `Cache-Control: no-cache` and `Connection: close`
  (`http_server.c:102-108`). There is **no keep-alive**, no pipelining, no chunked encoding
  and no `Content-Length`-less body other than the two streaming routes
  (`/` and the diagnostics page pass `(size_t)-1`, `mini_files.c:156,220`).
* One request per connection. `serve_connection()` closes the fd unless the route returned
  `HTTP_UPGRADED` (`http_server.c:343`), which only `/stream` does (`mini_icecast.c:420-422`).

### 2.5 Concurrency model

| Element | Value | Source |
|---|---|---|
| Connection workers | one permanent task per slot, `CONN_TASK_STACK 10240`, prio 5, pinned to core 1 | `http_server.c:30,383-397,469-474` |
| Worker count | `max_conns` from config, default 4, hard cap `HTTP_MAX_CONNS 6` | `http_server.c:454-459`; `http_server.h:39`; appliance sets 4 (`appliance.c:156`) |
| Accept task | separate permanent task, `ACCEPT_TASK_STACK 6144`, prio 5, core 1 | `http_server.c:31,401-446` |
| Hand-off | fd ring + counting semaphores `jobs` / `free_slots`; a slot is taken **before** queueing so the ring cannot overflow | `http_server.c:38-44,435-444` |
| Saturation | one connection beyond `max_conns` (the 5th, with the default pool of 4) gets **503** "too many concurrent requests" and is closed | `http_server.c:435-440` |
| Tasks never deleted | deliberate: deleting a caps-allocated task from itself aborts on IDF 6.1 | `http_server.c:347-380` |
| Stack telemetry | `http_server_conn_stack_headroom_bytes()` reported in the 60 s telemetry line | `http_server.c:393-399`; `mini_icecast.c:1286-1290` |

Note the division of labour: a `/stream` client occupies a connection worker only for the
registration handshake; after that its own sender task owns the fd
(`mini_icecast.c:224-250,432-509`). Downloads and the index occupy a worker for their whole
duration, which is why the pool is 4 and the download semaphore is 1.

### 2.6 Error and status behaviour

`reason_phrase()` knows 200, 202, 400, 404, 405, 411, 413, 414, 415, 500, 503, and falls back
to the literal string **`Error`** for everything else (`http_server.c:46-62`). Two surfaces
emit statuses missing from that table, so their status line reads `HTTP/1.0 403 Error` and
`HTTP/1.0 409 Error`:

* `POST /mcp` with a rejected `Origin` → 403 (`mcp.c:968-970`).
* `POST /api/rescan` while a scan runs → 409 (`app_services.c:121-123`).

Error bodies:

* Default transport errors are a small HTML page
  (`<!doctype html><meta charset="utf-8"><title>…`) with the status and a detail string
  (`http_server.c:113-125`). This includes 411/413/415 on `/api/play` and `/api/rescan`,
  because neither registers an `error_fn` (`http_server.c:201-208`).
* `/mcp` is the only route with a custom transport-error renderer: `mcp_body_error()` turns
  411/413/415/500 into a JSON-RPC error body with `id: null`
  (`mcp.c:1183-1194,1204-1205`).

CORS: **none**. No `Access-Control-Allow-*` header exists anywhere; the only cross-origin
logic in the firmware is MCP's `Origin` guard (`mcp.c:940-963`). A browser therefore cannot
read any of these endpoints cross-origin, including `/api/library`.

---

## 3. The seven routes

### 3.1 `GET /` — the index page

* **Handler**: `index_route` → `index_with_lib` (`components/portable/mini_files/mini_files.c:257-271,179-255`).
* **What it renders**: an HTML5-ish document with a `<meta charset>`, a viewport meta, a
  `<title>` = station name, a small inline `<style>`, an `<h1>` `"<station> - music library"`,
  a line `Now playing: <strong>…</strong> · <a href="/stream">live stream (MP3)</a>`, then a
  flat `<ul>` of **every track in Playlist order** — each item
  `<li><a href="/music/<url-encoded-relpath>">relpath</a> <span class="n">N B</span></li>`
  (`mini_files.c:142-255`). No directories are rendered as headings; nesting appears only in
  the paths.
* **Generation**: hand-written into a bounded 1 KB `writer_t` that flushes with
  `http_send_all()` — i.e. it **streams** and never buffers the page
  (`mini_files.c:56-98,220-252`). Because the length is unknown, it sends
  `Content-Length: (size_t)-1` → header omitted → body is close-delimited
  (`mini_files.c:220`; `http_server.c:105`).
* **Escaping**: names are HTML-escaped (`& < > " '`) and each path segment is percent-encoded
  with `/` preserved (`mini_files.c:100-132`).
* **Truncation**: if `library_truncated()`, one paragraph
  `"The library is truncated at N tracks."` (`mini_files.c:231-233`).
* **Diagnostic states, all HTTP 200** (`mini_files.c:154-215`):
  no card → "No card mounted" + mount error; card mounted but generation still the boot
  no-card one → "Library not rebuilt"; missing Music directory → "No music directory"; error
  → "Library error"; no generation at all → "No library generation".
* **Errors**: any path other than exactly `/` → 404 (`mini_files.c:181-185`) — unreachable in
  practice for other paths because longer prefixes win or the 404 path in `serve_connection`
  fires first.
* **Depends on**: the published `library_t` (leased via `library_acquire()`,
  `mini_files.c:172-177`), `mini_icecast_now_playing()` (`:218`), `http_server` writers.
* **Evidence**: `host/tests/check_index.py` via `host/tests/http_tests.sh` group "T6"
  (`http_tests.sh:190-200`); nested index `http_tests.sh:285-303`; empty/truncated/no-card/
  no-dir pages `http_tests.sh:336-412`; board index `host/tests/board_library.sh:1-14`.

### 3.2 `GET /stream` — the live MP3 broadcast

* **Handler**: `stream_route` → `mini_icecast_register_client`
  (`components/portable/mini_icecast/mini_icecast.c:417-430,432-509`).
* **Response headers** (written by hand, not through `http_send_headers`):
  `HTTP/1.0 200 OK`, `Content-Type: audio/mpeg`, `icy-name:<station>` in canonical
  no-space-after-colon form, `Cache-Control: no-cache`, `Connection: close`, and
  `icy-metaint: <metaint>` only if the client sent `Icy-MetaData: 1`
  (`mini_icecast.c:466-472`). No `Content-Length`, no `Accept-Ranges`, no CORS.
* **One shared encoder output**: there is exactly one `mi_enc_t` for the station
  (`mini_icecast.c:1331`), one broadcast task (`:1410-1412`), and every connected client gets
  the same bytes fanned out under one mutex (`:252-265`). Each client has its own ICY
  splitter so metadata cadence is per-client (`:437`); the audio is not re-encoded per client.
* **Encoder pipeline**: `dr_libs` decode (mp3/flac/wav, any rate, mono or multichannel) →
  windowed-sinc resample to the station rate (`mi_rs_new`, `mini_icecast.c:885`) → 3-band EQ +
  trim (`tone_apply`, `:667-675`) → 1024-frame chunker (`chunk_append`, `:689-703`) → encoder
  accumulator/re-framer (`mi_enc_feed`, `:684`) → `on_frame` → `broadcast()` → per-client ring.
  The encoder is vendored **shine**, CBR joint stereo, one MPEG frame per `on_frame`
  (`mi_encode.h:1-23`; spec measurements `docs/specs/sd-stream-server.md:196-244`).
* **Station format actually shipped**: 44 100 Hz, **256 kbps CBR, joint stereo, MPEG-1**
  (`appliance.c:146-157`, log line `mini_icecast.c:1326-1328`). Library default bitrate
  constant is 128 kbps (`mini_icecast.h:51`) but the appliance overrides it. Encoder frame is
  1152 samples ≈ 26.1 ms at 44.1 kHz (`mi_encode.h:32-33`).
* **Pacing / burst**: `pace()` compares encoded frames against `esp_timer_get_time()` and
  sleeps to hold `lead_ms` (default 1500 ms) ahead; a >1 s deficit is a "starvation rebase"
  (`mini_icecast.c:576-596`). The burst ring keeps `burst_ms` (default 500 ms) of whole
  encoded frames, capacity = `bitrate*1000/8*burst_ms/1000 + CHUNK_BYTES + 2`
  (`mini_icecast.c:1352-1366`), board log 20 098 B at 256 kbps
  (`README.md:478`). Records are `[len16 LE][whole MP3 frame]`, eviction drops whole records
  oldest-first (`mi_burst.h:1-16`; `mi_burst.c:1-70`).
* **ICY metadata**: per client, `metaint` default 16 000 **encoded bytes**
  (`mini_icecast.h:52-55`; `appliance.c` leaves 0 → default via `mi_station.c:34`). Blocks are
  `StreamTitle='<title>';` padded with zeros to a 16-byte boundary, length byte in 16-byte
  units; an unchanged title still emits a single zero length byte so clients stay aligned;
  `'` in the title is replaced by a space (`mi_icy.c:39-93`). Title = filename with extension
  stripped and `'`→space (`mini_icecast.c:526-535,513-521`); while the card is parked the
  title reads `"Processing"` (`:157-160`).
* **Multiple clients**: `MINI_ICECAST_MAX_CLIENTS 4` (`mini_icecast.h:50`); one permanent
  `ice_send` task and one 10-second ring **per slot**
  (`mini_icecast.c:63,1370-1396`; at 256 kbps ≈ 324 KB per client). The 5th listener gets
  **503** "every stream slot is taken" (`:423-426`). A client whose ring fills is marked
  `dead` and its sender closes it — a slow listener costs only its own slot (`:166-177,
  226-250`). Header write is bounded to 250 ms; steady-state send timeout is 10 s
  (`mini_icecast.c:65,460,483-485`).
* **Source-rate budget**: sources above `MINI_ICECAST_MAX_SOURCE_HZ` (96 000) are skipped with
  a log; the track stays listed and downloadable (`mini_icecast.h:36-49`;
  `mini_icecast.c:869-882`).
* **Card parking**: while a download or a rescan owns the card, the broadcast plays a −40 dBFS
  noise bed and resumes the track where it stopped (`mini_icecast.c:620-660,705-725`).
* **Depends on**: `library` lease, `mi_*`, `tone_eq`, `http_server`, PSRAM for rings/stacks.
* **Evidence**: `http_tests.sh` T3 (ffprobe says mp3/44100/stereo/256k, whole capture decodes
  with zero errors) and T5 (metaint at exactly 16000) — `http_tests.sh:136-173`;
  `host/tests/check_metadata.py`; unit tests `t02`, `t05`, `t07`, `t11`; board one-hour soak
  `README.md:495` and `host/tests/board_soak.sh:1-22`.

### 3.3 `GET /music/<relative path>` — byte-exact download

* **Handler**: `download_route` (`components/portable/mini_files/mini_files.c:310-432`),
  registered on prefix `/music/` (`:477`).
* **Path traversal defences, layered**:
  1. decode **one segment at a time**; a `%2F` inside a segment decodes to `/` and is refused
     rather than becoming a separator (`decode_relpath`, `mini_files.c:278-308`);
  2. reject empty segment, `.`-leading segment, any `..` substring, embedded `/` or `\`,
     decoded length > `LIB_NAME_MAX` (128) (`:294-296`);
  3. reject the whole path if the decoded length differs from the percent-decode length
     (`:293`) — no silent truncation (`http_url_decode` refuses overflow,
     `http_server.c:144`);
  4. resolve **only through the library tree** (`library_find`), never by joining the request
     path (`mini_files.c:341-347`; `library_find` itself refuses `.`/`..`/`\`,
     `library.c:1178-1222`);
  5. re-derive the absolute path from the tree node (`library_abspath`, `mini_files.c:349-355`);
  6. `stat()` + `open()` are on that derived path only (`:374-385`).
* **Exclusivity**: a single-slot mutex `F.dl`; a second download gets **503** "only one
  download at a time; the live stream has priority" (`mini_files.c:357-364,436-444`). The same
  mutex is what the rescan waits on (`app_services.c:61-62`).
* **Broadcast cooperation**: parks the card for up to 600 s, waits 80 ms, and backs off
  100 ms/2 s chunks when `mini_icecast_lag_ms() > 250` (`mini_files.c:366-372,414-421`).
* **Response**: `200`, `Content-Type` from the extension (`audio/mpeg`, `audio/flac`,
  `audio/wav`, `mini_files.c:47-54`), `Content-Length` from `stat()`, then 16 KB reads with a
  5 s send timeout and a 12 ms pause between chunks (`:26-31,386-421`).
* **Range**: **not supported** — no `Range`/`If-Range` parsing, no `Accept-Ranges`, no 206.
  A resumable download is impossible (also stated `README.md:1142`,
  `docs/specs/sd-stream-server.md:38-41`).
* **Errors**: 503 no library / no card; 404 not-in-library, not-a-track, `stat` fail, `open`
  fail; 400 malformed path (`mini_files.c:315-347,375-385`).
* **Depends on**: library lease + `library_find` + `library_abspath`, FATFS/`stat`/`open`, the
  download semaphore, one shared 16 KB read buffer in PSRAM (`mini_files.c:465-475`).
* **Evidence**: `http_tests.sh` T7 (byte-identical + length + type per extension,
  `:491-506`), T8 (11 hostile paths, all 400/404, `:508-528`), T9 (concurrent downloads,
  `:570-607`), nested downloads and `%2F`/`..` refusals (`:305-330`); unit tests
  `host/tests/hostile_names.py`, `two_downloads.py`.

### 3.4 `GET /api/library` and `GET /api/library.bin`

**Both**: `components/portable/library_http/library_http.c` (IMPLEMENTED, registered
`:90-96`). They serve a **prebuilt, immutable generation** — no per-request tree walk, no card
access (`library_http.c:1-11`; `library.h:1-15`).

Common behaviour:

* Path must equal the route exactly, else 404 (`library_http.c:17-20,65-68`).
* `library_acquire()` lease; `release()` after the write (`:69-77`). NULL generation → **503**
  "the library view is not available" (`:71-74`).
* `ETag: "gen-<generation>"` on both (`:36-42`).
* Send timeout raised to 5000 ms (`:51`), then one `http_send_all()` of the whole body
  (`:52-54`).
* No pagination, no filtering, no query parameters, no gzip, no `If-None-Match` handling (the
  ETag is decorative: nothing reads it).

`GET /api/library` — JSON schema, exactly as emitted by `json_document()` /
`json_node_open()` (`components/portable/library/library.c:658-723`):

```json
{
  "root": "/sdcard/music",
  "generation": 1,
  "count": 64,
  "bytes": 2277100658,
  "state": "ok",                 // ok | empty | no_dir | no_card | error
  "truncated": false,
  "error": "",
  "tree": {
    "id": 0, "type": "dir", "name": "music", "path": "",
    "children": [
      {"id": 1, "type": "dir",   "name": "Album", "path": "Album", "children": [ ... ]},
      {"id": 2, "type": "track", "name": "a.mp3", "path": "Album/a.mp3",
       "size": 12345, "format": "mp3"}          // mp3 | flac | wav | unknown
    ]
  }
}
```

* `count` = number of tracks, `bytes` = sum of track sizes, `state` names come from
  `library_state_name()` (`library.c:647-656`), `error` is the diagnosis sentence (`""` when
  fine).
* Node key order is fixed: `id,type,name,path` then `children` for dirs, or `size,format` for
  tracks (`library.c:622-645`). Escaping: `"`→`\"`, `\`→`\\`, control chars → `\u00XX`
  (`library.c:596-614`).
* `truncated` is `true` when the track cap, the byte budget, or the JSON budget stopped the
  scan (`library.c:397-406,935-971`).

`GET /api/library.bin` — binary arena, the same bytes the firmware reads
(`library.c:727-831`; independent reader `host/tests/check_library_bin.py:1-12`):

| Offset | Field | Type |
|---|---|---|
| 0 | magic `0x424C5458` (`"XTLB"` in LE byte order) | u32 |
| 4 | version = 1 | u16 |
| 6 | header_size = 64 | u16 |
| 8 | flags: bit0 = little-endian, bit1 = truncated | u32 |
| 12 | node_count | u32 |
| 16 | edge_count (= node_count − 1) | u32 |
| 20 | track_count | u32 |
| 24 | string_bytes | u32 |
| 28 | crc32 over everything after the header | u32 |
| 32 | generation | u32 |
| 36 | root_name_off | u32 |
| 40 | library_bytes (sum of track sizes) | u64 |
| 48 | reserved32[2] | 8 B |
| 56 | reserved64 | 8 B |

Then: `node_count` × 32-byte nodes `{u64 size, u32 name_off, u32 parent, u32 child_edge,
u16 child_count, u16 name_len, u8 kind(0 dir/1 track), u8 format, u16 flags, u32 track_index}`,
then the u32 child-edge array, then the u32 track index (node ids in Playlist order), then the
string table (`library.c:40-84,775-808`). Layout is pinned by `_Static_assert`s
(`library.c:70-84`) and by the Python reader. `Content-Disposition: attachment;
filename="library.bin"` is added (`library_http.c:43-50`).

**Generation / lease model** (this is the closest thing to a "database" the device has):

* `library_next_generation()` hands out strictly increasing numbers (`library.c:154-161`).
* `library_publish(next)` swaps `g_current` under a pthread mutex, refuses a generation ≤ the
  current one, and frees the old generation only when its last reader releases
  (`library.c:173-193`).
* `library_acquire/release` are reference counts; `library_release` on the current generation
  does not free it (`library.c:195-218`). Every HTTP surface that has no injected library uses
  the lease (`mini_files.c:172-177`; `library_http.c:69-77`; `mini_icecast.c:1095-1102`;
  `mcp.c:1115-1120`).
* A reader that started on gen N finishes on gen N even after a rescan (proved by
  `host/tests/rescan_tests.py:89-136`).

**PSRAM arena caps** (`library.h:28-31,56-63`):

| Cap | Default | Source |
|---|---|---|
| Byte budget | `LIB_DEFAULT_MAX_BYTES` = 4 MiB, and the scan stops when `arena × 3 > budget` (headroom for the JSON view) | `library.h:30`; `library.c:38,405` |
| Track cap | `LIB_DEFAULT_MAX_TRACKS` = 20 000 | `library.h:31`; `library.c:399` |
| Path ceiling | `LIB_PATH_MAX = FILENAME_MAX + 1` (1025 B on this build); a node whose absolute path exceeds it is skipped and counted | `library.h:29`; `library.c:443-450,480-486` |
| Name ceiling | `LIB_NAME_MAX 128` (= `CONFIG_FATFS_MAX_LFN=128`, `sdkconfig.defaults:113`) | `library.h:28`; `library.c:302-310` |
| Truncation flag | `LIB_FLAG_TRUNC` in the blob header + `"truncated":true` in JSON + a sentence on the index | `library.c:935-942,231-233` |
| Allocation class | arena and JSON via `heap_caps_malloc(MALLOC_CAP_SPIRAM)` | `library.c:768,845` |

If the JSON view alone would exceed the budget, the generation is still published with
`truncated=true` and an error sentence saying the binary view is being served only
(`library.c:957-972`).

* **Evidence**: `http_tests.sh` T14 covers 200/type/Content-Length/valid JSON/ETag/generation
  match/CRC corruption detection/frozen-after-deletion/nested tree/no-card/no-dir/empty/
  truncated (`:201-412`); unit test `host/tests/t14_library.c:1-10` parses the blob with an
  independent reader; oracles `check_library.py`, `check_library_bin.py`.

### 3.5 `POST /api/play`

* **Handler**: `play_route` (`components/portable/mini_icecast/mini_icecast.c:401-415`),
  registered POST-only (`:1402`).
* **Body**: exactly `{"path":"<relative path>"}` — the parser is hand-rolled and **refuses
  unknown keys, extra members, and trailing bytes** (`json_play_path`, `:374-393`; JSON string
  unescaping incl. `\uXXXX`/surrogate pairs `:312-372`).
* **Semantics**: `mini_icecast_play()` resolves the path through the library tree
  (case-insensitive fallback allowed, `library_find`), sets `play_pending`, and the broadcast
  task consumes it **at the next chunk boundary** in its playlist loop; the current track is
  not interrupted mid-chunk (`:1166-1179,926-968,1424-1456`). A pending pick survives a
  generation change by being re-resolved by relative path (`:946-961`).
* **Responses**: 200 `{"ok":true}`; 404 `{"ok":false,"error":"no such track"}` — including the
  case where there is no published generation at all, because `mini_icecast_play` returns
  false; 400 `{"ok":false,"error":"bad request"}` for a malformed body
  (`mini_icecast.c:405-414`).
* **Transport errors are HTML**, not JSON: 411/413/415/500 go through
  `http_server`'s default renderer for this route (`http_server.c:201-208`).
* **Who calls it**: the README documents it as the only transport control
  (`README.md:239,256-258`); MCP's `player_play` in control builds calls the same C entry
  point (`mcp.c:586-628`); the Menu's play row calls `mini_icecast_play(path)` in-process
  (`components/app/screen_display/cassette_menu.c:285`).
* **There is no next/previous endpoint.** `mini_icecast_skip()` / `mini_icecast_prev()` are
  reachable only through the MCP control tools (`mcp.c:630-642`) and the host transport test
  (`host/tests/t12_transport.c:140-195`); the current screen has no transport at all
  ("There is no transport on the chrome at all: next and previous belong to the control API",
  `components/app/screen_display/screen_input.c:14-15`; also `README.md:256-258`). With the
  default read-only MCP build, the only way to change what plays is `POST /api/play` or the
  Menu's own row.
* **Gating**: none — always registered, no build flag, no auth.
* **Evidence**: `http_tests.sh` T15 (`:414-446`) + `host/tests/post_play.py` (split and
  one-packet bodies, 411/413/415, unknown path, `Accept` ignored); index assertion
  "the index now names the selected Track" (`http_tests.sh:437`).

### 3.6 `POST /api/rescan`

* **Handler**: `rescan_route` (`components/portable/app_services/app_services.c:108-132`),
  registered POST-only after `mcp_start()` and before the listener (`:302-303`).
* **Body**: **ignored**. Only the transport rules apply (`Content-Length` required,
  `application/json` required) — `rescan_route` never reads `req->body`
  (`app_services.c:108-132`).
* **Semantics**: `app_services_rescan()` claims an atomic flag and starts an 8 KB task
  (`:147-155`); the task waits for the download/card lock, parks the broadcast for up to
  600 s, builds a **new** generation off to the side with the next generation number, then
  publishes it; the old generation is freed when its readers release
  (`:45-106`). A missing card produces the `no_card` generation instead of an error
  (`:76-77`).
* **Responses**: **202** `{"ok":true}`; **409** `{"ok":false,"error":"a scan is already
  running"}`; **500** `{"ok":false,"error":"cannot start the scan"}` for task-creation failure
  (`app_services.c:115-131`).
* **Gating**: none over HTTP. The same function is exposed as the MCP tool `library_rescan`
  in control builds only (`mcp.c:644-661`), and it is called internally by the card-retry task
  when a card appears after boot (`appliance.c:55-73`).
* **Evidence**: `http_tests.sh` T17 (`:460-488`) drives `host/tests/rescan_tests.py`
  (slow reader finishes on the old bytes, generation advances, count grows, one 202 + one
  409, `no_card` stays diagnosable), plus the card-less rescan assertions at `:478-487`.

### 3.7 `POST /mcp`

See §4 — this is the largest single surface and gets its own section.

### 3.8 Opt-in: `GET /api/screen.bmp` and `POST /api/scene?n=`

Covered in §6 (diagnostics); listed here only so the route count closes.

---

## 4. MCP

`components/portable/mcp/mcp.c` (IMPLEMENTED, default build = read-only tools).
Design rationale and client landscape: `docs/research/server/s13-library-tree-and-mcp.md:656-790`.

### 4.1 Transport and protocol revision

| Property | Value | Source |
|---|---|---|
| Endpoint | `POST /mcp` (single endpoint) | `mcp.c:1204` |
| Revisions spoken | `2025-11-25` (default), `2025-06-18`, `2025-03-26` | `mcp.c:26-28,934-938` |
| Version echo | echoes the client's version when supported, otherwise answers `2025-11-25` | `mcp.c:1071-1076` |
| Version header | `MCP-Protocol-Version` accepted on request; if present and unsupported → 400 `-32602`; **emitted on every JSON-RPC response** (incl. 202/403/500) | `mcp.c:989-992,819,821-825,895` |
| Sessions | **none** — never emits or accepts `Mcp-Session-Id` | `mcp.h:1-7`; asserted `host/tests/mcp_tests.py:80-81,125` |
| Framing | **JSON only**. No SSE, no standalone GET stream, no chunked responses, no batching | `mcp.c:994-997` (single object required), `:1171-1181` (GET/DELETE 405) |
| GET/DELETE `/mcp` | **405** with `Allow: POST`, `text/plain`, empty body | `mcp.c:1171-1181,1206-1207` |
| `Accept` | ignored (never parsed) | `http_server.h:41-56`; tested `mcp_tests.py:190-192` |
| `Content-Type` | must be `application/json` (enforced by the HTTP core, not MCP) | `http_server.c:308-314` |
| Body cap | `HTTP_BODY_MAX` 8192 → 413 | `http_server.h:34` |
| JSON parser limits | 128 tokens, nesting depth ≤ 16, integers ≤ 100000 | `mcp.c:30,262,393` |
| Response text cap | `MCP_TEXT_MAX` 128 KiB per tool result | `mcp.c:29,1121-1127` |
| Concurrency | one request per connection, handled on a connection worker; no queue, no worker pool of its own | `http_server.c:383-397` |

Deliberate scope statement: stateless legacy revision with `tools` only, no resources,
prompts, sampling, elicitation, logging or tasks; `capabilities` is
`{"tools":{"listChanged":false}}` (`mcp.c:925-932`; `docs/research/server/s13-library-tree-and-mcp.md:700-718`).

### 4.2 Lifecycle subset

| Method | Behaviour | Source |
|---|---|---|
| `initialize` | requires `params`, `protocolVersion` (string), `capabilities` (object), `clientInfo` (object), `clientInfo.name`, `clientInfo.version`; each missing field is 200 + `-32602` naming it. Result: `{protocolVersion, capabilities, serverInfo:{name:"xtape",version:"1.0.0"}, instructions}` | `mcp.c:1036-1078,925-932` |
| `notifications/initialized` | as a notification (no `id`) → **202, empty body**; sent with an `id` → 200 + `-32600` "must be sent as a notification" | `mcp.c:1018-1022,1031-1034` |
| `ping` | 200, `result: {}` | `mcp.c:1080-1083` |
| `tools/list` | the compile-time table (below) | `mcp.c:1085-1092` |
| `tools/call` | requires `params.name` (string) and optional `params.arguments` (object); unknown tool → 200 + `-32602` | `mcp.c:1094-1165` |
| any other method | 200 + `-32601 "method not found"` | `mcp.c:1167-1168` |

Notifications of *any* method are answered 202 without execution — `id` absent short-circuits
before the method switch (`mcp.c:1018-1022`), so e.g. `notifications/cancelled` is safely 202
and does nothing.

### 4.3 Tool list

Default build (`MCP_CONTROL == 0`) — exactly five, all `readOnlyHint:true`
(`mcp.c:759-779`):

| Tool | Input schema | Result shape (JSON text inside `content[0].text`) | Source |
|---|---|---|---|
| `library_stats` | `{}` | `{"available":true,"state","tracks","directories","bytes","generation","truncated","scan_ms"}` or `{"available":false}` | `mcp.c:530-542,1130-1131` |
| `library_list` | `{path?, depth? 1..8 (default 1), limit? 1..200 (default 200)}` | `{"path","entries":[{name,path,type[,size,format]}],"truncated"}` — recursive to `depth`, stops at `limit` entries | `mcp.c:664-689,553-567` |
| `library_search` | `{query (required, ≤255 B), limit? 1..50 (default 50)}` | `{"query","tracks":[{name,path,size,format,track_index}],"truncated"}` — **case-insensitive substring over the relative path**, not over ID3 | `mcp.c:691-723,476-492` |
| `library_track` | `{path (required)}` | one track object | `mcp.c:725-740` |
| `player_status` | `{}` | `{"title","file","path","elapsed_ms","duration_ms","level_l","level_r","library_count","generation","track_index"}` | `mcp.c:742-757` |

Control build (`XTAPE_MCP_CONTROL=1`) adds four, with `readOnlyHint:false` and
`destructiveHint:false` (`mcp.c:781-815`):

| Tool | Input schema | Result | Source |
|---|---|---|---|
| `player_play` | `{path?}` **or** `{id?, generation?}` (both ints, id/generation must be current) | `{"accepted":true}` or tool error text | `mcp.c:586-628,1141-1142` |
| `player_skip` | `{}` | `{"accepted":true}` | `mcp.c:630-635` |
| `player_prev` | `{}` | `{"accepted":true}` | `mcp.c:637-642` |
| `library_rescan` | `{}` | `{"started":true}`, or a tool error ("a scan is already running", "a scan is not available in this build", "the scan could not be started") | `mcp.c:644-661` |

Result envelope for every tool: `{"content":[{"type":"text","text":"<the JSON above, as a
string>"}],"isError":<bool>}` (`mcp.c:898-917`). **There is no `structuredContent`** — clients
must parse the text (`mcp_tests.py:132-133` asserts its absence). Tool-level failures are
HTTP 200 with `isError:true`, per MCP.

### 4.4 JSON-RPC error table as implemented

| Condition | HTTP | JSON-RPC | Source |
|---|---|---|---|
| `Origin` present but not same-origin | 403 | `-32600 "origin rejected"`, `id:null` | `mcp.c:968-970` |
| Path under `/mcp` but not exactly `/mcp` | 404 | `-32600 "no such path"` | `mcp.c:972-975` |
| Empty body | 400 | `-32700 "empty body"` | `mcp.c:976-980` |
| Unparseable JSON / zero tokens | 400 | `-32700 "parse error"` | `mcp.c:982-987` |
| `MCP-Protocol-Version` present and unsupported | 400 | `-32602 "unsupported MCP-Protocol-Version"` | `mcp.c:989-992` |
| Top level is not an object (array/batch/scalar) | 400 | `-32600 "a single JSON-RPC object is required"` | `mcp.c:994-997` |
| Bad envelope (`jsonrpc`≠"2.0", no `method`, bad `id` type, `params` not object) | 400 | `-32600 "invalid JSON-RPC envelope"` | `mcp.c:999-1016` |
| Valid notification (no `id`) | 202 | — no body | `mcp.c:1018-1022` |
| `notifications/initialized` carrying an `id` | 200 | `-32600` | `mcp.c:1031-1034` |
| `initialize` missing/ill-typed field | 200 | `-32602` + field name | `mcp.c:1036-1070` |
| Unknown method | 200 | `-32601` | `mcp.c:1167` |
| `tools/call` missing params/name, bad arguments type | 200 | `-32602` | `mcp.c:1095-1113` |
| Unknown tool name | 200 | `-32602 "unknown tool"` | `mcp.c:1150-1154` |
| Tool argument/value failure | 200 | result with `isError:true` | `mcp.c:1156-1161` |
| Missing `Content-Length` | 411 | `-32600` (via `mcp_body_error`) | `http_server.c:295-299`; `mcp.c:1183-1194` |
| Body > 8192 | 413 | `-32600` | `http_server.c:301-307`; `mcp.c:1186` |
| Wrong `Content-Type` | 415 | `-32600` | `http_server.c:308-314`; `mcp.c:1186` |
| OOM building a response | 500 | `-32603` | `mcp.c:855-859,878-880,904-906` |
| GET/DELETE on `/mcp` | 405 | not JSON-RPC; `Allow: POST` | `mcp.c:1171-1181` |

### 4.5 Origin guard (the only access control in the firmware)

`origin_ok()` (`mcp.c:940-963`):

* no `Origin` header → **accepted** (native clients);
* `Origin` must start `http://` (not `https://`), authority must contain no `/?#@`, and must
  equal `Host` exactly;
* it must end in `:<port>` where port is the configured HTTP port;
* and then be exactly `<current local IP>:<port>` (queried per request through
  `app_board_local_ip`) **or** `xtape.local:<port>`.

Consequences, all code-visible: a page served from `http://192.168.1.42:8000` can call MCP; a
page served from `http://xtape.lan:8000`, `http://localhost:8000`, `https://…`, or any
rebinding domain gets **403**. `Origin` with no `Host` → 403 (`:943`).

### 4.6 Client-landscape implications

Derived from `docs/research/server/s13-library-tree-and-mcp.md:666-720` (cited there against
the MCP changelog and the reference implementation), and consistent with the code above:

* **MCP Inspector** — the primary target: it defaults to the legacy era (`initialize`, no
  probing), POSTs JSON, and does not require SSE for a tools-only server. Works.
* **Claude Code** — negotiates down to legacy; its v1 runtime is legacy. Works over
  `http://xtape.local:8000/mcp`, subject to the Origin guard not being triggered (CLI clients
  send no `Origin`).
* **Claude Desktop** — local configuration is **stdio-only**, so it cannot attach to an HTTP
  MCP server at all. A bridge would be needed (sketched in
  `docs/research/server/s13-library-tree-and-mcp.md:949-976`, **RESEARCHED-ONLY**, no code in this repo).
* **2026-07-28 clients** — the modern revision removed `initialize`/`ping` and requires
  `server/discover` plus `Mcp-Method`/`Mcp-Name` headers. This server does **not** implement
  it (`mcp.c:26-28` list only the three legacy strings; `docs/research/server/s13-library-tree-and-mcp.md:977-997` is the upgrade path,
  not code). An SDK that insists on the modern era will fail with `-32601`/`-32602`.
* Clients that only accept `text/event-stream` are non-conforming for this server; `Accept` is
  ignored rather than answered 406 (`docs/research/server/s13-library-tree-and-mcp.md:717-720`).

### 4.7 Known wrinkles (code-verified)

* `library_stats.scan_ms` is captured **once at boot** (`mcp.c:64,1200`) and is not refreshed
  by a rescan, so after `POST /api/rescan` the reported scan time is stale.
* `player_play` by `id`+`generation` validates `generation == library_generation(lib)`, which
  is the *leased* generation, not the published one; a lease taken just before a publish can
  legitimately accept the older generation.
* `rpc_error`/`rpc_result` size their buffers from the *unescaped* message length
  (`mcp.c:854,875`), which is fine because the messages are fixed literals; tool text is the
  only variable-length payload and it is capped at 128 KiB.

### 4.8 Evidence

* `host/tests/mcp_tests.py` — initialize across three revisions + fallback, strict
  `initialize`, 202 notifications, `ping`, `-32601`, exact five-tool list, no control-tool
  leak, every tool has an `inputSchema` and `readOnlyHint:true`, no session header, text-only
  results, fixture-derived counts (`tracks=11`, `generation=1`), `library_list` truncation at
  the cap, `library_search` finds 2 tracks with `truncated:true`, `library_track`, unknown
  track → `isError`, unknown tool → `-32602`, `-32700`, `-32600`, boolean id, batch, version
  rejection, `Accept` ignored, 411/415 (`mcp_tests.py:75-210`).
* `host/tests/mcp_control_tests.py` — nine tools, control tools not read-only, bad path is a
  tool error while the stream survives, valid path reaches the broadcast, skip/prev take
  effect, id+generation play, one 202-equivalent start + one "already running", generation
  advances to 2 (`mcp_control_tests.py:89-162`).
* `host/tests/board_mcp_tests.py` — read-only smoke against a live board with no fixture
  oracle (`board_mcp_tests.py:1-4`); board pass through mDNS documented at `README.md:501`.
* Group T16 in `http_tests.sh:449-458` (host-only, because the fixture count is the oracle).

---

## 5. mDNS and discovery

| Property | Value | Source |
|---|---|---|
| Service type | `_mcp._tcp` | `components/app/board_esp32/board_esp32.c:154` |
| Instance name | the hostname, `"xtape"` | `board_esp32.c:146,154`; `appliance.c:231` |
| Hostname | `xtape.local` (fixed) | `board_esp32.c:145`; asserted by the MCP guard `mcp.c:960-962` |
| Port | 8000 (the HTTP port passed in) | `appliance.c:231`; `board_esp32.c:154` |
| TXT | `path=/mcp`, `endpoint=http://xtape.local:8000/mcp`, `device_type=mcp:device` | `board_esp32.c:149-153` |
| Started when | after `app_services_start()` (listener up) and after `app_board_wait_online()` (IP acquired) | `appliance.c:227-233` |
| Failure policy | logged, not fatal — the device still serves | `appliance.c:232-233` |
| Host build | **no-op** returning `ESP_OK` | `host/board_host.c:40-46` |
| Layering rule | mDNS never reaches the host or MCP (checked) | `host/tests/layering.sh:60-66` |
| One-per-LAN | the hostname is fixed, so two devices collide; documented as "one xtape per LAN" | `README.md:122`; `docs/specs/sd-stream-server.md:459-462`; `docs/research/server/s13-library-tree-and-mcp.md:915,1190` |

No other advertisement exists: no `_http._tcp`, no SSDP/UPnP, no RAOP/AirPlay (the only
service added to mDNS is `_mcp._tcp`), no Spotify Connect, and no Bluetooth at all
(`CONFIG_BT_ENABLED` is unset, `sdkconfig:1065`).

---

## 6. Diagnostics and dev-only services

### 6.1 Screen-over-HTTP (OPT-IN, `CONFIG_APP_UI_SCREEN_HTTP`)

Default **n** (`components/app/screen_display/Kconfig:29-42`); enabled for capture builds by
`sdkconfig.defaults.capture:1-16`. Registered from the appliance before the listener starts,
so the routes exist even before the screen is up (`appliance.c:159-168`).

| Route | Behaviour |
|---|---|
| `GET /api/screen.bmp` | grabs one complete frame under the LVGL lock (two full refreshes first), copies it to PSRAM, then streams a 24-bit bottom-up BMP (`image/bmp`, `Content-Disposition: inline; filename="screen.bmp"`, `Cache-Control: no-store`), 54-byte header + `w*h*3` bytes = 1 152 054 B at 800×480. Send timeout 20 s. 503 if the screen is not up or PSRAM is short. (`screen_frame.c:55-154`; capture `screen_display.c:569-637`) |
| `POST /api/scene?n=0..9` | re-aims the diagnostic scene driver; the only accepted query is `n=`; anything else 400 "n must be 0..9"; waits up to 2000 ms for the scene to settle and answers `text/plain` `"scene N ready"` / `"scene N not ready"`; 503 if the screen will not give its lock (`screen_frame.c:156-195`) |

Doc/code mismatch: the Kconfig help says `n=0..8`
(`components/app/screen_display/Kconfig:38`) while the code allows `0..CASSETTE_SCENE_MAX`
= **9** (`cassette_view.h:52`; `screen_frame.c:167,205-206`).

Client tool: `tools/screen_over_http.py` (`capture` / `compare` with per-scene masks;
`tools/screen_over_http.py:1-20`).

### 6.2 Serial console / screen dump (OPT-IN, not a network service)

`CONFIG_APP_UI_SERIAL_DUMP` dumps the framebuffer as base64 `UIROW <n> <data>` lines over the
115200 console, decoded by `tools/decode_screen_dump.py` (`components/app/screen_display/Kconfig:3-20`). No Telnet, no
console REPL, no command channel — the serial port carries `ESP_LOG` output only
(`appliance.c:238-246`).

### 6.3 Telemetry (log only, no endpoint)

Every 60 s the broadcast logs encoder µs/frame and % of one core, per-stage decode/SD/
resample/EQ cost, free internal/external memory and largest internal block, starvation rebase
count, and stack headroom for the broadcast and the worst connection
(`mini_icecast.c:1249-1292`). It is **not** exposed over HTTP; there is no `/status` or
`/metrics` route.

---

## 7. The library "database"

### 7.1 Boot scan and rescan

* The scan runs **inside `app_services_start()`**, synchronously, before the listener starts
  (`app_services.c:217-256`); it needs the 16 KB main-task stack
  (`sdkconfig.defaults:26`; spec `docs/specs/sd-stream-server.md:360-362`).
* Algorithm: explicit queue of node ids, one `opendir`/`readdir`/`closedir` per directory —
  no recursion, no depth limit, flat memory (`library.c:1-13,443-520,993-1001`).
* Ordering: children sorted **case-insensitively**, ties broken byte-exactly, directories and
  tracks interleaved, one flat qsort per directory (`library.c:236-262,522-546`). The
  Playlist is the **depth-first pre-order** of tracks (`library.c:747-759`).
* Dedupe: none — no hash, no name uniqueness enforcement. Two files that differ only in case
  both index (and `library_find` refuses an ambiguous case-folded match, `library.c:1206-1215`).
* Case handling: sorting folds ASCII case; lookup tries exact match first, then a unique
  ASCII-case-insensitive match, and refuses if more than one child folds together
  (`library.c:1166-1222`). FATFS on the board resolves names case-insensitively
  (`README.md:494`).
* Name rules: non-empty, ≤128 bytes, no leading `.`, no `..` substring, no `/` or `\`, must be
  valid UTF-8 (`library.c:302-310,266-300`). Symlinks and `DT_UNKNOWN` entries are skipped
  unconditionally (`:477-478`).
* Supported extensions: `.mp3`, `.flac`, `.wav`, case-insensitive, decided by one authority
  (`components/portable/audio_format/audio_format.c:9-19`); everything else not a directory is
  ignored (`library.c:494-496`). No m4a/AAC/ogg/opus (spec out-of-scope,
  `docs/specs/sd-stream-server.md:36-41`).
* Rescan: see §3.6. A rescan while card-less keeps `no_card` (`app_services.c:76-77`,
  tested `http_tests.sh:478-487`). Card arrival after boot is handled by a 5 s retry task that
  mounts, then starts a rescan (`appliance.c:44-81,224-225`).

### 7.2 What the device does NOT do with metadata

* **No ID3/Vorbis/APIC in any network surface.** The library JSON, binary arena and every MCP
  tool carry only `name`, `path`, `type`, `size`, `format`, `track_index`
  (`library.c:622-645`; `mcp.c:494-528`).
* ID3/FLAC tags and cover art **are** parsed — for the screen only:
  `components/portable/artwork/` locates APIC (ID3v2.3/2.4) and FLAC PICTURE blocks
  (`artwork.h:1-60`), reads `TPE1`/`TPE2`/`TIT2` and `ARTIST`/`ALBUMARTIST`/`TITLE`
  (`tags.c:1-20`), and `components/app/screen_display/artwork_loader.c` decodes the JPEG with
  TJpgDec into a 603×385 RGB565 buffer on its own task (`artwork_loader.c:1-50`;
  `artwork_loader.h:1-20`). Caps: picture ≤1 MiB, edge ≤800; **JPEG only** — a PNG is located
  and refused (`artwork.h:31-33`; `README.md:1151-1153`).
* No search over tags, no artist/album browse, no sorting by anything but name, no
  "now playing" metadata in `/api/library`, no last-modified time (`s15-webdav.md:526-552`
  notes the tree cannot supply it).
* No persistence of anything: no NVS writes beyond `nvs_flash_init()` (`appliance.c:103-108`),
  no playlists, no favourites, no play counts, no resume position.

---

## 8. Audio pipeline as a service

| Stage | Implementation | Externally addressable? |
|---|---|---|
| Container/extension rule | `components/portable/audio_format/audio_format.c:9-19` | indirectly (what appears in `/api/library`, `/`, `/music`, MCP) |
| Decoder | `dr_mp3` / `dr_flac` / `dr_wav` vendored, `mi_decode.c:4-23`; mono/multichannel → stereo (`mi_decode.c:99-133`) | **no** |
| Resampler | windowed-sinc, `mi_rs_new(src, station)`; refuses extreme downsampling rather than overflowing (`mi_resample.c`, D4 fix; `mi_station.c:49`) | **no** (station rate fixed at build) |
| Encoder | vendored `shine`, CBR joint stereo, one frame per callback, re-framed and padded at EOF (`mi_encode.h:1-23`; `mi_encode.c`) | **no** (bitrate/rate compile-time only) |
| Tone EQ + trim | 3 bands (low shelf / mid bell / high shelf), −26..+6 dB, trim −12..+12 dB, 2 dB slew per chunk (`tone_eq.h:1-30`) | **no network control** — only the screen's knobs call `mini_icecast_eq_set` / `mini_icecast_gain_set` (`mini_icecast.c:1515-1527`) |
| Cassette/tape FX | `components/portable/cassette/` is a **display/geometry model** (reel phases, pack fill, rewind, timeline, entrance), not an audio effect (`cassette.h`, `timeline.c`, `entrance.c`; host tests `t10`, `t27`, `t28`) | **no** — visual only |
| Transport controls | `mini_icecast_skip/prev/play/shuffle_set` (`mini_icecast.c:1424-1513`) | **yes, partly**: `POST /api/play` and MCP `player_play`; **skip/prev only via MCP control builds** — there is no HTTP next/previous and the screen has no transport (`mcp.c:630-642`; `screen_input.c:14-15`; `README.md:256-258`) |
| Shuffle | broadcast mode, one permutation built at the pass top, seeded from `esp_random()` at boot (`mini_icecast.c:970-1077`; ADR `docs/adr/0023-shuffle-is-a-broadcast-mode.md`) | **no** network control; screen Menu only |
| Levels | per-chunk RMS into `mini_icecast_status_t.level_l/r` (`mini_icecast.c:606-618`) | **yes**, read-only via MCP `player_status` |
| Burst ring | `mi_burst` (`mi_burst.c`) | **no** |
| Per-client send rings + sender tasks | `mi_ring` (`mi_ring.c`), `mini_icecast.c:226-250` | **no** |
| Cards/park arbitration | `mini_icecast_park_card` / the `mini_files` download lock / `lag_ms` (`mini_icecast.c:620-662,1529`) | **no** |

There is exactly **one** audio sink today: the encoder → `/stream`. The decoder→PCM path has no
local output (no I2S, no DAC, no analog out); adding one is RESEARCHED-ONLY
(`docs/research/server/s15-i2s-dac-output.md:1-12`, whose seam is `emit_chunk()`).

The only audio state a remote caller can read is `player_status`
(`mini_icecast.c:1531-1557` → `mcp.c:742-757`), and the only audio state it can change is
*which track plays* (and, in control builds, next/previous). EQ, gain, rate, bitrate and
shuffle are **not** remotely addressable.

---

## 9. Host tier and what the tests prove

### 9.1 What the host build is

`host/Makefile` compiles the **same** `components/portable/*.c` files the firmware compiles,
against `host/include` + `host/shim` instead of ESP-IDF, plus `host/icecast_host.c` (entry) and
`host/board_host.c` (board adapter) — `host/Makefile:1-9,59-113`. No component is forked, and
`components/` contains no `#ifdef HOST` (`host/Makefile:8-9`).

Entry point flags: `--music-dir`, `--port` (default **18000**), `--out-hz`, `--bitrate`,
`--max-clients` (default 2), `--metaint`, `--lead-ms`, `--burst-ms`,
`--library-max-bytes`, `--library-max-tracks`, `--name` (`host/icecast_host.c:10-13,45-76`).

| Capability | Host | Board |
|---|---|---|
| Whole HTTP surface (`/`, `/stream`, `/music`, `/api/library[.bin]`, `/api/play`, `/api/rescan`, `/mcp`) | yes | yes |
| Bit-exact encoder output, frame alignment, format matrix | yes (shine is fixed-point and bit-identical) | yes |
| Library tree/JSON/binary, CRC, generations, leases | yes | yes |
| Card absence / no-dir / empty / truncated states | yes, via `XTAPE_HOST_NO_CARD` (`host/board_host.c:17-27`) | yes (real slot) |
| Controls/menu/knob/pad/cassette/RGB/groups models | yes (`t16`, `t18`–`t28`) | drawn only |
| Screen, touch, backlight, panel, LVGL, artwork JPEG decode, screen HTTP routes | **no** (`app/` is firmware-only; `host/Makefile:4-7`) | yes |
| Wi-Fi, mDNS, SPI card, FATFS semantics, PSRAM/external-memory paths | **no** | yes |
| Encoder CPU at 240 MHz, memory fit, SD contention, long soak | **no** (spec §10.2 lists exactly these four, `docs/specs/sd-stream-server.md:567-579`) | yes |

The host's `heap_caps_malloc()` ignores the capability mask entirely — every class maps to
plain `malloc()`, and the free-size figures are fixed constants (320 KB "internal", 8 MB
"PSRAM") — so the PSRAM-first fallbacks are *not* exercised on the host
(`host/shim/shim.c:92-116`). That is a real behavioural difference the host tests do not claim
to cover; on the board the same paths are visible in the startup log
(`README.md:476-479`).

### 9.2 Host test suites and what they pin

`make -C host test` runs these binaries (`host/Makefile:130-138,280-283`):

| Test | Pins | Acceptance ticket |
|---|---|---|
| `t01_shine_behaviour` | `shine_check_config(44100,128)==3`, `(8000,128)==-1`, 1152 samples/pass, 95 896 B state, 100 frames == 41 796 B, `shine_flush` emits 0 | T1 |
| `t02_encoder_pipeline` | whole frames only, byte-identity with an independent reference encoder over the same PCM, nothing dropped/padded at the 1024→1152 boundary or at EOF | T2, T2b |
| `t03_resampler_guard` | 48000→3000 and 44100→1000 return NULL instead of overflowing `row[256]` | T2c (D4) |
| `t04_format_validation` | MPEG-2.5 (return 0) is valid; bad rate/bitrate refused with a named value; `max_clients` refused, not coerced | T12 (F1/F2, D3) |
| `t05_stream_formats` | every allowed rate/bitrate pair emits frames whose header matches the bytes (padding-bit fault caught) | T2 per format |
| `t06_status` | the status snapshot a display needs (name, format, duration, elapsed, library count) | T6-adjacent |
| `t07_burst_icy` | burst ring wraparound/eviction against an oracle; ICY block framing and the length byte | T13 |
| `t08_http_parse` | request-line rules: absolute-form target, query, one-byte-over path, header case, longest-prefix routing | T14 |
| `t11_ring` | send-ring put/get wrap in both directions | T19 |
| `t12_transport` | next/prev/forward through the real broadcast and the file it reports | T12 |
| `t14_library` | tree walk, lookup, JSON, and the blob parsed by an independent reader | T14 |
| `t15_artwork` | ID3v2.3/2.4 APIC + FLAC PICTURE location, tag decoding, cover fit, malformed containers | T15 |
| `t16`,`t18`–`t28` | cassette grid, menu model, shuffle order, button rule, knob dialect, pad classifier, controls model, RGB effects, group rule, timeline arithmetic, entrance rule | UI tickets |

`make -C host integration` (`host/Makefile:285-288`) runs `host/tests/http_tests.sh` — the
spec's **T3, T4, T5, T6, T7, T8, T9, T12, T14, T15, T16, T17** (`http_tests.sh:1-12`) — plus
`host/tests/hostile_names.py` for awkward file names. `make -C host layering`
(`host/tests/layering.sh`) enforces S1–S4/H2/H3: the broadcast owns no socket/parser/markup,
the HTTP server knows no audio, only the application layer depends on `mini_files`, mDNS stays
in the board adapter.

`make -C host control-check` builds `XTAPE_MCP_CONTROL=1` and runs
`host/tests/control_tests.sh` → `mcp_control_tests.py` (`Makefile:144-149`).

Board-tier scripts (need the device): `board_library.sh` (ticket 13: boot scan, JSON/binary
agreement, nested download, MCP via `xtape.local`), `board_mcp_tests.py` (read-only smoke),
`board_soak.sh` (T9b/T11: encoder %, starvation rebases, memory stability, faults). Results
are recorded in `README.md:470-509` — e.g. 80/80 HTTP assertions on the board, 41.4 % of one
core over 59 minutes with 0 rebases, 0 faults.

Naming caveat: the **file** names (`t01…t28`) follow the implementation ticket sequence, while
each file's internal comment cites the spec's acceptance number, and those two numbering
schemes diverge (e.g. `host/tests/t07_burst_icy.c:2` says "T13", `t08_http_parse.c:2` says
"T14"). Grep the comment, not the filename, when mapping a spec ticket to a test.

### 9.3 What the host cannot prove

Stated by the spec (`docs/specs/sd-stream-server.md:567-579`) and unchanged: CPU headroom at 240 MHz,
memory fit with Wi-Fi/lwIP/decoder live, microSD contention and timing, and FATFS behaviour
(case sensitivity, LFN, AppleDouble filtering). T13 (case sensitivity) is explicitly
**board-only** because macOS is case-insensitive and would flatter the result
(`docs/specs/sd-stream-server.md:647-652`; `http_tests.sh:536-542`).

---

## 10. Licensing constraints

### 10.1 What is actually in the tree

| Component | Licence | Obligation for a seller | Source |
|---|---|---|---|
| Project's own firmware | **MIT** | keep the notice | `LICENSE:1-3` |
| `components/portable/shine/` | **LGPL-2.0-only** (`GNU Library General Public License, Version 2, June 1991`; the string "or later" appears zero times) | ship the licence text, ship shine's complete corresponding source, and preserve a **relink path** — i.e. keep it as a separate, unmodified, separately-built component | `THIRD_PARTY_NOTICES:9-29`; ADR `docs/adr/0002-vendor-shine-as-separate-lgpl-component.md:1-8`; `docs/research/server/s11-licensing-verified-and-packaging.md:14-40`; `components/portable/shine/COPYING` |
| `fknrdcls/mp3_shine_esp32` | **GPL-3.0** | **must not be used** — would impose GPL on the whole project | `docs/adr/0002-vendor-shine-as-separate-lgpl-component.md:18-22`; `THIRD_PARTY_NOTICES:26-28` |
| `dr_mp3/dr_flac/dr_wav` | public domain (Unlicense) **or** MIT-0, dual | none (attribution not even required) | `THIRD_PARTY_NOTICES:30-38`; `docs/research/server/s6-codec-licensing-matrix.md:29` |
| LVGL 9.6 | MIT (+ OFL fonts), TJpgDec permissive, LodePNG zlib | keep notices | `THIRD_PARTY_NOTICES:45-70` |
| `web/` browser analyser | **AGPL-3.0-only** | network-copyleft: distributing or operating it as a network service obliges source disclosure under AGPL | `web/LICENSE:1-3`; `web/package.json:6` |
| `web/` Essentia ONNX models | **CC BY-NC-SA 4.0** (MTG licensing page says CC BY-NC-ND 4.0; the two pages disagree) | **non-commercial only**, and MTG sells a proprietary licence | `web/NOTICE:1-26` |

The web app never talks to the device (`web/src/audio/models.ts:21` is its only `fetch`, for
its own `manifest.json`), so it is a separate distribution — but it is the one part of this
repository that cannot be sold as-is.

### 10.2 MP3 patents: expired, verified

`docs/research/server/s6-codec-licensing-matrix.md:53-83` is the citation to use:

* Fraunhofer IIS: "On April 23, 2017, Technicolor's mp3 licensing program … has been
  terminated", and "That day, the last of the core mp3 patents which were part of the
  licensing program by Fraunhofer and Technicolor, expired."
* Sisvel/Audio MPEG: "The Audio MPEG licensing programme is no longer active."
* Neither current pool operator lists an active MP3 programme; `mp3licensing.com` does not
  resolve and `audiompeg.com` is a parking page.
* Verdict recorded there: **"does MP3 encoding carry any remaining patent obligation? No — no
  programme exists and no vendor asserts one."** The honest caveat (Fraunhofer's own) is that
  implementation-specific patents might exist; no primary source identifies one.

The spec reaches the same conclusion for the shipping decision:
`docs/specs/sd-stream-server.md:490-512` ("shine is LGPL-2.0-only … §6 permits distributing a
work that uses the library under terms of your choice"), with requirements L1–L4: keep shine
separate and unmodified, ship `LICENSES/LGPL-2.0.txt` and the notice, never use the GPL-3.0
port, own code unaffected.

### 10.3 AAC and the formats that were rejected

* **AAC** — `docs/research/server/s1-aac-licensing.md:317-337` is the verdict to read: decode
  *or* encode puts an AAC pool obligation on whoever ships the product (quoted there:
  $0.98/unit for the first 500 k, or a $15 000 / $1 000 initial fee), and "no FOSS licence
  avoids it"; the recommendation is to keep the default build MP3/FLAC/WAV only. (s1 was
  written when the station emitted PCM in WAV — its own scope line says "It encodes nothing",
  `s1-aac-licensing.md:3-6` — so the *encode*-side conclusion to use is s6's, not s1's.)
  AAC is out of scope in the spec (`docs/specs/sd-stream-server.md:36-41`).
* **Opus/Vorbis/Ogg/WebM/Matroska** — patent positions verified in the matrix
  (`docs/research/server/s6-codec-licensing-matrix.md:85-160`); all were dropped for size/CPU reasons, not legal ones
  (`docs/research/server/s12-mp3-only-plan.md`), and the spec reinstates none of them.
* **FLAC/WAV** — free; only decode is used (`docs/research/server/s6-codec-licensing-matrix.md:29-36`; `THIRD_PARTY_NOTICES:30-38`).

### 10.4 Practical constraints for a seller

1. Publish shine's source and the LGPL text; do not statically hide it behind a blob. The
   existing arrangement (separate component, unmodified, built from source in the consumer's
   tree, linker fragment only for placement — `components/portable/shine/linker.lf`) is
   designed for exactly this (`docs/adr/0002-vendor-shine-as-separate-lgpl-component.md:1-22`, `THIRD_PARTY_NOTICES:19-24`).
2. Keep `THIRD_PARTY_NOTICES` and `LICENSES/` in the shipped source.
3. Do not add the GPL-3.0 shine port.
4. MP3 royalties: nothing to pay on the encode side (`docs/research/server/s6-codec-licensing-matrix.md:77-83`).
5. If `web/` is shipped or hosted, AGPL-3.0 applies to it, and the ONNX weights make the
   combined analyser non-commercial unless a licence is bought from MTG (`web/NOTICE:20-26`).

---

## 11. What is NOT there

Things a naive reader of "ESP32 music server with a JSON API and MCP" would assume exist.
Each row is a code-verified absence, not an opinion.

| Assumed | Reality | Evidence |
|---|---|---|
| Authentication / API keys / users | none, anywhere; no credential store, no NVS settings | `README.md:18-31`; `docs/specs/CONTEXT.md:417-421` |
| Per-user state, sessions, profiles | none; "clients are all equal and all hear the same audio" | `docs/specs/CONTEXT.md:402-415`; `mini_icecast.c:69-77` |
| TLS / HTTPS | absent | no mbedtls/esp_tls use in `components/`; `sdkconfig:1535` |
| Range requests / resumable download / seek | absent; always 200 + whole file | §3.3; `README.md:1142` |
| Chunked transfer encoding | absent; `/` and diagnostics use close-delimited bodies, everything else `Content-Length` | `http_server.c:105`; `mini_files.c:156,220` |
| Keep-alive / HTTP/1.1 | absent; `HTTP/1.0` + `Connection: close` always | `http_server.c:102-108` |
| WebSocket | absent (no upgrade path; the only "upgrade" is fd hand-off for `/stream`) | `http_server.h:58-66` |
| SSE / event stream | absent, including for MCP — no `text/event-stream` anywhere, and GET on `/mcp` is 405 | `mcp.c:1171-1181`; `docs/research/server/s13-library-tree-and-mcp.md:700-720` |
| File upload / write API | absent; no PUT/POST that writes, no WebDAV | §3.3 note above; `docs/research/server/s15-webdav.md:1-20` |
| WebDAV | **RESEARCHED-ONLY**: a 55 KB research doc with a verdict and ticket plan, no code, and the parser/route table currently block it | `docs/research/server/s15-webdav.md:1-20,57-113` |
| UPnP / DLNA / SSDP | absent | no code; only `_mcp._tcp` advertised (`board_esp32.c:149-154`) |
| AirPlay / Spotify Connect / Chromecast | absent | no code; only `_mcp._tcp` is advertised (`board_esp32.c:149-154`) |
| Bluetooth / A2DP / BLE | absent (`CONFIG_BT_ENABLED` not set) | `sdkconfig:1065` |
| Local I2S DAC output (a second sink for the broadcast PCM) | **RESEARCHED-ONLY**: the seam (`emit_chunk`) and the pin/clock budget are analysed, no code exists (no `i2s_*` call in `components/` or `main/`) | `docs/research/server/s15-i2s-dac-output.md:1-30` |
| Home Assistant / MQTT integration | absent (no MQTT client, no discovery topic) | no `mqtt` in `components/`, `main/` |
| OTA updates | absent; partition table has **no OTA slot** | `partitions.csv:1-7`; `README.md:1144` |
| Wi-Fi provisioning / AP mode / captive portal | absent; `WIFI_MODE_STA` only, credentials compiled in | `board_esp32.c:42-46,110-115`; `README.md:1144-1146` |
| NTP / time sync | absent; nothing in the firmware needs wall-clock time (all timing is `esp_timer`/`lv_tick`) | no `sntp`/`clock_gettime`-for-wall-clock use in `components/`; `esp_timer_get_time()` is the only clock (`screen_display.c:108`; `mini_icecast.c:580`) |
| Telnet / serial console / command channel | absent; serial is log output only | §6.2 |
| Metadata / ID3 in any API | absent; tags are display-only | §7.2 |
| Search over ID3/artist/album | absent; `library_search` matches the **relative path** case-insensitively | `mcp.c:691-723` |
| Artwork / cover over HTTP | absent; no `/api/cover`, no artwork field anywhere | `components/portable/artwork/include/artwork.h`; `library.c:622-645` |
| Playlists persisted, queue, play-next | absent; one linear Playlist = depth-first tree order, plus shuffle mode | `library.h:130-132`; `mini_icecast.c:970-1077` |
| Multi-room / grouping / zone sync | absent; "grouping" in `components/portable/groups/` is **UI groups** (which right-bar panel is shown), not audio zones | `components/portable/groups/groups.c`; `t26_groups.c:1-10` |
| Pause / stop / seek | absent; the stream is a broadcast. `play` exists (`POST /api/play`), and `skip`/`prev` exist in the API but are only reachable through MCP **control** builds — there is no HTTP next/previous and no on-screen transport | `mini_icecast.h:161-163`; `mcp.c:630-642`; `screen_input.c:14-15`; `README.md:1154-1155` |
| `/status`, `/metrics`, `/health` | absent; telemetry is a log line | §6.3 |
| HEAD | absent | §2.2 |
| gzip / compression | absent | — |
| Any endpoint listing directories over HTTP | absent; the HTML index is a flat track list, and only the JSON/MCP tree exposes directories | `mini_files.c:234-251` |
| IPv6 / dual stack | not exercised; the listener binds `AF_INET`/`INADDR_ANY` | `http_server.c:404,412-416` |
| A second port (admin, RTSP, ICY) | absent; one listener, one port | `http_server.c:404-424` |

Also worth stating: **`/api/play` and `/api/rescan` are state-changing endpoints with no auth
and no CSRF protection.** A web page on the LAN can issue them as `no-cors` POSTs; the browser
cannot read the response (no CORS headers), but the side effect happens. MCP is the only
surface that checks `Origin` (`mcp.c:940-963`) — an inconsistency a security-minded reader
should know about.

---

## 12. File:line index

Bare filenames used in the prose above (`mcp.c`, `library.c`, `mini_files.c`,
`mini_icecast.c`, `http_server.c`, `library_http.c`, `mi_*.c`, `board_esp32.c`,
`appliance.c`, `app_services.c`) refer to the component named in that section; the table below
gives the full path for the load-bearing ones.

### Server core
| What | Where |
|---|---|
| Limits (`HTTP_*`) | `components/portable/http_server/include/http_server.h:27-39` |
| Request struct | `http_server.h:41-56` |
| Route registration API | `http_server.h:76-92`; `http_server.c:153-179` |
| Response helpers, 512-byte header buffer | `http_server.c:97-111` |
| Error page | `http_server.c:113-125` |
| URL decode (no truncation) | `http_server.c:127-149` |
| Socket options/timeouts | `http_server.c:183-199` |
| Header read loop | `http_server.c:231-250` |
| HTTP request-line parse | `components/portable/http_server/http_parse.c:38-145` |
| Route match (longest prefix, method) | `http_parse.c:147-175` |
| POST body rules (411/413/415) | `http_server.c:292-341` |
| Worker pool + 503 | `http_server.c:365-397,435-445` |
| Accept loop | `http_server.c:401-446` |
| Start/defaults | `http_server.c:448-478` |
| Reason phrases | `http_server.c:46-62` |

### Routes
| What | Where |
|---|---|
| `/` and `/music/` registration | `components/portable/mini_files/mini_files.c:477-479` |
| Index renderer | `mini_files.c:142-255` |
| Path decoder (traversal guard) | `mini_files.c:278-308` |
| Download handler | `mini_files.c:310-432` |
| `/api/library[.bin]` | `components/portable/library_http/library_http.c:13-96` |
| `/stream` + `POST /api/play` registration | `components/portable/mini_icecast/mini_icecast.c:1400-1406` |
| Stream headers | `mini_icecast.c:464-472` |
| Client registration/burst | `mini_icecast.c:432-509` |
| Fan-out + dead-client drop | `mini_icecast.c:252-265` |
| `POST /api/play` | `mini_icecast.c:374-415` |
| `/api/rescan` | `components/portable/app_services/app_services.c:108-132,302` |
| Service composition order | `app_services.c:179-315` |
| `/mcp` registration (POST/GET/DELETE) | `components/portable/mcp/mcp.c:1196-1210` |
| MCP POST handler | `mcp.c:965-1169` |
| MCP Origin guard | `mcp.c:940-963` |
| MCP tool tables | `mcp.c:759-815` |
| MCP tool dispatch | `mcp.c:1130-1154` |
| MCP transport-error renderer | `mcp.c:1183-1194` |
| Screen routes | `components/app/screen_display/screen_frame.c:113-208` |
| Screen capture copy/lock | `components/app/screen_display/screen_display.c:569-637` |

### Library
| What | Where |
|---|---|
| Caps and API | `components/portable/library/include/library.h:27-140` |
| Blob/header/node layout + asserts | `components/portable/library/library.c:32-84` |
| Generation store / lease | `library.c:148-218` |
| Name rules / UTF-8 | `library.c:266-310` |
| Scan | `library.c:397-520` |
| Sort | `library.c:236-262,522-546` |
| JSON view | `library.c:558-723` |
| Arena build | `library.c:727-831` |
| Truncation / budget | `library.c:397-406,935-972` |
| Lookup (case fallback) | `library.c:1178-1222` |
| Views accessors | `library.c:1236-1246` |

### Broadcast
| What | Where |
|---|---|
| Station defaults/validation | `components/portable/mini_icecast/mi_station.c:14-54`; `mini_icecast.h:57-73` |
| Encoder contract | `components/portable/mini_icecast/include/mi_encode.h:1-99` |
| Pacing / rebase | `mini_icecast.c:574-596` |
| Chunker | `mini_icecast.c:687-703` |
| Idle bed / park | `mini_icecast.c:620-660,705-725` |
| Decoded-source task | `mini_icecast.c:761-924` |
| Playlist pass / shuffle | `mini_icecast.c:1079-1246` |
| Shipped station format | `components/app/appliance/appliance.c:112-157` |
| ICY splitter | `components/portable/mini_icecast/mi_icy.c:39-93` |
| Burst ring | `components/portable/mini_icecast/mi_burst.c:1-138` |
| Status snapshot | `mini_icecast.c:1531-1557` |

### Board / discovery / diagnostics
| What | Where |
|---|---|
| Wi-Fi STA | `components/app/board_esp32/board_esp32.c:73-118` |
| SPI card mount | `board_esp32.c:166-244` |
| Hardware census | `board_esp32.c:246-267` |
| mDNS | `board_esp32.c:137-162` |
| Appliance order + retry task | `appliance.c:44-81,101-246` |
| Screen HTTP opt-in | `components/app/screen_display/Kconfig:29-42`; `sdkconfig.defaults.capture:1-16` |
| Serial dump opt-in | `components/app/screen_display/Kconfig:3-20` |

### Tests and evidence
| What | Where |
|---|---|
| HTTP acceptance (T3–T17) | `host/tests/http_tests.sh:1-12` and groups at `:136,161,175,190,201,414,449,460,491,508,570,609` |
| MCP read-only | `host/tests/mcp_tests.py:75-210` |
| MCP control | `host/tests/mcp_control_tests.py:89-162` |
| Rescan/generations | `host/tests/rescan_tests.py:89-153` |
| Binary oracle | `host/tests/check_library_bin.py:1-12,45-106` |
| JSON oracle | `host/tests/check_library.py:81-144` |
| Index oracle | `host/tests/check_index.py` |
| ICY framing | `host/tests/check_metadata.py` |
| Layering rules | `host/tests/layering.sh:19-66` |
| Board library / MCP / soak | `host/tests/board_library.sh:1-14`; `board_mcp_tests.py:1-4`; `board_soak.sh:1-22` |

### Specs, ADRs, research
| What | Where |
|---|---|
| Route table + requirements | `docs/specs/sd-stream-server.md:343-470` |
| Access model | `docs/specs/sd-stream-server.md:470-489` |
| Licence obligation | `docs/specs/sd-stream-server.md:490-512` |
| Acceptance tests T1–T18 | `docs/specs/sd-stream-server.md:514-546` |
| Host limits (what it can't prove) | `docs/specs/sd-stream-server.md:553-579` |
| MCP choice + client landscape | `docs/research/server/s13-library-tree-and-mcp.md:656-790,903-948` |
| Codec/patent matrix | `docs/research/server/s6-codec-licensing-matrix.md:27-160` |
| Licence verification | `docs/research/server/s11-licensing-verified-and-packaging.md:1-60` |
| MP3-only plan | `docs/research/server/s12-mp3-only-plan.md` |
| WebDAV (research only) | `docs/research/server/s15-webdav.md:1-20,57-113,526-552` |
| Transport extraction | `docs/adr/0003-extract-http-server-from-mini-icecast.md` |
| Original bytes, not transcode | `docs/adr/0001-serve-original-bytes-not-transcoded.md` |
| shine as LGPL component | `docs/adr/0002-vendor-shine-as-separate-lgpl-component.md` |
| Boot-built arena / generations | `docs/adr/0012-boot-built-library-arena.md` |
| Glossary | `docs/specs/CONTEXT.md:9-40,392-430` |
| Deployment warning + route table | `README.md:18-31,232-245` |
| Known limitations | `README.md:1137-1165` |
| Board-tier evidence | `README.md:470-509` |

### Doc/code disagreements found while writing this

| Claim | Doc says | Implementation says |
|---|---|---|
| Card bus | spec says "1-bit SDMMC at `/sdcard`" (`docs/specs/sd-stream-server.md:28`, R2) | implemented over **SPI**, with the SDMMC failure explained (`board_esp32.c:48-60,166-209`) |
| `POST /api/scene?n=` range | Kconfig says `0..8` (`components/app/screen_display/Kconfig:38`) | `0..9` (`components/app/screen_display/cassette_view.h:52`; `screen_frame.c:167`) |
| `max_clients` | spec §5 D3 describes silent coercion to 2 | code refuses out-of-range and defaults 0→2 (`mi_station.c:31,39-46`) and the tests pin it (`http_tests.sh:621-622`) |
| Per-request task | spec S1 requires one short-lived task per connection (`docs/specs/sd-stream-server.md:157-170`) | permanent worker pool, with the measured reasons (`http_server.c:347-380`; `README.md:946-955`) |
| ESP-IDF version | `README.md:50` says 6.1; `README.md:1163` still says "builds against ESP-IDF v5.4.4" (historical; board tier ran 6.1, `README.md:471`) | `main/idf_component.yml:7` allows `>=5.4` |
| MCP revision | spec M1 targets 2025-11-25 only | code accepts and echoes three legacy revisions (`mcp.c:26-28,934-938`) |
