/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          AGP Graphics Address Remapping Table remapping emulation.
 *
 *
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2021 RichardG.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/agpgart.h>
#include <86box/plat_unused.h>

#ifdef ENABLE_AGPGART_LOG
int agpgart_do_log = ENABLE_AGPGART_LOG;

static void
agpgart_log(const char *fmt, ...)
{
    va_list ap;

    if (agpgart_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define agpgart_log(fmt, ...)
#endif

void
agpgart_set_aperture(agpgart_t *dev, uint32_t base, uint32_t size, int enable)
{
    agpgart_log("AGP GART: set_aperture(%08X, %d, %d)\n", base, size, enable);

    /* Disable old aperture mapping. */
    mem_mapping_disable(&dev->aperture_mapping);

    /* Set new aperture base address, size, mask and enable. */
    dev->aperture_base   = base;
    dev->aperture_size   = size;
    dev->aperture_mask   = size - 1;
    dev->aperture_enable = enable;

    /* Enable new aperture mapping if requested. */
    if (dev->aperture_base && dev->aperture_size && dev->aperture_enable) {
        mem_mapping_set_addr(&dev->aperture_mapping, dev->aperture_base, dev->aperture_size);
        mem_mapping_enable(&dev->aperture_mapping);
    }
}

void
agpgart_set_gart(agpgart_t *dev, uint32_t base)
{
    agpgart_log("AGP GART: set_gart(%08X)\n", base);

    /* Set GART base address. */
    dev->gart_base = base;
}

static uint32_t
agpgart_translate(uint32_t addr, agpgart_t *dev)
{
    /* Extract the bits we care about. */
    addr &= dev->aperture_mask;

    /* Get the GART pointer for this page. */
    register uint32_t gart_ptr = mem_readl_phys(dev->gart_base + ((addr >> 10) & 0xfffffffc)) & 0xfffff000;

    /* Return remapped address with the page offset. */
    return gart_ptr | (addr & 0x00000fff);
}

static uint8_t
agpgart_aperture_readb(uint32_t addr, void *priv)
{
    agpgart_t *dev = (agpgart_t *) priv;
    return mem_readb_phys(agpgart_translate(addr, dev));
}

static uint16_t
agpgart_aperture_readw(uint32_t addr, void *priv)
{
    agpgart_t *dev = (agpgart_t *) priv;
    return mem_readw_phys(agpgart_translate(addr, dev));
}

static uint32_t
agpgart_aperture_readl(uint32_t addr, void *priv)
{
    agpgart_t *dev = (agpgart_t *) priv;
    return mem_readl_phys(agpgart_translate(addr, dev));
}

static void
agpgart_aperture_writeb(uint32_t addr, uint8_t val, void *priv)
{
    agpgart_t *dev = (agpgart_t *) priv;
    mem_writeb_phys(agpgart_translate(addr, dev), val);
}

static void
agpgart_aperture_writew(uint32_t addr, uint16_t val, void *priv)
{
    agpgart_t *dev = (agpgart_t *) priv;
    mem_writew_phys(agpgart_translate(addr, dev), val);
}

static void
agpgart_aperture_writel(uint32_t addr, uint32_t val, void *priv)
{
    agpgart_t *dev = (agpgart_t *) priv;
    mem_writel_phys(agpgart_translate(addr, dev), val);
}

static void *
agpgart_init(UNUSED(const device_t *info))
{
    agpgart_t *dev = malloc(sizeof(agpgart_t));
    memset(dev, 0, sizeof(agpgart_t));

    agpgart_log("AGP GART: init()\n");

    /* Create aperture mapping. */
    mem_mapping_add(&dev->aperture_mapping, 0, 0,
                    agpgart_aperture_readb, agpgart_aperture_readw, agpgart_aperture_readl,
                    agpgart_aperture_writeb, agpgart_aperture_writew, agpgart_aperture_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);

    return dev;
}

static void
agpgart_close(void *priv)
{
    agpgart_t *dev = (agpgart_t *) priv;

    agpgart_log("AGP GART: close()\n");

    /* Disable aperture. */
    mem_mapping_disable(&dev->aperture_mapping);

    free(dev);
}

const device_t agpgart_device = {
    .name          = "AGP Graphics Address Remapping Table",
    .internal_name = "agpgart",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = agpgart_init,
    .close         = agpgart_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
