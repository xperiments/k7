/*
 * handover_frame - the three frames a share window exchanges.
 *
 * The layouts are docs/specs/network-handover.md §5's tables, byte for byte. Every length is
 * read with a bounds check before it is used, so a hostile or truncated frame cannot walk
 * the reader off the end; and everything decoded is validated again with the same rules the
 * encoder applies, so a value that one side would refuse to write is refused on arrival.
 */
#include "handover.h"

#include <string.h>

/* The checksum both sides compute: CRC-8/ATM - polynomial 0x07, init 0x00, no reflection,
 * no final xor. The check value for "123456789" is 0xF4. */
static uint8_t crc8(const uint8_t *p, size_t n)
{
    uint8_t c = 0x00;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int bit = 0; bit < 8; bit++) {
            if (c & 0x80u)
                c = (uint8_t)((c << 1) ^ 0x07u);
            else
                c = (uint8_t)(c << 1);
        }
    }
    return c;
}

/* An SSID is 1..32 bytes with no control characters, which is what the device's own record
 * accepts and what keeps it safe as a C string on both sides. */
static bool ssid_ok(const char *s)
{
    size_t n = strlen(s);
    if (n < 1 || n > HO_SSID_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7f) return false;
    }
    return true;
}

/* A WPA2 passphrase is printable ASCII. A home network may be open (0 bytes); a setup
 * network may not (8..63), which the caller passes as min. */
static bool pass_ok(const char *s, size_t min)
{
    size_t n = strlen(s);
    if (n < min || n > HO_PASS_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c > 0x7e) return false;
    }
    return true;
}

static bool host_ok(const char *s)
{
    size_t n = strlen(s);
    if (n < 1 || n > HO_HOST_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x21 || c > 0x7e) return false; /* a hostname has no spaces */
    }
    return true;
}

static bool record_ok(const ho_record_t *r)
{
    if (r->has_home) {
        if (!ssid_ok(r->home_ssid)) return false;
        if (r->home_pass[0] && !pass_ok(r->home_pass, 8)) return false;
    } else if (r->home_ssid[0] || r->home_pass[0]) {
        return false;
    }
    if (r->has_ap) {
        if (!ssid_ok(r->ap_ssid)) return false;
        if (!pass_ok(r->ap_pass, 8)) return false;
    } else if (r->ap_ssid[0] || r->ap_pass[0]) {
        return false;
    }
    return host_ok(r->host);
}

/* ---------------------------------------------------------------- beacon */

size_t ho_beacon_encode(const ho_beacon_t *b, uint8_t *out, size_t cap)
{
    if (!b || !out || cap < HO_BEACON_SIZE) return 0;
    if (b->channel < 1 || b->channel > 13) return 0;
    if (b->seconds_left > 60) return 0;

    out[0] = HO_VERSION;
    out[1] = HO_TYPE_BEACON;
    memcpy(out + 2, b->pub, HO_KEY_LEN);
    out[34] = b->channel;
    out[35] = b->seconds_left;
    return HO_BEACON_SIZE;
}

bool ho_beacon_decode(const uint8_t *in, size_t len, ho_beacon_t *out)
{
    if (!in || !out || len != HO_BEACON_SIZE) return false;
    if (in[0] != HO_VERSION || in[1] != HO_TYPE_BEACON) return false;
    if (in[34] < 1 || in[34] > 13) return false;
    if (in[35] > 60) return false;

    memcpy(out->pub, in + 2, HO_KEY_LEN);
    out->channel = in[34];
    out->seconds_left = in[35];
    return true;
}

/* ---------------------------------------------------------------- request */

size_t ho_request_encode(const ho_request_t *r, uint8_t *out, size_t cap)
{
    if (!r || !out || cap < HO_REQUEST_SIZE) return 0;

    out[0] = HO_VERSION;
    out[1] = HO_TYPE_REQUEST;
    memcpy(out + 2, r->pub, HO_KEY_LEN);
    return HO_REQUEST_SIZE;
}

bool ho_request_decode(const uint8_t *in, size_t len, ho_request_t *out)
{
    if (!in || !out || len != HO_REQUEST_SIZE) return false;
    if (in[0] != HO_VERSION || in[1] != HO_TYPE_REQUEST) return false;

    memcpy(out->pub, in + 2, HO_KEY_LEN);
    return true;
}

