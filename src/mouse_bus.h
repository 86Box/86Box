/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of Bus Mouse devices.
 *
 *		These mice devices were made by both Microsoft (InPort) and
 *		Logitech. Sadly, they did not use the same I/O protocol, but
 *		they were close enough to fit into a single implementation.
 *
 *		Definitions for the Bus Mouse driver.
 *
 * Version:	@(#)mouse_bus.h	1.0.3	2017/04/22
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 1989-2017 Fred N. van Kempen.
 */
#ifndef MOUSE_BUS_H
# define MOUSE_BUS_H


#define BUSMOUSE_PORT		0x023c
#define BUSMOUSE_PORTLEN	4
#define BUSMOUSE_IRQ		5


extern mouse_t	mouse_bus;


#endif	/*MOUSE_BUS_H*/
