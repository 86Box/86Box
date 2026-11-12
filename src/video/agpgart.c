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

#define AGPGART_NOWHERE 0xffffffff

/* The table's place or length changed; mapping it again waits for the
   next access on the CPU thread. The pointer is cleared before the
   generation is published, so a reader that sees the new generation sees
   no pointer. */
static void
agpgart_gart_invalidate(agpgart_t *dev)
{
    atomic_store_explicit(&dev->gart_ptr, NULL, memory_order_relaxed);
    atomic_store_explicit(&dev->gart_gen, mem_mapping_gen - 1, memory_order_release);
}

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

    agpgart_gart_invalidate(dev);
}

void
agpgart_set_gart(agpgart_t *dev, uint32_t base)
{
    agpgart_log("AGP GART: set_gart(%08X)\n", base);

    /* Set GART base address. */
    dev->gart_base = base;

    agpgart_gart_invalidate(dev);
}

/* Entries are re-read on every access, so a table the guest rewrites
   needs no flush here; only the memory map can move it. The table is
   taken only where every granule of it is the RAM array at that granule's
   own address: those slots change on the CPU thread alone (SMRAM,
   shadowing, remaps), while a device may rebuild its own mapping from its
   own thread, and a device-owned ROM or RAM holding the table could move
   under a pointer taken here. The walk holds the map lock, so no rebuild
   from any thread is half done while it looks, and the generation it
   records is the one the map had throughout. CPU thread only. */
static __attribute__((noinline)) void
agpgart_gart_recalc(agpgart_t *dev)
{
    uint32_t       len  = ((dev->aperture_mask >> 10) & 0xfffffffc) + 4;
    const uint8_t *base = NULL;
    const uint8_t *p;
    uint32_t       done;
    uint32_t       addr;
    uint32_t       chunk;

    mem_mapping_lock();

    /* A reader that sees this generation sees the NULL stored before it,
       then at most the pointer derived for it, never an older one. The
       RAM array's end is the same under every generation, so a reader
       may pair it with any pointer. */
    atomic_store_explicit(&dev->gart_ptr, NULL, memory_order_relaxed);
    atomic_store_explicit(&dev->gart_end, ram + mem_ram_size(), memory_order_relaxed);
    atomic_store_explicit(&dev->gart_gen, mem_mapping_gen, memory_order_release);

    /* A table running past 4 GB wraps on the bus. */
    if (((uint64_t) dev->gart_base + len) <= 0x100000000ULL) {
        for (done = 0; done < len; done += chunk) {
            addr  = dev->gart_base + done;
            chunk = MEM_GRANULARITY_SIZE - (addr & MEM_GRANULARITY_MASK);
            if (chunk > (len - done))
                chunk = len - done;
            p = mem_phys_ptr(addr, chunk);
            if ((p == NULL) || ((uintptr_t) p != ((uintptr_t) ram + addr))) {
                base = NULL;
                break;
            }
            if (done == 0)
                base = p;
        }

        if (base != NULL)
            atomic_store_explicit(&dev->gart_ptr, base, memory_order_relaxed);
    }

    mem_mapping_unlock();
}

/* The live GART instances, so agpgart_bus_ptr can tell an aperture
   mapping from plain memory. Boards carry one; the array leaves room. */
#define AGPGART_MAX_INST 4
static agpgart_t *agpgart_inst[AGPGART_MAX_INST];

/* The bus address an aperture offset maps to through its GART entry. An
   entry pointing back into the aperture would have us translate our own
   accesses forever; such a page reads all ones and drops writes. */
static inline uint32_t
agpgart_remap(const agpgart_t *dev, uint32_t entry, uint32_t addr)
{
    entry &= 0xfffff000;
    if (((entry - dev->aperture_base) & ~dev->aperture_mask) == 0)
        return AGPGART_NOWHERE;

    /* Return remapped address with the page offset. */
    return entry | (addr & 0x00000fff);
}

static uint32_t
agpgart_translate(uint32_t addr, agpgart_t *dev)
{
    const uint8_t *p;
    uint32_t       off;
    uint32_t       entry;

    /* Extract the bits we care about. */
    addr &= dev->aperture_mask;
    off = (addr >> 10) & 0xfffffffc;

    /* Get the GART pointer for this page. Any thread may read through the
       pointer while its generation is current; the bytes are the RAM
       array's, the same the bus read returns while the map holds. Only the
       CPU thread derives it again: a device thread that finds the
       generation moved reads the entry through the bus. Around a rebuild a
       device thread, which takes no lock on the map in either path, may
       follow the old map for the accesses that overlap the rebuild's
       stores, as its bus read could follow a slot not yet rewritten. */
    if (atomic_load_explicit(&dev->gart_gen, memory_order_acquire) == mem_mapping_gen) {
        p = atomic_load_explicit(&dev->gart_ptr, memory_order_relaxed);
        if ((p != NULL) && (((uintptr_t) p + off + 4) <= (uintptr_t) atomic_load_explicit(&dev->gart_end, memory_order_relaxed))) {
            entry = *(const uint32_t *) &(p[off]);
            return agpgart_remap(dev, entry, addr);
        }
    } else if (is_cpu_thread) {
        agpgart_gart_recalc(dev);
        p = atomic_load_explicit(&dev->gart_ptr, memory_order_relaxed);
        if ((p != NULL) && (((uintptr_t) p + off + 4) <= (uintptr_t) atomic_load_explicit(&dev->gart_end, memory_order_relaxed))) {
            entry = *(const uint32_t *) &(p[off]);
            return agpgart_remap(dev, entry, addr);
        }
    }

    entry = mem_readl_phys(dev->gart_base + off);
    return agpgart_remap(dev, entry, addr);
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
        if ((((gart_ptr & 0xfffff000) - dev->aperture_base) & ~dev->aperture_mask) == 0)
            return NULL;
        return mem_phys_ptr((gart_ptr & 0xfffff000) | (off & 0x00000fff), len);
    }
    return mem_phys_ptr(addr, len);
}

