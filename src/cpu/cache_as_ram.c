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
 *            10.11.4.1), no bigger than the cache, with nothing on the bus
 *            behind any of it;
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
#include <86box/plat_unused.h>

#define MTRR_TYPE_UC       0x00
#define MTRR_TYPE_WB       0x06
#define MTRR_PHYS_VALID    0x00000800ULL
#define MTRR_DEF_TYPE_EN   0x00000800ULL

#define CR0_CD             (1 << 30)

/* The most the cache can hold: the L2 of a Slot 1 Pentium II or III. */
#define CAR_MAX_SIZE       (512 * 1024)

static struct {
    mem_mapping_t mapping;
    uint8_t      *data;
    uint32_t      base;  /* The write-back range the lines are of, */
    uint32_t      size;  /* none if 0. */
    int           enabled; /* The machine's firmware uses cache-as-RAM. */
    int           valid; /* Filled: the cache was on since it was set up. */
    int           shown; /* The mapping and the cache access state are on. */
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
car_read(uint32_t addr, UNUSED(void *priv))
{
    return car.data[addr - car.base];
}

static uint16_t
car_readw(uint32_t addr, UNUSED(void *priv))
{
    return *(uint16_t *) &car.data[addr - car.base];
}

static uint32_t
car_readl(uint32_t addr, UNUSED(void *priv))
{
    return *(uint32_t *) &car.data[addr - car.base];
}

static void
car_write(uint32_t addr, uint8_t val, UNUSED(void *priv))
{
    car.data[addr - car.base] = val;
}

static void
car_writew(uint32_t addr, uint16_t val, UNUSED(void *priv))
{
    *(uint16_t *) &car.data[addr - car.base] = val;
}

static void
car_writel(uint32_t addr, uint32_t val, UNUSED(void *priv))
{
    *(uint32_t *) &car.data[addr - car.base] = val;
}

/* The variable range the cache can keep as RAM, if any. */
static int
car_find_range(uint32_t *base, uint32_t *size)
{
    int count = msr.mtrr_cap & 0xff;

    if (!(msr.mtrr_deftype & MTRR_DEF_TYPE_EN))
        return 0;

    if (count > 8)
        count = 8;

    for (int i = 0; i < count; i++) {
        uint32_t mask = msr.mtrr_physmask[i] & 0xfffff000;
        uint32_t b    = msr.mtrr_physbase[i] & mask;
        uint32_t sz   = ~mask + 1;
        int      uc   = 0;

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
                uc = 1;
        }

        if (uc || !mem_bus_unbacked(b, sz))
            continue;

        *base = b;
        *size = sz;
        return 1;
    }

    return 0;
}

static void
car_show(int show)
{
    if (show == car.shown)
        return;

    car_log("CAR: %s %08X-%08X\n", show ? "Keeping" : "Dropping", car.base, car.base + car.size - 1);

    if (show) {
        mem_mapping_set_addr(&car.mapping, car.base, car.size);
        mem_set_cache_access(car.base, car.size, 1);
    } else {
        mem_set_cache_access(car.mapping.base, car.mapping.size, 0);
        mem_mapping_disable(&car.mapping);
    }

    car.shown = show;
}

/* After a write to an MTRR or to CR0.CD. */
void
cpu_car_update(void)
{
    uint32_t base = 0;
    uint32_t size = 0;

    if (!car.enabled)
        return;

    if (!car_find_range(&base, &size))
        base = size = 0;

    if ((base != car.base) || (size != car.size)) {
        car_show(0);

        car.base  = base;
        car.size  = size;
        car.valid = 0;

        if (size) {
            /* The handlers' word and long accesses stay within a granule. */
            car.data = realloc(car.data, size);
            memset(car.data, 0xff, size);
        }
    }

    if (car.size && !(cr0 & CR0_CD))
        car.valid = 1;

    car_show(car.size && car.valid);
}

/* INVD, WBINVD and RESET. */
void
cpu_car_invalidate(void)
{
    if (!car.enabled)
        return;

    if (car.size)
        memset(car.data, 0xff, car.size);
    car.valid = 0;

    cpu_car_update();
}

/* The memory mappings were made anew (mem_reset): off until the machine asks. */
void
cpu_car_reset(void)
{
    car.enabled = 0;
    car.base = car.size = 0;
    car.valid = car.shown = 0;
}

/* From the machine's init, after mem_reset, for firmware that needs it. */
void
cpu_car_enable(void)
{
    mem_mapping_add(&car.mapping, 0, 0,
                    car_read, car_readw, car_readl,
                    car_write, car_writew, car_writel,
                    NULL, MEM_MAPPING_CACHE_ONLY, NULL);

    car.enabled = 1;
}
