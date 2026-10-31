#pragma once
/*
 * board_shell - the Shell's SPIFFS partition, as this board mounts it.
 *
 * The page at `/` is packed into the `frontdoor` partition by the build
 * (spiffs_create_partition_image() in the root CMakeLists.txt) and mounted here at boot.
 * The mount lives beside the board adapter rather than in the portable app_board seam:
 * the host adapter has no SPIFFS partition and serves `web/dist-shell` from a directory,
 * so there is no honest portable signature for it.
 *
 * The appliance calls this after the boot mark is on the panel and before
 * app_services_start(), because the front door registers `/` inside that call.
 */
#ifdef __cplusplus
extern "C" {
#endif

/* Mount the frontdoor partition (nothing in the firmware writes it; the mount never
 * formats) and return its path, or NULL when this firmware carries no page: no
 * partition in the table, a partition that was never flashed, or a mount failure. Each
 * reason is logged once and is never
 * fatal - the front door then answers 503 at `/` and every other route keeps working.
 * The call is made once per boot; a repeat returns the same answer without remounting.
 * The returned path is a string literal and stays valid for the life of the process. */
const char *app_board_shell_mount(void);

#ifdef __cplusplus
}
#endif
