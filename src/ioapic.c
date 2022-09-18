/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Skeleton I/O APIC implementation, currently housing the MPS
 *		table patcher for machines that require it.
 *
 *
 *
 * Author:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2020 RichardG.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/chipset.h>

typedef struct {
    uint8_t dummy;
} ioapic_t;

#ifdef ENABLE_IOAPIC_LOG
int ioapic_do_log = ENABLE_IOAPIC_LOG;

static void
ioapic_log(const char *fmt, ...)
{
    va_list ap;

    if (ioapic_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ioapic_log(fmt, ...)
#endif

static void
ioapic_write(uint16_t port, uint8_t val, void *priv)
{
    uint32_t addr, pcmp;

    /* target POST FF, issued by Award before jumping to the bootloader */
    if (val != 0xff)
        return;

    ioapic_log("IOAPIC: Caught POST %02X\n", val);

    /* The _MP_ table must be located in the BIOS area, the EBDA, or the last 1k of conventional
       memory; at a 16-byte boundary in all cases. Award writes both tables to the BIOS area. */
    for (addr = 0xf0000; addr <= 0xfffff; addr += 16) {
        /* check signature for the _MP_ table (Floating Point Structure) */
        if (mem_readl_phys(addr) != 0x5f504d5f) /* ASCII "_MP_" */
            continue;

        /* read and check pointer to the PCMP table (Configuration Table) */
        pcmp = mem_readl_phys(addr + 4);
        if ((pcmp < 0xf0000) || (pcmp > 0xfffff) || (mem_readl_phys(pcmp) != 0x504d4350)) /* ASCII "PCMP" */
            continue;

        /* patch over the signature on both tables */
        ioapic_log("IOAPIC: Patching _MP_ [%08x] and PCMP [%08x] tables\n", addr, pcmp);
        ram[addr] = ram[addr + 1] = ram[addr + 2] = ram[addr + 3] = 0xff;
        ram[pcmp] = ram[pcmp + 1] = ram[pcmp + 2] = ram[pcmp + 3] = 0xff;

        break;
    }
}

static void
ioapic_reset(ioapic_t *dev)
{
}

static void
ioapic_close(void *priv)
{
    ioapic_t *dev = (ioapic_t *) priv;

    io_removehandler(0x80, 1,
                     NULL, NULL, NULL, ioapic_write, NULL, NULL, NULL);

    free(dev);
}

static void *
ioapic_init(const device_t *info)
{
    ioapic_t *dev = (ioapic_t *) malloc(sizeof(ioapic_t));
    memset(dev, 0, sizeof(ioapic_t));

    ioapic_reset(dev);

    io_sethandler(0x80, 1,
                  NULL, NULL, NULL, ioapic_write, NULL, NULL, NULL);

    return dev;
}

const device_t ioapic_device = {
    .name          = "I/O Advanced Programmable Interrupt Controller",
    .internal_name = "ioapic",
    .flags         = DEVICE_AT,
    .local         = 0,
    .init          = ioapic_init,
    .close         = ioapic_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
