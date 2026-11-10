/*
 * wifi_record - the record's codec and its rules. No NVS, no esp_wifi, no logging: the
 * adapter stores these bytes and the page and the route validate through them, so the
 * host tests exercise the same rules the device enforces.
 */
#include "wifi_record.h"

#include <string.h>

#define MAGIC0 'K'
#define MAGIC1 '7'
#define MAGIC2 'W'
#define MAGIC3 'F'
#define VERSION 1u

static size_t bounded_len(const char *s, size_t cap)
{
    size_t n = 0;
    while (n < cap && s[n]) n++;
    return n;
}

void wifi_record_clear(wifi_record_t *rec)
{
    if (!rec) return;
    memset(rec, 0, sizeof *rec);
}

bool wifi_record_provisioned(const wifi_record_t *rec)
{
    return rec && rec->ssid[0] != '\0';
}

const char *wifi_record_setup_password(const wifi_record_t *rec)
{
    if (rec && rec->setup_pass[0]) return rec->setup_pass;
    return WIFI_RECORD_DEFAULT_SETUP_PASSWORD;
}

bool wifi_record_valid_ssid(const char *ssid)
{
    if (!ssid) return false;
    size_t n = bounded_len(ssid, WIFI_RECORD_SSID_MAX + 1);
    if (n == 0 || n > WIFI_RECORD_SSID_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)ssid[i];
        if (c < 0x20 || c == 0x7f) return false;
    }
    return true;
}

bool wifi_record_valid_password(const char *pass)
{
    if (!pass) return false;
    size_t n = bounded_len(pass, WIFI_RECORD_PASS_MAX + 1);
    if (n < 8 || n > WIFI_RECORD_PASS_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)pass[i];
        if (c < 0x20 || c > 0x7e) return false;
    }
    return true;
}

bool wifi_record_valid(const wifi_record_t *rec)
{
    if (!rec) return false;
    if (!wifi_record_valid_ssid(rec->ssid)) return false;
    if (rec->pass[0] && !wifi_record_valid_password(rec->pass)) return false;
    if (rec->setup_pass[0] && !wifi_record_valid_password(rec->setup_pass)) return false;
    return true;
}

size_t wifi_record_encode(const wifi_record_t *rec, uint8_t *out, size_t cap)
{
    if (!out || !wifi_record_valid(rec)) return 0;

    size_t ssid_len = bounded_len(rec->ssid, WIFI_RECORD_SSID_MAX);
    size_t pass_len = rec->pass[0] ? bounded_len(rec->pass, WIFI_RECORD_PASS_MAX) : 0;
    size_t setup_len = rec->setup_pass[0] ? bounded_len(rec->setup_pass, WIFI_RECORD_PASS_MAX) : 0;
    size_t total = 8 + ssid_len + pass_len + setup_len;
    if (total > cap) return 0;

    out[0] = MAGIC0;
    out[1] = MAGIC1;
    out[2] = MAGIC2;
    out[3] = MAGIC3;
    out[4] = VERSION;
    out[5] = (uint8_t)ssid_len;
    out[6] = (uint8_t)pass_len;
    out[7] = (uint8_t)setup_len;
    memcpy(out + 8, rec->ssid, ssid_len);
    memcpy(out + 8 + ssid_len, rec->pass, pass_len);
    memcpy(out + 8 + ssid_len + pass_len, rec->setup_pass, setup_len);
    return total;
}

bool wifi_record_decode(wifi_record_t *rec, const uint8_t *blob, size_t len)
{
    wifi_record_clear(rec);
    if (!rec || !blob || len < 8) return false;
    if (blob[0] != MAGIC0 || blob[1] != MAGIC1 || blob[2] != MAGIC2 || blob[3] != MAGIC3) return false;
    if (blob[4] != VERSION) return false;

    size_t ssid_len = blob[5];
    size_t pass_len = blob[6];
    size_t setup_len = blob[7];
    /* The lengths must be the bytes in front of them: a record that is short, or that
     * carries trailing bytes, is not one this module wrote. */
    if (len != 8 + ssid_len + pass_len + setup_len) return false;
    if (ssid_len == 0 || ssid_len > WIFI_RECORD_SSID_MAX) return false;
    if (pass_len > WIFI_RECORD_PASS_MAX || setup_len > WIFI_RECORD_PASS_MAX) return false;

    memcpy(rec->ssid, blob + 8, ssid_len);
    rec->ssid[ssid_len] = '\0';
    memcpy(rec->pass, blob + 8 + ssid_len, pass_len);
    rec->pass[pass_len] = '\0';
    memcpy(rec->setup_pass, blob + 8 + ssid_len + pass_len, setup_len);
    rec->setup_pass[setup_len] = '\0';

    if (!wifi_record_valid(rec)) {
        wifi_record_clear(rec);
        return false;
    }
    return true;
}
