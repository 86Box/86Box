/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of Intel 82420EX chipset that acts as both the
 *          northbridge and the southbridge.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020 Miran Grca.
 */
#define USE_DRB_HACK
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/io.h>
#include <86box/apm.h>
#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/smram.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/plat_unused.h>
#include <86box/port_92.h>
#include <86box/hdc_ide.h>
#include <86box/hdc.h>
#include <86box/machine.h>
#include <86box/chipset.h>
#include <86box/spd.h>
#ifndef USE_DRB_HACK
#include <86box/row.h>
#endif

#define MEM_STATE_SHADOW_R 0x01
#define MEM_STATE_SHADOW_W 0x02
#define MEM_STATE_SMRAM    0x04

typedef struct i420ex_t {
    uint8_t has_ide;
    uint8_t smram_locked;
    uint8_t pci_slot;
    uint8_t pad;

    uint8_t regs[256];

    uint16_t timer_base;
    uint16_t timer_latch;

    smram_t *smram;

    double fast_off_period;

    pc_timer_t timer;
    pc_timer_t fast_off_timer;

    apm_t     *apm;
    port_92_t *port_92;
} i420ex_t;

#ifdef ENABLE_I420EX_LOG
int i420ex_do_log = ENABLE_I420EX_LOG;

static void
i420ex_log(const char *fmt, ...)
{
    va_list ap;

    if (i420ex_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define i420ex_log(fmt, ...)
#endif

static void
i420ex_map(uint32_t addr, uint32_t size, int state)
{
    switch (state & 3) {
        case 0:
            mem_set_mem_state_both(addr, size, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
            break;
        case 1:
            mem_set_mem_state_both(addr, size, MEM_READ_INTERNAL | MEM_WRITE_EXTANY);
            break;
        case 2:
            mem_set_mem_state_both(addr, size, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
            break;
        case 3:
            mem_set_mem_state_both(addr, size, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
            break;
        default:
            break;
    }
    flushmmucache_nopc();
}

static void
i420ex_smram_handler_phase0(void)
{
    /* Disable low extended SMRAM. */
    smram_disable_all();
}

static void
i420ex_smram_handler_phase1(i420ex_t *dev)
{
    const uint8_t *regs = (uint8_t *) dev->regs;

    uint32_t host_base = 0x000a0000;
    uint32_t ram_base  = 0x000a0000;
    uint32_t size      = 0x00010000;

    switch (regs[0x70] & 0x07) {
        default:
        case 0:
        case 1:
            host_base = ram_base = 0x00000000;
            size                 = 0x00000000;
            break;
        case 2:
            host_base = 0x000a0000;
            ram_base  = 0x000a0000;
            break;
        case 3:
            host_base = 0x000b0000;
            ram_base  = 0x000b0000;
            break;
        case 4:
            host_base = 0x000c0000;
            ram_base  = 0x000a0000;
            break;
        case 5:
            host_base = 0x000d0000;
            ram_base  = 0x000a0000;
            break;
        case 6:
            host_base = 0x000e0000;
            ram_base  = 0x000a0000;
            break;
        case 7:
            host_base = 0x000f0000;
            ram_base  = 0x000a0000;
            break;
    }

    smram_enable(dev->smram, host_base, ram_base, size,
                 (regs[0x70] & 0x70) == 0x40, !(regs[0x70] & 0x20));
}

#ifndef USE_DRB_HACK
static void
i420ex_drb_recalc(i420ex_t *dev)
{
    uint32_t boundary;

    for (int8_t i = 4; i >= 0; i--)
        row_disable(i);

    for (uint8_t i = 0; i <= 4; i++) {
        boundary = ((uint32_t) dev->regs[0x60 + i]) & 0xff;
        row_set_boundary(i, boundary);
    }

    flushmmucache();
}
#endif


static void
i420ex_write(int func, int addr, uint8_t val, void *priv)
{
    i420ex_t *dev = (i420ex_t *) priv;

    if (func > 0)
        return;

    if (((addr >= 0x0f) && (addr < 0x4c)) && (addr != 0x40))
        return;

    switch (addr) {
        case 0x05:
            dev->regs[addr] = (val & 0x01);
            break;

        case 0x07:
            dev->regs[addr] &= ~(val & 0xf0);
            break;

        case 0x40:
            dev->regs[addr] = (val & 0x7f);
            break;
        case 0x44:
            dev->regs[addr] = (val & 0x07);
            break;
        case 0x48:
            dev->regs[addr] = (val & 0x3f);
            if (dev->has_ide) {
                ide_pri_disable();
                switch (val & 0x03) {
                    case 0x01:
                        ide_set_base(0, 0x01f0);
                        ide_set_side(0, 0x03f6);
                        ide_pri_enable();
                        break;
                    case 0x02:
                        ide_set_base(0, 0x0170);
                        ide_set_side(0, 0x0376);
                        ide_pri_enable();
                        break;
                    default:
                        break;
                }
            }
            break;
        case 0x49:
        case 0x53:
            dev->regs[addr] = (val & 0x1f);
            break;
        case 0x4c:
        case 0x51:
        case 0x57:
        case 0x68:
        case 0x69:
            dev->regs[addr] = val;
            if (addr == 0x4c) {
                dma_alias_remove();
                if (!(val & 0x80))
                    dma_alias_set();
            }
            break;
        case 0x4d:
            dev->regs[addr] = (dev->regs[addr] & 0xef) | (val & 0x10);
            break;
        case 0x4e:
            dev->regs[addr] = (val & 0xf7);
            break;
        case 0x50:
            dev->regs[addr] = (val & 0x0f);
            break;
        case 0x52:
            dev->regs[addr] = (val & 0x7f);
            break;
        case 0x56:
            dev->regs[addr] = (val & 0x3e);
            break;
        case 0x59: /* PAM0 */
            if ((dev->regs[0x59] ^ val) & 0xf0) {
                i420ex_map(0xf0000, 0x10000, val >> 4);
                shadowbios = (val & 0x10);
            }
            dev->regs[0x59] = val & 0xf0;
            break;
        case 0x5a: /* PAM1 */
            if ((dev->regs[0x5a] ^ val) & 0x0f)
                i420ex_map(0xc0000, 0x04000, val & 0xf);
            if ((dev->regs[0x5a] ^ val) & 0xf0)
                i420ex_map(0xc4000, 0x04000, val >> 4);
            dev->regs[0x5a] = val;
            break;
        case 0x5b: /*PAM2 */
            if ((dev->regs[0x5b] ^ val) & 0x0f)
                i420ex_map(0xc8000, 0x04000, val & 0xf);
            if ((dev->regs[0x5b] ^ val) & 0xf0)
                i420ex_map(0xcc000, 0x04000, val >> 4);
            dev->regs[0x5b] = val;
            break;
        case 0x5c: /*PAM3 */
            if ((dev->regs[0x5c] ^ val) & 0x0f)
                i420ex_map(0xd0000, 0x04000, val & 0xf);
            if ((dev->regs[0x5c] ^ val) & 0xf0)
                i420ex_map(0xd4000, 0x04000, val >> 4);
            dev->regs[0x5c] = val;
            break;
        case 0x5d: /* PAM4 */
            if ((dev->regs[0x5d] ^ val) & 0x0f)
                i420ex_map(0xd8000, 0x04000, val & 0xf);
            if ((dev->regs[0x5d] ^ val) & 0xf0)
                i420ex_map(0xdc000, 0x04000, val >> 4);
            dev->regs[0x5d] = val;
            break;
        case 0x5e: /* PAM5 */
            if ((dev->regs[0x5e] ^ val) & 0x0f)
                i420ex_map(0xe0000, 0x04000, val & 0xf);
            if ((dev->regs[0x5e] ^ val) & 0xf0)
                i420ex_map(0xe4000, 0x04000, val >> 4);
            dev->regs[0x5e] = val;
            break;
        case 0x5f: /* PAM6 */
            if ((dev->regs[0x5f] ^ val) & 0x0f)
                i420ex_map(0xe8000, 0x04000, val & 0xf);
            if ((dev->regs[0x5f] ^ val) & 0xf0)
                i420ex_map(0xec000, 0x04000, val >> 4);
            dev->regs[0x5f] = val;
            break;
        case 0x60:
        case 0x61:
        case 0x62:
        case 0x63:
        case 0x64:
#ifdef USE_DRB_HACK
            spd_write_drbs(dev->regs, 0x60, 0x64, 1);
#else
            dev->regs[addr] = val;
            i420ex_drb_recalc(dev);
#endif
            break;
        case 0x66:
        case 0x67:
            i420ex_log("Set IRQ routing: INT %c -> %02X\n", 0x41 + (addr & 0x01), val);
            dev->regs[addr] = val & 0x8f;
            if (val & 0x80)
                pci_set_irq_routing(PCI_INTA + (addr & 0x01), PCI_IRQ_DISABLED);
            else
                pci_set_irq_routing(PCI_INTA + (addr & 0x01), val & 0xf);
            break;
        case 0x70: /* SMRAM */
            i420ex_smram_handler_phase0();
            if (dev->smram_locked)
                dev->regs[0x70] = (dev->regs[0x70] & 0xdf) | (val & 0x20);
            else {
                dev->regs[0x70]   = (dev->regs[0x70] & 0x88) | (val & 0x77);
                dev->smram_locked = (val & 0x10);
                if (dev->smram_locked)
                    dev->regs[0x70] &= 0xbf;
            }
            i420ex_smram_handler_phase1(dev);
            break;
        case 0xa0:
            dev->regs[addr] = val & 0x1f;
            apm_set_do_smi(dev->apm, !!(val & 0x01) && !!(dev->regs[0xa2] & 0x80));
            switch ((val & 0x18) >> 3) {
                case 0x00:
                    dev->fast_off_period = PCICLK * 32768.0 * 60000.0;
                    break;
                case 0x01:
                default:
                    dev->fast_off_period = 0.0;
                    break;
                case 0x02:
                    dev->fast_off_period = PCICLK;
                    break;
                case 0x03:
                    dev->fast_off_period = PCICLK * 32768.0;
                    break;
            }
            cpu_fast_off_count = cpu_fast_off_val + 1;
            cpu_fast_off_period_set(cpu_fast_off_val, dev->fast_off_period);
            break;
        case 0xa2:
            dev->regs[addr] = val & 0xff;
            apm_set_do_smi(dev->apm, !!(dev->regs[0xa0] & 0x01) && !!(val & 0x80));
            break;
        case 0xaa:
            dev->regs[addr] &= (val & 0xff);
            break;
        case 0xac:
        case 0xae:
            dev->regs[addr] = val & 0xff;
            break;
        case 0xa4:
            dev->regs[addr]    = val & 0xfb;
            cpu_fast_off_flags = (cpu_fast_off_flags & 0xffffff00) | dev->regs[addr];
            break;
        case 0xa5:
            dev->regs[addr]    = val;
            cpu_fast_off_flags = (cpu_fast_off_flags & 0xffff00ff) | (dev->regs[addr] << 8);
            break;
        case 0xa7:
            dev->regs[addr]    = val & 0xe0;
            cpu_fast_off_flags = (cpu_fast_off_flags & 0x00ffffff) | (dev->regs[addr] << 24);
            break;
        case 0xa8:
            dev->regs[addr]    = val & 0xff;
            cpu_fast_off_val   = val;
            cpu_fast_off_count = val + 1;
            cpu_fast_off_period_set(cpu_fast_off_val, dev->fast_off_period);
            break;
        default:
            break;
    }
}

static uint8_t
i420ex_read(int func, int addr, void *priv)
{
    const i420ex_t *dev = (i420ex_t *) priv;
    uint8_t         ret;

    ret = 0xff;

    if (func == 0)
        ret = dev->regs[addr];

    return ret;
}

static void
i420ex_reset_hard(void *priv)
{
    i420ex_t *dev = (i420ex_t *) priv;

    memset(dev->regs, 0, 256);

    dev->regs[0x00] = 0x86;
    dev->regs[0x01] = 0x80; /*Intel*/
    dev->regs[0x02] = 0x86;
    dev->regs[0x03] = 0x04; /*82378IB (I420EX)*/
    dev->regs[0x04] = 0x07;
    dev->regs[0x07] = 0x02;

    dev->regs[0x4c] = 0x4d;
    dev->regs[0x4e] = 0x03;
   /* Bits 2:1 of register 50h are 00 is 25 MHz, and 01 if 33 MHz, 10 and 11 are reserved. */
    if (cpu_busspeed >= 33333333)
        dev->regs[0x50] |= 0x02;
    dev->regs[0x51] = 0x80;
    dev->regs[0x60] = dev->regs[0x61] = dev->regs[0x62] = dev->regs[0x63] = dev->regs[0x64] = 0x01;
    dev->regs[0x66]                                                                         = 0x80;
    dev->regs[0x67]                                                                         = 0x80;
    dev->regs[0x69]                                                                         = 0x02;
    dev->regs[0xa0]                                                                         = 0x08;
    dev->regs[0xa8]                                                                         = 0x0f;

    mem_set_mem_state(0x000a0000, 0x00060000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
    mem_set_mem_state_smm(0x000a0000, 0x00060000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);

    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);

    if (dev->has_ide)
        ide_pri_disable();
}

static void
i420ex_apm_out(UNUSED(uint16_t port), UNUSED(uint8_t val), void *priv)
{
    i420ex_t *dev = (i420ex_t *) priv;

    if (dev->apm->do_smi)
        dev->regs[0xaa] |= 0x80;
}

static void
i420ex_fast_off_count(void *priv)
{
    i420ex_t *dev = (i420ex_t *) priv;

    cpu_fast_off_count--;

    smi_raise();
    dev->regs[0xaa] |= 0x20;
}

static void
i420ex_reset(void *priv)
{
    i420ex_t *dev = (i420ex_t *) priv;

    i420ex_write(0, 0x48, 0x00, priv);

    /* Disable the PIC mouse latch. */
    i420ex_write(0, 0x4e, 0x03, priv);

    for (uint8_t i = 0; i < 7; i++)
        i420ex_write(0, 0x59 + i, 0x00, priv);

    for (uint8_t i = 0; i <= 4; i++)
        dev->regs[0x60 + i] = 0x01;

    dev->regs[0x70] &= 0xef; /* Forcibly unlock the SMRAM register. */
    dev->smram_locked = 0;
    i420ex_write(0, 0x70, 0x00, priv);

    mem_set_mem_state(0x000a0000, 0x00060000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
    mem_set_mem_state_smm(0x000a0000, 0x00060000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);

    i420ex_write(0, 0xa0, 0x08, priv);
    i420ex_write(0, 0xa2, 0x00, priv);
    i420ex_write(0, 0xa4, 0x00, priv);
    i420ex_write(0, 0xa5, 0x00, priv);
    i420ex_write(0, 0xa6, 0x00, priv);
    i420ex_write(0, 0xa7, 0x00, priv);
    i420ex_write(0, 0xa8, 0x0f, priv);
}

static void
i420ex_close(void *priv)
{
    i420ex_t *dev = (i420ex_t *) priv;

    smram_del(dev->smram);

    free(dev);
}

static void
i420ex_speed_changed(void *priv)
{
    i420ex_t *dev = (i420ex_t *) priv;
    int       te;

    te = timer_is_enabled(&dev->timer);

    timer_disable(&dev->timer);
    if (te)
        timer_set_delay_u64(&dev->timer, ((uint64_t) dev->timer_latch) * TIMER_USEC);

    te = timer_is_on(&dev->fast_off_timer);

    timer_stop(&dev->fast_off_timer);
    if (te)
        timer_on_auto(&dev->fast_off_timer, dev->fast_off_period);
}

static void *
i420ex_init(const device_t *info)
{
    i420ex_t *dev = (i420ex_t *) calloc(1, sizeof(i420ex_t));

    dev->smram = smram_add();

    pci_add_card(PCI_ADD_NORTHBRIDGE, i420ex_read, i420ex_write, dev, &dev->pci_slot);

    dev->has_ide = info->local;

    timer_add(&dev->fast_off_timer, i420ex_fast_off_count, dev, 0);

    cpu_fast_off_flags = 0x00000000;

    cpu_fast_off_val   = dev->regs[0xa8];
    cpu_fast_off_count = cpu_fast_off_val + 1;

    cpu_register_fast_off_handler(&dev->fast_off_timer);

    dev->apm = device_add(&apm_pci_device);
    /* APM intercept handler to update 82420EX SMI status on APM SMI. */
    io_sethandler(0x00b2, 0x0001, NULL, NULL, NULL, i420ex_apm_out, NULL, NULL, dev);

    dev->port_92 = device_add(&port_92_pci_device);

    dma_alias_set();

    device_add(&ide_pci_2ch_device);

#ifndef USE_DRB_HACK
    row_device.local = 4 | (1 << 8) | (0x01 << 16) | (8 << 24);
    device_add((const device_t *) &row_device);
#endif

    i420ex_reset_hard(dev);

    return dev;
}

const device_t i420ex_device = {
    .name          = "Intel 82420EX",
    .internal_name = "i420ex",
    .flags         = DEVICE_PCI,
    .local         = 0x00,
    .init          = i420ex_init,
    .close         = i420ex_close,
    .reset         = i420ex_reset,
    .available     = NULL,
    .speed_changed = i420ex_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t i420ex_ide_device = {
    .name          = "Intel 82420EX (With IDE)",
    .internal_name = "i420ex_ide",
    .flags         = DEVICE_PCI,
    .local         = 0x01,
    .init          = i420ex_init,
    .close         = i420ex_close,
    .reset         = i420ex_reset,
    .available     = NULL,
    .speed_changed = i420ex_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};
