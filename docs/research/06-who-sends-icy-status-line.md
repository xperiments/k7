# Who actually sends `ICY 200 OK`? (Icecast + SHOUTcast source read, 2026-10-16)

An important refinement to the transport design: the fact that `esp_http_client`'s parser
understands `ICY 200 OK` does **not** mean modern Icecast emits it. Reading the Icecast
server source settles which side of the wire the ICY status line comes from, and therefore
what the parser's ICY support is actually for.

## Icecast listener responses are HTTP, never ICY

Three independent places in `xiph/Icecast-Server` (`master`) confirm this:

1. **`connection_handle_client()` rejects anything that is not `ICE` or `HTTP`** as the
   *request* protocol ([`src/connection_handle.c`](https://raw.githubusercontent.com/xiph/Icecast-Server/master/src/connection_handle.c)):

   ```c
   if (strcmp("ICE",  httpp_getvar(parser, HTTPP_VAR_PROTOCOL)) &&
       strcmp("HTTP", httpp_getvar(parser, HTTPP_VAR_PROTOCOL))) {
       ICECAST_LOG_ERROR("Bad HTTP protocol detected");
       client_destroy(client);
       return;
   }
   ```

   So Icecast's own vocabulary for `HTTPP_VAR_PROTOCOL` is `ICE` or `HTTP` — the string
   `ICY` is not a listener-side protocol it produces.

2. **`ICY` is set only on the source-upload admin path.** The sole assignment found is
   inside `__prepare_shoutcast_admin_cgi_request()`, which runs for a request to
   `/admin.cgi` (i.e. a source client updating metadata), not for a listener:

   ```c
   httpp_setvar(client->parser, HTTPP_VAR_PROTOCOL, "ICY");
   ```

   It is immediately followed by `client->password = strdup(pass)`, i.e. it is part of the
   shoutcast source-authentication dance.

3. **Shoutcast source connections are normalised *into* HTTP.** In
   [`src/connection.c`](https://raw.githubusercontent.com/xiph/Icecast-Server/master/src/connection.c),
   `_handle_shoutcast_compatible()` synthesises a compliant request rather than speaking
   ICY back:

   ```c
   snprintf(http_compliant, http_compliant_len,
            "SOURCE %s HTTP/1.0\r\n%s", shoutcast_mount, client->refbuf->data);
   ...
   client->protocol = ICECAST_PROTOCOL_SHOUTCAST;
   ```

   The file's own header comment documents the shoutcast source handshake, and it is
   `Icecast -> ... sends OK2\r\n`, not an `ICY 200 OK` status line.

Also confirmed: even Icecast's *stats* handler hard-codes HTTP
(`_handle_stats_request`: `"HTTP/1.0 200 OK\r\n\r\n"`).

**Conclusion (VERIFIED against source):** a modern Icecast server answering a *listener*
uses an `HTTP/1.x 200 OK` status line. This matches the live capture in
`04-live-stream-observations.md`, where Radio Paradise sent `HTTP/1.1 200 OK` and NPO
Radio 1 sent `HTTP/1.0 200 OK`, and no sampled station sent `ICY 200 OK`.

## So what is the parser's ICY support for?

The ICY status line is the **legacy Nullsoft SHOUTcast DNAS (`sc_serv` v1) dialect**, plus
other shoutcast-lineage servers and appliances. Those servers are still deployed, and a
player that cannot parse `ICY 200 OK` will fail against them while working against every
Icecast station — a confusing, station-specific bug.

This does not weaken the recommendation; it sharpens it:

- `esp_http_client` remains the right transport, because it costs nothing extra to also
  accept the legacy dialect, and because the *headers* (`icy-metaint`, `icy-name`,
  `Icy-MetaData` opt-in) are identical across both dialects. Verified: the parser surfaces
  `icy-*` headers whether the status line is `ICY` or `HTTP`
  (`03-transport-verified-locally.md`).
- The **ICY status line is a compatibility path, not the main path**. Test fixtures must
  therefore cover both status-line forms, with the HTTP form being the common case and the
  ICY form the regression case. A test suite that only exercises `ICY 200 OK` (the
  intuitive choice, since the protocol is *called* ICY) would be testing the rarer branch.
- The metadata framing itself is what is universally called "ICY metadata"; it is carried
  inside an otherwise ordinary HTTP response. This naming mismatch is the source of much of
  the confusion in secondary write-ups.

## Not verified

- Whether any currently-operating station still sends `ICY 200 OK`. No live example was
  found (all four sampled stations sent HTTP), and the SHOUTcast DNAS source is not
  publicly available to read, so the legacy claim rests on the protocol's history and on
  the fact that client parsers (including ESP-IDF's, and FFmpeg's, whose parser this is a
  fork of) implement it deliberately rather than speculatively.
- `sc_serv` v2's behaviour under its own protocol; only Icecast's side was read.
