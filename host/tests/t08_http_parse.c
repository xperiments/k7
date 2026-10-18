/*
 * T14: the request rules, without opening a socket.
 *
 * Every case here used to be reachable only by connecting and typing something
 * malformed. The awkward ones are the point: an absolute-form target from a proxy
 * or a hand-typed URL, a query string, a path one byte over the limit, a header
 * name in the wrong case, and a route table where the longest prefix must win.
 */
#include "test_util.h"

#include "http_parse.h"
#include "http_server.h"

/* Parse `raw` in a writable copy and return the status. */
static int parse(const char *raw, http_request_t *req)
{
    char buf[HTTP_HEADER_MAX];
    snprintf(buf, sizeof buf, "%s", raw);
    memset(req, 0, sizeof *req);
    return http_parse_request(buf, req);
}

static http_result_t dummy(int fd, const http_request_t *req, void *ctx)
{
    (void)fd; (void)req; (void)ctx;
    return HTTP_HANDLED;
}

int main(void)
{
    http_request_t req;

    CHECK_EQ(parse("GET /stream HTTP/1.0\r\nHost: x\r\n\r\n", &req), 0, "a plain request parses");
    CHECK(strcmp(req.method, "GET") == 0, "the method is GET");
    CHECK(strcmp(req.path, "/stream") == 0, "the path is /stream");
    CHECK(!req.want_icy_metadata, "no metadata unless asked");

    CHECK_EQ(parse("GET /s HTTP/1.1\r\nIcy-MetaData: 1\r\n\r\n", &req), 0, "a metadata request parses");
    CHECK(req.want_icy_metadata, "Icy-MetaData: 1 is detected");

    CHECK_EQ(parse("GET /s HTTP/1.1\r\nicy-metadata:1\r\n\r\n", &req), 0, "the header name is case-insensitive");
    CHECK(req.want_icy_metadata, "and no space after the colon is accepted");

    CHECK_EQ(parse("GET /s HTTP/1.1\r\nIcy-MetaData:   1\r\n\r\n", &req), 0, "leading spaces are skipped");
    CHECK(req.want_icy_metadata, "and the value is still 1");

    CHECK_EQ(parse("GET /s HTTP/1.1\r\nIcy-MetaData: 0\r\n\r\n", &req), 0, "a zero value parses");
    CHECK(!req.want_icy_metadata, "zero means no metadata");

    CHECK_EQ(parse("GET /a?b=c&d=e HTTP/1.0\r\n\r\n", &req), 0, "a query string parses");
    CHECK(strcmp(req.path, "/a") == 0, "the query is stripped from the path");

    CHECK_EQ(parse("GET http://host:8000/music/x%20y.mp3 HTTP/1.0\r\n\r\n", &req), 0,
             "an absolute-form target parses");
    CHECK(strcmp(req.path, "/music/x%20y.mp3") == 0,
          "the path is extracted and left percent-encoded: '%s'", req.path);

    CHECK_EQ(parse("GET http://host:8000 HTTP/1.0\r\n\r\n", &req), 400,
             "an absolute-form target with no path is malformed");
    CHECK_EQ(parse("GET http:// HTTP/1.0\r\n\r\n", &req), 400, "a scheme with nothing after it is malformed");

    CHECK_EQ(parse("OPTIONS / HTTP/1.0\r\n\r\n", &req), 0, "a non-GET method parses");
    CHECK(strcmp(req.method, "OPTIONS") == 0, "the method is reported; 405 is the caller's decision");

    CHECK_EQ(parse("ABCDEFGHI / HTTP/1.0\r\n\r\n", &req), 400, "a method too long for the buffer is 400");

    CHECK_EQ(parse("garbage\r\n\r\n", &req), 400, "a request line with no target is 400");
    CHECK_EQ(parse("GET\r\n\r\n", &req), 400, "a request line with no space is 400");
    CHECK_EQ(parse("GET /s HTTP/1.0\n\n", &req), 400, "a request line without CRLF is 400");

    /* The request line without a version is accepted, which is what the old
     * parser did: S1's clients always send one, and refusing it would be a new
     * behaviour for no gain. */
    CHECK_EQ(parse("GET /s\r\n\r\n", &req), 0, "a request line without a version still parses");
    CHECK(strcmp(req.path, "/s") == 0, "and its path is the target");

    /* One byte over the path limit. */
    char long_req[HTTP_HEADER_MAX];
    size_t n = (size_t)snprintf(long_req, sizeof long_req, "GET /");
    memset(long_req + n, 'a', HTTP_PATH_MAX - 1);
    snprintf(long_req + n + HTTP_PATH_MAX - 1, sizeof long_req - n - (HTTP_PATH_MAX - 1), " HTTP/1.0\r\n\r\n");
    CHECK_EQ(parse(long_req, &req), 414, "a path at the limit is 414");

    /* Routing: the longest prefix wins, and "/" is the catch-all. */
    http_route_t routes[3] = {
        {.prefix = "/", .plen = 1, .fn = dummy},
        {.prefix = "/music/", .plen = 7, .fn = dummy},
        {.prefix = "/stream", .plen = 7, .fn = dummy},
    };
    const http_route_t *r = http_route_match(routes, 3, "/music/x.flac");
    CHECK(r == &routes[1], "a download matches /music/, not /");
    r = http_route_match(routes, 3, "/stream");
    CHECK(r == &routes[2], "the stream matches /stream");
    r = http_route_match(routes, 3, "/streaming");
    CHECK(r == &routes[2], "a prefix match is a prefix, not a path segment");
    r = http_route_match(routes, 3, "/");
    CHECK(r == &routes[0], "the root matches /");
    r = http_route_match(routes, 3, "/nope");
    CHECK(r == &routes[0], "an unknown path falls through to /");
    CHECK(http_route_match(routes, 0, "/") == NULL, "no routes, no match");
    CHECK(http_route_match(NULL, 3, "/") == NULL, "no table, no match");

    TEST_END();
}
