/*
 * T41: handover, host tier.
 *
 * Two things are checked here, and they are checked differently on purpose.
 *
 * The primitives are checked against vectors that were not produced by this repository: FIPS
 * 180-4's SHA-256 examples, RFC 4231's HMAC cases and RFC 5869's HKDF cases, whose published
 * OKMs the vectors below reproduce. The handover's own LMK is checked against a value computed
 * from the same specification in Python's hashlib/hmac - an independent implementation of the
 * same arithmetic, so a mistake shared by the C and the test is not one this file can hide.
 *
 * The frames are checked against the specification's tables instead: an independent CRC-8,
 * whose variant is pinned by its published check value, and a byte-by-byte reading of the
 * layout - the IPv4's byte order, the fields that vanish with their flag, and every way a
 * frame can be damaged or over-long.
 */
#include "test_util.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "handover.h"

static void check_hex(const uint8_t *got, size_t n, const char *want, const char *what)
{
    char hex[2 * 255 + 1];
    if (n > 255) n = 255;
    for (size_t i = 0; i < n; i++) snprintf(hex + 2 * i, 3, "%02x", got[i]);
    hex[2 * n] = '\0';
    CHECK(strcmp(hex, want) == 0, "%s\n      got %s\n     want %s", what, hex, want);
}

/* CRC-8/ATM, written out again: polynomial 0x07, init 0x00, no reflection, no final xor.
 * Its check value over "123456789" is 0xF4, which is what pins the variant. */
static uint8_t test_crc8(const uint8_t *p, size_t n)
{
    uint8_t c = 0;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int bit = 0; bit < 8; bit++) c = (uint8_t)((c & 0x80) ? ((c << 1) ^ 0x07) : (c << 1));
    }
    return c;
}

static ho_record_t full_record(void)
{
    ho_record_t r;
    memset(&r, 0, sizeof(r));
    r.has_home = true;
    r.has_ap = true;
    strcpy(r.home_ssid, "xperiments-2.4G");
    strcpy(r.home_pass, "correct horse battery staple");
    strcpy(r.ap_ssid, "K7.local");
    strcpy(r.ap_pass, "12345678");
    strcpy(r.host, "k7");
    r.ipv4 = 0xC0A8002Cu; /* 192.168.0.44 */
    return r;
}

