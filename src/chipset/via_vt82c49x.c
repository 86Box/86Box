/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the VIA VT82C49X chipset.
 *
 *
 *
 * Authors:	Tiseno100,
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2020 Tiseno100.
 *		Copyright 2020 Miran Grca.
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
#include <86box/io.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/smram.h>
#include <86box/pic.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/port_92.h>
#include <86box/chipset.h>

typedef struct
{
    uint8_t has_ide, index,
        regs[256];

    smram_t *smram_smm, *smram_low,
        *smram_high;
} vt82c49x_t;

#ifdef ENABLE_VT82C49X_LOG
int vt82c49x_do_log = ENABLE_VT82C49X_LOG;

static void
vt82c49x_log(const char *fmt, ...)
{
    va_list ap;

    if (vt82c49x_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define vt82c49x_log(fmt, ...)
#endif

static void
vt82c49x_recalc(vt82c49x_t *dev)
{
    int      i, relocate;
    uint8_t  reg, bit;
    uint32_t base, state;
    uint32_t shadow_bitmap = 0x00000000;

    relocate = (dev->regs[0x33] >> 2) & 0x03;

    shadowbios       = 0;
    shadowbios_write = 0;

    for (i = 0; i < 8; i++) {
        base = 0xc0000 + (i << 14);
        reg  = 0x30 + (i >> 2);
        bit  = (i & 3) << 1;

        if ((base >= 0xc0000) && (base <= 0xc7fff)) {
            if (dev->regs[0x40] & 0x80)
                state = MEM_WRITE_DISABLED;
            else if ((dev->regs[reg]) & (1 << bit))
                state = MEM_WRITE_INTERNAL;
            else
                state = (dev->regs[0x33] & 0x40) ? MEM_WRITE_ROMCS : MEM_WRITE_EXTERNAL;

            if ((dev->regs[reg]) & (1 << (bit + 1)))
                state |= MEM_READ_INTERNAL;
            else
                state |= (dev->regs[0x33] & 0x40) ? MEM_READ_ROMCS : MEM_READ_EXTERNAL;
        }
        if ((base >= 0xc8000) && (base <= 0xcffff)) {
            if ((dev->regs[reg]) & (1 << bit))
                state = MEM_WRITE_INTERNAL;
            else
                state = (dev->regs[0x33] & 0x80) ? MEM_WRITE_ROMCS : MEM_WRITE_EXTERNAL;

            if ((dev->regs[reg]) & (1 << (bit + 1)))
                state |= MEM_READ_INTERNAL;
            else
                state |= (dev->regs[0x33] & 0x80) ? MEM_READ_ROMCS : MEM_READ_EXTERNAL;
        } else {
            state = ((dev->regs[reg]) & (1 << bit)) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
            state |= ((dev->regs[reg]) & (1 << (bit + 1))) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
        }

        vt82c49x_log("(%02X=%02X, %i) Setting %08X-%08X to: write %sabled, read %sabled\n",
                     reg, dev->regs[reg], bit, base, base + 0x3fff,
                     ((dev->regs[reg]) & (1 << bit)) ? "en" : "dis", ((dev->regs[reg]) & (1 << (bit + 1))) ? "en" : "dis");

        if ((dev->regs[reg]) & (1 << bit))
            shadow_bitmap |= (1 << i);
        if ((dev->regs[reg]) & (1 << (bit + 1)))
            shadow_bitmap |= (1 << (i + 16));

        mem_set_mem_state_both(base, 0x4000, state);
    }

    for (i = 0; i < 4; i++) {
        base = 0xe0000 + (i << 15);
        bit  = 6 - (i & 2);

        if ((base >= 0xe0000) && (base <= 0xe7fff)) {
            if (dev->regs[0x40] & 0x20)
                state = MEM_WRITE_DISABLED;
            else if ((dev->regs[0x32]) & (1 << bit))
                state = MEM_WRITE_INTERNAL;
            else
                state = (dev->regs[0x33] & 0x10) ? MEM_WRITE_ROMCS : MEM_WRITE_EXTERNAL;

            if ((dev->regs[0x32]) & (1 << (bit + 1)))
                state |= MEM_READ_INTERNAL;
            else
                state |= (dev->regs[0x33] & 0x10) ? MEM_READ_ROMCS : MEM_READ_EXTERNAL;
        } else if ((base >= 0xe8000) && (base <= 0xeffff)) {
            if (dev->regs[0x40] & 0x20)
                state = MEM_WRITE_DISABLED;
            else if ((dev->regs[0x32]) & (1 << bit))
                state = MEM_WRITE_INTERNAL;
            else
                state = (dev->regs[0x33] & 0x20) ? MEM_WRITE_ROMCS : MEM_WRITE_EXTERNAL;

            if ((dev->regs[0x32]) & (1 << (bit + 1)))
                state |= MEM_READ_INTERNAL;
            else
                state |= (dev->regs[0x33] & 0x20) ? MEM_READ_ROMCS : MEM_READ_EXTERNAL;
        } else {
            if (dev->regs[0x40] & 0x40)
                state = MEM_WRITE_DISABLED;
            else if ((dev->regs[0x32]) & (1 << bit))
                state = ((dev->regs[0x32]) & (1 << bit)) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;

            state |= ((dev->regs[0x32]) & (1 << (bit + 1))) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
        }

        vt82c49x_log("(32=%02X, %i) Setting %08X-%08X to: write %sabled, read %sabled\n",
                     dev->regs[0x32], bit, base, base + 0x7fff,
                     ((dev->regs[0x32]) & (1 << bit)) ? "en" : "dis", ((dev->regs[0x32]) & (1 << (bit + 1))) ? "en" : "dis");

        if ((dev->regs[0x32]) & (1 << bit)) {
            shadow_bitmap |= (0xf << ((i << 2) + 8));
            shadowbios_write |= 1;
        }
        if ((dev->regs[0x32]) & (1 << (bit + 1))) {
            shadow_bitmap |= (0xf << ((i << 2) + 24));
            shadowbios |= 1;
        }

        mem_set_mem_state_both(base, 0x8000, state);
    }

    vt82c49x_log("Shadow bitmap: %08X\n", shadow_bitmap);

    mem_remap_top(0);

    switch (relocate) {
        case 0x02:
            if (!(shadow_bitmap & 0xfff0fff0))
                mem_remap_top(256);
            break;
        case 0x03:
            if (!shadow_bitmap)
                mem_remap_top(384);
            break;
    }
}

static void
vt82c49x_write(uint16_t addr, uint8_t val, void *priv)
{
    vt82c49x_t *dev = (vt82c49x_t *) priv;
    uint8_t     valxor;

    switch (addr) {
        case 0xa8:
            dev->index = val;
            break;

        case 0xa9:
            valxor = (val ^ dev->regs[dev->index]);
            if (dev->index == 0x55)
                dev->regs[dev->index] &= ~val;
            else
                dev->regs[dev->index] = val;

            vt82c49x_log("dev->regs[0x%02x] = %02x\n", dev->index, val);

            switch (dev->index) {
                /* Wait States */
                case 0x03:
                    cpu_update_waitstates();
                    break;

                /* Shadow RAM and top of RAM relocation */
                case 0x30:
                case 0x31:
                case 0x32:
                case 0x33:
                case 0x40:
                    vt82c49x_recalc(dev);
                    break;

                /* External Cache Enable(Based on the 486-VC-HD BIOS) */
                case 0x50:
                    cpu_cache_ext_enabled = (val & 0x84);
                    break;

                /* Software SMI */
                case 0x54:
                    if ((dev->regs[0x5b] & 0x80) && (valxor & 0x01) && (val & 0x01)) {
                        if (dev->regs[0x5b] & 0x20)
                            smi_raise();
                        else
                            picint(1 << 15);
                        dev->regs[0x55] = 0x01;
                    }
                    break;

                /* SMRAM */
                case 0x5b:
                    smram_disable_all();

                    if (val & 0x80) {
                        smram_enable(dev->smram_smm, (val & 0x40) ? 0x00060000 : 0x00030000, 0x000a0000, 0x00020000,
                                     0, (val & 0x10));
                        smram_enable(dev->smram_high, 0x000a0000, 0x000a0000, 0x00020000,
                                     (val & 0x08), (val & 0x08));
                        smram_enable(dev->smram_low, 0x00030000, 0x000a0000, 0x00020000,
                                     (val & 0x02), 0);
                    }
                    break;

                /* Edge/Level IRQ Control */
                case 0x62:
                case 0x63:
                    if (dev->index == 0x63)
                        pic_elcr_write(dev->index, val & 0xde, &pic2);
                    else {
                        pic_elcr_write(dev->index, val & 0xf8, &pic);
                        pic_elcr_set_enabled(val & 0x01);
                    }
                    break;

                /* Local Bus IDE Controller */
                case 0x71:
                    if (dev->has_ide) {
                        ide_pri_disable();
                        ide_set_base(0, (val & 0x40) ? 0x170 : 0x1f0);
                        ide_set_side(0, (val & 0x40) ? 0x376 : 0x3f6);
                        if (val & 0x01)
                            ide_pri_enable();
                        vt82c49x_log("VT82C496 IDE now %sabled as %sary\n", (val & 0x01) ? "en" : "dis",
                                     (val & 0x40) ? "second" : "prim");
                    }
                    break;
            }
            break;
    }
}

static uint8_t
vt82c49x_read(uint16_t addr, void *priv)
{
    uint8_t     ret = 0xff;
    vt82c49x_t *dev = (vt82c49x_t *) priv;

    switch (addr) {
        case 0xa9:
            /* Register 64h is jumper readout. */
            if (dev->index == 0x64)
                ret = 0xff;
            else if (dev->index == 0x63)
                ret = pic_elcr_read(dev->index, &pic2) | (dev->regs[dev->index] & 0x01);
            else if (dev->index == 0x62)
                ret = pic_elcr_read(dev->index, &pic) | (dev->regs[dev->index] & 0x07);
            else if (dev->index < 0x80)
                ret = dev->regs[dev->index];
            break;
    }

    return ret;
}

static void
vt82c49x_reset(void *priv)
{
    uint16_t i;

    for (i = 0; i < 256; i++)
        vt82c49x_write(i, 0x00, priv);
}

static void
vt82c49x_close(void *priv)
{
    vt82c49x_t *dev = (vt82c49x_t *) priv;

    smram_del(dev->smram_high);
    smram_del(dev->smram_low);
    smram_del(dev->smram_smm);

    free(dev);
}

static void *
vt82c49x_init(const device_t *info)
{
    vt82c49x_t *dev = (vt82c49x_t *) malloc(sizeof(vt82c49x_t));
    memset(dev, 0x00, sizeof(vt82c49x_t));

    dev->smram_smm  = smram_add();
    dev->smram_low  = smram_add();
    dev->smram_high = smram_add();

    dev->has_ide = info->local & 1;
    if (dev->has_ide) {
        device_add(&ide_vlb_2ch_device);
        ide_sec_disable();
    }

    device_add(&port_92_device);

    io_sethandler(0x0a8, 0x0002, vt82c49x_read, NULL, NULL, vt82c49x_write, NULL, NULL, dev);

    pic_elcr_io_handler(0);
    pic_elcr_set_enabled(1);

    vt82c49x_recalc(dev);

    return dev;
}

const device_t via_vt82c49x_device = {
    .name          = "VIA VT82C49X",
    .internal_name = "via_vt82c49x",
    .flags         = 0,
    .local         = 0,
    .init          = vt82c49x_init,
    .close         = vt82c49x_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t via_vt82c49x_pci_device = {
    .name          = "VIA VT82C49X PCI",
    .internal_name = "via_vt82c49x_pci",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = vt82c49x_init,
    .close         = vt82c49x_close,
    .reset         = vt82c49x_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t via_vt82c49x_ide_device = {
    .name          = "VIA VT82C49X (With IDE)",
    .internal_name = "via_vt82c49x_ide",
    .flags         = 0,
    .local         = 1,
    .init          = vt82c49x_init,
    .close         = vt82c49x_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t via_vt82c49x_pci_ide_device = {
    .name          = "VIA VT82C49X PCI (With IDE)",
    .internal_name = "via_vt82c49x_pci_ide",
    .flags         = DEVICE_PCI,
    .local         = 1,
    .init          = vt82c49x_init,
    .close         = vt82c49x_close,
    .reset         = vt82c49x_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
