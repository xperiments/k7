# Transport layer: locally verified findings (executed, not recalled)

This file records findings established by **running code on this machine**, because
they are the load-bearing claims for the library's transport design and they are
repeatedly reported incorrectly in secondary sources. Everything here was verified
against the ESP-IDF trees actually installed at `~/.espressif/` (v5.4.2, v5.5.5, v6.1)
— the sources that own the behaviour — and then re-confirmed across all three versions.

Date of checks: **2026-10-16** (local IDF trees, and `components.espressif.com` as
reachable that day).

## 1. `esp_http_client` **does** support SHOUTcast/ICY — no raw-socket client needed

The common claim that Espressif's HTTP client cannot talk to Icecast/SHOUTcast is
**false**. The vendored parser in `components/http_parser/http_parser.c` has an explicit
ICY status-line path. In `s_start_res`:

```c
case 'H':  UPDATE_STATE(s_res_H);   break;
case 'I':  /* ICY URI */  UPDATE_STATE(s_res_I);  break;
case CR:
case LF:   break;
default:   SET_ERRNO(HPE_INVALID_CONSTANT); goto error;
```

and the ICY chain `s_res_I → s_res_IC → s_res_http_minor` consumes the literal `ICY`.
It also handles the second SHOUTcast quirk, the reversed `HTTP/1.x 200 OK` form, via
`s_res_or_resp_H` → request-method state (`http_parser.c:755-769`). Identical support
exists in v5.4.2, v5.5.5 and v6.1 (4 `s_res_IC` occurrences and 3 `s_res_or_resp_H`
occurrences in each).

**This was executed.** The IDF parser was compiled standalone (`cc t.c http_parser.c`)
and fed a byte-for-byte classic ICY response. Results (strict **on**, which is the
default — `#ifndef HTTP_PARSER_STRICT / #define HTTP_PARSER_STRICT 1`):

| Response fed to the parser | Outcome |
|---|---|
| `ICY 200 OK` + `icy-name/genre/br/metaint` + `content-type` | `HPE_OK`; `on_status` = `'OK'`; **`status_code=200`**; every `icy-*` header surfaced as `on_header_field`/`on_header_value`; `content_length=ULLONG_MAX` |
| `HTTP/1.0 200 OK` + `icy-metaint: 8192` + `content-type: audio/aacp` | `HPE_OK`; `status_code=200`; `icy-metaint` surfaced; `content_length=ULLONG_MAX` |
| `HTTP/1.1 200 OK` + `Content-Length: 42` (playlist case) | `HPE_OK`; `status_code=200`; `content_length=42` |

Consequences for the design:

- `status_code` is populated correctly for ICY — `200`, not garbage. Redirect handling
  in `esp_http_client_fetch_headers` keys off `status_code`, so it still works.
- `http_major`/`http_minor` are **0/0** for an `ICY` line (there is no version token).
  Any code that branches on protocol version must not assume `1`. `esp_http_client`
  itself does not gate on it.
- The literal token `ICY` arrives through the normal `on_header_*` callbacks, so
  `Icy-MetaData` can be *requested* with the ordinary
  `esp_http_client_set_header(client, "Icy-MetaData", "1")`.
- A response with no `Content-Length` yields `content_length = ULLONG_MAX`
  (= `(uint64_t)-1`), which is precisely the "endless stream" signal.

## 2. `esp_http_client_get_header()` does **not** read response headers

This is the sharpest API trap for a radio player and it is easy to get wrong, because
the function name suggests otherwise and it is declared without the distinction being
obvious. In `esp_http_client.c:367-374`:

```c
esp_err_t esp_http_client_get_header(esp_http_client_handle_t client, const char *key, char **value)
{
    if (client == NULL || client->request == NULL || client->request->headers == NULL || key == NULL)
        return ESP_ERR_INVALID_ARG;
    return http_header_get(client->request->headers, key, value);   /* <-- REQUEST headers */
}
```

It resolves against `client->request->headers` — the headers **you sent**. Calling
`esp_http_client_get_header(client, "icy-metaint", &v)` to learn the stream's metadata
interval cannot work.

Response headers are delivered two ways only:

1. **Event callback** (`esp_http_client_config_t.event_handler`): each header fires
   `HTTP_EVENT_ON_HEADER` with `client->event.header_key` / `header_value`
   (`http_on_header_event`, `esp_http_client.c:239-253`). Header names are stored in
   **wire case** (verified: `icy-name` came through lowercase, `Content-Type`
   capitalised), so matching must be case-**insensitive**.
2. Or parse the raw response bytes yourself, which defeats the purpose.

