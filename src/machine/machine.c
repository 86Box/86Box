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
 * Version:	@(#)machine.c	1.0.27	2017/11/08
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../dma.h"
#include "../pic.h"
#include "../pit.h"
#include "../mem.h"
#include "../rom.h"
#include "../lpt.h"
#include "../serial.h"
#include "../floppy/floppy.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
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

    /* Load the machine's ROM BIOS. */
    rom_load_bios(romset);
    mem_add_bios();

    /* All good, boot the machine! */
    machines[machine].init(&machines[machine]);
}


void
machine_common_init(machine_t *model)
{
    /* System devices first. */
    dma_init();
    pic_init();
    pit_init();

    if (lpt_enabled)
	lpt_init();

    if (serial_enabled[0])
	serial_setup(1, SERIAL1_ADDR, SERIAL1_IRQ);

    if (serial_enabled[1])
	serial_setup(2, SERIAL2_ADDR, SERIAL2_IRQ);

    fdc_add();
}
