/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Cache-as-RAM: the processor's cache holding lines of memory that
 *          nothing on the bus backs.
 *
 *          Firmware that runs before DRAM is up (coreboot's bootblock and
 *          romstage on the P6 family, src/cpu/intel/car/p3/cache_as_ram.S)
 *          makes a variable MTRR write-back over an address where nothing
 *          answers, turns the cache on, reads the range in and then keeps
 *          its stack and data there. The lines are never evicted, so they
 *          never reach the bus, and the cache serves them alone; INVD later
 *          throws them away. 86Box does not keep a cache, so without this
 *          the writes went nowhere and the first RET read back FFFFFFFFh.
 *
 *          The lines are kept here while:
 *          - MTRRs are on and a valid variable range is write-back (no
 *            uncacheable range overlapping it, as UC wins, SDM vol. 3,
 *            10.11.4.1), no bigger than 32 KB, with nothing on the bus
 *            behind any of it (each variable MTRR's lines are allocated on
 *            their own, as its range starts to qualify, and freed when it
 *            stops);
 *          - and the cache has been on (CR0.CD clear) since the range was
 *            set up: with CD set, the P6 still serves the lines it has but
 *            fills no new ones (SDM vol. 3, Table 10-5).
 *          INVD, WBINVD (whose write-back has nowhere to go) and RESET
 *          drop them, and so does the range stopping being write-back (the
 *          P6 self-snoops an uncacheable access, writing the line back,
 *          to nothing, and invalidating it). A line then reads as the bus
 *          would, FFh, once it is filled again. INIT keeps the caches, as it
 *          keeps the MTRRs (cpu_init_keeps_state).
 *
 *          Ranges with anything behind them are left alone: the cache is
 *          coherent with them, and not keeping one gives the same result.
 *
 *          Only machines whose firmware relies on this turn it on
 *          (cpu_car_enable), for the BIOS that needs it; everywhere else the
 *          hooks return at once and no mapping exists.
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
#include <86box/mem.h>

#define MTRR_TYPE_UC       0x00
#define MTRR_TYPE_WB       0x06
#define MTRR_PHYS_VALID    0x00000800ULL
#define MTRR_DEF_TYPE_EN   0x00000800ULL

#define CR0_CD             (1 << 30)

/* The variable MTRRs the P6 family has. */
#define CAR_RANGES         8

/* The most kept of any one range. */
#define CAR_MAX_SIZE       (32 * 1024)

/* The lines of one variable range. */
typedef struct car_range_t {
    mem_mapping_t mapping;
    uint8_t      *data;
    uint32_t      base;  /* The write-back range the lines are of, */
    uint32_t      size;  /* none if 0. */
    int           valid; /* Filled: the cache was on since it was set up. */
    int           shown; /* The mapping and the cache access state are on. */
} car_range_t;

static struct {
    car_range_t range[CAR_RANGES]; /* One per variable MTRR. */
    int         enabled;           /* The machine's firmware uses cache-as-RAM. */
} car;

#ifdef ENABLE_CAR_LOG
int car_do_log = ENABLE_CAR_LOG;

