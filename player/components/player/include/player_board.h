#pragma once
/*
 * player_board - what the K7Player's firmware needs from its board, and nothing else.
 *
 * Two adapters: the ESP32 classic (player/components/board_player/, NVS, the radio and
 * ESP-NOW, the curve) and the host build (player/host/board_player_host.c, which scripts all
 * of it). The policy that uses them is portable and host-tested (player_net, and later the
 * stream and the outputs); this file is machinery.
 *
 * The network record is the handover's own type (components/portable/handover): the bytes
 * are the same on both sides of the pair, so there is one definition of what a record is.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "handover.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The board's short name, for the boot line. A value, never hardware: "esp32" or "host". */
const char *player_board_name(void);

/* Bring the board up: NVS (where the record lives) and the radio's stack. False when
 * something the player cannot work without refused. */
bool player_board_init(void);

/* --------------------------------------------------------------- the record
 *
 * One blob in NVS (k7player.md R16). Load decodes and validates, so a damaged record reads
 * as no record - a player with none sweeps, which is the state that can recover. Save is
 * given a record that already encoded and decoded, and stores exactly its bytes. */
bool player_board_record_load(ho_record_t *out);
bool player_board_record_save(const ho_record_t *rec);
void player_board_record_drop(void);

/* --------------------------------------------------------------- the station
 *
 * The join attempt is the K7's own model applied to the player (wifi-provisioning.md P29):
 * an empty password is an open network, disconnects are counted per attempt, and an
 * attempt's failure is the caller's decision (25 s or five tries). */
typedef struct {
    bool associated;  /* the station has an address */
    int  disconnects; /* disconnect events since the last join() */
    int  last_reason; /* the most recent one's reason, 0 when there has been none */
    char ip[16];      /* the station's address, "" when it has none */
} player_board_wifi_t;

bool player_board_wifi_join(const char *ssid, const char *pass);
void player_board_wifi_state(player_board_wifi_t *out);

/* The K7's address by name (mDNS): `k7.local` on whichever interface is up. Bounded and
 * blocking (about a second when nothing answers); false means the name did not resolve and
 * the caller should use the record's address instead. */
bool player_board_resolve(const char *hostname, char *out, size_t cap);

/* --------------------------------------------------------------- the light
 *
 * One level, 0..255: 0 is off, the middle of the range is "dim" and the top is "bright"
 * (k7player.md R31's patterns). The board decides what that means - a PWM duty on one GPIO
 * here - and the light's policy stays portable. */
void player_board_light(uint8_t level);

/* --------------------------------------------------------------- the handover's radio
 *
 * The other side of components/portable/share's frame exchange: the player sets the
 * channel, listens for a beacon, asks, and receives one encrypted record. The callback runs
 * on the Wi-Fi task and only copies; the state machine answers on its own tick. */
typedef void (*player_board_frame_fn)(const uint8_t src[6], const uint8_t *data, size_t len,
                                      void *ctx);

bool player_board_handover_start(player_board_frame_fn on_frame, void *ctx);
void player_board_handover_stop(void);

/* ------------------------------------------------------- the stream's radio
 *
 * ESP-NOW again, and now for the audio (docs/specs/espnow-stream.md, ADR 0103). It is the same
 * radio as the handover's, so it is started once and left up: the sweep borrows it and a window
 * closing does not take the stream with it. The board dispatches by type - 0x04 audio to the
 * stream, the handover's 0x01-0x03 to its callback - so neither knows the other exists.
 *
 * `on_packet` runs on the Wi-Fi task and must be short. */
typedef void (*player_board_packet_fn)(const uint8_t *data, size_t len);
bool player_board_espnow_start(player_board_packet_fn on_packet);

/* The K7's tone packets (espnow-stream.md E12). The player's status advertises that it
 * applies them (E3a); this is where one lands. The callback runs on the Wi-Fi task and must
 * be short - player_tone_packet() parses ten bytes and copies four floats. */
void player_board_espnow_set_tone(player_board_packet_fn on_tone);

/* The status packet, to the broadcast address: the player has no way to know the K7's MAC, and
 * the K7 learns the player's from the packet's own source address. */
