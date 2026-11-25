/*
 * The host build's board adapter.
 *
 * There is no Wi-Fi and no SPI card here: the music directory is a path and it is
 * "mounted" unless XTAPE_HOST_NO_CARD is set, the host-only switch the acceptance
 * tests use for the cardless state. The adapter exists so the services composition
 * the firmware runs is the composition the host tests and integration suite run -
 * the board is the only thing that differs.
 */
#include "app_board.h"
#include "device_name.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

esp_err_t app_board_net_start(uint16_t http_port) { (void)http_port; return ESP_OK; }

/* The network record, which has no radio behind it here. By default it lives in this
 * process's memory, so a unit test can round-trip it; XTAPE_HOST_WIFI names a file
 * instead, which is how the integration tests give the server a network and read back
 * what it stored. An absent file is an unprovisioned device, the same way an absent
 * record is one on the board. */
static bool s_record_has;
static wifi_record_t s_record;

static const char *host_record_path(void)
{
    const char *path = getenv("XTAPE_HOST_WIFI");
    return (path && path[0]) ? path : NULL;
}

/* The ESP-NOW emitter's seam. The host has no radio, so a test that asks for ESP-NOW is told
 * there is none and the transport falls back to HTTP, which is what the firmware does too. */
bool app_board_audio_set(bool on)
{
    (void)on;
    return false;
}

void app_board_audio_frame(void *ctx, const uint8_t *frame, size_t bytes)
{
    (void)ctx;
    (void)frame;
    (void)bytes;
}

void app_board_audio_tone(const float band_db[3], float gain_db)
{
    (void)band_db;
    (void)gain_db;
}

bool app_board_player_tone_capable(void) { return false; }

bool app_board_player_espnow_load(bool *on)
{
    (void)on;
    return false; /* no stored choice: the firmware's default is ESP-NOW */
}

esp_err_t app_board_player_espnow_save(bool on)
{
    (void)on;
    return ESP_OK;
}

/* The host draws into a PSRAM framebuffer with no scan-out behind it: there is no picture
 * to settle. */
void app_board_set_flash_settled(app_board_flash_settled_fn fn) { (void)fn; }

bool app_board_wifi_record_load(wifi_record_t *out)
{
    if (!out) return false;
    wifi_record_clear(out);

    const char *path = host_record_path();
    if (path) {
        FILE *f = fopen(path, "rb");
        if (!f) return false;
        uint8_t blob[WIFI_RECORD_BLOB_MAX];
        size_t len = fread(blob, 1, sizeof blob, f);
        fclose(f);
        return wifi_record_decode(out, blob, len);
    }
    if (!s_record_has) return false;
    *out = s_record;
    return true;
}

esp_err_t app_board_wifi_record_save(const wifi_record_t *rec)
{
    if (!rec) return ESP_ERR_INVALID_ARG;

    uint8_t blob[WIFI_RECORD_BLOB_MAX];
    size_t len = wifi_record_encode(rec, blob, sizeof blob);
    if (len == 0 && wifi_record_provisioned(rec)) return ESP_ERR_INVALID_ARG;

    const char *path = host_record_path();
    if (!wifi_record_provisioned(rec)) {
        s_record_has = false;
        wifi_record_clear(&s_record);
        if (path) remove(path); /* a file that is not there is not an error */
        return ESP_OK;
    }
    s_record = *rec;
    s_record_has = true;
    if (path) {
        FILE *f = fopen(path, "wb");
        if (!f) return ESP_FAIL;
        size_t wrote = fwrite(blob, 1, len, f);
        fclose(f);
        if (wrote != len) return ESP_FAIL;
    }
    return ESP_OK;
}

/* The setup password: beside the record when the test names a file, in this process
 * otherwise - the same two shapes as the record itself. */
static char s_setup_password[64];

static bool host_password_path(char *out, size_t cap)
{
    const char *path = host_record_path();
    if (!path) return false;
    snprintf(out, cap, "%s.ap", path);
    return true;
}

