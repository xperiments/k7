/*
 * One small knob inside a bar cell; see knob_cell.h.
 *
 * The same numbers the DJ block has always used: a 44 px disc inset 26 px in the cell, the crown
 * 32 px from the centre with 4 px dots, and the owner's 4x16 line whose pivot is the disc's
 * centre. Nothing here allocates after create, and a value move writes only the dots' colours and
 * one rotation.
 */
#include "knob_cell.h"

#include <math.h>

#include "cassette_layout.h"
#include "cassette_style.h"
#include "cassette_widgets.h"
#include "knob_editor.h"

#define KNOB_D       44
#define KNOB_R       (KNOB_D / 2)
#define KNOB_INSET   ((CASSETTE_CELL_W - KNOB_D) / 2)
#define DOT_R        32   /* outside the knob circle, inside the carcase */
#define DOT_D        4
#define STICK_W      4
#define STICK_L      16   /* the owner's tuned line: 4 wide, 16 long */
#define STICK_R      3

void knob_cell_apply(knob_cell_t *c, const knob_t *k)
{
    if (!c || !c->disc || !k) return;
    knob_editor_crown_ink(c->dot, KNOB_CELL_DOTS, k);
    if (c->stick)
        lv_obj_set_style_transform_rotation(c->stick, knob_angle_ddeg(k), 0);
}

bool knob_cell_create(knob_cell_t *out, lv_obj_t *cell)
{
    if (!out || !cell) return false;
    out->cell = cell;

    /* Two circles, like the buttons: the outer one carries the broad shadow and the inner one
     * is the disc with the contact shadow and the ring. */
    out->ambient = cassette_border_box(cell, KNOB_INSET, KNOB_INSET,
                                       KNOB_D, KNOB_D, KNOB_R,
                                       k_col_bg, LV_OPA_TRANSP, k_col_bg, 0);
    out->disc = cassette_border_box(out->ambient, 0, 0,
                                    KNOB_D, KNOB_D, KNOB_R,
                                    k_col_bg, LV_OPA_TRANSP, k_col_button_circle, 2);

    int cx = CASSETTE_CELL_W / 2, cy = CASSETTE_CELL_H / 2;
    for (int i = 0; i < KNOB_CELL_DOTS; i++) {
        float a = (float)i * 15.0f * 3.14159265358979323846f / 180.0f;
        int x = cx + (int)lroundf((float)DOT_R * sinf(a));
        int y = cy - (int)lroundf((float)DOT_R * cosf(a));
        out->dot[i] = cassette_box(cell, x - DOT_D / 2, y - DOT_D / 2,
                                   DOT_D, DOT_D, DOT_D / 2, k_col_eq_dim);
    }

    /* The line is inside the disc: from its centre to the rim, its pivot at the centre, so it
     * swings like a knob's line and stops at the circle's edge. */
    out->stick = cassette_box(cell, cx - STICK_W / 2, cy - STICK_L,
                              STICK_W, STICK_L, STICK_R, k_col_eq_tick);
    lv_obj_set_style_transform_pivot_x(out->stick, STICK_W / 2, 0);
    lv_obj_set_style_transform_pivot_y(out->stick, STICK_L, 0);

    return true;
}

void knob_cell_set_page(knob_cell_t *c, bool white)
{
    if (!c || !c->cell) return;

    /* The cell becomes the plate on the white page: a fill and a dark border, so the carcase's
     * line does not run round the knob. */
    lv_obj_set_style_bg_color(c->cell, white ? k_col_knob_rect : k_col_bg, 0);
    lv_obj_set_style_border_color(c->cell, white ? k_col_bg : k_col_button_rect, 0);
    if (!c->disc) return;

    cassette_circle_face(c->ambient, c->disc, white, k_col_knob_cap);
    lv_obj_set_style_border_width(c->disc, white ? 0 : 2, 0);
    if (!white) lv_obj_set_style_border_color(c->disc, k_col_button_circle, 0);
}

lv_obj_t *knob_cell_disc(const knob_cell_t *c)
{
    return c ? c->disc : NULL;
}