static void
car_log(const char *fmt, ...)
{
    va_list ap;

    if (car_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define car_log(fmt, ...)
#endif

static uint8_t
car_read(uint32_t addr, void *priv)
{
    const car_range_t *r = (car_range_t *) priv;

    return r->data[addr - r->base];
}

static uint16_t
car_readw(uint32_t addr, void *priv)
{
    const car_range_t *r = (car_range_t *) priv;

    return *(uint16_t *) &r->data[addr - r->base];
}

static uint32_t
car_readl(uint32_t addr, void *priv)
{
    const car_range_t *r = (car_range_t *) priv;

    return *(uint32_t *) &r->data[addr - r->base];
}

static void
car_write(uint32_t addr, uint8_t val, void *priv)
{
    car_range_t *r = (car_range_t *) priv;

    r->data[addr - r->base] = val;
}

static void
car_writew(uint32_t addr, uint16_t val, void *priv)
{
    car_range_t *r = (car_range_t *) priv;

    *(uint16_t *) &r->data[addr - r->base] = val;
}

static void
car_writel(uint32_t addr, uint32_t val, void *priv)
{
    car_range_t *r = (car_range_t *) priv;

    *(uint32_t *) &r->data[addr - r->base] = val;
}

/* The variable ranges the cache can keep as RAM, by MTRR; size 0 for none. */
static void
car_find_ranges(uint32_t *base, uint32_t *size)
{
    int count = msr.mtrr_cap & 0xff;

    memset(base, 0, CAR_RANGES * sizeof(uint32_t));
    memset(size, 0, CAR_RANGES * sizeof(uint32_t));

    if (!(msr.mtrr_deftype & MTRR_DEF_TYPE_EN))
        return;

    if (count > CAR_RANGES)
        count = CAR_RANGES;

    for (int i = 0; i < count; i++) {
        uint32_t mask = msr.mtrr_physmask[i] & 0xfffff000;
        uint32_t b    = msr.mtrr_physbase[i] & mask;
        uint32_t sz   = ~mask + 1;
        int      skip = 0;

        if (!(msr.mtrr_physmask[i] & MTRR_PHYS_VALID) || ((msr.mtrr_physbase[i] & 0xff) != MTRR_TYPE_WB) ||
            (msr.mtrr_physbase[i] >> 32))
            continue;
        /* One contiguous range, which fits the cache. */
        if (!mask || (sz & (sz - 1)) || (sz > CAR_MAX_SIZE))
            continue;

        for (int j = 0; j < count; j++) {
            uint32_t mask_j = msr.mtrr_physmask[j] & 0xfffff000;

            if ((j != i) && (msr.mtrr_physmask[j] & MTRR_PHYS_VALID) &&
                ((msr.mtrr_physbase[j] & 0xff) == MTRR_TYPE_UC) &&
                !((b ^ (uint32_t) msr.mtrr_physbase[j]) & mask & mask_j))
                skip = 1;
        }

        /* A range overlapping one already kept is held in the same lines. */
        for (int j = 0; j < i; j++) {
            if (size[j] && (b < (base[j] + size[j])) && (base[j] < (b + sz)))
                skip = 1;
        }

        if (skip || !mem_bus_unbacked(b, sz))
            continue;

        base[i] = b;
        size[i] = sz;
    }
}

static void
car_show(car_range_t *r, int show)
{
    if (show == r->shown)
        return;

    car_log("CAR: %s %08X-%08X\n", show ? "Keeping" : "Dropping", r->base, r->base + r->size - 1);

    if (show) {
        mem_mapping_set_addr(&r->mapping, r->base, r->size);
        mem_set_cache_access(r->base, r->size, 1);
    } else {
        mem_set_cache_access(r->mapping.base, r->mapping.size, 0);
        mem_mapping_disable(&r->mapping);
    }

    r->shown = show;
}

/* After a write to an MTRR or to CR0.CD. */
void
cpu_car_update(void)
{
    uint32_t base[CAR_RANGES];
    uint32_t size[CAR_RANGES];

    if (!car.enabled)
        return;

    car_find_ranges(base, size);

    /* Drop the ranges that changed before keeping any new one, which may
       overlap where an old one was. */
    for (int i = 0; i < CAR_RANGES; i++) {
        car_range_t *r = &car.range[i];

        if ((base[i] == r->base) && (size[i] == r->size))
            continue;

        car_show(r, 0);

        r->base  = base[i];
        r->size  = size[i];
        r->valid = 0;

        if (r->size) {
            /* The handlers' word and long accesses stay within a granule. */
            r->data = realloc(r->data, r->size);
            memset(r->data, 0xff, r->size);
        } else {
            free(r->data);
            r->data = NULL;
        }
    }

    for (int i = 0; i < CAR_RANGES; i++) {
        car_range_t *r = &car.range[i];

        if (r->size && !(cr0 & CR0_CD))
            r->valid = 1;

        car_show(r, r->size && r->valid);
    }
}

/* INVD, WBINVD and RESET. */
void
cpu_car_invalidate(void)
{
    if (!car.enabled)
        return;

    for (int i = 0; i < CAR_RANGES; i++) {
        car_range_t *r = &car.range[i];

        if (r->size)
            memset(r->data, 0xff, r->size);
        r->valid = 0;
    }

    cpu_car_update();
}

/* The memory mappings were made anew (mem_reset): off until the machine asks. */
void
cpu_car_reset(void)
{
    car.enabled = 0;

    for (int i = 0; i < CAR_RANGES; i++) {
        car_range_t *r = &car.range[i];

        free(r->data);
        r->data = NULL;
        r->base = r->size = 0;
        r->valid = r->shown = 0;
    }
}

/* From the machine's init, after mem_reset, for firmware that needs it. */
void
cpu_car_enable(void)
{
    for (int i = 0; i < CAR_RANGES; i++) {
        mem_mapping_add(&car.range[i].mapping, 0, 0,
                        car_read, car_readw, car_readl,
                        car_write, car_writew, car_writel,
                        NULL, MEM_MAPPING_CACHE_ONLY, &car.range[i]);
    }

    car.enabled = 1;
}