bool app_board_setup_password_load(char *out, size_t cap)
{
    if (!out || cap == 0) return false;
    out[0] = '\0';
    char path[512];
    if (host_password_path(path, sizeof path)) {
        FILE *f = fopen(path, "rb");
        if (!f) return false;
        char buf[80] = {0};
        size_t got = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        buf[got] = '\0';
        while (got && (buf[got - 1] == '\n' || buf[got - 1] == '\r')) buf[--got] = '\0';
        if (!wifi_record_valid_password(buf)) return false;
        snprintf(out, cap, "%s", buf);
        return true;
    }
    if (!s_setup_password[0]) return false;
    snprintf(out, cap, "%s", s_setup_password);
    return true;
}

esp_err_t app_board_setup_password_save(const char *password)
{
    if (!password) return ESP_ERR_INVALID_ARG;
    if (*password && !wifi_record_valid_password(password)) return ESP_ERR_INVALID_ARG;
    snprintf(s_setup_password, sizeof s_setup_password, "%s", password);
    char path[512];
    if (host_password_path(path, sizeof path)) {
        if (!*password) { remove(path); return ESP_OK; }
        FILE *f = fopen(path, "wb");
        if (!f) return ESP_FAIL;
        size_t len = strlen(password);
        size_t wrote = fwrite(password, 1, len, f);
        fclose(f);
        return wrote == len ? ESP_OK : ESP_FAIL;
    }
    return ESP_OK;
}

/* The host card, with the board's semantics: a card is present only while it is
 * mounted, so the watcher has to try a mount to see one arrive. Three switches let
 * the integration suite drive what a plain directory cannot:
 *
 *   XTAPE_HOST_NO_CARD        the always-out switch the cardless acceptance tests use
 *   XTAPE_HOST_CARD_FILE      a one-word file, "in" or "out", re-read on every probe
 *                             and mount, so a test can eject and reinsert a card
 *   XTAPE_HOST_CARD_FAIL_FILE when the file exists, the next probe fails with EIO and
 *                             the file is removed - one transient, not a removal
 */
static bool s_mounted;

static bool host_card_in_slot(void)
{
    if (getenv("XTAPE_HOST_NO_CARD")) return false;
    const char *path = getenv("XTAPE_HOST_CARD_FILE");
    if (!path || !path[0]) return true; /* no script: the normal host card */
    FILE *f = fopen(path, "r");
    if (!f) return true; /* a script that has gone is not an ejected card */
    char word[8] = {0};
    bool in = fgets(word, sizeof word, f) && strncmp(word, "out", 3) != 0;
    fclose(f);
    return in;
}

app_board_card_t app_board_card_mount(const char *expected_music_dir)
{
    if (!host_card_in_slot()) {
        app_board_card_t none = {.music_dir = NULL, .mount_error = "host: no card in the slot"};
        return none;
    }
    s_mounted = true;
    app_board_card_t card = {.music_dir = expected_music_dir, .mount_error = NULL};
    return card;
}

const char *app_board_name(void) { return "host"; }

/* The deck's name: beside the record when the test names a file, in this process
 * otherwise - the two shapes the record and the setup password already have. */
static char s_deck_name[DEVICE_NAME_MAX + 1];

static bool host_name_path(char *out, size_t cap)
{
    const char *path = host_record_path();
    if (!path) return false;
    snprintf(out, cap, "%s.name", path);
    return true;
}

bool app_board_name_load(char *out, size_t cap)
{
    if (!out || cap == 0) return false;
    out[0] = '\0';
    char path[512];
    if (host_name_path(path, sizeof path)) {
        FILE *f = fopen(path, "rb");
        if (f) {
            char buf[DEVICE_NAME_MAX + 8] = {0};
            size_t got = fread(buf, 1, sizeof buf - 1, f);
            fclose(f);
            buf[got] = '\0';
            while (got && (buf[got - 1] == '\n' || buf[got - 1] == '\r')) buf[--got] = '\0';
            if (!device_name_word_ok(buf)) return false;
            snprintf(out, cap, "%s", buf);
            return true;
        }
    }
    if (!s_deck_name[0]) return false;
    snprintf(out, cap, "%s", s_deck_name);
    return true;
}

