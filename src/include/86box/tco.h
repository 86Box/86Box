/*
 * Intel TCO Header
 *
 * Authors:	Tiseno100,
 *
 * Copyright 2022 Tiseno100.
 */

#ifndef EMU_TCO_H
# define EMU_TCO_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint8_t regs[17];
    uint16_t tco_irq;
    pc_timer_t *tco_timer;
} tco_t;

extern const device_t   tco_device;

extern void tco_irq_update(tco_t *dev, uint16_t new_irq);
extern void tco_write(uint16_t addr, uint8_t val, tco_t *dev);
extern uint8_t tco_read(uint16_t addr, tco_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* EMU_TCO_H */
