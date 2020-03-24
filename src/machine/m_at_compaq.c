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
 *
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
#include "86box.h"
#include "cpu.h"
#include "timer.h"
#include "mem.h"
#include "rom.h"
#include "device.h"
#include "fdd.h"
#include "fdc.h"
#include "hdc.h"
#include "hdc_ide.h"
#include "machine.h"


enum
{
    COMPAQ_PORTABLEII = 0
#if defined(DEV_BRANCH) && defined(USE_PORTABLE3)
    , COMPAQ_PORTABLEIII,
    COMPAQ_PORTABLEIII386
#endif
};


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


static void
machine_at_compaq_init(const machine_t *model, int type)
{
    machine_at_init(model);

    mem_remap_top(384);
	
    device_add(&fdc_at_device);

    mem_mapping_add(&ram_mapping, 0xfa0000, 0x60000,
                    read_ram, read_ramw, read_raml,
                    write_ram, write_ramw, write_raml,
                    0xa0000+ram, MEM_MAPPING_INTERNAL, NULL);

    switch(type) {
	case COMPAQ_PORTABLEII:
		break;

#if defined(DEV_BRANCH) && defined(USE_PORTABLE3)
	case COMPAQ_PORTABLEIII:
		break;

	case COMPAQ_PORTABLEIII386:
		if (hdc_current == 1)
			device_add(&ide_isa_device);
		break;
#endif
    }
}


int
machine_at_portableii_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved(L"roms/machines/portableii/109740-001.rom",
				L"roms/machines/portableii/109739-001.rom",
				0x000f8000, 32768, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_compaq_init(model, COMPAQ_PORTABLEII);

    return ret;
}


#if defined(DEV_BRANCH) && defined(USE_PORTABLE3)
int
machine_at_portableiii_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved(L"roms/machines/portableiii/109738-002.bin",
				L"roms/machines/portableiii/109737-002.bin",
				0x000f0000, 65536, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_compaq_init(model, COMPAQ_PORTABLEIII);

    return ret;
}


int
machine_at_portableiii386_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved(L"roms/machines/portableiii/109738-002.bin",
				L"roms/machines/portableiii/109737-002.bin",
				0x000f0000, 65536, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_compaq_init(model, COMPAQ_PORTABLEIII386);

    return ret;
}
#endif
