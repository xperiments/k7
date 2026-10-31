# WebDAV: what actually mounts, what it costs, and whether it is worth it here

**Status:** research only; no source changed. Written 2026-10-30. Repository at
`12c83c5`. The `method[8]` table in §1.1 was measured against this commit's host build.

> **Rejected 2026-10-31.** The global premise in `docs/specs/CONTEXT.md` forbids serving a
> Track, and a mount hands out Tracks by definition, so WebDAV is out of scope. This note is
> kept for its evidence about HTTP, clients and cost, not as a plan.

**Question:** the only way to get music off this device today is the HTML index at `/`
plus one `/music/<path>` download per file. The user wants to **"add webdav"** so the
card appears as a network drive in Finder, Windows Explorer, rclone, Cyberduck,
davfs2 and phone apps. What is the smallest WebDAV surface that actually mounts on
those clients, does a read-only class-1 server mount cleanly or degrade, and what would
this firmware have to grow — versus the uglier alternatives?

**Verdict, in ten lines:**

1. **The mounting surface is four methods and one header:** `OPTIONS` (`DAV: 1`,
   `Allow`, `MS-Author-Via`), `PROPFIND` (`Depth: 0` and `1`, 207 multistatus, a few
   live properties), `GET`, `HEAD`. `LOCK`/`UNLOCK`/`PUT`/`MKCOL`/`DELETE`/`PROPPATCH`
   exist only to decide whether the mount is read-only or is a server that lies about
   being writable.
2. **Class 1 mounts read-only cleanly, and macOS says so in writing.**
   `mount_webdav(8)` forces `rdonly` because it "will not allow files to be opened with
   write access on servers which do not support the DAV LOCK method". Read-only is the
   documented outcome on macOS, not a degraded one.
3. **Advertising `DAV: 1, 2` without `LOCK` is the field's most common lie.**
   `golang.org/x/net/webdav` does it unconditionally; so does the surviving ESP8266
   server. It buys nothing on macOS — the gate there is the `LOCK` method, not the
   header token — and costs a Windows client one failed `LOCK`.
4. **This firmware cannot be asked the question yet.** `char method[8]` plus its guard
   reject `PROPFIND` (8 chars) and `PROPPATCH` (9) with **400 before routing**,
   MEASURED (§1.1). Separately, **all 12 route slots are taken.**
5. **`PROPFIND` must read the library tree, not FAT.** The tree is a resident arena
   with no SD I/O; a FAT walk costs a directory read plus a stat per entry per request,
   and `Depth: 1` fan-out makes that the hot path.
6. **`getlastmodified` is the one property the tree cannot supply**, and FAT answers it
   badly: 2-second resolution, local time with no timezone, and `fstat()` on an open fd
   reports `st_mtime = 0`.
7. **A 207 must be `Content-Length`'d** because this server cannot chunk. That forces a
   two-pass measure-then-stream over the arena — the shape `build_json()` already uses
   to size the JSON view before filling it.
8. **WebDAV buys browsing and file-at-a-time pulls, not "copy the card".** One download
   at a time (503 to the second), no `Range`, one connection per request, ~250 KB/s
   alone on the card: ~11 h for 10 GB versus ~15 minutes of carrying the card to a
   laptop.
9. **The cheapest correct implementation** is 2 new routes, a wider method buffer, a 207
   branch in the reason table, one streaming XML writer and a date formatter — under 900
   lines, no allocation, no new dependency.
10. **Do Windows last and expect it to fail.** `BasicAuthLevel` defaults to `1` (Basic
    auth over SSL only); the rest of the Windows story is a 60-second negative cache and
    a 50 MB per-file ceiling.

Labels: **VERIFIED** = read in the repository or the primary source cited on the line;
**ESTIMATE** = derivation with the assumption stated; **MEASURED** = produced by a
command given here; **UNVERIFIED** = could not confirm, and said so.

---

## 1. What the code does today, and what blocks a WebDAV request

### 1.1 The parser rejects the method before anything looks at it

