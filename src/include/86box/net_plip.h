/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the PLIP parallel port network device.
 *
 *
 *
 * Author:	RichardG, <richardg867@gmail.com>
 *		Copyright 2020 RichardG.
 */

#ifndef NET_PLIP_H
# define NET_PLIP_H
# include <86box/device.h>
# include <86box/lpt.h>

extern const lpt_device_t lpt_plip_device;
extern const device_t plip_device;

#endif /*NET_PLIP_H*/