esp_err_t app_board_name_save(const char *word)
{
    if (!word || !device_name_word_ok(word)) return ESP_ERR_INVALID_ARG;
    snprintf(s_deck_name, sizeof s_deck_name, "%s", word);
    char path[512];
    if (!host_name_path(path, sizeof path)) return ESP_OK;
    if (!*word) {
        remove(path);
        return ESP_OK;
    }
    FILE *f = fopen(path, "wb");
    if (!f) return ESP_FAIL;
    size_t wrote = fwrite(word, 1, strlen(word), f);
    fclose(f);
    return wrote == strlen(word) ? ESP_OK : ESP_FAIL;
}

/* The one value, movable here so the host tier reads what every consumer says about a
 * name it did not write. `K7_HOSTNAME` is a test seam (the full label, for a test that
 * wants to move the name without the word's rules); otherwise the stored word derives it,
 * exactly as the board's does. */
const char *app_board_hostname(void)
{
    const char *full = getenv("K7_HOSTNAME");
    if (full && full[0]) return full;
    static char label[DEVICE_NAME_ADDRESS_MAX];
    char word[DEVICE_NAME_MAX + 1] = {0};
    app_board_name_load(word, sizeof word);
    if (!device_name_label(word, label, sizeof label))
        snprintf(label, sizeof label, "%s", DEVICE_NAME_PRODUCT);
    return label;
}

/* No radio, so no MAC: a host is not a deck, and the status document says so rather than
 * inventing an address a home automation could key on. The value is a fixed twelve hex
 * characters, so a reader that requires the shape is still satisfied. */
bool app_board_id(char *out, size_t out_size)
{
    if (!out || out_size < 13) return false;
    snprintf(out, out_size, "%s", "000000000000");
    return true;
}

const char *app_board_version(void) { return "host"; }

void app_board_wait_online(void) { }

bool app_board_local_ip(char *out, size_t out_size)
{
    if (!out || !out_size) return false;
    snprintf(out, out_size, "127.0.0.1");
    return true;
}

esp_err_t app_board_mdns_start(const char *hostname, uint16_t port, const char *path)
{
    (void)hostname;
    (void)port;
    (void)path;
    return ESP_OK; /* there is no mDNS in the host build */
}

void app_board_log_hardware(void) { }

/* The host has no partition, so the Snapshot is a file: XTAPE_SNAPSHOT names it and
 * the integration test points it at a generated image. Absent is the normal host
 * state, not an error. */
/* The host card never carries an update file: the host exercises the snapshot
 * through XTAPE_SNAPSHOT, not through the card. */
bool app_board_card_probe(int *err)
{
    if (err) *err = 0;
    if (!s_mounted) {
        if (err) *err = ENODEV;
        return false;
    }
    const char *fail = getenv("XTAPE_HOST_CARD_FAIL_FILE");
    if (fail && fail[0]) {
        FILE *f = fopen(fail, "r");
        if (f) {
            fclose(f);
            remove(fail);
            if (err) *err = EIO;
            return false;
        }
    }
    if (!host_card_in_slot()) {
        if (err) *err = EIO;
        return false;
    }
    return true;
}
void app_board_card_forget(void) { s_mounted = false; }
bool app_board_snapshot_pending(void) { return false; }

/* The host's partition is the file XTAPE_SNAPSHOT names. Written whole or not at all:
 * a half-written image would be the one thing worse than none. */
app_board_flash_t app_board_snapshot_flash(const uint8_t *image, size_t len, char *why,
                                           size_t why_size)
{
    if (why && why_size) why[0] = 0;
    const char *path = getenv("XTAPE_SNAPSHOT");
    if (!path || !path[0]) {
        if (why) snprintf(why, why_size, "no snapshot partition on the host");
        return APP_BOARD_FLASH_NO_PARTITION;
    }
    if (!image || !len) {
        if (why) snprintf(why, why_size, "the upload is empty");
        return APP_BOARD_FLASH_INVALID;
    }

    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        if (why) snprintf(why, why_size, "cannot write %s", tmp);
        return APP_BOARD_FLASH_FAILED;
    }
    bool ok = fwrite(image, 1, len, f) == len;
    ok = fclose(f) == 0 && ok;
    if (!ok || rename(tmp, path) != 0) {
        remove(tmp);
        if (why) snprintf(why, why_size, "cannot replace %s", path);
        return APP_BOARD_FLASH_FAILED;
    }
    return APP_BOARD_FLASH_OK;
}

