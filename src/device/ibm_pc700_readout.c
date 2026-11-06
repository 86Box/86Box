/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the IBM PC 730/750 system board readout at
 *          ports 7Ch-7Fh, the "L2 Cache ID, tamper EEPROM, SMI/PCI IRQ
 *          enable" ports of the Technical Information Manual (52H3381,
 *          page 9).
 *
 *          The BIOS sizes the L2 cache from bits 3-0 of 7Ch, inverted:
 *          0Bh is the standard 256 KB of pipelined burst SRAM. It pulses
 *          bit 0 of 7Ch around writes to 3F7h.
 *
 * Authors: Avastrap2, <110710503+Avastrap2@users.noreply.github.com>
 *
 *          Copyright 2026 Avastrap2.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/plat_unused.h>

typedef struct ibm_pc700_readout_t {
    uint8_t regs[4];
} ibm_pc700_readout_t;

static uint8_t
ibm_pc700_readout_read(uint16_t port, void *priv)
{
    const ibm_pc700_readout_t *dev = (ibm_pc700_readout_t *) priv;

    return dev->regs[port & 0x03];
}

static void
ibm_pc700_readout_write(uint16_t port, uint8_t val, void *priv)
{
    ibm_pc700_readout_t *dev = (ibm_pc700_readout_t *) priv;

    if ((port & 0x03) == 0x00)
        dev->regs[0] = (dev->regs[0] & 0xfe) | (val & 0x01);
    else
        dev->regs[port & 0x03] = val;
}

static void
ibm_pc700_readout_reset(void *priv)
{
    ibm_pc700_readout_t *dev = (ibm_pc700_readout_t *) priv;

    memset(dev->regs, 0x00, sizeof(dev->regs));
    dev->regs[0] = 0x0b;
}

static void *
ibm_pc700_readout_init(UNUSED(const device_t *info))
{
    ibm_pc700_readout_t *dev = (ibm_pc700_readout_t *) calloc(1, sizeof(ibm_pc700_readout_t));

    io_sethandler(0x007c, 0x0004, ibm_pc700_readout_read, NULL, NULL, ibm_pc700_readout_write, NULL, NULL, dev);
    ibm_pc700_readout_reset(dev);

    return dev;
}

static void
ibm_pc700_readout_close(void *priv)
{
    ibm_pc700_readout_t *dev = (ibm_pc700_readout_t *) priv;

    free(dev);
}

const device_t ibm_pc700_readout_device = {
    .name          = "IBM PC 730/750 System Board Readout",
    .internal_name = "ibm_pc700_readout",
    .flags         = 0,
    .local         = 0,
    .init          = ibm_pc700_readout_init,
    .close         = ibm_pc700_readout_close,
    .reset         = ibm_pc700_readout_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
