#include "player_update_act.h"

#include <stdio.h>
#include <string.h>

/* A timer in milliseconds that survives the wrap: the spec's waits are seconds, the clock is
 * millis, and the difference is the only signed thing here. */
static bool reached(uint32_t now, uint32_t at) { return (int32_t)(now - at) >= 0; }

/* The station comes back exactly as often as it was stopped: every path that ends an act or a
 * player goes through here. */
static void station_back(pua_t *a)
{
    if (!a->station_stopped) return;
    a->station_stopped = false;
    if (a->seam->station_start) a->seam->station_start(a->ctx);
}

static void finish(pua_t *a, const char *why)
{
    a->state = PUA_DONE;
    snprintf(a->outcome, sizeof a->outcome, "%s", why);
    station_back(a);
}

void pua_init(pua_t *a, const pua_seam_t *seam, void *ctx)
{
    if (!a) return;
    memset(a, 0, sizeof *a);
    a->seam = seam;
    a->ctx = ctx;
    a->state = PUA_IDLE;
}

void pua_set_image(pua_t *a, pu_version_t version, uint32_t image_len, const uint8_t sha256[32],
                   const uint8_t *sig, uint8_t sig_len)
{
    if (!a) return;
    a->loaded = image_len > 0 && sig_len > 0 && sig_len <= PU_SIG_MAX && sha256;
    if (!a->loaded) return;
    a->image_version = version;
    a->image_len = image_len;
    memcpy(a->sha256, sha256, 32);
    a->sig_len = sig_len;
    memcpy(a->sig, sig, sig_len);
}

void pua_stop(pua_t *a)
{
    if (!a) return;
    station_back(a);
    a->state = PUA_IDLE;
}

void pua_start(pua_t *a, uint32_t now, bool transfer)
{
    if (!a || !a->seam) return;
    pua_stop(a);

    a->state = PUA_IDLE;
    a->found = 0;
    a->at = 0;
    a->stale_total = 0;
    a->stale_at = 0;
    a->served = 0;
    a->failed = 0;
    a->blocks = 0;
    a->block = 0;
    a->sends = 0;
    a->probes = 0;
    a->nonce++;
    a->next_send = now;
    a->deadline = now + PUA_PROBE_MS;
    a->outcome[0] = '\0';
    a->transfer = transfer;
    a->now = now;

    if (!a->loaded) {
        a->state = PUA_DONE;
        snprintf(a->outcome, sizeof a->outcome, "no image");
        return;
    }
    a->state = PUA_PROBING;
}

static void send_probe(pua_t *a)
{
    uint8_t pkt[PU_PROBE_SIZE];
    size_t n = pu_probe_encode(a->nonce, pkt);
    if (n) a->seam->send(a->ctx, NULL, pkt, n);
}

static void send_block_now(pua_t *a, uint32_t now);
static void serve_next(pua_t *a, uint32_t now);

static void fail_player(pua_t *a, uint32_t now, const char *why)
{
    (void)why;
    a->failed++;
    station_back(a);
    a->state = PUA_IDLE;
    serve_next(a, now);
}

static void player_ok(pua_t *a, uint32_t now)
{
    a->served++;
    station_back(a);
    a->state = PUA_IDLE;
    serve_next(a, now);
}

/* The next stale player, or the end of the act. */
static void serve_next(pua_t *a, uint32_t now)
{
    while (a->at < a->found) {
        const pua_player_t *p = &a->players[a->at++];
        if (pu_version_cmp(p->version, a->image_version) == 0) continue;

        memcpy(a->cur, p->mac, 6);
        a->cur_version = p->version;
        a->stale_at++;

        if (!a->station_stopped) {
            a->station_stopped = true;
            if (a->seam->station_stop) a->seam->station_stop(a->ctx);
        }

        pu_begin_t begin = {
            .version = a->image_version,
            .board = PU_BOARD_ESP32,
            .image_len = a->image_len,
            .sig_len = a->sig_len,
        };
        memcpy(begin.sha256, a->sha256, 32);
        memcpy(begin.sig, a->sig, a->sig_len);
        uint8_t pkt[PU_BEGIN_SIZE];
        size_t n = pu_begin_encode(&begin, pkt);
        if (n) a->seam->send(a->ctx, a->cur, pkt, n);
        a->state = PUA_OPENING;
        a->deadline = now + PUA_OPEN_MS;
        return;
    }

    char why[sizeof a->outcome];
    if (a->served == 0 && a->failed == 0) snprintf(why, sizeof why, "up to date");
    else if (a->failed == 0) snprintf(why, sizeof why, "updated %d", a->served);
    else if (a->served == 0) snprintf(why, sizeof why, "failed");
    else snprintf(why, sizeof why, "updated %d, %d failed", a->served, a->failed);
    finish(a, why);
}

static void probing_done(pua_t *a, uint32_t now)
{
    a->stale_total = 0;
    for (int i = 0; i < a->found; i++)
        if (pu_version_cmp(a->players[i].version, a->image_version) != 0) a->stale_total++;

    if (a->found == 0) {
        finish(a, "no player");
        return;
    }
    if (a->stale_total == 0) {
        finish(a, "up to date");
        return;
    }
    if (!a->transfer) {
        finish(a, "found");
        return;
    }
    a->at = 0;
    serve_next(a, now);
}