void app_board_reboot_soon(uint32_t ms)
{
    (void)ms; /* the test starts the next process itself */
}

/* The image read from XTAPE_SNAPSHOT, held until app_board_snapshot_release(). */
static uint8_t *image;
static size_t   image_size;
static bool     tried;

const uint8_t *app_board_snapshot_image(size_t *size, const char **why, int *state)
{
    if (state) *state = 1; /* SNAPSHOT_NO_PARTITION */
    if (tried) {
        if (size) *size = image_size;
        if (why && !image) *why = "no snapshot partition on the host";
        return image;
    }
    tried = true;
    if (why) *why = "no snapshot partition on the host";

    const char *path = getenv("XTAPE_SNAPSHOT");
    if (!path || !path[0]) return NULL;
    /* From here on a failure is a bad image, not an absent one. */
    if (state) *state = 3; /* SNAPSHOT_INVALID */
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (why) *why = "XTAPE_SNAPSHOT could not be opened";
        return NULL;
    }
    long n = -1;
    if (fseek(f, 0, SEEK_END) == 0) n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        if (why) *why = "XTAPE_SNAPSHOT is not a readable file";
        return NULL;
    }
    image = malloc((size_t)n);
    if (!image || fread(image, 1, (size_t)n, f) != (size_t)n) {
        free(image);
        image = NULL;
        fclose(f);
        if (why) *why = "XTAPE_SNAPSHOT could not be read";
        return NULL;
    }
    fclose(f);
    image_size = (size_t)n;
    if (size) *size = image_size;
    if (why) *why = "";
    if (state) *state = 0; /* SNAPSHOT_OK */
    return image;
}

void app_board_snapshot_release(void)
{
    free(image);
    image = NULL;
    image_size = 0;
    tried = false; /* a later call reads XTAPE_SNAPSHOT again */
}

/* The radio, scripted. The setup machine's policy is exercised through these knobs by the
 * integration tests; without them the fake behaves like a board that joins anything.
 *
 *   XTAPE_HOST_WIFI_JOIN   "ok" (default) or "fail:<reason>[:<disconnects>]"
 *   XTAPE_HOST_WIFI_SCAN   a file of "ssid|rssi|secure(0/1)" lines, the picker's list
 *   XTAPE_HOST_AP_FAIL     set: the setup network cannot start
 *   XTAPE_HOST_AP_STATE    a file the fake writes "on"/"off" to, so a test sees the radio
 */
static app_board_wifi_t s_radio;
static bool s_ap_on;

void app_board_wifi_state(app_board_wifi_t *out)
{
    if (!out) return;
    *out = s_radio;
}

static void host_ap_note(void)
{
    const char *path = getenv("XTAPE_HOST_AP_STATE");
    if (!path || !*path) return;
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fputs(s_ap_on ? "on" : "off", f);
    fclose(f);
}

void app_board_wifi_forget(void)
{
    memset(&s_radio, 0, sizeof s_radio);
}

esp_err_t app_board_setup_ap(bool on)
{
    if (on == s_ap_on) return ESP_OK;
    /* A start the tests can make slow: it is what a real esp_wifi_set_mode() costs, and it
     * is the window in which a lock held across the call would freeze the panel. */
    const char *ap_ms = getenv("XTAPE_HOST_AP_MS");
    if (on && ap_ms && *ap_ms) usleep((useconds_t)(atoi(ap_ms) * 1000));
    if (on && getenv("XTAPE_HOST_AP_FAIL")) {
        host_ap_note();
        return ESP_FAIL;
    }
    s_ap_on = on;
    host_ap_note();
    return ESP_OK;
}

