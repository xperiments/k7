/*
 * player_ota - the receive half of a player's firmware update (player-update.md U10-U25).
 *
 * One lock guards the small state the radio's callback and this task share: the control queue
 * (a probe to answer, a begin to start, a block to acknowledge again) and the 4 KB block being
 * assembled. Everything that can block - the erase, the write, the read-back verify - happens
 * on the task, outside that lock.
 *
 * A block's fragments are copied by the callback into one stage; when the last one is in, the
 * stage is the task's until the write and the acknowledgement are done, and the callback drops
 * anything that arrives meanwhile. The K7 waits for each block's acknowledgement before sending
 * the next, so nothing is dropped in a healthy transfer.
 *
 * The begin is the exception. `esp_ota_begin` erases the whole spare slot - about five seconds
 * on this chip - so the begin is answered with `ready` only when the slot is open, and the K7
 * sends no block before it (U10, U13a).
 */
#include "player_ota.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "player_board.h"
#include "player_update.h"

static const char *TAG = "ota";

/* Two entries: only a probe, a begin and a repeat acknowledgement are ever carried; the blocks
 * are assembled in place. */
#define OTA_CTRL_SLOTS 2

typedef enum {
    OTA_CTRL_PROBE = 1,
    OTA_CTRL_BEGIN,
    OTA_CTRL_REACK,
} ota_ctrl_kind_t;

typedef struct {
    uint8_t  kind;
    uint8_t  src[6];
    uint16_t len;
    uint16_t block;
    uint8_t  data[PU_BEGIN_SIZE];
} ota_ctrl_t;

typedef struct {
    bool     updating;
    bool     pending;       /* this boot is an update that has not proved itself */
    bool     pending_timed;
    uint32_t pending_since;

    /* the act */
    uint8_t      peer[6];
    pu_version_t mine;
    pu_version_t version;
    uint32_t     image_len;
    uint8_t      sha256[32];
    uint8_t      sig[PU_SIG_MAX];
    uint8_t      sig_len;
    uint32_t     written;
    uint16_t     next_block;
    uint16_t     last_acked;
    bool         have_acked;
    bool         reack_pending;
    uint32_t     last_activity;

    /* the block being assembled */
    uint16_t block;
    uint8_t  nfrags;
    uint32_t mask;
    uint32_t stage_len;
    bool     ready;         /* every fragment is in: the task owns the stage */
    bool     busy;          /* the task is writing it: the callback drops fragments */
    /* 4 KB is a third of what this chip has left after the ring, so it is the heap's, not the
     * link's: the static segment overflowed by 208 bytes the day it was in .bss. */
    uint8_t *stage;

    /* the control queue */
    ota_ctrl_t ctrl[OTA_CTRL_SLOTS];
    uint8_t    head;
    uint8_t    count;
} ota_t;

static ota_t S;
static SemaphoreHandle_t s_lock;

static void lock(void)
{
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    if (s_lock) xSemaphoreGive(s_lock);
}

/* "0.6.2" to three numbers, the form the header and every frame carry. */
static pu_version_t parse_version(const char *text)
{
    pu_version_t v = {0, 0, 0};
    unsigned field[3] = {0, 0, 0};
    int part = 0;
    if (text) {
        /* A leading "v" (a descriptor IDF filled from git describe) is not a number: the
         * three that follow are. */
        while (*text && (*text < '0' || *text > '9')) text++;
        for (const char *p = text; *p && part < 3; p++) {
            if (*p >= '0' && *p <= '9') {
                field[part] = field[part] * 10u + (unsigned)(*p - '0');
                if (field[part] > 0xFFFF) field[part] = 0xFFFF;
            } else if (*p == '.' && part < 2) {
                part++;
            } else {
                break;
            }
        }
    }
    v.major = (uint16_t)field[0];
    v.minor = (uint16_t)field[1];
    v.patch = (uint16_t)field[2];
    return v;
}

/* --------------------------------------------------------------- the queue */

static bool ctrl_push(uint8_t kind, const uint8_t src[6], const uint8_t *data, size_t len,
                      uint16_t block)
{
    if (S.count >= OTA_CTRL_SLOTS) return false;
    ota_ctrl_t *c = &S.ctrl[(S.head + S.count) % OTA_CTRL_SLOTS];
    memset(c, 0, sizeof *c);
    c->kind = kind;
    c->block = block;
    if (src) memcpy(c->src, src, 6);
    if (data && len) {
        if (len > sizeof c->data) len = sizeof c->data;
        memcpy(c->data, data, len);
        c->len = (uint16_t)len;
    }
    S.count++;
    return true;
}

