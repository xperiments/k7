# Internet-radio player: network / transport layer (ESP32-S3, ESP-IDF component)

Axis of this note: **everything between `esp_wifi` and the audio decoder** for an ESP-IDF
component that plays internet radio streams for days at a time.

Conventions used below:

- **VERIFIED** = I read it in the source/document that owns the behaviour. Where the source is
  ESP-IDF, I read the actual file in a local checkout of `espressif/esp-idf` **v5.5.5**
  (`~/.espressif/v5.5.5/esp-idf`), whose `version.txt` says `5.5.5`, and I also linked the
  canonical GitHub permalink at tag `v5.5.5`.
- **TESTED** = VERIFIED *and* reproduced locally by compiling
  `components/http_parser/http_parser.{c,h}` out of that tree with a small driver and running it
  (md5 of the compiled file matched the checkout: `67c928f91e34e2e4084836ba205b17e2`).
- **OBSERVED** = captured from a live public stream/site on this machine (headers/bytes shown).
- **LIKELY / INFERENCE** = my reasoning, not sourced.

Local checkout version check: `~/.espressif/v5.5.5/esp-idf/version.txt` → `5.5.5`. Other trees on
this machine: `v5.4.2`, `v6.1` (both present but not used here).

Sibling notes in this directory that this one deliberately does not duplicate:
[`06-who-sends-icy-status-line.md`](06-who-sends-icy-status-line.md) (Icecast-side proof of who emits
`ICY 200 OK`) and [`07-esp32s3-hardware-verified.md`](07-esp32s3-hardware-verified.md) (memory map,
I2S, SoC capabilities).

---

## 0. Scope note on `resources/`

`resources/` is the *opposite* side of the wire: a tiny Icecast-style **server**
(SD card → HTTP WAV stream, `mini_icecast.c`/`.h`). It is not a player and shares no code with one.
What is worth stealing from it for the player component:

- `send_meta()` in `resources/mini_icecast.c` is a correct reference implementation of the
  metadata *writer*: `int blocks = (len + 15) / 16; uint8_t buf[1 + 16*10] = {blocks, "StreamTitle='...';"}`
  — i.e. it confirms the "length byte counts 16-byte blocks" rule and the `StreamTitle='...';`
  payload shape. Useful as a **loopback test fixture** for the player: point the player at this
  server and assert the parser recovers the titles.
- `handle_new()` sets `SO_RCVTIMEO`/`SO_SNDTIMEO` and `TCP_NODELAY` — the same lwIP socket knobs the
  player side needs (§5).
- It answers `icy-metadata: 1` with `icy-metaint: 16000` on an `HTTP/1.0 200 OK` line
  (`mini_icecast.c:224-227`), i.e. it speaks the same dialect a real Icecast does.
  **Caveat for fixture use (verified by reading both):** `resources/` writes a
  **space after the colon** (`"icy-name: %s\r\n"`, `mini_icecast.c:225`), while real Icecast writes
  none (`"icy-name:%s\r\n"`, `format.c:421`). A header parser that requires the space will pass
  against the fixture and fail against the internet, so test both spellings.

Nothing else there is reusable for a player.

---

## 1. The SHOUTcast/ICY dialect, as a client

### 1.1 There is no spec

ICY/SHOUTcast was never standardised. It is the *de-facto* wire format of Nullsoft SHOUTcast,
implemented independently by Icecast ("shoutcast compat"), by clients (FFmpeg, VLC), and by a long
tail of stream hosts. **VERIFIED**: I could find no normative
specification — no RFC, no IETF draft, no vendor document. What exists instead is (a) server
implementations, (b) client parsers, and (c) HTTP's own `Icy-MetaData` request header. Treat every
rule below as "what the implementations do", and make the parser permissive.

### 1.2 Status line: `ICY 200 OK` vs `HTTP/1.0 200 OK`

- **OBSERVED**: modern Icecast answers with HTTP, not ICY. `http://icecast.omroep.nl/radio1-bb-mp3`:

  ```
  HTTP/1.0 200 OK
  Connection: Close
  Content-Type: audio/mpeg
  icy-br: 192
  icy-genre: Talk
  icy-metadata: 1
  icy-name: NPO Radio1
  icy-pub: 0
  icy-url: http://www.radio1.nl
  icy-metaint: 16000
  ```

  `http://stream.radioparadise.com/mp3-128` answers `HTTP/1.1 200 OK` with the same `icy-*` set.
