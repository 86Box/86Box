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
 *
 * Authors:	Tiseno100,
 *
 *		Copyright 2021 Tiseno100.
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

#ifdef ENABLE_UMC_8890_LOG
int umc_8890_do_log = ENABLE_UMC_8890_LOG;
static void
umc_8890_log(const char *fmt, ...)
{
    va_list ap;

    if (umc_8890_do_log)
    {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define umc_8890_log(fmt, ...)
#endif

/* Shadow RAM Flags */
#define ENABLE_SHADOW (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL)
#define DISABLE_SHADOW (MEM_READ_EXTANY | MEM_WRITE_EXTANY)

typedef struct umc_8890_t
{
    apm_t *apm;
    smram_t *smram;

    uint8_t pci_conf[256];
} umc_8890_t;

uint16_t umc_8890_shadow_flag(uint8_t flag)
{
return (flag & 1) ? (MEM_READ_INTERNAL | ((flag & 2) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL)) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY);
}

void umc_8890_shadow(umc_8890_t *dev)
{

    mem_set_mem_state_both(0xe0000, 0x10000, umc_8890_shadow_flag((dev->pci_conf[0x5f] & 0x0c) >> 2));
    mem_set_mem_state_both(0xf0000, 0x10000, umc_8890_shadow_flag((dev->pci_conf[0x5f] & 0xc0) >> 6));

    for(int i = 0; i < 8; i++)
    mem_set_mem_state_both(0xc0000 + (i << 14), 0x4000, umc_8890_shadow_flag(!!(dev->pci_conf[0x5d] & (1 << i))));

    flushmmucache_nopc();
}

static void
um8890_write(int func, int addr, uint8_t val, void *priv)
{
    umc_8890_t *dev = (umc_8890_t *)priv;

    dev->pci_conf[addr] = val;

    switch (addr)
    {
    case 0x5c:
    case 0x5d:
    case 0x5e:
    case 0x5f:
        umc_8890_shadow(dev);
        break;

    case 0x65: /* We don't know the default SMRAM values */
        smram_disable_all();
        smram_enable(dev->smram, 0xe0000, 0xe0000, 0x10000, dev->pci_conf[0x65] & 0x10, 1);
        flushmmucache_nopc();
        break;
    }

    umc_8890_log("UM8890: dev->regs[%02x] = %02x POST: %02x\n", addr, dev->pci_conf[addr], inb(0x80));
}

static uint8_t
um8890_read(int func, int addr, void *priv)
{
    umc_8890_t *dev = (umc_8890_t *)priv;
    return dev->pci_conf[addr];
}

static void
umc_8890_reset(void *priv)
{
    umc_8890_t *dev = (umc_8890_t *)priv;

    /* Defaults */
    dev->pci_conf[0] = 0x60; /* UMC */
    dev->pci_conf[1] = 0x10;

    dev->pci_conf[2] = 0x91; /* 8891F */
    dev->pci_conf[3] = 0x88;

    dev->pci_conf[8] = 1;

    dev->pci_conf[0x09] = 0x00;
    dev->pci_conf[0x0a] = 0x00;
    dev->pci_conf[0x0b] = 0x06;
}

static void
umc_8890_close(void *priv)
{
    umc_8890_t *dev = (umc_8890_t *)priv;

    smram_del(dev->smram);
    free(dev);
}

static void *
umc_8890_init(const device_t *info)
{
    umc_8890_t *dev = (umc_8890_t *)malloc(sizeof(umc_8890_t));
    memset(dev, 0, sizeof(umc_8890_t));
    pci_add_card(PCI_ADD_NORTHBRIDGE, um8890_read, um8890_write, dev); /* Device 0: UMC 8890 */

    /* APM */
    dev->apm = device_add(&apm_pci_device);

    /* SMRAM(Needs excessive documentation before we begin SMM implementation) */
    dev->smram = smram_add();

    /* Port 92 */
    device_add(&port_92_pci_device);

    umc_8890_reset(dev);

    return dev;
}

const device_t umc_8890_device = {
    "UMC 8890(8891BF/8892BF)",
    DEVICE_PCI,
    0x886a,
    umc_8890_init,
    umc_8890_close,
    umc_8890_reset,
    {NULL},
    NULL,
    NULL,
    NULL};