/* A begin waits while a block is being written: taking it would abort a slot mid-write. */
static bool ctrl_peek(ota_ctrl_t *out)
{
    if (!S.count) return false;
    *out = S.ctrl[S.head];
    return !(out->kind == OTA_CTRL_BEGIN && S.busy);
}

static void ctrl_pop(void)
{
    if (!S.count) return;
    S.head = (uint8_t)((S.head + 1) % OTA_CTRL_SLOTS);
    S.count--;
}

/* ------------------------------------------------------------ what it sends */

static void send_ack(const uint8_t dst[6], uint16_t block, uint8_t result)
{
    uint8_t pkt[PU_ACK_SIZE];
    size_t n = pu_ack_encode(block, result, pkt);
    if (n) player_board_espnow_send_to(dst, pkt, n);
}

static void send_result(const uint8_t dst[6], uint8_t result, pu_version_t version)
{
    uint8_t pkt[PU_RESULT_SIZE];
    size_t n = pu_result_encode(result, version, pkt);
    if (n) player_board_espnow_send_to(dst, pkt, n);
}

/* ------------------------------------------------------------ the acts */

static void begin(const uint8_t src[6], const uint8_t *pkt, size_t len, uint32_t now)
{
    pu_begin_t claim;
    if (!pu_begin_parse(pkt, len, &claim)) return;

    if (claim.board != PU_BOARD_ESP32) {
        send_result(src, PU_WRONG_BOARD, claim.version);
        return;
    }
    if (pu_version_cmp(claim.version, S.mine) == 0) {
        send_result(src, PU_SAME_VERSION, claim.version);
        return;
    }
    if (claim.image_len > player_board_ota_slot_size()) {
        send_result(src, PU_NO_ROOM, claim.version);
        return;
    }

    /* A begin while an image is in flight is the K7 starting the act again: drop what we had
     * and take this one, which is the recovery a stop-and-wait transfer needs (U17). */
    lock();
    S.updating = false;
    S.ready = false;
    S.busy = false;
    unlock();
    player_board_ota_abort();
    if (!player_board_ota_begin(claim.image_len)) {
        send_result(src, PU_NO_ROOM, claim.version);
        return;
    }

    lock();
    memcpy(S.peer, src, 6);
    S.image_len = claim.image_len;
    S.version = claim.version;
    memcpy(S.sha256, claim.sha256, 32);
    S.sig_len = claim.sig_len;
    memcpy(S.sig, claim.sig, claim.sig_len);
    S.written = 0;
    S.next_block = 0;
    S.have_acked = false;
    S.reack_pending = false;
    S.block = 0;
    S.mask = 0;
    S.nfrags = 0;
    S.stage_len = 0;
    S.updating = true;
    S.last_activity = now;
    unlock();

    send_result(src, PU_READY, claim.version);
    ESP_LOGI(TAG, "taking %u.%u.%u: %u B in %u blocks", (unsigned)claim.version.major,
             (unsigned)claim.version.minor, (unsigned)claim.version.patch,
             (unsigned)claim.image_len, (unsigned)pu_block_count(claim.image_len));
}

/* The spare slot is written; prove it, switch to it, or put it aside. */
static void finish(uint32_t now)
{
    uint8_t sha[32], sig[PU_SIG_MAX], peer[6];
    uint8_t sig_len;
    pu_version_t version;

    lock();
    memcpy(sha, S.sha256, 32);
    memcpy(sig, S.sig, S.sig_len);
    sig_len = S.sig_len;
    memcpy(peer, S.peer, 6);
    version = S.version;
    unlock();

    char text[PU_VERSION_TEXT_MAX];
    pu_version_format(version, text);
    if (!player_board_ota_verify(text, sha, sig, sig_len)) {
        ESP_LOGW(TAG, "the image does not verify: keeping what was running");
        lock();
        S.updating = false;
        unlock();
        player_board_ota_abort();
        send_result(peer, PU_VERIFY_FAILED, version);
        return;
    }
    if (!player_board_ota_commit(text)) {
        ESP_LOGW(TAG, "the image will not switch");
        lock();
        S.updating = false;
        unlock();
        player_board_ota_abort();
        send_result(peer, PU_ABORTED, version);
        return;
    }

    lock();
    S.updating = false;
    unlock();
    ESP_LOGI(TAG, "switching to %u.%u.%u after %u B", (unsigned)version.major,
             (unsigned)version.minor, (unsigned)version.patch, (unsigned)now);
    send_result(peer, PU_OK, version);
    player_board_ota_reboot();
}

