/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Header for the implementation of Port 92 used by PS/2
 *		machines and 386+ clones.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2019 Miran Grca.
 */

#ifndef EMU_PORT_92_H
#define EMU_PORT_92_H

#ifdef _TIMER_H_
typedef struct
{
    uint8_t reg, flags;

    pc_timer_t pulse_timer;

    uint64_t pulse_period;
} port_92_t;
#endif

extern void port_92_set_period(void *priv, uint64_t pulse_period);
extern void port_92_set_features(void *priv, int reset, int a20);

extern void port_92_add(void *priv);
extern void port_92_remove(void *priv);

extern const device_t port_92_device;
extern const device_t port_92_inv_device;
extern const device_t port_92_word_device;
extern const device_t port_92_pci_device;

#endif /*EMU_PORT_92_H*/
