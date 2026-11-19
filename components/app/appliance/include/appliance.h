#pragma once
/*
 * appliance - the device's entry policy: NVS, the station configuration, the card
 * retry and the console line.
 *
 * One call and the device is serving. The services and their load-bearing order are
 * components/portable/app_services; the screen is components/app/screen_display. The
 * appliance is the policy above both, and the only place the station format is chosen.
 */
#ifdef __cplusplus
extern "C" {
#endif

/* Run the appliance. It does not return: the console loop is the last thing between boot
 * and the device's life. A station that cannot be served is logged once and left idle
 * rather than rebooted, so the diagnosis survives. */
void appliance_run(void);

#ifdef __cplusplus
}
#endif
