/*
 * The Cassette screen's palette. One definition per colour; see cassette_style.h.
 */
#include "cassette_style.h"

const lv_color_t k_col_bg         = COL_HEX(COL_BLACK);
const lv_color_t k_col_white      = COL_HEX(COL_WHITE);
const lv_color_t k_col_glass      = COL_DARKEN_HEX(COL_WHITE,96);
const lv_color_t k_col_tape_dark  = COL_HEX(COL_PRIMARY);
const lv_color_t k_col_tape       = COL_HEX(COL_PRIMARY);
const lv_color_t k_col_tape_preview = COL_HEX(0xFF0000);   /* the owner's red, the preview's tape */
const lv_color_t k_col_hub_ring   = COL_DARKEN_HEX(COL_PRIMARY, 20);
const lv_color_t k_col_hub_white  = COL_HEX(0xEEEEEE);
const lv_color_t k_col_hub_ink    = COL_DARKEN_HEX(COL_PRIMARY, 80);

const lv_color_t k_col_meter_dim  = COL_HEX(0x7F2C18);
const lv_color_t k_col_meter_bar  = COL_DARKEN_HEX(COL_PRIMARY, 85);
const lv_color_t k_col_meter_lit  = COL_HEX(COL_PRIMARY);
const lv_color_t k_col_meter_back = COL_DARKEN_HEX(COL_PRIMARY, 30);
const lv_color_t k_col_meter_window = COL_HEX(COL_BLACK);

const lv_color_t k_col_eq_dim  = COL_DARKEN_HEX(COL_PRIMARY, 38);
const lv_color_t k_col_eq_lit  = COL_HEX(COL_PRIMARY);
const lv_color_t k_col_eq_tick = COL_HEX(0xFF8A3D);

const lv_color_t k_col_frame      = COL_HEX(COL_BLACK);

/* The cassettes: plastic, then the ink of the label printed on it. Five to start with - a row
 * per shell colour, each with a label that reads against it - and adding one is adding a row
 * (ADR 0040). The Track's path picks the row; nothing else decides. */
const cassette_ink_t k_col_cassette[] = {
    { COL_HEX(0xCC0000), COL_HEX(0x363636), COL_HEX(0xF5F5F5) },   /* red, charcoal ink, white label */
    { COL_HEX(0x39FF14), COL_HEX(0x363636), COL_HEX(0xF5F5F5) },   /* lime, charcoal ink, white label */
    { COL_HEX(0x002366), COL_HEX(0x363636), COL_HEX(0xF5F5F5) },   /* royal blue, charcoal ink, white label */
    { COL_HEX(0x0F52BA), COL_HEX(0x363636), COL_HEX(0xF5F5F5) },   /* sapphire, charcoal ink, white label */
    { COL_HEX(0x363636), COL_HEX(0x363636), COL_HEX(0xDCD0BA) },   /* smoke, charcoal ink, beige label */
    { COL_HEX(0xF5F5F5), COL_HEX(0x363636), COL_HEX(0xDCD0BA) },   /* white, charcoal ink, beige label */
    { COL_HEX(0xDCD0BA), COL_HEX(0x363636), COL_HEX(0xF5F5F5) },   /* beige, charcoal ink, white label */
};

const int k_col_cassette_count = (int)(sizeof k_col_cassette / sizeof k_col_cassette[0]);

const lv_color_t k_col_back       = COL_DARKEN_HEX(COL_PRIMARY, 40);
const lv_color_t k_col_cover_bg   = COL_HEX(0xB48744);
const lv_color_t k_col_cover_ink  = COL_HEX(0xEEEEEE);
const lv_color_t k_col_art_bg     = COL_HEX(0x141414);
const lv_color_t k_col_paper      = COL_HEX(COL_BLACK);
const lv_color_t k_col_primary    = COL_HEX(COL_PRIMARY);
const lv_color_t k_col_button_rect    = COL_DARKEN_HEX(COL_PRIMARY, 40);
const lv_color_t k_col_button_cap     = COL_HEX(0xAAAAAA);
const lv_color_t k_col_button_circle    = COL_HEX(COL_PRIMARY);
const lv_color_t k_col_button_letter    = COL_HEX(COL_PRIMARY);
const lv_color_t k_col_dome        = COL_HEX(0x929292);
const lv_color_t k_col_button_white  = COL_HEX(0xEEEEEE);
const lv_color_t k_col_knob_cap       = COL_HEX(0x383838);
const lv_color_t k_col_knob_rect      = COL_HEX(0x727272);
const lv_color_t k_col_logo_back      = COL_DARKEN_HEX(COL_PRIMARY, 85);