static void abandon(const char *why)
{
    lock();
    bool was = S.updating;
    S.updating = false;
    S.ready = false;
    S.busy = false;
    S.mask = 0;
    S.stage_len = 0;
    unlock();
    if (was) {
        player_board_ota_abort();
        ESP_LOGW(TAG, "abandoned: %s", why);
    }
}

/* The stage holds a whole block: write it, acknowledge it, and finish the act on the last one. */
static void take_block(uint32_t now)
{
    uint16_t block;
    uint32_t len;
    uint32_t written_before;

    lock();
    block = S.block;
    len = S.stage_len;
    written_before = S.written;
    unlock();

    uint32_t offset = (uint32_t)block * PU_BLOCK_SIZE;
    /* esp_ota_write is sequential: only the next block can be the one in the stage. */
    bool ok = len > 0 && offset == written_before &&
              player_board_ota_write(offset, S.stage, len);

    uint8_t peer[6];
    lock();
    memcpy(peer, S.peer, 6);
    S.busy = false;
    S.ready = false;
    S.mask = 0;
    S.stage_len = 0;
    S.last_activity = now;
    if (ok) {
        S.written = offset + len;
        S.last_acked = block;
        S.have_acked = true;
        S.next_block = (uint16_t)(block + 1);
    }
    bool done = ok && S.written >= S.image_len;
    uint32_t written = S.written;
    unlock();

    if (ok && (block < 2 || block % 32 == 0)) ESP_LOGI(TAG, "block %u written", (unsigned)block);
    send_ack(peer, block, ok ? PU_ACK_WRITTEN : PU_ACK_REFUSED);
    ESP_LOGD(TAG, "block %u: %u B, %u written, %s", (unsigned)block, (unsigned)len,
             (unsigned)written, ok ? "ok" : "refused");
    if (done) finish(now);
}

static void answer_probe(const ota_ctrl_t *item)
{
    uint32_t nonce = 0;
    if (!pu_probe_parse(item->data, item->len, &nonce)) return;

    /* U6: a player with no record does not answer - it could not prove the image afterwards. */
    ho_record_t record;
    if (!player_board_record_load(&record)) return;

    pu_answer_t answer = {
        .nonce = nonce,
        .version = S.mine,
        .board = PU_BOARD_ESP32,
        .flags = 0,
    };
    uint8_t pkt[PU_ANSWER_SIZE];
    size_t n = pu_answer_encode(&answer, pkt);
    if (n) player_board_espnow_send_to(item->src, pkt, n);
}

/* Take the queue's head, then act on it outside the lock: a begin erases the slot. */
static bool take_ctrl(uint32_t now)
{
    ota_ctrl_t item;
    lock();
    bool have = ctrl_peek(&item);
    if (have) ctrl_pop();
    unlock();
    if (!have) return false;

    switch (item.kind) {
    case OTA_CTRL_PROBE:
        answer_probe(&item);
        break;
    case OTA_CTRL_BEGIN:
        begin(item.src, item.data, item.len, now);
        break;
    case OTA_CTRL_REACK:
        lock();
        S.reack_pending = false;
        unlock();
        send_ack(item.src, item.block, PU_ACK_WRITTEN);
        break;
    default:
        break;
    }
    return true;
}

int player_ota_tick(uint32_t now)
{
    /* The image a K7 gave us has to prove itself: sound, or the other slot (U22). */
    lock();
    if (S.pending && !S.pending_timed) {
        S.pending_timed = true;
        S.pending_since = now;
    }
    bool expired = S.pending && S.pending_timed && now - S.pending_since > PLAYER_OTA_PROOF_MS;
    if (expired) S.pending = false;
    unlock();
    if (expired) {
        ESP_LOGW(TAG, "the image never made sound: rolling back");
        player_board_ota_rollback();
        return 0;
    }

    lock();
    bool ready = S.ready;
    unlock();

    if (!take_ctrl(now) && ready) take_block(now);

    lock();
    bool timeout = S.updating && !S.busy && !S.ready && now - S.last_activity > PLAYER_OTA_GAP_MS;
    unlock();
    if (timeout) abandon("no block for two seconds");

    return 0;
}

