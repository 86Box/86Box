/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Intel 386EX Chip-Select Unit.
 *
 *          Each of the eight channels (CS6:0#, UCS#) has a 15-bit address
 *          and a 15-bit mask, compared against A25:11 for memory or A15:1
 *          for I/O. Ones in the mask exclude bits from the comparison: the
 *          lowest zero in the mask sets the block size and any ones above
 *          it repeat the block. The CSU does no overlap arbitration, all
 *          matching channels go active, so it only reports the blocks each
 *          channel selects and leaves the rest to the board. For a memory
 *          device on a channel, it can also keep a mapping of the device
 *          in each block, which the device repeats over if the block is
 *          bigger than it.
 *
 *          Bus size, wait states, READY# and pin multiplexing are stored
 *          but not emulated. SMM is not emulated either: a channel that
 *          only responds in SMM is treated as inactive.
 *
 *          The registers keep their contents over a Port 92 CPU-only
 *          reset, so they are only initialised when the device is. They
 *          are in the expanded I/O space, so the device does not map them
 *          itself: i386ex_csu_handler() does, for whoever decodes that.
 *
 * Authors: Nathan Lewis, <git@nrlewis.dev>
 *
 *          Copyright 2026 Nathan Lewis.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/i386ex_csu.h>
#include <86box/plat_unused.h>

/* Register addresses: channel n's registers are at CS0ADL + 8n, UCS being channel 7. */
#define CS0ADL     0xf400

#define ADL_CASMM  0x0400
#define ADL_MEM    0x0100
#define MSKL_CMSMM 0x0400
#define MSKL_CSEN  0x0001

#define FIELD_MASK 0x7fff

enum {
    REG_ADL = 0,
    REG_ADH,
    REG_MSKL,
    REG_MSKH
};

typedef struct i386ex_csu_decode_t {
    uint8_t  active;
    uint8_t  is_mem;
    uint16_t addr; /* CA15:1 */
    uint16_t mask; /* CM15:1 */
} i386ex_csu_decode_t;

/* A mapping of a channel's memory device, kept for reuse once added. */
typedef struct i386ex_csu_mapping_t {
    mem_mapping_t mapping;
    uint32_t      base;
    uint8_t       added;
    uint8_t       used;
} i386ex_csu_mapping_t;

typedef struct i386ex_csu_channel_t {
    int      index;
    uint16_t regs[4];

    i386ex_csu_decode_t decode; /* What the targets were last told. */
    i386ex_cs_func_t    func;
    void               *priv;

    i386ex_cs_mem_t        mem;
    i386ex_csu_mapping_t **mappings;
    int                    mappings_num;
} i386ex_csu_channel_t;

#define REPORT_FUNC 1
#define REPORT_MEM  2

struct i386ex_csu_t {
    i386ex_csu_channel_t ch[I386EX_CSU_CHANNELS];
};

#ifdef ENABLE_I386EX_CSU_LOG
int i386ex_csu_do_log = ENABLE_I386EX_CSU_LOG;

static void
i386ex_csu_log(const char *fmt, ...)
{
    va_list ap;

    if (i386ex_csu_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define i386ex_csu_log(fmt, ...)
#endif

static i386ex_csu_decode_t
i386ex_csu_decode(const i386ex_csu_channel_t *ch)
{
    i386ex_csu_decode_t d = { 0 };

    /* A channel that only selects in SMM never does, as SMM is not emulated. */
    d.active = (ch->regs[REG_MSKL] & MSKL_CSEN) && ((ch->regs[REG_MSKL] & MSKL_CMSMM) || !(ch->regs[REG_ADL] & ADL_CASMM));
    d.is_mem = !!(ch->regs[REG_ADL] & ADL_MEM);
    d.addr   = ((ch->regs[REG_ADH] & 0x03ff) << 5) | (ch->regs[REG_ADL] >> 11);
    d.mask   = ((ch->regs[REG_MSKH] & 0x03ff) << 5) | (ch->regs[REG_MSKL] >> 11);

    return d;
}

static i386ex_csu_mapping_t *
i386ex_csu_get_mapping(i386ex_csu_channel_t *ch)
{
    for (int i = 0; i < ch->mappings_num; i++) {
        if (!ch->mappings[i]->used)
            return ch->mappings[i];
    }

    ch->mappings                   = realloc(ch->mappings, (ch->mappings_num + 1) * sizeof(i386ex_csu_mapping_t *));
    ch->mappings[ch->mappings_num] = calloc(1, sizeof(i386ex_csu_mapping_t));

    return ch->mappings[ch->mappings_num++];
}

/* Map or unmap the channel's memory device in one block. */
static void
i386ex_csu_block_mem(i386ex_csu_channel_t *ch, int set, uint32_t base, uint32_t size)
{
    if (set) {
        i386ex_csu_mapping_t *m = i386ex_csu_get_mapping(ch);

        if (m->added)
            ch->mem.remap(ch->mem.priv, &m->mapping, base, size);
        else
            ch->mem.map(ch->mem.priv, &m->mapping, base, size);

        m->added = m->used = 1;
        m->base            = base;
    } else {
        for (int i = 0; i < ch->mappings_num; i++) {
            if (ch->mappings[i]->used && (ch->mappings[i]->base == base)) {
                mem_mapping_disable(&ch->mappings[i]->mapping);
                ch->mappings[i]->used = 0;
            }
        }
    }
}

/*
   Report every block the decode selects to the targets in which: the memory
   device is mapped before the function is told of a new block and unmapped
   before it is told one is gone, so the function sees the result.
 */
static void
i386ex_csu_report(i386ex_csu_channel_t *ch, const i386ex_csu_decode_t *d, int set, int which)
{
    int      shift      = d->is_mem ? 11 : 1; /* Bit 1 of the fields is A11 or A1. */
    int      block_bits = 0;
    uint16_t holes;
    uint16_t h = 0;

    if (!(which & REPORT_MEM) || !d->is_mem || (ch->mem.map == NULL))
        which &= ~REPORT_MEM;
    if (ch->func == NULL)
        which &= ~REPORT_FUNC;
    if (!which || !d->active)
        return;

    /* The lowest zero in the mask sets the block size, ones above it repeat the block. */
    while ((block_bits < 15) && (d->mask & (1 << block_bits)))
        block_bits++;
    holes = d->mask & ~((1 << block_bits) - 1) & FIELD_MASK;

    do {
        uint32_t base = (uint32_t) ((d->addr & ~d->mask & FIELD_MASK) | h) << shift;
        uint32_t size = (uint32_t) 1 << (block_bits + shift);

        if (which & REPORT_MEM)
            i386ex_csu_block_mem(ch, set, base, size);
        if (which & REPORT_FUNC)
            ch->func(ch->priv, set, d->is_mem, base, size);

        /* Next combination of the hole bits. */
        h = (h - holes) & holes;
    } while (h != 0);
}

static void
i386ex_csu_recalc(i386ex_csu_channel_t *ch)
{
    i386ex_csu_decode_t d = i386ex_csu_decode(ch);

    if (!memcmp(&d, &ch->decode, sizeof(d)))
        return;

    i386ex_csu_log("386EX CSU: %s channel %i: %s, addr %04X, mask %04X\n",
                   (ch->func != NULL) ? "attached" : "unattached", ch->index,
                   d.active ? (d.is_mem ? "memory" : "I/O") : "inactive", d.addr, d.mask);

    i386ex_csu_report(ch, &ch->decode, 0, REPORT_FUNC | REPORT_MEM);
    ch->decode = d;
    i386ex_csu_report(ch, &ch->decode, 1, REPORT_FUNC | REPORT_MEM);
}

static uint16_t
i386ex_csu_readw(uint16_t port, void *priv)
{
    const i386ex_csu_t *dev = (i386ex_csu_t *) priv;

    return dev->ch[(port >> 3) & 7].regs[(port >> 1) & 3];
}

static uint8_t
i386ex_csu_read(uint16_t port, void *priv)
{
    return i386ex_csu_readw(port & ~1, priv) >> ((port & 1) << 3);
}

static void
i386ex_csu_writew(uint16_t port, uint16_t val, void *priv)
{
    i386ex_csu_t         *dev = (i386ex_csu_t *) priv;
    i386ex_csu_channel_t *ch  = &dev->ch[(port >> 3) & 7];

    i386ex_csu_log("386EX CSU: [%04X] = %04X\n", port, val);

    ch->regs[(port >> 1) & 3] = val;
    i386ex_csu_recalc(ch);
}

static void
i386ex_csu_write(uint16_t port, uint8_t val, void *priv)
{
    uint16_t old = i386ex_csu_readw(port & ~1, priv);

    if (port & 1)
        i386ex_csu_writew(port & ~1, (old & 0x00ff) | (val << 8), priv);
    else
        i386ex_csu_writew(port, (old & 0xff00) | val, priv);
}

void
i386ex_csu_attach(i386ex_csu_t *dev, int channel, i386ex_cs_func_t func, void *priv)
{
    i386ex_csu_channel_t *ch = &dev->ch[channel];

    i386ex_csu_report(ch, &ch->decode, 0, REPORT_FUNC);
    ch->func = func;
    ch->priv = priv;
    i386ex_csu_report(ch, &ch->decode, 1, REPORT_FUNC);
}

void
i386ex_csu_attach_mem(i386ex_csu_t *dev, int channel, const i386ex_cs_mem_t *mem)
{
    i386ex_csu_channel_t *ch = &dev->ch[channel];

    i386ex_csu_report(ch, &ch->decode, 0, REPORT_MEM);
    ch->mem = *mem;
    i386ex_csu_report(ch, &ch->decode, 1, REPORT_MEM);
}

/* The registers are in the expanded I/O space, so whoever decodes that maps them. */
void
i386ex_csu_handler(i386ex_csu_t *dev, int set)
{
    io_handler(set, CS0ADL, I386EX_CSU_CHANNELS * 8,
               i386ex_csu_read, i386ex_csu_readw, NULL,
               i386ex_csu_write, i386ex_csu_writew, NULL, dev);
}

static void *
i386ex_csu_init(UNUSED(const device_t *info))
{
    i386ex_csu_t *dev = (i386ex_csu_t *) calloc(1, sizeof(i386ex_csu_t));

    /* Only UCS is enabled after reset, selecting the whole 64 MB memory space. */
    dev->ch[I386EX_CSU_UCS].regs[REG_ADL]  = 0xff6f;
    dev->ch[I386EX_CSU_UCS].regs[REG_ADH]  = 0xffff;
    dev->ch[I386EX_CSU_UCS].regs[REG_MSKL] = 0xffff;
    dev->ch[I386EX_CSU_UCS].regs[REG_MSKH] = 0xffff;

    for (int i = 0; i < I386EX_CSU_CHANNELS; i++) {
        dev->ch[i].index  = i;
        dev->ch[i].decode = i386ex_csu_decode(&dev->ch[i]);
    }

    return dev;
}

static void
i386ex_csu_close(void *priv)
{
    i386ex_csu_t *dev = (i386ex_csu_t *) priv;

    for (int i = 0; i < I386EX_CSU_CHANNELS; i++) {
        for (int j = 0; j < dev->ch[i].mappings_num; j++)
            free(dev->ch[i].mappings[j]);
        free(dev->ch[i].mappings);
    }

    free(dev);
}

const device_t i386ex_csu_device = {
    .name          = "Intel 386EX Chip-Select Unit",
    .internal_name = "i386ex_csu",
    .flags         = 0,
    .local         = 0,
    .init          = i386ex_csu_init,
    .close         = i386ex_csu_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
