/*
 * T36: wifi_record, host tier.
 *
 * The record is built byte by byte by this test, and the independent rules are written
 * out again below rather than called - so a change in the module cannot make the test
 * agree with itself. What is exercised is the contract the adapter and the setup page
 * rely on: a round trip, the default setup password, every field's limits, and a
 * refusal for every way a blob can be damaged.
 */
#include "test_util.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "wifi_record.h"

/* ---------------------------------------------------------------- the spec, again */

static bool ssid_ok(const char *s)
{
    size_t n = strlen(s);
    if (n < 1 || n > 32) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7f) return false;
    }
    return true;
}

static bool pass_ok(const char *s)
{
    size_t n = strlen(s);
    if (n < 8 || n > 63) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c > 0x7e) return false;
    }
    return true;
}

static void fill(char *dst, size_t n, char c)
{
    memset(dst, c, n);
    dst[n] = '\0';
}

/* ------------------------------------------------------------------------- tests */

static void test_round_trip(void)
{
    wifi_record_t in, out;
    wifi_record_clear(&in);
    fill(in.ssid, 32, 's');                       /* the longest SSID */
    fill(in.pass, 63, 'p');                       /* the longest password */
    fill(in.setup_pass, 63, 'q');                 /* and the longest setup password */

    CHECK(wifi_record_valid(&in), "the longest record is valid");
    CHECK(wifi_record_provisioned(&in), "an SSID makes it provisioned");
    CHECK(strcmp(wifi_record_setup_password(&in), in.setup_pass) == 0, "the record's own setup password wins");

    uint8_t blob[WIFI_RECORD_BLOB_MAX];
    size_t n = wifi_record_encode(&in, blob, sizeof blob);
    CHECK(n == 8 + 32 + 63 + 63, "the blob is header plus the three fields");
    CHECK(memcmp(blob, "K7WF", 4) == 0, "the magic is K7WF");
    CHECK(blob[4] == 1, "the version is 1");
    CHECK(blob[5] == 32, "the header names the SSID length");

    wifi_record_clear(&out);
    CHECK(wifi_record_decode(&out, blob, n), "the record decodes");
    CHECK(strcmp(out.ssid, in.ssid) == 0, "the SSID survives");
    CHECK(strcmp(out.pass, in.pass) == 0, "the password survives");
    CHECK(strcmp(out.setup_pass, in.setup_pass) == 0, "the setup password survives");
}

static void test_shortest_and_open(void)
{
    /* One-byte SSID, no passwords: an open network with the default setup password. */
    wifi_record_t in, out;
    wifi_record_clear(&in);
    in.ssid[0] = 'a';
    CHECK(wifi_record_valid(&in), "an open network with a one-byte SSID is valid");
    CHECK(strcmp(wifi_record_setup_password(&in), "12345678") == 0, "an empty setup password means the default");

    uint8_t blob[WIFI_RECORD_BLOB_MAX];
    size_t n = wifi_record_encode(&in, blob, sizeof blob);
    CHECK(n == 9, "a one-byte SSID and no passwords is nine bytes");
    CHECK(blob[6] == 0 && blob[7] == 0, "both password lengths are zero");

    CHECK(wifi_record_decode(&out, blob, n), "the open record decodes");
    CHECK(strcmp(out.ssid, "a") == 0, "the SSID is a");
    CHECK(out.pass[0] == '\0', "no password");
    CHECK(out.setup_pass[0] == '\0', "no setup password");
    CHECK(strcmp(wifi_record_setup_password(&out), "12345678") == 0, "and the default is in force");
}

static void test_field_limits(void)
{
    char buf[80];
    for (size_t n = 0; n <= 40; n++) {
        fill(buf, n, 'x');
        CHECK(wifi_record_valid_ssid(buf) == ssid_ok(buf), "SSID rule agrees at length %zu", n);
        CHECK(wifi_record_valid_password(buf) == pass_ok(buf), "password rule agrees at length %zu", n);
    }
    /* The edges, named rather than inferred from the loop. */
    CHECK(!wifi_record_valid_ssid(""), "an empty SSID is refused");
    CHECK(wifi_record_valid_ssid("a"), "a one-byte SSID is accepted");
    CHECK(wifi_record_valid_ssid("1234567890123456789012345678901"), "31 bytes is accepted");
    CHECK(wifi_record_valid_ssid("12345678901234567890123456789012"), "32 bytes is accepted");
    CHECK(!wifi_record_valid_ssid("123456789012345678901234567890123"), "33 bytes is refused");
    CHECK(!wifi_record_valid_password("1234567"), "7 characters is refused");
    CHECK(wifi_record_valid_password("12345678"), "8 characters is accepted");
    CHECK(!wifi_record_valid_password(
              "1234567890123456789012345678901234567890123456789012345678901234"),
          "64 characters is refused");
    /* Non-ASCII is a network name, not a mistake; a control character is. */
    CHECK(wifi_record_valid_ssid("Caf\xc3\xa9"), "a UTF-8 SSID is accepted");
    CHECK(!wifi_record_valid_ssid("bad\tname"), "a tab in an SSID is refused");
    CHECK(!wifi_record_valid_ssid("bad\nname"), "a newline in an SSID is refused");
    CHECK(!wifi_record_valid_password("pass\x01word"), "a control byte in a password is refused");
    CHECK(!wifi_record_valid_password("pass\xc3\xa9word"), "a non-ASCII password is refused");
}