int main(void)
{
    /* ------------------------------------------------------------ SHA-256, FIPS 180-4 */
    {
        uint8_t out[32];
        ho_sha256((const uint8_t *)"", 0, out);
        check_hex(out, 32, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                  "sha256(\"\")");
        ho_sha256((const uint8_t *)"abc", 3, out);
        check_hex(out, 32, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                  "sha256(\"abc\")");
        ho_sha256((const uint8_t *)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, out);
        check_hex(out, 32, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
                  "sha256(56 bytes)");
        ho_sha256((const uint8_t *)"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
                                    "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
                  112, out);
        check_hex(out, 32, "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1",
                  "sha256(112 bytes)");
    }

    /* ------------------------------------------------------------ HMAC, RFC 4231 */
    {
        uint8_t key[20], key2[8], out[32];
        memset(key, 0x0b, sizeof(key));
        ho_hmac_sha256(key, sizeof(key), (const uint8_t *)"Hi There", 8, out);
        check_hex(out, 32, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
                  "hmac rfc4231 #1");
        memcpy(key2, "Jefe", 4);
        ho_hmac_sha256(key2, 4, (const uint8_t *)"what do ya want for nothing?", 28, out);
        check_hex(out, 32, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
                  "hmac rfc4231 #2");
    }

    /* ------------------------------------------------------------ HKDF, RFC 5869 */
    {
        uint8_t ikm[22], salt[13], info[10], out[82];
        memset(ikm, 0x0b, sizeof(ikm));
        for (int i = 0; i < 13; i++) salt[i] = (uint8_t)i;
        for (int i = 0; i < 10; i++) info[i] = (uint8_t)(0xf0 + i);
        CHECK(ho_hkdf_sha256(ikm, sizeof(ikm), salt, sizeof(salt), info, sizeof(info), out, 42),
              "hkdf accepts the RFC's first case");
        check_hex(out, 42,
                  "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
                  "34007208d5b887185865",
                  "hkdf rfc5869 #1");

        uint8_t ikm2[80], salt2[80], info2[80];
        for (int i = 0; i < 80; i++) {
            ikm2[i] = (uint8_t)i;
            salt2[i] = (uint8_t)(0x60 + i);
            info2[i] = (uint8_t)(0xb0 + i);
        }
        CHECK(ho_hkdf_sha256(ikm2, 80, salt2, 80, info2, 80, out, 82), "hkdf accepts the long case");
        check_hex(out, 82,
                  "b11e398dc80327a1c8e7f78c596a49344f012eda2d4efad8a050cc4c19afa97c"
                  "59045a99cac7827271cb41c65e590e09da3275600c2f09b8367793a9aca3db71"
                  "cc30c58179ec3e87c14c01d5c1f3434f1d87",
                  "hkdf rfc5869 #2");

        CHECK(ho_hkdf_sha256(ikm, sizeof(ikm), NULL, 0, NULL, 0, out, 42),
              "hkdf accepts an absent salt and info");
        check_hex(out, 42,
                  "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d"
                  "9d201395faa4b61a96c8",
                  "hkdf rfc5869 #3");
        CHECK(!ho_hkdf_sha256(ikm, sizeof(ikm), NULL, 0, NULL, 0, out, 0),
              "hkdf refuses an empty output");
        CHECK(!ho_hkdf_sha256(ikm, sizeof(ikm), NULL, 0, NULL, 0, out, 255 * 32 + 1),
              "hkdf refuses more than 255 blocks");
    }

    /* ------------------------------------------------------------ the LMK */
    {
        uint8_t shared[32], k7pub[32], ppub[32];
        uint8_t a[HO_LMK_LEN], b[HO_LMK_LEN], c[HO_LMK_LEN];
        for (int i = 0; i < 32; i++) {
            shared[i] = (uint8_t)i;
            k7pub[i] = (uint8_t)(32 + i);
            ppub[i] = (uint8_t)(64 + i);
        }
        CHECK(ho_lmk(shared, k7pub, ppub, a), "lmk derives");
        check_hex(a, HO_LMK_LEN, "877a3cc623eaeca67c3c27b9b6249932",
                  "lmk over the fixed inputs");

        CHECK(ho_lmk(shared, k7pub, ppub, b), "lmk derives again");
        CHECK(memcmp(a, b, HO_LMK_LEN) == 0, "both sides of the same transcript derive the same LMK");

        shared[0] ^= 0x01;
        CHECK(ho_lmk(shared, k7pub, ppub, c), "lmk derives from a different secret");
        CHECK(memcmp(a, c, HO_LMK_LEN) != 0, "a different shared secret is a different LMK");
        shared[0] ^= 0x01;

        ppub[31] ^= 0x80;
        CHECK(ho_lmk(shared, k7pub, ppub, c), "lmk derives from a different transcript");
        CHECK(memcmp(a, c, HO_LMK_LEN) != 0, "a different transcript is a different LMK");
        ppub[31] ^= 0x80;

        uint8_t zero[32] = {0}, untouched[HO_LMK_LEN];
        memset(untouched, 0xa5, sizeof(untouched));
        memcpy(c, untouched, sizeof(c));
        CHECK(!ho_lmk(zero, k7pub, ppub, c), "an all-zero shared secret is refused");
        CHECK(memcmp(c, untouched, sizeof(c)) == 0, "a refused LMK leaves the output untouched");
    }

    /* ------------------------------------------------------------ beacon */
    {
        ho_beacon_t b, back;
        uint8_t buf[HO_FRAME_MAX];
        memset(&b, 0, sizeof(b));
        for (int i = 0; i < HO_KEY_LEN; i++) b.pub[i] = (uint8_t)(i * 3);
        b.channel = 6;
        b.seconds_left = 43;

        size_t n = ho_beacon_encode(&b, buf, sizeof(buf));
        CHECK_EQ(n, HO_BEACON_SIZE, "beacon size");
        CHECK_EQ(buf[0], HO_VERSION, "beacon version byte");
        CHECK_EQ(buf[1], HO_TYPE_BEACON, "beacon type byte");
        CHECK(memcmp(buf + 2, b.pub, HO_KEY_LEN) == 0, "beacon carries the key in the clear");
        CHECK_EQ(buf[34], 6, "beacon channel at offset 34");
        CHECK_EQ(buf[35], 43, "beacon seconds at offset 35");

        CHECK(ho_beacon_decode(buf, n, &back), "beacon decodes");
        CHECK(memcmp(back.pub, b.pub, HO_KEY_LEN) == 0, "beacon key round-trips");
        CHECK_EQ(back.channel, 6, "beacon channel round-trips");
        CHECK_EQ(back.seconds_left, 43, "beacon seconds round-trip");

        b.channel = 0;
        CHECK_EQ(ho_beacon_encode(&b, buf, sizeof(buf)), 0, "channel 0 is not a channel");
        b.channel = 14;
        CHECK_EQ(ho_beacon_encode(&b, buf, sizeof(buf)), 0, "channel 14 is not a channel");
        b.channel = 1;
        b.seconds_left = 61;
        CHECK_EQ(ho_beacon_encode(&b, buf, sizeof(buf)), 0, "a window longer than 60 s is refused");
        b.seconds_left = 0;
        CHECK_EQ(ho_beacon_encode(&b, buf, 35), 0, "a beacon does not fit in 35 bytes");

        n = ho_beacon_encode(&b, buf, sizeof(buf));
        CHECK(!ho_beacon_decode(buf, n - 1, &back), "a truncated beacon is refused");
        buf[0] = 2;
        CHECK(!ho_beacon_decode(buf, n, &back), "an unknown version is refused");
        buf[0] = HO_VERSION;
        buf[1] = HO_TYPE_REQUEST;
        CHECK(!ho_beacon_decode(buf, n, &back), "a request is not a beacon");
    }

    /* ------------------------------------------------------------ request */
    {
        ho_request_t r, back;
        uint8_t buf[HO_FRAME_MAX];
        for (int i = 0; i < HO_KEY_LEN; i++) r.pub[i] = (uint8_t)(255 - i);
        size_t n = ho_request_encode(&r, buf, sizeof(buf));
        CHECK_EQ(n, HO_REQUEST_SIZE, "request size");
        CHECK_EQ(buf[1], HO_TYPE_REQUEST, "request type byte");
        CHECK(ho_request_decode(buf, n, &back), "request decodes");
        CHECK(memcmp(back.pub, r.pub, HO_KEY_LEN) == 0, "request key round-trips");
        CHECK(!ho_request_decode(buf, n + 1, &back), "a request with a trailing byte is refused");
        buf[0] = 9;
        CHECK(!ho_request_decode(buf, n, &back), "an unknown version is refused");
    }

    /* ------------------------------------------------------------ record, the honest case */
    {
        ho_record_t r = full_record(), back;
        uint8_t buf[HO_FRAME_MAX];
        size_t n = ho_record_encode(&r, buf, sizeof(buf));
        CHECK(n > 0 && n <= HO_RECORD_MAX, "a full record fits its own ceiling (got %zu)", n);
        CHECK(n <= HO_FRAME_MAX, "a full record fits ESP-NOW's ceiling (got %zu)", n);
        CHECK_EQ(buf[1], HO_TYPE_RECORD, "record type byte");
        CHECK_EQ(buf[2], 0x03, "both networks present");
        CHECK(ho_record_decode(buf, n, &back), "the record decodes");
        CHECK(strcmp(back.home_ssid, r.home_ssid) == 0, "home ssid round-trips");
        CHECK(strcmp(back.home_pass, r.home_pass) == 0, "home password round-trips");
        CHECK(strcmp(back.ap_ssid, r.ap_ssid) == 0, "setup ssid round-trips");
        CHECK(strcmp(back.ap_pass, r.ap_pass) == 0, "setup password round-trips");
        CHECK(strcmp(back.host, r.host) == 0, "hostname round-trips");
        CHECK_EQ(back.ipv4, 0xC0A8002Cu, "ipv4 round-trips");
        CHECK(back.has_home && back.has_ap, "both flags round-trip");

        /* 192.168.0.44, most significant byte first, four bytes before the checksum. */
        CHECK_EQ(buf[n - 5], 0xC0, "ipv4 first wire byte");
        CHECK_EQ(buf[n - 4], 0xA8, "ipv4 second wire byte");
        CHECK_EQ(buf[n - 3], 0x00, "ipv4 third wire byte");
        CHECK_EQ(buf[n - 2], 0x2C, "ipv4 fourth wire byte");
        CHECK_EQ(buf[n - 1], test_crc8(buf, n - 1), "the checksum is CRC-8/ATM over the rest");

        /* The check value that pins the variant. */
        const uint8_t check[] = "123456789";
        CHECK_EQ(test_crc8(check, 9), 0xF4, "CRC-8/ATM's check value");

        /* Absent networks take their fields with them. */
        ho_record_t only_ap;
        memset(&only_ap, 0, sizeof(only_ap));
        only_ap.has_ap = true;
        strcpy(only_ap.ap_ssid, "K7.local");
        strcpy(only_ap.ap_pass, "12345678");
        strcpy(only_ap.host, "k7");
        only_ap.ipv4 = 0xC0A8002Cu;
        n = ho_record_encode(&only_ap, buf, sizeof(buf));
        CHECK_EQ(n, 3 + (1 + 8) + (1 + 8) + (1 + 2) + 4 + 1, "an absent home network is absent");
        CHECK_EQ(buf[2], 0x02, "only the setup network's flag is set");
        CHECK(ho_record_decode(buf, n, &back), "the reduced record decodes");
        CHECK(!back.has_home && back.has_ap, "the reduced record's flags round-trip");
        CHECK(back.home_ssid[0] == '\0', "an absent home network decodes empty");

        /* An open home network carries a zero-length password and is still a record. */
        ho_record_t open_net = full_record();
        open_net.home_pass[0] = '\0';
        n = ho_record_encode(&open_net, buf, sizeof(buf));
        CHECK(n > 0, "an open home network encodes");
        CHECK(ho_record_decode(buf, n, &back), "an open home network decodes");
        CHECK(back.home_pass[0] == '\0', "an open home network's password is empty");
    }

    /* ------------------------------------------------------------ record, the ceiling */
    {
        ho_record_t r;
        uint8_t buf[HO_FRAME_MAX];
        memset(&r, 0, sizeof(r));
        r.has_home = true;
        r.has_ap = true;
        memset(r.home_ssid, 'H', HO_SSID_MAX);
        memset(r.home_pass, 'p', HO_PASS_MAX);
        memset(r.ap_ssid, 'A', HO_SSID_MAX);
        memset(r.ap_pass, 'q', HO_PASS_MAX);
        memset(r.host, 'h', HO_HOST_MAX);
        r.ipv4 = 0xFFFFFFFFu;

        size_t n = ho_record_encode(&r, buf, sizeof(buf));
        CHECK_EQ(n, HO_RECORD_MAX, "the longest legal record is HO_RECORD_MAX");
        CHECK_EQ(HO_RECORD_MAX, 235, "the arithmetic in the spec is 235 bytes");
        CHECK(HO_RECORD_MAX <= HO_FRAME_MAX, "and it fits under ESP-NOW's 250");

        ho_record_t back;
        CHECK(ho_record_decode(buf, n, &back), "the longest record decodes");
        CHECK(strlen(back.home_ssid) == HO_SSID_MAX, "a 32-byte SSID survives");
        CHECK(strlen(back.home_pass) == HO_PASS_MAX, "a 63-byte password survives");
        CHECK(strlen(back.host) == HO_HOST_MAX, "a 32-byte hostname survives");
    }

    /* ------------------------------------------------------------ record, what is refused */
    {
        ho_record_t r;
        uint8_t buf[HO_FRAME_MAX];

        /* A 33-byte SSID cannot be expressed in the struct - the field is 32 plus its
         * terminator - so the encoder's length rule is exercised from the wire instead, in
         * the "over-long SSID" case below. */
        r = full_record();
        r.home_ssid[0] = '\0';
        CHECK_EQ(ho_record_encode(&r, buf, sizeof(buf)), 0, "a present network needs a name");

        r = full_record();
        r.home_ssid[0] = '\x01';
        CHECK_EQ(ho_record_encode(&r, buf, sizeof(buf)), 0, "a control character in an SSID is refused");

        r = full_record();
        strcpy(r.home_pass, "seven77");
        CHECK_EQ(ho_record_encode(&r, buf, sizeof(buf)), 0, "a 7-character password is refused");

        r = full_record();
        strcpy(r.ap_pass, "1234567");
        CHECK_EQ(ho_record_encode(&r, buf, sizeof(buf)), 0, "the setup network cannot be open");

        r = full_record();
        r.host[0] = '\0';
        CHECK_EQ(ho_record_encode(&r, buf, sizeof(buf)), 0, "a record needs a hostname");

        r = full_record();
        r.has_home = false;
        CHECK_EQ(ho_record_encode(&r, buf, sizeof(buf)), 0, "a cleared flag with a name is refused");

        r = full_record();
        CHECK_EQ(ho_record_encode(&r, buf, 10), 0, "a record does not fit a 10-byte buffer");

        /* A frame whose flag and fields disagree: home claimed, with an empty name. Every
         * length is inside the frame, so only the field rule can refuse it. */
        uint8_t bad[12];
        memset(bad, 0, sizeof(bad));
        bad[0] = HO_VERSION;
        bad[1] = HO_TYPE_RECORD;
        bad[2] = 0x01; /* home present */
        bad[3] = 0;    /* home ssid: empty */
        bad[4] = 0;    /* home password: empty */
        bad[5] = 1;    /* host */
        bad[6] = 'k';
        bad[7] = 192;
        bad[8] = 168;
        bad[9] = 0;
        bad[10] = 44;
        bad[11] = test_crc8(bad, 11);
        ho_record_t back;
        CHECK(!ho_record_decode(bad, 12, &back), "a flag without its fields is refused");

        /* Every way a good frame can be damaged. */
        r = full_record();
        size_t n = ho_record_encode(&r, buf, sizeof(buf));
        CHECK(n > 0, "the reference record encodes");
        CHECK(!ho_record_decode(buf, n - 1, &back), "a truncated record is refused");
        buf[n] = 0;
        CHECK(!ho_record_decode(buf, n + 1, &back), "a record with a trailing byte is refused");
        buf[n - 1] ^= 0xFF;
        CHECK(!ho_record_decode(buf, n, &back), "a corrupted checksum is refused");
        buf[n - 1] ^= 0xFF;
        CHECK(ho_record_decode(buf, n, &back), "and the record is good again");
        buf[0] = HO_VERSION + 1;
        CHECK(!ho_record_decode(buf, n, &back), "an unknown version is refused");
        buf[0] = HO_VERSION;
        buf[1] = HO_TYPE_BEACON;
        CHECK(!ho_record_decode(buf, n, &back), "a beacon is not a record");
        buf[1] = HO_TYPE_RECORD;
        buf[2] |= 0x04;
        CHECK(!ho_record_decode(buf, n, &back), "an unknown flag bit is refused");
        buf[2] = 0x03;
        buf[3] = 40; /* the home SSID claims 40 bytes */
        buf[n - 1] = test_crc8(buf, n - 1);
        CHECK(!ho_record_decode(buf, n, &back), "an over-long SSID is refused even with a good checksum");
    }

    TEST_END();
}
