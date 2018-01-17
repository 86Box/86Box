/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of various Compaq PC's.
 *
 * Version:	@(#)m_at_compaq.c	1.0.3	2018/01/16
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../cpu/cpu.h"
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "../disk/hdc.h"
#include "../disk/hdc_ide.h"
#include "machine.h"


/* Compaq Deskpro 386 remaps RAM from 0xA0000-0xFFFFF to 0xFA0000-0xFFFFFF */
static mem_mapping_t	ram_mapping;


static uint8_t
read_ram(uint32_t addr, void *priv)
{
    addr = (addr & 0x7ffff) + 0x80000;
    addreadlookup(mem_logical_addr, addr);

    return(ram[addr]);
}


static uint16_t
read_ramw(uint32_t addr, void *priv)
{
    addr = (addr & 0x7ffff) + 0x80000;
    addreadlookup(mem_logical_addr, addr);

    return(*(uint16_t *)&ram[addr]);
}


static uint32_t
read_raml(uint32_t addr, void *priv)
{
    addr = (addr & 0x7ffff) + 0x80000;
    addreadlookup(mem_logical_addr, addr);

    return(*(uint32_t *)&ram[addr]);
}


static void
write_ram(uint32_t addr, uint8_t val, void *priv)
{
    addr = (addr & 0x7ffff) + 0x80000;
    addwritelookup(mem_logical_addr, addr);

    mem_write_ramb_page(addr, val, &pages[addr >> 12]);
}


static void
write_ramw(uint32_t addr, uint16_t val, void *priv)
{
    addr = (addr & 0x7ffff) + 0x80000;
    addwritelookup(mem_logical_addr, addr);

    mem_write_ramw_page(addr, val, &pages[addr >> 12]);
}


static void
write_raml(uint32_t addr, uint32_t val, void *priv)
{
    addr = (addr & 0x7ffff) + 0x80000;
    addwritelookup(mem_logical_addr, addr);

    mem_write_raml_page(addr, val, &pages[addr >> 12]);
}


void
machine_at_compaq_init(machine_t *model)
{
    machine_at_top_remap_init(model);
    device_add(&fdc_at_device);

    mem_mapping_add(&ram_mapping, 0xfa0000, 0x60000,
                    read_ram, read_ramw, read_raml,
                    write_ram, write_ramw, write_raml,
                    0xa0000+ram, MEM_MAPPING_INTERNAL, NULL);

    switch(model->id) {
#ifdef PORTABLE3
	case ROM_DESKPRO_386:
		if (hdc_current == 1)
			ide_init();
		break;
#endif

	case ROM_PORTABLE:
		break;

	case ROM_PORTABLEII:
		break;

#ifdef PORTABLE3
	case ROM_PORTABLEIII:
		machine_olim24_video_init();
		break;

	case ROM_PORTABLEIII386:
		machine_olim24_video_init();
		if (hdc_current == 1)
			ide_init();
		break;
#endif
    }
}
