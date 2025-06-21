/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of various Compaq PC's.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          TheCollector1995, <mariogplayer@gmail.com>
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <math.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/keyboard.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/machine.h>
#include <86box/video.h>
#include <86box/vid_cga.h>
#include <86box/vid_cga_comp.h>
#include <86box/plat_unused.h>

enum {
    COMPAQ_PORTABLEII = 0,
    COMPAQ_PORTABLEIII,
    COMPAQ_PORTABLEIII386,
    COMPAQ_DESKPRO386,
    COMPAQ_DESKPRO386_05_1988
};

static int compaq_machine_type = 0;

/* Compaq Deskpro 386 remaps RAM from 0xA0000-0xFFFFF to 0xFA0000-0xFFFFFF */
static mem_mapping_t ram_mapping;


static uint8_t
read_ram(uint32_t addr, UNUSED(void *priv))
{
    addr = (addr & 0x7ffff) + 0x80000;
    addreadlookup(mem_logical_addr, addr);

    return (ram[addr]);
}

static uint16_t
read_ramw(uint32_t addr, UNUSED(void *priv))
{
    addr = (addr & 0x7ffff) + 0x80000;
    addreadlookup(mem_logical_addr, addr);

    return (*(uint16_t *) &ram[addr]);
}

static uint32_t
read_raml(uint32_t addr, UNUSED(void *priv))
{
    addr = (addr & 0x7ffff) + 0x80000;
    addreadlookup(mem_logical_addr, addr);

    return (*(uint32_t *) &ram[addr]);
}

static void
write_ram(uint32_t addr, uint8_t val, UNUSED(void *priv))
{
    addr = (addr & 0x7ffff) + 0x80000;
    addwritelookup(mem_logical_addr, addr);

    mem_write_ramb_page(addr, val, &pages[addr >> 12]);
}

static void
write_ramw(uint32_t addr, uint16_t val, UNUSED(void *priv))
{
    addr = (addr & 0x7ffff) + 0x80000;
    addwritelookup(mem_logical_addr, addr);

    mem_write_ramw_page(addr, val, &pages[addr >> 12]);
}

static void
write_raml(uint32_t addr, uint32_t val, UNUSED(void *priv))
{
    addr = (addr & 0x7ffff) + 0x80000;
    addwritelookup(mem_logical_addr, addr);

    mem_write_raml_page(addr, val, &pages[addr >> 12]);
}

static void
machine_at_compaq_init(const machine_t *model, int type)
{
    compaq_machine_type = type;

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    if (type < COMPAQ_PORTABLEIII386) {
        mem_remap_top(384);
        mem_mapping_add(&ram_mapping, 0xfa0000, 0x60000,
                        read_ram, read_ramw, read_raml,
                        write_ram, write_ramw, write_raml,
                        0xa0000 + ram, MEM_MAPPING_INTERNAL, NULL);
    }

    video_reset(gfxcard[0]);

    switch (type) {
        case COMPAQ_PORTABLEII:
            machine_at_common_init(model);
            device_add(&keyboard_at_compaq_device);
            break;

        case COMPAQ_PORTABLEIII:
            if (hdc_current[0] == HDC_INTERNAL)
                device_add(&ide_isa_device);
            if (gfxcard[0] == VID_INTERNAL)
                device_add(&compaq_plasma_device);

            machine_at_common_init(model);
            device_add(&keyboard_at_compaq_device);
            break;

        case COMPAQ_PORTABLEIII386:
            if (hdc_current[0] == HDC_INTERNAL)
                device_add(&ide_isa_device);
            if (gfxcard[0] == VID_INTERNAL)
                device_add(&compaq_plasma_device);
            device_add(&compaq_386_device);
            machine_at_common_init(model);
            device_add(&keyboard_at_compaq_device);
            break;

        case COMPAQ_DESKPRO386:
        case COMPAQ_DESKPRO386_05_1988:
            device_add(&compaq_386_device);
            machine_at_common_init(model);
            device_add(&keyboard_at_compaq_device);
            break;

        default:
            break;
    }
}

int
machine_at_portableii_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleavedr("roms/machines/portableii/109740-001.rom",
                                 "roms/machines/portableii/109739-001.rom",
                                 0x000f8000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_compaq_init(model, COMPAQ_PORTABLEII);

    return ret;
}

int
machine_at_portableiii_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linearr("roms/machines/portableiii/K Combined.bin",
                                0x000f8000, 65536, 0);


    if (bios_only || !ret)
        return ret;

    machine_at_compaq_init(model, COMPAQ_PORTABLEIII);

    return ret;
}

int
machine_at_portableiii386_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linearr("roms/machines/portableiii/P.2 Combined.bin",
                                0x000f0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_compaq_init(model, COMPAQ_PORTABLEIII386);

    return ret;
}

int
machine_at_deskpro386_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linearr("roms/machines/deskpro386/1986-09-04-HI.json.bin",
                            0x000f8000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_compaq_init(model, COMPAQ_DESKPRO386);

    return ret;
}

int
machine_at_deskpro386_05_1988_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linearr("roms/machines/deskpro386/1988-05-10.json.bin",
                            0x000f8000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_compaq_init(model, COMPAQ_DESKPRO386_05_1988);

    return ret;
}
