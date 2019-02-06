/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of various Zenith PC compatible machines.
 *		Currently only the Zenith Data Systems Supersport is emulated.
 *
 * Version:	@(#)m_xt_compaq.c	1.0.0	2019/01/13
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../86box.h"
#include "../cpu/cpu.h"
#include "../nmi.h"
#include "../pit.h"
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "../game/gameport.h"
#include "../keyboard.h"
#include "../lpt.h"
#include "machine.h"

typedef struct {
    mem_mapping_t scratchpad_mapping;
    uint8_t *scratchpad_ram;
} zenith_t;

static uint8_t zenith_scratchpad_read(uint32_t addr, void *p)
{
	zenith_t *dev = (zenith_t *)p;
        return dev->scratchpad_ram[addr & 0x3fff];
}
static void zenith_scratchpad_write(uint32_t addr, uint8_t val, void *p)
{
	zenith_t *dev = (zenith_t *)p;
        dev->scratchpad_ram[addr & 0x3fff] = val;
}

static void *
zenith_scratchpad_init(const device_t *info)
{
    zenith_t *dev;

    dev = (zenith_t *)malloc(sizeof(zenith_t));
    memset(dev, 0x00, sizeof(zenith_t));	
	
    dev->scratchpad_ram = malloc(0x4000);
    
    mem_mapping_disable(&bios_mapping[4]);
    mem_mapping_disable(&bios_mapping[5]);

    mem_mapping_add(&dev->scratchpad_mapping, 0xf0000, 0x4000,
			zenith_scratchpad_read, NULL, NULL,
			zenith_scratchpad_write, NULL, NULL,
			dev->scratchpad_ram,  MEM_MAPPING_EXTERNAL, dev);
			
    return dev;
}

static void 
zenith_scratchpad_close(void *p)
{
        zenith_t *dev = (zenith_t *)p;

	free(dev->scratchpad_ram);
        free(dev);
}


static const device_t zenith_scratchpad_device = {
    "Zenith scratchpad RAM",
    0, 0,
    zenith_scratchpad_init, zenith_scratchpad_close, NULL,
    NULL,
    NULL,
    NULL
};

void
machine_xt_zenith_init(const machine_t *model)
{		
    machine_common_init(model);

    lpt2_remove(); /* only one parallel port */    
    
    device_add(&zenith_scratchpad_device);
    
    pit_set_out_func(&pit, 1, pit_refresh_timer_xt);

    device_add(&keyboard_xt_device);
    device_add(&fdc_xt_device);
    nmi_init();
    if (joystick_type != 7)
	device_add(&gameport_device);
}