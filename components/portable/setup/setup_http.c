/*
 * setup_http - see include/setup_http.h.
 *
 * The page's markup is a single constant with no double quotes and no backslashes in it:
 * HTML attributes and the script use single quotes, so the C literal needs no escaping
 * and nothing here can rot into a broken string. The script builds every list row with
 * createElement and textContent rather than innerHTML, because a network name is a string
 * a stranger on the air chose.
 */
#include "setup_http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_board.h"
#include "http_json.h"
#include "http_server.h"
#include "json.h"
#include "setup.h"

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "setup_http";

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* The whole setup surface — the page and all seven routes — exists only while the setup
 * network does (ADR 0077). Outside that window these paths are 404, exactly like any other
 * path this device does not serve, so a device sitting happily on its LAN has no page that
 * can rewrite its network. */
static bool surface_open(void)
{
    setup_status_t st;
    setup_status(&st);
    return st.session;
}

/* -------------------------------------------------------------------- the page */

static const char PAGE[] =
    "<!doctype html>\n"
    "<html lang='en'><head><meta charset='utf-8'>\n"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>\n"
    "<title>K7 - Wi-Fi setup</title>\n"
    "<style>\n"
    "body{font:16px/1.45 -apple-system,system-ui,'Segoe UI',sans-serif;margin:0;"
    "background:#17181a;color:#a6a8a8}\n"
    "main{max-width:34rem;margin:0 auto;padding:1.25rem}\n"
    "h1{font-size:1.3rem;font-weight:300;margin:.2rem 0 .8rem}\n"
    "h2{font-size:.8rem;margin:1.5rem 0 .5rem;letter-spacing:.06em;"
    "text-transform:uppercase;color:#858787}\n"
    ".state{background:#121314;border:1px solid #2e3030;border-radius:7px;padding:.7rem}\n"
    ".state.bad{border-color:#F15A22;color:#F15A22}\n"
    "ul{list-style:none;margin:0;padding:0}\n"
    "li{display:flex;gap:.6rem;align-items:center;padding:.55rem .7rem;background:#121314;"
    "border:1px solid #2e3030;border-radius:7px;margin-bottom:.4rem;cursor:pointer}\n"
    "li[aria-selected=true]{border-color:#F15A22;background:#F15A22;color:#111111}\n"
    "li[aria-selected=true] .sig{color:rgba(17,17,17,.72)}\n"
    ".name{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}\n"
    ".sig{font-size:.78rem;color:#858787}\n"
    "label{display:block;margin:.7rem 0 .25rem;font-size:.82rem;color:#858787}\n"
    "input{width:100%;box-sizing:border-box;padding:.55rem;font-size:1rem;"
    "border:1px solid #2e3030;border-radius:7px;background:#121314;color:#a6a8a8}\n"
    "input::placeholder{color:#858787}\n"
    "button{margin-top:.6rem;padding:.6rem 1rem;font-size:1rem;border-radius:7px;"
    "border:1px solid #F15A22;background:#F15A22;color:#111111;width:100%;font-weight:300}\n"
    "button.alt{background:transparent;color:#a6a8a8;border-color:#5a5c5c}\n"
    "button:disabled{opacity:.45}\n"
    "small{display:block;margin-top:.5rem;color:#858787;font-size:.78rem}\n"
    "</style></head><body><main>\n"
    "<h1>K7 - Wi-Fi setup</h1>\n"
    "<p class='state' id='state'>Reading the device...</p>\n"
    "<h2>Networks</h2>\n"
    "<p id='scan' class='sig'>Scanning...</p>\n"
    "<ul id='list'></ul>\n"
    "<button class='alt' id='refresh'>Scan again</button>\n"
    "<h2>The network to join</h2>\n"
    "<label for='pass'>Password</label>\n"
    "<input id='pass' type='password' autocomplete='off' spellcheck='false'>\n"
    "<button id='join'>Join</button>\n"
    "<h2>Another network</h2>\n"
    "<label for='other'>Name</label>\n"
    "<input id='other' autocomplete='off' spellcheck='false'>\n"
    "<label for='otherpass'>Password</label>\n"
    "<input id='otherpass' type='password' autocomplete='off'>\n"
    "<button class='alt' id='joinother'>Join this one</button>\n"
    "<h2>The hotspot</h2>\n"
    "<label for='apass'>New password (at least 8 characters)</label>\n"
    "<input id='apass' type='password' autocomplete='off'>\n"
    "<button class='alt' id='setap'>Set the hotspot password</button>\n"
    "<small>Applied the next time the hotspot starts.</small>\n"
    "<button class='alt' id='stop'>Stop the setup hotspot</button>\n"
    "<button class='alt' id='forget'>Forget this network</button>\n"
    "<small>The radio only sees 2.4 GHz networks. A 5 GHz-only network will not appear here "
    "and cannot be joined.</small>\n"
    "</main><script>\n"
    "var $ = function (id) { return document.getElementById(id); };\n"
    "var chosen = null;\n"
    "var ticker = null;\n"
    "function say(text, bad) { var s = $('state'); s.textContent = text;"
    " s.className = 'state' + (bad ? ' bad' : ''); }\n"
    "function busy(on) { ['join','joinother','refresh','setap','stop'].forEach(function (id)"
    " { $(id).disabled = on; }); }\n"
    "function post(path, body) { return fetch(path, {method: 'POST',"
    " headers: {'Content-Type': 'application/json'}, body: JSON.stringify(body)}); }\n"
    "function seconds(ms) { return Math.max(0, Math.round(ms / 1000)); }\n"
    "function describe(st) {\n"
    "  if (st.state === 'joining') return ['Joining ' + (st.last_attempt ?"
    " st.last_attempt.ssid : 'the network') + '...', false];\n"
    "  if (st.state === 'unprovisioned') return ['Choose the Wi-Fi network this K7 should"
    " join.', false];\n"
    "  if (st.state === 'joined' && st.address) return ['Connected to ' + st.network +"
    " ' at ' + st.address + '.', false];\n"
    "  if (st.last_attempt && !st.last_attempt.ok) return ['Could not join ' +"
    " st.last_attempt.ssid + ': ' + st.last_attempt.why + '.', true];\n"
    "  return ['The hotspot is open. Choose a network, or change the hotspot password.',"
    " false];\n"
    "}\n"
    "async function load() {\n"
    "  try {\n"
    "    var r = await fetch('/api/setup'); var st = await r.json();\n"
    "    var d = describe(st); say(d[0], d[1]);\n"
    /* One countdown at a time: load() runs again after every join and every visit, and a
     * left-over interval would fight the new one over the status line and never stop. */
    "    if (ticker) { clearInterval(ticker); ticker = null; }\n"
    "    if (st.closes_in_ms > 0) {\n"
    "      var left = st.closes_in_ms;\n"
    "      say('Connected to ' + st.network + '. This hotspot closes in ' +"
    " seconds(left) + ' seconds.', false);\n"
    "      ticker = setInterval(function () { left -= 1000; if (left <= 0) {"
    " clearInterval(ticker); ticker = null; say('The hotspot is closed. The device is on ' +"
    " st.network + '.', false); return; } say('Connected to ' + st.network + '. This hotspot"
    " closes in ' + seconds(left) + ' seconds.', false); }, 1000);\n"
    "    }\n"
    "  } catch (e) { say('The device did not answer: ' + e, true); }\n"
    "}\n"
    "async function scan() {\n"
    "  $('scan').textContent = 'Scanning...'; $('list').textContent = '';\n"
    "  try {\n"
    "    var r = await fetch('/api/setup/scan'); var j = await r.json();\n"
    "    var ul = $('list');\n"
    "    (j.networks || []).forEach(function (n) {\n"
    "      var li = document.createElement('li');\n"
    "      var name = document.createElement('span'); name.className = 'name';"
    " name.textContent = n.ssid;\n"
    "      var sig = document.createElement('span'); sig.className = 'sig';"
    " sig.textContent = n.rssi + ' dBm';\n"
    "      li.appendChild(name);\n"
    "      if (n.secure) { var l = document.createElement('span'); l.className = 'sig';"
    " l.textContent = 'locked'; li.appendChild(l); }\n"
    "      else { var o = document.createElement('span'); o.className = 'sig';"
    " o.textContent = 'open'; li.appendChild(o); }\n"
    "      li.appendChild(sig);\n"
    "      li.onclick = function () { chosen = n;"
    " Array.prototype.forEach.call(ul.children, function (c) {"
    " c.setAttribute('aria-selected', c === li ? 'true' : 'false'); }); };\n"
    "      ul.appendChild(li);\n"
    "    });\n"
    "    $('scan').textContent = (j.networks || []).length ? 'Tap the one to join.' :"
    " 'No networks found. Type one under Another network.';\n"
    "  } catch (e) { $('scan').textContent = 'The scan failed: ' + e; }\n"
    "}\n"
    "async function join(ssid, pass) {\n"
    "  if (!ssid) { say('Choose a network first.', true); return; }\n"
    "  busy(true); say('Joining ' + ssid + '...', false);\n"
    "  try {\n"
    "    var r = await post('/api/setup/join', {ssid: ssid, password: pass});\n"
    "    var j = await r.json();\n"
    "    if (r.ok && j.ok) say('Joined ' + ssid + '.', false);\n"
    "    else if (j.why) say('Could not join ' + ssid + ': ' + j.why + '.', true);\n"
    "    else say('Could not join ' + ssid + '.', true);\n"
    "  } catch (e) { say('The device did not answer: ' + e, true); }\n"
    "  busy(false); load();\n"
    "}\n"
    "$('refresh').onclick = scan;\n"
    "$('join').onclick = function () { join(chosen ? chosen.ssid : null, $('pass').value); };\n"
    "$('joinother').onclick = function () { join($('other').value, $('otherpass').value); };\n"
    "$('setap').onclick = async function () {\n"
    "  var p = $('apass').value; busy(true);\n"
    "  var r = await post('/api/setup/ap-password', {password: p});\n"
    "  if (r.status === 204) { $('apass').value = ''; say('The hotspot password is set. It"
    " applies the next time the hotspot starts.', false); }\n"
    "  else { var j = await r.json().catch(function () { return {}; });"
    " say(j.why || 'That password was not accepted.', true); }\n"
    "  busy(false);\n"
    "};\n"
    "$('forget').onclick = async function () {\n"
    "  busy(true); say('Forgetting the stored network...', false);\n"
    "  var r = await post('/api/setup/forget', {});\n"
    "  if (r.status === 204) say('The device is unprovisioned. Choose a network, or type"
    " one under Another network.', false);\n"
    "  else say('The stored network was not forgotten.', true);\n"
    "  busy(false); load();\n"
    "};\n"
    "$('stop').onclick = async function () {\n"
    "  busy(true); say('Closing the hotspot...', false);\n"
    "  await post('/api/setup/ap', {on: false});\n"
    "  say('The hotspot is closed.', false);\n"
    "};\n"
    "load(); scan();\n"
    "</script></body></html>\n";

