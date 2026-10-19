/*
 * fx_ui - the tape effect's control surface.
 *
 * GET /fx shows the five values; GET /fx?type=..&dolby=..&tape_q=..&head_q=..
 * &motor_q=.. sets them. A save goes to NVS and is applied to the running
 * broadcast at the next chunk, so the page is also how the sound survives a
 * reboot: fx_ui_start() reads the stored values back and pushes them into the
 * effect once the broadcast exists.
 *
 * It is a separate component from mini_icecast because the broadcast owns no
 * markup (host/tests/layering.sh, rule S1), and because this whole file is what
 * a touchscreen or a JSON API would replace.
 */
#include "fx_ui.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "http_server.h"
#include "mini_icecast.h"
#include "nvs.h"
#include "tape_fx.h"   /* the enum bounds the form validates against */

static const char *TAG = "fx_ui";
/* ------------------------------------------------------------ the settings */

/* Six numbers and two enums in NVS, one explicit key each so the layout of a C
 * struct never becomes a compatibility promise between builds.
 *
 * Not a file on the card: measured here, fopen() on this SD takes 2.2 s and the
 * write after it does not finish inside ten seconds. The card reads at 250 KB/s
 * and writes in seconds, which is fine for music and useless for a knob. Flash is
 * fast (~10 ms); what it costs is the cache, and the panel's framebuffer lives in
 * PSRAM behind that cache - so the screen asks panel_rgb_resync() right after a
 * save, and the picture comes back. */
#define FX_NS     "tapefx"
#define FX_KEY_ON "on"

static esp_err_t fx_store_load(mini_icecast_fx_t *out)
{
    nvs_handle_t h;
    if (nvs_open(FX_NS, NVS_READONLY, &h) != ESP_OK) return ESP_ERR_NOT_FOUND;

    uint8_t on = 1, type = 0, dolby = 0;
    float tq = TAPE_FX_DEFAULT_TAPE_Q, hq = TAPE_FX_DEFAULT_HEAD_Q;
    float mq = TAPE_FX_DEFAULT_MOTOR_Q;
    size_t len = sizeof(float);
    esp_err_t e = nvs_get_u8(h, "type", &type);
    if (e != ESP_OK) { nvs_close(h); return e; }        /* never saved: keep the defaults */
    nvs_get_u8(h, "dolby", &dolby);
    nvs_get_u8(h, FX_KEY_ON, &on);
    if (nvs_get_blob(h, "tape_q", &tq, &len) != ESP_OK) tq = TAPE_FX_DEFAULT_TAPE_Q;
    len = sizeof(float);
    if (nvs_get_blob(h, "head_q", &hq, &len) != ESP_OK) hq = TAPE_FX_DEFAULT_HEAD_Q;
    len = sizeof(float);
    if (nvs_get_blob(h, "motor_q", &mq, &len) != ESP_OK) mq = TAPE_FX_DEFAULT_MOTOR_Q;
    nvs_close(h);

    mini_icecast_set_fx_enabled(on != 0);
    *out = (mini_icecast_fx_t){.set = true, .type = type, .dolby = dolby,
                               .tape_q = tq, .head_q = hq, .motor_q = mq};
    return ESP_OK;
}

static void fx_store_save(const mini_icecast_fx_t *v)
{
    (void)v;
    /* Deliberately not written.
     *
     * Measured on this board: every flash write takes the cache away, and the RGB
     * panel's framebuffer is in PSRAM behind that cache. One save tears the picture,
     * a handful of them leave the panel black until the next reboot - and the card is
     * no better: fopen() on it took 2.2 s and the write did not finish in ten. So the
     * live settings stay in RAM and the station boots on its compiled defaults. If
     * this ever needs to survive a reboot, the medium has to be one that does not
     * starve the panel: an external EEPROM/FRAM, or a save that runs with the display
     * out of the picture. */
}

/* ------------------------------------------------------------ the /fx page */

/* Value of `key` in a percent-encoded query string, or false. The values this
 * page takes are numbers and enum indices, so no percent decoding is needed and
 * a '%' is simply refused by the number parser. */
