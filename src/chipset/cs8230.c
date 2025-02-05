/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of C&T CS8230 ("386/AT") chipset.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *
 *          Copyright 2020 Sarah Walker.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/plat_unused.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/chipset.h>

typedef struct cs8230_t {
    int     idx;
    uint8_t regs[256];
} cs8230_t;

static void
shadow_control(uint32_t addr, uint32_t size, int state)
{
    switch (state) {
        case 0x00:
            mem_set_mem_state(addr, size, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
            break;
        case 0x01:
            mem_set_mem_state(addr, size, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
            break;
        case 0x10:
            mem_set_mem_state(addr, size, MEM_READ_INTERNAL | MEM_WRITE_EXTANY);
            break;
        case 0x11:
            mem_set_mem_state(addr, size, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
            break;
        default:
            break;
    }

    flushmmucache_nopc();
}

static void
rethink_shadow_mappings(cs8230_t *cs8230)
{
    for (uint8_t c = 0; c < 32; c++) {
        /* Addresses 40000-bffff in 16k blocks */
        if (cs8230->regs[0xa + (c >> 3)] & (1 << (c & 7)))
            mem_set_mem_state(0x40000 + (c << 14), 0x4000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL); /* I/O channel */
        else
            mem_set_mem_state(0x40000 + (c << 14), 0x4000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL); /* System board */
    }

    for (uint8_t c = 0; c < 16; c++) {
        /* Addresses c0000-fffff in 16k blocks. System board ROM can be mapped here */
        if (cs8230->regs[0xe + (c >> 3)] & (1 << (c & 7)))
            mem_set_mem_state(0xc0000 + (c << 14), 0x4000, MEM_READ_EXTANY | MEM_WRITE_EXTANY); /* I/O channel */
        else
            shadow_control(0xc0000 + (c << 14), 0x4000, (cs8230->regs[9] >> (3 - (c >> 2))) & 0x11);
    }
}

static uint8_t
cs8230_read(uint16_t port, void *priv)
{
    const cs8230_t *cs8230 = (cs8230_t *) priv;
    uint8_t         ret    = 0xff;

    if (port & 1) {
        switch (cs8230->idx) {
            case 0x04: /* 82C301 ID/version */
                ret = cs8230->regs[cs8230->idx] & ~0xe3;
                break;

            case 0x08: /* 82C302 ID/Version */
                ret = cs8230->regs[cs8230->idx] & ~0xe0;
                break;

            case 0x05:
            case 0x06: /* 82C301 registers */
            case 0x09:
            case 0x0a:
            case 0x0b:
            case 0x0c: /* 82C302 registers */
            case 0x0d:
            case 0x0e:
            case 0x0f:
            case 0x10:
            case 0x11:
            case 0x12:
            case 0x13:
            case 0x28:
            case 0x29:
            case 0x2a:
                ret = cs8230->regs[cs8230->idx];
                break;

            default:
                break;
        }
    }

    return ret;
}

static void
cs8230_write(uint16_t port, uint8_t val, void *priv)
{
    cs8230_t *cs8230 = (cs8230_t *) priv;

    if (!(port & 1))
        cs8230->idx = val;
    else {
        cs8230->regs[cs8230->idx] = val;
        switch (cs8230->idx) {
            case 0x09: /* RAM/ROM Configuration in boot area */
            case 0x0a:
            case 0x0b:
            case 0x0c:
            case 0x0d:
            case 0x0e:
            case 0x0f: /* Address maps */
                rethink_shadow_mappings(cs8230);
                break;
            default:
                break;
        }
    }
}

static void
cs8230_close(void *priv)
{
    cs8230_t *cs8230 = (cs8230_t *) priv;

    free(cs8230);
}

static void *
cs8230_init(UNUSED(const device_t *info))
{
    cs8230_t *cs8230 = (cs8230_t *) calloc(1, sizeof(cs8230_t));

    io_sethandler(0x0022, 0x0002, cs8230_read, NULL, NULL, cs8230_write, NULL, NULL, cs8230);

    if (mem_size > 768) {
        mem_mapping_set_addr(&ram_mid_mapping, 0xa0000, mem_size > 1024 ? 0x60000 : 0x20000 + (mem_size - 768) * 1024);
        mem_mapping_set_exec(&ram_mid_mapping, ram + 0xa0000);
    }

    return cs8230;
}

const device_t cs8230_device = {
    .name          = "C&T CS8230 (386/AT)",
    .internal_name = "cs8230",
    .flags         = 0,
    .local         = 0,
    .init          = cs8230_init,
    .close         = cs8230_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
