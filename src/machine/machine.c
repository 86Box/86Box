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
 * Version:	@(#)machine.c	1.0.33	2018/03/26
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../device.h"
#include "../dma.h"
#include "../pic.h"
#include "../pit.h"
#include "../mem.h"
#include "../rom.h"
#include "../lpt.h"
#include "../serial.h"
#include "../cpu/cpu.h"
#include "machine.h"


int machine;
int AT, PCI;
int romset;


void
machine_init(void)
{
    pclog("Initializing as \"%s\"\n", machine_getname());

    /* Set up the architecture flags. */
    AT = IS_ARCH(machine, MACHINE_AT);
    PCI = IS_ARCH(machine, MACHINE_PCI);

    /* Resize the memory. */
    mem_reset();

    /* Load the machine's ROM BIOS. */
    rom_load_bios(romset);
    mem_add_bios();

    /* All good, boot the machine! */
    machines[machine].init(&machines[machine]);
}


void
machine_common_init(const machine_t *model)
{
    /* System devices first. */
    pic_init();
    dma_init();
    pit_init();

    cpu_set();
    if (AT)
	setrtcconst(machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].rspeed);
    else
	setrtcconst(14318184.0);

    if (lpt_enabled)
	lpt_init();

    if (serial_enabled[0])
	serial_setup(1, SERIAL1_ADDR, SERIAL1_IRQ);

    if (serial_enabled[1])
	serial_setup(2, SERIAL2_ADDR, SERIAL2_IRQ);
}