static http_result_t page_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (!surface_open()) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    if (strcmp(req->path, "/setup") != 0) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    http_send_headers(fd, 200, "text/html; charset=utf-8", sizeof PAGE - 1, NULL);
    http_send_all(fd, PAGE, sizeof PAGE - 1);
    return HTTP_HANDLED;
}

/* ---------------------------------------------------------------- the API */

/* One body-shaped request field, unescaped, or false when it is missing or not a string.
 * A body that is not a JSON object is refused here rather than by a crash later. */
static bool field(const http_request_t *req, const char *key, char *out, size_t cap)
{
    json_tok_t tok[16];
    int nt = 0;
    const char *body = (const char *)req->body;
    if (!body || !json_parse(body, tok, 16, &nt)) return false;
    if (nt == 0 || tok[0].type != JSON_TOK_OBJECT) return false;
    int m = json_tok_member(body, tok, nt, 0, key);
    if (m < 0) return false;
    return json_tok_unescape(body, &tok[m], out, cap);
}

/* The bytes of a token as they stand. json_tok_eq() only compares strings (it strips no
 * quotes), so a boolean member has to be read as the primitive it is. */
static bool tok_is(const char *body, const json_tok_t *t, const char *want)
{
    size_t n = (size_t)(t->end - t->start);
    return n == strlen(want) && memcmp(body + t->start, want, n) == 0;
}

