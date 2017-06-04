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
 * Version:	@(#)nvr.h	1.0.1	2017/06/03
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Mahod,
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 *		Copyright 2016-2017 Mahod.
 */
#ifndef EMU_NVR_H
# define EMU_NVR_H


extern int enable_sync;
extern int nvr_dosave;


extern void nvr_init(void);
extern void time_get(char *nvrram);
extern void nvr_recalc(void);
extern void loadnvr(void);
extern void savenvr(void);


#endif	/*EMU_NVR_H*/
