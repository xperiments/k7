#pragma once
/*
 * board_shell - the Shell's image, as this board makes it resident.
 *
 * The page at `/` is packed into the `frontdoor` partition by the build
 * (tools/pack_shell.py, and esptool_py_flash_to_partition in the root CMakeLists.txt), and
 * read out of it ONCE at boot into PSRAM. The reason is the panel: any flash access takes the
 * bus its scan-out draws from - a partition read disables the cache and the PSRAM window with
 * it, and even a mapped read is a cache line fetch on the same controller - so the page's
 * bytes cannot come from flash while the screen is drawing (ADR 0055).
 *
 * That is why this lives beside the board adapter rather than in the portable app_board seam:
 * the host has no partition to read - it hands the front door the same image from a buffer -
 * so there is no honest portable signature for it.
 *
 * The appliance calls this BEFORE screen_display_boot(), while nothing is scanning, so the
 * copy's burst of flash reads costs nothing visible; and before app_services_start(), because
 * the front door registers `/` inside that call.
 */
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Read the `frontdoor` partition's image into PSRAM and hand back its bytes. Returns false
 * when this firmware carries no page: no partition in the table, an image that was never
 * flashed or does not validate, or no PSRAM to hold it. There is no halfway house - serving
 * the image from flash would tear the picture - so each refusal is logged once, and none is
 * fatal: the front door then answers 503 at `/` and every other route keeps working.
 *
 * The call is made once per boot; a repeat returns the same answer without reading again.
 * The bytes stay valid for the life of the process. */
bool app_board_shell_image(const void **bytes, size_t *size);

#ifdef __cplusplus
}
#endif
