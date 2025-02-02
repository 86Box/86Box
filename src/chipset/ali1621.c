/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the ALi M1621/2 CPU-to-PCI Bridge.
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
#include <86box/timer.h>

#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>
#include <86box/smram.h>
#include <86box/spd.h>

#include <86box/chipset.h>

typedef struct ali1621_t {
    uint8_t pci_slot;
    uint8_t pad;
    uint8_t pad0;
    uint8_t pad1;

    uint8_t pci_conf[256];

    smram_t *smram[2];
} ali1621_t;

#ifdef ENABLE_ALI1621_LOG
int ali1621_do_log = ENABLE_ALI1621_LOG;

static void
ali1621_log(const char *fmt, ...)
{
    va_list ap;

    if (ali1621_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ali1621_log(fmt, ...)
#endif

/* Table translated to a more sensible format:
        Read cycles:
        SMREN    SMM Mode    Code    Data
        0   X    X           PCI     PCI
        1   0    Close       PCI     PCI
        1   0    Lock        PCI     PCI
        1   0    Protect     PCI     PCI
        1   0    Open        DRAM    DRAM
        1   1    Open        DRAM    DRAM
        1   1    Protect     DRAM    DRAM
        1   1    Close       DRAM    PCI
        1   1    Lock        DRAM    PCI

        Write cycles:
        SMWEN    SMM Mode    Data
        0   X    X           PCI
        1   0    Close       PCI
        1   0    Lock        PCI
        1   0    Protect     PCI
        1   0    Open        DRAM
        1   1    Open        DRAM
        1   1    Protect     DRAM
        1   1    Close       PCI
        1   1    Lock        PCI

        Explanation of the modes based above:
                If SM*EN = 0, SMRAM is entirely disabled, otherwise:
                        If mode is Close or Lock, then SMRAM always goes to PCI outside SMM,
                        and data to PCI, code to DRAM in SMM;
                        If mode is Protect, then SMRAM always goes to PCI outside SMM and
                        DRAM in SMM;
                        If mode is Open, then SMRAM always goes to DRAM.
                Read and write are enabled separately.
 */
static void
ali1621_smram_recalc(uint8_t val, ali1621_t *dev)
{
    uint16_t access_smm    = 0x0000;
    uint16_t access_normal = 0x0000;

    smram_disable_all();

    if (val & 0xc0) {
        /* SMRAM 0: A0000-BFFFF */
        if (val & 0x80) {
            access_smm = ACCESS_SMRAM_X;

            switch (val & 0x30) {
                case 0x10: /* Open. */
                    access_normal = ACCESS_SMRAM_RX;
                    fallthrough;
                case 0x30: /* Protect. */
                    access_smm |= ACCESS_SMRAM_R;
                    break;
                default:
                    break;
            }
        }

        if (val & 0x40)
            switch (val & 0x30) {
                case 0x10: /* Open. */
                    access_normal |= ACCESS_SMRAM_W;
                    fallthrough;
                case 0x30: /* Protect. */
                    access_smm |= ACCESS_SMRAM_W;
                    break;
                default:
                    break;
            }

        smram_enable(dev->smram[0], 0xa0000, 0xa0000, 0x20000, ((val & 0x30) == 0x10), (val & 0x30));

        mem_set_access(ACCESS_NORMAL, 3, 0xa0000, 0x20000, access_normal);
        mem_set_access(ACCESS_SMM, 3, 0xa0000, 0x20000, access_smm);
    }

    if (val & 0x08)
        smram_enable(dev->smram[1], 0x38000, 0xa8000, 0x08000, 0, 1);

    flushmmucache_nopc();
}

static void
ali1621_shadow_recalc(UNUSED(int cur_reg), ali1621_t *dev)
{
    int      r_bit;
    int      w_bit;
    int      reg;
    uint32_t base;
    uint32_t flags = 0;

    shadowbios = shadowbios_write = 0;

    /* C0000-EFFFF */
    for (uint8_t i = 0; i < 12; i++) {
        base  = 0x000c0000 + (i << 14);
        r_bit = (i << 1) + 4;
        reg   = 0x84;
        if (r_bit > 23) {
            r_bit &= 7;
            reg += 3;
        } else if (r_bit > 15) {
            r_bit &= 7;
            reg += 2;
        } else if (r_bit > 7) {
            r_bit &= 7;
            reg++;
        }
        w_bit = r_bit + 1;

        flags = (dev->pci_conf[reg] & (1 << r_bit)) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
        flags |= ((dev->pci_conf[reg] & (1 << w_bit)) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY);

        if (base >= 0x000e0000) {
            if (dev->pci_conf[reg] & (1 << r_bit))
                shadowbios |= 1;
            if (dev->pci_conf[reg] & (1 << r_bit))
                shadowbios_write |= 1;
        }

        ali1621_log("%08X-%08X shadow: R%c, W%c\n", base, base + 0x00003fff,
                    (dev->pci_conf[reg] & (1 << r_bit)) ? 'I' : 'E', (dev->pci_conf[reg] & (1 << w_bit)) ? 'I' : 'E');
        mem_set_mem_state_both(base, 0x00004000, flags);
    }

    /* F0000-FFFFF */
    base  = 0x000f0000;
    r_bit = 4;
    w_bit = 5;
    reg   = 0x87;

    flags = (dev->pci_conf[reg] & (1 << r_bit)) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
    flags |= ((dev->pci_conf[reg] & (1 << w_bit)) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY);

    if (dev->pci_conf[reg] & (1 << r_bit))
        shadowbios |= 1;
    if (dev->pci_conf[reg] & (1 << r_bit))
        shadowbios_write |= 1;

    ali1621_log("%08X-%08X shadow: R%c, W%c\n", base, base + 0x0000ffff,
                (dev->pci_conf[reg] & (1 << r_bit)) ? 'I' : 'E', (dev->pci_conf[reg] & (1 << w_bit)) ? 'I' : 'E');
    mem_set_mem_state_both(base, 0x00010000, flags);

    flushmmucache_nopc();
}

static void
ali1621_mask_bar(ali1621_t *dev)
{
    uint32_t bar;
    uint32_t mask;

    switch (dev->pci_conf[0xbc] & 0x0f) {
        default:
        case 0x00:
            mask = 0x00000000;
            break;
        case 0x01:
            mask = 0xfff00000;
            break;
        case 0x02:
            mask = 0xffe00000;
            break;
        case 0x03:
            mask = 0xffc00000;
            break;
        case 0x04:
            mask = 0xff800000;
            break;
        case 0x06:
            mask = 0xff000000;
            break;
        case 0x07:
            mask = 0xfe000000;
            break;
        case 0x08:
            mask = 0xfc000000;
            break;
        case 0x09:
            mask = 0xf8000000;
            break;
        case 0x0a:
            mask = 0xf0000000;
            break;
    }

    bar                 = ((dev->pci_conf[0x13] << 24) | (dev->pci_conf[0x12] << 16)) & mask;
    dev->pci_conf[0x12] = (bar >> 16) & 0xff;
    dev->pci_conf[0x13] = (bar >> 24) & 0xff;
}

static void
ali1621_write(UNUSED(int func), int addr, uint8_t val, void *priv)
{
    ali1621_t *dev = (ali1621_t *) priv;

    switch (addr) {
        case 0x04:
            dev->pci_conf[addr] = val & 0x01;
            break;
        case 0x05:
            dev->pci_conf[addr] = val & 0x01;
            break;

        case 0x07:
            dev->pci_conf[addr] &= ~(val & 0xf0);
            break;

        case 0x0d:
            dev->pci_conf[addr] = val & 0xf8;
            break;

        case 0x12:
            dev->pci_conf[0x12] = (val & 0xc0);
            ali1621_mask_bar(dev);
            break;
        case 0x13:
            dev->pci_conf[0x13] = val;
            ali1621_mask_bar(dev);
            break;

        case 0x34:
            dev->pci_conf[addr] = val;
            break;

        case 0x40:
            dev->pci_conf[addr] = val;
            break;
        case 0x41:
            dev->pci_conf[addr] = val;
            break;

        case 0x42:
            dev->pci_conf[addr] = val;
            break;
        case 0x43:
            dev->pci_conf[addr] = val;
            break;

        case 0x44:
            dev->pci_conf[addr] = val;
            break;
        case 0x45:
            dev->pci_conf[addr] = val;
            break;

        case 0x46:
            dev->pci_conf[addr] = val;
            break;
        case 0x47:
            dev->pci_conf[addr] = val;
            break;

        case 0x48:
            dev->pci_conf[addr] = val;
            break;
        case 0x49:
            dev->pci_conf[addr] = val;
            break;

        case 0x4a:
            dev->pci_conf[addr] = val;
            break;

        case 0x4b:
            dev->pci_conf[addr] = val & 0x0f;
            break;

        case 0x4c:
            dev->pci_conf[addr] = val;
            break;

        case 0x4d:
            dev->pci_conf[addr] = val;
            break;

        case 0x4e:
            dev->pci_conf[addr] = val;
            break;
        case 0x4f:
            dev->pci_conf[addr] = val;
            break;

        case 0x50:
            dev->pci_conf[addr] = val & 0xef;
            break;

        case 0x51:
            dev->pci_conf[addr] = val;
            break;

        case 0x52:
            dev->pci_conf[addr] = val & 0x9f;
            break;

        case 0x53:
            dev->pci_conf[addr] = val;
            break;

        case 0x54:
            dev->pci_conf[addr] = val & 0xb4;
            break;
        case 0x55:
            dev->pci_conf[addr] = val & 0x01;
            break;

        case 0x56:
            dev->pci_conf[addr] = val & 0x3f;
            break;

        case 0x57:
            dev->pci_conf[addr] = val & 0x08;
            break;

        case 0x58:
            dev->pci_conf[addr] = val;
            break;

        case 0x59:
            dev->pci_conf[addr] = val;
            break;

        case 0x5a:
            dev->pci_conf[addr] = val;
            break;

        case 0x5c:
            dev->pci_conf[addr] = val & 0x01;
            break;

        case 0x60:
            dev->pci_conf[addr] = val;
            break;

        case 0x61:
            dev->pci_conf[addr] = val;
            break;

        case 0x62:
            dev->pci_conf[addr] = val;
            break;

        case 0x63:
            dev->pci_conf[addr] = val;
            break;

        case 0x64:
            dev->pci_conf[addr] = val & 0xb7;
            break;
        case 0x65:
            dev->pci_conf[addr] = val & 0x01;
            break;

        case 0x66:
            dev->pci_conf[addr] &= ~(val & 0x33);
            break;

        case 0x67:
            dev->pci_conf[addr] = val;
            break;

        case 0x68:
            dev->pci_conf[addr] = val;
            break;

        case 0x69:
            dev->pci_conf[addr] = val;
            break;

        case 0x6c ... 0x7b:
            /* Bits 22:20 = DRAM Row size:
                    - 000: 4 MB;
                    - 001: 8 MB;
                    - 010: 16 MB;
                    - 011: 32 MB;
                    - 100: 64 MB;
                    - 101: 128 MB;
                    - 110: 256 MB;
                    - 111: Reserved. */
            dev->pci_conf[addr] = val;
            spd_write_drbs_ali1621(dev->pci_conf, 0x6c, 0x7b);
            break;

        case 0x7c ... 0x7f:
            dev->pci_conf[addr] = val;
            break;

        case 0x80:
            dev->pci_conf[addr] = val;
            break;
        case 0x81:
            dev->pci_conf[addr] = val & 0xdf;
            break;

        case 0x82:
            dev->pci_conf[addr] = val & 0xf7;
            break;

        case 0x83:
            dev->pci_conf[addr] = val & 0xfc;
            ali1621_smram_recalc(val & 0xfc, dev);
            break;

        case 0x84 ... 0x87:
            if (addr == 0x87)
                dev->pci_conf[addr] = val & 0x3f;
            else
                dev->pci_conf[addr] = val;
            ali1621_shadow_recalc(val, dev);
            break;

        case 0x88:
        case 0x89:
            dev->pci_conf[addr] = val;
            break;
        case 0x8a:
            dev->pci_conf[addr] = val & 0xc5;
            break;
        case 0x8b:
            dev->pci_conf[addr] = val & 0xbf;
            break;

        case 0x8c ... 0x8f:
            dev->pci_conf[addr] = val;
            break;

        case 0x90:
            dev->pci_conf[addr] = val;
            break;
        case 0x91:
            dev->pci_conf[addr] = val & 0x07;
            break;

        case 0x94 ... 0x97:
            dev->pci_conf[addr] = val;
            break;

        case 0x98 ... 0x9b:
            dev->pci_conf[addr] = val;
            break;

        case 0x9c ... 0x9f:
            dev->pci_conf[addr] = val;
            break;

        case 0xa0:
        case 0xa1:
            dev->pci_conf[addr] = val;
            break;

        case 0xbc:
            dev->pci_conf[addr] = val & 0x0f;
            ali1621_mask_bar(dev);
            break;
        case 0xbd:
            dev->pci_conf[addr] = val & 0xf0;
            break;
        case 0xbe:
        case 0xbf:
            dev->pci_conf[addr] = val;
            break;

        case 0xc0:
            dev->pci_conf[addr] = val & 0xb1;
            break;

        case 0xc4 ... 0xc7:
            dev->pci_conf[addr] = val;
            break;

        case 0xc8:
            dev->pci_conf[addr] = val & 0x8c;
            break;
        case 0xc9:
            dev->pci_conf[addr] = val;
            break;
        case 0xca:
            dev->pci_conf[addr] = val & 0x7f;
            break;
        case 0xcb:
            dev->pci_conf[addr] = val & 0x87;
            break;

        case 0xcc ... 0xcf:
            dev->pci_conf[addr] = val;
            break;

        case 0xd0:
            dev->pci_conf[addr] = val & 0x80;
            break;
        case 0xd2:
            dev->pci_conf[addr] = val & 0x40;
            break;
        case 0xd3:
            dev->pci_conf[addr] = val & 0xb0;
            break;

        case 0xd4:
            dev->pci_conf[addr] = val;
            break;
        case 0xd5:
            dev->pci_conf[addr] = val & 0xef;
            break;
        case 0xd6:
        case 0xd7:
            dev->pci_conf[addr] = val;
            break;

        case 0xf0 ... 0xff:
            dev->pci_conf[addr] = val;
            break;

        default:
            break;
    }
}

static uint8_t
ali1621_read(UNUSED(int func), int addr, void *priv)
{
    const ali1621_t *dev = (ali1621_t *) priv;
    uint8_t          ret = 0xff;

    ret = dev->pci_conf[addr];

    return ret;
}

static void
ali1621_reset(void *priv)
{
    ali1621_t *dev = (ali1621_t *) priv;

    /* Default Registers */
    dev->pci_conf[0x00] = 0xb9;
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x21;
    dev->pci_conf[0x03] = 0x16;
    dev->pci_conf[0x04] = 0x06;
    dev->pci_conf[0x05] = 0x00;
    dev->pci_conf[0x06] = 0x10;
    dev->pci_conf[0x07] = 0x04;
    dev->pci_conf[0x08] = 0x01;
    dev->pci_conf[0x09] = 0x00;
    dev->pci_conf[0x0a] = 0x00;
    dev->pci_conf[0x0b] = 0x06;
    dev->pci_conf[0x10] = 0x08;
    dev->pci_conf[0x34] = 0xb0;
    dev->pci_conf[0x40] = 0x0c;
    dev->pci_conf[0x41] = 0x0c;
    dev->pci_conf[0x4c] = 0x04;
    dev->pci_conf[0x4d] = 0x04;
    dev->pci_conf[0x4e] = 0x7f;
    dev->pci_conf[0x4f] = 0x7f;
    dev->pci_conf[0x50] = 0x0c;
    dev->pci_conf[0x53] = 0x02;
    dev->pci_conf[0x5a] = 0x02;
    dev->pci_conf[0x63] = 0x02;
    dev->pci_conf[0x6c] = dev->pci_conf[0x70] = dev->pci_conf[0x74] = dev->pci_conf[0x78] = 0xff;
    dev->pci_conf[0x6d] = dev->pci_conf[0x71] = dev->pci_conf[0x75] = dev->pci_conf[0x79] = 0xff;
    dev->pci_conf[0x6e] = dev->pci_conf[0x72] = dev->pci_conf[0x76] = dev->pci_conf[0x7a] = 0x00;
    dev->pci_conf[0x6f] = dev->pci_conf[0x73] = dev->pci_conf[0x77] = dev->pci_conf[0x7b] = 0xe0;
    dev->pci_conf[0x6f] |= 0x06;
    dev->pci_conf[0x7c] = 0x11;
    dev->pci_conf[0x7d] = 0xc4;
    dev->pci_conf[0x7e] = 0xc7;
    dev->pci_conf[0x80] = 0x01;
    dev->pci_conf[0x81] = 0xc0;
    dev->pci_conf[0x8e] = 0x01;
    dev->pci_conf[0xa0] = 0x20;
    dev->pci_conf[0xb0] = 0x02;
    dev->pci_conf[0xb2] = 0x10;
    dev->pci_conf[0xb4] = 0x03;
    dev->pci_conf[0xb5] = 0x02;
    dev->pci_conf[0xb7] = 0x20;
    dev->pci_conf[0xc0] = 0x80;
    dev->pci_conf[0xc9] = 0x28;
    dev->pci_conf[0xd4] = 0x10;
    dev->pci_conf[0xd5] = 0x01;
    dev->pci_conf[0xf0] = dev->pci_conf[0xf4] = dev->pci_conf[0xf8] = dev->pci_conf[0xfc] = 0x20;
    dev->pci_conf[0xf1] = dev->pci_conf[0xf5] = dev->pci_conf[0xf9] = dev->pci_conf[0xfd] = 0x43;
    dev->pci_conf[0xf2] = dev->pci_conf[0xf6] = dev->pci_conf[0xfa] = dev->pci_conf[0xfe] = 0x21;
    dev->pci_conf[0xf3] = dev->pci_conf[0xf7] = dev->pci_conf[0xfb] = dev->pci_conf[0xff] = 0x43;

    ali1621_write(0, 0x83, 0x08, dev);

    for (uint8_t i = 0; i < 4; i++)
        ali1621_write(0, 0x84 + i, 0x00, dev);
}

static void
ali1621_close(void *priv)
{
    ali1621_t *dev = (ali1621_t *) priv;

    smram_del(dev->smram[1]);
    smram_del(dev->smram[0]);

    free(dev);
}

static void *
ali1621_init(UNUSED(const device_t *info))
{
    ali1621_t *dev = (ali1621_t *) calloc(1, sizeof(ali1621_t));

    pci_add_card(PCI_ADD_NORTHBRIDGE, ali1621_read, ali1621_write, dev, &dev->pci_slot);

    dev->smram[0] = smram_add();
    dev->smram[1] = smram_add();

    ali1621_reset(dev);

    device_add(&ali5247_agp_device);

    return dev;
}

const device_t ali1621_device = {
    .name          = "ALi M1621 CPU-to-PCI Bridge",
    .internal_name = "ali1621",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = ali1621_init,
    .close         = ali1621_close,
    .reset         = ali1621_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
