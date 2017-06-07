/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of Serial Mouse devices.
 *
 *		Definitions for the Serial Mouse driver.
 *
 * Version:	@(#)mouse_serial.h	1.0.2	2017/05/06
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 */
#ifndef MOUSE_SERIAL_H
# define MOUSE_SERIAL_H


#define SERMOUSE_PORT		1		/* attach to Serial1 */


extern mouse_t	mouse_serial_microsoft;
extern mouse_t	mouse_msystems;


#endif	/*MOUSE_SERIAL_H*/