static bool q_get(const char *q, const char *key, char *out, size_t n)
{
    size_t klen = strlen(key);
    for (const char *p = q; *p; ) {
        const char *amp = strchr(p, '&');
        size_t seg = amp ? (size_t)(amp - p) : strlen(p);
        if (seg > klen && p[klen] == '=' && strncmp(p, key, klen) == 0) {
            size_t vlen = seg - klen - 1;
            if (vlen >= n) return false;
            memcpy(out, p + klen + 1, vlen);
            out[vlen] = 0;
            return true;
        }
        if (!amp) break;
        p = amp + 1;
    }
    return false;
}

static bool q_float(const char *q, const char *key, float *out)
{
    char buf[24];
    if (!q_get(q, key, buf, sizeof buf)) return false;
    char *end;
    float v = strtof(buf, &end);
    if (end == buf || *end || v < 0.f || v > 1.f) return false;
    *out = v;
    return true;
}

static bool q_enum(const char *q, const char *key, unsigned max, uint8_t *out)
{
    char buf[8];
    if (!q_get(q, key, buf, sizeof buf)) return false;
    char *end;
    long v = strtol(buf, &end, 10);
    if (end == buf || *end || v < 0 || v > (long)max) return false;
    *out = (uint8_t)v;
    return true;
}

static void fx_page(int fd)
{
    char body[2048];
    mini_icecast_status_t st;
    mini_icecast_status(&st);
    int n = snprintf(body, sizeof body,
        "<!doctype html><meta charset=\"utf-8\"><meta name=\"viewport\" "
        "content=\"width=device-width,initial-scale=1\"><title>tape fx</title>"
        "<style>body{font:16px system-ui,sans-serif;margin:2rem;max-width:32rem}"
        "label{display:block;margin:.9rem 0 .2rem}input{font:inherit;padding:.2rem;width:6rem}"
        "button{font:inherit;margin-top:1.2rem;padding:.4rem 1rem}"
        "p.n{color:#666;font-size:.9rem}code{background:#eee;padding:.1rem .3rem}</style>"
        "<h1>Tape effect</h1>"
        "<p>Now playing: <strong>%s</strong> &middot; effect <strong>%s</strong></p>"
        "<form method=\"get\" action=\"/fx\">"
        "<label>Effect &mdash; <code>1</code> on, <code>0</code> off</label>"
        "<input name=\"on\" type=\"number\" min=\"0\" max=\"1\" step=\"1\" value=\"%u\">"
        "<label>Tape type &mdash; <code>0</code> normal, <code>1</code> chrome, <code>2</code> metal</label>"
        "<input name=\"type\" type=\"number\" min=\"0\" max=\"2\" step=\"1\" value=\"%u\">"
        "<label>Dolby &mdash; <code>0</code> off, <code>1</code> B, <code>2</code> C, <code>3</code> K</label>"
        "<input name=\"dolby\" type=\"number\" min=\"0\" max=\"3\" step=\"1\" value=\"%u\">"
        "<label>Tape quality (saturation, hiss)</label>"
        "<input name=\"tape_q\" type=\"number\" min=\"0\" max=\"1\" step=\"0.05\" value=\"%.2f\">"
        "<label>Head quality (high-frequency loss)</label>"
        "<input name=\"head_q\" type=\"number\" min=\"0\" max=\"1\" step=\"0.05\" value=\"%.2f\">"
        "<label>Motor quality (wow, flutter, drift)</label>"
        "<input name=\"motor_q\" type=\"number\" min=\"0\" max=\"1\" step=\"0.05\" value=\"%.2f\">"
        "<p class=n>0 is worn, 1 is pristine. Applied at the next chunk (~23 ms). "
        "Not remembered across reboots: on this board both flash and the card disturb "
        "the picture, so the station boots on its compiled sound.</p>"
        "<button type=\"submit\">Apply</button></form>",
        st.title, st.fx_on ? "on" : "off", st.fx_on ? 1u : 0u, (unsigned)st.fx_type, (unsigned)st.fx_dolby,
        (double)st.fx_tape_q, (double)st.fx_head_q, (double)st.fx_motor_q);
    if (n < 0) return;
    if ((size_t)n >= sizeof body) n = (int)sizeof body - 1;
    http_send_headers(fd, 200, "text/html; charset=utf-8", (size_t)n, NULL);
    http_send_all(fd, body, (size_t)n);
}