/* For a CPU write at bus address addr through mapping map: when map is an
   aperture whose table is mapped at the current generation and the entry
   for addr points outside the aperture, the entry's host address, with
   the generation it holds under in *gen, the entry's value in *val and
   the bus address that value sends the write to in *ra; NULL otherwise.
   The entry is read once: a device may rewrite it at any time. While *gen
   and the entry keep their values, agpgart_translate() finds the same
   entry and the same address. The caller holds the map lock. CPU thread
   only. */
const uint32_t *
agpgart_cpu_entry(const mem_mapping_t *map, uint32_t addr, _Atomic uint32_t **gen, uint32_t *val, uint32_t *ra)
{
    const uint8_t *p;
    uint32_t       off;

    for (int i = 0; i < AGPGART_MAX_INST; i++) {
        agpgart_t *dev = agpgart_inst[i];

        if ((dev == NULL) || (map != &dev->aperture_mapping))
            continue;
        if (atomic_load_explicit(&dev->gart_gen, memory_order_acquire) != mem_mapping_gen)
            return NULL;
        addr &= dev->aperture_mask;
        off = (addr >> 10) & 0xfffffffc;
        p   = atomic_load_explicit(&dev->gart_ptr, memory_order_relaxed);
        if ((p == NULL) || (((uintptr_t) p + off + 4) > (uintptr_t) atomic_load_explicit(&dev->gart_end, memory_order_relaxed)))
            return NULL;
        *val = *(const uint32_t *) &(p[off]);
        *ra  = agpgart_remap(dev, *val, addr);
        if (*ra == AGPGART_NOWHERE)
            return NULL;
        *gen = &dev->gart_gen;
        return (const uint32_t *) &(p[off]);
    }
    return NULL;
}

static uint8_t
agpgart_aperture_readb(uint32_t addr, void *priv)
{
    agpgart_t *dev = (agpgart_t *) priv;
    uint32_t   ra  = agpgart_translate(addr, dev);

    return (ra == AGPGART_NOWHERE) ? 0xff : mem_readb_phys(ra);
}

static uint16_t
agpgart_aperture_readw(uint32_t addr, void *priv)
{
    agpgart_t *dev = (agpgart_t *) priv;
    uint32_t   ra;

    /* Each 4 KB page has its own GART entry, so split accesses that
       straddle two pages (AGP 2.0 2.1, Figure 2-1). */
    if ((addr & 0xfff) > 0xffe)
        return agpgart_aperture_readb(addr, priv) | (agpgart_aperture_readb(addr + 1, priv) << 8);
    ra = agpgart_translate(addr, dev);
    return (ra == AGPGART_NOWHERE) ? 0xffff : mem_readw_phys(ra);
}

static uint32_t
agpgart_aperture_readl(uint32_t addr, void *priv)
{
    agpgart_t *dev = (agpgart_t *) priv;
    uint32_t   ra;

    if ((addr & 0xfff) > 0xffc)
        return agpgart_aperture_readw(addr, priv) | ((uint32_t) agpgart_aperture_readw(addr + 2, priv) << 16);
    ra = agpgart_translate(addr, dev);
    return (ra == AGPGART_NOWHERE) ? 0xffffffff : mem_readl_phys(ra);
}

static void
agpgart_aperture_writeb(uint32_t addr, uint8_t val, void *priv)
{
    agpgart_t *dev = (agpgart_t *) priv;
    uint32_t   ra  = agpgart_translate(addr, dev);

    if (ra != AGPGART_NOWHERE)
        mem_writeb_phys(ra, val);
}

static void
agpgart_aperture_writew(uint32_t addr, uint16_t val, void *priv)
{
    agpgart_t *dev = (agpgart_t *) priv;
    uint32_t   ra;

    if ((addr & 0xfff) > 0xffe) {
        agpgart_aperture_writeb(addr, val & 0xff, priv);
        agpgart_aperture_writeb(addr + 1, val >> 8, priv);
        return;
    }
    ra = agpgart_translate(addr, dev);
    if (ra != AGPGART_NOWHERE)
        mem_writew_phys(ra, val);
}

static void
agpgart_aperture_writel(uint32_t addr, uint32_t val, void *priv)
{
    agpgart_t *dev = (agpgart_t *) priv;
    uint32_t   ra;

    if ((addr & 0xfff) > 0xffc) {
        agpgart_aperture_writew(addr, val & 0xffff, priv);
        agpgart_aperture_writew(addr + 2, val >> 16, priv);
        return;
    }
    ra = agpgart_translate(addr, dev);
    if (ra != AGPGART_NOWHERE)
        mem_writel_phys(ra, val);
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

    agpgart_gart_invalidate(dev);

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
