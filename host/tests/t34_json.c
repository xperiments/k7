/*
 * T34: the JSON reader and writer.
 *
 * This module was five copies of "write a bounded JSON reply" and two parsers before it was
 * one thing, and the one failure that mattered had no test: a reply built with `snprintf`
 * put a validation sentence between quotes without escaping it, and stayed correct only
 * because those sentences happened to be plain English. So the escaping is pinned here, byte
 * for byte, and so is the refusal when a document does not fit.
 */
#include "test_util.h"

#include <stdbool.h>
#include <string.h>

#include "json.h"

static void test_writer(void)
{
    char buf[256];
    json_writer_t w = json_writer_make(buf, sizeof buf);

    json_puts(&w, "{\"a\":\"");
    json_escaped(&w, "say \"hi\"\\then\n");
    json_puts(&w, "\"}");
    CHECK(json_ok(&w), "a short document fits");
    CHECK(strcmp(buf, "{\"a\":\"say \\\"hi\\\"\\\\then\\u000a\"}") == 0, "escaped: %s", buf);

    /* A control character is escaped as \u00XX, because JSON forbids it raw. */
    w = json_writer_make(buf, sizeof buf);
    json_escaped(&w, "\x01\x1f");
    CHECK(strcmp(buf, "\\u0001\\u001f") == 0, "control: %s", buf);

    /* UTF-8 is passed through: the device's names are already UTF-8. */
    w = json_writer_make(buf, sizeof buf);
    json_escaped(&w, "caf\xc3\xa9");
    CHECK(strcmp(buf, "caf\xc3\xa9") == 0, "utf-8 passthrough: %s", buf);

    /* A bounded writer refuses rather than truncating a document into something else. */
    char small[8];
    w = json_writer_make(small, sizeof small);
    json_puts(&w, "0123456789");
    CHECK(!json_ok(&w), "an overlong write is refused");
    CHECK(strlen(small) < sizeof small, "and the buffer still ends where it must");

    /* `printf` reports its own overflow the same way. */
    w = json_writer_make(buf, 4);
    json_printf(&w, "%s", "0123456789");
    CHECK(!json_ok(&w), "a printf that does not fit is refused");
}

static void test_reader(void)
{
    const char *body = "{\"path\":\"music/a\\u00e9b.mp3\",\"n\":42,"
                       "\"xs\":[1,2,3],\"ok\":true,\"obj\":{\"k\":\"v\"}}";
    json_tok_t t[64];
    int nt = 0;
    CHECK(json_parse(body, t, 64, &nt), "the body parses");
    CHECK(nt > 0, "and produced tokens");

    int root = 0;
    int path = json_tok_member(body, t, nt, root, "path");
    CHECK(path >= 0, "path is found");
    char out[64];
    CHECK(json_tok_unescape(body, &t[path], out, sizeof out), "path unescapes");
    CHECK(strcmp(out, "music/a\xc3\xa9" "b.mp3") == 0, "a \\u escape becomes UTF-8: %s", out);

    int n = json_tok_member(body, t, nt, root, "n");
    int value = 0;
    CHECK(n >= 0 && json_tok_int(body, &t[n], &value), "n is an int");
    CHECK_EQ(value, 42, "and 42");

    int xs = json_tok_member(body, t, nt, root, "xs");
    CHECK(xs >= 0, "xs is found");
    CHECK_EQ(json_tok_array_item(t, nt, xs, 2), json_tok_array_item(t, nt, xs, 2),
             "the third item is stable");
    int third = json_tok_array_item(t, nt, xs, 2);
    CHECK(third >= 0 && json_tok_int(body, &t[third], &value), "the third item is an int");
    CHECK_EQ(value, 3, "and 3");
    CHECK_EQ(json_tok_array_item(t, nt, xs, 3), -1, "past the end is -1");

    int obj = json_tok_member(body, t, nt, root, "obj");
    int k = obj >= 0 ? json_tok_member(body, t, nt, obj, "k") : -1;
    CHECK(k >= 0 && json_tok_unescape(body, &t[k], out, sizeof out), "a nested member is found");
    CHECK(strcmp(out, "v") == 0, "and reads as v");

    CHECK_EQ(json_tok_member(body, t, nt, root, "absent"), -1, "a missing member is -1");

    /* A body that needs more tokens than the table holds is refused, not truncated. */
    CHECK(!json_parse(body, t, 3, &nt), "too few tokens is a refusal");
    CHECK(!json_parse("{", t, 64, &nt), "an unfinished object is a refusal");
    CHECK(!json_parse("nope", t, 64, &nt), "a bare word is a refusal");

    /* An escape too long for the buffer is refused rather than cut mid-sequence. */
    char tiny[4];
    CHECK(!json_tok_unescape(body, &t[path], tiny, sizeof tiny), "a value that does not fit");
}

int main(void)
{
    test_writer();
    test_reader();
    TEST_END();
}
