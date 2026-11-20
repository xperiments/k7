/*
 * label_pool - see include/label_pool.h.
 *
 * Ownership is explicit and small: a sheet handed to the pool is closed by the pool, the
 * caller keeps nothing. That is what lets the art endpoint replace the user half under
 * the screen's lock without a second owner to keep in step.
 */
#include "label_pool.h"

#include <stddef.h>

void label_pool_init(label_pool_t *pool)
{
    if (!pool) return;
    pool->factory = NULL;
    pool->user = NULL;
    pool->factory_count = 0;
    pool->user_count = 0;
}

bool label_pool_set_factory(label_pool_t *pool, label_sheet_t *sheet)
{
    if (!pool) return false;
    if (pool->factory && pool->factory != sheet) label_close(pool->factory);
    pool->factory = sheet;
    pool->factory_count = sheet ? label_templates(sheet) : 0;
    return true;
}

bool label_pool_set_user(label_pool_t *pool, label_sheet_t *sheet)
{
    if (!pool) return false;
    if (pool->user && pool->user != sheet) label_close(pool->user);
    pool->user = sheet;
    pool->user_count = sheet ? label_templates(sheet) : 0;
    return true;
}

void label_pool_clear_user(label_pool_t *pool) { (void)label_pool_set_user(pool, NULL); }

void label_pool_close(label_pool_t *pool)
{
    if (!pool) return;
    if (pool->factory) label_close(pool->factory);
    if (pool->user) label_close(pool->user);
    label_pool_init(pool);
}

uint16_t label_pool_count(const label_pool_t *pool)
{
    if (!pool) return 0;
    return (uint16_t)(pool->factory_count + pool->user_count);
}

uint16_t label_pool_factory_count(const label_pool_t *pool)
{
    return pool ? pool->factory_count : 0;
}

uint16_t label_pool_user_count(const label_pool_t *pool) { return pool ? pool->user_count : 0; }

bool label_pool_at(const label_pool_t *pool, uint32_t index,
                   const label_sheet_t **sheet, uint16_t *template)
{
    if (!pool || !sheet || !template) return false;
    if (index < pool->factory_count) {
        *sheet = pool->factory;
        *template = (uint16_t)index;
        return *sheet != NULL;
    }
    index -= pool->factory_count;
    if (index >= pool->user_count) return false;
    *sheet = pool->user;
    *template = (uint16_t)index;
    return *sheet != NULL;
}

bool label_pool_pick(const label_pool_t *pool, uint32_t ink_seed,
                     const label_sheet_t **sheet, uint16_t *template)
{
    const uint16_t count = label_pool_count(pool);
    if (!count) {
        if (sheet) *sheet = NULL;
        if (template) *template = 0;
        return false;
    }
    return label_pool_at(pool, ink_seed % count, sheet, template);
}
