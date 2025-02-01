/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		DRAM row handling.
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2020 Miran Grca.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include "x86_ops.h"
#include "x86.h"
#include <86box/config.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/spd.h>
#include <86box/row.h>
#include <86box/plat_unused.h>

/*  0   1   2   3   4   5   6   7   */
static uint8_t  rows_num;
static uint8_t  rows_default;
static uint8_t  rows_bits;
static uint32_t row_unit;
static uint8_t  drb_defaults[16];
static row_t    *rows;


static uint8_t
row_read(uint32_t addr, void *priv)
{
    const row_t *dev = (row_t *) priv;
    uint32_t     new_addr = ((addr - dev->host_base) & dev->ram_mask) + dev->ram_base;

    addreadlookup(mem_logical_addr, new_addr);

    return dev->buf[new_addr];
}


static uint16_t
row_readw(uint32_t addr, void *priv)
{
    row_t *dev = (row_t *) priv;
    uint32_t new_addr = ((addr - dev->host_base) & dev->ram_mask) + dev->ram_base;

    addreadlookup(mem_logical_addr, new_addr);

    return *(uint16_t *) &(dev->buf[new_addr]);
}


static uint32_t
row_readl(uint32_t addr, void *priv)
{
    row_t *dev = (row_t *) priv;
    uint32_t new_addr = ((addr - dev->host_base) & dev->ram_mask) + dev->ram_base;

    addreadlookup(mem_logical_addr, new_addr);

    return *(uint32_t *) &(dev->buf[new_addr]);
}


static void
row_write(uint32_t addr, uint8_t val, void *priv)
{
    const row_t *dev = (row_t *) priv;
    uint32_t     new_addr = ((addr - dev->host_base) & dev->ram_mask) + dev->ram_base;

    addwritelookup(mem_logical_addr, new_addr);
    mem_write_ramb_page(new_addr, val, &pages[addr >> 12]);
}


static void
row_writew(uint32_t addr, uint16_t val, void *priv)
{
    const row_t *dev = (row_t *) priv;
    uint32_t     new_addr = ((addr - dev->host_base) & dev->ram_mask) + dev->ram_base;

    addwritelookup(mem_logical_addr, new_addr);
    mem_write_ramw_page(new_addr, val, &pages[addr >> 12]);
}


static void
row_writel(uint32_t addr, uint32_t val, void *priv)
{
    const row_t *dev = (row_t *) priv;
    uint32_t     new_addr = ((addr - dev->host_base) & dev->ram_mask) + dev->ram_base;

    addwritelookup(mem_logical_addr, new_addr);
    mem_write_raml_page(new_addr, val, &pages[addr >> 12]);
}


