#pragma once
/*
 * wifi_record - the network the device joins, and the two secrets around it.
 *
 * One record: the home network's SSID, its password, and the setup network's password.
 * It is the only place those three live; the firmware compiles none of them in, and the
 * board adapter writes the encoded bytes to NVS. ADR 0076 is the decision;
 * docs/specs/wifi-provisioning.md §5 is the contract.
 *
 * The type is not called wifi_config_t because ESP-IDF already owns that name
 * (esp_wifi_types.h): one struct in the same file cannot be two things, and this record
 * is not a radio configuration - it is what the device remembers.
 *
 * There is no CRC in the record, and that is deliberate. It is an NVS blob, and NVS
 * validates its own entry checksum on every read, so a damaged record fails before this
 * module sees it. A second checksum would only be able to disagree with the first.
 *
 * Layout (all lengths are bytes):
 *
 *   0   4   magic "K7WF"
 *   4   1   version = 1
 *   5   1   ssid_len       1..32
 *   6   1   pass_len       0..63   (0 = the network is open)
 *   7   1   setup_pass_len 0..63   (0 = the default, "12345678")
 *   8   ssid_len        the SSID, no terminator
 *   ..  pass_len        the network password
 *   ..  setup_pass_len  the setup network's password
 *
 * "Unprovisioned" is the absence of a record, never a record with an empty SSID: the
 * device clears its record by erasing the key, and decode() refuses an empty SSID
 * outright rather than letting a second state mean the same thing.
 *
 * The passwords are plaintext. There is no flash encryption on this board and no NVS key
 * partition; a person holding the device can read them out of the flash and can also
 * reflash it. Provisioning's specification says so rather than implying otherwise.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* WPA2-PSK's limits, and SSIDs': the same numbers esp_wifi enforces, named once. */
#define WIFI_RECORD_SSID_MAX 32
#define WIFI_RECORD_PASS_MAX 63

/* The largest record encode() can produce, and the buffer size a reader needs. */
#define WIFI_RECORD_BLOB_MAX (8 + WIFI_RECORD_SSID_MAX + 2 * WIFI_RECORD_PASS_MAX)

/* Where the record lives. The adapter owns the storage; these name it once. */
#define WIFI_RECORD_NAMESPACE "k7wifi"
#define WIFI_RECORD_KEY "rec"

/* The setup network's password until a person changes it. Printed on the panel and in
 * the README, so it is a published default by design, not an accident. */
#define WIFI_RECORD_DEFAULT_SETUP_PASSWORD "12345678"

typedef struct {
    /* The home network. Empty means unprovisioned, and encode() refuses it. */
    char ssid[WIFI_RECORD_SSID_MAX + 1];
    /* The home network's password. Empty with a non-empty SSID is an open network. */
    char pass[WIFI_RECORD_PASS_MAX + 1];
    /* The setup network's password. Empty means WIFI_RECORD_DEFAULT_SETUP_PASSWORD. */
    char setup_pass[WIFI_RECORD_PASS_MAX + 1];
} wifi_record_t;

/* Zero the record: unprovisioned, with the default setup password implied. */
void wifi_record_clear(wifi_record_t *rec);

/* Whether the record names a home network. */
bool wifi_record_provisioned(const wifi_record_t *rec);

/* The setup password in force: the record's own when it has one, the default otherwise.
 * Points into `rec` or at the constant - never at a temporary. */
const char *wifi_record_setup_password(const wifi_record_t *rec);

/* One rule, three readings: the page's field, the route's answer and this module's
 * header comment all say the same thing because they all call these.
 *
 * An SSID is 1..32 bytes with no control characters; non-ASCII bytes are allowed,
 * because a network name is UTF-8 and rejecting it would refuse networks people have.
 * A password is 8..63 printable ASCII (0x20..0x7e): WPA2-PSK's rule, which also keeps a
 * password out of a byte range the JSON or a terminal would mangle. */
bool wifi_record_valid_ssid(const char *ssid);
bool wifi_record_valid_password(const char *pass);

/* The whole record: a valid SSID, and each password either empty or valid. */
bool wifi_record_valid(const wifi_record_t *rec);

/* Encode the record into `out`. Returns the bytes written, or 0 when the record is
 * invalid - which is how "a bad password is never stored" is enforced: the adapter
 * encodes before it opens NVS, so a rejected record never reaches the flash. */
size_t wifi_record_encode(const wifi_record_t *rec, uint8_t *out, size_t cap);

/* Decode a blob into `rec`. False - with `rec` cleared - for any blob that is not
 * exactly one valid record: short, over-long, wrong magic, wrong version, a length that
 * does not match its bytes, or a field that fails the rules above. Never repairs. */
bool wifi_record_decode(wifi_record_t *rec, const uint8_t *blob, size_t len);

#ifdef __cplusplus
}
#endif
