#pragma once
/*
 * card_select - the Playlist the broadcast last walked, remembered on the card.
 *
 * One fixed 32-byte record at the card root, k7.select. It carries only the Snapshot's
 * opaque list id; id 0 means "no selection". The record is card-only: there is no NVS
 * copy, so a card that is out or read-only leaves the in-RAM choice standing for the
 * session and the caller logs the failure. The module touches no other subsystem --
 * stdio only -- so the host tests exercise the same code the firmware runs.
 *
 * Layout (little-endian):
 *
 *   0   8   magic "K7SEL\0\0\0" (NUL-padded to eight)
 *   8   2   version = 1
 *   10  2   reserved = 0
 *   12  8   id
 *   20  8   reserved = 0
 *   28  4   CRC-32/IEEE of bytes [0, 28)
 *
 * A record is valid only when the file holds the full 32 bytes, has the magic, version 1,
 * both reserved fields zero, and a matching CRC. `load` returns false for any of those; it
 * never repairs a bad record and never logs.
 */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Read the record at `path` into `id`. False when the file is missing or shorter than
 * 32 bytes, or when the magic, version, reserved fields or CRC do not check out. */
bool card_select_load(const char *path, uint64_t *id);

/* Write `id` as the record at `path`, in place when the file exists and by creating it
 * otherwise. Flushes before returning; true only when all 32 bytes reached stdio. */
bool card_select_save(const char *path, uint64_t id);

/* Create a zero (id 0) record at `path` when the file is absent. An existing file is
 * left exactly as it is, valid or not; true when the file is there afterwards. */
bool card_select_ensure(const char *path);

#ifdef __cplusplus
}
#endif
