/*
 * setup - the policy of the setup network. See setup.h for what it owns and why.
 *
 * One lock guards the machine because three callers reach it: the tick task, an HTTP
 * worker running a join, and the panel. Nothing here touches flash on a worker's stack:
 * the record is cached in RAM, and the one write goes through flash_job (ADR 0054).
 */
#include "setup.h"

#include <stdio.h>
#include <string.h>

#include "app_board.h"
#include "flash_job.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "setup";

typedef struct {
    bool provisioned;
    bool session;  /* the access point is up */
    bool joining;  /* an attempt is in flight, owned by the worker that asked */
    bool ap_failed;
    /* Whether the station was already associated when the session started. Raising the
     * hotspot on a device that is happily online is a request to reconfigure it, not a
     * join: without this the grace below starts at once and the hotspot vanishes while the
     * person is still choosing. Found by doing exactly that on the board. */
    bool station_was_associated;
    uint32_t ap_retry_at_ms;
    uint32_t boot_deadline_ms;
    uint32_t grace_deadline_ms;
    /* The record, cached: an HTTP worker must not read NVS (ADR 0054). */
    wifi_record_t rec;
    bool has_attempt;
    bool attempt_ok;
    char attempt_ssid[WIFI_RECORD_SSID_MAX + 1];
    int attempt_reason;
    int attempt_tries;
} machine_t;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static machine_t M;
static SemaphoreHandle_t s_lock;
static bool s_task_started;

static void setup_tick_task(void *arg)
{
    (void)arg;
    for (;;) {
        setup_tick(now_ms());
        vTaskDelay(pdMS_TO_TICKS(SETUP_TICK_MS));
    }
}

static bool lock(void)
{
    if (!s_lock) return true; /* before init: the boot is single-threaded */
    return xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE;
}

static void unlock(void)
{
    if (s_lock) xSemaphoreGive(s_lock);
}

static bool reached(uint32_t now, uint32_t deadline)
{
    return deadline != 0 && (int32_t)(now - deadline) >= 0;
}

/* --------------------------------------------------------------- the attempt */

/* Both run on the flash writer's internal stack, never on the caller's (ADR 0054). */
static bool save_record_job(void *ctx)
{
    return app_board_wifi_record_save((const wifi_record_t *)ctx) == ESP_OK;
}

static bool save_password_job(void *ctx)
{
    return app_board_setup_password_save((const char *)ctx) == ESP_OK;
}

/* --------------------------------------------------------------- the access point */

/* Raises the access point. **Called with the lock released**: esp_wifi_set_mode() restarts
 * the radio, which takes a few hundred milliseconds, and setup_status() runs on the panel's
 * task twenty-five times a second - a lock held across the restart freezes the glass at the
 * moment someone presses Setup. The state is read and written under the lock on either side
 * of the call; two callers racing is harmless because starting an access point that is
 * already up is not an error. */
static void start_session_now(uint32_t now)
{
    lock();
    if (M.session) {
        unlock();
        return;
    }
    unlock();

    esp_err_t e = app_board_setup_ap(true);

    lock();
    if (e == ESP_OK) {
        app_board_wifi_t radio;
        app_board_wifi_state(&radio);
        M.session = true;
        M.ap_failed = false;
        M.grace_deadline_ms = 0;
        M.station_was_associated = radio.associated;
    } else {
        /* P35: keep asking, and let the panel say the row is unavailable rather than
         * pretending the setup network was never wanted. */
        M.ap_failed = true;
        M.ap_retry_at_ms = now + SETUP_AP_RETRY_MS;
        ESP_LOGE(TAG, "the setup network did not start; asking again in %d ms",
                 (int)SETUP_AP_RETRY_MS);
    }
    unlock();
}

static void stop_session(void)
{
    if (!M.session) return;
    if (app_board_setup_ap(false) != ESP_OK)
        ESP_LOGW(TAG, "the setup network did not stop cleanly");
    M.session = false;
    M.grace_deadline_ms = 0;
}

/* -------------------------------------------------------------------- public */

void setup_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    memset(&M, 0, sizeof M);
    lock();
    /* Boot runs on an internal stack, so the record is read here and cached. */
    M.provisioned = app_board_wifi_record_load(&M.rec) && wifi_record_provisioned(&M.rec);
    if (!app_board_setup_password_load(M.rec.setup_pass, sizeof M.rec.setup_pass))
        M.rec.setup_pass[0] = '\0';
    M.boot_deadline_ms = now_ms() + SETUP_BOOT_WINDOW_MS;
    bool wants = !M.provisioned;
    unlock();
    if (wants) start_session_now(now_ms());
}

