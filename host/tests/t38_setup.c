/*
 * T38: the setup machine's policy, host tier.
 *
 * The radio is the host adapter's fake, scripted by the environment (host/board_host.c),
 * and the clock is the test's: setup_init() and setup_tick() take milliseconds from the
 * caller for exactly this reason. What is exercised is wifi-provisioning.md's policy —
 * the two boot triggers and no third (P9, P10), the grace close (P11), a stored record
 * surviving a failed attempt (P24), and the socket the page reads (P21).
 *
 * What is NOT exercised here: the 25 s join ceiling. The attempt's poll loop uses the real
 * clock, so a timeout test would take 25 seconds; the failed-attempt path it guards is
 * covered by the fake's disconnect count, and the ceiling is B-book material.
 */
#include "test_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app_board.h"
#include "setup.h"

static char s_record[256];
static char s_ap[256];

static void fresh_device(const char *join_result)
{
    /* The fake radio keeps its state between machines, as a radio would: put it back to
     * "no access point" so each test starts from the same air. */
    app_board_setup_ap(false);
    snprintf(s_record, sizeof s_record, "/tmp/k7setup-test-%d.bin", (int)getpid());
    snprintf(s_ap, sizeof s_ap, "/tmp/k7setup-test-%d.apstate", (int)getpid());
    remove(s_record);
    remove(s_ap);
    char pass_path[300];
    snprintf(pass_path, sizeof pass_path, "%s.ap", s_record);
    remove(pass_path);
    setenv("XTAPE_HOST_WIFI", s_record, 1);
    setenv("XTAPE_HOST_AP_STATE", s_ap, 1);
    if (join_result) setenv("XTAPE_HOST_WIFI_JOIN", join_result, 1);
    else unsetenv("XTAPE_HOST_WIFI_JOIN");
}

static void store_network(const char *ssid, const char *pass)
{
    wifi_record_t rec;
    wifi_record_clear(&rec);
    snprintf(rec.ssid, sizeof rec.ssid, "%s", ssid);
    snprintf(rec.pass, sizeof rec.pass, "%s", pass);
    CHECK(app_board_wifi_record_save(&rec) == ESP_OK, "the test can store a network");
}

