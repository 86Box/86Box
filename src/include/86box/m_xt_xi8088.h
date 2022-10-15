#ifndef MACHINE_XI80888_H
#define MACHINE_XI80888_H

#include <86box/device.h>

extern const device_t xi8088_device;

uint8_t xi8088_turbo_get();
void    xi8088_turbo_set(uint8_t value);
void    xi8088_bios_128kb_set(int val);
int     xi8088_bios_128kb();

#endif /*MACHINE_XI80888_H*/