bool player_board_espnow_send(const uint8_t *data, size_t len);

/* One frame to the K7 that just asked. The update is the one exchange the player answers
 * directly: it has no stored address for the sender, so this registers it as a plaintext peer
 * the first time and sends (player-update.md U7a). */
bool player_board_espnow_send_to(const uint8_t dst[6], const uint8_t *data, size_t len);

/* The update's frames, on the Wi-Fi task: the callback parses and copies, and every flash
 * write happens on player_ota's own task. Same rule as the audio's (espnow-stream.md E11). */
typedef void (*player_board_update_fn)(const uint8_t src[6], const uint8_t *data, size_t len);
void player_board_espnow_set_update(player_board_update_fn on_update);

/* One frame to the K7. The request is unicast to the address the beacon came from. */
bool player_board_handover_send(const uint8_t *dst, const uint8_t *data, size_t len);

/* Register the K7 as a plaintext peer, so a request can be sent to it. The request has to
 * travel in clear: the K7 cannot derive the key for an answer until it has read the
 * request's public key, so an encrypted request is one it cannot read.
 *
 * `peer` then registers the same address *encrypted*, which is how the record comes back
 * (H30): both ends derive the LMK from the same two public keys. Both calls are idempotent
 * and may be repeated; a request is sent between them, every time. */
bool player_board_handover_address(const uint8_t mac[6]);
bool player_board_handover_peer(const uint8_t mac[6], const uint8_t lmk[16]);
void player_board_handover_peer_drop(const uint8_t mac[6]);

/* The channel to listen on. False when the radio would not take it (a station mid-join). */
bool player_board_set_channel(uint8_t channel);

void player_board_random(uint8_t *out, size_t len);
bool player_board_x25519_public(const uint8_t priv[32], uint8_t pub[32]);
bool player_board_x25519(const uint8_t priv[32], const uint8_t peer_pub[32], uint8_t shared[32]);

/* ------------------------------------------------------------ the update's flash
 *
 * player-update.md U18-U25, ADR 0117. The player writes the slot it is not running from, one
 * 4 KB block at a time; the running firmware verifies the bytes before it switches, and the
 * marker decides, on the next boot, whether the new image has to prove itself.
 */

/* The running image's version, from the application descriptor ("0.6.2"). */
const char *player_board_version(void);

/* True when an over-the-air image is waiting to prove itself; copies the version that update
 * installed, so a boot of the old image after a rollback can tell it is not the one. */
bool player_board_ota_pending(char *version, size_t cap);

/* The spare slot's size in bytes; 0 when there is no spare slot (a table without OTA). */
uint32_t player_board_ota_slot_size(void);

/* Erase and open the spare slot for `image_len` bytes. False when it will not hold them. */
bool player_board_ota_begin(uint32_t image_len);

/* Append `len` bytes at the running offset. Sequential, like every OTA write: the caller only
 * ever gives the next block. */
bool player_board_ota_write(uint32_t offset, const uint8_t *data, size_t len);

/* Discard a half-written slot. The running image is untouched. */
void player_board_ota_abort(void);

/* The image in the spare slot, checked before it can boot (U18): its chip, its own application
 * descriptor against the `version` the header claimed, the digest of the bytes written, and the
 * signature over them with the public key compiled into this firmware. This is the one ECDSA in
 * the player's path, and it is the board's because it is mbedTLS's (ADR 0117's consequence). */
bool player_board_ota_verify(const char *version, const uint8_t sha256[32], const uint8_t *sig,
                             size_t sig_len);

/* End the write, switch the boot partition and write the marker naming `version` — the image
 * that will boot. False leaves the running image the one that boots. */
bool player_board_ota_commit(const char *version);

/* This image is good: cancel the rollback (U21, U22). */
void player_board_ota_mark_valid(void);

/* This image is not good: ask the bootloader to put the other slot back. On the board it does
 * not return. */
void player_board_ota_rollback(void);

/* Reboot into the image just committed, after a moment for the last frame to leave. */
void player_board_ota_reboot(void);

#ifdef __cplusplus
}
#endif
