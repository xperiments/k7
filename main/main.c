/*
 * The entry point. The device's policy lives in components/app/appliance; the board
 * adapter in components/app/board_esp32.
 *
 * Edit the SD pins for your board, and put your network in
 * components/app/board_esp32/wifi_credentials.h (copied from the .example file,
 * gitignored). Everything else has defaults that work.
 *
 * Two things this firmware does that a deployer must know (README, deployment):
 *   - the Wi-Fi credentials are compiled into the binary;
 *   - the root address and /music/<name> are open to anyone who can reach the
 *     port, with no authentication.
 * Do not put it on an untrusted network or forward its port.
 */
#include "appliance.h"

void app_main(void)
{
    appliance_run();
}