/* ------------------------------------------------------------ the entry points */

void player_ota_packet(const uint8_t src[6], const uint8_t *pkt, size_t len)
{
    if (!src || !pkt || len < 2) return;
    if (pkt[0] != PU_VERSION) return;

    lock();
    switch (pkt[1]) {
    case PU_TYPE_PROBE:
        if (len == PU_PROBE_SIZE) ctrl_push(OTA_CTRL_PROBE, src, pkt, len, 0);
        break;
    case PU_TYPE_BEGIN:
        if (len == PU_BEGIN_SIZE) ctrl_push(OTA_CTRL_BEGIN, src, pkt, len, 0);
        break;
    case PU_TYPE_BLOCK: {
        pu_frag_t frag;
        if (!S.updating || S.busy || !S.stage) break;
        if (!pu_block_parse(pkt, len, &frag)) break;

        /* A retried block was written already: acknowledge it again rather than appending it
         * to a slot that only grows (U15). */
        if (S.have_acked && frag.block == S.last_acked) {
            if (!S.reack_pending) S.reack_pending = ctrl_push(OTA_CTRL_REACK, src, NULL, 0, frag.block);
            break;
        }
        if (frag.block != S.next_block) break;

        if (frag.block != S.block || frag.nfrags != S.nfrags) {
            S.block = frag.block;
            S.nfrags = frag.nfrags;
            S.mask = 0;
            S.stage_len = 0;
        }
        size_t at = (size_t)frag.frag * PU_FRAG_PAYLOAD;
        if (at + frag.payload_len > PU_BLOCK_SIZE) break;
        memcpy(S.stage + at, frag.payload, frag.payload_len);
        S.mask |= 1u << frag.frag;
        if (at + frag.payload_len > S.stage_len) S.stage_len = (uint32_t)(at + frag.payload_len);
        if (S.mask == (1u << S.nfrags) - 1u) {
            S.ready = true;
            S.busy = true;
        }
        break;
    }
    default:
        break;
    }
    unlock();
}

bool player_ota_updating(void)
{
    lock();
    bool updating = S.updating;
    unlock();
    return updating;
}

void player_ota_streamed(void)
{
    bool proved = false;
    lock();
    if (S.pending) {
        S.pending = false;
        proved = true;
    }
    unlock();
    if (proved) {
        player_board_ota_mark_valid();
        ESP_LOGI(TAG, "the image made sound: it stays");
    }
}

void player_ota_init(void)
{
    memset(&S, 0, sizeof S);
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!S.stage) S.stage = heap_caps_malloc(PU_BLOCK_SIZE, MALLOC_CAP_8BIT);
    if (!S.stage) ESP_LOGE(TAG, "no memory for the block stage: updates cannot be taken");

    S.mine = parse_version(player_board_version());

    char marker[PU_VERSION_TEXT_MAX];
    memset(marker, 0, sizeof marker);
    if (!player_board_ota_pending(marker, sizeof marker)) {
        /* The first boot after the cable: a USB flash is trusted, and a pending image here
         * would roll back into an empty slot (U21). */
        player_board_ota_mark_valid();
        return;
    }
    if (strcmp(marker, player_board_version()) != 0) {
        /* The old image after a rollback: the marker names something that is not running. */
        player_board_ota_mark_valid();
        return;
    }
    S.pending = true;
    ESP_LOGI(TAG, "waiting for %s to prove itself", marker);
}

static void ota_task(void *arg)
{
    (void)arg;
    for (;;) {
        player_ota_tick((uint32_t)(esp_timer_get_time() / 1000));
        /* The K7's acknowledgement wait is 200 ms; a 20 ms tick keeps the flash writes off the
         * radio's callback without making the block-ahead wait longer than one. */
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void player_ota_start_task(void)
{
    if (xTaskCreatePinnedToCore(ota_task, "ota", 5120, NULL, 3, NULL, 0) != pdPASS)
        ESP_LOGE(TAG, "no memory for the update task");
}