void setup_tick(uint32_t now)
{
    app_board_wifi_t radio;
    app_board_wifi_state(&radio);

    lock();
    bool wants = false;
    if (!M.session && !M.joining && !M.ap_failed) {
        /* P9: the two boot triggers. Neither is "the network went away later" (P10). */
        if (!M.provisioned) wants = true;
        else if (!radio.associated && reached(now, M.boot_deadline_ms)) wants = true;
    }

    /* A station that associates on its own, while a session is up, counts as joined
     * (P32): a device whose network came back does not sit in setup mode. A station that was
     * already associated when the session opened is not that - see station_was_associated. */
    if (M.session && !M.joining && radio.associated && !M.station_was_associated &&
        M.grace_deadline_ms == 0)
        M.grace_deadline_ms = now + SETUP_GRACE_MS;

    /* P11: the session closes a minute after a successful join, or never while failing. */
    if (M.session && reached(now, M.grace_deadline_ms)) {
        stop_session();
        ESP_LOGI(TAG, "setup network closed: the device is on its network");
    }

    /* The retry leaves ap_failed standing until a start succeeds: clearing it first made the
     * panel's row flicker back to START for the few hundred milliseconds of every attempt. */
    if (M.ap_failed && reached(now, M.ap_retry_at_ms)) {
        M.ap_retry_at_ms = now + SETUP_AP_RETRY_MS;
        wants = true;
    }
    unlock();
    if (wants) start_session_now(now);
}

void setup_start_task(void)
{
    if (s_task_started) return;
    s_task_started = true;
    if (xTaskCreatePinnedToCore(setup_tick_task, "setup", 4096, NULL, 3, NULL, 1) != pdPASS) {
        s_task_started = false;
        ESP_LOGE(TAG, "no task for the setup network; it will not appear by itself");
    }
}

bool setup_session_start(uint32_t now)
{
    start_session_now(now);
    lock();
    bool ok = M.session;
    unlock();
    return ok;
}

void setup_session_stop(uint32_t now)
{
    (void)now;
    lock();
    stop_session();
    unlock();
}

setup_join_t setup_join(const char *ssid, const char *pass, uint32_t now)
{
    if (!ssid || !pass) return SETUP_JOIN_INVALID;

    lock();
    if (!M.session) {
        unlock();
        return SETUP_JOIN_NO_SESSION;
    }
    if (M.joining) {
        unlock();
        return SETUP_JOIN_BUSY;
    }

    wifi_record_t rec = M.rec; /* keeps the setup password as it is */
    snprintf(rec.ssid, sizeof rec.ssid, "%s", ssid);
    snprintf(rec.pass, sizeof rec.pass, "%s", pass);
    if (!wifi_record_valid(&rec)) {
        unlock();
        return SETUP_JOIN_INVALID;
    }
    M.joining = true;
    unlock();

    /* P24/P33: the choice is stored before it is attempted, so it survives the attempt
     * however it goes - and the write is on the flash writer, not this worker. */
    if (!flash_job_run(save_record_job, &rec)) {
        lock();
        M.joining = false;
        M.has_attempt = true;
        M.attempt_ok = false;
        snprintf(M.attempt_ssid, sizeof M.attempt_ssid, "%s", rec.ssid);
        M.attempt_reason = 0;
        M.attempt_tries = 0;
        unlock();
        return SETUP_JOIN_FAILED;
    }

    esp_err_t e = app_board_wifi_join(rec.ssid, rec.pass);
    bool ok = false;
    int reason = 0;
    int tries = 0;
    if (e == ESP_OK) {
        uint32_t start = now_ms();
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(SETUP_TICK_MS));
            app_board_wifi_t radio;
            app_board_wifi_state(&radio);
            tries = radio.disconnects;
            if (radio.associated) {
                ok = true;
                break;
            }
            /* P29: five failures and the attempt is over. The radio keeps retrying in the
             * background; what ends is this attempt's answer. */
            if (tries >= SETUP_JOIN_TRIES) {
                reason = radio.last_reason;
                break;
            }
            if (reached(now_ms(), start + SETUP_JOIN_TIMEOUT_MS)) {
                reason = radio.last_reason;
                break;
            }
        }
    }

    lock();
    M.joining = false;
    M.rec = rec;
    /* The record was stored before the attempt (P24), so the device *is* provisioned now,
     * joined or not: the panel and the page must say what it will try, not what it wishes. */
    M.provisioned = wifi_record_provisioned(&rec);
    M.has_attempt = true;
    M.attempt_ok = ok;
    snprintf(M.attempt_ssid, sizeof M.attempt_ssid, "%s", rec.ssid);
    M.attempt_reason = ok ? 0 : reason;
    M.attempt_tries = tries;
    if (ok && M.session) M.grace_deadline_ms = now_ms() + SETUP_GRACE_MS;
    (void)now;
    unlock();
    return ok ? SETUP_JOIN_OK : SETUP_JOIN_FAILED;
}

