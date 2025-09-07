/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the ACPI emulation.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020-2025 Miran Grca.
 */
#ifndef ACCESS_BUS_H
#define ACCESS_BUS_H

#define AB_RST 0x80

typedef struct access_bus_t {
  uint8_t  control;
  uint8_t  status;
  uint8_t  own_addr;
  uint8_t  data;
  uint8_t  clock;
  uint8_t  enable;
  uint16_t base;
} access_bus_t;

extern const device_t access_bus_device;

/* Functions */
extern void    access_bus_handler(access_bus_t *dev, uint8_t enable, uint16_t base);

#endif /*ACCESS_BUS_H*/