static http_result_t status_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (!surface_open()) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    if (strcmp(req->path, "/api/setup") != 0) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }

    setup_status_t st;
    setup_status(&st);

    char body[640];
    json_writer_t w = json_writer_make(body, sizeof body);
    json_puts(&w, "{\"state\":\"");
    json_puts(&w, setup_state_name(st.state));
    json_puts(&w, "\",\"network\":");
    if (st.provisioned) {
        json_puts(&w, "\"");
        json_escaped(&w, st.network);
        json_puts(&w, "\"");
    } else {
        json_puts(&w, "null");
    }
    json_puts(&w, ",\"address\":");
    char ip[16];
    if (app_board_local_ip(ip, sizeof ip)) {
        json_puts(&w, "\"");
        json_puts(&w, ip);
        json_puts(&w, "\"");
    } else {
        json_puts(&w, "null");
    }
    json_puts(&w, ",\"hotspot\":{\"ssid\":\"" SETUP_NETWORK_SSID "\",\"address\":\""
                  SETUP_NETWORK_IP "\",\"up\":");
    json_puts(&w, st.session ? "true" : "false");
    json_puts(&w, ",\"failed\":");
    json_puts(&w, st.ap_failed ? "true" : "false");
    json_puts(&w, "},\"closes_in_ms\":");
    json_printf(&w, "%u", (unsigned)st.grace_left_ms);
    json_puts(&w, ",\"last_attempt\":");
    if (!st.has_attempt) {
        json_puts(&w, "null");
    } else {
        json_puts(&w, "{\"ssid\":\"");
        json_escaped(&w, st.attempt_ssid);
        json_puts(&w, "\",\"ok\":");
        json_puts(&w, st.attempt_ok ? "true" : "false");
        json_puts(&w, ",\"reason\":");
        json_printf(&w, "%d", st.attempt_reason);
        json_puts(&w, ",\"why\":\"");
        json_escaped(&w, st.attempt_ok ? "" : setup_reason_text(st.attempt_reason));
        json_puts(&w, "\",\"tries\":");
        json_printf(&w, "%d", st.attempt_tries);
        json_puts(&w, "}");
    }
    json_puts(&w, "}");

    http_json_send_writer(fd, 200, &w, NULL);
    return HTTP_HANDLED;
}