esp_err_t app_board_wifi_join(const char *ssid, const char *pass)
{
    (void)pass;
    if (!ssid || !wifi_record_valid_ssid(ssid)) return ESP_ERR_INVALID_ARG;

    s_radio.disconnects = 0;
    s_radio.last_reason = 0;
    const char *script = getenv("XTAPE_HOST_WIFI_JOIN");
    if (!script || strcmp(script, "ok") == 0) {
        s_radio.associated = true;
        snprintf(s_radio.ip, sizeof s_radio.ip, "%s", "192.168.0.44");
        return ESP_OK;
    }
    int reason = 0, disconnects = 0;
    if (strncmp(script, "fail:", 5) == 0) {
        reason = atoi(script + 5);
        const char *colon = strchr(script + 5, ':');
        disconnects = colon ? atoi(colon + 1) : 0;
    }
    if (disconnects <= 0) disconnects = 5;
    s_radio.associated = false;
    s_radio.disconnects = disconnects;
    s_radio.last_reason = reason;
    s_radio.ip[0] = '\0';
    return ESP_OK;
}

int app_board_wifi_scan(app_board_scan_t *out, int max)
{
    if (!out || max <= 0) return -1;

    /* A scan the tests can make slow, so "one at a time" is observable (a 429). */
    const char *delay = getenv("XTAPE_HOST_SCAN_MS");
    if (delay && *delay) usleep((useconds_t)(atoi(delay) * 1000));

    const char *path = getenv("XTAPE_HOST_WIFI_SCAN");
    int count = 0;
    if (!path || !*path) {
        const app_board_scan_t fixed[] = {
            {"xperiments-2.4G", -41, true},
            {"Cafe Guest", -66, false},
            {"neighbour-5g", -80, true},
        };
        for (size_t i = 0; i < sizeof fixed / sizeof fixed[0] && count < max; i++)
            out[count++] = fixed[i];
        return count;
    }

    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[160];
    while (count < max && fgets(line, sizeof line, f)) {
        char *bar1 = strchr(line, '|');
        if (!bar1) continue;
        *bar1 = '\0';
        char *bar2 = strchr(bar1 + 1, '|');
        if (!bar2) continue;
        *bar2 = '\0';
        if (line[0] == '\0') continue;
        snprintf(out[count].ssid, sizeof out[count].ssid, "%s", line);
        out[count].rssi = (int8_t)atoi(bar1 + 1);
        out[count].secure = atoi(bar2 + 1) != 0;
        count++;
    }
    fclose(f);
    for (int i = 1; i < count; i++) { /* strongest first, as the board sorts it */
        app_board_scan_t row = out[i];
        int k = i - 1;
        while (k >= 0 && out[k].rssi < row.rssi) { out[k + 1] = out[k]; k--; }
        out[k + 1] = row;
    }
    return count;
}

/* --------------------------------------------------------------- the share window
 *
 * There is no radio here either. The window's state machine is tested against a fake that
 * records what it was asked to send, and whose curve is a stand-in - the real one is the
 * board's and PL-09's evidence. Everything the test needs to know is in a file:
 *
 *   XTAPE_HOST_SHARE_STATE    "on"/"off", written when the radio starts and stops
 *   XTAPE_HOST_SHARE_SENT     one line per frame, "<dst>|<data>", dst "bc" for a broadcast
 *   XTAPE_HOST_SHARE_PEER     "<mac>|<lmk>", or "none" when the peer is dropped
 *   XTAPE_HOST_SHARE_CHANNEL  the channel the window beacons on (default 6)
 *   XTAPE_HOST_SHARE_AP_PASS  the setup password in force (default 12345678)
 *   XTAPE_HOST_SHARE_FAIL     set: ESP-NOW refuses to start
 *
 * The stand-in curve is `pub = priv ^ 0xA5` and `shared = priv ^ peer ^ 0x3C`. A test that
 * checks a key can compute the same two lines; what it is checking is that the window used
 * the right two keys in the right order, not that X25519 works.
 */

static app_board_share_frame_fn s_share_cb;
static void *s_share_ctx;
static bool s_share_on;

static void host_share_text(const char *env, const char *text)
{
    const char *path = getenv(env);
    if (!path || !*path) return;
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fputs(text, f);
    fclose(f);
}

static void host_share_hex(FILE *f, const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++) fprintf(f, "%02x", b[i]);
}

