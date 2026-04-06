/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Intel TCO Header
 *
 *
 *
 * Authors: Tiseno100,
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2022      Tiseno100.
 *          Copyright 2022-2023 Jasmine Iwanek.
 */

#ifndef EMU_TCO_H
#define EMU_TCO_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint8_t  regs[17];
    uint16_t tco_irq;
} tco_t;

extern const device_t tco_device;

extern void    tco_irq_update(tco_t *dev, uint16_t new_irq);
extern void    tco_write(uint16_t addr, uint8_t val, tco_t *dev);
extern uint8_t tco_read(uint16_t addr, tco_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* EMU_TCO_H */
