/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the Compaq 386 memory controller.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2023 Miran Grca.
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
#include <86box/chipset.h>

/* Compaq Deskpro 386 remaps RAM from 0xA0000-0xFFFFF to 0xFA0000-0xFFFFFF */
typedef struct cpq_t {
    mem_mapping_t ram_mapping;
} cpq_t;

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
compaq_close(void *priv)
{
    cpq_t *dev = (cpq_t *) priv;

    free(dev);
}

static void *
compaq_init(UNUSED(const device_t *info))
{
    cpq_t *dev = (cpq_t *) calloc(1, sizeof(cpq_t));

    mem_remap_top(384);
    mem_mapping_add(&dev->ram_mapping, 0xfa0000, 0x60000,
                    read_ram, read_ramw, read_raml,
                    write_ram, write_ramw, write_raml,
                    0xa0000 + ram, MEM_MAPPING_INTERNAL, NULL);

    return dev;
}

const device_t compaq_device = {
    .name          = "Compaq Memory Control",
    .internal_name = "compaq",
    .flags         = 0,
    .local         = 0,
    .init          = compaq_init,
    .close         = compaq_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