void app_board_share_net(app_board_share_net_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof *out);

    wifi_record_t rec;
    if (app_board_wifi_record_load(&rec)) {
        out->has_home = true;
        snprintf(out->home_ssid, sizeof out->home_ssid, "%s", rec.ssid);
        snprintf(out->home_pass, sizeof out->home_pass, "%s", rec.pass);
    }

    const char *ap = getenv("XTAPE_HOST_SHARE_AP_PASS");
    snprintf(out->ap_pass, sizeof out->ap_pass, "%s",
             (ap && *ap) ? ap : WIFI_RECORD_DEFAULT_SETUP_PASSWORD);

    if (s_radio.associated && s_radio.ip[0]) {
        unsigned a = 0, b = 0, c = 0, d = 0;
        if (sscanf(s_radio.ip, "%u.%u.%u.%u", &a, &b, &c, &d) == 4)
            out->ipv4 = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) |
                        (uint32_t)d;
    }

    const char *ch = getenv("XTAPE_HOST_SHARE_CHANNEL");
    out->channel = (uint8_t)((ch && *ch) ? atoi(ch) : 6);
}

bool app_board_share_radio_start(app_board_share_frame_fn on_frame, void *ctx)
{
    if (getenv("XTAPE_HOST_SHARE_FAIL")) {
        host_share_text("XTAPE_HOST_SHARE_STATE", "off");
        return false;
    }
    s_share_cb = on_frame;
    s_share_ctx = ctx;
    s_share_on = true;
    host_share_text("XTAPE_HOST_SHARE_STATE", "on");
    return true;
}

void app_board_share_radio_stop(void)
{
    if (!s_share_on) return;
    s_share_on = false;
    s_share_cb = NULL;
    s_share_ctx = NULL;
    host_share_text("XTAPE_HOST_SHARE_STATE", "off");
}

bool app_board_share_send(const uint8_t *dst, const uint8_t *data, size_t len)
{
    if (!s_share_on || !data || !len) return false;
    const char *path = getenv("XTAPE_HOST_SHARE_SENT");
    if (!path || !*path) return true;
    FILE *f = fopen(path, "ab");
    if (!f) return false;
    if (dst)
        host_share_hex(f, dst, 6);
    else
        fputs("bc", f);
    fputc('|', f);
    host_share_hex(f, data, len);
    fputc('\n', f);
    fclose(f);
    return true;
}

bool app_board_share_peer(const uint8_t mac[6], const uint8_t lmk[16])
{
    if (!s_share_on || !mac || !lmk) return false;
    const char *path = getenv("XTAPE_HOST_SHARE_PEER");
    if (!path || !*path) return true;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    host_share_hex(f, mac, 6);
    fputc('|', f);
    host_share_hex(f, lmk, 16);
    fclose(f);
    return true;
}

void app_board_share_peer_drop(const uint8_t mac[6])
{
    (void)mac;
    host_share_text("XTAPE_HOST_SHARE_PEER", "none");
}

void app_board_random(uint8_t *out, size_t len)
{
    if (!out) return;
    for (size_t i = 0; i < len; i++) out[i] = (uint8_t)i; /* deterministic: the test's stand-in */
}

bool app_board_x25519_public(const uint8_t priv[32], uint8_t pub[32])
{
    if (!priv || !pub) return false;
    for (int i = 0; i < 32; i++) pub[i] = (uint8_t)(priv[i] ^ 0xA5);
    return true;
}

bool app_board_x25519(const uint8_t priv[32], const uint8_t peer_pub[32], uint8_t shared[32])
{
    if (!priv || !peer_pub || !shared) return false;
    for (int i = 0; i < 32; i++) shared[i] = (uint8_t)(priv[i] ^ peer_pub[i] ^ 0x3C);
    return true;
}

/* Host-only: deliver a frame through the callback the window registered, as the radio
 * would. False when no window is listening. */
bool host_share_deliver(const uint8_t src[6], const uint8_t *data, size_t len)
{
    if (!s_share_on || !s_share_cb) return false;
    s_share_cb(src, data, len, s_share_ctx);
    return true;
}
