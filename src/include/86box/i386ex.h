/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Intel 386EX on-chip peripherals.
 *
 * Authors: Nathan Lewis, <git@nrlewis.dev>
 *
 *          Copyright 2026 Nathan Lewis.
 */
#ifndef EMU_I386EX_H
#define EMU_I386EX_H

/* Device parameters: which on-chip UARTs the board wires up. */
#define I386EX_SIO0 0x01 /* COM1 */
#define I386EX_SIO1 0x02 /* COM2 */

typedef struct i386ex_t i386ex_t;

/*
 * Sets the frequency the board drives the COMCLK pin at, which the UARTs can
 * take their baud-rate clock from; a board can change it at any time. Until
 * it is set, the pin is not driven.
 */
extern void i386ex_set_comclk(i386ex_t *dev, double hz);

/*
 * Adds a handler in the expanded I/O space (0xF000-0xFFFF), which is only
 * decoded while REMAPCFG's ESE bit is set. This is for board logic that
 * answers there, like the parallel port pins a board reads its jumpers from.
 */
extern void i386ex_expanded_handler(i386ex_t *dev, uint16_t base, int size,
                                    uint8_t (*inb)(uint16_t addr, void *priv),
                                    uint16_t (*inw)(uint16_t addr, void *priv),
                                    uint32_t (*inl)(uint16_t addr, void *priv),
                                    void (*outb)(uint16_t addr, uint8_t val, void *priv),
                                    void (*outw)(uint16_t addr, uint16_t val, void *priv),
                                    void (*outl)(uint16_t addr, uint32_t val, void *priv),
                                    void *priv);

#ifdef EMU_DEVICE_H
extern const device_t i386ex_device;
#endif

#endif /*EMU_I386EX_H*/
