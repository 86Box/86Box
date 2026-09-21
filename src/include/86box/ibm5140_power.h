/* IBM PC Convertible system-board power, RTC and keyboard. */
#ifndef EMU_IBM5140_POWER_H
#define EMU_IBM5140_POWER_H

#include <stdint.h>
#include <86box/vid_ibm5140.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ibm5140_power_t ibm5140_power_t;

ibm5140_power_t *ibm5140_power_create(ibm5140_video_t *video);
void ibm5140_power_nmi(ibm5140_power_t *dev, uint8_t cause, int asserted);
int ibm5140_power_is_off(void);
/* Call before a host hard reset/power loss, before the old machine's NVR save. */
void ibm5140_power_hard_off(void);
/* Independent supply inputs; losing SRAM retention is not losing RTC power. */
void ibm5140_power_set_supply(ibm5140_power_t *dev, int external, int low_battery,
                             int rtc_valid, int sram_valid);

#ifdef __cplusplus
}
#endif
#endif
