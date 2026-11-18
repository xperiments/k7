#pragma once
/*
 * share - the K7's share window: the sixty seconds in which it hands its network to a
 * K7Player.
 *
 * A person opens it from the Menu. While it is open the device beacons a secret-free frame
 * on its own channel, answers the first player that asks, and answers nobody else; the
 * answer is a **Network record** encrypted under a key the two just derived. When it
 * closes, nothing about it is left behind: no registry, no MAC, no key (ADR 0098).
 *
 * The policy is here and the radio is the board's (app_board's share seam). The bytes are
 * components/portable/handover's, so both firmwares speak the same format, and
 * docs/specs/network-handover.md is the contract: §3 is this window, §5 the exchange.
 *
 * Time is milliseconds passed in by the caller, so the host test owns the clock - the same
 * rule the setup machine and the Menu follow.
 *
 * The receive path is split on purpose. The adapter's callback runs on the Wi-Fi task,
 * where there is no stack for a curve, so it only copies the frame
 * (share_on_frame()). The window's own task answers it on the next tick, with the 6 KB of
 * stack the X25519 call needs - and that task exists only while the window does, created by
 * share_start() and deleted by itself once the window is closed.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The window's length, and how often the tick runs. The tick's period *is* the beacon's
 * period: network-handover.md H14 asks for one every 200 ms, and H19 needs the player's
 * 250 ms dwell to contain one, which a 200 ms period guarantees and a slower one does not. */
#define SHARE_WINDOW_MS 60000
#define SHARE_TICK_MS   200

/* The record, and the three retries H25 allows it: a player whose frame was lost asks
 * again, and the window answers that player up to four times in total. */
#define SHARE_ANSWERS 4

typedef enum {
    SHARE_CLOSED = 0, /* no window; the radio is not involved */
    SHARE_OPEN,       /* beaconing, waiting for the first player */
    SHARE_ANSWERED,   /* one player answered; no more beacons, its retries still answered */
} share_state_t;

/* What the panel reads, taken under the lock. */
typedef struct {
    share_state_t state;
    bool     failed;    /* the last start was refused by the radio; the row says so */
    uint32_t left_ms;   /* the countdown, 0 when closed */
    bool     has_player;
    uint8_t  player[6];
} share_status_t;

/* Create the lock and settle into CLOSED. Nothing touches the radio. */
void share_init(void);

/* The machine's clock, and the only place frames are sent: the beacon's tick and the
 * answer's turn both happen here. Safe from any task; takes the lock. */
void share_tick(uint32_t now_ms);

/* The panel's action. Opens a window, or answers true when one is already open; false when
 * the radio or the record refused, and `failed` says why on the next status. */
bool share_start(uint32_t now_ms);

/* The panel's other action, and the window's expiry. Idempotent. */
void share_stop(uint32_t now_ms);

/* A copy of the state, for the Menu's row. */
void share_status(share_status_t *out);

/* Called by the board adapter for every ESP-NOW frame received while a window is open,
 * on the Wi-Fi task. Decodes the request and records who asked; never derives a key and
 * never sends. A frame from anyone but the answered player, or after the window closed,
 * is dropped here. */
void share_on_frame(const uint8_t src[6], const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