void
row_allocate(uint8_t row_id, uint8_t set)
{
    uint32_t offset;

    /* Do nothing if size is either zero or invalid. */
    if ((rows[row_id].host_size == 0x00000000) || (rows[row_id].host_size == 0xffffffff))
        return;

    if (rows[row_id].ram_size == 0x00000000)
        return;

    for (uint32_t c = (rows[row_id].host_base >> 12); c < ((rows[row_id].host_base + rows[row_id].host_size) >> 12); c++) {
        offset = c - (rows[row_id].host_base >> 12);

        pages[c].mem = set ? (rows[row_id].buf + rows[row_id].ram_base + ((offset << 12) & rows[row_id].ram_mask)) : page_ff;
        pages[c].write_b = set ? mem_write_ramb_page : NULL;
        pages[c].write_w = set ? mem_write_ramw_page : NULL;
        pages[c].write_l = set ? mem_write_raml_page : NULL;
#ifdef USE_NEW_DYNAREC
        pages[c].evict_prev = EVICT_NOT_IN_LIST;
        pages[c].byte_dirty_mask = &byte_dirty_mask[offset * 64];
        pages[c].byte_code_present_mask = &byte_code_present_mask[offset * 64];
#endif
    }

    if (rows[row_id].host_base >= 0x00100000) {
        mem_set_mem_state_both(rows[row_id].host_base, rows[row_id].host_base + rows[row_id].host_size,
                               set ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) : (MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL));
    } else {
        if (0x000a0000 > rows[row_id].host_base) {
            mem_set_mem_state_both(rows[row_id].host_base, 0x000a0000 - rows[row_id].host_base,
                                   set ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) : (MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL));
        }
        if ((rows[row_id].host_base + rows[row_id].host_size) > 0x00100000) {
            mem_set_mem_state_both(0x00100000, (rows[row_id].host_base + rows[row_id].host_size) - 0x00100000,
                                   set ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) : (MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL));
        }
    }

    if (set) {
        mem_mapping_set_addr(&rows[row_id].mapping, rows[row_id].host_base, rows[row_id].host_size);
        mem_mapping_set_exec(&rows[row_id].mapping, rows[row_id].buf + rows[row_id].ram_base);
        mem_mapping_set_mask(&rows[row_id].mapping, rows[row_id].ram_mask);
        if ((rows[row_id].host_base == rows[row_id].ram_base) && (rows[row_id].host_size == rows[row_id].ram_size)) {
#if (defined __amd64__ || defined _M_X64 || defined __aarch64__ || defined _M_ARM64)
            mem_mapping_set_handler(&rows[row_id].mapping, mem_read_ram,mem_read_ramw,mem_read_raml,
                                    mem_write_ram,mem_write_ramw,mem_write_raml);
#else
            if (rows[row_id].buf == ram2) {
                mem_mapping_set_handler(&rows[row_id].mapping, mem_read_ram_2gb,mem_read_ram_2gbw,mem_read_ram_2gbl,
                                        mem_write_ram,mem_write_ramw,mem_write_raml);
            } else {
                mem_mapping_set_handler(&rows[row_id].mapping, mem_read_ram,mem_read_ramw,mem_read_raml,
                                        mem_write_ram,mem_write_ramw,mem_write_raml);
            }
#endif
        } else {
            mem_mapping_set_handler(&rows[row_id].mapping, row_read, row_readw, row_readl,
                                    row_write, row_writew, row_writel);
        }
    } else
        mem_mapping_disable(&rows[row_id].mapping);
}


void
row_disable(uint8_t row_id)
{
    row_allocate(row_id, 0);
}


void
row_set_boundary(uint8_t row_id, uint32_t boundary)
{
    if (row_id >= rows_num)
        return;

    boundary &= ((1 << rows_bits) - 1);

    rows[row_id].host_size = boundary * row_unit;
    if (row_id == 0)
        rows[row_id].host_base = 0x00000000;
    else {
        rows[row_id].host_base = rows[row_id - 1].boundary * row_unit;
        if (rows[row_id - 1].boundary > boundary)
            rows[row_id].host_size = 0x00000000;
        else
            rows[row_id].host_size -= rows[row_id].host_base;
    }

    rows[row_id].boundary = boundary;

    row_allocate(row_id, 1);
}


void
row_reset(UNUSED(void *priv))
{
    uint32_t boundary;
    uint32_t shift;

    for (int8_t i = (rows_num - 1); i >= 0; i--)
        row_disable(i);

    for (uint8_t i = 0; i < rows_num; i++) {
        shift = (i & 1) << 2;
        boundary = ((uint32_t) drb_defaults[i]) + (((((uint32_t) drb_defaults[(i >> 1) + 8]) >> shift) & 0xf) << 8);
        row_set_boundary(i, boundary);
    }
}


void
row_close(UNUSED(void *priv))
{
    free(rows);
    rows = NULL;
}