static void test_encode_refuses(void)
{
    wifi_record_t rec;
    uint8_t blob[WIFI_RECORD_BLOB_MAX];

    wifi_record_clear(&rec);                     /* no SSID: nothing to store */
    CHECK(!wifi_record_valid(&rec), "an empty record is invalid");
    CHECK(!wifi_record_provisioned(&rec), "and unprovisioned");
    CHECK(wifi_record_encode(&rec, blob, sizeof blob) == 0, "and cannot be encoded");

    wifi_record_clear(&rec);
    snprintf(rec.ssid, sizeof rec.ssid, "net");
    snprintf(rec.pass, sizeof rec.pass, "1234567");     /* one short */
    CHECK(!wifi_record_valid(&rec), "a seven-character password is invalid");
    CHECK(wifi_record_encode(&rec, blob, sizeof blob) == 0, "and cannot be encoded");

    wifi_record_clear(&rec);
    snprintf(rec.ssid, sizeof rec.ssid, "net");
    snprintf(rec.setup_pass, sizeof rec.setup_pass, "short");   /* one short, setup side */
    CHECK(wifi_record_encode(&rec, blob, sizeof blob) == 0, "a short setup password cannot be encoded");

    /* A buffer that cannot hold a valid record must not write a truncated one. */
    wifi_record_clear(&rec);
    snprintf(rec.ssid, sizeof rec.ssid, "net");
    snprintf(rec.pass, sizeof rec.pass, "12345678");
    CHECK(wifi_record_encode(&rec, blob, 9) == 0, "a short buffer writes nothing");
}

static void expect_refused(const uint8_t *blob, size_t len, const char *why)
{
    wifi_record_t out;
    wifi_record_clear(&out);
    snprintf(out.ssid, sizeof out.ssid, "keep me");   /* decode must clear this */
    bool ok = wifi_record_decode(&out, blob, len);
    CHECK(!ok, "refused: %s", why);
    CHECK(out.ssid[0] == '\0', "a refusal clears the record: %s", why);
    CHECK(!wifi_record_provisioned(&out), "a refusal leaves it unprovisioned: %s", why);
}

static void test_decode_refuses(void)
{
    wifi_record_t good, out;
    uint8_t blob[WIFI_RECORD_BLOB_MAX], bad[WIFI_RECORD_BLOB_MAX];

    wifi_record_clear(&good);
    snprintf(good.ssid, sizeof good.ssid, "xperiments-2.4G");
    snprintf(good.pass, sizeof good.pass, "viernes13");
    snprintf(good.setup_pass, sizeof good.setup_pass, "12345678");
    size_t n = wifi_record_encode(&good, blob, sizeof blob);
    CHECK(n > 8, "the fixture record encoded");

    expect_refused(blob, 0, "nothing at all");
    expect_refused(blob, 4, "shorter than the header");
    expect_refused(blob, 8, "the header with no payload");
    expect_refused(blob, n - 1, "one byte short");
    expect_refused(blob, n + 1, "trailing garbage");

    /* Each mutation starts from the good record, so the failure names one byte. */
#define MUTATE(offset, value)                       \
    do {                                            \
        memcpy(bad, blob, n);                       \
        bad[(offset)] = (value);                    \
    } while (0)

    MUTATE(0, 'Z');
    expect_refused(bad, n, "wrong magic");
    MUTATE(4, 2);
    expect_refused(bad, n, "wrong version");
    MUTATE(5, 0);
    expect_refused(bad, n, "no SSID");
    MUTATE(5, 33);
    expect_refused(bad, n, "an SSID length over 32");
    MUTATE(6, 64);
    expect_refused(bad, n, "a password length over 63");
    MUTATE(7, 64);
    expect_refused(bad, n, "a setup password length over 63");
    MUTATE(5, 4);
    expect_refused(bad, n, "header lengths that disagree with the bytes");
    MUTATE(8, 0x01);
    expect_refused(bad, n, "a control byte in the SSID");
#undef MUTATE

    /* And the good record still decodes: the refusals above changed nothing shared. */
    CHECK(wifi_record_decode(&out, blob, n), "the good record still decodes");
    CHECK(strcmp(out.ssid, "xperiments-2.4G") == 0, "and is the one that was stored");
}

int main(void)
{
    printf("t36: wifi_record\n");
    test_round_trip();
    test_shortest_and_open();
    test_field_limits();
    test_encode_refuses();
    test_decode_refuses();
    TEST_END();
}
