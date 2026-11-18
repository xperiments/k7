#pragma once
/*
 * player_net - how the K7Player gets a network, and what it does when it has none.
 *
 * The player's half of the handover (docs/specs/network-handover.md §4, §6). It owns one
 * policy and nothing else:
 *
 *   - with no record it **sweeps** channels 1-13, 250 ms each, listening for a K7's beacon
 *     (H19-H20), and it sweeps from boot because that is all it can do (R21);
 *   - a beacon makes it **ask** - a request, then up to three retries - and a valid record
 *     is stored whole and used; a malformed one is refused whole (H37);
 *   - it prefers the home network to the K7's own, in that order, every boot (H33, R17);
 *   - an attempt is 25 s or five disconnects, the K7's own ceiling (H34);
 *   - while connected it never sweeps: the stream is on the channel it would leave (H21);
 *   - when nothing joins it alternates 20 s of retrying with 10 s of sweeping (H21, R20);
 *   - the K7 is found by `k7.local` first and by the record's address second (H36, R19).
 *
 * Time is milliseconds passed in by the caller, so the host test owns the clock. Frames
 * arrive from the board's callback, which only copies them; everything else happens on the
 * tick, on this module's own task, where there is stack for the curve.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The tick's period, and the sweep's dwell. The dwell must exceed the K7's beacon period
 * (200 ms, network-handover.md H14) for a visit to a channel to be sure of containing one. */
#define PLAYER_NET_TICK_MS   50
#define PLAYER_NET_DWELL_MS  250
#define PLAYER_NET_CHANNELS  13

/* The request and its three retries (H25's three, from this side), 400 ms apart: the K7
 * answers on its next 200 ms tick, so two ticks and a margin. */
#define PLAYER_NET_REQUESTS  4
#define PLAYER_NET_REQUEST_MS 400

/* An attempt: the K7's ceiling (wifi-provisioning.md P29). */
#define PLAYER_NET_JOIN_MS    25000
#define PLAYER_NET_JOIN_TRIES 5

/* The failing alternation: 20 s of retrying, then 10 s of sweeping (H21, R20). */
#define PLAYER_NET_RETRY_MS 20000
#define PLAYER_NET_SWEEP_MS 10000

typedef enum {
    PLAYER_NET_IDLE = 0, /* before the first tick */
    PLAYER_NET_SWEEPING, /* no record, or the sweep half of the failing alternation */
    PLAYER_NET_ASKING,   /* a beacon heard; the request is out, the record is waited for */
    PLAYER_NET_CONNECTING, /* joining a network from the record */
    PLAYER_NET_CONNECTED,  /* associated, the K7's address resolved */
    PLAYER_NET_FAILING,    /* a record, nothing joining: the 20 s half of the alternation */
} player_net_state_t;

typedef struct {
    player_net_state_t state;
    uint8_t  channel;      /* the channel it is listening on, 0 before the first step */
    bool     provisioned;  /* a record is held */
    bool     has_home;     /* that record offers a home network */
    char     network[33];  /* the one being joined, "" when none */
    bool     associated;
    bool     attempted;    /* true once a join has been made from this record */
    char     host[64];     /* the K7's address once connected, "" before */
    int      requests;     /* sends in the current ask, 0 when not asking */
} player_net_status_t;

/* Load the record, if any, and settle into the state it implies. No radio traffic yet: the
 * first tick does the entry work, so a test can watch it. */
void player_net_init(void);

/* The task that drives player_net_tick() every PLAYER_NET_TICK_MS. Called once by main. */
void player_net_start_task(void);

/* The machine's clock. Performs the transitions and, through the seam, sweeps the radio,
 * joins a network and asks for the record. Safe from any task; takes the lock. */
void player_net_tick(uint32_t now_ms);

/* Called by the board for every ESP-NOW frame received, on the Wi-Fi task. Copies the frame
 * and returns; the tick consumes it. */
void player_net_on_frame(const uint8_t src[6], const uint8_t *data, size_t len);

/* A copy of the state, for the light and the log. */
void player_net_status(player_net_status_t *out);

/* The K7's address once connected, for the stream client (PL-05). False before that. */
bool player_net_host(char *out, size_t cap);

const char *player_net_state_name(player_net_state_t state);

#ifdef __cplusplus
}
#endif
