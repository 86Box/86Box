/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Header for the implementation of Port 6x used by various
 *		machines.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2021 Miran Grca.
 */

#ifndef EMU_PORT_6X_H
# define EMU_PORT_6X_H

#ifdef _TIMER_H_
typedef struct
{
    uint8_t	refresh, flags;

    pc_timer_t	refresh_timer;
} port_6x_t;
#endif


extern const device_t	port_6x_device;
extern const device_t	port_6x_xi8088_device;
extern const device_t	port_6x_ps2_device;
extern const device_t	port_6x_olivetti_device;


#endif	/*EMU_PORT_6X_H*/
