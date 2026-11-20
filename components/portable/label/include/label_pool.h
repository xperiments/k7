#pragma once
/*
 * label_pool - the factory sheet and, when the `art` partition holds a valid pack, the
 * user's, as one pool: factory first, then the user's, each in id order
 * (docs/specs/art-packs.md §5).
 *
 * The pool is deliberately dull. It owns two opened sheets, never the bytes; the caller
 * opens them (the factory from its embedded blob, the user's with label_pack_open_why)
 * and hands them over. `label_pool_pick()` is the only mapping rule:
 *
 *     index = ink_seed % (factory_count + user_count)
 *     index < factory_count  ->  the factory sheet, template index
 *     else                   ->  the user sheet, index - factory_count
 *
 * An absent or invalid user pack leaves the factory alone, because the caller simply
 * never sets one. `label_pick()` still picks within a sheet; this is the mapping above it
 * that the panel's label code applies.
 */
#include <stdbool.h>
#include <stdint.h>

#include "label.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    label_sheet_t *factory;
    label_sheet_t *user;
    uint16_t factory_count;
    uint16_t user_count;
} label_pool_t;

/* A zeroed pool is valid: no sheets, no designs. */
void label_pool_init(label_pool_t *pool);

/* Take ownership of an opened sheet and close the one it replaces. A NULL clears that
 * half. Returns false for a NULL pool; a NULL sheet is the clear, not a failure. */
bool label_pool_set_factory(label_pool_t *pool, label_sheet_t *sheet);
bool label_pool_set_user(label_pool_t *pool, label_sheet_t *sheet);
void label_pool_clear_user(label_pool_t *pool);

/* Close both sheets and leave the pool empty. */
void label_pool_close(label_pool_t *pool);

uint16_t label_pool_count(const label_pool_t *pool);
uint16_t label_pool_factory_count(const label_pool_t *pool);
uint16_t label_pool_user_count(const label_pool_t *pool);

/* The sheet and its own template at a pool index, factory first. False when the pool is
 * empty or `index` is past it. */
bool label_pool_at(const label_pool_t *pool, uint32_t index,
                   const label_sheet_t **sheet, uint16_t *template);

/* The same, chosen by the Track's ink seed: `ink_seed % count`. */
bool label_pool_pick(const label_pool_t *pool, uint32_t ink_seed,
                     const label_sheet_t **sheet, uint16_t *template);

#ifdef __cplusplus
}
#endif