/* The scan's list, in one request with a 5 s ceiling (P18). The result is not cached here:
 * the radio scans on demand, and a page reload is a second scan a person asked for. */
static http_result_t scan_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (!surface_open()) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    if (strcmp(req->path, "/api/setup/scan") != 0) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }

    /* One scan at a time (P22): a second request while one is running is a 429, not a
     * second radio trip stacking up behind the first. The flag is the only shared state on
     * this route, and a plain bool is enough because the worst case is one extra refusal. */
    static volatile bool scanning;
    if (scanning) {
        http_json_send(fd, 429, "{\"networks\":[],\"why\":\"a scan is already running\"}", NULL);
        return HTTP_HANDLED;
    }
    scanning = true;
    static app_board_scan_t found[APP_BOARD_SCAN_MAX];
    int n = app_board_wifi_scan(found, APP_BOARD_SCAN_MAX);
    scanning = false;
    if (n < 0) {
        http_json_send(fd, 503, "{\"networks\":[],\"why\":\"the scan could not run\"}", NULL);
        return HTTP_HANDLED;
    }

    char body[1600];
    json_writer_t w = json_writer_make(body, sizeof body);
    json_puts(&w, "{\"networks\":[");
    for (int i = 0; i < n; i++) {
        if (i) json_puts(&w, ",");
        json_puts(&w, "{\"ssid\":\"");
        json_escaped(&w, found[i].ssid);
        json_puts(&w, "\",\"rssi\":");
        json_printf(&w, "%d", found[i].rssi);
        json_puts(&w, ",\"secure\":");
        json_puts(&w, found[i].secure ? "true" : "false");
        json_puts(&w, "}");
    }
    json_puts(&w, "]}");
    http_json_send_writer(fd, 200, &w, NULL);
    return HTTP_HANDLED;
}

static http_result_t join_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (!surface_open()) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    if (strcmp(req->path, "/api/setup/join") != 0) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }

    char ssid[WIFI_RECORD_SSID_MAX + 1];
    char pass[WIFI_RECORD_PASS_MAX + 1];
    memset(pass, 0, sizeof pass);
    if (!field(req, "ssid", ssid, sizeof ssid)) {
        http_json_send(fd, 400,
                       "{\"ok\":false,\"why\":\"the body needs an ssid and a password\"}", NULL);
        return HTTP_HANDLED;
    }
    (void)field(req, "password", pass, sizeof pass); /* absent is an open network */

    switch (setup_join(ssid, pass, now_ms())) {
    case SETUP_JOIN_OK:
        http_json_send(fd, 200, "{\"ok\":true}", NULL);
        break;
    case SETUP_JOIN_FAILED: {
        setup_status_t st;
        setup_status(&st);
        char body[256];
        json_writer_t w = json_writer_make(body, sizeof body);
        json_puts(&w, "{\"ok\":false,\"reason\":");
        json_printf(&w, "%d", st.attempt_reason);
        json_puts(&w, ",\"tries\":");
        json_printf(&w, "%d", st.attempt_tries);
        json_puts(&w, ",\"why\":\"");
        json_escaped(&w, setup_reason_text(st.attempt_reason));
        json_puts(&w, "\"}");
        http_json_send_writer(fd, 200, &w, NULL);
        break;
    }
    case SETUP_JOIN_NO_SESSION:
        http_json_send(fd, 409,
                       "{\"ok\":false,\"why\":\"no hotspot: start it from Menu - Settings - "
                       "Wi-Fi - Setup\"}",
                       NULL);
        break;
    case SETUP_JOIN_BUSY:
        http_json_send(fd, 409, "{\"ok\":false,\"why\":\"a join is already running\"}", NULL);
        break;
    case SETUP_JOIN_INVALID:
        http_json_send(fd, 400,
                       "{\"ok\":false,\"why\":\"a password is 8 to 63 printable characters, and "
                       "a name is 1 to 32\"}",
                       NULL);
        break;
    }
    return HTTP_HANDLED;
}

