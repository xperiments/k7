/*
 * The entry point. The device's policy lives in components/app/appliance; the board
 * adapter in components/app/board_esp32.
 *
 * Edit the SD pins for your board. Everything else has defaults that work, including the
 * network: none is compiled in - the device carries its own setup network, `K7.local`, and
 * the one it joins is chosen there and stored in NVS (ADR 0076, wifi-provisioning.md).
 *
 * Two things this firmware does that a deployer must know (README, deployment):
 *   - the setup page at /setup is open to anyone who can reach it, and the network's
 *     password is stored on the device in plaintext;
 *   - the root address, the stream and the control endpoints are open to anyone
 *     who can reach the port, with no authentication.
 * Do not put it on an untrusted network or forward its port.
 */
#include "appliance.h"

void app_main(void)
{
    appliance_run();
}