/* ---------------------------------------------------------------- record */

size_t ho_record_encode(const ho_record_t *r, uint8_t *out, size_t cap)
{
    if (!r || !out || !record_ok(r)) return 0;

    size_t need = 3; /* version, type, flags */
    if (r->has_home)
        need += 1 + strlen(r->home_ssid) + 1 + strlen(r->home_pass);
    if (r->has_ap)
        need += 1 + strlen(r->ap_ssid) + 1 + strlen(r->ap_pass);
    need += 1 + strlen(r->host) + 4 + 1;

    if (need > HO_FRAME_MAX || need > cap) return 0;

    uint8_t *p = out;
    *p++ = HO_VERSION;
    *p++ = HO_TYPE_RECORD;
    *p++ = (uint8_t)((r->has_home ? 0x01u : 0u) | (r->has_ap ? 0x02u : 0u));

    if (r->has_home) {
        size_t n = strlen(r->home_ssid);
        *p++ = (uint8_t)n;
        memcpy(p, r->home_ssid, n);
        p += n;
        n = strlen(r->home_pass);
        *p++ = (uint8_t)n;
        memcpy(p, r->home_pass, n);
        p += n;
    }
    if (r->has_ap) {
        size_t n = strlen(r->ap_ssid);
        *p++ = (uint8_t)n;
        memcpy(p, r->ap_ssid, n);
        p += n;
        n = strlen(r->ap_pass);
        *p++ = (uint8_t)n;
        memcpy(p, r->ap_pass, n);
        p += n;
    }

    size_t n = strlen(r->host);
    *p++ = (uint8_t)n;
    memcpy(p, r->host, n);
    p += n;

    *p++ = (uint8_t)(r->ipv4 >> 24);
    *p++ = (uint8_t)(r->ipv4 >> 16);
    *p++ = (uint8_t)(r->ipv4 >> 8);
    *p++ = (uint8_t)r->ipv4;

    *p = crc8(out, (size_t)(p - out));
    return (size_t)(p - out) + 1;
}

/* Copy a length-prefixed string out of the frame, refusing a length over the field's own
 * maximum or one that runs past the end. p advances past the field on success only. */
static bool get_str(const uint8_t *in, size_t len, size_t *p, char *dst, size_t max)
{
    if (*p >= len) return false;
    size_t n = in[*p];
    if (n > max) return false;
    if (*p + 1 + n > len) return false;
    memcpy(dst, in + *p + 1, n);
    dst[n] = '\0';
    *p += 1 + n;
    return true;
}

bool ho_record_decode(const uint8_t *in, size_t len, ho_record_t *out)
{
    if (!in || !out || len < 10 || len > HO_RECORD_MAX) return false;
    if (in[0] != HO_VERSION || in[1] != HO_TYPE_RECORD) return false;
    if (in[2] & (uint8_t)~0x03u) return false;

    memset(out, 0, sizeof(*out));
    out->has_home = (in[2] & 0x01u) != 0;
    out->has_ap = (in[2] & 0x02u) != 0;

    size_t p = 3;
    if (out->has_home) {
        if (!get_str(in, len - 1, &p, out->home_ssid, HO_SSID_MAX)) return false;
        if (!get_str(in, len - 1, &p, out->home_pass, HO_PASS_MAX)) return false;
    }
    if (out->has_ap) {
        if (!get_str(in, len - 1, &p, out->ap_ssid, HO_SSID_MAX)) return false;
        if (!get_str(in, len - 1, &p, out->ap_pass, HO_PASS_MAX)) return false;
    }
    if (!get_str(in, len - 1, &p, out->host, HO_HOST_MAX)) return false;

    if (p + 4 + 1 != len) return false;
    out->ipv4 = ((uint32_t)in[p] << 24) | ((uint32_t)in[p + 1] << 16) |
                ((uint32_t)in[p + 2] << 8) | (uint32_t)in[p + 3];
    p += 4;
    if (crc8(in, p) != in[p]) return false;

    return record_ok(out);
}
