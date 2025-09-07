/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the UMC 8890 Chipset.
 *
 *		Note: This chipset has no datasheet, everything were done via
 *		reverse engineering the BIOS of various machines using it.
 *
 * Authors: Tiseno100,
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2021 Tiseno100.
 *          Copyright 2021-2024 Miran Grca.
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

#include <86box/apm.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/port_92.h>
#include <86box/smram.h>

#include <86box/chipset.h>
#include <86box/plat_unused.h>

#ifdef ENABLE_UMC_8890_LOG
int umc_8890_do_log = ENABLE_UMC_8890_LOG;

static void
umc_8890_log(const char *fmt, ...)
{
    va_list ap;

    if (umc_8890_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define umc_8890_log(fmt, ...)
#endif

typedef struct umc_8890_t {
    uint8_t  pci_slot;

    uint8_t  pci_conf[256]; /* PCI Registers */

    int      mem_state[2];

    uint32_t smram_base;

    smram_t *smram;         /* SMRAM Handler */
} umc_8890_t;

static void
um8890_shadow(umc_8890_t *dev)
{
    uint8_t flag;
    uint16_t state;

    flag = (dev->pci_conf[0x5f] & 0x0c) >> 2;
    state = (flag & 1) ? (MEM_READ_INTERNAL | ((flag & 2) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL)) :
            (MEM_READ_EXTANY | MEM_WRITE_EXTANY);

    if ((dev->mem_state[1] ^ dev->pci_conf[0x5f]) & 0x0c) {
        mem_set_mem_state_both(0xe0000, 0x10000, state);
        dev->mem_state[1] = (dev->mem_state[1] & 0xf0) | (dev->pci_conf[0x5f] & 0x0f);
    }

    flag = (dev->pci_conf[0x5f] & 0xc0) >> 6;
    state = (flag & 1) ? (MEM_READ_INTERNAL | ((flag & 2) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL)) :
            (MEM_READ_EXTANY | MEM_WRITE_EXTANY);

    if ((dev->mem_state[1] ^ dev->pci_conf[0x5f]) & 0xc0) {
        mem_set_mem_state_both(0xf0000, 0x10000, state);
        dev->mem_state[1] = (dev->mem_state[1] & 0x0f) | (dev->pci_conf[0x5f] & 0xf0);
    }

    for (uint8_t i = 0; i < 8; i++) {
        state = (dev->pci_conf[0x5d] & (1 << i)) ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) :
                (MEM_READ_EXTANY | MEM_WRITE_EXTANY);

        if ((dev->mem_state[0] ^ dev->pci_conf[0x5d]) & (1 << i)) {
            mem_set_mem_state_both(0xc0000 + (i << 14), 0x4000, state);
            dev->mem_state[0] = (dev->mem_state[0] & ~(1 << i)) | (dev->pci_conf[0x5d] & (1 << i));
        }
    }

    flushmmucache_nopc();
}


static void
um8890_smram(umc_8890_t *dev)
{
    smram_disable_all();

    /* Bit 4, if set, enables SMRAM access outside SMM. SMRAM appears to be always enabled
       in SMM, and is always set to A0000-BFFFF. */
    smram_enable(dev->smram, 0x000a0000, 0x000a0000, 0x20000, dev->pci_conf[0x65] & 0x10, 1);
}

static void
um8890_write(int func, int addr, uint8_t val, void *priv)
{
    umc_8890_t *dev = (umc_8890_t *)priv;

    if (func == 0)  switch (addr) {
        case 0x04 ... 0x05:
        case 0x0c ... 0x0d:
        case 0x40 ... 0x5b:
        case 0x60 ... 0x63:
        case 0x66 ... 0xff:
            dev->pci_conf[addr] = val;
            break;

        case 0x07:
            dev->pci_conf[addr] &= ~(val & 0xf9);
            break;

        case 0x5c ... 0x5f:
            dev->pci_conf[addr] = val;
            um8890_shadow(dev);
            break;

        /* Register 64h, 16-bit:
               Bit     12: SMRAM enabled outside SMM (1 = yes, 0 = no);
               Bit     10: ???? (set by Award BIOS);
               Bits  7- 0: SMM handler offset to SMBASE, shifted to the right by 14.
         */
        case 0x64: case 0x65:
            dev->pci_conf[addr] = val;
            if (addr == 0x65)
                um8890_smram(dev);
            break;
    }

    umc_8890_log("UM8890: dev->regs[%02x] = %02x POST: %02x\n", addr, dev->pci_conf[addr], inb(0x80));
}


static uint8_t
um8890_read(int func, int addr, void *priv)
{
    umc_8890_t *dev = (umc_8890_t *)priv;
    uint8_t ret = 0xff;

    if (func == 0)
        ret = dev->pci_conf[addr];

    return ret;
}

static void
umc_8890_reset(void *priv)
{
    umc_8890_t *dev = (umc_8890_t *)priv;

    memset(dev->pci_conf, 0x00, sizeof(dev->pci_conf));

    /* Defaults */
    dev->pci_conf[0x00] = 0x60; /* UMC */
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x91; /* 8891F */
    dev->pci_conf[0x03] = 0x88;
    dev->pci_conf[0x07] = 0x02;
    dev->pci_conf[0x08] = 0x01;
    dev->pci_conf[0x09] = 0x00;
    dev->pci_conf[0x0a] = 0x00;
    dev->pci_conf[0x0b] = 0x06;
    dev->pci_conf[0x5c] = 0x00;
    dev->pci_conf[0x5d] = 0x00;
    dev->pci_conf[0x5e] = 0x00;
    dev->pci_conf[0x5f] = 0x00;
    dev->pci_conf[0x64] = 0x00;
    dev->pci_conf[0x65] = 0x00;

    um8890_shadow(dev);

    um8890_smram(dev);
}


static void
umc_8890_close(void *priv)
{
    umc_8890_t *dev = (umc_8890_t *)priv;

    smram_del(dev->smram);
    free(dev);
}


static void *
umc_8890_init(UNUSED(const device_t *info))
{
    umc_8890_t *dev = (umc_8890_t *) calloc(1, sizeof(umc_8890_t));

    /* Device 0: UMC 8890 */
    pci_add_card(PCI_ADD_NORTHBRIDGE, um8890_read, um8890_write, dev, &dev->pci_slot);

    /* Port 92 */
    device_add(&port_92_pci_device);

    dev->smram = smram_add();

    umc_8890_reset(dev);

    return dev;
}

const device_t umc_8890_device = {
    .name          = "UMC 8890(8891BF/8892BF)",
    .internal_name = "umc_8890",
    .flags         = DEVICE_PCI,
    .local         = 0x886a,
    .init          = umc_8890_init,
    .close         = umc_8890_close,
    .reset         = umc_8890_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
