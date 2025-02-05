/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Contaq/Cypress 82C596(A) and 597 chipsets.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2021 Miran Grca.
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
#include <86box/smram.h>
#include <86box/chipset.h>

#ifdef ENABLE_CONTAQ_82C59X_LOG
int contaq_82c59x_do_log = ENABLE_CONTAQ_82C59X_LOG;

static void
contaq_82c59x_log(const char *fmt, ...)
{
    va_list ap;

    if (contaq_82c59x_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define contaq_82c59x_log(fmt, ...)
#endif

typedef struct mem_remapping_t {
    uint32_t phys;
    uint32_t virt;
} mem_remapping_t;

typedef struct contaq_82c59x_t {
    uint8_t index;
    uint8_t green;
    uint8_t smi_status_set;
    uint8_t regs[256];
    uint8_t smi_status[2];

    smram_t *smram[2];
} contaq_82c59x_t;

static void
contaq_82c59x_isa_speed_recalc(contaq_82c59x_t *dev)
{
    if (dev->regs[0x1c] & 0x02)
        cpu_set_isa_speed(7159091);
    else {
        /* TODO: ISA clock dividers for 386 and alt. 486. */
        switch (dev->regs[0x10] & 0x03) {
            case 0x00:
                cpu_set_isa_speed(cpu_busspeed / 4);
                break;
            case 0x01:
                cpu_set_isa_speed(cpu_busspeed / 6);
                break;
            case 0x02:
                cpu_set_isa_speed(cpu_busspeed / 8);
                break;
            case 0x03:
                cpu_set_isa_speed(cpu_busspeed / 5);
                break;
            default:
                break;
        }
    }
}

static void
contaq_82c59x_shadow_recalc(contaq_82c59x_t *dev)
{
    uint32_t i;
    uint32_t base;
    uint8_t  bit;

    shadowbios = shadowbios_write = 0;

    /* F0000-FFFFF */
    if (dev->regs[0x15] & 0x80) {
        shadowbios |= 1;
        mem_set_mem_state_both(0xf0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_EXTANY);
    } else {
        shadowbios_write |= 1;
        mem_set_mem_state_both(0xf0000, 0x10000, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
    }

    /* C0000-CFFFF */
    if (dev->regs[0x15] & 0x01)
        mem_set_mem_state_both(0xc0000, 0x10000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
    else {
        for (i = 0; i < 4; i++) {
            base = 0xc0000 + (i << 14);
            bit  = 1 << (i + 2);
            if (dev->regs[0x15] & bit) {
                if (dev->regs[0x15] & 0x02)
                    mem_set_mem_state_both(base, 0x04000, MEM_READ_INTERNAL | MEM_WRITE_EXTERNAL);
                else
                    mem_set_mem_state_both(base, 0x04000, MEM_READ_EXTERNAL | MEM_WRITE_INTERNAL);
            } else
                mem_set_mem_state_both(base, 0x04000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
        }
    }

    if (dev->green) {
        /* D0000-DFFFF */
        if (dev->regs[0x6e] & 0x01)
            mem_set_mem_state_both(0xd0000, 0x10000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
        else {
            for (i = 0; i < 4; i++) {
                base = 0xd0000 + (i << 14);
                bit  = 1 << (i + 2);
                if (dev->regs[0x6e] & bit) {
                    if (dev->regs[0x6e] & 0x02)
                        mem_set_mem_state_both(base, 0x04000, MEM_READ_INTERNAL | MEM_WRITE_EXTERNAL);
                    else
                        mem_set_mem_state_both(base, 0x04000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                } else
                    mem_set_mem_state_both(base, 0x04000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
            }
        }

        /* E0000-EFFFF */
        if (dev->regs[0x6f] & 0x01)
            mem_set_mem_state_both(0xe0000, 0x10000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
        else {
            for (i = 0; i < 4; i++) {
                base = 0xe0000 + (i << 14);
                bit  = 1 << (i + 2);
                if (dev->regs[0x6f] & bit) {
                    shadowbios |= 1;
                    if (dev->regs[0x6f] & 0x02)
                        mem_set_mem_state_both(base, 0x04000, MEM_READ_INTERNAL | MEM_WRITE_EXTERNAL);
                    else {
                        shadowbios_write |= 1;
                        mem_set_mem_state_both(base, 0x04000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                    }
                } else
                    mem_set_mem_state_both(base, 0x04000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
            }
        }
    }
}

static void
contaq_82c59x_smram_recalc(contaq_82c59x_t *dev)
{
    smram_disable(dev->smram[1]);

    if (dev->regs[0x70] & 0x04)
        smram_enable(dev->smram[1], 0x00040000, 0x000a0000, 0x00020000, 1, 1);
}

static void
contaq_82c59x_write(uint16_t addr, uint8_t val, void *priv)
{
    contaq_82c59x_t *dev = (contaq_82c59x_t *) priv;

    switch (addr) {
        case 0x22:
            dev->index = val;
            break;

        case 0x23:
            contaq_82c59x_log("Contaq 82C59x: dev->regs[%02x] = %02x\n", dev->index, val);

            if ((dev->index >= 0x60) && !dev->green)
                return;

            switch (dev->index) {
                /* Registers common to 82C596(A) and 82C597. */
                case 0x10:
                    dev->regs[dev->index] = val;
                    contaq_82c59x_isa_speed_recalc(dev);
                    break;

                case 0x11:
                    dev->regs[dev->index] = val;
                    cpu_cache_int_enabled = !!(val & 0x01);
                    cpu_update_waitstates();
                    break;

                case 0x12:
                case 0x13:
                    dev->regs[dev->index] = val;
                    break;

                case 0x14:
                    dev->regs[dev->index] = val;
                    reset_on_hlt          = !!(val & 0x80);
                    break;

                case 0x15:
                    dev->regs[dev->index] = val;
                    contaq_82c59x_shadow_recalc(dev);
                    break;

                case 0x16 ... 0x1b:
                    dev->regs[dev->index] = val;
                    break;

                case 0x1c:
                    /* TODO: What's NPRST (generated if bit 3 is set)? */
                    dev->regs[dev->index] = val;
                    contaq_82c59x_isa_speed_recalc(dev);
                    break;

                case 0x1d ... 0x1f:
                    dev->regs[dev->index] = val;
                    break;

                /* Green (82C597-specific) registers. */
                case 0x60 ... 0x63:
                    dev->regs[dev->index] = val;
                    break;

                case 0x64:
                    dev->regs[dev->index] = val;
                    if (val & 0x80) {
                        if (dev->regs[0x65] & 0x80)
                            smi_raise();
                        dev->smi_status[0] |= 0x10;
                    }
                    break;

                case 0x65 ... 0x69:
                    dev->regs[dev->index] = val;
                    break;

                case 0x6a:
                    dev->regs[dev->index] = val;
                    dev->smi_status_set   = !!(val & 0x80);
                    break;

                case 0x6b ... 0x6d:
                    dev->regs[dev->index] = val;
                    break;

                case 0x6e:
                case 0x6f:
                    dev->regs[dev->index] = val;
                    contaq_82c59x_shadow_recalc(dev);
                    break;

                case 0x70:
                    dev->regs[dev->index] = val;
                    contaq_82c59x_smram_recalc(dev);
                    break;

                case 0x71 ... 0x79:
                    dev->regs[dev->index] = val;
                    break;

                case 0x7b:
                case 0x7c:
                    dev->regs[dev->index] = val;
                    break;

                default:
                    break;
            }
            break;
        
        default:
            break;
    }
}

static uint8_t
contaq_82c59x_read(uint16_t addr, void *priv)
{
    contaq_82c59x_t *dev = (contaq_82c59x_t *) priv;
    uint8_t          ret = 0xff;

    if (addr == 0x23) {
        if (dev->index == 0x6a) {
            ret = dev->smi_status[dev->smi_status_set];
            /* I assume it's cleared on read. */
            dev->smi_status[dev->smi_status_set] = 0x00;
        } else
            ret = dev->regs[dev->index];
    }

    return ret;
}

static void
contaq_82c59x_close(void *priv)
{
    contaq_82c59x_t *dev = (contaq_82c59x_t *) priv;

    if (dev->green) {
        smram_del(dev->smram[1]);
        smram_del(dev->smram[0]);
    }

    free(dev);
}

static void *
contaq_82c59x_init(const device_t *info)
{
    contaq_82c59x_t *dev = (contaq_82c59x_t *) calloc(1, sizeof(contaq_82c59x_t));

    dev->green = info->local;

    io_sethandler(0x0022, 0x0002, contaq_82c59x_read, NULL, NULL, contaq_82c59x_write, NULL, NULL, dev);

    contaq_82c59x_isa_speed_recalc(dev);

    cpu_cache_int_enabled = 0;
    cpu_update_waitstates();

    reset_on_hlt = 0;

    contaq_82c59x_shadow_recalc(dev);

    if (dev->green) {
        /* SMRAM 0: Fixed A0000-BFFFF to A0000-BFFFF DRAM. */
        dev->smram[0] = smram_add();
        smram_enable(dev->smram[0], 0x000a0000, 0x000a0000, 0x00020000, 0, 1);

        /* SMRAM 1: Optional. */
        dev->smram[1] = smram_add();
        contaq_82c59x_smram_recalc(dev);
    }

    return dev;
}

const device_t contaq_82c596a_device = {
    .name          = "Contaq 82C596A",
    .internal_name = "contaq_82c596a",
    .flags         = 0,
    .local         = 0,
    .init          = contaq_82c59x_init,
    .close         = contaq_82c59x_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t contaq_82c597_device = {
    .name          = "Contaq 82C597",
    .internal_name = "contaq_82c597",
    .flags         = 0,
    .local         = 1,
    .init          = contaq_82c59x_init,
    .close         = contaq_82c59x_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
