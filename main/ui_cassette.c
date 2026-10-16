/*
 * The cassette screen.
 *
 * The artwork is two pieces: the shell (assets/cs_front.png) and one reel hub
 * (assets/cs_wheel.png). The hubs go *behind* the shell, centred on the two reel
 * windows the shell's alpha channel cuts, so they show through the windows and turn -
 * which is what a cassette does. Every number below was measured from the artwork by
 * tools/convert_cassette_assets.py rather than eyeballed, so re-exporting the art and
 * re-running that tool keeps the hubs in their windows.
 *
 * Layer order is the whole trick: children of a container are drawn in the order they
 * were created, so the two hubs are created first and the shell last. Creating the
 * shell first would hide them.
 */
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

#include "ui.h"

/* Measured by tools/convert_cassette_assets.py: the shell scaled to leave a band for
 * the file's details, and the centres of the two reel windows in panel coordinates. */
#define FRONT_W 618
#define FRONT_H 400
#define FRONT_X 91
#define FRONT_Y 14
#define WHEEL_PX 135
static const struct { int x, y; } k_window[2] = { { 272, 197 }, { 524, 197 } };

/* The band below the cassette. */
#define BAND_Y 424
#define BAND_H 52
#define BAND_X FRONT_X
#define BAND_W FRONT_W

/* The shell's own label strip, which the title is written over. Sampled from the art:
 * covering the printed placeholder with its own paper colour keeps the label looking
 * printed rather than pasted. */
#define LABEL_X 244
#define LABEL_Y 76
#define LABEL_W 372
#define LABEL_H 30
#define LABEL_COLOR lv_color_hex(0xf2f1ee)

/* The artwork, embedded by EMBED_FILES in main/CMakeLists.txt. RGB565A8: LVGL's
 * two-plane format, colour first then alpha, which is what the converter writes. */
extern const uint8_t cs_front_bin_start[] asm("_binary_cs_front_bin_start");
extern const uint8_t cs_wheel_bin_start[] asm("_binary_cs_wheel_bin_start");

static const lv_image_dsc_t k_front = {
    .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565A8,
                .w = FRONT_W, .h = FRONT_H, .stride = FRONT_W * 2 },
    .data_size = (uint32_t)(FRONT_W * FRONT_H * 3),
    .data = cs_front_bin_start,
};
static const lv_image_dsc_t k_wheel = {
    .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565A8,
                .w = WHEEL_PX, .h = WHEEL_PX, .stride = WHEEL_PX * 2 },
    .data_size = (uint32_t)(WHEEL_PX * WHEEL_PX * 3),
    .data = cs_wheel_bin_start,
};

static lv_obj_t *s_wheel[2];
static lv_obj_t *s_title, *s_file, *s_facts, *s_time, *s_bar;
static int       s_angle[2];           /* tenths of a degree, so the reels can crawl */
static uint32_t  s_last_ms;

/* ------------------------------------------------------------------ helpers */

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, lv_color_t colour, lv_align_t align,
                      int x, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, colour, 0);
    lv_obj_align(l, align, x, y);
    return l;
}

static void fmt_time(char *out, size_t n, uint32_t ms)
{
    if (!ms) { snprintf(out, n, "--:--"); return; }
    snprintf(out, n, "%u:%02u", (unsigned)(ms / 60000), (unsigned)((ms / 1000) % 60));
}

static const char *fmt_name(mi_fmt_t f)
{
    switch (f) {
    case MI_FMT_MP3: return "MP3";
    case MI_FMT_FLAC: return "FLAC";
    case MI_FMT_WAV: return "WAV";
    default: return "--";
    }
}

/* ------------------------------------------------------------------- build */

void ui_cassette_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101216), 0);
    lv_obj_set_scrollable(scr, false);

    /* The hubs, then the shell over them. */
    for (int i = 0; i < 2; i++) {
        lv_obj_t *w = lv_image_create(scr);
        lv_image_set_src(w, &k_wheel);
        lv_obj_set_pos(w, k_window[i].x - WHEEL_PX / 2, k_window[i].y - WHEEL_PX / 2);
        lv_image_set_pivot(w, WHEEL_PX / 2, WHEEL_PX / 2);
        lv_image_set_antialias(w, true);
        s_wheel[i] = w;
    }
    lv_obj_t *front = lv_image_create(scr);
    lv_image_set_src(front, &k_front);
    lv_obj_set_pos(front, FRONT_X, FRONT_Y);

    /* The title, written on the shell's own label strip. */
    lv_obj_t *paper = lv_obj_create(scr);
    lv_obj_remove_style_all(paper);
    lv_obj_set_size(paper, LABEL_W, LABEL_H);
    lv_obj_set_pos(paper, LABEL_X, LABEL_Y);
    lv_obj_set_style_bg_color(paper, LABEL_COLOR, 0);
    lv_obj_set_style_bg_opa(paper, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(paper, false);
    s_title = text(scr, &lv_font_montserrat_22, lv_color_hex(0x1b2733), LV_ALIGN_TOP_LEFT,
                   LABEL_X + 6, LABEL_Y + 3);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_title, LABEL_W - 12);

    /* The band: what the file is, and how far into it we are. */
    s_file = text(scr, &lv_font_montserrat_18, lv_color_hex(0xf0f2f5), LV_ALIGN_TOP_LEFT, BAND_X, BAND_Y);
    lv_label_set_long_mode(s_file, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_file, BAND_W - 150);
    s_facts = text(scr, &lv_font_montserrat_14, lv_color_hex(0x8f98a6), LV_ALIGN_TOP_RIGHT,
                   -(800 - BAND_X - BAND_W), BAND_Y + 4);

    s_bar = lv_bar_create(scr);
    lv_obj_set_size(s_bar, BAND_W - 96, 10);
    lv_obj_set_pos(s_bar, BAND_X, BAND_Y + 30);
    lv_bar_set_range(s_bar, 0, 1000);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x2a2f38), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0xd8a24a), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 5, LV_PART_INDICATOR);
    s_time = text(scr, &lv_font_montserrat_14, lv_color_hex(0x8f98a6), LV_ALIGN_TOP_RIGHT,
                  -(800 - BAND_X - BAND_W), BAND_Y + 28);

    ui_cassette_update(NULL);
}