static http_result_t ap_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (!surface_open()) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    if (strcmp(req->path, "/api/setup/ap") != 0) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }

    /* `on` is a JSON boolean, which is neither a string nor an int: it is compared as the
     * primitive it is, so {"on":false} and {"on":true} both work and anything else is a 400. */
    json_tok_t tok[16];
    int nt = 0;
    const char *body = (const char *)req->body;
    int m = (body && json_parse(body, tok, 16, &nt) && nt > 0 && tok[0].type == JSON_TOK_OBJECT)
                ? json_tok_member(body, tok, nt, 0, "on")
                : -1;
    if (m < 0 || (!tok_is(body, &tok[m], "false") && !tok_is(body, &tok[m], "true"))) {
        http_json_send(fd, 400, "{\"why\":\"the body needs on: true or false\"}", NULL);
        return HTTP_HANDLED;
    }
    bool on = tok_is(body, &tok[m], "true");

    if (on) {
        if (!setup_session_start(now_ms())) {
            http_json_send(fd, 503, "{\"why\":\"the hotspot did not start\"}", NULL);
            return HTTP_HANDLED;
        }
    } else {
        setup_session_stop(now_ms());
    }
    http_send_headers(fd, 204, NULL, 0, NULL);
    return HTTP_HANDLED;
}

static http_result_t ap_password_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (!surface_open()) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    if (strcmp(req->path, "/api/setup/ap-password") != 0) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }

    char pass[WIFI_RECORD_PASS_MAX + 1];
    if (!field(req, "password", pass, sizeof pass)) {
        http_json_send(fd, 400, "{\"why\":\"the body needs a password\"}", NULL);
        return HTTP_HANDLED;
    }
    if (!setup_set_password(pass, now_ms())) {
        http_json_send(fd, 400,
                       "{\"why\":\"a password is 8 to 63 printable characters\"}", NULL);
        return HTTP_HANDLED;
    }
    http_send_headers(fd, 204, NULL, 0, NULL);
    return HTTP_HANDLED;
}

static http_result_t forget_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (!surface_open()) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    if (strcmp(req->path, "/api/setup/forget") != 0) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    if (!setup_forget_network(now_ms())) {
        http_json_send(fd, 503, "{\"why\":\"the stored network could not be erased\"}", NULL);
        return HTTP_HANDLED;
    }
    http_send_headers(fd, 204, NULL, 0, NULL);
    return HTTP_HANDLED;
}

esp_err_t setup_http_start(void)
{
    esp_err_t e = http_server_register_route_method("/setup", "GET", page_route, NULL);
    if (e != ESP_OK) return e;
    e = http_server_register_route_method("/api/setup", "GET", status_route, NULL);
    if (e != ESP_OK) return e;
    e = http_server_register_route_method("/api/setup/scan", "GET", scan_route, NULL);
    if (e != ESP_OK) return e;
    e = http_server_register_route_method("/api/setup/join", "POST", join_route, NULL);
    if (e != ESP_OK) return e;
    e = http_server_register_route_method("/api/setup/ap", "POST", ap_route, NULL);
    if (e != ESP_OK) return e;
    e = http_server_register_route_method("/api/setup/forget", "POST", forget_route, NULL);
    if (e != ESP_OK) return e;
    e = http_server_register_route_method("/api/setup/ap-password", "POST", ap_password_route, NULL);
    if (e == ESP_OK) ESP_LOGI(TAG, "the setup page is at /setup");
    return e;
}
