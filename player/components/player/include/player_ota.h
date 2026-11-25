#pragma once
/*
 * player_ota - the image a K7 hands over: the begin's claim, 4 KB blocks written to the spare
 * slot, the verify the running firmware does before it switches, and the proof that keeps a new
 * image (player-update.md U10-U25, ADR 0117).
 *
 * The radio's callback calls player_ota_packet() and returns: the frame is parsed and copied,
 * and every flash write happens on this module's own task. The host harness drives the same two
 * functions with a scripted board, which is where the block machine, the refusals and the
 * rollback decision are proven - the ECDSA is the board's, exactly as the handover's curve is.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Ninety seconds for a new image to make sound (U22). */
#define PLAYER_OTA_PROOF_MS 90000

/* Two seconds without a block is an abandoned transfer (U17). */
#define PLAYER_OTA_GAP_MS 2000

void player_ota_init(void);
void player_ota_start_task(void);

/* One frame from the radio: the K7's 0x07-0x0C. Short - it parses, copies and returns. */
void player_ota_packet(const uint8_t src[6], const uint8_t *pkt, size_t len);

/* One step of the machine at `now`, in milliseconds: the queued probe or begin, a block whose
 * fragments have all arrived, the abandon's timeout, and the rollback's clock. Returns 0; the
 * task uses it for the delay and the host uses it to script time. */
int player_ota_tick(uint32_t now);

/* True while an image is being received or verified; the light's **Updating** (R31, U25). */
bool player_ota_updating(void);

/* The playout wrote decoded audio: a pending image has proved itself (U22). */
void player_ota_streamed(void);

#ifdef __cplusplus
}
#endif
