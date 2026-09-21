#ifndef EMU_M_IBM5140_H
#define EMU_M_IBM5140_H

#include <stdint.h>

void ibm5140_clock_wake(void);
void ibm5140_feature_control(uint8_t value);
void ibm5140_chipset_reset(void);

#endif
