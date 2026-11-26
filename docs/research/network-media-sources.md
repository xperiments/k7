# Which network music sources are worth supporting

**Status:** research · **Updated:** 2026-11-20
**Conclusion:** Build Jellyfin first, Navidrome second, Emby third as a one-way import; no direct Plex or DLNA adapter.

**Scope:** research only; no source changed. Written 2026-11-14 against `649bcdd` (plus
untracked working-tree files); another lane committed `2399003` while it was being written. Every
external URL was read on 2026-11-14 unless a different date is named. Labels: **VERIFIED** = read in the cited primary source (vendor docs, source
code at a named tag, a spec); **REPORTED** = a credible third party's observation, not the
vendor's own statement; **ESTIMATE** = arithmetic with the assumption stated; **UNVERIFIED** =
could not prove, and says what was tried.

**The question.** The App can now play the broadcast and a card's files, measure a library,
author Playlists and write a Snapshot. Which modern data sources — Plex, Jellyfin, Emby,
Navidrome/Subsonic, DLNA, the NAS boxes people actually own, and the streaming services —
are worth supporting, and in what order?

**The answer in eight lines.**

1. **The App is a card composer, not a streaming client.** Its albums are its *measured* rows;
   its player plays the device's broadcast or a file on the card; the device only plays from
   SD. A media server is therefore valuable as an **intake** — download, measure, compose —
   not as a fourth live library.
2. **Jellyfin is the one to build first.** Official OpenAPI, CORS open by default, Quick
   Connect (passwordless, user-scoped), original-file download, and the largest open
   media-server audience after Plex.
3. **Navidrome / OpenSubsonic is the close second and the cheapest.** Wildcard CORS, simple
   query-parameter auth, raw download on by default, Range, and a music-first audience. Its
   revocable API-key auth is merged to `master` but not in the current release.
4. **Plex is the biggest ask and cannot be built directly.** Its server sends
   `Access-Control-Allow-Origin: https://app.plex.tv`; a third-party HTTPS origin is refused.
   Plex now publishes official API docs — the API is no longer the objection — but the CORS
   policy still is.
5. **Emby is the cheapest third adapter, not the first.** Its API is Jellyfin's ancestor, its
   audience is roughly four times Navidrome's by Docker pulls, and a 2026 response capture shows
   `Access-Control-Allow-Origin: *` — an open policy like Jellyfin's and Navidrome's, plus a
   legacy `Access-Control-Allow-Private-Network` header that LNA no longer needs. It is a day's
   work once Jellyfin exists; it is not a reason to start with it.
