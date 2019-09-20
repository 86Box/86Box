/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Handling of the emulated machines.
 *
 * Version:	@(#)machine.c	1.0.37	2018/11/12
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../device.h"
#include "../timer.h"
#include "../dma.h"
#include "../pic.h"
#include "../pit.h"
#include "../mem.h"
#include "../rom.h"
#include "../lpt.h"
#include "../serial.h"
#include "../cpu/cpu.h"
#include "../video/video.h"
#include "machine.h"


int bios_only = 0;
int machine;
int AT, PCI;


#ifdef ENABLE_MACHINE_LOG
int machine_do_log = ENABLE_MACHINE_LOG;


static void
machine_log(const char *fmt, ...)
{
   va_list ap;

   if (machine_do_log)
   {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
   }
}
#else
#define machine_log(fmt, ...)
#endif


static int
machine_init_ex(int m)
{
    int ret = 0;

    if (!bios_only) {
	machine_log("Initializing as \"%s\"\n", machine_getname_ex(m));

	/* Set up the architecture flags. */
	AT = IS_ARCH(machine, MACHINE_AT);
	PCI = IS_ARCH(machine, MACHINE_PCI);

	/* Resize the memory. */
	mem_reset();

	lpt_init();
    }

    /* All good, boot the machine! */
    if (machines[m].init)
	ret = machines[m].init(&machines[m]);

    if (bios_only || !ret)
	return ret;

    /* Reset the graphics card (or do nothing if it was already done
       by the machine's init function). */
    video_reset(gfxcard);

    return ret;
}


void
machine_init(void)
{
    bios_only = 0;
    (void) machine_init_ex(machine);
}


int
machine_available(int m)
{
    int ret;

    bios_only = 1;
    ret = machine_init_ex(m);

    bios_only = 0;
    return ret;
}


void
machine_common_init(const machine_t *model)
{
    /* System devices first. */
    pic_init();
    dma_init();

    cpu_set();

    pit_init();
}
