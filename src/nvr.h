/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		CMOS NVRAM emulation.
 *
 * Version:	@(#)nvr.h	1.0.0	2017/05/30
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Mahod,
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 *		Copyright 2016-2017 Mahod.
 */

void nvr_init();

extern int enable_sync;

extern int nvr_dosave;

void time_get(char *nvrram);

void nvr_recalc();

void loadnvr();
void savenvr();
