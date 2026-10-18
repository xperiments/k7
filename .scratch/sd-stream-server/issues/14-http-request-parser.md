# 14 — Make the request parser a module

**What to build:** Every rule about what a request line means lives in `static` functions
inside the connection worker: absolute-form request targets, query stripping, the 414 path
limit, case-insensitive `Icy-MetaData`, malformed-request detection, and longest-prefix
routing. They are observable only by opening a socket, so the integration script's happy
paths are the only coverage.

Extract the rules into the http_server interface as pure functions:

- `int http_parse_request(char *buf, http_request_t *req)` — returns `0` or the HTTP status
  to answer with. It may modify `buf` (splits the request line); the caller owns the
  header block.
- `const http_route_t *http_route_match(const http_route_t *routes, int count,
  const char *path)` — longest matching prefix wins, `NULL` when nothing matches.

`serve_connection()` keeps the recv/send, the status replies and the fd handoff.

**Blocked by:** none

**Status:** done (host tier and both firmware builds; board tier not run - no board attached)

- [ ] `GET /stream HTTP/1.0` parses to method `GET`, path `/stream`, no metadata.
- [ ] `Icy-MetaData: 1` is detected case-insensitively.
- [ ] An absolute-form target is reduced to its path; a query string is stripped.
- [ ] A path at or over `HTTP_PATH_MAX` returns 414; a malformed line returns 400.
- [ ] `OPTIONS /` parses (the 405 is the caller's decision, not the parser's).
- [ ] Routing picks `/music/` over `/` and falls back to `/` for an unknown path.
- [ ] `http_server.c` no longer contains a request parser.
- [ ] Host unit tests and the integration suite pass.

**Files:** `components/portable/http_server/http_parse.c` (new),
`components/portable/http_server/include/http_parse.h` (new),
`components/portable/http_server/http_server.c`,
`components/portable/http_server/include/http_server.h`,
`host/tests/t08_http_parse.c` (new), `host/Makefile`.

**Note on the route callback type:** `http_route_t` carries the public `http_route_fn`, so
the matcher can be tested with dummy handlers and the server keeps its own table.

**Tests:** t08 covers the cases above, including a path with percent-encoding (which the
parser must leave encoded) and an over-long header value.

**Source:** architecture review, candidate 4.

## Comments

- Added `http_parse.c/.h`: `http_parse_request()` and `http_route_match()` are pure and in the
  component's interface; `serve_connection()` keeps the recv/send and the replies.
- `t08_http_parse` covers absolute-form targets, query stripping, the 414 limit, a
  case-insensitive first-header `Icy-MetaData`, method length and longest-prefix routing.
- **Bug found by the new test:** `find_header()` skipped the first header line, so
  `Icy-MetaData` was not detected when the client sent it first. Clients that sent `Host`
  first hid it. The parser now reads the header block from its first line.
- Verified: unit, layering and integration suites; both firmware builds.
