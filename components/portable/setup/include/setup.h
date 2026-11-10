#pragma once
/*
 * setup - the device's setup network, and the policy of when it is up.
 *
 * The device has no compiled-in network (ADR 0076). When it has none, or cannot reach the
 * one it has, it raises its own access point so a person standing in front of it can say
 * which network it should join. This module owns exactly that policy and nothing else:
 *
 *   - **three triggers, and no others** (wifi-provisioning.md P9): boot with no record,
 *     boot with a record that has not associated within SETUP_BOOT_WINDOW_MS, and a
 *     person asking for it from the panel;
 *   - **never on a mid-session drop** (P10): a running device that loses its network
 *     retries quietly; it does not start broadcasting an access point;
 *   - **60 s of grace after a successful join** (P11), then the access point closes;
 *   - **five disconnect events and the attempt is a failure** (P29), recorded so a page
 *     whose browser was dropped by the channel move can still say why;
 *   - **retry every 5 s when the access point cannot start** (P35), reported rather than
 *     given up on.
 *
 * It drives the radio through the app_board seam (raise the access point, join, read the
 * state) and knows nothing about HTTP, the page or the panel. Time is milliseconds, passed
 * in by the caller, so the host tests drive a fake clock (docs/specs/menu.md's rule for the
 * Menu applies here for the same reason).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wifi_record.h"

#ifdef __cplusplus
extern "C" {
#endif

/* How long a join is given before the attempt is called a failure, in milliseconds. The
 * join request blocks in the caller for at most this long (P22, "blocks ≤25 s"). */
#define SETUP_JOIN_TIMEOUT_MS 25000

/* Disconnect events that make an attempt a failure (P29). */
#define SETUP_JOIN_TRIES 5

/* The grace window after a successful join (P11). */
#define SETUP_GRACE_MS 60000

/* How often the tick task reads the radio (and the retry period for a failed access
 * point, P35). */
#define SETUP_TICK_MS 100
#define SETUP_AP_RETRY_MS 5000

/* What the page's state line reads (P21). */
typedef enum {
    SETUP_STATE_UNPROVISIONED = 0, /* no record */
    SETUP_STATE_SETUP,             /* the setup network is up, waiting for a choice */
    SETUP_STATE_JOINING,           /* an attempt is in flight */
    SETUP_STATE_JOINED,            /* a provisioned device, its station's business */
} setup_state_t;

/* The answer to "join this network" (P22). */
typedef enum {
    SETUP_JOIN_OK = 0,
    SETUP_JOIN_NO_SESSION, /* 409: no setup network to join from */
    SETUP_JOIN_BUSY,       /* 409: an attempt is already in flight */
    SETUP_JOIN_INVALID,    /* 400: the rules in wifi_record.h refused the SSID or password */
    SETUP_JOIN_FAILED,     /* 200 with ok:false: the radio could not join */
} setup_join_t;

/* What the page and the panel read. A copy, taken under the lock: the caller never holds
 * the machine open. */
typedef struct {
    setup_state_t state;
    bool provisioned;
    bool associated;   /* the station has an address */
    bool session;      /* the setup network is up */
    bool ap_failed;    /* it could not be raised; the retry is due */
    char network[WIFI_RECORD_SSID_MAX + 1];
    char password[WIFI_RECORD_PASS_MAX + 1]; /* the setup password in force */
    uint32_t grace_left_ms;                  /* 0 when no grace window is running */
    bool has_attempt;
    bool attempt_ok;
    char attempt_ssid[WIFI_RECORD_SSID_MAX + 1];
    int attempt_reason;
    int attempt_tries;
} setup_status_t;

/* Create the machine, read the record, decide the first state, and raise the setup
 * network if there is no network to join. Does not start a task: the caller either ticks
 * it or calls setup_start_task(), so a unit test owns the clock. */
void setup_init(void);

/* The 100 ms task that drives setup_tick(). Called once, by the services composition. */
void setup_start_task(void);

/* The machine's clock. Performs the transitions and, through the seam, raises or drops
 * the access point and reads the station. Safe to call from any task; takes the lock. */
void setup_tick(uint32_t now_ms);

/* The panel's action: raise or drop the setup network now. Returns false when the access
 * point could not be started (the retry keeps trying). */
bool setup_session_start(uint32_t now_ms);
void setup_session_stop(uint32_t now_ms);

/* The page's action: try `ssid` with `pass` (empty for an open network). Blocks until the
 * attempt resolves or SETUP_JOIN_TIMEOUT_MS passes. Stores the record first, as P24 and
 * P33 require, so the chosen network survives the attempt however it goes. */
setup_join_t setup_join(const char *ssid, const char *pass, uint32_t now_ms);

/* Forget the home network: erase the record and become unprovisioned again. The setup
 * password is a separate key and survives (P25), which is the point of keeping them apart.
 * A device with no session raises one, because that is what unprovisioned means (P9). */
bool setup_forget_network(uint32_t now_ms);

/* Change the setup network's password. Validated; applied at the next session (P5). */
bool setup_set_password(const char *password, uint32_t now_ms);

/* The record changed under us (the panel forgot the network, a password was restored):
 * re-read it and recompute what the device is. */
void setup_record_changed(uint32_t now_ms);

/* Take a copy of what the page and the panel display. */
void setup_status(setup_status_t *out);

/* The name the state maps to in the API (P21: unprovisioned/setup/joining/joined). */
const char *setup_state_name(setup_state_t state);

/* The panel's words for a disconnect reason (P31): 15 is a wrong password, 201 no access
 * point found, 205 the setup network's own access point was dropped; everything else is
 * "join failed (n)". Writes a short line, never a sentence. */
const char *setup_reason_text(int reason);

#ifdef __cplusplus
}
#endif
