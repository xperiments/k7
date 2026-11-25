#pragma once
/*
 * player_update_act - the K7's side of a player's update: the probe, the players that answer,
 * and the per-player transfer (player-update.md U4-U10, U26-U29; ADR 0117).
 *
 * It is the policy and none of the machinery: the frames go out through `send`, the image comes
 * in through `image_read`, and the station is paused and resumed through the seam. The board
 * hands it frames from the radio's worker and calls its tick; the host hands it the same and
 * scripts a player, which is where the retries, the drops and the per-player loop are proven.
 *
 * The sequence, per the spec: a probe broadcast three times; every answer is a player; each
 * stale one is served in turn - the station stops, the begin goes out and waits for `ready`,
 * blocks go out stop-and-wait, the result comes back - and the station starts again before the
 * next. Nothing outlives the act: the players are its own, in RAM, and the K7 still keeps no
 * record (ADR 0098).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "player_update.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One act serves the players that answered one probe; four is more than a household has and
 * bounds the act's memory. */
#define PUA_MAX_PLAYERS 4

/* The begin's wait: the player erases a whole slot before it says `ready` (U13a). */
#define PUA_OPEN_MS 15000
/* The block's acknowledgement wait (U13). */
#define PUA_ACK_MS 200
/* The verify's wait (U16). */
#define PUA_RESULT_MS 10000
/* The probe: three broadcasts, then three hundred milliseconds of answers. */
#define PUA_PROBE_MS 500

typedef enum {
    PUA_IDLE = 0,
    PUA_PROBING,   /* the probe is out; answers are being collected */
    PUA_OPENING,   /* the begin is out; waiting for `ready` or a refusal */
    PUA_BLOCKS,    /* the blocks are going out, stop-and-wait */
    PUA_VERIFYING, /* the last block is written; waiting for the result */
    PUA_DONE,      /* the act is over; pua_outcome says how */
} pua_state_t;

typedef struct {
    /* One frame, unicast to `dst`, or broadcast when `dst` is NULL. Called from the radio's
     * worker (or from the host), never from an ISR. */
    bool (*send)(void *ctx, const uint8_t *dst, const uint8_t *pkt, size_t len);
    /* The image's bytes at `offset`, `len` of them, into `out`. */
    bool (*image_read)(void *ctx, uint32_t offset, uint8_t *out, size_t len);
    void (*station_stop)(void *ctx);
    void (*station_start)(void *ctx);
} pua_seam_t;

typedef struct {
    uint8_t      mac[6];
    pu_version_t version;
} pua_player_t;

typedef struct {
    const pua_seam_t *seam;
    void *ctx;

    /* the image the K7 carries, read from its partition at boot (U1) */
    bool         loaded;
    pu_version_t image_version;
    uint32_t     image_len;
    uint8_t      sha256[32];
    uint8_t      sig[PU_SIG_MAX];
    uint8_t      sig_len;

    /* the act */
    pua_state_t  state;
    bool         transfer;   /* a press, not just the probe the list opens */
    uint32_t     now;
    uint32_t     next_send;
    uint32_t     deadline;
    uint32_t     nonce;
    int          probes;

    pua_player_t players[PUA_MAX_PLAYERS];
    int          found;
    int          at;          /* where the search for the next stale player is */
    int          stale_total;
    int          stale_at;    /* 1-based, for the row's "2 of 3" */
    int          served;
    int          failed;

    uint8_t      cur[6];
    pu_version_t cur_version;
    bool         station_stopped;

    uint32_t     blocks;      /* how many 4 KB blocks the image is */
    uint32_t     block;       /* the one in flight */
    int          sends;
    char         outcome[64];

    uint8_t      scratch[PU_BLOCK_SIZE];
    uint8_t      packets[PU_BLOCK_MAX_FRAGS * PU_MTU];
} pua_t;

void pua_init(pua_t *a, const pua_seam_t *seam, void *ctx);

/* The image the K7 will hand over, once it has read and checked its partition. Until this is
 * called the act has nothing to offer and says so. */
void pua_set_image(pua_t *a, pu_version_t version, uint32_t image_len, const uint8_t sha256[32],
                   const uint8_t *sig, uint8_t sig_len);

/* Start an act at `now`. `transfer` false is the probe the Player list opens: find the players
 * and stop, so the row can say who is there and whether an image is needed. `transfer` true
 * serves every stale player that answers. */
void pua_start(pua_t *a, uint32_t now, bool transfer);

/* One frame from a player (the probe's answer, an acknowledgement, a result). May send the next
 * block, so it runs where sends are allowed: the radio's worker or the host. */
void pua_frame(pua_t *a, const uint8_t src[6], const uint8_t *pkt, size_t len, uint32_t now);

/* One step: the probe's schedule, the retries, the waits. */
void pua_tick(pua_t *a, uint32_t now);

/* Abandon the act (a person leaves the list): the station goes back on, nothing is sent. */
void pua_stop(pua_t *a);

pua_state_t pua_state(const pua_t *a);
const char *pua_outcome(const pua_t *a);

/* For the row: who answered, how many need the image, which one is being served, and how far. */
int  pua_found(const pua_t *a);
const pua_player_t *pua_player(const pua_t *a, int index);
int  pua_stale(const pua_t *a);
int  pua_serving(const pua_t *a);
int  pua_stale_total(const pua_t *a);
int  pua_blocks_done(const pua_t *a);
int  pua_blocks_total(const pua_t *a);

#ifdef __cplusplus
}
#endif
