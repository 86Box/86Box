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

/* The live GART instances, so agpgart_bus_ptr can tell an aperture
   mapping from plain memory. Boards carry one; the array leaves room. */
#define AGPGART_MAX_INST 4
static agpgart_t *agpgart_inst[AGPGART_MAX_INST];

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

/* Host pointer for len bytes at bus address addr, all inside one 4 KB page,
   or NULL when they must be read through the handlers. For an address
   inside an enabled aperture the GART entry is read and the target page
   resolved once, exactly as agpgart_translate does per access; for any
   other address this is mem_phys_ptr. A pointer is valid until the memory
   map or the GART entry changes, so a caller holds it for one transfer. */
const uint8_t *
agpgart_bus_ptr(uint32_t addr, uint32_t len)
{
    const mem_mapping_t *map = mem_read_mapping_at(addr);
    const uint8_t       *entry;
    uint32_t             gart_ptr;
    uint32_t             off;

    if (!len || (((addr & 0xfff) + len) > 0x1000))
        return NULL;
    for (int i = 0; i < AGPGART_MAX_INST; i++) {
        agpgart_t *dev = agpgart_inst[i];

        if ((dev == NULL) || (map != &dev->aperture_mapping))
            continue;
        off   = addr & dev->aperture_mask;
        entry = mem_phys_ptr(dev->gart_base + ((off >> 10) & 0xfffffffc), 4);
        if (entry == NULL)
            return NULL;
        memcpy(&gart_ptr, entry, 4);
        return mem_phys_ptr((gart_ptr & 0xfffff000) | (off & 0x00000fff), len);
    }
    return mem_phys_ptr(addr, len);
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
    agpgart_t *dev = calloc(1, sizeof(agpgart_t));

    agpgart_log("AGP GART: init()\n");

    /* Create aperture mapping. */
    mem_mapping_add(&dev->aperture_mapping, 0, 0,
                    agpgart_aperture_readb, agpgart_aperture_readw, agpgart_aperture_readl,
                    agpgart_aperture_writeb, agpgart_aperture_writew, agpgart_aperture_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);

    for (int i = 0; i < AGPGART_MAX_INST; i++)
        if (agpgart_inst[i] == NULL) {
            agpgart_inst[i] = dev;
            break;
        }

    return dev;
}

static void
agpgart_close(void *priv)
{
    agpgart_t *dev = (agpgart_t *) priv;

    agpgart_log("AGP GART: close()\n");

    /* Disable aperture. */
    mem_mapping_disable(&dev->aperture_mapping);

    for (int i = 0; i < AGPGART_MAX_INST; i++)
        if (agpgart_inst[i] == dev)
            agpgart_inst[i] = NULL;

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