static void send_block_now(pua_t *a, uint32_t now)
{
    uint32_t offset = a->block * PU_BLOCK_SIZE;
    if (offset >= a->image_len) {
        fail_player(a, now, "the image ran out");
        return;
    }
    uint32_t len = a->image_len - offset;
    if (len > PU_BLOCK_SIZE) len = PU_BLOCK_SIZE;

    if (!a->seam->image_read || !a->seam->image_read(a->ctx, offset, a->scratch, len)) {
        fail_player(a, now, "the image could not be read");
        return;
    }
    size_t lens[PU_BLOCK_MAX_FRAGS];
    size_t n = pu_block_fragment((uint16_t)a->block, a->scratch, len, a->packets, lens);
    if (!n) {
        fail_player(a, now, "the block could not be framed");
        return;
    }
    for (size_t i = 0; i < n; i++) a->seam->send(a->ctx, a->cur, a->packets + i * PU_MTU, lens[i]);
    a->next_send = now + PUA_ACK_MS;
}

void pua_frame(pua_t *a, const uint8_t src[6], const uint8_t *pkt, size_t len, uint32_t now)
{
    if (!a || !src || !pkt || len < 2 || pkt[0] != PU_VERSION) return;

    switch (pkt[1]) {
    case PU_TYPE_ANSWER: {
        if (a->state != PUA_PROBING) return;
        pu_answer_t answer;
        if (!pu_answer_parse(pkt, len, &answer)) return;
        if (answer.nonce != a->nonce) return;
        if (answer.board != PU_BOARD_ESP32) return;
        for (int i = 0; i < a->found; i++)
            if (memcmp(a->players[i].mac, src, 6) == 0) return;
        if (a->found >= PUA_MAX_PLAYERS) return;
        memcpy(a->players[a->found].mac, src, 6);
        a->players[a->found].version = answer.version;
        a->found++;
        return;
    }
    case PU_TYPE_ACK: {
        if (a->state != PUA_BLOCKS || memcmp(src, a->cur, 6) != 0) return;
        uint16_t block = 0;
        uint8_t result = 0;
        if (!pu_ack_parse(pkt, len, &block, &result)) return;
        if (block != (uint16_t)a->block) return;

        if (result == PU_ACK_WRITTEN) {
            a->block++;
            if (a->block >= a->blocks) {
                a->state = PUA_VERIFYING;
                a->deadline = now + PUA_RESULT_MS;
            } else {
                a->sends = 1;
                send_block_now(a, now);
            }
        } else if (result == PU_ACK_RETRY) {
            if (a->sends >= 3) fail_player(a, now, "the block was refused");
            else {
                a->sends++;
                send_block_now(a, now);
            }
        } else {
            fail_player(a, now, "the block was refused");
        }
        return;
    }
    case PU_TYPE_RESULT: {
        if (memcmp(src, a->cur, 6) != 0) return;
        uint8_t result = 0;
        pu_version_t version;
        if (!pu_result_parse(pkt, len, &result, &version)) return;

        if (a->state == PUA_OPENING) {
            if (result == PU_READY) {
                a->state = PUA_BLOCKS;
                a->block = 0;
                a->blocks = pu_block_count(a->image_len);
                a->sends = 1;
                send_block_now(a, now);
            } else {
                fail_player(a, now, "the player refused the image");
            }
        } else if (a->state == PUA_VERIFYING) {
            if (result == PU_OK) player_ok(a, now);
            else fail_player(a, now, "the image did not verify");
        }
        return;
    }
    default:
        return;
    }
}

void pua_tick(pua_t *a, uint32_t now)
{
    if (!a) return;
    a->now = now;

    switch (a->state) {
    case PUA_PROBING:
        if (a->probes < 3 && reached(now, a->next_send)) {
            send_probe(a);
            a->probes++;
            a->next_send += 100;
        }
        if (reached(now, a->deadline)) probing_done(a, now);
        break;
    case PUA_OPENING:
        if (reached(now, a->deadline)) fail_player(a, now, "no answer to the begin");
        break;
    case PUA_BLOCKS:
        if (reached(now, a->next_send)) {
            if (a->sends >= 3) {
                fail_player(a, now, "the block was not acknowledged");
            } else {
                a->sends++;
                send_block_now(a, now);
            }
        }
        break;
    case PUA_VERIFYING:
        if (reached(now, a->deadline)) fail_player(a, now, "no result from the player");
        break;
    default:
        break;
    }
}

pua_state_t pua_state(const pua_t *a) { return a ? a->state : PUA_IDLE; }
const char *pua_outcome(const pua_t *a) { return a ? a->outcome : ""; }
int pua_found(const pua_t *a) { return a ? a->found : 0; }
const pua_player_t *pua_player(const pua_t *a, int index)
{
    if (!a || index < 0 || index >= a->found) return NULL;
    return &a->players[index];
}
int pua_stale(const pua_t *a) { return a ? a->stale_total : 0; }
int pua_serving(const pua_t *a) { return a ? a->stale_at : 0; }
int pua_stale_total(const pua_t *a) { return a ? a->stale_total : 0; }
int pua_blocks_done(const pua_t *a) { return a ? (int)a->block : 0; }
int pua_blocks_total(const pua_t *a) { return a ? (int)a->blocks : 0; }
