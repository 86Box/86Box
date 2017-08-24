/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the memory I/O scratch registers on ports 0xE1
 *		and 0xE2, used by just about any emulated machine.
 *
 * Version:	@(#)memregs.h	1.0.1	2017/08/23
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2016,2017 Miran Grca.
 */
#ifndef EMU_MEMREGS_H
# define EMU_MEMREGS_H


extern void powermate_memregs_init(void);
extern void memregs_init(void);


#endif	/*EMU_MEMREGS_H*/