/* ------------------------------------------------------------------ update */

void ui_cassette_update(const mini_icecast_status_t *st)
{
    static mini_icecast_status_t last;
    static bool have;

    if (st) last = *st;
    else { memset(&last, 0, sizeof last); last.format = MI_FMT_NONE; }
    const mini_icecast_status_t *s = &last;

    /* Text only when it changed: a label that is rewritten every 20 ms would restart
     * its scroll and re-invalidate the whole band, which costs more than it shows. */
    static char shown_file[MINI_ICECAST_STATUS_FILE_MAX], shown_title[MINI_ICECAST_STATUS_TITLE_MAX];
    const char *title = s->title[0] ? s->title : "no card, or nothing to play";
    if (!have || strcmp(shown_title, title) != 0) {
        lv_label_set_text(s_title, title);
        snprintf(shown_title, sizeof shown_title, "%.120s", title);
    }
    if (!have || strcmp(shown_file, s->file) != 0) {
        lv_label_set_text(s_file, s->file[0] ? s->file : "");
        snprintf(shown_file, sizeof shown_file, "%.150s", s->file);
    }

    char facts[96];
    if (s->format != MI_FMT_NONE)
        snprintf(facts, sizeof facts, "%s  %u Hz -> %u kHz  %u kbps  %u tracks", fmt_name(s->format),
                 (unsigned)s->source_hz, (unsigned)(s->out_hz / 1000), (unsigned)s->bitrate,
                 (unsigned)s->library_count);
    else
        snprintf(facts, sizeof facts, "--");
    lv_label_set_text(s_facts, facts);

    char elapsed[16], total[16], both[40];
    fmt_time(elapsed, sizeof elapsed, s->elapsed_ms);
    fmt_time(total, sizeof total, s->duration_ms);
    snprintf(both, sizeof both, "%s / %s", elapsed, total);
    lv_label_set_text(s_time, both);

    if (s->duration_ms) {
        uint32_t permille = (uint32_t)((uint64_t)s->elapsed_ms * 1000ULL / s->duration_ms);
        lv_bar_set_value(s_bar, permille > 1000 ? 1000 : permille, LV_ANIM_OFF);
    } else {
        lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
    }

    /* The reels turn while there is something playing, and keep the angle they are at
     * when it stops. A real cassette's hubs turn together, driven by the tape; the
     * take-up hub slows as its pack grows and the supply hub speeds up, which is what
     * the two rates below are - an approximation of that, not a simulation. */
    have = true;
    uint32_t now = lv_tick_get();
    if (!s_last_ms) s_last_ms = now;
    uint32_t dt = now - s_last_ms;
    s_last_ms = now;
    if (s->file[0] && dt < 500) {
        uint32_t permille = s->duration_ms ? (uint32_t)((uint64_t)s->elapsed_ms * 1000ULL / s->duration_ms) : 0;
        /* 0.36 deg/ms at the start of a side, 0.72 by the end (supply reel), and the
         * other way round for the take-up reel. */
        uint32_t supply = 360 + 360 * permille / 1000;
        uint32_t takeup = 720 - 360 * permille / 1000;
        s_angle[0] = (s_angle[0] + (int)(supply * dt / 10)) % 3600;
        s_angle[1] = (s_angle[1] + (int)(takeup * dt / 10)) % 3600;
        lv_image_set_rotation(s_wheel[0], s_angle[0]);
        lv_image_set_rotation(s_wheel[1], s_angle[1]);
#if CONFIG_APP_UI_SERIAL_DUMP
        /* A diagnostic build says what the reels are doing, because a still image
         * cannot show that they turn. One line a second is enough to see it. */
        static uint32_t last_log;
        if (now - last_log > 1000) {
            last_log = now;
            ESP_LOGI("ui", "reels %d.%d deg (supply), %d.%d deg (take-up), %u ms into %u ms",
                     s_angle[0] / 10, s_angle[0] % 10, s_angle[1] / 10, s_angle[1] % 10,
                     (unsigned)s->elapsed_ms, (unsigned)s->duration_ms);
        }
#endif
    }
}
