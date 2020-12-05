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
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 *		Copyright 2017-2020 Fred N. van Kempen.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include "cpu.h"
#include <86box/video.h>
#include <86box/machine.h>


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
	machine_log("Initializing as \"%s\"\n", machine_getname());

	is_vpc = 0;

	/* Set up the architecture flags. */
	AT = IS_AT(machine);
	PCI = IS_ARCH(machine, MACHINE_BUS_PCI);

	/* Reset the memory state. */
	mem_reset();
	smbase = is_am486 ? 0x00060000 : 0x00030000;

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

    pit_common_init(!!AT, pit_irq0_timer, NULL);
}
