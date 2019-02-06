/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		AV9194 clock generator emulation.
 *
 *		Used by the S3 86c801 (V7-Mirage) card.
 *
 * Version:	@(#)vid_av9194.c	1.0.1	2019/01/12
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 */
float av9194_getclock(int clock, void *p);

extern const device_t av9194_device;
