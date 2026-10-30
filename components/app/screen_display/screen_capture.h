#pragma once
/*
 * One complete copy of the screen, for the HTTP diagnostics.
 *
 * The two boards keep their picture in different places: the panel board's frame is the RGB
 * panel's own PSRAM buffer, which LVGL draws into in direct mode, and the headless board's is
 * the full-frame buffer this component allocated for the console dump. Neither is a thing the
 * endpoint should know about, so the board selection stays in screen_display.c - the file that
 * already chooses the adapter - and this is the one call the diagnostics make.
 *
 * The copy is PSRAM, owned by the caller, and taken under whatever lock the board has (the
 * port's on the panel; none on the headless path, where the screen task is the only LVGL
 * user). NULL when the screen is not up yet or PSRAM is short.
 */
#include <stdbool.h>
#include <stdint.h>

/* Width and height in pixels are written to `w` and `h` when the copy is returned; both are
 * left at 0 otherwise. */
uint16_t *screen_capture_frame(int *w, int *h);

/* The screen's lock, when this board has one: the port's on the panel board, and nothing on
 * the headless one, where the screen task is the only LVGL user and a diagnostic's few ints
 * are not worth a mutex it does not otherwise own. Paired around anything that reaches the
 * view from another task - the scene route does. Both return/succeed so a caller can write
 * `if (!lock) return 503;`. */
bool screen_capture_lock(void);
void screen_capture_unlock(void);