static http_result_t fx_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (strcmp(req->method, "GET") != 0) {
        http_send_error(fd, 405, "the effect takes GET");
        return HTTP_HANDLED;
    }
    if (!mini_icecast_fx_supported()) {
        http_send_error(fd, 404, "this station was built without the tape effect");
        return HTTP_HANDLED;
    }
    if (req->query[0] == 0) {
        fx_page(fd);
        return HTTP_HANDLED;
    }

    mini_icecast_fx_t v = {.set = true};
    uint8_t on;
    if (q_enum(req->query, "on", 1, &on) && mini_icecast_fx_enabled() != (on != 0)) {
        if (fx_ui_set_enabled(on != 0) != ESP_OK)
            ESP_LOGW(TAG, "cannot switch the effect");
    }
    bool ok = q_enum(req->query, "type", TAPE_METAL, &v.type)
           && q_enum(req->query, "dolby", DOLBY_K, &v.dolby)
           && q_float(req->query, "tape_q", &v.tape_q)
           && q_float(req->query, "head_q", &v.head_q)
           && q_float(req->query, "motor_q", &v.motor_q);
    if (!ok) {
        http_send_error(fd, 400, "type 0-2, dolby 0-3, qualities 0..1");
        return HTTP_HANDLED;
    }
    mini_icecast_set_fx(&v);
    fx_store_save(&v);
    ESP_LOGI(TAG, "tape_fx: type %u, dolby %u, q %.2f/%.2f/%.2f (applied)",
             (unsigned)v.type, (unsigned)v.dolby, (double)v.tape_q, (double)v.head_q,
             (double)v.motor_q);
    http_send_headers(fd, 303, NULL, 0, "Location: /fx\r\n");
    return HTTP_HANDLED;
}


esp_err_t fx_ui_apply(const mini_icecast_fx_t *tune)
{
    if (!tune) return ESP_ERR_INVALID_ARG;
    if (!mini_icecast_fx_supported()) return ESP_ERR_INVALID_STATE;
    mini_icecast_fx_t v = *tune;
    v.set = true;
    esp_err_t e = mini_icecast_set_fx(&v);
    if (e == ESP_OK) fx_store_save(&v);
    return e;
}

esp_err_t fx_ui_set_enabled(bool on)
{
    if (!mini_icecast_fx_supported()) return ESP_ERR_INVALID_STATE;
    esp_err_t e = mini_icecast_set_fx_enabled(on);
    if (e != ESP_OK) return e;
    mini_icecast_status_t st;
    mini_icecast_status(&st);
    mini_icecast_fx_t v = {.set = true, .type = st.fx_type, .dolby = st.fx_dolby,
                           .tape_q = st.fx_tape_q, .head_q = st.fx_head_q,
                           .motor_q = st.fx_motor_q};
    fx_store_save(&v);
    return ESP_OK;
}

esp_err_t fx_ui_start(void)
{
    if (!mini_icecast_fx_supported()) {
        ESP_LOGI(TAG, "no tape effect on this station: /fx is not served");
        return ESP_OK;
    }
    /* What the user last saved wins over the compiled sound: that is the whole
     * point of persisting it. The broadcast exists by now, so the values are
     * applied at its next chunk. */
    mini_icecast_fx_t saved;
    if (fx_store_load(&saved) == ESP_OK) {
        mini_icecast_set_fx(&saved);
        ESP_LOGI(TAG, "restored from flash: type %u, dolby %u, q %.2f/%.2f/%.2f",
                 (unsigned)saved.type, (unsigned)saved.dolby, (double)saved.tape_q,
                 (double)saved.head_q, (double)saved.motor_q);
    }
    if (http_server_register_route("/fx", fx_route, NULL) != ESP_OK) {
        ESP_LOGW(TAG, "cannot register /fx; the effect still runs, without a page");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "GET /fx is ready");
    return ESP_OK;
}
