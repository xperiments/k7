#pragma once
/*
 * art - the one user K7Art pack: the `art` partition, the pool the panel draws from and
 * the `/api/art` routes the App installs through (docs/specs/art-packs.md §4/§5).
 *
 * The factory sheet is embedded in the App's screen component and handed here at boot,
 * before the listener starts; the user sheet comes from the partition. The two are one
 * pool, factory first, and the picker is `ink_seed % (factory + user)`. An absent or
 * invalid pack leaves the factory alone - the device never refuses to boot because of the
 * user's art - and the status says why.
 *
 * The bytes are read in place and served read-only. The device never parses a K7Art:
 * label_pack_open_why() validates the header, the CRC, the bounds and the sheet's own
 * open, and the J-cards are only bytes on their way to the App.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "label.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The `art` partition's size, and the most a pack may be (art-packs.md §1). */
#define APP_SERVICES_ART_PACK_MAX (1024u * 1024u)

/* What `GET /api/art` reports. `installed` is "the partition holds bytes"; `valid` is
 * "they are a pack this firmware reads". An absent pack is installed=false, valid=true
 * (nothing is wrong); an invalid one is installed=true, valid=false with `why` set. */
typedef struct {
    bool installed;
    bool valid;
    char name[33];
    char author[33];
    uint16_t design_count;
    uint32_t sheet_crc;
    uint16_t factory_count;
    char why[160];
} art_status_t;

/* The screen's lock bridge: the HTTP task calls `fn` before it drops the active user
 * sheet, and `fn` takes the screen's lock, calls app_services_art_quiesced(), lets the
 * label forget the sheet, and releases. False means the lock could not be taken and the
 * upload changes nothing. NULL (the host) runs app_services_art_quiesced() directly. */
typedef bool (*art_quiesce_fn)(void *ctx);
void app_services_art_set_quiesce(art_quiesce_fn fn, void *ctx);

/* Hand the embedded factory sheet and the compiled font count over. Called from the
 * screen's boot, before app_services_start(): `font_count` is the pack's compatibility
 * contract and `size` is the sheet's own length. False when the factory sheet does not
 * open; the device still serves, without a label. */
bool app_services_art_set_factory(const uint8_t *sheet, size_t size, uint8_t font_count);

/* Open the partition's pack and register GET/POST/DELETE /api/art and GET /api/art/pack.
 * Call before http_server_start(). A bad pack is a state with a reason, not a failure;
 * only a route that could not register is. */
esp_err_t app_services_art_start(void);

/* Under the screen's lock: drop the user sheet, so the HTTP task may unmap the bytes its
 * template strings point into. The label calls cassette_label_pool_dropped() after this. */
void app_services_art_quiesced(void);

/* Under the screen's lock, for the Track's ink seed: the pool's sheet and template, and
 * the generation that changes whenever the pool does (so the label rebuilds even when the
 * index lands on the same template of a different sheet). False when no sheet is open. */
bool app_services_art_pick(uint32_t ink_seed, const label_sheet_t **sheet,
                           uint16_t *template, uint32_t *generation);

/* The pool's size and the active pack's status, for the log and the status route. */
uint16_t app_services_art_count(void);
void app_services_art_status(art_status_t *out);

#ifdef __cplusplus
}
#endif