Note `HTTP_EVENT_ON_DATA` also fires (from `http_on_body`) and hands over the body,
including ICY metadata bytes interleaved in the audio — the client does **not** strip
them.

## 3. ICY metadata framing — a validated reference algorithm

The wire contract: the server sends `metaint` audio bytes, then a **1-byte length**
`L` counted in **16-byte blocks**, then exactly `16*L` bytes of metadata. `L == 0`
means "no metadata here" and occupies that single byte.

A correct demuxer was written and tested against the edge cases that break naive ones.
The contract it satisfies:

- Wait for ≥ 1 byte before deciding anything; `L` is unknown until then.
- The metadata region is **exactly `16*L` bytes**. Do **not** rely on a NUL terminator:
  verified that a payload which fills its last block byte completely has **no padding**,
  and a naive "scan to NUL" reader mangles or overshoots it. Scan the region for
  `StreamTitle='` and terminate at the closing quote **or** at the region end.
- A socket read can split a metadata block anywhere. The demuxer must return a
  "need more" status and the caller must keep buffering; the tested half-available case
  correctly yields `NEED_MORE` with nothing consumed.
- `L == 0` consumes exactly one byte.

Verified behaviours worth designing around, all executed:

| Case | Result |
|---|---|
| 1-block title, UTF-8 title, empty title, `L=0` | parsed correctly, correct byte consumption |
| Title spanning 3 blocks (49 bytes consumed) | parsed correctly |
| Payload completely filling the final block byte | parsed correctly by region-scan; **naive NUL-scan is wrong** |
| Read split mid-metadata | `NEED_MORE`, 0 consumed — caller must buffer |

### The unfixable quirk: apostrophes in titles

A title containing a single quote — `StreamTitle='It's a station';` — parses as `"It"`,
because the first `'` is indistinguishable from the terminator. There is no escaping in
the ICY convention. Tested and confirmed. Notably, xtape's own server already
demonstrates awareness of this by sanitising `'` → space when *emitting* titles
(`resources/mini_icecast.c:284`), which is why clean implementations send titles that
survive the round trip. The player-side library must therefore:

- accept the truncation as the de-facto behaviour (matching what other players show), and
- **document it**, rather than silently returning wrong titles.

## 4. Practical parameters for a long-running player

- `esp_http_client_open()` → `esp_http_client_fetch_headers()` → loop
  `esp_http_client_read()` is the supported streaming sequence; the helper
  `esp_http_client_read_response()` and the `perform()` path buffer to completion and
  are therefore **unusable for an endless stream**.
- Timeouts come from `esp_http_client_config_t.timeout_ms` and the transport's
  `network_timeout_ms`. Verified in the headers that `esp_http_client_read` is the
  byte-oriented read and that `esp_http_client_is_chunked_response()` exists for the
  chunked case; note the client only sets `is_chunked` when the header value literally
  begins `chunked` (`strcasecmp(key,"Transfer-Encoding")==0 && memcmp(at,"chunked",length)==0`),
  so a non-conformant stream may report `false` while actually chunking — the reader
  must be defensive.
- `SO_RCVTIMEO`-style stall detection is needed on top: a stream that stops sending
  without closing the socket otherwise blocks the network task indefinitely.

## 5. Version coverage

The ICY path and the streaming API were checked in **v5.4.2, v5.5.5, v6.1** and are
present in all three; `esp_driver_i2s`'s standard-mode API
(`i2s_channel_init_std_mode`, `i2s_channel_reconfig_std_clock`) is also present in
v6.1, so the same component source can span IDF 5.4 → 6.x without `#if` gymnastics for
these two subsystems. This sets a credible `idf: ">=5.4"` floor.

## Reproduction

```bash
IDF=~/.espressif/v5.4.2/esp-idf/components/http_parser
cp $IDF/http_parser.{c,h} /tmp/icytest/ && cd /tmp/icytest
# feed "ICY 200 OK\r\nicy-metaint: 16000\r\n\r\n" to http_parser_execute and observe
# status_code=200 with on_header_field/on_header_value firing for each icy-* header
```

## Explicitly not verified here

- Whether any *specific* real-world station is non-conformant (no live stream was
  contacted from this environment); the malformed-stream handling above is derived from
  the parser code and the framing contract, not from observed traffic.
- TLS/mbedtls heap cost for `https://` streams — **not measured**, and it should be,
  since it competes with the decoder for the same SRAM.
- Behaviour of `esp_http_client` when a redirect chain crosses into a `.pls`/`.m3u`
  body rather than a `Location:` header; the parser surfaces the body but any playlist
  parsing must be written by the library.
