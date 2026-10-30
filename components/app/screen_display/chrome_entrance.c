/*
 * chrome_entrance - one piece of the controls screen's chrome, arriving or leaving; see
 * chrome_entrance.h.
 *
 * One animation per piece, and the animator's own variable is the piece, not the object it
 * moves: that is what lets one exec callback serve every piece, and what makes
 * lv_anim_delete(piece, exec) the cancellation for all of them. The value written each step
 * is a screen coordinate for the bands, an offset for a bar, an opacity for the window.
 */
#include "chrome_entrance.h"

#include <stddef.h>

#include "cassette_bars.h"   /* CASSETTE_BARS_SLIDE_MS: the chrome's one clock */

static void entrance_exec_cb(void *var, int32_t value)
{
    chrome_entrance_t *c = var;
    entrance_step(&c->rule, value);
    if (c->write) c->write(value, c->ctx);
}

/* The motion out is over: hide the piece so it is not in the draw tree. A piece that arrived
 * never reaches here - the callback is only registered on the way out - and a reversal deleted
 * the animation, so this does not run at all. */
static void entrance_done_cb(lv_anim_t *a)
{
    chrome_entrance_t *c = lv_anim_get_user_data(a);
    if (!c) return;
    if (entrance_landed(&c->rule) && c->hide) c->hide(c->ctx);
}

void chrome_entrance_init(chrome_entrance_t *c, int32_t away, int32_t here,
                          void (*write)(int32_t value, void *ctx),
                          void (*hide)(void *ctx), void *ctx)
{
    if (!c) return;
    entrance_init(&c->rule, away, here);
    c->write = write;
    c->hide = hide;
    c->ctx = ctx;
}

void chrome_entrance_set(chrome_entrance_t *c, bool shown)
{
    if (!c) return;

    int32_t from = 0;
    int32_t to = 0;
    if (!entrance_ask(&c->rule, shown, &from, &to)) return;

    /* Place it before the motion starts. A piece that is coming back is still hidden from its
     * last exit, and its write callback is what brings it into the tree. */
    if (shown && c->write) c->write(from, c->ctx);

    lv_anim_delete(c, entrance_exec_cb);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, c);
    lv_anim_set_exec_cb(&a, entrance_exec_cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, CASSETTE_BARS_SLIDE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_user_data(&a, c);
    if (!shown) lv_anim_set_completed_cb(&a, entrance_done_cb);
    lv_anim_start(&a);
}
