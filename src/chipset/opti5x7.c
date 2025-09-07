/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the OPTi 82C546/82C547(Python) & 82C596/82C597(Cobra) chipsets.

 * Authors: plant/nerd73,
 *          Miran Grca, <mgrca8@gmail.com>
 *          Tiseno100
 *
 *          Copyright 2020 plant/nerd73.
 *          Copyright 2020 Miran Grca.
 *          Copyright 2021 Tiseno100.
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
#include <86box/mem.h>
#include <86box/port_92.h>
#include <86box/chipset.h>

typedef struct opti5x7_t {
    uint8_t idx;
    uint8_t is_pci;
    uint8_t regs[18];
} opti5x7_t;

#ifdef ENABLE_OPTI5X7_LOG
int opti5x7_do_log = ENABLE_OPTI5X7_LOG;

static void
opti5x7_log(const char *fmt, ...)
{
    va_list ap;

    if (opti5x7_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define opti5x7_log(fmt, ...)
#endif

static void
opti5x7_shadow_map(int cur_reg, opti5x7_t *dev)
{

    /*
    Register 4h: Cxxxx Segment
    Register 5h: Dxxxx Segment

    Bits 7-6: xC000-xFFFF
    Bits 5-4: x8000-xBFFF
    Bits 3-2: x4000-x7FFF
    Bits 0-1: x0000-x3FFF

         x-y
         0 0 Read/Write AT bus
         1 0 Read from AT - Write to DRAM
         1 1 Read from DRAM - Write to DRAM
         0 1 Read from DRAM (write protected)
    */
    if (cur_reg == 0x06) {
        if (dev->is_pci) {
            mem_set_mem_state_cpu_both(0xe0000, 0x10000, ((dev->regs[6] & 1) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[6] & 2) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
            mem_set_mem_state_cpu_both(0xf0000, 0x10000, ((dev->regs[6] & 4) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[6] & 8) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
        } else {
            mem_set_mem_state_both(0xe0000, 0x10000, ((dev->regs[6] & 1) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[6] & 2) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
            mem_set_mem_state_both(0xf0000, 0x10000, ((dev->regs[6] & 4) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[6] & 8) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
        }
    } else {
        for (uint8_t i = 0; i < 4; i++) {
            if (dev->is_pci)
                mem_set_mem_state_cpu_both(0xc0000 + ((cur_reg & 1) << 16) + (i << 14), 0x4000, ((dev->regs[cur_reg] & (1 << (2 * i))) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[cur_reg] & (2 << (2 * i))) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
            else
                mem_set_mem_state_both(0xc0000 + ((cur_reg & 1) << 16) + (i << 14), 0x4000, ((dev->regs[cur_reg] & (1 << (2 * i))) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[cur_reg] & (2 << (2 * i))) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
        }
    }

    flushmmucache_nopc();
}

static void
opti5x7_write(uint16_t addr, uint8_t val, void *priv)
{
    opti5x7_t *dev = (opti5x7_t *) priv;

    switch (addr) {
        case 0x22:
            dev->idx = val;
            break;
        case 0x24:
            switch (dev->idx) {
                case 0x00: /* DRAM Configuration Register #1 */
                    dev->regs[dev->idx] = val & 0x7f;
                    break;
                case 0x01: /* DRAM Control Register #1 */
                    dev->regs[dev->idx] = val;
                    break;
                case 0x02: /* Cache Control Register #1 */
                    dev->regs[dev->idx]   = val;
                    cpu_cache_ext_enabled = !!(dev->regs[0x02] & 0x0c);
                    cpu_update_waitstates();
                    break;
                case 0x03: /* Cache Control Register #2 */
                    dev->regs[dev->idx] = val;
                    break;
                case 0x04: /* Shadow RAM Control Register #1 */
                case 0x05: /* Shadow RAM Control Register #2 */
                case 0x06: /* Shadow RAM Control Register #3 */
                    dev->regs[dev->idx] = val;
                    opti5x7_shadow_map(dev->idx, dev);
                    break;
                case 0x07: /* Tag Test Register */
                case 0x08: /* CPU Cache Control Register #1 */
                case 0x09: /* System Memory Function Register #1 */
                case 0x0a: /* System Memory Address Decode Register #1 */
                case 0x0b: /* System Memory Address Decode Register #2 */
                    dev->regs[dev->idx] = val;
                    break;
                case 0x0c: /* Extended DMA Register */
                    dev->regs[dev->idx] = val & 0xcf;
                    break;
                case 0x0d: /* ROMCS# Register */
                case 0x0e: /* Local Master Preemption Register */
                case 0x0f: /* Deturbo Control Register #1 */
                case 0x10: /* Cache Write-Hit Control Register */
                case 0x11: /* Master Cycle Control Register */
                    dev->regs[dev->idx] = val;
                    break;
                default:
                    break;
            }
            opti5x7_log("OPTi 5x7: dev->regs[%02x] = %02x\n", dev->idx, dev->regs[dev->idx]);
            break;
        default:
            break;
    }
}

static uint8_t
opti5x7_read(uint16_t addr, void *priv)
{
    const opti5x7_t *dev = (opti5x7_t *) priv;

    return ((addr == 0x24) && (dev->idx < sizeof(dev->regs))) ? dev->regs[dev->idx] : 0xff;
}

static void
opti5x7_close(void *priv)
{
    opti5x7_t *dev = (opti5x7_t *) priv;

    free(dev);
}

static void *
opti5x7_init(const device_t *info)
{
    opti5x7_t *dev = (opti5x7_t *) calloc(1, sizeof(opti5x7_t));

    dev->is_pci = info->local;

    io_sethandler(0x0022, 0x0001, opti5x7_read, NULL, NULL, opti5x7_write, NULL, NULL, dev);
    io_sethandler(0x0024, 0x0001, opti5x7_read, NULL, NULL, opti5x7_write, NULL, NULL, dev);

    device_add(&port_92_device);

    return dev;
}

const device_t opti5x7_device = {
    .name          = "OPTi 82C5x6/82C5x7",
    .internal_name = "opti5x7",
    .flags         = 0,
    .local         = 0,
    .init          = opti5x7_init,
    .close         = opti5x7_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t opti5x7_pci_device = {
    .name          = "OPTi 82C5x6/82C5x7 (PCI)",
    .internal_name = "opti5x7_pci",
    .flags         = 0,
    .local         = 1,
    .init          = opti5x7_init,
    .close         = opti5x7_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
