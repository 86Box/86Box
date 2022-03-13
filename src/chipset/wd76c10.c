/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Western Digital WD76C10 chipset.
 *
 *      Note: This chipset has no datasheet, everything were done via
 *      reverse engineering the BIOS of various machines using it.
 *
 *      Authors: Tiseno100
 *
 *		Copyright 2021 Tiseno100
 *
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/hdd.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/lpt.h>
#include <86box/mem.h>
#include <86box/port_92.h>
#include <86box/serial.h>
#include <86box/chipset.h>

/* Lock/Unlock Procedures */
#define LOCK dev->lock
#define UNLOCKED !dev->lock

#define ENABLE_WD76C10_LOG 1

#ifdef ENABLE_WD76C10_LOG
int wd76c10_do_log = ENABLE_WD76C10_LOG;
static void
wd76c10_log(const char *fmt, ...)
{
    va_list ap;

    if (wd76c10_do_log)
    {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define wd76c10_log(fmt, ...)
#endif

typedef struct
{
    uint16_t lock_reg, oscillator_40mhz, cache_flush, ems_page_reg,
        ems_page_reg_pointer, port_shadow, pmc_interrupt,
        high_mem_protect_boundry, delay_line, diagnostic,
        nmi_status, pmc_input, pmc_timer,
        pmc_output, ems_control_low_address_boundry, shadow_ram,
        split_addr, bank32staddr, bank10staddr,
        non_page_mode_dram_timing, mem_control,
        refresh_control, disk_chip_select, prog_chip_sel_addr,
        bus_timing_power_down_ctl, clk_control;

    int lock;

    fdc_t *fdc_controller;
    mem_mapping_t *mem_mapping;
    serial_t *uart[2];
} wd76c10_t;

static void wd76c10_refresh_control(wd76c10_t *dev)
{
    serial_remove(dev->uart[1]);
    /* Serial B */
    switch ((dev->refresh_control >> 1) & 7)
    {
    case 1:
        serial_setup(dev->uart[1], 0x3f8, 3);
        break;
    case 2:
        serial_setup(dev->uart[1], 0x2f8, 3);
        break;
    case 3:
        serial_setup(dev->uart[1], 0x3e8, 3);
        break;
    case 4:
        serial_setup(dev->uart[1], 0x2e8, 3);
        break;
    }

    serial_remove(dev->uart[0]);
    /* Serial A */
    switch ((dev->refresh_control >> 5) & 7)
    {
    case 1:
        serial_setup(dev->uart[0], 0x3f8, 4);
        break;
    case 2:
        serial_setup(dev->uart[0], 0x2f8, 4);
        break;
    case 3:
        serial_setup(dev->uart[0], 0x3e8, 4);
        break;
    case 4:
        serial_setup(dev->uart[0], 0x2e8, 4);
        break;
    }

    lpt1_remove();
    /* LPT */
    switch ((dev->refresh_control >> 9) & 3)
    {
    case 1:
        lpt1_init(0x3bc);
        lpt1_irq(7);
        break;
    case 2:
        lpt1_init(0x378);
        lpt1_irq(7);
        break;
    case 3:
        lpt1_init(0x278);
        lpt1_irq(7);
        break;
    }
}

static void wd76c10_split_addr(wd76c10_t *dev)
{
    switch ((dev->split_addr >> 8) & 3)
    {
    case 1:
        if (((dev->shadow_ram >> 8) & 3) == 2)
            mem_remap_top(256);
        break;
    case 2:
        if (((dev->shadow_ram >> 8) & 3) == 1)
            mem_remap_top(320);
        break;
    case 3:
        if (((dev->shadow_ram >> 8) & 3) == 3)
            mem_remap_top(384);
        break;
    }
}

static void wd76c10_disk_chip_select(wd76c10_t *dev)
{
    ide_pri_disable();
    if (!(dev->disk_chip_select & 1))
    {
        ide_set_base(0, !(dev->disk_chip_select & 0x0010) ? 0x1f0 : 0x170);
        ide_set_side(0, !(dev->disk_chip_select & 0x0010) ? 0x3f6 : 0x376);
    }
    ide_pri_enable();

    fdc_remove(dev->fdc_controller);
    if (!(dev->disk_chip_select & 2))
        fdc_set_base(dev->fdc_controller, !(dev->disk_chip_select & 0x0010) ? FDC_PRIMARY_ADDR : FDC_SECONDARY_ADDR);
}

static void wd76c10_shadow_recalc(wd76c10_t *dev)
{
    switch ((dev->shadow_ram >> 14) & 3)
    {
    case 0:
        mem_set_mem_state_both(0x20000, 0x80000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        break;
    case 1:
        mem_set_mem_state_both(0x80000, 0x20000, MEM_READ_DISABLED | MEM_WRITE_DISABLED);
        break;
    case 2:
        mem_set_mem_state_both(0x40000, 0x60000, MEM_READ_DISABLED | MEM_WRITE_DISABLED);
        break;
    case 3:
        mem_set_mem_state_both(0x20000, 0x80000, MEM_READ_DISABLED | MEM_WRITE_DISABLED);
        break;
    }

    switch ((dev->shadow_ram >> 8) & 3)
    {
    case 0:
        mem_set_mem_state_both(0xe0000, 0x20000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
        mem_set_mem_state_both(0xc0000, 0x10000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
        break;
    case 1:
        mem_set_mem_state_both(0xf0000, 0x10000, MEM_READ_INTERNAL | (!!(dev->shadow_ram & 0x1000) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL));
        break;
    case 2:
        mem_set_mem_state_both(0xe0000, 0x20000, MEM_READ_INTERNAL | (!!(dev->shadow_ram & 0x1000) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL));
        break;
    case 3:
        mem_set_mem_state_both(0x20000, 0x80000, MEM_READ_DISABLED | (!!(dev->shadow_ram & 0x1000) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL));
        break;
    }
}

static void
wd76c10_write(uint16_t addr, uint16_t val, void *priv)
{
    wd76c10_t *dev = (wd76c10_t *)priv;

    if (UNLOCKED)
    {
        switch (addr)
        {
        case 0x1072:
            dev->clk_control = val;
            break;

        case 0x1872:
            dev->bus_timing_power_down_ctl = val;
            break;

        case 0x2072:
            dev->refresh_control = val;
            wd76c10_refresh_control(dev);
            break;

        case 0x2872:
            dev->disk_chip_select = val;
            wd76c10_disk_chip_select(dev);
            break;

        case 0x3072:
            dev->prog_chip_sel_addr = val;
            break;

        case 0x3872:
            dev->non_page_mode_dram_timing = val;
            break;

        case 0x4072:
            dev->mem_control = val;
            break;

        case 0x4872:
            dev->bank10staddr = val;
            break;

        case 0x5072:
            dev->bank32staddr = val;
            break;

        case 0x5872:
            dev->split_addr = val;
            wd76c10_split_addr(dev);
            break;

        case 0x6072:
            dev->shadow_ram = val & 0xffbf;
            wd76c10_shadow_recalc(dev);
            break;

        case 0x6872:
            dev->ems_control_low_address_boundry = val & 0xecff;
            break;

        case 0x7072:
            dev->pmc_output = (val >> 8) & 0x00ff;
            break;

        case 0x7872:
            dev->pmc_output = val & 0xff00;
            break;

        case 0x8072:
            dev->pmc_timer = val;
            break;

        case 0x8872:
            dev->pmc_input = val;
            break;

        case 0x9072:
            dev->nmi_status = val & 0x00fc;
            break;

        case 0x9872:
            dev->diagnostic = val & 0xfdff;
            break;

        case 0xa072:
            dev->delay_line = val;
            break;

        case 0xc872:
            dev->pmc_interrupt = val & 0xfcfc;
            break;

        case 0xf072:
            dev->oscillator_40mhz = 0;
            break;

        case 0xf472:
            dev->oscillator_40mhz = 1;
            break;

        case 0xf872:
            dev->cache_flush = val;
            flushmmucache();
            break;
        }
        wd76c10_log("WD76C10: dev->regs[%04x] = %04x\n", addr, val);
    }

    switch (addr)
    {
    case 0xe072:
        dev->ems_page_reg_pointer = val & 0x003f;
        break;

    case 0xe872:
        dev->ems_page_reg = val & 0x8fff;
        break;

    case 0xf073:
        dev->lock_reg = val & 0x00ff;
        LOCK = !(val & 0x00da);
        break;
    }
}

static uint16_t
wd76c10_read(uint16_t addr, void *priv)
{
    wd76c10_t *dev = (wd76c10_t *)priv;
    wd76c10_log("WD76C10: R dev->regs[%04x]\n", addr);
    switch (addr)
    {
    case 0x1072:
        return dev->clk_control;

    case 0x1872:
        return dev->bus_timing_power_down_ctl;

    case 0x2072:
        return dev->refresh_control;

    case 0x2872:
        return dev->disk_chip_select;

    case 0x3072:
        return dev->prog_chip_sel_addr;

    case 0x3872:
        return dev->non_page_mode_dram_timing;

    case 0x4072:
        return dev->mem_control;

    case 0x4872:
        return dev->bank10staddr;

    case 0x5072:
        return dev->bank32staddr;

    case 0x5872:
        return dev->split_addr;

    case 0x6072:
        return dev->shadow_ram;

    case 0x6872:
        return dev->ems_control_low_address_boundry;

    case 0x7072:
        return (dev->pmc_output << 8) & 0xff00;

    case 0x7872:
        return (dev->pmc_output) & 0xff00;

    case 0x8072:
        return dev->pmc_timer;

    case 0x8872:
        return dev->pmc_input;

    case 0x9072:
        return dev->nmi_status;

    case 0x9872:
        return dev->diagnostic;

    case 0xa072:
        return dev->delay_line;

    case 0xb872:
        return (inb(0x040b) << 8) | inb(0x04d6);

    case 0xc872:
        return dev->pmc_interrupt;

    case 0xd072:
        return dev->port_shadow;

    case 0xe072:
        return dev->ems_page_reg_pointer;

    case 0xe872:
        return dev->ems_page_reg;

    case 0xfc72:
        return 0x0ff0;

    default:
        return 0xffff;
    }
}

static void
wd76c10_close(void *priv)
{
    wd76c10_t *dev = (wd76c10_t *)priv;

    free(dev);
}

static void *
wd76c10_init(const device_t *info)
{
    wd76c10_t *dev = (wd76c10_t *)malloc(sizeof(wd76c10_t));
    memset(dev, 0, sizeof(wd76c10_t));

    device_add(&port_92_inv_device);
    dev->uart[0] = device_add_inst(&ns16450_device, 1);
    dev->uart[1] = device_add_inst(&ns16450_device, 2);
    dev->fdc_controller = device_add(&fdc_at_device);
    device_add(&ide_isa_device);

    /* Lock Configuration */
    LOCK = 1;

    /* Clock Control */
    io_sethandler(0x1072, 1, NULL, wd76c10_read, NULL, NULL, wd76c10_write, NULL, dev);

    /* Bus Timing & Power Down Control */
    io_sethandler(0x1872, 1, NULL, wd76c10_read, NULL, NULL, wd76c10_write, NULL, dev);

    /* Refresh Control(Serial & Parallel) */
    io_sethandler(0x2072, 1, NULL, wd76c10_read, NULL, NULL, wd76c10_write, NULL, dev);

    /* Disk Chip Select */
    io_sethandler(0x2872, 1, NULL, wd76c10_read, NULL, NULL, wd76c10_write, NULL, dev);

    /* Programmable Chip Select Address(Needs more further examination!) */
    io_sethandler(0x3072, 1, NULL, wd76c10_read, NULL, NULL, wd76c10_write, NULL, dev);

    /* Bank 1 & 0 Start Address */
    io_sethandler(0x4872, 1, NULL, wd76c10_read, NULL, NULL, wd76c10_write, NULL, dev);

    /* Bank 3 & 2 Start Address */
    io_sethandler(0x5072, 1, NULL, wd76c10_read, NULL, NULL, wd76c10_write, NULL, dev);

    /* Split Address */
    io_sethandler(0x5872, 1, NULL, wd76c10_read, NULL, NULL, wd76c10_write, NULL, dev);

    /* EMS Control & EMS Low level boundry */
    io_sethandler(0x6072, 1, NULL, wd76c10_read, NULL, NULL, wd76c10_write, NULL, dev);

    /* EMS Control & EMS Low level boundry */
    io_sethandler(0x6872, 1, NULL, wd76c10_read, NULL, NULL, wd76c10_write, NULL, dev);

    /* PMC Output */
    io_sethandler(0x7072, 1, NULL, wd76c10_read, NULL, NULL, wd76c10_write, NULL, dev);

    /* PMC Output */
    io_sethandler(0x7872, 1, NULL, wd76c10_read, NULL, NULL, wd76c10_write, NULL, dev);

    /* PMC Status */
    io_sethandler(0x8072, 1, NULL, wd76c10_read, NULL, NULL, wd76c10_write, NULL, dev);

    /* PMC Status */
    io_sethandler(0x8872, 1, NULL, wd76c10_read, NULL, NULL, wd76c10_write, NULL, dev);

    /* NMI Status (Needs further checkup) */
    io_sethandler(0x9072, 1, NULL, wd76c10_read, NULL, NULL, wd76c10_write, NULL, dev);

    /* Diagnostics */
    io_sethandler(0x9872, 1, NULL, wd76c10_read, NULL, NULL, wd76c10_write, NULL, dev);

    /* Delay Line */
    io_sethandler(0xa072, 1, NULL, wd76c10_read, NULL, NULL, wd76c10_write, NULL, dev);

    /* DMA Mode Shadow(Needs Involvement on the DMA code) */
    io_sethandler(0xb872, 1, NULL, wd76c10_read, NULL, NULL, NULL, NULL, dev);

    /* High Memory Protection Boundry */
    io_sethandler(0xc072, 1, NULL, wd76c10_read, NULL, NULL, NULL, NULL, dev);

    /* PMC Interrupt Enable */
    io_sethandler(0xc872, 1, NULL, wd76c10_read, NULL, NULL, NULL, NULL, dev);

    /* Port Shadow (Needs further lookup) */
    io_sethandler(0xd072, 1, NULL, wd76c10_read, NULL, NULL, NULL, NULL, dev);

    /* EMS Page Register Pointer */
    io_sethandler(0xe072, 1, NULL, wd76c10_read, NULL, NULL, wd76c10_write, NULL, dev);

    /* EMS Page Register */
    io_sethandler(0xe872, 1, NULL, wd76c10_read, NULL, NULL, wd76c10_write, NULL, dev);

    /* Lock/Unlock Configuration */
    io_sethandler(0xf073, 1, NULL, NULL, NULL, NULL, wd76c10_write, NULL, dev);

    /* 40Mhz Oscillator Enable Disable */
    io_sethandler(0xf072, 1, NULL, NULL, NULL, NULL, wd76c10_write, NULL, dev);
    io_sethandler(0xf472, 1, NULL, NULL, NULL, NULL, wd76c10_write, NULL, dev);

    /* Lock Status */
    io_sethandler(0xfc72, 1, NULL, wd76c10_read, NULL, NULL, NULL, NULL, dev);

    /* Cache Flush */
    io_sethandler(0xf872, 1, NULL, wd76c10_read, NULL, NULL, NULL, NULL, dev);

    dma_ext_mode_init();

    wd76c10_shadow_recalc(dev);
    wd76c10_refresh_control(dev);
    wd76c10_disk_chip_select(dev);
    return dev;
}

const device_t wd76c10_device = {
    .name = "Western Digital WD76C10",
    .internal_name = "wd76c10",
    .flags = 0,
    .local = 0,
    .init = wd76c10_init,
    .close = wd76c10_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