6. **DLNA/UPnP is not supportable from a browser.** DLNA the alliance has dissolved; SSDP has
   no browser API (W3C's discovery work was discontinued in 2017); ContentDirectory is SOAP
   with no CORS.
7. **"Boxes" are not a category.** Synology, QNAP, TrueNAS and Unraid users run
   Plex/Jellyfin/Navidrome; support the server software, not the box. Sonos, WiiM, BluOS and
   MusicCast are renderers, not libraries.
8. **Streaming services are out of scope.** They give no original bytes, they are DRM-protected,
   and their terms forbid the download-and-measure that is K7's whole value.

The rest of this note is the evidence, the browser constraints that decide it, and the shape
the integration should take.

---

## 1. The candidates, in one table

| Source | Status on 2026-11-14 | CORS from a third-party origin | Auth a browser can hold | Original bytes | Fit for K7 | Effort | Verdict |
|---|---|---|---|---|---|---|---|
| **Jellyfin** | 12.4, 2026-10-26; GPL-2.0; 59.3k stars; 465M Docker pulls | **Yes, by default** — `AllowAnyOrigin` when `CorsHosts` is empty or `*`; live probe with this origin header passed | Quick Connect (passwordless, user-scoped) or `AuthenticateByName`; `Authorization: MediaBrowser …`; API keys are **admin-level** | `GET /Items/{id}/File` (original, Range, no Download policy) and `/Audio/{id}/stream?static=true`; CORS global | High: full audio library, search, covers, sizes | M | **Build first** |
| **Navidrome / OpenSubsonic** | 0.64.2, 2026-09-24; GPL-3.0; 24.7k stars; 63M Docker pulls | **Yes** — hardcoded `*` on every route | `apiKey` is in `master`, not in 0.64.2; until then `u` + `t` + `s` with a client-fixed salt | `GET /rest/download`, raw by default; Range | High: music-first, BPM/mood fields, small footprint | M | **Build second** |
| **Plex** | PMS 1.43.5.11079, 2026-10-23; closed source; official 207-path API at developer.plex.tv; 25M+ users; 926M Docker pulls | **No** — `ACAO: https://app.plex.tv` | PIN works at plex.tv; new apps are pushed to an Ed25519 keypair (moot) | `/library/parts/…` (moot) | Highest reach, but no API access from this origin | L / blocked | **Do not build direct** |
| **Emby** | 4.10.2.0, 2026-10-20; closed source; 266M Docker pulls | **Yes, reported** — `ACAO: *` (plus a legacy `Allow-Private-Network` header); closed source, so verify on a real server | `AuthenticateByName` + `X-Emby-Token`/`?api_key=` | `/emby/Items/{id}/File`; `/emby/Audio/{id}/stream?static=true` | Medium | M | **Third, after Jellyfin and Navidrome** |
| **Ampache, Gonic, LMS** | active, smaller | per server, not in the protocol | `u`/`t`/`s` | `/rest/download` | Medium | S once Navidrome exists | Free-ish, not promised |
| **DLNA / UPnP** | DLNA dissolved; UPnP under OCF | **No** | none | file URL only | Low | L | **No** |
| **WebDAV** | standard, widely deployed | config only | Basic/Digest | `GET` | Low | M | Escape hatch |
| **NAS vendor APIs** | Synology Audio Station unofficial | unknown | session | unknown | Low | L | No |
| **Sonos, WiiM, BluOS** | renderers | n/a | n/a | no | None | — | No |
| **Volumio** | REST API with `browse?uri=` | n/a (server-side) | n/a | no (playback only) | Low | M | No |
| **Spotify, Tidal, Apple Music, YouTube** | DRM / terms | n/a | OAuth | no | None | — | No |

By Docker pulls the reach order is Plex (926M) > Jellyfin (465M) > Emby (266M) > Navidrome (63M);
the feasibility order is nearly the reverse. The recommendation in §5 is the intersection:
**Jellyfin first, Navidrome second, Emby third**.

---

## 2. What "support" means for this app

### 2.1 The App is a card composer, not a player

The App's library is not a catalog it streams from. It is the set of tracks this browser has
**measured** onto a card. `web/src/media/albums.ts` says it plainly: "No cache row, no album,
which is why the empty state says 'measure the card once'." The album index is built from
`k7-media` rows; the device supplies existence and measurements, not names and covers. The
Player area is read-only: it reports what the broadcast is carrying, and its only transport verb
is the device's `POST /api/play {path}` (`web/src/ui/areas/Player.tsx`).

The device itself only plays from the SD card in it. A track that lives only on a media server
cannot be played by the K7 hardware until the App has copied it to a card and measured it. That
is the global premise working as intended: all SD content processing happens outside the device,
and the App is the outside.

So a server integration has one natural shape:

> **connect → browse/search → choose an album, artist or playlist → download the original
> bytes into the open card → measure as they land → the existing Snapshot/Write-to-SD
> pipeline takes over.**

It is a one-way import, not a sync and not a second library. That is a feature: the card remains
the unit, the device remains the destination, and the server is a source of files the person
already owns.

### 2.2 Four possible levels, and the one worth building

| Level | What it gives | What it costs | Does it fit K7? |
|---|---|---|---|
| **Browse** | read artists/albums/tracks, search, covers | an API client and a picker | As a step inside import, yes. On its own it is a worse Plex/Jellyfin app. |
| **Play** | `<audio>` direct from the server | CORS for the UI, codecs, no measurement | A nice extra, never the point. The App is not a second player. |
| **Import** | original bytes, SHA-256, BPM/key/mood, Playlists, Snapshot, SD | download + the existing measure pipeline | **This is the feature.** It is the only level that uses what K7 uniquely does. |
| **Sync** | mirror the server library onto the card | conflict rules, deletion, a second source of truth | No. A card is a Snapshot, not a replica. |

The repo already has the seam for **Import**. `web/src/card/compose.ts` defines a `DroppedFile`
as `{ file: { stream(): ReadableStream<Uint8Array> } }`, and `copyIntoMusic` streams it into the
card's `music/`, then hands the written `File` to the queue to measure. A fetch response body is
exactly that stream. The missing pieces are the API client and the picker, not a new pipeline.
That is why the effort numbers in §1 are **M** rather than **L**: the expensive part — measuring,
tagging, caching by content hash, building `k7.db`, writing the Snapshot — is already built and
tested.

### 2.3 Why not a fourth `LibrarySource`

`web/src/library/source.ts` has two implementations, `device` and `image`, behind one `search`
seam — the spec's `list`/`stats`/`track` are not in this interface yet. A server could be a third
implementation. It should not be, for three reasons:

1. **The surface is card-scoped.** The selected card decides the source; a server library is not
   a card and has no scope, no mark, no covers in `k7.covers`, and no Snapshot rows.
2. **Unmeasured tracks are second-class by design.** They cannot be an album, cannot be a
   Playlist row, and cannot be played by the device. A live server source would show a library
   whose BPM/key/mood columns are empty and whose "play" button does nothing on the hardware.
3. **The measurement is the product.** Browsing a server is a commodity; measuring it and
   turning it into a cassette is not. Import puts the measurement in the path; a live source
   puts it off to the side.

If a read-only "listen to the server" mode is ever wanted, it should be its own small surface
with its own honest label, not a fourth source pretending to be a card.

---

## 3. The browser gate

Four constraints decide what a browser-only app can do. They are ranked by how little can be
done about them.

### 3.1 CORS decides

A cross-origin `fetch` with `Authorization`, `X-Plex-Token`, `X-Emby-Token` or
`Content-Type: application/json` is a preflight: `OPTIONS` with `Origin`,
`Access-Control-Request-Method` and `Access-Control-Request-Headers`, and the server must answer
with `Access-Control-Allow-Origin`, `-Methods` and `-Headers` ([Fetch Standard, CORS preflight](https://fetch.spec.whatwg.org/#cors-preflight-fetch)).
Without those headers the response is unreadable, whatever its status. A service worker cannot
launder it: to relay the bytes it must fetch them, and that fetch is itself subject to CORS
([Fetch Standard, HTTP fetch](https://fetch.spec.whatwg.org/#http-fetch)). There is no
browser-side workaround.

This is the single fact that separates the candidates:

- **Jellyfin** answers by default. Its `CorsPolicyProvider` calls `AllowAnyOrigin()` when
  `CorsHosts` is empty — the shipped default is `["*"]` — and always allows any method and any
  header ([`CorsPolicyProvider.cs`](https://github.com/jellyfin/jellyfin/blob/v12.4/Jellyfin.Server/Configuration/CorsPolicyProvider.cs),
  [`ServerConfiguration.cs`](https://github.com/jellyfin/jellyfin/blob/v12.4/MediaBrowser.Model/Configuration/ServerConfiguration.cs#L236)).
- **Navidrome** answers by default, and hardcodes it: `AllowedOrigins: []string{"*"}`,
  `AllowedHeaders: []string{"*"}`, `AllowCredentials: false`, applied at the root router
  ([`server/middlewares.go`](https://github.com/navidrome/navidrome/blob/v0.64.2/server/middlewares.go),
  [`server/server.go`](https://github.com/navidrome/navidrome/blob/v0.64.2/server/server.go)).
- **Plex** does not. Its server pins `Access-Control-Allow-Origin` to `https://app.plex.tv`;
  a request from another origin is refused even though the token is valid. Plex's own web client
  is the only browser client that origin policy admits (see §4.3).
- **DLNA** does not, and its SOAP `POST` with a `SOAPACTION` header is a non-simple request, so
  preflight is mandatory and fails.
- **Emby** reports an open policy too: a 2026-08-31 header capture through Caddy shows
  `Access-Control-Allow-Origin: *`, `Access-Control-Allow-Headers` naming every Emby header, and
  `Access-Control-Allow-Private-Network: true`, with an Emby staff reply treating the set as
  intentional ([Emby community](https://emby.media/community/topic/149469-can-modify-response-headers/)).
  It is closed source, so this is **REPORTED**, not source-verified; measure it on a real server
  before shipping (`§4.4`).
- **WebDAV** is a server-config question: `PROPFIND` and `Authorization` both preflight, and core
  Nextcloud does not emit CORS ([Nextcloud PR #40537](https://github.com/nextcloud/server/pull/40537)).

### 3.2 LNA and mixed content

The App already does the hard Chrome work: `fetch(url, { targetAddressSpace: 'local' })` after
`navigator.permissions.query({name: 'local-network'})` is granted. The current rules
([Local Network Access](https://wicg.github.io/local-network-access/),
[Chrome's guide](https://developer.chrome.com/blog/local-network-access)):

- Chrome enforces LNA by default from **142**; the permission splits into `local-network` and
  `loopback-network` in **145**; the enterprise opt-out is gone by **156**. The repo measured
  Chrome 156 on 2026-11-03.
- The prompt triggers on the **resolved address**, not the hostname: `192.168.1.5`, `k7.local`,
  and `192-168-1-5.<hash>.plex.direct` all resolve private and all prompt. It is scoped to the
  **top-level origin**, and it persists.
- Mixed content is exempt for a **local IP literal**, a **`.local` host**, or a fetch that
  carries `targetAddressSpace: 'local'`. So `http://192.168.1.5:8096` and `http://nas.local:4533`
  can work after the grant; `http://jellyfin.example.com` resolving private needs
  `targetAddressSpace: 'local'` set explicitly.
- **Self-signed HTTPS does not work for `fetch`.** Chrome's certificate interstitial is a
  navigation throttle; a fetch or media load to an untrusted cert fails, and there is no JS API
  to accept it. A real certificate (Let's Encrypt, or a trusted local CA in the OS store) is
  required for HTTPS servers.

LNA is not a gate for K7: it is the Chrome-only product's normal price, already paid for the
device. It is a gate for the *server's* setup: a default Jellyfin or Navidrome on plain HTTP
needs the CSP change in §3.3, and an HTTPS server needs a certificate real enough for Chrome.

### 3.3 The CSP is narrower than the browser

The deployed app sends ([`vercel.json`](../../vercel.json)):

```
connect-src 'self' https: http://k7.local data: blob:;
media-src  'self' blob: http://k7.local https:;
```

**HTTPS media servers already pass.** `connect-src https:` and `media-src https:` allow any
HTTPS origin, which is why the Invidious Instance and the weights CDN work. A Jellyfin behind a
reverse proxy with a valid certificate, or Plex's `plex.direct`, is allowed by CSP today — CSP is
not the blocker for Plex; CORS is (§4.3).

**Plain-HTTP LAN servers do not.** `http://` is allowed only for `http://k7.local`. A default
Jellyfin at `http://192.168.1.5:8096`, or Navidrome at `http://192.168.1.5:4533`, is blocked
by CSP before LNA ever prompts. Supporting them means adding `http:` to `connect-src` and
`media-src`, which permits cleartext to *any* host — a deliberate weakening of D6, the policy
whose job is to limit what injected script can reach (`docs/specs/k7-deploy.md` D6).

The clean first release is therefore **HTTPS servers only**, with a documented reverse-proxy
requirement. Widening to `http:` is a separate security decision, and the research recommendation
is not to take it in the same change as the first server adapter.

### 3.4 Credentials: the App's "no secret" meets a server token

ADR 0084 is explicit: "**The App holds no secret.** No key is stored, and no other secret
replaces it." The app even deletes the superseded Provider blob rather than leaving a key in it
(`web/src/settings.ts`). A media server needs a token. That is not a small implementation detail;
it reopens a decision the project made recently and deliberately.

The options, in order of preference:

1. **Memory-only, per session.** The token lives in a module variable, dies on reload, and the
   person reconnects. This preserves the storage half of the rule — "nothing in this origin's
   storage is a credential" — but it is still a secret the running App holds, so the ADR should
   say whether that satisfies 0084's first sentence or supersedes it. It costs a reconnect per
   session.
2. **Opt-in persistence.** IndexedDB/localStorage with an explicit "remember this server" and a
   visible "forget" action. This is how Jellyfin Web and Navidrome's own UI behave
   ([`jellyfin-apiclient`](https://unpkg.com/jellyfin-apiclient@1.11.0/dist/jellyfin-apiclient.js),
   [`ui/src/subsonic/index.js`](https://github.com/navidrome/navidrome/blob/master/ui/src/subsonic/index.js)).
   It needs an ADR that supersedes the last bullet of 0084 and says why a server token is a
   different trust relationship from a model key.
3. **Never a password at rest.** For Jellyfin use Quick Connect (a user token, no password); for
   Navidrome use its API key when released, or a client-fixed-salt derived token until then. Do
   not store the Subsonic password, and do not use `p=` (plaintext in the URL).

There is no way to keep the token out of JS: no backend means no `HttpOnly` cookie
([MDN `Set-Cookie`](https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/Set-Cookie)).
The CSP is therefore doing more work than before: a token makes any XSS worth more. It also means
the server's `Access-Control-Allow-Origin: *` is not a security boundary — the token is. A leaked
token is a leaked library, so the CSP and the "forget server" action matter as much as the API
client. That is an argument for keeping `script-src` exactly as it is and not widening anything
else in the same release.

### 3.5 Bytes, Range and codecs

A media element can play a cross-origin URL without CORS (`<audio src>` is a no-CORS request),
but **reading the bytes** — `fetch`, `response.blob()`, `crypto.subtle.digest` — needs CORS.
K7 needs the bytes: the content hash is SHA-256 of the original file, and the analyser decodes
them. A server that can only stream a transcode cannot feed the measurement pipeline.

Range matters twice: the media element sends it for seeking, and an interrupted import should
resume rather than restart. A server that answers 206 with `Content-Range` is the one to prefer.
Jellyfin, Navidrome and Emby all do.

Codecs are a smaller issue for K7 than for a video client, but not zero. The analyser's contract
is MP3 and FLAC. Chromium plays MP3, AAC, FLAC and Opus; it has no MKV demuxer and its HEVC/AC-3
support is platform-dependent ([Chromium media formats](https://chromium.googlesource.com/chromium/src/+/main/media/formats/)).
An audio library may still hold ALAC/M4A, WMA, APE, WavPack or DSD. The import picker should
either filter to what the analyser accepts or mark the rest "not measurable" rather than download
them and fail later.

---

## 4. The candidates

### 4.1 Jellyfin — build first

**Status.** Jellyfin Server **12.4**, published 2026-10-26; GPL-2.0; 59,331 stars; 465M Docker
pulls; `master` pushed 2026-11-13 ([release](https://github.com/jellyfin/jellyfin/releases/tag/v12.4),
[repo](https://api.github.com/repos/jellyfin/jellyfin),
[Docker Hub](https://hub.docker.com/v2/repositories/jellyfin/jellyfin/)). It is the free-software
successor to Emby and the largest open media server after Plex.

**API.** Official, generated from an OpenAPI document
([stable spec](https://api.jellyfin.org/openapi/jellyfin-openapi-stable.json), 12.4.0, 294
paths). 12.0 removed the legacy `/emby/` and `/mediabrowser/` prefixes, so the base is the
server root. The paths K7 needs are ordinary: `/Items` with `IncludeItemTypes=Audio`,
`Recursive`, `SearchTerm`, `StartIndex`, `Limit`; `/Items/{id}`; `/Artists` and
`/Items/{id}/Images/Primary` for covers; `/Items/{id}/File` for the original bytes;
`/Audio/{id}/stream?static=true` for inline playback. `MediaSources[].Size` gives the byte size
before a download, which is what a card capacity estimate needs, and `MediaSources[].Container`
is the codec filter (§3.5). The official API is a real advantage over the community spec a
third-party client used to depend on — and, unlike Plex's, it is meant to be used from any
client. One compatibility note: the 10.x line (last 10.11.11, 2026-06-06) still answers under
`/emby/` and accepts the legacy auth headers. Decide whether the adapter requires 12.x or carries
both base paths for one release; the latter is cheap and avoids turning a 10.11 user away.

**Auth.** Two families:

- **Quick Connect**, enabled by default: `POST /QuickConnect/Initiate` returns a short code, the
  person approves it in an already-signed-in Jellyfin session, and
  `POST /Users/AuthenticateWithQuickConnect` exchanges the secret for a user-scoped access token
  ([Jellyfin Quick Connect](https://jellyfin.org/docs/general/server/quick-connect/)). No
  password crosses to K7; the token is a user token and can be revoked.
- **Username/password** via `/Users/AuthenticateByName`, also a user token.

Use the supported header, not the legacy aliases: `Authorization: MediaBrowser Client="K7",
Device="Chrome", DeviceId="…", Version="…", Token="…"`. In 12.x the `X-Emby-Token`,
`X-Emby-Authorization`, `X-MediaBrowser-Token` and `?api_key=` forms sit behind
`EnableLegacyAuthorization`, which defaults to false; `?ApiKey=` is not gated but is not the
shape to build on ([`AuthorizationContext.cs`](https://github.com/jellyfin/jellyfin/blob/v12.4/Jellyfin.Server.Implementations/Security/AuthorizationContext.cs)).
Tokens do not expire; revoke them per device (`DELETE /Devices?deviceId=…`, `POST /Sessions/Logout`,
or the dashboard). That long life is exactly why the credential decision in §3.4 matters.

Do **not** ask for an API key. Jellyfin API keys are administrator-level and userless; they
bypass the user model and would be the most powerful credential in the user's house. Quick
Connect is the right flow, and it is the closest thing this list has to Plex's PIN flow.

**CORS and bytes.** `AllowAnyOrigin()` by default, `AllowAnyMethod()` and `AllowAnyHeader()`;
CORS is global middleware (`app.UseCors()` before routing), so it covers the file and stream
responses as well as the JSON. Measured against the public 12.4.0 demo with
`Origin: https://k7.xperiments.dev`: `GET /System/Info/Public` answered `200` with
`access-control-allow-origin: *`, and `OPTIONS /Items` with
`Access-Control-Request-Headers: authorization,x-emby-token,x-emby-authorization,range` answered
`204` with the same origin and those headers echoed in `Access-Control-Allow-Headers`. The
preflight K7 would send passes. An admin can still set `CorsHosts` to an explicit list, so the
App must treat a CORS refusal as "this server is not configured for this origin", not as a bug
in the adapter.

Use **`/Items/{id}/File`** for the import: it returns the original with
`PhysicalFile(..., enableRangeProcessing: true)`, needs no `Download` permission, and does not
write an activity entry. `/Download` also returns the original with Range, but it requires the
user's `EnableContentDownloading` policy and logs the download; skip it.
`/Audio/{id}/stream?static=true` is the inline original and honours Range too — the controller
has no `[Authorize]`, which is useful for `<audio>` and `<img>` — but leave `static=true` on:
a transcode path sets `Accept-Ranges: none` and no longer returns the original bytes.

**Metadata.** Titles, artists, album artists, album, track/disc numbers, genres, year, duration,
cover tags and `Size`; no reliable BPM/key/mood. That is fine: the import measures them, which is
the point. It also means the server's own metadata is only good enough to *pick* an album, not to
populate the measured columns.

**Friction.** The common Jellyfin setup is `http://host:8096` with a self-signed HTTPS option.
That is blocked by the CSP (§3.3) and, for HTTPS, by Chrome's certificate rule (§3.2). The
supported setups are: a reverse proxy with a real certificate, or a deliberate CSP widening to
`http:` plus an LNA grant. The first is the honest v1.

**Verdict.** **Build first.** Official API, wildcard CORS, Quick Connect, original bytes, a
large audience, and a music library that is usually well organized. Effort **M**.

### 4.2 Navidrome / OpenSubsonic — build second

**Status.** Navidrome **0.64.2**, 2026-09-24; GPL-3.0; 24,709 stars; 63.3M Docker pulls;
`master` pushed 2026-11-13 ([release](https://github.com/navidrome/navidrome/releases/tag/v0.64.2),
[repo](https://api.github.com/repos/navidrome/navidrome),
[Docker Hub](https://hub.docker.com/r/deluan/navidrome)). It is music-first in a way Jellyfin is
not: small, tag-driven, built around the Subsonic/OpenSubsonic API rather than a general media
library.

**API.** The OpenSubsonic spec, a set of extensions to the frozen Subsonic API, version 1
([API index](https://opensubsonic.github.io/docs/opensubsonic-api/)). The paths: `getArtists`,
`getArtist`, `getAlbum`, `getAlbumList2` (paged, max 500), `getSong`, `search3`, `getCoverArt`,
`stream`, `download`. There is no `getSongs`; `search3` with an empty query is the
specification-sanctioned full-catalogue enumeration
([`search3`](https://opensubsonic.github.io/docs/endpoints/search3/)). IDs are opaque strings;
treat them as such.

**Auth.** The token scheme is `u`, `t = md5(password + salt)`, `s`, plus `v` and `c`
([Navidrome's middleware](https://github.com/navidrome/navidrome/blob/v0.64.2/server/subsonic/middlewares.go)).
The salt is client-chosen and not replay-protected, so K7 can pick one salt at pairing time and
store only `md5(password + salt)` — a self-minted, password-equivalent token. That is better than
storing the password, but it is not revocable except by changing the password, it travels in the
query string, and it lands in server logs. The proper fix is OpenSubsonic's
`apiKeyAuthentication` extension ([spec](https://opensubsonic.github.io/docs/extensions/apikeyauth/)):
`apiKey=<key>`, no `u`, user-scoped, revocable.

As of 2026-11-14 that extension is **merged to Navidrome `master`** — the `getOpenSubsonicExtensions`
list on `master` includes `apiKeyAuthentication`, and the auth middleware implements it — but it
is **not in 0.64.2**, the current release ([`master`](https://github.com/navidrome/navidrome/blob/master/server/subsonic/opensubsonic.go),
[`master` auth](https://github.com/navidrome/navidrome/blob/master/server/subsonic/middlewares.go)).
Plan for it; do not depend on it in the first release.

**CORS and bytes.** `AllowedOrigins: ["*"]`, `AllowedHeaders: ["*"]`, `AllowCredentials: false`,
applied at the root, so `/rest/*` is readable from any origin
([`server/middlewares.go`](https://github.com/navidrome/navidrome/blob/v0.64.2/server/middlewares.go)).
`download` is enabled by default, `AutoTranscodeDownload` is false, so it returns the original
bytes; `Stream.Serve` uses `http.ServeContent` and honours Range
([`stream.go`](https://github.com/navidrome/navidrome/blob/v0.64.2/server/subsonic/stream.go),
[`media_streamer.go`](https://github.com/navidrome/navidrome/blob/v0.64.2/core/stream/media_streamer.go),
[`configuration.go`](https://github.com/navidrome/navidrome/blob/v0.64.2/conf/configuration.go)).
A browser can fetch and hash it.

**Metadata.** `getSong` carries `genre`, and the OpenSubsonic extensions add `bpm` and `moods`;
Navidrome fills them from tags when present. There is no musical-key field. Most libraries will
have neither BPM nor mood, which is exactly the gap K7 fills.

**Verdict.** **Build second.** It is the closest to K7's audience, the cheapest API, and the
CORS/download story is proven at source level. Its auth is the one weakness, and the next release
should include the API-key fix. Effort **M**, less once the Jellyfin picker exists.

### 4.3 Plex — the biggest ask, and the one that cannot be built directly

**Status.** Plex Media Server **1.43.5.11079**, 2026-10-23, closed source, monthly point releases;
Plex states **25M+ users**; 926M `plexinc/pms-docker` pulls plus 910M `linuxserver/plex` — the
official image alone is about twice Jellyfin's — and the largest consumer media-server audience
([downloads API](https://plex.tv/api/downloads/5.json), [about](https://www.plex.tv/about/),
[Docker Hub](https://hub.docker.com/v2/repositories/plexinc/pms-docker/)). Plex now publishes
**official** PMS API documentation at [developer.plex.tv](https://developer.plex.tv/pms/) — an
OpenAPI 3.1 document with 207 paths, linked from plex.tv's own navigation. The old objection that
"the Plex API is unofficial" is stale. The docs describe `X-Plex-Token` header/query auth, the
`plex.direct` connection template, and a modern Plex-account device flow. The API is not the
problem.

**The problem is CORS.** Plex's server sends `Access-Control-Allow-Origin: https://app.plex.tv`
and does not provide an allowlist for other origins. A third-party app gets a CORS refusal on a
valid request. Three independent captures say the same thing: a Vercel-hosted client against
`https://<ip>.plex.direct:32400/library/sections` — "The 'Access-Control-Allow-Origin' header has
a value 'https://app.plex.tv' that is not equal to the supplied origin"; a `file://` client
against a 1.41 server — "contains multiple values 'https://app.plex.tv, *', but only one is
allowed"; and, in a 2024 pre-LNA report, Plex Web itself failing a preflight against a private
`plex.direct` address for lack of `Access-Control-Allow-Private-Network`
([plexy #4](https://github.com/ricoloic/plexy/issues/4),
[Plex forum](https://forums.plex.tv/t/bug-cors-error-on-htpc-player-on-linux/886531/14),
[Plex Web thread](https://forums.plex.tv/t/plex-web-local-ip-pms-cors-issues/900023)). The policy
is the fix for the 2020 Tenable "weak CORS" advisory
([TRA-2020-35](https://www.tenable.com/security/research/tra-2020-35)), and it is still in force:
a 2026-02 client release still reports "CORS issues with local plex server queries"
([pasta 2.2.5](https://newreleases.io/project/github/cglatot/pasta/release/2.2.5)). Plex's
official API docs contain no occurrence of "CORS" and no browser-origin exemption.

**The 2026 auth direction is a second wall.** Plex documents the legacy PIN — which works, and
whose `plex.tv` side is CORS-open — but says new apps should use a JWT flow: an Ed25519 keypair,
the public half posted as a JWK, seven-day tokens, and refreshes signed by the device's **private
key**. Registering a JWK makes the legacy token expire. A private key that must be persisted is
exactly the secret ADR 0084 forbids; even if Plex's CORS opened up, K7 would have to decide
whether to store one. Plex's Terms of Service also grant a personal, non-commercial, revocable
licence and forbid using the Plex solution to build a service that "performs substantially the
same functionality" — a clause a third-party Plex client should not ignore
([ToS](https://www.plex.tv/about/privacy-legal/plex-terms-of-service/)).

`plex.direct` does solve the *transport*: the hostname resolves to the LAN address and carries a
valid Let's Encrypt certificate, so an HTTPS page can reach it and LNA prompts once
([Plex secure connections](https://support.plex.tv/articles/206225077-how-to-use-secure-server-connections/)).
It does not solve CORS. The hostname needs public DNS; a local resolver that blocks rebinding can
break it, and Plex documents the dnsmasq/pfSense workaround. A bare `<audio>` element might play a
known part URL opaquely, but opaque means no bytes for SHA-256 and no samples for BPM/key — play
at best, never import.

**What would make Plex supportable.** Any one of:

- Plex adds a CORS allowlist (or allows the App's origin), or an official browser-client
  registration with a CORS policy. Not in the project's control.
- K7 gains a **native or companion** component that is not subject to browser CORS. That is a
  different product, and it contradicts the App's one-artifact shape (`k7-app.md` K1) and its
  direct transport (ADR 0067).
- A **local CORS proxy** the user runs. It works, but it is a second thing to install, it holds
  the token, and it is exactly the kind of "just add a proxy" that turns a one-click app into a
  support burden. Do not ship it as the Plex answer.

**What to tell a Plex user today.** Mount the Plex library's folder (a network share is fine)
and use the existing **Folder** source; the App will scan and measure it. Or run Jellyfin or
Navidrome on the same files in Docker; both are supported and neither asks them to move the
library. This is honest, and it is not "Plex support".

**Verdict.** **Do not build a direct Plex adapter.** It is the obvious ask and the one that fails
at the browser boundary. Revisit only if Plex changes its CORS policy or the product gains a
native companion.

### 4.4 Emby — the cheap third

**Status.** Emby Server **4.10.2.0**, 2026-10-20, closed source, proprietary; 266M
`emby/embyserver` pulls plus 46M `linuxserver/emby` — more than Navidrome, less than Jellyfin.
The server is developed by Emby LLC, with a Premiere subscription ($4.99/month, $119 lifetime,
30-device household cap) that does not gate the API. Its API is the ancestor of Jellyfin's:
`/emby` base path, `/Items` with the same `IncludeItemTypes`, `/Users/AuthenticateByName`,
`X-Emby-Authorization` on login, `X-Emby-Token` or `?api_key=` afterwards, and
`/emby/Items/{id}/File` or `/emby/Audio/{id}/stream?static=true` for the original
([official Swagger](https://raw.githubusercontent.com/MediaBrowser/Emby.SDK/master/Documentation/Download/openapi_v2_noversion.json),
[REST docs](https://dev.emby.media/reference/RestAPI.html)). It is most of a Jellyfin adapter
with a different base path and header name.

**CORS.** A 2026-08-31 capture through Caddy shows `Access-Control-Allow-Origin: *`,
`Access-Control-Allow-Methods` covering the verbs, `Access-Control-Allow-Headers` naming
`Authorization`, `Range`, `X-MediaBrowser-Token`, `X-Emby-Token`, `X-Emby-Authorization` and
friends, and a legacy `Access-Control-Allow-Private-Network: true`; an Emby staff reply treats the
set as intentional ([Emby community](https://emby.media/community/topic/149469-can-modify-response-headers/)).
Emby is closed source, so this is **REPORTED**, not read from code; it is recent and consistent
with Emby's hosted web client at `app.emby.media`. The private-network header belongs to the
paused PNA design; LNA replaces it with a permission, so it is history, not an advantage. Measure
the policy on a real server before shipping.

**Friction.** Media endpoints require auth, so `<audio>`/`<img>` must use `?api_key=` (they cannot
set a header). The 30-device Premiere cap counts client devices; whether a new `DeviceId` per K7
install consumes a slot is **UNVERIFIED** and worth testing. The API base is `/emby`, which
differs from Jellyfin's root.

**Verdict.** **Third, after Jellyfin and Navidrome.** It is a cheap adapter once the Jellyfin
client exists, its audience is larger than Navidrome's, and its CORS is as open as theirs. It is
not a reason to start, and its closed source means the CORS evidence needs a live check.

### 4.5 Other Subsonic servers — nearly free, once Navidrome exists

OpenSubsonic is a protocol, not a permission. Supporting Navidrome's `/rest/*` path makes
**Ampache** (AGPL-3.0, active), **Gonic** (GPL-3.0, active) and **LMS/Lyrion** (GPL-3.0, active,
Subsonic via plugin) candidates for the same adapter. **Airsonic-Advanced** is effectively dormant
(last push 2024-04-24), so "free support" there is theoretical. **Koel** is not Subsonic at all
and would need its own adapter. CORS is not part of the spec, so each server must be probed; do
not promise the long tail.

**Verdict.** **Opportunistic.** The adapter is the same; the support matrix is a probe, not a
promise.

### 4.6 DLNA / UPnP — no

**Status.** DLNA the alliance has dissolved; its certification and testing are now provided by
SpireSpark with permission ([dlna.org](https://www.dlna.org/)). UPnP lives on under the Open
Connectivity Foundation, which took the Forum's assets in 2016; the Device Architecture 2.0 and
ContentDirectory v1–v3 specs are still published
([OCF](https://openconnectivity.org/developer/specifications/upnp-resources/upnp/),
[UDA 2.0](https://upnp.org/specs/arch/UPnP-arch-DeviceArchitecture-v2.0.pdf),
[ContentDirectory v3](https://upnp.org/specs/av/UPnP-av-ContentDirectory-v3-Service.pdf)). So
"DLNA" is a legacy label on a still-deployed protocol, not a current certification to target.

**Why a browser cannot use it.** Discovery is SSDP over UDP multicast; the web platform has no
datagram socket, and the W3C's Network Service Discovery work — the only spec that would have
given a document access to SSDP/mDNS services — was discontinued in 2017 and says it "should not
be referenced or used as a basis for implementation"
([N-DISCO](https://www.w3.org/TR/discovery-api/)). Manual URL entry is the only discovery. Then
ContentDirectory is a SOAP `POST` with a `SOAPACTION` header; that is a non-simple request, so
preflight is mandatory, and DLNA servers do not emit `Access-Control-Allow-Origin`. The protocol
typically has no auth, so a granted page could enumerate a LAN's libraries with no credential —
a worse security posture than a token, not a better one.

**Verdict.** **No.** It is a month of work for a capability that mostly fails, and K7's own
device already provides the one DLNA-like role the app controls end to end.

### 4.7 WebDAV — an escape hatch, not a feature

WebDAV is HTTP, so `PROPFIND` and `GET` are possible; `PROPFIND` and `Authorization` are
non-simple and need preflight. CORS is not in RFC 4918 and is entirely a server configuration:
core Nextcloud does not ship it (a configurable-CORS PR is still open), Apache `mod_dav` needs
`mod_headers` to add it, and rclone's WebDAV server has no CORS option
([Nextcloud #40537](https://github.com/nextcloud/server/pull/40537),
[Apache mod_dav](https://httpd.apache.org/docs/2.4/mod/mod_dav.html),
[rclone WebDAV](https://rclone.org/webdav/)). Auth is Basic/Digest, which means the user's NAS
password in the tab — the same credential decision as §3.4, with a broader blast radius (all
files, not just music).

**Verdict.** **An escape hatch for power users**, if a generic "NAS with no media server" answer
is wanted. Not the headline, and not before Jellyfin/Navidrome.

### 4.8 The boxes — NAS, Sonos, WiiM, Volumio

**NAS.** Synology, QNAP, TrueNAS and Unraid do not have a common music API. Synology's
`SYNO.AudioStation.*` is community-reverse-engineered, not a stable public contract; QNAP's Music
Station is similar; TrueNAS and Unraid have no first-party music server at all. What people
actually run on these boxes is **Plex, Jellyfin or Navidrome in Docker**. That is the category:
support the server software, not the box. A Synology user with Audio Station and no Jellyfin is
better served by the Folder source or by installing Jellyfin.

**Players.** Sonos is a renderer with a cloud control API and no official third-party local browse
API; WiiM is DLNA/AirPlay/Chromecast, also a renderer; Bluesound's BluOS API page returned 404
and Yamaha's MusicCast developer page did not resolve in this pass; **Volumio** is the exception,
with a documented REST `browse?uri=` that can read a library
([Volumio REST](https://developers.volumio.com/api/rest-api)). None of them gives K7 the original
bytes to hash, and K7 does not need playback control — it has its own device and its own player.

**Verdict.** **No vendor-specific box support.** The server software is the integration point;
the box is hardware.

### 4.9 Streaming services — not data sources

Spotify has a public Web API, but its developer policy permits streaming only for Premium
subscribers, forbids integrating streams from another service, and its playback SDK delivers
DRM-protected audio, not fetchable files ([Spotify policy](https://developer.spotify.com/policy)).
YouTube's API terms grant no right to reproduce or distribute content other than through the API
([YouTube API ToS](https://developers.google.com/youtube/terms/api-services-terms-of-service)).
Tidal is OAuth/SDK, not a byte source; Apple Music is MusicKit + FairPlay; Bandcamp publishes no
public music API. None permits downloading and analysing the original file.

**Verdict.** **Out of scope** unless the product stops being "measure and compose" and becomes
"remote-control a player". That is a different product, and it is not this one.

---

## 5. Recommendation

### 5.1 Order and scope

**Do Jellyfin first, Navidrome second, Emby third — one intake, three adapters.**

The intake, in order:

1. **Connect.** A server URL and a credential. Jellyfin: Quick Connect. Navidrome: API key when
   released, derived token before. Emby: `AuthenticateByName` + `X-Emby-Token` (or `?api_key=`
   for the media elements). Store the token in memory by default (§3.4).
2. **Browse/search.** Artists → albums → tracks; a search box; covers. This is the picker, not a
   new library surface.
3. **Choose.** An album, an artist's albums, a server playlist, or a multi-select. Show the
   server's `Size` for the selection before downloading; compare it with the card's free space.
4. **Download.** `GET` the original file per track, stream it into the open card's
   `music/Artist/Album/…`, and let `copyIntoMusic`'s existing stream path do the write. Resume
   with `Range` after an interruption; never leave a half file measured as whole.
5. **Measure.** The existing queue measures each file as it lands, tags it, caches by content
   hash, and updates `k7.db`/`k7.covers`. Nothing in the measure pipeline changes.
6. **Finish.** The card is now a normal card: Playlists, Snapshot, Write to SD. The server is no
   longer involved.

**Why this order.** Jellyfin has the larger audience and the official API; it is the reference
adapter. Navidrome is the music-first audience and the cheaper API, and its next release makes
the credential clean. Emby is the cheapest third — a day's work once Jellyfin exists, and its
reported headers are the friendliest to LNA — but its closed source means the CORS capture needs
a live check first. WebDAV waits for demand; Plex and DLNA are not on the list. If reach matters
more than fit, swap Emby and Navidrome; the code is nearly the same either way.

### 5.2 What not to build

- **A direct Plex adapter.** It cannot read the API from this origin. A proxy is a second
  product; a native shell contradicts the one-artifact App and ADR 0067's direct transport. Say
  no, and offer the Folder/Jellyfin workaround.
- **A DLNA/UPnP client.** No SSDP, no CORS, no auth, a dissolved certification body.
- **Vendor NAS APIs** (Synology Audio Station, QNAP Music Station). Unofficial, narrow, and the
  same users can run Jellyfin/Navidrome.
- **Renderer integrations** (Sonos, WiiM, BluOS, MusicCast). They do not give bytes, and K7 does
  not need a remote control.
- **Streaming services.** DRM and terms; no original bytes.
- **A live server `LibrarySource`.** It would be a second-class library with no measurements and
  no device playback (§2.3).

### 5.3 If Plex support is a hard requirement

Then the honest options, in order:

1. **Mount the Plex library as a folder and use the Folder source.** Works today; no new code;
   the user picks the mounted share.
2. **Run Jellyfin or Navidrome on the same files.** Both read the same directory tree; the
   library does not move.
3. **A local CORS proxy.** Possible, but it is a second install, it holds the token, and it
   widens the attack surface. Not recommended as the product's answer.
4. **A native/companion app.** A different product with a different packaging and trust story.

If none of those is acceptable, the answer is "Plex is not supported", not "Plex support is
coming".

---

## 6. What it costs, and what it changes

### 6.1 One ADR, for the credential

The server token is the only genuinely new trust decision. The repo's current answer is "no
secret" (ADR 0084, X4). The recommendation is **memory-only by default**, with an explicit,
opt-in persistence decision recorded in a new ADR if the reconnect friction proves too high. That
ADR should:

- supersede the last bullet of ADR 0084 (not rewrite it — accepted ADRs are superseded);
- say which credentials are allowed (user-scoped tokens, not admin API keys, not passwords);
- say where they live (memory first, IndexedDB only with an explicit "remember");
- say how they are revoked (a visible "forget server", and the server's own revocation);
- repeat that the CSP is now protecting a token, so `script-src` does not widen.

### 6.2 The CSP and the HTTPS boundary

A first release that supports **HTTPS servers only** needs **no CSP change**: `connect-src https:`
and `media-src https:` already allow any HTTPS origin. That is the cleanest boundary, and it
matches the app's existing trust model. It does mean the Jellyfin/Navidrome/Emby setup
instructions must say "put it behind a reverse proxy with a real certificate" — a real ask, but a
one-time one.

If plain-HTTP LAN servers are a must, the change is `http:` in `connect-src` and `media-src`, plus
the LNA grant. That is a deliberate weakening of D6 and should be its own decision, not a line
snuck into the server PR. The research recommendation is to defer it.

One more spec sentence needs a scope: X2 says "The App fetches `http://k7.local` and nothing
else." The App already fetches the Invidious Instance and the weights CDN; a server source adds a
**user-chosen** third-party origin. The sentence should say what it means — the device origin is
the only *device* origin — rather than be quietly falsified.

### 6.3 The import UX and the card's capacity

The server gives `Size` before download, so the picker can say "this album is 412 MB; the card has
2.1 GB free" before a byte moves. A full-library import is a different animal: 500 FLAC tracks at
~30 MB is ~15 GB (**ESTIMATE**), and the card's capacity is the limit the Virtual card already
models. The import should:

- refuse a selection larger than the card's free space, rather than fill it and fail;
- measure as files land, not after the whole download (the queue already supports this);
- show the two numbers separately — downloaded and measured — because a download that finishes
  is not a run that finishes;
- treat a partial file as not-a-track: delete it or resume it, never measure it.

The throughput estimate is unremarkable: the analyser is ~2 s per 4-minute track on this machine
at 8 workers, so 500 tracks is ~17 minutes of measurement (**ESTIMATE**; the download is
LAN-bound). The bottleneck is the person's patience, not the pipeline.

### 6.4 Maintenance

Three adapters, three API surfaces if all three ship. Jellyfin's OpenAPI is versioned and
official; Navidrome's OpenSubsonic is stable and small; Emby's is the ancestor of Jellyfin's and
well documented. The maintenance cost is real but bounded, and it is the same kind of cost the
app already pays for the device's API. Plex would be a fourth, undocumented-in-practice surface
with a CORS policy that can change without notice; that is a reason not to take it on.

---

## 7. What to measure before committing

1. **CORS, on a real server, from the deployed origin.** Jellyfin 12.4: preflight
   `POST /Users/AuthenticateByName`, `GET /Items/{id}/File`, and `GET /Audio/{id}/stream?static=true`;
   record `ACAO`, `ACAM`, `ACAH`, and the 206/`Content-Range`. Navidrome 0.64.2 and `master`: the
   same for `/rest/download` and `/rest/stream`, and the `apiKey` route on `master`. Emby 4.10:
   the preflight for `/emby/Items/{id}/File` and the `?api_key=` media URL.
2. **Quick Connect end to end.** Initiate, approve in a separate Jellyfin tab, exchange, and a
   user-scoped token that is accepted by `/Items`. Confirm it works from the installed PWA, not
   only a browser tab.
3. **Original bytes + hash.** Fetch a FLAC through `/Items/{id}/File` (Jellyfin),
   `/rest/download` (Navidrome), or `/emby/Items/{id}/File` (Emby); hash it; compare with the same
   file on disk. Confirm the server did not transcode (size and bytes equal).
4. **A 100-track import.** Time download, measure, tag and `k7.db`; the numbers that matter are
   per-track seconds and the failure modes (a skipped file, a dropped connection, a full card).
5. **The credential threat model.** A token in memory only; what a reload costs; what an XSS
   would get; whether the CSP still contains it.
6. **Plex and Emby, re-checked.** A current PMS's `ACAO` from a non-Plex origin; a current Emby's
   preflight. If either has changed, the ranking changes with it.

---

## 8. Sources

**Repo (VERIFIED by reading this checkout):** `docs/specs/k7-app.md` §2, §4, §8 (X2, X4);
`docs/specs/k7-deploy.md` D6; `docs/adr/0084-the-assistant-is-the-browsers-built-in-model.md`;
`vercel.json` (CSP); `web/src/card/compose.ts`; `web/src/library/source.ts`;
`web/src/media/albums.ts`; `web/src/ui/playback.tsx`; `web/src/ui/areas/Player.tsx`;
`web/src/settings.ts`.

**Jellyfin:** [12.4 release](https://github.com/jellyfin/jellyfin/releases/tag/v12.4) ·
[repo metadata](https://api.github.com/repos/jellyfin/jellyfin) ·
[Docker Hub](https://hub.docker.com/v2/repositories/jellyfin/jellyfin/) ·
[CORS policy at v12.4](https://github.com/jellyfin/jellyfin/blob/v12.4/Jellyfin.Server/Configuration/CorsPolicyProvider.cs) ·
[default `CorsHosts` at v12.4](https://github.com/jellyfin/jellyfin/blob/v12.4/MediaBrowser.Model/Configuration/ServerConfiguration.cs#L236) ·
[`/File` and `/Download` Range](https://github.com/jellyfin/jellyfin/blob/v12.4/Jellyfin.Api/Controllers/LibraryController.cs) ·
[static stream Range](https://github.com/jellyfin/jellyfin/blob/master/Jellyfin.Api/Helpers/FileStreamResponseHelpers.cs) ·
[12.x authorization scheme](https://github.com/jellyfin/jellyfin/blob/v12.4/Jellyfin.Server.Implementations/Security/AuthorizationContext.cs) ·
[12.0 release notes](https://github.com/jellyfin/jellyfin.org/blob/master/blog/2026/09-07-jellyfin-release-12.0/index.mdx) ·
[OpenAPI stable](https://api.jellyfin.org/openapi/jellyfin-openapi-stable.json) ·
[Quick Connect](https://jellyfin.org/docs/general/server/quick-connect/) ·
[Jellyfin Vue, cross-origin browser proof](https://github.com/jellyfin/jellyfin-vue) ·
[API-key posture, REPORTED](https://github.com/magnus919/agent-skills/blob/main/jellyfin/references/auth-and-sessions.md).

**Navidrome / OpenSubsonic:** [0.64.2 release](https://github.com/navidrome/navidrome/releases/tag/v0.64.2) ·
[repo metadata](https://api.github.com/repos/navidrome/navidrome) ·
[Docker Hub](https://hub.docker.com/r/deluan/navidrome) ·
[CORS middleware](https://github.com/navidrome/navidrome/blob/v0.64.2/server/middlewares.go) ·
[root router](https://github.com/navidrome/navidrome/blob/v0.64.2/server/server.go) ·
[auth middleware](https://github.com/navidrome/navidrome/blob/v0.64.2/server/subsonic/middlewares.go) ·
[download/stream](https://github.com/navidrome/navidrome/blob/v0.64.2/server/subsonic/stream.go) ·
[`http.ServeContent`](https://github.com/navidrome/navidrome/blob/v0.64.2/core/stream/media_streamer.go) ·
[`apiKeyAuthentication` in `master`](https://github.com/navidrome/navidrome/blob/master/server/subsonic/opensubsonic.go) ·
[`master` auth](https://github.com/navidrome/navidrome/blob/master/server/subsonic/middlewares.go) ·
[OpenSubsonic API](https://opensubsonic.github.io/docs/opensubsonic-api/) ·
[`search3`](https://opensubsonic.github.io/docs/endpoints/search3/) ·
[`apiKeyAuthentication`](https://opensubsonic.github.io/docs/extensions/apikeyauth/).

**Plex:** [official PMS API](https://developer.plex.tv/pms/) ·
[downloads API 1.43.5](https://plex.tv/api/downloads/5.json) ·
[25M+ users](https://www.plex.tv/about/) ·
[ToS](https://www.plex.tv/about/privacy-legal/plex-terms-of-service/) ·
[Plex secure connections](https://support.plex.tv/articles/206225077-how-to-use-secure-server-connections/) ·
[Plex forum CORS log, REPORTED](https://forums.plex.tv/t/plex-htpc-cant-connecto-to-server/884088/6) ·
[multiple-ACAO capture, REPORTED](https://forums.plex.tv/t/bug-cors-error-on-htpc-player-on-linux/886531/14) ·
[Plex Web PNA failure, REPORTED](https://forums.plex.tv/t/plex-web-local-ip-pms-cors-issues/900023) ·
[plexy #4, REPORTED](https://github.com/ricoloic/plexy/issues/4) ·
[pasta 2.2.5, REPORTED](https://newreleases.io/project/github/cglatot/pasta/release/2.2.5) ·
[Tenable TRA-2020-35](https://www.tenable.com/security/research/tra-2020-35) ·
[Docker Hub](https://hub.docker.com/v2/repositories/plexinc/pms-docker/).

**Emby:** [4.10.2.0 release](https://github.com/MediaBrowser/Emby.Releases/releases) ·
[official Swagger](https://raw.githubusercontent.com/MediaBrowser/Emby.SDK/master/Documentation/Download/openapi_v2_noversion.json) ·
[REST API docs](https://dev.emby.media/reference/RestAPI.html) ·
[API-key auth](https://dev.emby.media/reference/RestAPI/API-Key-Authentication.html) ·
[CORS header capture + staff reply, REPORTED](https://emby.media/community/topic/149469-can-modify-response-headers/) ·
[Docker Hub](https://hub.docker.com/v2/repositories/emby/embyserver/).

**DLNA / UPnP:** [DLNA dissolution](https://www.dlna.org/) ·
[OCF UPnP](https://openconnectivity.org/developer/specifications/upnp-resources/upnp/) ·
[UDA 2.0](https://upnp.org/specs/arch/UPnP-arch-DeviceArchitecture-v2.0.pdf) ·
[ContentDirectory v3](https://upnp.org/specs/av/UPnP-av-ContentDirectory-v3-Service.pdf) ·
[W3C N-DISCO, discontinued](https://www.w3.org/TR/discovery-api/).

**WebDAV:** [rclone WebDAV](https://rclone.org/webdav/) · [Apache `mod_dav`](https://httpd.apache.org/docs/2.4/mod/mod_dav.html) ·
[Nextcloud CORS PR](https://github.com/nextcloud/server/pull/40537).

**Boxes / streaming:** [Volumio REST](https://developers.volumio.com/api/rest-api) ·
[Spotify developer policy](https://developer.spotify.com/policy) ·
[YouTube API terms](https://developers.google.com/youtube/terms/api-services-terms-of-service).

**Browser platform:** [Local Network Access](https://wicg.github.io/local-network-access/) ·
[Chrome LNA guide](https://developer.chrome.com/blog/local-network-access) ·
[Chrome Status](https://chromestatus.com/feature/5152728072060928) ·
[Fetch Standard](https://fetch.spec.whatwg.org/) ·
[HTML media](https://html.spec.whatwg.org/multipage/media.html) ·
[Web Audio](https://webaudio.github.io/web-audio-api/) ·
[Web Crypto](https://w3c.github.io/webcrypto/) ·
[Chromium media formats](https://chromium.googlesource.com/chromium/src/+/main/media/formats/).