static const char *ap_state(void)
{
    FILE *f = fopen(s_ap, "r");
    if (!f) return "?";
    static char buf[8];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

static uint32_t now_ms(void)
{
    /* The machine's own boot deadline is set from the real clock in setup_init(), so the
     * tests advance from a reading taken after it. */
    extern int64_t esp_timer_get_time(void);
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* P9, trigger one: no record at all. The access point is up before anything is attempted. */
static void test_unprovisioned(void)
{
    fresh_device("ok");
    setup_status_t st;
    setup_init();
    setup_status(&st);
    CHECK(st.state == SETUP_STATE_UNPROVISIONED, "no record reads as unprovisioned");
    CHECK(st.session, "the setup network is up at once");
    CHECK(!st.provisioned, "and the device names no network");
    CHECK(strcmp(ap_state(), "on") == 0, "the radio was asked for it");
    CHECK(strcmp(st.password, "12345678") == 0, "the default setup password is in force");
}

/* P9, trigger two: a record, nothing on the air, twenty seconds. */
static void test_boot_window(void)
{
    fresh_device("ok");
    store_network("xperiments-2.4G", "viernes13");
    setup_init();
    setup_tick(now_ms() + 1000);
    setup_status_t st;
    setup_status(&st);
    CHECK(!st.session, "inside the window the hotspot stays down");
    CHECK(st.state == SETUP_STATE_SETUP || st.state == SETUP_STATE_JOINED,
          "and the state is one of the two that means 'not in a session yet'");

    setup_tick(now_ms() + SETUP_BOOT_WINDOW_MS + 1000);
    setup_status(&st);
    CHECK(st.session, "after the window the setup network appears");
}

/* P9/P10: a device that is associated never raises it, however long it runs. */
static void test_associated_is_left_alone(void)
{
    fresh_device("ok");
    store_network("xperiments-2.4G", "viernes13");
    CHECK(app_board_wifi_join("xperiments-2.4G", "viernes13") == ESP_OK, "the fake associates");
    setup_init();
    setup_tick(now_ms() + SETUP_BOOT_WINDOW_MS + 1000);
    setup_tick(now_ms() + 10 * SETUP_BOOT_WINDOW_MS);
    setup_status_t st;
    setup_status(&st);
    CHECK(!st.session, "an associated device never shows a setup network");
    CHECK(st.state == SETUP_STATE_JOINED, "it reads as joined");
    CHECK(strcmp(ap_state(), "off") == 0 || strcmp(ap_state(), "?") == 0,
          "and the radio was never asked for one");
}

/* P11 and P32: a successful join starts the grace, and the session closes at its end. */
static void test_grace(void)
{
    fresh_device("ok");
    setup_init();
    setup_status_t st;
    setup_status(&st);
    CHECK(st.session, "the hotspot is up on an unprovisioned device");

    setup_join_t r = setup_join("xperiments-2.4G", "viernes13", now_ms());
    CHECK(r == SETUP_JOIN_OK, "the join succeeds");
    setup_status(&st);
    CHECK(st.session, "the hotspot is still up while the person reads the page");
    CHECK(st.grace_left_ms > 0, "and the grace is counting");
    CHECK(st.provisioned, "the record is stored");

    setup_tick(now_ms() + SETUP_GRACE_MS + 1000);
    setup_status(&st);
    CHECK(!st.session, "and the session is gone a minute later");
    CHECK(st.grace_left_ms == 0, "with no countdown left");
    CHECK(strcmp(ap_state(), "off") == 0, "the radio was told to drop it");
}

/* P24/P27: a refused network never reaches the flash, and the stored one is untouched. */
static void test_bad_choice_is_refused(void)
{
    fresh_device("ok");
    setup_init();
    setup_join_t r = setup_join("xperiments-2.4G", "1234567", now_ms());
    CHECK(r == SETUP_JOIN_INVALID, "a 7-character password is refused before the attempt");
    CHECK(access(s_record, F_OK) != 0, "and nothing was stored");
}

/* P24: the choice is stored before it is attempted, so a failure still says what the
 * device will try - and P30: the attempt is recorded for the page to report. */
static void test_failed_attempt_is_recorded(void)
{
    fresh_device("fail:15:5");
    setup_init();
    setup_join_t r = setup_join("xperiments-2.4G", "wrongpass", now_ms());
    CHECK(r == SETUP_JOIN_FAILED, "the attempt fails");

    setup_status_t st;
    setup_status(&st);
    CHECK(st.has_attempt, "the attempt is recorded");
    CHECK(!st.attempt_ok, "as a failure");
    CHECK(st.attempt_reason == 15, "with the radio's reason");
    CHECK(st.attempt_tries == 5, "and its count");
    CHECK(strcmp(setup_reason_text(st.attempt_reason), "wrong password") == 0,
          "in the panel's words");
    CHECK(strcmp(setup_reason_text(2), "the network refused the device") == 0,
          "an auth failure names the refusal, not a password");
    CHECK(strcmp(setup_reason_text(201), "no access point found") == 0,
          "201 is a missing network");
    CHECK(strcmp(setup_reason_text(999), "join failed") == 0, "anything else is the fallback");
    CHECK(st.provisioned, "and the device still names what it will try");
    CHECK(strcmp(st.network, "xperiments-2.4G") == 0, "which is the network that was chosen");
    CHECK(st.grace_left_ms == 0, "no grace runs after a failure");
    CHECK(st.session, "and the hotspot stays up for another choice");
}

/* The board found this: raising the hotspot on a device that is already online is a
 * request to reconfigure it, not a join, so it must not start the grace that closes it. */
static void test_on_demand_while_connected(void)
{
    fresh_device("ok");
    store_network("xperiments-2.4G", "viernes13");
    CHECK(app_board_wifi_join("xperiments-2.4G", "viernes13") == ESP_OK, "the fake associates");
    setup_init();
    setup_tick(now_ms() + 1000);

    CHECK(setup_session_start(now_ms()), "the panel can raise the hotspot on demand");
    setup_status_t st;
    setup_status(&st);
    CHECK(st.session, "it is up");
    CHECK(st.grace_left_ms == 0, "and no grace is running: nothing has joined");

    setup_tick(now_ms() + 10 * SETUP_GRACE_MS);
    setup_status(&st);
    CHECK(st.session, "so it is still up long after a grace would have closed it");
    setup_session_stop(now_ms());
}

/* P5: a new setup password is stored, validated, and applies at the next session. */
static void test_setup_password(void)
{
    fresh_device("ok");
    setup_init();
    CHECK(!setup_set_password("short", now_ms()), "a 5-character password is refused");
    CHECK(setup_set_password("newpass99", now_ms()), "a good one is accepted");

    setup_status_t st;
    setup_status(&st);
    CHECK(strcmp(st.password, "newpass99") == 0, "and is the one in force after a restart");

    /* The running session keeps the password it started with: nothing restarted it. */
    CHECK(strcmp(ap_state(), "on") == 0, "the running hotspot was not restarted");
}

int main(void)
{
    printf("t38: setup\n");
    test_unprovisioned();
    test_boot_window();
    test_associated_is_left_alone();
    test_grace();
    test_bad_choice_is_refused();
    test_failed_attempt_is_recorded();
    test_on_demand_while_connected();
    test_setup_password();

    remove(s_record);
    remove(s_ap);
    char pass_path[300];
    snprintf(pass_path, sizeof pass_path, "%s.ap", s_record);
    remove(pass_path);
    TEST_END();
}
