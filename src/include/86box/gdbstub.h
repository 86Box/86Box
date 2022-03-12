/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the GDB stub server.
 *
 *
 *
 * Authors:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2022 RichardG.
 */
#ifndef EMU_GDBSTUB_H
# define EMU_GDBSTUB_H

#ifdef USE_GDBSTUB

extern int gdbstub_singlestep;

extern void gdbstub_pause(int *p);
extern void gdbstub_init();
extern void gdbstub_close();

#else

#define gdbstub_singlestep 0

#define gdbstub_pause(p)
#define gdbstub_init()
#define gdbstub_close()

#endif

#endif
