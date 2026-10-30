/*
 * entrance - one piece of chrome's arrival, as arithmetic (entrance.h).
 */
#include "entrance.h"

#include <stddef.h>

void entrance_init(entrance_t *e, int32_t away, int32_t here)
{
    if (!e) return;
    e->value = away;
    e->away = away;
    e->here = here;
    e->target = false;
}

bool entrance_ask(entrance_t *e, bool shown, int32_t *from, int32_t *to)
{
    if (!e) return false;
    /* The state it is already heading for: a repeated request is not a restart. This is what
     * keeps a page flip that touches every piece from re-running six slides. */
    if (e->target == shown) return false;

    e->target = shown;
    if (from) *from = e->value;
    if (to) *to = shown ? e->here : e->away;
    return true;
}

void entrance_step(entrance_t *e, int32_t value)
{
    if (!e) return;
    e->value = value;
}

bool entrance_landed(const entrance_t *e)
{
    if (!e) return false;
    return !e->target;
}