void *
row_init(const device_t *info)
{
    uint32_t cur_drb = 0;
    uint32_t cur_drbe = 0;
    uint32_t last_drb = 0;
    uint32_t last_drbe = 0;
    uint8_t  phys_drbs[16];
    int      i;
    int      max = info->local & 0xff;
    uint32_t shift;
    uint32_t drb;
    uint32_t boundary;
    uint32_t mask;
    row_t   *new_rows = NULL;

    rows_bits = ((info->local >> 24) & 0xff);
    mask = (1 << rows_bits) - 1;
    row_unit = ((info->local >> 8) & 0xff);
    memset(phys_drbs, 0x00, 16);
    spd_write_drbs(phys_drbs, 0x00, max, row_unit);
    row_unit <<= 20;
    rows_default = (info->local >> 16) & 0xff;
    memset(drb_defaults, 0x00, 16);
    for (i = 0; i < 8; i++)
        drb_defaults[i] = rows_default;

    new_rows = calloc(max + 1, sizeof(row_t));
    rows_num = max + 1;

    rows = new_rows;

    mem_mapping_disable(&ram_low_mapping);
    mem_mapping_disable(&ram_mid_mapping);
    mem_mapping_disable(&ram_high_mapping);
#if (!(defined __amd64__ || defined _M_X64 || defined __aarch64__ || defined _M_ARM64))
    if (mem_size > 1048576)
        mem_mapping_disable(&ram_2gb_mapping);
#endif

    for (uint32_t c = 0; c < pages_sz; c++) {
        pages[c].mem = page_ff;
        pages[c].write_b = NULL;
        pages[c].write_w = NULL;
        pages[c].write_l = NULL;
#ifdef USE_NEW_DYNAREC
	pages[c].evict_prev = EVICT_NOT_IN_LIST;
	pages[c].byte_dirty_mask = &byte_dirty_mask[c * 64];
	pages[c].byte_code_present_mask = &byte_code_present_mask[c * 64];
#endif
    }

    /* Set all memory space above the default allocated area to external. */
    boundary = ((uint32_t) rows_default) * row_unit;
    mem_set_mem_state_both(boundary, (mem_size << 10) - boundary, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);

    for (i = 0; i <= max; i++) {
        cur_drb = phys_drbs[i];
        cur_drbe = phys_drbs[(i >> 1) + 8];

        shift = (i & 1) << 2;
        drb = (cur_drb & mask) + (((cur_drbe >> shift) & 0x03) << 8);
        rows[i].ram_size = drb * row_unit;

        shift = ((i - 1) & 1) << 2;
        drb = (last_drb & mask) + (((last_drbe >> shift) & 0x03) << 8);
        rows[i].ram_base = drb * row_unit;
        rows[i].ram_size -= rows[i].ram_base;

        rows[i].buf = ram;
#if (!(defined __amd64__ || defined _M_X64 || defined __aarch64__ || defined _M_ARM64))
        if (rows[i].ram_base >= (1 << 30)) {
            rows[i].ram_base -= (1 << 30);
            rows[i].buf = ram2;
        }
#endif

        rows[i].ram_mask = rows[i].ram_size - 1;

        mem_mapping_add(&rows[i].mapping, rows[i].ram_base, rows[i].ram_size,
                        row_read, row_readw, row_readl,
                        row_write, row_writew, row_writel,
                        rows[i].buf + rows[i].ram_base, MEM_MAPPING_INTERNAL, &(rows[i]));
        mem_mapping_disable(&rows[i].mapping);

        shift = (i & 1) << 2;
        boundary = ((uint32_t) drb_defaults[i]) + ((((uint32_t) drb_defaults[(i >> 1) + 8]) >> shift) << 8);
        row_set_boundary(i, boundary);

        last_drb = cur_drb;
        last_drbe = cur_drbe;
    }

    flushmmucache();

    return new_rows;
}


/* NOTE: NOT const, so that we can patch it at init. */
device_t row_device = {
    .name          = "DRAM Rows",
    .internal_name = "dram_rows",
    .flags         = DEVICE_AT,
    .local         = 0x0000,
    .init          = row_init,
    .close         = row_close,
    .reset         = row_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