- **VERIFIED**: Icecast does not emit `ICY 200 OK` for listeners at all. The string `"ICY"` only
  appears as `httpp_setvar(client->parser, HTTPP_VAR_PROTOCOL, "ICY")` for the
  **shoutcast admin.cgi source-login path**
  ([`src/connection_handle.c:188`](https://github.com/xiph/Icecast-Server/blob/master/src/connection_handle.c#L188)),
  and even the error path hard-codes HTTP:
  `"HTTP/1.0 500 Internal Server Error\r\n..."`
  ([`src/client.c:536`](https://github.com/xiph/Icecast-Server/blob/master/src/client.c#L536)).
- **LIKELY**: `ICY 200 OK` still occurs from older/other SHOUTcast-lineage servers
  (the v1/v2 `sc_serv` DNAS family) and some shoutcast-compatible appliances. I could not reach one
  from this machine to capture it, so treat "ICY appears in the wild" as *inferred from the fact
  that multiple independent client parsers explicitly implement it* (below), not as observed here.

**Decision-relevant consequence: you must accept both.** That is cheap, and one of the two
reference HTTP parsers on your shortlist already does (§1.2 below); note that "accept both" is a
requirement on *your* parser if you ever bypass `esp_http_client`, because FFmpeg — the other
reference — does not.

- **TESTED (ESP-IDF `http_parser` accepts `ICY 200 OK`)**: compiled
  `components/http_parser/http_parser.c` from v5.5.5 (`HTTP_PARSER_STRICT == 1`, see
  [`http_parser.h:51-54`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/http_parser/http_parser.h#L51-L54))
  and fed it `ICY 200 OK\r\nicy-name:Test\r\nicy-metaint:16000\r\n\r\n`. Result:
  `status=200`, `http_major=0`, `http_minor=0`, reason `OK`, headers parsed, `HPE_OK`.
  The state machine has an explicit ICY branch:
  `case s_res_IC: STRICT_CHECK(ch != 'Y'); UPDATE_STATE(s_res_http_minor);`
  ([`http_parser.c:801-804`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/http_parser/http_parser.c#L801-L804)),
  entered from `s_start_res` on `'I'`
  ([`http_parser.c:781`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/http_parser/http_parser.c#L781)).
  Read it carefully: `s_res_IC` jumps to `s_res_http_minor`, *not*
  `s_res_first_http_minor` as the `HTTP/` path does, so after `ICY` a space is accepted where a
  digit would normally be required, the next token (`200`) is consumed as
  `s_res_first_status_code`/`s_res_status_code`, and `http_minor` is left at `0`. That is exactly
  why `ICY 200 OK` yields `status_code == 200`. There is no `ICY/` version string on the wire and
  the parser does not require one.
  **Important side effect:** since `parser->http_major == 0 && http_minor == 0`,
  `http_should_keep_alive()` treats it as HTTP/1.0-or-earlier and returns 0 unless
  `Connection: keep-alive` is present
  ([`http_parser.c:2120-2157`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/http_parser/http_parser.c#L2120-L2157)).
  For a radio stream that is *desired* — you never want the connection reused for a second request.
- **VERIFIED (FFmpeg does *not* accept it today)**: FFmpeg's response-line parser is strict —
  `if (!av_strstart(line, "HTTP/", &p) || ...) return AVERROR_INVALIDDATA;`
  ([`libavformat/http.c:938-955`](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/http.c#L938-L955)).
  FFmpeg's ICY support is entirely about the `Icy-MetaData: 1` request header, the `Icy-*` response
  headers, and the in-body metadata ([`libavformat/http.c:1458-1461`](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/http.c#L1458-L1461),
  [`:1811-1812`](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/http.c#L1811-L1812)),
  *not* about the ICY status line. **LIKELY:** this is deliberate — the maintainers concluded that
  every station worth reaching also answers over HTTP — and it is a useful signal that
  `ICY 200 OK` support is a compatibility nicety, not a load-bearing requirement.

### 1.3 Response headers

All of these are optional; a player must tolerate any subset, any order, and any case.

| Header | Meaning | Where it comes from |
|---|---|---|
| `icy-name` | station / stream name | Icecast renames source `ice-name` → `icy-name` ([`format.c:409-424`](https://github.com/xiph/Icecast-Server/blob/master/src/format.c#L409-L424)) |
| `icy-genre` | genre | generic `icy-*` passthrough ([`format.c:427-441`](https://github.com/xiph/Icecast-Server/blob/master/src/format.c#L427-L441)) |
| `icy-br` | nominal bitrate in kbit/s | derived from source `ice-audio-info`'s `bitrate=` field ([`format.c:389-399`](https://github.com/xiph/Icecast-Server/blob/master/src/format.c#L389-L399)) |
| `icy-url` | station homepage (advisory) | generic `icy-*` passthrough |
| `icy-pub` | directory-listing wish (0/1) | source `ice-public` → `icy-pub` ([`format.c:427-435`](https://github.com/xiph/Icecast-Server/blob/master/src/format.c#L427-L435)) |
| `icy-metaint` | **audio bytes between metadata blocks**; only sent if metadata was requested | [`format_mp3.c:739-752`](https://github.com/xiph/Icecast-Server/blob/master/src/format_mp3.c#L739-L752) |
| `icy-metadata: 1` | server echoing that it will send metadata | **OBSERVED** on omroep.nl; not emitted by all servers |
| `ice-audio-info` | `channels=2;samplerate=44100;bitrate=128` — the real codec parameters | **OBSERVED** from radioparadise.com (`channels=2;samplerate=44100;bitrate=128`) and, on an earlier connection, omroep.nl (`samplerate=48000;channels=2;bitrate=192`); Icecast passes the source's `ice-audio-info` through ([`format.c:389-405`](https://github.com/xiph/Icecast-Server/blob/master/src/format.c#L389-L405)) |
| `icy-description` | free-text description | **OBSERVED** on radioparadise.com (generic `icy-*` passthrough) |
| `Content-Type` | `audio/mpeg`, `audio/aacp`, `audio/ogg`… | **OBSERVED**; do not trust it for codec selection alone |

Notes:

- **VERIFIED**: Icecast writes these header names in **lowercase** (`"icy-name:%s\r\n"`,
  `"icy-metaint:%u\r\n"`), with `\r\n`, and **no space after the colon**. Do not assume a space,
  and match case-insensitively.
- **VERIFIED**: `icy-metaint` is *conditional*. In Icecast's mp3 client setup:
  `metadata = httpp_getvar(client->parser, "icy-metadata"); if (metadata && atoi(metadata)) { ... snprintf(..., "icy-metaint:%u\r\n", client_mp3->interval); }`
  ([`format_mp3.c:739-752`](https://github.com/xiph/Icecast-Server/blob/master/src/format_mp3.c#L739-L752)).
  No `Icy-MetaData: 1` in the request ⇒ **no `icy-metaint` and no metadata bytes at all**.
  This is the single most common reason a hand-rolled client sees garbage: it assumed metadata and
  started consuming length bytes at 16000-byte intervals that the server never inserted.
- **LIKELY**: `ice-audio-info` is the only trustworthy source of sample rate/channel count before
  the first decoder frame; treat it as optional and fall back to probing the bitstream.

### 1.4 The request header that opts in

- Send, verbatim (this is what FFmpeg sends when its `icy` option is on, default on):
  `Icy-MetaData: 1\r\n` — **VERIFIED** at
  [`libavformat/http.c:1811-1812`](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/http.c#L1811-L1812)
  (`if (!has_header(s->headers, "\r\nIcy-MetaData: ") && s->icy) av_bprintf(&request, "Icy-MetaData: 1\r\n");`).
- **VERIFIED**: Icecast looks up the request variable as `"icy-metadata"` — the libhttpp variable
  lookup is case-insensitive (`httpp_getvar`), so `Icy-MetaData: 1` works
  ([`format_mp3.c:739`](https://github.com/xiph/Icecast-Server/blob/master/src/format_mp3.c#L739)).
  A `1`-valued lookup is required (`atoi(metadata)` must be non-zero), so **do not send
  `Icy-MetaData: 0` expecting metadata.**
- **LIKELY**: some servers also honour `icy-metadata: 1` (lowercase) and, anecdotally, the legacy
  `icy-metaint` *request* header (Icecast reads `icy-metaint` from the **source** client's request
  to set its own interval: [`format_mp3.c:117-127`](https://github.com/xiph/Icecast-Server/blob/master/src/format_mp3.c#L117-L127)) —
  not from the listener. Do not send it as a listener.
- **LIKELY**: existing `icy-metaint` in the request path can also be honoured by some relays.
  Harmless to omit.

Also send a sane `User-Agent` and `Accept: */*`. All stations I probed replied fine without them,
but Icecast has at least one historical user-agent special case for the Flash player
(`Content-Length: 221183499` hack for `X-Flash-Version`/`MSIE`),
[`format_mp3.c:726-737`](https://github.com/xiph/Icecast-Server/blob/master/src/format_mp3.c#L726-L737);
not relevant, but proof that UA sniffing happens in this ecosystem.

---

## 2. ICY metadata framing inside the audio body

### 2.1 The rule

After every `icy-metaint` bytes of audio:

```
+--------+-----------------------------------+
| 1 byte |  N * 16 bytes (N = length byte)   |
|  N     |  padded with 0x00                 |
+--------+-----------------------------------+
```

- **VERIFIED (writer)**: Icecast computes
  `len_byte = (len-1) / 16 + 1;` then writes `%c%s';', len_byte, "StreamTitle='"` — i.e. the length
  byte precedes the payload and counts 16-byte blocks ([`format_mp3.c:304-335`](https://github.com/xiph/Icecast-Server/blob/master/src/format_mp3.c#L304-L335)).
  Note `(len-1)/16 + 1 == ceil(len/16)`: **the semicolon is included in the count.**
- **VERIFIED (reader)**: FFmpeg implements exactly the same, including the 255-block ceiling and a
  `char data[255*16 + 1]` buffer:
  `len = ch * 16; http_read_stream_all(h, data, len); data[len] = 0;`
  ([`libavformat/http.c:2141-2181`](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/http.c#L2141-L2181)).
- **VERIFIED**: Icecast caps a metadata block at `#define MAX_META_LEN 255*16`; a longer title is
  dropped and the old one is kept ([`format_mp3.c:304-313`](https://github.com/xiph/Icecast-Server/blob/master/src/format_mp3.c#L304-L313)).
  So the length byte is always 0..255, and 4080 bytes is the hard payload ceiling.
- **OBSERVED** (raw bytes, `http://icecast.omroep.nl/radio1-bb-mp3`, `icy-metaint: 16000`):
  at file offset 16000 the first byte is `0x03` (= 3 blocks = 48 bytes), followed by
  `StreamTitle='NPO Radio 1 - EenVandaag - AVROTROS';` then `0x00` padding to the full 48 bytes, and
  the next MP3 frame sync `ff fb` sits at byte offset 16000+1+48 = 16049 (hex 0x3EB9). Byte-exact confirmation of the layout.

### 2.2 Edge cases the parser must implement

1. **Length byte == 0 ⇒ no payload, title unchanged.** **VERIFIED** in both implementations:
   FFmpeg `if (ch > 0) { ... } s->icy_data_read = 0;`
   ([`http.c:2157-2177`](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/http.c#L2157-L2177));
   Icecast writes exactly one `"\0"` when the title has not changed
   ([`format_mp3.c:374-397`](https://github.com/xiph/Icecast-Server/blob/master/src/format_mp3.c#L374-L397)).
   *Never* treat it as "empty title"; it means *keep the previous title*.
2. **The first metadata block arrives immediately after the headers, before any audio.**
   **VERIFIED**: Icecast's initial state is `"\001StreamTitle='';"` (17 bytes: length byte 1, empty
   title) and it is sent as the first thing on a change
   ([`format_mp3.c:110-114`](https://github.com/xiph/Icecast-Server/blob/master/src/format_mp3.c#L110-L114),
   [`:380-393`](https://github.com/xiph/Icecast-Server/blob/master/src/format_mp3.c#L380-L393)).
   **LIKELY/INFERENCE:** therefore "wait `metaint` bytes, then read the length byte" is wrong for
   the first block; a robust reader should read the length byte when its audio byte-counter reaches
   `metaint` **or** when the server has flagged that it is mid-metadata, and should not be surprised
   by a metadata block in the header-read buffer.
3. **A metadata block can straddle TCP segments / your read buffer.** **LIKELY/INFERENCE**, but
   unavoidable: the 1+N*16 block has no alignment relationship to TCP. Your reader must be a byte
   state machine (accumulate audio into the ring; when `audio_since_meta == metaint`, switch to a
   "read 1 byte; if 0 → back to audio; else read exactly `n*16` bytes" sub-state) rather than
   assuming a metadata block arrives within one `recv()`.
4. **The title is inside single quotes and there is no escaping.** **VERIFIED**: Icecast writes
   `StreamTitle='%s'` with the raw, charset-converted string
   ([`format_mp3.c:329-336`](https://github.com/xiph/Icecast-Server/blob/master/src/format_mp3.c#L329-L336)).
   There is **no `\'` escape** in the writer. **VERIFIED** that FFmpeg's own ICY *title* parser
   (`ff_http_parse_icy_packet`) is not defined in `libavformat/http.c` — that file only calls it
   ([`:2170`](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/http.c#L2170)) — while
   `store_icy` itself hands the raw NUL-terminated block upward. **LIKELY/INFERENCE:** therefore a title containing `'` will
   make a naive `find "'"` parser truncate. Best practice: parse up to the *last* `';` in the block,
   or scan for `StreamTitle='` and then the next `';` — and accept both since neither is authoritative.
   Icecast's *reader* side is similarly naive: `filter_shoutcast_metadata` matches
   `strncmp(metadata, "StreamTitle='", 13)` then `strstr(metadata+13, "';")`
   ([`format_mp3.c:182-204`](https://github.com/xiph/Icecast-Server/blob/master/src/format_mp3.c#L182-L204)).
   That is the de-facto rule: **find `';` after the opening quote.**
5. **Trailing semicolon is part of the count and always present** in what Icecast emits.
   **LIKELY:** a strict parser should not *require* it (some encoders omit it), but should accept
   payloads that are zero-padded after it. Strip trailing `\0` and whitespace.
6. **`StreamUrl` is a real second field.** **VERIFIED**: Icecast appends
   `StreamUrl='...';` after the title when the source provided a URL
   ([`format_mp3.c:259-260`](https://github.com/xiph/Icecast-Server/blob/master/src/format_mp3.c#L259-L260),
   [`:340-349`](https://github.com/xiph/Icecast-Server/blob/master/src/format_mp3.c#L340-L349)).
   So the payload is a *list* of `Key='value';` pairs; parse field-by-field, not with one `sscanf`.
   Expect at least `StreamTitle`; ignore unknown keys.
7. **Character encoding: ISO-8859-1 is what Icecast actually produces by default.**
   **VERIFIED**: `if (format->charset == NULL) { ... format->charset = strdup("ISO8859-1"); }`
   ([`format_mp3.c:244-247`](https://github.com/xiph/Icecast-Server/blob/master/src/format_mp3.c#L244-L247)),
   and the title is *converted from UTF-8 into that charset* before being written
   (`util_conv_string(in_url_title, "UTF-8", charset)`, [`:284-288`](https://github.com/xiph/Icecast-Server/blob/master/src/format_mp3.c#L284-L288)).
   A per-mount `<charset>` can set something else (commonly UTF-8). **LIKELY/INFERENCE:** in
   practice you will see both ISO-8859-1 and UTF-8 in the wild, and servers rarely announce which.
   Recommendation: default to UTF-8 for display (it degrades gracefully), optionally keep a heuristic
   (if the bytes are not valid UTF-8, treat as Latin-1). Do not hard-fail on invalid UTF-8.
8. **Multiple title changes inside one `metaint` window collapse.** **VERIFIED**: the metadata
   buffer is replaced wholesale on each change and re-sent at the next interval
   ([`format_mp3.c:347-354`](https://github.com/xiph/Icecast-Server/blob/master/src/format_mp3.c#L347-L354));
   only the latest survives. **LIKELY:** a `StreamTitle` you never see is normal, not a bug.
9. **`metaint` changes on re-read/reconnect.** **LIKELY/INFERENCE** (no source found that changes it
   mid-stream): Icecast computes the interval per client at connect time
   ([`format_mp3.c:224-250`](https://github.com/xiph/Icecast-Server/blob/master/src/format_mp3.c#L224-L250)),
   so a **new** connection can have a different interval (per-mount `icy-metadata-interval`, or a
   relay's value). The mount option is documented:
   "This optional setting specifies what interval, in bytes, between ICY metadata updates for streams
   using ICY metadata … The default is either the hardcoded server default or the value passed from a
   relay." ([Icecast docs, `icy-metadata-interval`](https://github.com/xiph/Icecast-Server/blob/master/doc/config_file/index.html)).
   Hardcoded default `#define ICY_METADATA_INTERVAL 16000`
   ([`format_mp3.c:57`](https://github.com/xiph/Icecast-Server/blob/master/src/format_mp3.c#L57));
   16000 is what every station I probed sent. **Design rule: re-read `icy-metaint` on every
   response, and reset the audio byte counter on every reconnect.**
10. **Metadata may appear only after a redirect/re-read.** **LIKELY/INFERENCE:** if the first response
    is `302` (or a playlist), the *final* response carries `icy-metaint`. Because you must send
    `Icy-MetaData: 1` on **every** request — including redirected ones — the parser must be per-response,
    not per-session. Note this is a real hazard with `esp_http_client`'s auto-redirect: the
    `Icy-MetaData` request header is a client-wide setting and *is* reused across redirects
    (**VERIFIED**: `esp_http_client_set_redirection()` only swaps the URL,
    [`esp_http_client.c:1071-1099`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/esp_http_client.c#L1071-L1099)),
    but the cached `icy-metaint` you captured from an intermediate response is **not** reset for you.
11. **No `icy-metaint` at all** ⇒ the body is pure audio; read to EOF. **VERIFIED** that this is
    exactly what Icecast does when the client did not opt in (§1.3). Also send `Icy-MetaData: 1`
    anyway: servers that don't understand it ignore it.

---

## 3. ESP-IDF: what can actually do this

### 3.1 `esp_http_client` — the surprising parts

Sources: [`components/esp_http_client/include/esp_http_client.h`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/include/esp_http_client.h),
[`components/esp_http_client/esp_http_client.c`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/esp_http_client.c) (v5.5.5).

**VERIFIED — it does parse ICY status lines and it does surface `Icy-*` headers**, but:

- **The status line is not exposed as text.** `http_on_status()` is an empty stub
  ([`esp_http_client.c:237-240`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/esp_http_client.c#L237-L240));
  only the numeric code survives (`esp_http_client_get_status_code()`,
  [`esp_http_client.h:688`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/include/esp_http_client.h#L688)).
  You therefore **cannot distinguish `ICY 200 OK` from `HTTP/1.0 200 OK`** through this API.
  If the component wants to report the dialect (e.g. for diagnostics), it must sniff raw bytes itself.
- **Response headers are only available through the `HTTP_EVENT_ON_HEADER` callback, and are freed
  immediately after it returns.** `http_on_header_event()` sets
  `event.header_key/header_value` to `client->current_header_key/value` and then `free()`s both
  ([`esp_http_client.c:242-256`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/esp_http_client.c#L242-L256)).
  **You must copy anything you want to keep.**
- **`esp_http_client_get_header()` reads the *request* headers, not the response.**
  **VERIFIED**: it calls `http_header_get(client->request->headers, ...)`
  ([`esp_http_client.c:370-377`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/esp_http_client.c#L370-L377)),
  and `http_header_get` does a `strcasecmp` over that dictionary
  ([`lib/http_header.c:49-75`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/lib/http_header.c#L49-L75)).
  So `esp_http_client_get_header(c, "icy-metaint", &v)` after `fetch_headers()` will **not** give you
  the server's value. Capture it in the event handler. (The header *name* comparison is
  case-insensitive, so `esp_http_client_set_header(c, "Icy-MetaData", "1")` is fine.)
- **`icy-metaint` is nowhere in the client.** `grep -rn "Icy-MetaData\|metaint\|StreamTitle"`
  over the whole v5.5.5 `components/` tree finds nothing in `esp_http_client` — the only ICY-aware
  code is the `http_parser` state machine (§1.2). The metadata framing is entirely the
  application's job.
- **Endless body (no `Content-Length`, no `Transfer-Encoding`): supported, but for the wrong reason.**
  **TESTED**: `http_message_needs_eof()` returns 1 for a 200 with neither header, so the parser sets
  `UPDATE_STATE(s_body_identity_eof)` and streams body bytes forever
  ([`http_parser.c:2120-2136`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/http_parser/http_parser.c#L2120-L2136),
  [`:1892-1898`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/http_parser/http_parser.c#L1892-L1898)).
  But `esp_http_client_fetch_headers()` conflates two things:
  ```
  if (client->response->content_length <= 0) {
      client->response->is_chunked = true;
      return 0;
  }
  ```
  ([`esp_http_client.c:1585-1589`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/esp_http_client.c#L1585-L1589)).
  **`is_chunked == true` here means "length unknown", not "chunked transfer-encoding".** The header's
  own doc admits the ambiguity: `fetch_headers()` returns "(0) if stream doesn't contain content-length
  header, or chunked encoding" and points at `esp_http_client_is_chunked_response()`
  ([`esp_http_client.h:645-653`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/include/esp_http_client.h#L645-L653)),
  but that predicate just returns the same conflated flag
  ([`esp_http_client.c:1912-1919`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/esp_http_client.c#L1912-L1919)).
  **Consequence (LIKELY, and important):** in blocking `esp_http_client_perform()`, the read loop is
  `while (client->response->is_chunked && !client->is_chunk_complete)`
  ([`esp_http_client.c:1487`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/esp_http_client.c#L1487)),
  which for a *genuinely* chunked, endless ICY stream is exactly what you want (it never exits), and
  for a non-chunked endless stream also happens to iterate forever because `is_chunked` was forced
  true and `is_chunk_complete` never fires. So the callback style does work for endless radio.
  But the two cases are indistinguishable, and on a *finite* non-chunked response the same flag makes
  the loop run to socket EOF/timeout instead of stopping at `Content-Length`. Prefer the explicit
  streaming API (§3.2) and treat the flag as a heuristic.
- **Chunked body is decoded for you, and a bogus chunk is a hard error.**
  **TESTED** with the same driver: `Transfer-Encoding: chunked` + `5\r\nhello\r\n0\r\n\r\n` →
  `chunk_header len=5`, `body 5 bytes`, `chunk_header len=0`, `msg_complete`, `HPE_OK`.
  `ZZ\r\n` as the first chunk → `HPE_INVALID_CHUNK_SIZE`, `http_parser_execute()` stops early
  ([`http_parser.c:1958-1995`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/http_parser/http_parser.c#L1958-L1995)).
  **VERIFIED** that `esp_http_client_read()` treats a `FIN` while `is_chunked` as a *clean* end
  (`ERR_TCP_TRANSPORT_CONNECTION_CLOSED_BY_FIN` → calls the parser with 0 length to fire
  `message_complete`, and logs at DEBUG), but returns `ESP_FAIL`/`-ESP_ERR_HTTP_EAGAIN` otherwise
  ([`esp_http_client.c:1361-1390`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/esp_http_client.c#L1361-L1390)).
  **LIKELY/INFERENCE:** "server closed mid-chunk" is *the normal end of an internet radio stream*
  (Icecast sends `Connection: Close`, **OBSERVED**), so your read loop must treat
  `FIN`, `ESP_ERR_HTTP_CONNECTION_CLOSED` and `-ESP_ERR_HTTP_EAGAIN` on a stream that produced audio
  as **"reconnect"**, not as "fatal error".
- **Data path overhead.** Every body byte goes through the parser and then through
  `http_on_body()`, which `realloc()`s a cache buffer when it is in "fetch headers" state and
  otherwise copies into the caller's buffer; `HTTP_EVENT_ON_DATA` is dispatched per parser call
  ([`esp_http_client.c:305-337`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/esp_http_client.c#L305-L337)).
  **VERIFIED**: `client->cache_data_in_fetch_hdr` is enabled by default at init
  ([`:992`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/esp_http_client.c#L992))
  and disabled for the `perform()` path ([`:1456`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/esp_http_client.c#L1456)),
  re-enabled after headers ([`:1478-1480`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/esp_http_client.c#L1478-L1480)).
  **LIKELY/INFERENCE:** if you use `perform()` + `HTTP_EVENT_ON_DATA`, you pay a parser pass and two
  event-dispatch calls per received chunk; if you use the streaming API you pay a `memcpy` per read
  into your own buffer. Both are affordable at 128–320 kbit/s on an S3; neither is free.
- **Default buffer is 512 bytes.** `#define DEFAULT_HTTP_BUF_SIZE (512)` and it is used when
  `config.buffer_size == 0` ([`esp_http_client.h:18`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/include/esp_http_client.h#L18),
  [`esp_http_client.c:519-529`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/esp_http_client.c#L519-L529)).
  For a stream you want something like 2–8 KB; `esp_http_client_read()` clamps each transport read to
  `buffer_size_rx` ([`esp_http_client.c:1357-1360`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/esp_http_client.c#L1357-L1360)),
  so 512 is a syscall-per-512-bytes tax, not a correctness problem.
- **`esp_http_client_read()` returns `-ESP_ERR_HTTP_EAGAIN` (= `-0x7007`) when the transport read
  times out before any data was ready** — **VERIFIED** in the header doc
  ([`esp_http_client.h:668-678`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/include/esp_http_client.h#L668-L678),
  implementation at [`esp_http_client.c:1374-1384`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/esp_http_client.c#L1374-L1384):
  a timeout with `ridx == 0` returns `-ESP_ERR_HTTP_EAGAIN`, a timeout after partial data returns the
  partial count). This is your hook for the byte-arrival watchdog.
- **`esp_http_client_get_socket()` exists** in v5.5.5
  ([`esp_http_client.h:898-903`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/include/esp_http_client.h#L898-L903),
  impl [`esp_http_client.c:2079-2086`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/esp_http_client.c#L2079-L2086)).
  **LIKELY/INFERENCE:** useful as an escape hatch to poke socket options, but it is documented as
  "the socket from the underlying transport", and with `esp-tls` it is the raw TCP fd — **do not**
  read from it directly while the HTTP client owns it.
- **TCP keep-alive config exists on the client struct** but reaches the socket only through esp-tls:
  `keep_alive_enable/idle/interval/count` are copied into `esp_transport_keep_alive_t` and applied by
  `esp_tls` via `setsockopt(SO_KEEPALIVE/TCP_KEEPIDLE/TCP_KEEPINTVL/TCP_KEEPCNT)`
  ([`esp_http_client.h:213-216`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/include/esp_http_client.h#L213-L216),
  [`esp_http_client.c:738-743`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/esp_http_client.c#L738-L743),
  [`esp-tls/esp_tls.c:312-340`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp-tls/esp_tls.c#L312-L340)).
  **VERIFIED**: `esp_transport_tcp_set_keep_alive` is just an alias of the SSL one
  ([`tcp_transport/transport_ssl.c:603-606`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/tcp_transport/transport_ssl.c#L603-L606)),
  so it only has an effect when esp-tls is in the path. Idle keep-alive is the wrong tool for
  detecting a stalled *stream* anyway (§5).

### 3.2 The streaming API, and when to prefer it

**VERIFIED** (docs + header):
`esp_http_client_open(c, 0)` → `esp_http_client_fetch_headers(c)` →
`esp_http_client_read(c, buf, len)` → `esp_http_client_close(c)` / `cleanup(c)`, with
`esp_http_client_get_status_code()` valid after `fetch_headers()`
([`esp_http_client.h:622-627`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/include/esp_http_client.h#L622-L627),
[Espressif ESP HTTP Client docs — "HTTP Stream"](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/protocols/esp_http_client.html)).

Use this shape for the player. It gives you:

- an explicit, bounded read (so your own ring buffer owns the backpressure policy),
- a clean place to notice "no bytes for N ms" (`-ESP_ERR_HTTP_EAGAIN`),
- **no dependence on the `is_chunked` conflation**, because you read until you decide to stop.

Do **not** use `esp_http_client_perform()` for the steady state: it is a blocking call whose
termination condition is the body being "complete", which for radio means "never, or on
disconnect". `perform()` is fine for the short **playlist** fetches (`.pls`/`.m3u`, §4).

### 3.3 Raw lwIP socket client — what it buys, what it costs

Options, cheapest-effort first:

1. `esp_transport_tcp_init()` (`components/tcp_transport/`,
   [`include/esp_transport_tcp.h:41`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/tcp_transport/include/esp_transport_tcp.h#L41))
   — an `esp_transport_handle_t` with `_connect/_read/_write/_close`, no HTTP. **VERIFIED** as a
   public component (`REQUIRES tcp_transport`). **LIKELY:** the right middle ground if you want to
   write your own status-line/header parser but not deal with `getaddrinfo()`/`connect()`.
   **Caveat (VERIFIED):** it has no socket-option API of its own; `esp_transport_tcp_set_keep_alive`
   forwards to the SSL transport, so keep-alive is a no-op on plain TCP in v5.5.5.
2. Raw BSD sockets from lwIP (`sys/socket.h`). **VERIFIED** the knobs exist in ESP-IDF's lwIP:
   `#define SO_RCVTIMEO 0x1006` / `SO_SNDTIMEO 0x1005`
   ([`lwip/src/include/lwip/sockets.h:224-225`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/lwip/lwip/src/include/lwip/sockets.h#L224-L225)),
   `TCP_KEEPIDLE/TCP_KEEPINTVL/TCP_KEEPCNT/TCP_KEEPALIVE`
   ([`sockets.h:289-292`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/lwip/lwip/src/include/lwip/sockets.h#L289-L292)),
   and both features are compiled in by ESP-IDF:
   `#define LWIP_SO_RCVTIMEO 1` and `#define LWIP_TCP_KEEPALIVE 1`
   ([`lwip/port/include/lwipopts.h:995-1005`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/lwip/port/include/lwipopts.h#L995-L1005)).

**Recommendation (LIKELY, but strongly held):** write the component against
**`esp_http_client`'s streaming API**, and put your *own* framing/robustness layer above it.
Reasons, each rooted in a verified fact: `esp_http_client` already survives the ICY status line
(§1.2 TESTED), already decodes chunked bodies (§3.1 TESTED), already does TLS + certificate bundle
(§6), already follows redirects with a hop limit (§4), and already has an explicit "no data yet"
return code. A raw socket client re-implements all of that and needs its own HTTP parser.
Keep raw sockets for the two things `esp_http_client` genuinely cannot do: turn off Nagle
(`TCP_NODELAY`) / tune `SO_RCVTIMEO` on the live socket — reachable via
`esp_http_client_get_socket()` — and read the raw status line if you insist on dialect diagnostics.

### 3.4 ESP-IDF version differences (v5.x)

**VERIFIED** by reading the v5.5.5 tree; the items below are the ones that would break a
copy-pasted player:

- `esp_http_client_get_socket()` exists in v5.5.5
  ([`esp_http_client.c:2079`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/esp_http_client.c#L2079)).
  **LIKELY:** it is recent; do not assume it in v5.0–v5.2.
- `esp_http_client_clear_response_buffer()` and `esp_http_client_prepare()` /
  `esp_http_client_request_send()` / `esp_http_client_cancel_request()` are visible in the v5.5.5
  header ([`esp_http_client.h:714-724`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/include/esp_http_client.h#L714-L724));
  `clear_response_buffer()` is documented as "Clear cached response buffer (e.g. data received during
  fetch headers) … so the next request does not see stale data". **LIKELY:** if you reuse a handle
  across reconnects on older IDF, that stale cache is a real bug source.
- New errors in v5.5.5 that older code cannot know:
  `ESP_ERR_HTTP_READ_TIMEOUT` `(-0x700B)`, `ESP_ERR_HTTP_INCOMPLETE_DATA` `(-0x700C)`,
  `ESP_ERR_HTTP_REDIRECT_DOWNGRADE` `(-0x700D)`
  ([`esp_http_client.h:269-281`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/include/esp_http_client.h#L269-L281)).
- HTTPS→HTTP redirect downgrade is now **blocked** by default:
  "HTTPS origin can only redirect to https:// targets …"
  ([`esp_http_client.c:1080-1088`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/esp_http_client.c#L1080-L1088)).
  **LIKELY:** this bit an existing ESP32 radio project at some point; with `disable_auto_redirect`
  you can handle it yourself. Note that many stations' playlists (`https://`) point at `http://`
  mirrors — this rule applies to HTTP redirects, not to URLs *inside* a playlist you fetch yourself.
- `MBEDTLS_DYNAMIC_BUFFER` + `esp_http_client_tls_dyn_buf_strategy` (`HTTP_TLS_DYN_BUF_RX_STATIC`)
  appear under `#if CONFIG_MBEDTLS_DYNAMIC_BUFFER`
  ([`esp_http_client.h:152-155`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/include/esp_http_client.h#L152-L155)).
  §6.
- A local tree for `v5.4.2` and `v6.1` is present on this machine, but I did **not** diff them; treat
  any v5.4-vs-v5.5 claim above as anchored to v5.5.5 only.

---

## 4. Redirects and playlist indirection

### 4.1 HTTP redirects

- **VERIFIED**: `esp_http_client` follows `301/302/303/307/308` automatically by default
  (`HttpStatus_MovedPermanently`…`HttpStatus_PermanentRedirect`,
  [`esp_http_client.c:1119-1126`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/esp_http_client.c#L1119-L1126)),
  default hop limit `static const int DEFAULT_MAX_REDIRECT = 10`
  ([`esp_http_client.c:167`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/esp_http_client.c#L167),
  applied at [`:531-532`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/esp_http_client.c#L531-L532)).
  With `disable_auto_redirect = true` you get `HTTP_EVENT_REDIRECT` carrying
  `{ client, status_code }` ([`esp_http_client.h:70-74`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/include/esp_http_client.h#L70-L74))
  and call `esp_http_client_set_redirection()` yourself.
- **VERIFIED**: only `Location` is captured (`strcasecmp(..., "Location")` at
  [`esp_http_client.c:273-275`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/esp_http_client.c#L273-L275));
  **likely** no relative-URL resolution — FFmpeg, by contrast, explicitly calls
  `ff_make_absolute_url()` on the `Location` value
  ([`libavformat/http.c:996-1001`](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/http.c#L996-L1001)).
  **LIKELY:** absolute `Location:` is overwhelmingly what radio hosts send, but if you want to be
  safe, resolve relative Locations yourself before calling `set_url`.
- **VERIFIED, and this is what token-signed hosts look like** — `http://stream.zeno.fm/0r0xa792kwzuv`
  answers a `302` with `content-length: 0` and a `Location` carrying a JWT (`?zt=...`), and the
  followed target answers `HTTP/1.1 200 OK` + `transfer-encoding: chunked` + `content-type: audio/mpeg`
  + `icy-name` + `icy-br: 128` + `icy-metaint: 16000` (**OBSERVED**, this session).
  **LIKELY/INFERENCE:** the token is time-limited, so **do not cache the redirected URL**; always
  re-walk the redirect chain on reconnect, and re-send `Icy-MetaData: 1` each hop.

### 4.2 Playlist indirection (`.pls`, `.m3u`, `.m3u8`)

Both are plain text and both are *just a list of URLs*; either may be served gzip'd or chunked.

- **OBSERVED (PLS, live)** — `https://somafm.com/groovesalad.pls` →
  `Content-Type: audio/x-scpls`, `Transfer-Encoding: chunked`, body:

  ```
  [playlist]
  numberofentries=3
  File1=https://ice6.somafm.com/groovesalad-128-mp3
  Title1=SomaFM: Groove Salad (#1): A nicely chilled plate of ambient/downtempo beats and grooves.
  Length1=-1
  File2=...   Length2=-1
  File3=...   Length3=-1
  Version=2
  ```

  Note: keys in **lowercase**, `Length=-1` = live stream, and **multiple mirrors** — pick one,
  prefer the first, and rotate on reconnect.
- **LIKELY (PLS format)**: INI-like, `[playlist]` section, `NumberOfEntries=N`, then `FileN=`,
  `TitleN=`, `LengthN=` for N=1..; `LengthN=-1` or `0` means "stream, unknown/endless". Case-insensitive
  keys. This is de-facto (no standards body owns PLS); the primary evidence is the live file above
  plus the `audio/x-scpls` MIME type used by SomaFM's nginx. Wikipedia's
  [PLS (file format)](https://en.wikipedia.org/wiki/PLS_(file_format)) is a **secondary** source that
  agrees.
- **VERIFIED (M3U MIME)**: Icecast serves `.m3u` as `audio/x-mpegurl`
  ([`src/fserve.c:373-374`](https://github.com/xiph/Icecast-Server/blob/master/src/fserve.c#L373-L374))
  and generates one with `Content-Disposition: attachment; filename=listen.m3u`
  ([`src/admin.c:1105-1117`](https://github.com/xiph/Icecast-Server/blob/master/src/admin.c#L1105-L1117)).
- **LIKELY (M3U/M3U8 format)**: line-based; first line may be `#EXTM3U`; `#EXTINF:<seconds>,<title>`
  lines annotate the *next* URL; **lines beginning with `#` are comments/metadata, never URLs**;
  a URL may be absolute (`http://`, `https://`) or relative to the playlist's own URL; blank lines and
  CRLF are normal. HLS `.m3u8` uses the same syntax with `#EXT-X-*` tags and lists *segments*, not a
  live stream — **a player for continuous radio should treat an `.m3u8` as "not this component's job"**
  (or explicitly reject it) rather than half-implement HLS. FFmpeg's HLS writer shows the tag
  grammar it emits (`#EXTM3U`, `#EXT-X-TARGETDURATION`, `#EXTINF`) at
  [`libavformat/hlsplaylist.c:36-161`](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/hlsplaylist.c#L36-L161).
- **Design rules (LIKELY):**
  1. Decide "playlist or stream?" from **`Content-Type` *and* the first bytes**, not the URL suffix:
     `audio/x-scpls`, `audio/x-mpegurl`, `audio/mpegurl`, `application/playlist` +
     `text/plain`-looking bodies. Some stations serve a playlist from a URL ending in `/stream`.
  2. Cap the playlist body (e.g. 64 KB) — you are fetching a 300-byte file, and a hostile/broken
     server should not be able to OOM you.
  3. Cap the total number of hops: HTTP redirects (10) × playlist indirections (2–3) — pick a hard
     ceiling (e.g. 8 socket connections) and then back off.
  4. On a PLS with multiple `FileN`, **rotate** on reconnect instead of always retrying mirror #1.
  5. Re-send `Icy-MetaData: 1` on the final stream request, always.

---

## 5. Playing for days: timeouts, watchdog, buffering

This section is mostly **LIKELY/INFERENCE** — it is engineering judgement about a design, not a
quotable spec. The verifiable hooks are named where they exist.

### 5.1 Bitrates and CBR/VBR

- **OBSERVED**: `icy-br: 128` / `192` on the stations probed; `ice-audio-info` carries the real
  `samplerate`/`channels`/`bitrate`. Typical internet radio MP3 is **96–320 kbit/s CBR**; AAC/HE-AAC
  streams are often **32–128 kbit/s** and are frequently **VBR**.
- **LIKELY (and it matters):** `icy-br` is *nominal*, not a promise; with VBR the instantaneous rate
  can differ by ±50 % around it. Consequences for the component:
  - **Never size the buffer from `icy-br` alone.** Size it in *seconds* and let the ring hold
    seconds, not kilobytes.
  - **Never** treat "the decoder consumed X bytes in T seconds" as a clock: for VBR streams, derive
    the timeline from **decoded PCM sample counts**, not from byte counts. (This is also why the
    "unbounded latency" problem in §5.4 must be solved at the decoder/PCM boundary.)
  - The **byte-arrival watchdog** (§5.3) must be a *no-bytes-for-N-ms* check, not a
    minimum-throughput check, precisely because VBR dips are normal.

### 5.2 Socket-level settings

- `SO_RCVTIMEO` — **VERIFIED** available and enabled (`lwipopts.h:998`,
  `sockets.h:225`). Set it to something small-ish (e.g. 1–2 s) so `esp_http_client_read()`
  periodically returns `-ESP_ERR_HTTP_EAGAIN` and your loop can do other work and update the
  watchdog. `esp_http_client`'s `timeout_ms` is threaded into `esp_transport_read(...)` per call
  (**VERIFIED**: [`esp_http_client.c:1357-1360`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/esp_http_client.c#L1357-L1360)),
  so you rarely need to touch the fd directly.
  **Note (VERIFIED as a docs statement)**: when `esp_tls_cfg_t.timeout_ms <= 0` the connection relies
  on the underlying socket timeout `ESP_TLS_DEFAULT_CONN_TIMEOUT`
  ([`esp-tls/esp_tls.h:448-452`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp-tls/esp_tls.h#L448-L452)).
- `TCP_NODELAY` — for a stream you send nothing after the request, so it is irrelevant on the player
  side (it matters for the *server*, cf. `resources/mini_icecast.c`).
- TCP keep-alive (`SO_KEEPALIVE` + `TCP_KEEPIDLE/INTVL/CNT`) — **VERIFIED** present in lwIP and
  applied through esp-tls when `keep_alive_enable` is set
  (`esp_tls.c:312-340`). **LIKELY:** it only detects a *dead peer* (router/NAT/wifi took the path
  away) after idle+interval×count seconds (defaults 5 s idle, 5 s interval, 3 probes). It does **not**
  detect an alive-but-stalled server, which is the common real failure. Use it as a cheap backstop,
  not as the primary health signal.
- **Wi-Fi state is a first-class input.** **VERIFIED** from the example code in this workspace that
  `WIFI_EVENT_STA_DISCONNECTED` is the event to hook (`resources/main.c:41-47`).
  **LIKELY:** on `STA_DISCONNECTED`, proactively `esp_http_client_close()`/`cleanup()` and reset the
  reconnect backoff instead of waiting for the socket to notice.

### 5.3 Stalled-stream watchdog (byte-arrival)

**LIKELY/INFERENCE — the design I'd build:**

- Record `last_rx_ms` whenever `esp_http_client_read()` returns `> 0`.
- Two thresholds, because a stalled stream and a slow one look different at first:
  - **soft stall** = no bytes for `max(3 s, 3 × metaint / bitrate)` → log, increment a stall counter;
  - **hard stall** = no bytes for e.g. 10 s → tear down and reconnect.
  - For a 128 kbit/s stream, 16000 audio bytes take ≈1.0 s, so metadata/audio should arrive roughly
    once a second. A 5 s silence is already abnormal; 10 s is dead.
- Also watchdog the **decoder/producer** side: if the ring has data but the I2S/decoder task is not
  draining it, that is a different fault (CPU/Wi-Fi contention), and the fix is not a reconnect.
- The kill switch must be able to interrupt a blocked `read`: with `SO_RCVTIMEO`/`timeout_ms` set,
  the read returns `-ESP_ERR_HTTP_EAGAIN` and the watchdog task sees control. **Do not** use an
  infinite timeout.
- **LIKELY:** `icy-metaint` gives you a free protocol-level liveness heartbeat: you should see a
  metadata block every `metaint` audio bytes, i.e. ~1 s at 128 kbit/s. A missing metadata block with
  audio still flowing means the framing is broken, not the network.

### 5.4 Buffering, watermarks, and not growing latency

**LIKELY/INFERENCE**, stated as concrete numbers to argue with:

- Target **3–6 s** of audio in the ring (after decode) for a Wi-Fi device that must survive
  hiccups; less than ~2 s and ordinary Wi-Fi retries cause audible dropouts, more than ~10 s and
  station changes/zapping feel broken. Bytes: at 128 kbit/s CBR that is 48–96 KB *compressed*;
  if you ring **PCM**, 44.1 kHz stereo 16-bit is 176.4 KB/s, so 6 s = ~1 MB — **do not** ring decoded
  PCM unless you have PSRAM.
- Ring **compressed** bytes (the natural unit for `read()`), and put the watermark policy on that
  ring:
  - **high watermark**: stop reading from the socket until it drains to `high − hysteresis`
    (e.g. 6 s → resume at 4 s). This is what lets the socket buffer + TCP window do their job
    instead of you pointlessly copying.
  - **low watermark**: if the ring falls below ~1 s, that's the moment to *start* looking unhealthy;
    below ~0.3 s you are going to glitch regardless.
- **Avoiding unbounded latency growth** is the hard part. Three rules:
  1. **Never re-buffer on top of old audio after a reconnect.** On reconnect, flush the ring (or mark
     a discontinuity) — otherwise the listener drifts further and further behind live. Real players
     solve this by jumping to the live edge whenever the buffer exceeds a target.
  2. **Measure drift, don't guess it.** You cannot compare byte counts to wall-clock for VBR.
     Either (a) count decoded PCM samples and compare against a wall clock, or (b) timestamp blocks
     when they arrive and drop on a threshold. (a) is the only one that survives VBR.
  3. **Cap re-buffering**: if the ring is full and the clock says you are > target seconds behind,
     drop the oldest N seconds rather than growing the ring.
- **Multi-stage buffering is unavoidable and each stage has its own latency:** lwIP/TCP receive
  window, the `esp_http_client` rx buffer, your ring, then the decoder's internal/DMA buffers.
  **LIKELY:** on the S3, keeping the sum near 3–6 s is achievable; going below ~1 s end-to-end
  usually requires a smaller TCP rx window (`CONFIG_LWIP_TCP_WND_DEFAULT`) and is rarely worth it for
  radio.

### 5.5 Reconnect and backoff

**LIKELY/INFERENCE:**

- On any of: socket error, `FIN` with `is_chunked` set, `ESP_ERR_HTTP_CONNECTION_CLOSED`,
  `ESP_ERR_HTTP_READ_TIMEOUT`, zero-length read, watchdog expiry, Wi-Fi disconnect →
  tear the client down (`esp_http_client_cleanup()` — reuse across a dead connection is a
  bug farm, cf. `esp_http_client_clear_response_buffer()`), and rebuild it.
- **Exponential backoff with jitter**, capped: e.g. 0.5 s, 1 s, 2 s, 4 s, 8 s, 15 s, 15 s… and reset
  the sequence after, say, 60 s of healthy playback. Jitter matters because a station outage makes
  thousands of devices retry in lockstep.
- **Do not back off on a successful short connection.** Some hosts will accept and then drop after a
  few hundred ms (capacity shedding). Distinguish "connected but got no audio" (back off) from
  "played for a minute then dropped" (reconnect promptly, rotate the mirror).
- **Rotate** mirrors from a PLS and/or walk the redirect chain again on each attempt (§4).
- **LIKELY:** the number one cause of "it dies after 6 hours" in this class of device is a
  reconnect that never resets parser state (`metaint`, audio byte counter, partial metadata block,
  the `StreamTitle` cache). Reset all four explicitly.

---

## 6. TLS (`https://` streams)

**VERIFIED** facts:

- HTTPS is supported in `esp_http_client` via mbedTLS/esp-tls, gated by
  `CONFIG_ESP_HTTP_CLIENT_ENABLE_HTTPS` (enabled by default)
  ([ESP HTTP Client docs — "HTTPS Request"](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/protocols/esp_http_client.html)).
- Server verification needs either `cert_pem` (a specific root) or `crt_bundle_attach =
  esp_crt_bundle_attach` (the Mozilla-derived bundle) — same doc; bundle API in
  [`components/mbedtls/esp_crt_bundle/include/esp_crt_bundle.h`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/mbedtls/esp_crt_bundle/include/esp_crt_bundle.h)
  and the ["ESP x509 Certificate Bundle"](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/protocols/esp_crt_bundle.html)
  doc: "more than 130 certificates" for the full bundle, or a **filtered list of ~42** with
  "around 93 % absolute usage coverage and 99 % market share coverage". Options:
  `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE`, `CONFIG_MBEDTLS_DEFAULT_CERTIFICATE_BUNDLE`,
  `CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH`.
- **Memory cost is dominated by the mbedTLS record buffers**, and the defaults are worth knowing
  exactly ([`components/mbedtls/Kconfig:54-118`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/mbedtls/Kconfig#L54-L118)):
  - `MBEDTLS_ASYMMETRIC_CONTENT_LEN` **default y**: "enabling this with default values saves 12 KB of
    dynamic memory per TLS connection".
  - `MBEDTLS_SSL_IN_CONTENT_LEN` **default 16384** (range 512–16384).
  - `MBEDTLS_SSL_OUT_CONTENT_LEN` **default 4096** (range 512–16384).
  - `MBEDTLS_DYNAMIC_BUFFER` **default n**: allocate TX/RX only while needed — "to save more heap,
    users can set the options to be an appropriate value". Requires
    `MBEDTLS_ASYMMETRIC_CONTENT_LEN`, `select`s it.
  - `MBEDTLS_SSL_MAX_CONTENT_LEN` **default 16384**, "required to comply fully with TLS standards",
    with the warning that setting it too low yields "a failed TLS handshake or … 
    `MBEDTLS_ERR_SSL_INVALID_RECORD` (-0x7200)".
  - **LIKELY/INFERENCE (arithmetic):** with asymmetric defaults you're at ~16 KB in + ~4 KB out
    ≈ **20 KB of record buffers per TLS connection**, plus the handshake's transient allocations
    (bignum, ECDHE, cert chain parsing) which are typically tens of KB peak, plus the bundle's
    rodata (in flash, not RAM) and the RSA/ECC code. `MBEDTLS_DYNAMIC_BUFFER` cuts the idle cost.
- **Espressif's own framing of the RAM situation**: "ESP32-S3 has a few hundred kilobytes of internal
  RAM"
  ([Support for External RAM](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-guides/external-ram.html)).
  I could **not** extract the datasheet's headline "512 KB SRAM" figure from a primary Espressif page
  from here (the datasheet link redirects to `documentation.espressif.com`, and the PDF text extraction
  I attempted did not surface the memory section) — **so treat "512 KB SRAM" as UNVERIFIED in this
  note**. The sibling note [`07-esp32s3-hardware-verified.md`](07-esp32s3-hardware-verified.md) has the
  verified address windows (`SOC_IRAM_LOW/HIGH` = 0x40370000–0x403E0000, `SOC_DRAM_LOW/HIGH` =
  0x3FC88000–0x3FD00000, i.e. 448 KB IRAM / 480 KB DRAM windows, partly aliased) straight out of
  `components/soc/esp32s3/include/soc/soc.h`; budget against *those*, not against "512 KB".
- **Also verified**: TLS on ESP32-S3 has a buffer-size API reachable from esp-tls
  (`buf_size_tx`/`buf_size_rx` fields exist in the component, and `esp_http_client` exposes
  `HTTP_TLS_DYN_BUF_RX_STATIC` under `CONFIG_MBEDTLS_DYNAMIC_BUFFER` to convert dynamic RX allocation
  to static after the handshake, "reducing memory fragmentation",
  [`esp_http_client.h:152-155`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/include/esp_http_client.h#L152-L155)).

**Does it fit next to a decoder in a ~512 KB-SRAM part? — LIKELY yes, with discipline:**

- One TLS connection at a time, ~20 KB of record buffers + transient handshake, plus the audio ring
  (compressed, tens of KB) plus the decoder (MP3 decoders are single-digit-to-~40 KB of state
  depending on implementation). That is comfortably inside a few hundred KB **if** you (a) don't run
  TLS and a second socket simultaneously, (b) put the big ring in PSRAM when present, (c) consider
  `MBEDTLS_DYNAMIC_BUFFER` + `MBEDTLS_SSL_IN_CONTENT_LEN` reduction (a radio stream receives small
  records; 16384 is only needed to satisfy the standard, not this workload).
- **`skip_cert_common_name_check` / `ESP_TLS_INSECURE` / `ESP_TLS_SKIP_SERVER_CERT_VERIFY`** exist
  ([`esp-tls/Kconfig:85-96`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp-tls/Kconfig#L85-L96),
  `esp_http_client.h:207`). **LIKELY:** as a *shipping default* this is wrong (it turns TLS into
  obfuscation), but as a **fallback** it converts "station is broken forever on this firmware" into
  "station plays with a warning". Recommend: bundle-verified by default; on a *certificate* error
  only, retry once with verification relaxed and surface that to the UI. That needs
  `esp_http_client_get_and_clear_last_tls_error()` to tell a cert failure from a TCP failure
  ([`esp_http_client.h:558-568`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_client/include/esp_http_client.h#L558-L568)).
  **LIKELY:** the filtered ~42-cert bundle is the right default for a radio device (station CDNs use
  mainstream CAs), and massively cheaper than a per-station `cert_pem` database.
- **HTTPS radio is still the minority.** Every station I probed served **HTTP** (SomoFM's `.pls` is
  HTTPS but its `FileN` mirrors are `https://ice6.somafm.com/...` — **OBSERVED**). **LIKELY:**
  support TLS, but make plain HTTP the zero-cost fast path so an HTTP-only build is possible.

---

## 7. Real-world non-conformance checklist

Each row: what the server does → what the player must do. Sources as marked.

| # | Non-conformance | Evidence | Player behaviour |
|---|---|---|---|
| 1 | `ICY 200 OK` instead of `HTTP/1.0 200 OK` | **TESTED**: ESP-IDF `http_parser` accepts it (`http_parser.c:801`, `:781`); FFmpeg's does **not** (`http.c:938`) | Accept both. If writing your own line parser, accept `ICY` or `HTTP/1.x`; do not require a `/` version after ICY |
| 2 | No `icy-metaint` in the response | **VERIFIED** when client omits the opt-in (`format_mp3.c:739-752`) | Body is pure audio. Also: the *presence* of `icy-metaint` is the only signal to enable in-body framing |
| 3 | `icy-metaint` present but the server sends only zero-length metadata blocks | **VERIFIED** possible: Icecast sends `"\0"` whenever the title has not changed (`format_mp3.c:374-397`) | Length byte 0 ⇒ keep previous title, continue audio. Never treat as error |
| 4 | `Transfer-Encoding: chunked` on an endless stream | **OBSERVED** on zeno.fm/stream-285; **TESTED** ESP-IDF decodes chunked correctly (`HPE_OK`, chunk lengths reported) | Use `esp_http_client` (chunk decode free) or implement chunk framing. Beware the `is_chunked` conflation (§3.1). Audio framing (`metaint`) is *beneath* chunk framing — de-chunk first, then count audio bytes |
| 5 | Chunked body with a bogus first chunk (`ZZ\r\n`) | **TESTED**: `HPE_INVALID_CHUNK_SIZE`, parser stops; `esp_http_client_read` then yields partial/`ESP_FAIL` | Treat as a broken server, close, back off, try another mirror. Do not spin-wait on it |
| 6 | No `Content-Length`, no `Transfer-Encoding` | **TESTED**: `http_message_needs_eof()` → body until EOF | Read until EOF/timeout. This is the *normal* case for radio |
| 7 | `Connection: Close` on an `HTTP/1.0` stream | **OBSERVED** (omroep.nl) | One request per connection. Never attempt request pipelining/reuse on the stream socket |
| 8 | `302` with `content-length: 0` and a time-limited signed `Location` | **OBSERVED** (zeno.fm) | Follow; do not cache the target URL beyond the current session |
| 9 | Playlist served with `Transfer-Encoding: chunked` and `audio/x-scpls` | **OBSERVED** (somafm.com) | Parse a bounded body; PLS keys are lowercase; `Length=-1` means stream |
| 10 | Multiple mirrors | **OBSERVED** (PLS `File1..File3`) | Rotate on reconnect |
| 11 | Title contains `'` or `;`, or is ISO-8859-1 while you assume UTF-8 | **VERIFIED** no escaping on write (`format_mp3.c:329-336`); **VERIFIED** default charset ISO-8859-1 (`format_mp3.c:244-247`) | Parse to the next `';`; never `sscanf("%[^']")` blindly; tolerate invalid UTF-8 and optional Latin-1 fallback |
| 12 | Metadata block bigger than expected / longer title | **VERIFIED** cap `MAX_META_LEN 255*16` (`format_mp3.c:304`) | Allocate/limit 4081 bytes max; reject higher length bytes |
| 13 | `metaint` differs on the next connection | **LIKELY** (per-mount `icy-metadata-interval`; relay values) | Reset the counter and re-read `icy-metaint` per connection |
| 14 | Server closes mid-chunk / mid-`metaint` | **VERIFIED** `esp_http_client` logs chunked FIN at DEBUG and treats it as clean (`esp_http_client.c:1361-1390`) | Discard the partial metadata block, treat as reconnect (not fatal) |
| 15 | Very slow trickle / stall with the socket open | **LIKELY** | Byte-arrival watchdog, not `icy-br`-based throughput checks (§5.3) |

---

## 8. Concrete shape for the component (all points above collapsed)

**LIKELY/INFERENCE** — the interface this research argues for. Public API in `include/`, one header.

State one connection at a time:

```
IDLE → RESOLVE(playlist?) → CONNECT → HEADERS(parse icy-*, decide playlist vs stream)
     → STREAMING{ audio | metadata } → (error/stall/redirect) → backoff → CONNECT …
```

Non-negotiable implementation details, each traceable to a section:

1. Set `Icy-MetaData: 1` **and** copy every `Icy-*` response header in `HTTP_EVENT_ON_HEADER`
   (they are freed immediately) — §3.1.
2. `esp_http_client_open()` + `fetch_headers()` + a `read()` loop; never `perform()` for audio — §3.2.
3. Byte-level state machine: `audio_left = metaint`; on 0 → read 1 length byte → if 0, resume with
   `metaint`; else read `n*16` bytes into a 4081-byte buffer, NUL-terminate, parse `Key='value';`
   pairs, resume with `metaint`. Handle a metadata block split across reads — §2.
4. Reset `metaint`, `audio_left`, the partial-block buffer and the title on **every** connect — §5.5.
5. Watchdog on `last_rx_ms`; `SO_RCVTIMEO`/`timeout_ms` must be finite so `-ESP_ERR_HTTP_EAGAIN`
   can drive it — §5.3.
6. Ring on compressed bytes, high/low watermarks in **seconds**, flush on reconnect, drift measured
   from decoded PCM sample counts, not bytes — §5.4.
7. Treat `FIN`/`ESP_ERR_HTTP_CONNECTION_CLOSED`/`READ_TIMEOUT`/`EAGAIN`-after-audio as reconnect
   triggers; back off with jitter; rotate PLS mirrors — §5.5, §7.
8. TLS: filtered cert bundle by default, one connection at a time, `MBEDTLS_DYNAMIC_BUFFER` and/or
   reduced `MBEDTLS_SSL_IN_CONTENT_LEN` when SRAM is tight; optional single relaxed-verify retry on a
   *certificate* error only — §6.

Testing hooks worth building early: point the player at `resources/` (the mini Icecast server) to
exercise the metadata framing deterministically, including the length-byte-0 path
(`mini_icecast.c:send_meta()`), and at a local `nc -l` that emits a hand-written `ICY 200 OK` header
plus a metadata block split across TCP segments.

---

## 9. Claims I could not verify

1. **"512 KB SRAM" for ESP32-S3** — I could not pull the number out of a primary Espressif page from
   this machine. Espressif's own external-RAM guide says only "a few hundred kilobytes of internal
   RAM". Treat the exact figure as unverified here; the memory *arithmetic* in §6 rests on the
   Kconfig defaults, which are verified.
2. **A live `ICY 200 OK` server** — I could not reach one to capture the response. The claim that it
   still occurs in the wild is an inference from ESP-IDF's `http_parser` having an explicit ICY
   branch (and from the de-facto protocol's Nullsoft origin), not from an observation here. What *is*
   verified is that ESP-IDF's parser handles it correctly (TESTED) and that modern Icecast does not
   emit it — the sibling note [`06-who-sends-icy-status-line.md`](06-who-sends-icy-status-line.md)
   reaches the same conclusion from the Icecast source, with the extra detail that
   `connection_handle_client()` rejects any request protocol that is not `ICE` or `HTTP`.
3. **Whether `esp_http_client` resolves relative `Location:` headers** — I verified only that
   `Location` is captured and that FFmpeg explicitly absolutizes it. I did not trace
   `esp_http_client_set_url()` deeply enough to rule out partial resolution.
4. **v5.4.2 / v6.1 differences** — trees exist locally but I did not diff them. All version claims
   are v5.5.5-anchored.
5. **Exact `StreamUrl` / extra-key presence in the wild** — verified that Icecast *can* emit it
   (`format_mp3.c:338-349`); none of the streams I captured happened to include it.
6. **Whether some servers send `icy-metaint` > 4080-byte-block-representable values or change
   `metaint` mid-stream** — no source found for either; Icecast's interval is fixed per client at
   connect time.