static bool forget_job(void *ctx)
{
    wifi_record_t empty;
    wifi_record_clear(&empty); /* a valid record with no SSID: the adapter erases the key */
    (void)ctx;
    return app_board_wifi_record_save(&empty) == ESP_OK;
}

bool setup_forget_network(uint32_t now)
{
    lock();
    wifi_record_t rec = M.rec;
    unlock();

    if (!flash_job_run(forget_job, &rec)) return false;

    app_board_wifi_forget();              /* the radio stops chasing what was erased */
    lock();
    wifi_record_clear(&M.rec);            /* ssid gone; the setup password is not */
    snprintf(M.rec.setup_pass, sizeof M.rec.setup_pass, "%s", rec.setup_pass);
    M.provisioned = false;
    M.has_attempt = false;
    bool wants = !M.session && !M.joining;
    unlock();
    if (wants) start_session_now(now);
    return true;
}

bool setup_set_password(const char *password, uint32_t now)
{
    (void)now;
    if (!password || !wifi_record_valid_password(password)) return false;

    wifi_record_t rec;
    lock();
    rec = M.rec;
    snprintf(rec.setup_pass, sizeof rec.setup_pass, "%s", password);
    unlock();

    /* P5: stored now, applied at the next start of the setup network - restarting the
     * access point here would drop the client that just asked for the change. */
    if (!flash_job_run(save_password_job, (void *)password)) return false;

    lock();
    M.rec = rec;
    unlock();
    return true;
}

void setup_record_changed(uint32_t now)
{
    wifi_record_t rec;
    bool provisioned = app_board_wifi_record_load(&rec) && wifi_record_provisioned(&rec);
    if (!app_board_setup_password_load(rec.setup_pass, sizeof rec.setup_pass))
        rec.setup_pass[0] = '\0';
    lock();
    M.rec = rec;
    M.provisioned = provisioned;
    bool wants = !provisioned && !M.session && !M.joining;
    if (wants) M.ap_failed = false;
    unlock();
    if (wants) start_session_now(now);
}

void setup_status(setup_status_t *out)
{
    if (!out) return;
    lock();
    memset(out, 0, sizeof *out);
    app_board_wifi_t radio;
    app_board_wifi_state(&radio); /* flags only, no flash: safe on an HTTP worker */
    out->associated = radio.associated;
    out->state = M.joining                             ? SETUP_STATE_JOINING
                 : !M.provisioned                      ? SETUP_STATE_UNPROVISIONED
                 : (M.session || !radio.associated)    ? SETUP_STATE_SETUP
                                                       : SETUP_STATE_JOINED;
    out->provisioned = M.provisioned;
    out->session = M.session;
    out->ap_failed = M.ap_failed;
    snprintf(out->network, sizeof out->network, "%s", M.rec.ssid);
    snprintf(out->password, sizeof out->password, "%s", wifi_record_setup_password(&M.rec));
    uint32_t n = now_ms();
    if (M.session && M.grace_deadline_ms != 0 && (int32_t)(M.grace_deadline_ms - n) > 0)
        out->grace_left_ms = M.grace_deadline_ms - n;
    out->has_attempt = M.has_attempt;
    out->attempt_ok = M.attempt_ok;
    snprintf(out->attempt_ssid, sizeof out->attempt_ssid, "%s", M.attempt_ssid);
    out->attempt_reason = M.attempt_reason;
    out->attempt_tries = M.attempt_tries;
    unlock();
}

const char *setup_state_name(setup_state_t state)
{
    switch (state) {
    case SETUP_STATE_UNPROVISIONED: return "unprovisioned";
    case SETUP_STATE_SETUP: return "setup";
    case SETUP_STATE_JOINING: return "joining";
    case SETUP_STATE_JOINED: return "joined";
    }
    return "setup";
}

const char *setup_reason_text(int reason)
{
    switch (reason) {
    case 2:
    case 4:
        /* Measured: a WPA2 wrong password can end as 2 (AUTH_FAIL) rather than 15, and the
         * pair is not distinguishable from a MAC filter refusing the device. */
        return "the network refused the device";
    case 15: return "wrong password";
    case 201: return "no access point found";
    case 205: return "the hotspot's own access point was dropped";
    case 0: return "join failed";
    }
    return "join failed";
}