`http_request_t.method` is `char method[8]`
([`http_server.h:42`](../../../components/portable/http_server/include/http_server.h#L42)),
copied only after `if (strlen(buf) == 0 || strlen(buf) >= sizeof req->method) return 400;`
([`http_parse.c:53`](../../../components/portable/http_server/http_parse.c#L53)). The
guard is `>=`, so an 8-character method does not fit. Measured on the host build by
linking the real `http_parse.o` against a 20-line probe, one request per method, target
`/music/a.mp3`, `Host: x`:

```sh
cc -std=gnu11 -I host/include -I components/portable/http_server/include \
   -o /tmp/probe probe.c host/build/obj/http_parse.o && /tmp/probe
```

| Method | Chars | Parser status |
|---|---:|---:|
| `GET` `HEAD` `OPTIONS` `PUT` `DELETE` `MKCOL` `COPY` `MOVE` `LOCK` `UNLOCK` | 3–7 | 0 (accepted) |
| **`PROPFIND`** | 8 | **400** |
| **`PROPPATCH`** | 9 | **400** |

**MEASURED.** The method is otherwise copied verbatim and compared case-insensitively
by `http_route_match_method()`
([`http_parse.c:160-175`](../../../components/portable/http_server/http_parse.c#L160-L175)),
so widening the buffer is the whole change — the router is already method-generic. This
is the same class of blocking fact as s13's "the HTTP server cannot receive an MCP
request" ([`s13:110-123`](s13-library-tree-and-mcp.md#L110-L123)) and should be stated
as plainly.

### 1.2 The route table is exactly full, and routing is already per-method

`HTTP_MAX_ROUTES` is 12
([`http_server.h:37`](../../../components/portable/http_server/include/http_server.h#L37))
and registration returns `ESP_ERR_NO_MEM` past the cap
([`http_server.c:159`](../../../components/portable/http_server/http_server.c#L159)).
Every slot is used: `/stream`, `POST /api/play`
([`mini_icecast.c:1400-1402`](../../../components/portable/mini_icecast/mini_icecast.c#L1400-L1402)),
`/music/`, `/`
([`mini_files.c:477-478`](../../../components/portable/mini_files/mini_files.c#L477-L478)),
`/api/library.bin`, `/api/library`
([`library_http.c:93-94`](../../../components/portable/library_http/library_http.c#L93-L94)),
`POST|GET|DELETE /mcp`
([`mcp.c:1204-1207`](../../../components/portable/mcp/mcp.c#L1204-L1207)),
`POST /api/rescan`
([`app_services.c:302`](../../../components/portable/app_services/app_services.c#L302)),
`/api/screen.bmp`, `POST /api/scene`
([`screen_frame.c:201-203`](../../../components/app/screen_display/screen_frame.c#L201-L203)).
**VERIFIED, 12 of 12.**

Matching is longest-prefix and per method, and a path that exists with the wrong method
is 405 rather than 404
([`http_server.c:264-290`](../../../components/portable/http_server/http_server.c#L264-L290))
— so `PROPFIND /` cannot ride on the `GET /` index route. Raising the cap to 16 costs
4 × `sizeof(http_route_t)`, about 200 bytes of static RAM. **ESTIMATE.**

### 1.3 HTTP/1.0, `Connection: close`, no chunking, no 207, no `Allow`

`http_send_headers()` writes `HTTP/1.0 %d %s`, `Cache-Control: no-cache` and
`Connection: close` into a **512-byte** stack block and returns `ESP_ERR_INVALID_SIZE`
on overflow
([`http_server.c:100-111`](../../../components/portable/http_server/http_server.c#L100-L111));
there is no chunked encoding anywhere, so the only framings are `Content-Length: n` or
omitted meaning "until close", and the worker closes the fd after every `HTTP_HANDLED`
([`http_server.c:343`](../../../components/portable/http_server/http_server.c#L343)).
`reason_phrase()` switches on 200/202/400/404/405/411/413/414/415/500/503 and renders
everything else as the literal `Error`, so a Multi-Status goes out as `HTTP/1.0 207 Error`
([`http_server.c:46-62`](../../../components/portable/http_server/http_server.c#L46-L62)).
The 405 path emits no `Allow`
([`http_server.c:282-283`](../../../components/portable/http_server/http_server.c#L282-L283)),
which RFC 9110 §15.5.6 makes a MUST. **VERIFIED.** A 207 of unknown length is therefore
impossible; §6.3 makes that a design constraint rather than a problem.

No `Date` header is sent either, and that one is legal: RFC 9110 §6.6.1 requires a `Date`
from an origin server *with* a clock and says "An origin server without a clock MUST NOT
generate a Date header field"
([§6.6.1](https://www.rfc-editor.org/rfc/rfc9110#section-6.6.1)). This firmware compiles
SNTP in (`sdkconfig:3063-3075`) but never starts it and never calls `settimeofday`, so the
wall clock is unset and the omission is the conformant choice. The corollary for §6.4:
`getlastmodified` derives from FAT, never from the system clock, so a board that has been
up for a week reports the card's timestamps, not "a week ago".

### 1.4 Only POST bodies are read, and no request header beyond six is captured

The worker allocates a body only when the *route's* method is `POST`
([`http_server.c:294-341`](../../../components/portable/http_server/http_server.c#L294-L341)):
`Content-Length` required (411), capped at `HTTP_BODY_MAX` 8192 (413), `Content-Type`
must be `application/json` (415). A `PROPFIND` body would be left on the socket with the
connection closed under it. `http_request_t` captures only `content-type`,
`content-length`, `origin`, `host`, `mcp-protocol-version`, `icy-metadata`
([`http_parse.c:81-143`](../../../components/portable/http_server/http_parse.c#L81-L143)):
**no `Depth`**, no `If`, `Destination`, `Overwrite`, `Translate` or `Lock-Token`.
**VERIFIED.**

That is a conformance error, not just a gap. RFC 4918 §8.4: "Servers MUST examine all
requests for a body, even when a body was not expected. In cases where a request body is
present but would be ignored by a server, the server MUST reject the request with 415"
([§8.4](https://www.rfc-editor.org/rfc/rfc4918#section-8.4)) — **VERIFIED.** Leaving the
body unread and closing is the case the spec forbids. Also missing: **`HEAD`** (no route,
so it falls into the 405 branch, though RFC 4918 §9.4 keeps `HEAD` semantics unchanged
for collections) and **`Range`** by design
([`README.md:1142`](../../../README.md#L1142)) — no client in §2 needs `Range` to *copy*
a file, only to resume.

### 1.5 The download path serialises to one transfer

`mini_files.c` keeps one shared 16 KB read buffer and a `SemaphoreHandle_t dl`; the route
takes it non-blocking and answers `503 "only one download at a time; the live stream has
priority"` to the second caller
([`mini_files.c:359-364`](../../../components/portable/mini_files/mini_files.c#L359-L364)),
releasing at `done:`
([`mini_files.c:427-431`](../../../components/portable/mini_files/mini_files.c#L427-L431)),
with the broadcast parked for the duration
([`mini_files.c:370`](../../../components/portable/mini_files/mini_files.c#L370)).
**VERIFIED.** Any WebDAV `GET` reusing this path inherits the 503.

---

## 2. The minimum surface that mounts on the real clients

### 2.1 `OPTIONS`: `DAV:` is the only header that must exist

RFC 4918 §10.1: "All DAV-compliant resources MUST return the DAV header with
compliance-class "1" on all OPTIONS responses"
([§10.1](https://www.rfc-editor.org/rfc/rfc4918#section-10.1)) — **VERIFIED.** `DAV: 1`
is enough for a read-only server. `DAV: 1, 2` claims `LOCK`, `UNLOCK`,
`DAV:supportedlock`, `DAV:lockdiscovery`, the `Time-Out` response header and the
`Lock-Token` request header
([§18.2](https://www.rfc-editor.org/rfc/rfc4918#section-18.2)); advertising class 2
without `LOCK` is non-conformant, and §3.1 shows the cost.

`Allow` is required on `OPTIONS` by RFC 9110. `x/net/webdav` sets it per resource —
`"OPTIONS, LOCK, PUT, MKCOL"` for a non-existent path, `"OPTIONS, LOCK, DELETE,
PROPPATCH, COPY, MOVE, UNLOCK, PROPFIND"` for a directory, plus `GET, HEAD, POST, PUT`
for a file
([`webdav.go:192-214`](https://github.com/golang/net/blob/master/webdav/webdav.go#L192-L214))
— **VERIFIED**, and a good template with the write verbs removed.

`MS-Author-Via: DAV` has a formal grammar, `MS-Author-Via = "MS-Author-Via" ": "
("MS-FP/4.0" / "MS-FP/4.0,DAV" / "DAV" / "DAV,MS-FP/4.0" / token ",DAV")`, and "DAV is
used to indicate that WebDAV is enabled on the WebDAV server, and a WebDAV client MAY use
WebDAV commands"
([MS-WDVSE
§2.2.2](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-wdvse/c99757e4-ea88-49a6-bae5-c6297a024c99))
— **VERIFIED.** It is an `OPTIONS` response header, `x/net/webdav` sends it
unconditionally
([`webdav.go:210`](https://github.com/golang/net/blob/master/webdav/webdav.go#L210)),
and it costs 21 bytes of the 512-byte block.

### 2.2 `PROPFIND`: Depth 0 and 1 are mandatory, infinity is optional

§9.1 verbatim: "A client MUST submit a Depth header with a value of "0", "1", or
"infinity" … Servers MUST support "0" and "1" depth requests on WebDAV-compliant
resources and SHOULD support "infinity" requests. In practice, support for infinite-depth
requests MAY be disabled … Servers SHOULD treat a request without a Depth header as if a
"Depth: infinity" header was included." **VERIFIED.** Four more §9.1 MUSTs decide the
implementation:

- "An empty PROPFIND request body MUST be treated as if it were an 'allprop' request" —
  the body cannot be skipped.
- "All servers MUST support returning a response of content type text/xml or
  application/xml that contains a multistatus XML element."
- A missing property "MUST be noted with a 'response' XML element that contains a 404
  (Not Found) status value" — the 404 lives **inside** a `propstat`, not the status line.
- "the 'multistatus' XML element for a collection resource MUST include a 'response' XML
  element for each member URL of the collection, to whatever depth was requested."

Refusing infinity is sanctioned: §9.1.1 — "A server MAY reject PROPFIND requests on
collections with depth header of "Infinity", in which case it SHOULD use this error with
the precondition code 'propfind-finite-depth' inside the error body"
([§9.1.1](https://www.rfc-editor.org/rfc/rfc4918#section-9.1.1)) — **VERIFIED.** Note the
shape: **403 with an XML body**, not 501, not a silent `Depth: 1`.

The wire format, from §9.1.3: `HTTP/1.1 207 Multi-Status`, `Content-Type: application/xml;
charset="utf-8"`, `<?xml version="1.0" encoding="utf-8" ?>`, then
`<D:multistatus xmlns:D="DAV:"><D:response><D:href>…</D:href><D:propstat><D:prop>…</D:prop>`
`<D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response></D:multistatus>`
([§9.1.3](https://www.rfc-editor.org/rfc/rfc4918#section-9.1.3)) — **VERIFIED.** The
namespace is declared once on the root; `status` carries a **full status line**, not a
bare code.

### 2.3 `href` and the live properties

§8.3: "A server MUST ensure that every 'href' value within a Multi-Status response uses
the same format"; WebDAV "only uses one form of relative reference … the absolute path";
senders MUST NOT "use dot-segments" or have "prefixes that do not match the Request-URI";
and "Identifiers for collections SHOULD end in a '/' character"
([§8.3](https://www.rfc-editor.org/rfc/rfc4918#section-8.3)) — **VERIFIED.** The worked
example returns `/sample/` and `/sample/a%20test` for a member stored as `a test`, and
warns a legal URI may still need XML escaping (the ampersand): two encoders, percent
first, XML second. The repo already has the percent encoder — `w_url_path()` escapes one
segment at a time, preserves `/`, uppercase hex
([`mini_files.c:117-132`](../../../components/portable/mini_files/mini_files.c#L117-L132))
— and it is `static`, so it needs promoting, not rewriting. `x/net/webdav` shows the
slash rule in three lines: `href := path.Join(h.Prefix, reqPath); if href != "/" &&
info.IsDir() { href += "/" }`
([`webdav.go:583-586`](https://github.com/golang/net/blob/master/webdav/webdav.go#L583-L586)).

The four `get*` properties are *conditional* MUSTs on the corresponding GET header
existing — a server returning no `ETag` has no `getetag` obligation:

| Property | § | Obligation |
|---|---|---|
| `DAV:resourcetype` | 15.9 | "MUST be defined on all DAV-compliant resources"; empty = non-collection, `<D:collection/>` = collection; no text |
| `DAV:getcontentlength` | 15.4 | MUST if GET returns `Content-Length`; computed, so protected |
| `DAV:getcontenttype` | 15.5 | MUST if GET returns `Content-Type` |
| `DAV:getetag` | 15.6 | MUST if GET returns `ETag`; "MUST be protected" |
| `DAV:getlastmodified` | 15.7 | MUST if GET returns `Last-Modified`; value is `rfc1123-date` |
| `DAV:displayname` | 15.2 | "SHOULD NOT be protected"; any text |
| `DAV:supportedlock` | 15.10 | class 2 only; a class-1 server may 404 it |
| `DAV:lockdiscovery` | 15.8 | class 2 only; with no locks but lock support it "will be present but contain zero 'activelock' elements" |

**VERIFIED** for every row ([§15](https://www.rfc-editor.org/rfc/rfc4918#section-15)).
A class-1 read-only server should answer `supportedlock` and `lockdiscovery` with **404
inside a `propstat`** — legal, and what tells a client it cannot lock; `x/net/webdav`
declares both with empty values instead
([`prop.go:154-159`](https://github.com/golang/net/blob/master/webdav/prop.go#L154-L159)),
also fine. And `getetag` is the property a read-only server most wants to skip: with no
`ETag` on `GET` there is no obligation and no client in §2 breaks. The repo's only ETag
is a *generation* tag on the library blob, not per-file
([`library_http.c:37-38`](../../../components/portable/library_http/library_http.c#L37-L38)).

### 2.4 RFC 4331 and RFC 5397: optional, cheap, and misleading here

RFC 4331 defines `DAV:quota-available-bytes` and `DAV:quota-used-bytes` on *collections
only*, and specifies "A <DAV:allprop> PROPFIND request SHOULD NOT return any of the
properties defined by this document. However, these property names MUST be returned in a
<DAV:propname> request for a resource that supports the properties" ([RFC 4331
§2](https://www.rfc-editor.org/rfc/rfc4331#section-2)) — **VERIFIED.** Not required; without
them the answer is 404 in the `propstat`, which clients handle. Implementing them here would
be actively misleading: the card is read-only, so "available bytes = 0" reads to a user as a
full disk.

RFC 5397 defines `DAV:current-user-principal` as one `DAV:href` or `DAV:unauthenticated`
element, and "When authentication has not been done or has failed, this property MUST
contain the DAV:unauthenticated pseudo-principal" ([RFC 5397
§3](https://www.rfc-editor.org/rfc/rfc5397#section-3)) — **VERIFIED.** Nothing in §2 asks
for it; if something does,
`<D:current-user-principal><D:unauthenticated/></D:current-user-principal>` is cheaper than
a 404 and more honest than inventing a principal.

One stated non-conformance worth fixing because it is one literal: RFC 4918 §18 — "minimally
all DAV-compliant resources, clients, and proxies MUST be compliant with [RFC2616]"
([§18](https://www.rfc-editor.org/rfc/rfc4918#section-18)) — **VERIFIED.** This server
writes `HTTP/1.0`. Every client in §2 accepts an `HTTP/1.0` 207 with a `Content-Length`, but
it is a stated deviation.

---

## 3. Read-only versus read-write, client by client

### 3.1 macOS has a written policy, and it is class 1 → read-only

`mount_webdav(8)`, present at `/usr/share/man/man8/mount_webdav.8` on macOS 26.3.1:

> "If connecting to a Class 1 compliant WebDAV server, the rdonly option will be set even
> if it was not specified because mount_webdav will not allow files to be opened with
> write access on servers which do not support the DAV LOCK method."

**VERIFIED**, primary Apple documentation, and the single most decision-changing sentence
here. A class-1 server is not refused; it is mounted with `rdonly` forced. Finder shows a
volume, browses it, copies out of it, greys out writes. The same page documents that the
scheme "must be "http://" or "https://"", the 80/443 port defaults, the `/` path default,
that credentials sent insecurely are "allowed but will be logged" unless `-s`, and
`[ENODEV]` = "not WebDAV-enabled or … does not exist".

So the write gate on macOS is **the `LOCK` method, not the `DAV:` token**. Advertising
`DAV: 1, 2` without implementing `LOCK` does not enable writes; it only makes the
advertisement false and hands Finder a `LOCK` it must fail — the exact shortcut the
ESP8266 server in §7.5 takes.

### 3.2 Windows: the write gate is `SupportLocking`, default on

`HKLM\SYSTEM\CurrentControlSet\Services\WebClient\Parameters` → `SupportLocking`,
"Specifies whether the WebClient service supports locking", default `1`
([WebDAV Redirector Registry
Settings](https://learn.microsoft.com/en-us/archive/blogs/robert_mcmurray/webdav-redirector-registry-settings))
— **VERIFIED** for key, meaning, default. What the redirector does with a class-1 server —
mount read-only, or refuse — is **UNVERIFIED**: no Microsoft statement of the fallback
found, and [MS-WDV] is not a page I could read. Ticket W8 settles it with one `net use`.

### 3.3 The write verbs, and which error is correct

RFC 4918 does not legislate; RFC 9110 does. **403 Forbidden** is correct for "this share is
read-only", and is what a class-1 server returns for `PROPPATCH` on a protected property via
`cannot-modify-protected-property`
([§16](https://www.rfc-editor.org/rfc/rfc4918#section-16)). **405 Method Not Allowed** is
correct for "not allowed *on this resource*": §9.3.1 names it for `MKCOL` on an existing
collection, §9.7.2 for `PUT` on a collection ("treated as an error (405 Method Not
Allowed)"). **501 is wrong** — it says the method is unknown, which invites exactly the "try
it anyway" behaviour the share wants to stop. The rule: **403 with a short XML body carrying
a DAV precondition** for `PUT`/`DELETE`/`MKCOL`/`PROPPATCH`/`COPY`/`MOVE`, and **405 with
`Allow`** where the RFC names 405. Never 501, never 500, never today's
404-because-no-route-matches.

What clients then do: macOS never asks (§3.1); rclone surfaces the error and stops; davfs2
"will therefore check if the file has been changed on the server before it uploads a new
version. If it finds it impossible to upload the locally changed file, it will store it in
the local backup directory lost+found"
([mount.davfs(8)](https://manpages.debian.org/testing/davfs2/mount.davfs.8.en.html)) —
**VERIFIED** — so a write does not hang; it lands in `lost+found` on the client, a confusing
but non-blocking failure worth documenting.

### 3.4 `.DS_Store` and `._*`: this repo is already immune, by accident

Finder drops `.DS_Store` in every directory it browses and writes `._name` AppleDouble
companions for files with resource forks or xattrs. Both are **UNVERIFIED** against primary
Apple documentation — observed behaviour, universally reported, no Apple page found. What
*is* verifiable is that this firmware does not care, twice: the scan's name predicate
rejects any name starting with `.` and any containing `..`, `/` or `\`
([`library.c:302-310`](../../../components/portable/library/library.c#L302-L310)), and the
walk skips a failing name silently when it starts with a dot
([`library.c:469-474`](../../../components/portable/library/library.c#L469-L474)).
**VERIFIED.** So `._foo.mp3` is skipped by the leading dot even though it ends in `.mp3`,
and `.DS_Store` never reaches the tree, the JSON view, the index or the playlist. On a
read-only mount the question does not arise — but if writes were ever enabled, the first
thing a rescanned library must keep doing is exactly what it already does.

---

## 4. Windows specifics

**Why Explorer refuses plain-HTTP WebDAV by default.** `BasicAuthLevel` under
`HKLM\SYSTEM\CurrentControlSet\Services\WebClient\Parameters`
([source](https://learn.microsoft.com/en-us/archive/blogs/robert_mcmurray/webdav-redirector-registry-settings)):
`0` disabled, `1` enabled for SSL sites only, `2` enabled for SSL and non-SSL — default
**1**, with: "Using basic authentication can cause serious security issues as the
username/password are transmitted in clear text, therefore the use of basic authentication
over WebDAV is disabled by default unless the connection is using SSL" — **VERIFIED.** That
is the mechanism behind the folk rule "Windows WebDAV needs HTTPS or a registry edit".

Note what it is *not*: a rule about **sending Basic credentials**, not about mounting. A
server that never issues a 401 and needs no credentials is not in the Basic-auth path at
all. Whether `http://` + no auth + a correct `OPTIONS` mounts in Explorer is **UNVERIFIED**;
this firmware has no auth and no TLS ([`README.md:1139`](../../../README.md#L1139)), so that
is exactly the case a board-tier test must settle. The two outcomes are "it mounts" (then
document `BasicAuthLevel` as irrelevant) and "the folder you entered does not appear to be
valid" (then Explorer needs `BasicAuthLevel=2` plus a credential, and a read-only
unauthenticated LAN appliance should tell Windows users to use another client).

**`MS-Author-Via: DAV`** is quoted in §2.1; its purpose is to tell "the issuer of an HTTP
OPTIONS command what protocol mechanism is preferred for authoring documents in this
particular namespace". Whether Explorer *requires* it is **UNVERIFIED** — but every
implemented server that works on Windows sends it and it costs 21 bytes.

**`Translate: f`** is the Microsoft convention for "do not translate the body, serve it
raw". No Microsoft specification defines it — it appears in IIS and Exchange docs and in the
folklore of every WebDAV implementation, and **not** in [MS-WDVSE]. **UNVERIFIED.** A server
that ignores it serves the file verbatim, which is what the header asks for; the failure
mode is a server that honours `Translate: t` by transcoding, which this firmware cannot do
and must not claim. Ignoring it needs no code.

**The 50 MB ceiling.** `FileSizeLimitInBytes`: "the maximum size in bytes that the WebClient
service allows for file transfers", default **50,000,000** — **VERIFIED**, same source. A
FLAC rip over 50 MB fails with an opaque client-side error that has nothing to do with the
server; raise to `4294967295` and restart the WebClient service.

**Also load-bearing, same table:** `LocalServerTimeoutInSec` **15** and
`InternetServerTimeoutInSec` **30** — "the connection timeout … when communicating with a
local WebDAV server", and Windows treats a `192.168.x.x` address as local, so the tight one
is the one that applies. A 2.2 MB 207 (§6.3) that takes longer than 15 s to arrive is
aborted by the client with nothing wrong at the server end — the sharper form of the
directory-size argument. Then `SendReceiveTimeoutInSec` **60** ("the timeout … after issuing
a request"); `ServerNotFoundCacheLifeTimeInSec` **60** ("maintains a list of non-WebDAV
servers … a fail is returned immediately without attempting to contact the server");
`FileAttributesLimitInBytes` **1,000,000** ("the maximum size … for all properties on a
specific collection") — a hard cap on PROPFIND payload per collection, so a directory whose
207 exceeds 1 MB starts failing on Windows regardless of the server. **VERIFIED** for keys
and defaults; the consequence for a huge directory is **ESTIMATE** (§6.3).

---

## 5. macOS and iOS specifics

**Finder mounts through the same machinery as AFP/SMB/NFS.** Apple: the Server Address
field's scheme "loads and runs the appropriate URLMount plug-in", and the browse list comes
from a fixed set of schemes — "This list is currently afp, smb, cifs, nfs, webdav, and ftp"
([QA1387](https://developer.apple.com/library/archive/qa/qa1387/_index.html)) —
**VERIFIED**, if old (the note says "accurate for Mac OS X 10.4.x"). WebDAV being in that
list is why `Cmd-K → http://device:8000/` is the whole mount procedure, and why
`mount_webdav` still ships. `mount_webdav` is the path to test with because it prints an
errno instead of a dialog: `mount_webdav http://host:8000/ /Volumes/mntpnt/`, exit 0 on
success, `[ENODEV]` if "not WebDAV-enabled".

**Does Finder demand authentication?** It handles 401 with a credentials dialog — the man
page documents `-i` (interactive prompt), `-S` ("suppresses … the authentication dialogs")
and `[ECANCELED]` ("the user did not provide proper authentication credentials").
**VERIFIED** that the mechanism exists; whether a server that never issues a 401 mounts
without a prompt is **UNVERIFIED**, but follows from the absence of a challenge — a client
cannot prompt for credentials it was never asked for. The design consequence: **never send a
401 on this device.** There is no password to accept, and a `WWW-Authenticate: Basic`
challenge on a plain-HTTP LAN appliance is a prompt the user cannot satisfy correctly.

**Proxy interference.** The man page's "non-responsive ("disconnect") server dialogs" and
CFNetwork's proxy support mean a system HTTP proxy that captures `http://192.168.x.x:8000`
breaks the mount in a way that looks like a server bug. One line of documentation.

**Quirks I could not verify from an Apple source** (all **UNVERIFIED**, listed because they
drive the tickets): Finder issues `Depth: 1` PROPFINDs per directory rather than `Depth:
infinity`; it sends a PROPFIND body naming
`getlastmodified`/`getcontentlength`/`resourcetype`; it accepts RFC 1123 and tolerates the
two obsolete HTTP-date formats; it creates `.DS_Store` on write mounts (moot, §3.4). The
date question has a *verifiable* answer for the neon/litmus client only (§7.3). For iOS:
Files.app's "Connect to Server" is the same URLMount machinery and Apple documents SMB
support; WebDAV support there is **UNVERIFIED** — do not claim it.

---

## 6. What this repo must change

### 6.1 The blocks, in the order they bite

| # | Block | Evidence |
|---|---|---|
| 1 | `PROPFIND`/`PROPPATCH` are 400 before routing | `method[8]` + `>=` guard, MEASURED §1.1 |
| 2 | 12/12 route slots used | §1.2 |
| 3 | No `Depth`/`If`/`Destination`/`Overwrite`/`Lock-Token` capture | `http_parse.c:81-143` |
| 4 | Non-POST bodies never read (RFC 4918 §8.4 violation) | `http_server.c:294-341` |
| 5 | No 207/403/401/423 reason phrases, 405 without `Allow` | `http_server.c:46-62`, `:282-283` |
| 6 | No chunked framing → 207 must be length-known | `http_server.c:100-111` |
| 7 | No XML, no HTTP-date code, no `HEAD` anywhere in the tree | §6.5, §6.7, §1.4 |
| 8 | One download at a time, 503 to the second | `mini_files.c:359-364` |

### 6.2 Widen the method buffer, add `Depth`, add two routes

`char method[8]` → `[16]` (longest standard method is `PROPPATCH`, 9) and keep
`strlen(buf) >= sizeof req->method` — `sizeof` does the right thing, which is why the
guard is written that way. Cost: 8 bytes per `http_request_t`, which already lives on the
10 KB connection stack
([`http_server.c:254`](../../../components/portable/http_server/http_server.c#L254)); add
`Depth` (16 bytes) and an `If`-presence flag (a token is not needed for a read-only
server) to the same struct.

Because matching is longest-prefix **and** method-specific, and `PROPFIND` must answer
every path, the minimal registration set is **two new slots** — `OPTIONS` on `/` (every
path's capability query) and `PROPFIND` on `/` (dispatch by tree lookup), with
`GET /music/` and `GET /` unchanged and the write verbs falling through to the existing
"path exists, wrong method" branch. Bump `HTTP_MAX_ROUTES` to 16 with a comment naming
WebDAV, or the next feature hits the wall. That branch must gain `Allow`, and
`http_send_error()` takes no headers — so either a new helper or an extended one.

### 6.3 A 207 must be measured, then streamed

The XML for `Depth: 1` on a directory with *n* children is bounded and computable: a fixed
prologue, one `<D:response>` per entry whose size is a pure function of the
percent-encoded href length and the RFC 1123 date, and a fixed epilogue. So the length
comes from a first pass over the arena and the body streams in a second — no buffer
proportional to the directory, no chunking. The repo already contains the pattern twice:
`build_json()` runs `json_document()` once into a NULL-buffer writer to measure, then again
to fill ([`library.c:839-857`](../../../components/portable/library/library.c#L839-L857)),
and the index uses a 1 KB flushing writer
([`mini_files.c:57-98`](../../../components/portable/mini_files/mini_files.c#L57-L98)).
Follow `build_json()`, not `malloc(len)`.

Size, **ESTIMATE** with the assumption stated (average name 40 bytes → ~48 encoded, a
29-byte RFC 1123 date, ~180 bytes of fixed markup per response):

| Directory | 207 body | Windows `FileAttributesLimitInBytes` |
|---:|---:|---|
| 100 tracks | ~22 KB | fine |
| 1 000 tracks | ~220 KB | fine |
| 4 500 tracks | ~1.0 MB | **at the 1 000 000-byte default** |
| 10 000 tracks | ~2.2 MB | over it |

A 10 000-track flat directory is ~2.2 MB of XML on a `Content-Length`-framed HTTP/1.0
response, per listing, paid again on every re-list because nothing is cached. That argues
for documenting a shallow-but-populated directory shape rather than engineering around it.

### 6.4 `getlastmodified` cannot come from the library tree

The arena's node record is 32 bytes holding `size`, `name_off`, `parent`, `child_edge`,
`child_count`, `name_len`, `kind`, `format`, `flags`, `track_index`
([`library.c:40-51`](../../../components/portable/library/library.c#L40-L51)). **No
timestamp field.** The scan reads `struct stat` and uses only `st_mode` and `st_size`
([`library.c:487-504`](../../../components/portable/library/library.c#L487-L504)).
**VERIFIED** — so the property is net-new and costs one `stat()` per child. What FAT
gives, from `update_stat_struct()` in ESP-IDF 6.1 `vfs_fat.c`:

```c
tm.tm_sec = ftime.sec * 2;   /* FAT timestamps have 2-second resolution */
tm.tm_isdst = -1;            /* FAT keeps no DST flag */
st->st_mtime = mktime(&tm);  /* interpreted as LOCAL time */
st->st_atime = 0; st->st_ctime = 0;
```

**VERIFIED in source**, with two consequences: `vfs_fat_fstat()` sets `st_mtime = 0`
explicitly, so the property must come from `stat(path)` and never from the download's fd;
and the value is local time via `mktime()` with `tm_isdst = -1`, so on a device with no
SNTP/TZ a file written in another timezone reports a shifted mtime. That is harmless for
the client's actual use — a stable cache validator — and wrong as a clock. Format with
`gmtime_r()` + `strftime("%a, %d %b %Y %H:%M:%S GMT")`, which is RFC 1123, which §15.7
demands: ~10 lines, and no such code exists today — `mini_files.c` stats only for
`st_size` and `S_ISREG`
([`mini_files.c:374-379`](../../../components/portable/mini_files/mini_files.c#L374-L379)).

### 6.5 The tree is the right source for PROPFIND, FAT the wrong one

`library_find()` turns a decoded `/music/<path>` into a node with no filesystem call,
rejects empty segments, `.` and `..`, and falls back to ASCII case-insensitive only when
unambiguous
([`library.c:1178-1222`](../../../components/portable/library/library.c#L1178-L1222));
`library_child()`/`library_child_count()` walk a directory's members in sorted order;
`library_relpath()`/`library_abspath()` build the two strings a href and a download need.
**VERIFIED.** A `PROPFIND` on a collection is: find the node, iterate the children, emit
one response each. Zero SD I/O except the mtime.

The FAT alternative costs one `opendir`/`readdir` pass plus one `stat` per entry per
request, and the ESP-IDF cache softens but does not remove it: `vfs_fat_stat()` calls
`vfs_fat_consume_stat_cache()`, which matches the **exact path string** against the single
cached `FILINFO` of each open directory and then **clears** it. **VERIFIED** in
`vfs_fat.c` — the cache is single-use and path-keyed, so it helps only when the stat
immediately follows the readdir for that entry, as in the boot scan
([`s13`](s13-library-tree-and-mcp.md) §1.1, §3.2), and would not help a per-request walk
that builds paths fresh. The whole mount also sits behind one lock (`fat_ctx->lock`, taken
in `vfs_fat_stat`), so concurrent PROPFINDs serialise on FAT, not merely on connection
slots. **Verdict: the tree is the source of truth for structure, size, name and type; FAT
supplies only the mtime, one `stat()` per child.** If a `stat()` fails the file vanished
after the scan — omit `getlastmodified` for that entry and let it 404 in the `propstat`
rather than failing the whole 207.

### 6.6 The write side has no shared primitive, and now would be the third copy

`http_server` offers the 512-byte header block, the HTML error body and `http_send_all()`
([`http_server.c:97-125`](../../../components/portable/http_server/http_server.c#L97-L125)).
Everything else is component-local: `mini_files.c` has `writer_t`, `w_escaped()` and
`w_url_path()`; `library.c` has `json_w_t` and `jw_escaped()`. A WebDAV module would add a
fourth escaper (XML), a href encoder and an RFC 1123 formatter. Promote one bounded writer
into `http_server`; if the budget forbids that, the copy is defensible *only* because all
three are the same 40 lines — say which was chosen.

### 6.7 Concurrency: 6 slots, 4 default, and WebDAV clients fan out

`HTTP_MAX_CONNS` is 6 and `max_conns` defaults to 4
([`http_server.h:39`](../../../components/portable/http_server/include/http_server.h#L39),
[`http_server.c:454`](../../../components/portable/http_server/http_server.c#L454)). The
accept loop takes a free slot *before* queueing and answers **503 "too many concurrent
requests"** when none is free
([`http_server.c:435-441`](../../../components/portable/http_server/http_server.c#L435-L441));
each request is one connection, one worker, closed at the end. **VERIFIED.** A WebDAV client
opening a folder is a fan-out machine: the Windows redirector uses multiple connections,
davfs2 is a FUSE daemon with concurrent callers, Finder browses asynchronously. With 4
workers and `Connection: close` the fifth simultaneous request gets a 503 — survivable,
since clients retry, but the difference between "mounts and browses" and "mounts and
stalls". It is the argument for not lowering `max_conns` and for never blocking inside a
WebDAV handler. Each worker has 10 KB of stack of which the request buffer is 1 KB
([`http_server.c:231`](../../../components/portable/http_server/http_server.c#L231)); a
PROPFIND streaming from the arena with a 1 KB local buffer fits, and the headroom telemetry
([`http_server.h:116`](../../../components/portable/http_server/include/http_server.h#L116))
will confirm it.

---

## 7. Prior art on this hardware

### 7.1 `x/net/webdav` — the reference implementation and its two useful lies

`handleOptions` sets `Allow` per resource type, then unconditionally:

```go
w.Header().Set("DAV", "1, 2")
// http://msdn.microsoft.com/en-au/library/cc250217.aspx
w.Header().Set("MS-Author-Via", "DAV")
```

([`webdav.go:206-210`](https://github.com/golang/net/blob/master/webdav/webdav.go#L206-L210))
— **VERIFIED.** It advertises class 2 always, even on a lock system that does nothing, and
never implements `propfind-finite-depth`: a `Depth: infinity` PROPFIND walks the whole tree.
Its `liveProps` table declares `lockdiscovery` and `supportedlock` with empty values and a
`// TODO` on the locking part
([`prop.go:154-159`](https://github.com/golang/net/blob/master/webdav/prop.go#L154-L159)).
**Copy**: the per-resource `Allow` strings, the trailing-slash rule, the streaming
`multistatusWriter`, and `readPropfind()`'s empty-body-is-allprop. **Do not copy**:
advertising class 2 over a no-op lock system — false here in a way that costs a round trip.

### 7.2 `rclone` — the client that needs the least

`backend/webdav/webdav.go`, read as source: `defaultDepth = "1"` for listings, `Depth: 0`
for one object's metadata; `Depth: infinity` **never** sent (grep for `infinity`: absent);
`LOCK`/`UNLOCK` **never** sent (no locking code at all); the PROPFIND body is **empty**
unless a vendor quirk applies — "According to WebDAV RFC 4918, empty PROPFIND body defaults
to allprop" — and for unknown vendors it falls back to `standardProps`, exactly four
properties:

```xml
<d:propfind xmlns:d="DAV:"><d:prop>
  <d:displayname/><d:getlastmodified/><d:getcontentlength/><d:resourcetype/>
</d:prop></d:propfind>
```

([`webdav.go:849-858`](https://github.com/rclone/rclone/blob/master/backend/webdav/webdav.go#L849-L858),
`:361-410`, `:873-885`) — **VERIFIED.** This is the specification for the minimum: rclone
will `ls`, `copy` and `sync` against a class-1 server answering `Depth: 0`/`1` with those
four properties and no `getetag`. **Copy nothing** — it is the client to test against, and
its four-property list is the acceptance criterion.

### 7.3 `davfs2`, and what litmus actually asserts

`davfs2` source not read: the Savannah git host does not resolve from here and the GitHub
mirrors 404. From its manual, **VERIFIED** as primary documentation: userspace FUSE daemon
(needs the `fuse` group or root); "mount.davfs uses them [locks] by default … But not
always: … **The server might not support locks (they are not mandatory)**"; and it caches
directory and attribute information in memory for "a configurable time". So davfs2 is the
most forgiving of the five for a read-only server — and the wrong first test, because it
needs `/etc/davfs2/secrets` and root.

`litmus`'s `src/props.c` is the conformance core. `propfind_d0` does a `Depth: 0` PROPFIND
on the root collection and fails the server if the response `href` does not match the
request URI ("response href for wrong resource") or if "Base collection did not define
{DAV:}collection property" — it parses `resourcetype` and requires the `DAV:` namespace with
a `collection` child. `propgetlastmod` warns if the value is not exactly RFC 1123
("getlastmodified value was not RFC1123-format per RFC4918:S15.7") but only **fails** if
`ne_httpdate_parse()` cannot parse it at all — and neon accepts RFC 1123, RFC 850 and
asctime ([`props.c`](https://github.com/notroj/litmus/blob/master/src/props.c),
[`ne_dates.c`](https://github.com/notroj/neon/blob/master/src/ne_dates.c)) — **VERIFIED.**
Notably, litmus's own `options` test — which read `DAV:` and `Allow` and checked for each
method by name — is inside `#if 0` in current `basic.c`
([`basic.c:37-98`](https://github.com/notroj/litmus/blob/master/src/basic.c#L37-L98)), so
even the reference suite no longer enforces the advertisement. **Use the `props` suite as
the host-tier acceptance test**: its assertions catch a namespace typo, a wrong `href`, or a
date that is not a date.

### 7.4 The actual ESP hardware prior art, and what it gets wrong

The repository named in the brief, `g1b/arduino-webdav-server`, does not resolve — the
GitHub API returns 404 (**MEASURED**). The surviving ESP8266/ESP32 implementation is
`ashtons/ESPGadgetWebDAV`, a single 587-line `.ino`, read as source:

- Advertises `Allow: PROPFIND, GET, DELETE, PUT, MOVE` and `DAV: 1, 2` with **no `LOCK` and
  no `UNLOCK` anywhere in the file** — the class-2 lie of §3.1 in the wild
  ([`:38`](https://github.com/ashtons/ESPGadgetWebDAV/blob/master/ESPGadgetWebDAV.ino#L38)).
- **Ignores `Depth` entirely** (no parsing anywhere): a PROPFIND on a directory always
  returns the whole listing, so `Depth: 0` returns children it must not and `Depth:
  infinity` silently returns one level.
- **`PROPFIND` on a file returns 405** instead of a single-resource 207 — breaking rclone's
  per-object `Depth: 0` metadata call (§7.2) exactly where it matters.
- Builds the whole response in one Arduino `String` (`BuildFileListXml`), and spells the
  status line `HTTP/1.1 207 Multi Status`.
- Detects the method by `strstr(request_line, "PROPFIND ")` before the standard parser sees
  it — the same hack this repo would need if it refused to touch `method[8]`, and a good
  reason not to.

**Copy**: the idea that a fixed set of `FLASH_TEXT` string constants (`MULTISTATUS_START`,
`RESOURCETYPE_START`, `MODDATE_START`) is enough markup for a conformant response — because
it is. **Do not copy**: the `DAV: 1, 2` advertisement, ignoring `Depth`, the 405-on-file, or
the whole-listing-in-a-`String` model.

### 7.5 What `esp_http_server` would have given, and would not

`httpd_method_t` is `typedef enum http_method httpd_method_t` — the enum comes from the
bundled `http_parser` library, and routes are keyed by `(uri, method)` with `HTTP_ANY =
INT_MAX` as wildcard
([`esp_http_server.h:117-121`](https://github.com/espressif/esp-idf/blob/master/components/esp_http_server/include/esp_http_server.h#L117-L121))
— **VERIFIED.** So even the vendor's own server has no first-class `PROPFIND`: a WebDAV
handler there is an `HTTP_ANY` route that re-parses the method itself. It does give chunked
responses (`httpd_resp_send_chunk`) and a `max_uri_len` default from
`CONFIG_HTTPD_MAX_URI_LEN`
([`httpd_parse.c:141-143,786-787`](https://github.com/espressif/esp-idf/blob/master/components/esp_http_server/src/httpd_parse.c#L141-L143)).
Two conclusions: **there is no ESP-IDF WebDAV component to depend on**, so this is
hand-written C either way and the repo's own server is not the wrong foundation; and
**chunked framing is the one thing the vendor server has that this one lacks, and §6.3 shows
it is not needed** — a measured `Content-Length` is strictly better on a device with no
buffer to spare.

---

## 8. The honest alternative

| Option | Credible on ESP32? | Licensing | Cost here |
|---|---|---|---|
| **WebDAV** | hand-written, no dependency | none (own code) | ~900 lines, 2 routes, 1 writer, 1 date formatter |
| **SMB / CIFS** | no credible ESP32 *server*; SMB2 needs signing, NTLM, dialect negotiation | Samba is GPLv3; from-scratch is months | far more than WebDAV for the same outcome |
| **NFS** | v2/v3 over UDP only, RPC/XDR, portmapper | mostly BSD | needs a portmap service; Windows needs "Services for NFS" |
| **FTP** | sketches exist | none | Finder's FTP support removed; Explorer's FTP is not a drive |
| **Index + `curl`** | already shipped | none | no mount, but **works today** |

Blunt version: **WebDAV does not solve the problem the user actually has.** Over WebDAV,
"copy the card" is N PROPFINDs (one per directory, each ~2 KB to 2 MB) plus one `GET` per
track, serialised to **one at a time** (§1.5), each with a fresh TCP connection, against a
card measuring ~250 KB/s with the broadcast parked
([`mini_files.c:366-372`](../../../components/portable/mini_files/mini_files.c#L366-L372)).
For 10 GB: **~11 hours, ESTIMATE**, zero protocol overhead. Carrying the microSD to the
laptop: ~15 minutes.

So: **WebDAV is worth it** for *browsing* from a machine with no other path — a phone, a
guest laptop, an iPad — and for pulling a few tracks with nothing installed. That is what
the mount actually delivers. **It is not worth it** as a bulk-transfer mechanism, and no
implementation care changes that: the bottleneck is one card, one download, and Wi-Fi. **The
cheaper complement** is a single-response archive: `GET /api/library.tar` streaming the
whole card with a known `Content-Length`, turning 500 round trips into one and needing no
new protocol, no XML and no mount — ~100 lines on top of the existing `writer_t`.

Recommendation: **if the goal is "browse the card from anything", do WebDAV and stop after
`PROPFIND` + `GET`, class 1, read-only. If the goal is "get the music onto my laptop", build
the tar stream first** — smaller, faster, and no Windows `BasicAuthLevel` problem. Doing
both is little more work than either, and they answer different questions.

---

## 9. A concrete plan

### 9.1 Memory and line budget

| Item | Cost | Basis |
|---|---|---|
| `method[16]`, `Depth`, `If`-flag in `http_request_t` | +32 B on the 10 KB conn stack | struct read, ESTIMATE |
| `HTTP_MAX_ROUTES` 12 → 16 | +4 × `sizeof(http_route_t)` ≈ +200 B static | ESTIMATE |
| 207/403/401 in `reason_phrase()`, `Allow` on 405 | ~6 lines, no RAM | — |
| XML writer (1 KB, on stack) | 1 KB × 6 conns worst case, inside the 10 KB stack | `http_server.c:30` |
| RFC 1123 formatter + `gmtime_r` | ~40 lines, ~100 B stack | — |
| PROPFIND over the arena | **zero heap** — two passes, no buffer | §6.3 |
| Total new code | **~700–900 lines**, ~250 of them the XML/href emitters | ESTIMATE |

No new component, dependency, or allocation on the request path. The tree is already
resident ([`library.h`](../../../components/portable/library/include/library.h), 4 MiB /
20 000 track defaults) and PROPFIND adds nothing to it.

### 9.2 Tickets

| # | Ticket | Test (tier) |
|---|---|---|
| **W1** | `method[16]`, `Depth` capture, `If` presence | host: extend `t08_http_parse.c`'s table with `PROPFIND`/`PROPPATCH`/`MKCOL`/over-long, asserting `req->method` and `req->depth` — the MEASURED table of §1.1 as a regression test. No board. |
| **W2** | `OPTIONS /` answering `DAV: 1`, `Allow`, `MS-Author-Via: DAV` | host: `t29_webdav.c` drives the handler over a socketpair and greps the header block. board: `curl -i -X OPTIONS …/` asserts all three and that `DAV` does **not** contain `2`. |
| **W3** | `PROPFIND Depth: 0` on a file and a collection: `getcontentlength`/`getcontenttype`/`resourcetype`/`displayname`/`getlastmodified`, trailing-slash href, `207`, `Content-Type: application/xml` | board: `check_webdav_props.py` parses with `xml.etree.ElementTree` and asserts the `{DAV:}multistatus` root, that `href` matches the request path, and that `{DAV:}resourcetype` contains `{DAV:}collection`. |
| **W4** | `PROPFIND Depth: 1`, two-pass measure/stream, arena iteration, `Depth: infinity` → **403 `propfind-finite-depth`** | host: `t29_webdav.c` builds a 1000-track fixture in memory, renders to a socketpair, asserts bytes written == announced `Content-Length` — the whole correctness question for streaming, no board needed. board: child count matches `check_library.py`. |
| **W5** | `getlastmodified` from `stat(path)`, RFC 1123 | host: assert the *format* over a temp dir with a known mtime (FAT cannot be simulated). board: assert *sanity* — within 2 minutes of now. |
| **W6** | `HEAD` folded into the download route; `Allow` on the 405 branch | board: `curl -I` returns the same headers as `curl -i -X GET` with no body. |
| **W7** | the write verbs say no: 403 + a DAV precondition for `PUT`/`DELETE`/`MKCOL`/`PROPPATCH`/`COPY`/`MOVE`/`LOCK`/`UNLOCK`, 405 + `Allow` where RFC 4918 names 405 | board: a `hostile_names.py`-style script asserting the status *and* no hang (a 2 s timeout per request proves the server closes). |
| **W8** | mount it for real (manual, the only test that counts) | in order: litmus `props`; `rclone lsl --webdav-vendor other`; `mount_webdav http://<board>:8000/ /Volumes/xtape` + `cp` one track; `net use` from Windows 11 + `dir`. Record which worked and what each printed — W8 turns §3.2's UNVERIFIED into a fact. |

### 9.3 Risks, in descending likelihood

1. **A 10 000-track flat directory is a 2.2 MB 207, over Windows'
   `FileAttributesLimitInBytes` default of 1 MB** (ESTIMATE, §6.3). Document the limit;
   prefer a directory tree on the card. Do not try to page a PROPFIND — no client asks.
2. **Windows' `FileSizeLimitInBytes` default of 50 MB** silently caps any file Explorer
   copies, with an opaque client-side error. VERIFIED default; document the fix.
3. **Windows' 60-second negative cache** (`ServerNotFoundCacheLifeTimeInSec`) makes the
   attempt *after* a failed mount fail immediately for a minute, which reads as "the server
   changed". VERIFIED default; put it in the testing notes.
4. **One download at a time.** A Finder folder copy is N sequential GETs with a 503 waiting
   for any overlap. Decide explicitly: reuse the gate and document "one transfer at a time",
   or give WebDAV its own slot and double SD contention while the broadcast plays. Do not
   discover it mid-copy.
5. **The 401 loop.** Any path that emits `401` without a satisfiable challenge makes Finder
   re-prompt. The mitigation is architectural: **never send a 401** (§5).
6. **Timeouts.** Header read 2 s
   ([`http_server.h:31`](../../../components/portable/http_server/include/http_server.h#L31));
   send stall 400 ms by default, raised to 5 s for bulk
   ([`mini_files.c:395`](../../../components/portable/mini_files/mini_files.c#L395)). A 2.2
   MB 207 needs the longer timeout too — a PROPFIND inheriting the 400 ms discipline
   truncates on a busy link and looks like corrupt XML. Call `http_send_timeout_ms()` at the
   top of the handler. Windows adds its own: a **local** server connection idle for 15 s
   (`LocalServerTimeoutInSec`, §4) is aborted client-side.
7. **Per-request SD cost.** PROPFIND is one `stat()` per child for the mtime and nothing
   else. If that proves too slow, drop `getlastmodified` — it is a *conditional* MUST (§2.3)
   — and serve a fixed honest value, but say so, because clients will then treat every file
   as unchanged.

---

## 10. Sources

Specifications: [RFC 4918](https://www.rfc-editor.org/rfc/rfc4918) §8.3, §8.4, §9.1, §9.1.1,
§9.1.3, §9.4, §9.7.2, §10.1, §15, §16, §18 · [RFC
4331](https://www.rfc-editor.org/rfc/rfc4331) §2 · [RFC
5397](https://www.rfc-editor.org/rfc/rfc5397) §3 · [RFC
9110](https://www.rfc-editor.org/rfc/rfc9110) §6.6.1, §15.5.6.

Apple: `mount_webdav(8)` at `/usr/share/man/man8/mount_webdav.8`, macOS 26.3.1 (`sw_vers` →
26.3.1, build 25D2128) — the class-1/`rdonly` sentence, scheme/port defaults,
`-s`/`-S`/`-i`, `ENODEV`/`ECANCELED` ·
[QA1387](https://developer.apple.com/library/archive/qa/qa1387/_index.html) — URLMount and
the `afp, smb, cifs, nfs, webdav, ftp` list.

Microsoft: [\[MS-WDVSE\]
§2.2.2](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-wdvse/c99757e4-ea88-49a6-bae5-c6297a024c99)
· [WebDAV Redirector Registry
Settings](https://learn.microsoft.com/en-us/archive/blogs/robert_mcmurray/webdav-redirector-registry-settings)
(`BasicAuthLevel` 1, `FileSizeLimitInBytes` 50 000 000, `FileAttributesLimitInBytes` 1 000
000, `LocalServerTimeoutInSec` 15, `InternetServerTimeoutInSec` 30,
`SendReceiveTimeoutInSec` 60, `ServerNotFoundCacheLifeTimeInSec` 60, `SupportLocking` 1).

Source read, not summarised:
[x/net/webdav](https://github.com/golang/net/tree/master/webdav) (`webdav.go:192-214`,
`:540-600`; `prop.go:154-159`) · [rclone
`backend/webdav`](https://github.com/rclone/rclone/blob/master/backend/webdav/webdav.go)
(`:51`, `:361-410`, `:849-858`, `:873-885`) · [litmus](https://github.com/notroj/litmus)
`src/props.c`, `src/basic.c:37-98` · [neon
`src/ne_dates.c`](https://github.com/notroj/neon/blob/master/src/ne_dates.c) · [davfs2
`mount.davfs(8)`](https://manpages.debian.org/testing/davfs2/mount.davfs.8.en.html) (source
**not** read: Savannah does not resolve, GitHub mirrors 404) ·
[ESPGadgetWebDAV](https://github.com/ashtons/ESPGadgetWebDAV) (587-line `.ino`;
`g1b/arduino-webdav-server` returns 404 — MEASURED) · [ESP-IDF
`esp_http_server`](https://github.com/espressif/esp-idf/tree/master/components/esp_http_server)
(`esp_http_server.h:117-121`, `httpd_parse.c:141-143,786-787`).

Repository: [`http_server.c`](../../../components/portable/http_server/http_server.c),
[`http_parse.c`](../../../components/portable/http_server/http_parse.c),
[`http_server.h`](../../../components/portable/http_server/include/http_server.h),
[`http_parse.h`](../../../components/portable/http_server/include/http_parse.h),
[`mini_files.c`](../../../components/portable/mini_files/mini_files.c),
[`library.c`](../../../components/portable/library/library.c),
[`library.h`](../../../components/portable/library/include/library.h),
[`library_http.c`](../../../components/portable/library_http/library_http.c),
[`main.c`](../../../main/main.c), [`s13`](s13-library-tree-and-mcp.md). ESP-IDF 6.1
`components/fatfs/vfs/vfs_fat.c`: `update_stat_struct()`, `vfs_fat_fstat()`,
`vfs_fat_consume_stat_cache()`.

Left **UNVERIFIED** rather than guessed: whether Windows mounts a class-1 unauthenticated
`http://` share (§3.2, settled by W8); a formal definition of `Translate: f` (§4); Finder's
PROPFIND bodies, `Depth` choice and AppleDouble behaviour (§5, settled by W8); WebDAV in iOS
Files.app (§5); davfs2 at source level (§7.3).
