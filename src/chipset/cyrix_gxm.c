/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Cyrix MediaGX MMX's integrated functions
 *          other than graphics: the PCI host bridge, the internal bus
 *          interface unit, the memory controller, the power management
 *          registers, and the memory map of GX_BASE.
 *
 *          Everything follows the MediaGX MMX-Enhanced Processor data
 *          book, revision 2.0 (October 1998), and its addendum.
 *
 *          GX_BASE (GCR[1:0]) holds, in its 16 MB:
 *          - 0000h-0FFFh: the scratchpad RAM, the part of the L1 cache
 *            the GCR sizes, seen by the processor alone;
 *          - 8000h-8FFFh: the control registers;
 *          - 800000h-FFFFFFh: the graphics memory, the top of DRAM from
 *            MC_GBASE_ADD on.
 *          The rest goes to PCI: the CS5530 has its register BARs there.
 *
 * Authors: Cacodemon345
 *
 *          Copyright 2026 Cacodemon345.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/timer.h>
#include <86box/plat_unused.h>
#include <86box/chipset.h>
#include <86box/cyrix_gxm.h>

#ifdef ENABLE_CYRIX_GXM_LOG
int cyrix_gxm_do_log = ENABLE_CYRIX_GXM_LOG;

static void
cyrix_gxm_log(const char *fmt, ...)
{
    va_list ap;

    if (cyrix_gxm_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define cyrix_gxm_log(fmt, ...)
#endif

/* BC_XMAP_1 (Table 4-10, with the addendum's item 8). */
#define XMAP1_GEA   (1 << 4)
#define XMAP1_SMIB  (1 << 13) /* 3B0h-3BFh */
#define XMAP1_SMIC  (1 << 14) /* 3C0h-3CFh */
#define XMAP1_SMID  (1 << 15) /* 3D0h-3DFh */
#define XMAP1_GEB0  (1 << 20)
#define XMAP1_A20M  (1 << 21)
#define XMAP1_GEB8  (1 << 28)

/* The region control fields (Table 4-11). */
#define REGION_RE   0x01
#define REGION_WE   0x02

/* MC_GBASE_ADD: graphics memory on 512 KB boundaries. */
#define GBADD_SHIFT 19

#define GMEM_SIZE   0x00800000

/* MC_MEM_CNTRL1: VGA Wrap Enable. */
#define MC_VGAWRP   (1 << 2)

typedef struct gxm_dimm_t {
    int      installed;
    uint32_t size;
    int      col_bits;
    int      bank_bits;
    int      mb_bits;
    int      row_bits;
} gxm_dimm_t;

typedef struct cyrix_gxm_t {
    uint8_t  pci_conf[256];
    uint8_t  pci_slot;

    uint32_t gx_base;

    uint32_t bc_dram_top;
    uint32_t bc_xmap[3];

    uint32_t mc_mem_cntrl1;
    uint32_t mc_mem_cntrl2;
    uint32_t mc_bank_cfg;
    uint32_t mc_sync_tim1;
    uint32_t mc_gbase_add;
    uint32_t mc_dr_add;
    uint8_t  dirty_ram[1024];

    uint32_t pm_stat_smi;
    uint32_t pm_cntrl_ten;
    uint32_t pm_cntrl_cstp;
    uint32_t pm_ser_pack;
    pc_timer_t ser_timer; /* The serial packet's transmission interval */

    uint32_t gmem_base;
    uint32_t gmem_size;

    uint8_t  vga_io; /* The trapped VGA I/O ranges, XMAP1_SMIx >> 13 */

    /* The DIMMs: as they are, and as MC_BANK_CFG has them. */
    gxm_dimm_t dimm[2];
    gxm_dimm_t prog[2];
    int        auto_loi;
    int        sizing;  /* MC_BANK_CFG differs from the DIMMs: addresses translate */
    uint32_t   dram_top;

    mem_mapping_t scratch_mapping;
    mem_mapping_t reg_mapping;
    mem_mapping_t gmem_mapping;
    mem_mapping_t dram_mapping;
    mem_mapping_t dram_mapping_hi;
    mem_mapping_t smm_mapping;
} cyrix_gxm_t;

static cyrix_gxm_t *gxm_dev = NULL;

/* ----------------------------------------------------------------------
   The SDRAM array (4.3). A DIMM's own address is, from the top, the row,
   the module bank, the component bank, the column (a page of 1 to 16 KB),
   and the byte in the 64-bit word. Two DIMMs follow each other (HOI), or,
   alike, interleave (Auto LOI): the DIMM is picked after the banks, and the
   module bank before it (Tables 4-17 to 4-20).

   The BIOS sizes the array by setting MC_BANK_CFG large and finding where
   the writes alias. The DIMMs are modelled with 4 component banks of 4 KB
   pages, and one module bank up to 128 MB; the address goes through the
   geometry programmed, and the chips take the bits they have: the others
   are not wired (they alias), and a module bank or DIMM not there floats.
   With MC_BANK_CFG matching the DIMMs, the translation is the identity,
   and RAM is mapped as usual.
   ---------------------------------------------------------------------- */
static int
gxm_log2(uint32_t v)
{
    int r = 0;

    while (v > 1) {
        v >>= 1;
        r++;
    }

    return r;
}

static void
gxm_dimm_geometry(gxm_dimm_t *d)
{
    const int bits = gxm_log2(d->size);

    d->row_bits = bits - 3 - d->col_bits - d->bank_bits - d->mb_bits;
    if (d->row_bits < 0)
        d->row_bits = 0;
}

/* The DIMMs installed: the RAM as one DIMM of a power of two, and the rest
   as a smaller one, each made of the SDRAM chips of its time, with rows of
   11 or 12 bits: the BIOS finds the rows by aliasing row bits 8 to 10, and
   fewer rows read as a larger DIMM. A DIMM is at most 128 MB, two module
   banks of 64 Mbit chips; a larger one would read as 128 MB. Two alike
   DIMMs interleave (Auto LOI) once programmed; RAM is then mapped as usual
   all the same, as nothing can tell the order the bytes are kept in. */
static void
gxm_dimms_init(cyrix_gxm_t *dev)
{
    /* By size from 4 MB: columns, component bank bits, module bank bits. */
    static const int geom[6][3] = {
        { 8, 1, 0 }, /*   4 MB: 16 Mbit x16, 10 rows */
        { 8, 1, 0 }, /*   8 MB: 16 Mbit x16 */
        { 9, 1, 0 }, /*  16 MB: 16 Mbit x8 */
        { 8, 2, 0 }, /*  32 MB: 64 Mbit x16 */
        { 9, 2, 0 }, /*  64 MB: 64 Mbit x8 */
        { 9, 2, 1 }  /* 128 MB: 64 Mbit x8, two module banks */
    };
    uint32_t left = mem_size << 10;

    memset(dev->dimm, 0x00, sizeof(dev->dimm));
    for (int i = 0; (i < 2) && (left >= 0x00400000); i++) {
        gxm_dimm_t *d = &dev->dimm[i];
        const int  *g;

        d->size = 1 << gxm_log2(left);
        if (d->size > 0x08000000)
            d->size = 0x08000000;
        g            = geom[gxm_log2(d->size >> 22)];
        d->installed = 1;
        d->col_bits  = g[0];
        d->bank_bits = g[1];
        d->mb_bits   = g[2];
        gxm_dimm_geometry(d);
        left -= d->size;
    }
}

static void
gxm_prog_decode(cyrix_gxm_t *dev)
{
    for (int i = 0; i < 2; i++) {
        const uint16_t cfg = dev->mc_bank_cfg >> (i << 4);
        gxm_dimm_t    *d   = &dev->prog[i];
        const int      pg  = (cfg >> 4) & 7;

        memset(d, 0x00, sizeof(gxm_dimm_t));
        d->installed = (pg != 7);
        d->col_bits  = 7 + ((pg >= 4) ? 4 : pg);
        d->bank_bits = (cfg & 0x1000) ? 2 : 1;
        d->mb_bits   = !!(cfg & 0x4000);
        d->size      = 0x00400000 << ((cfg >> 8) & 7);
        gxm_dimm_geometry(d);
    }

    dev->auto_loi = dev->prog[0].installed && dev->prog[1].installed &&
                    ((dev->mc_bank_cfg & 0xffff) == (dev->mc_bank_cfg >> 16));
}

static int
gxm_dimm_equal(const gxm_dimm_t *a, const gxm_dimm_t *b)
{
    if (a->installed != b->installed)
        return 0;
    if (!a->installed)
        return 1;

    return (a->size == b->size) && (a->col_bits == b->col_bits) && (a->bank_bits == b->bank_bits) &&
           (a->mb_bits == b->mb_bits);
}

/* The RAM byte a physical address reaches, -1 if no chip drives it. */
static int64_t
gxm_dram_translate(cyrix_gxm_t *dev, uint32_t addr)
{
    const gxm_dimm_t *p;
    const gxm_dimm_t *a;
    uint32_t          local;
    uint32_t          col;
    uint32_t          bank;
    uint32_t          mb;
    uint32_t          row;
    int               d;
    int               sh;

    if (dev->auto_loi) {
        p  = &dev->prog[0];
        sh = 3 + p->col_bits + p->bank_bits;
        if (addr >= (p->size << 1))
            return -1;
        col  = (addr >> 3) & ((1 << p->col_bits) - 1);
        bank = (addr >> (3 + p->col_bits)) & ((1 << p->bank_bits) - 1);
        mb   = p->mb_bits ? ((addr >> sh) & 1) : 0;
        sh += p->mb_bits;
        d    = (addr >> sh) & 1;
        row  = addr >> (sh + 1);
    } else {
        /* The larger DIMM comes first: the BIOS sizes one DIMM with the
           other set to 4 MB, and looks for the aliases at address 0. */
        const int         first = dev->prog[1].installed &&
                                  (!dev->prog[0].installed || (dev->prog[1].size > dev->prog[0].size));
        const gxm_dimm_t *f     = &dev->prog[first];
        const gxm_dimm_t *s     = &dev->prog[!first];

        if (f->installed && (addr < f->size)) {
            d     = first;
            local = addr;
        } else if (s->installed && f->installed && ((addr - f->size) < s->size)) {
            d     = !first;
            local = addr - f->size;
        } else
            return -1;

        p    = &dev->prog[d];
        col  = (local >> 3) & ((1 << p->col_bits) - 1);
        bank = (local >> (3 + p->col_bits)) & ((1 << p->bank_bits) - 1);
        sh   = 3 + p->col_bits + p->bank_bits;
        mb   = p->mb_bits ? ((local >> sh) & 1) : 0;
        row  = local >> (sh + p->mb_bits);
    }

    /* The chips: MA lines and bank bits beyond theirs are not wired. */
    a = &dev->dimm[d];
    if (!a->installed || (mb >= (1u << a->mb_bits)))
        return -1;

    col &= (1 << a->col_bits) - 1;
    bank &= (1 << a->bank_bits) - 1;
    row &= (1 << a->row_bits) - 1;

    return (int64_t) (d ? dev->dimm[0].size : 0) +
           ((((((((row << a->mb_bits) | mb) << a->bank_bits) | bank) << a->col_bits) | col) << 3) | (addr & 7));
}

static uint8_t
gxm_dram_read(uint32_t addr, void *priv)
{
    const int64_t a = gxm_dram_translate((cyrix_gxm_t *) priv, addr);

    return (a < 0) ? 0xff : ram[a];
}

static uint16_t
gxm_dram_readw(uint32_t addr, void *priv)
{
    return gxm_dram_read(addr, priv) | (gxm_dram_read(addr + 1, priv) << 8);
}

static uint32_t
gxm_dram_readl(uint32_t addr, void *priv)
{
    return gxm_dram_readw(addr, priv) | (gxm_dram_readw(addr + 2, priv) << 16);
}

static void
gxm_dram_write(uint32_t addr, uint8_t val, void *priv)
{
    const int64_t a = gxm_dram_translate((cyrix_gxm_t *) priv, addr);

    if (a >= 0)
        ram[a] = val;
}

static void
gxm_dram_writew(uint32_t addr, uint16_t val, void *priv)
{
    gxm_dram_write(addr, val, priv);
    gxm_dram_write(addr + 1, val >> 8, priv);
}

static void
gxm_dram_writel(uint32_t addr, uint32_t val, void *priv)
{
    gxm_dram_writew(addr, val, priv);
    gxm_dram_writew(addr + 2, val >> 16, priv);
}

/* The scratchpad: the top 2, 3 or 4 KB of the first 4 KB of GX_BASE. Its
   lines are always valid and never written back (4.1.4.1). */
static uint8_t
gxm_scratch_read(uint32_t addr, UNUSED(void *priv))
{
    return gxm.scratchpad[addr & 0x0fff];
}

/* Words and dwords at any byte address. */
static uint16_t
gxm_scratch_readw(uint32_t addr, void *priv)
{
    addr &= 0x0fff;
    if (addr > 0x0ffe)
        return gxm_scratch_read(addr, priv) | (gxm_scratch_read(addr + 1, priv) << 8);

    return *(uint16_t *) &gxm.scratchpad[addr];
}

static uint32_t
gxm_scratch_readl(uint32_t addr, void *priv)
{
    addr &= 0x0fff;
    if (addr > 0x0ffc)
        return gxm_scratch_readw(addr, priv) | (gxm_scratch_readw(addr + 2, priv) << 16);

    return *(uint32_t *) &gxm.scratchpad[addr];
}

static void
gxm_scratch_write(uint32_t addr, uint8_t val, UNUSED(void *priv))
{
    gxm.scratchpad[addr & 0x0fff] = val;
}

static void
gxm_scratch_writew(uint32_t addr, uint16_t val, void *priv)
{
    addr &= 0x0fff;
    if (addr > 0x0ffe) {
        gxm_scratch_write(addr, val & 0xff, priv);
        gxm_scratch_write(addr + 1, val >> 8, priv);
    } else
        *(uint16_t *) &gxm.scratchpad[addr] = val;
}

static void
gxm_scratch_writel(uint32_t addr, uint32_t val, void *priv)
{
    addr &= 0x0fff;
    if (addr > 0x0ffc) {
        gxm_scratch_writew(addr, val & 0xffff, priv);
        gxm_scratch_writew(addr + 2, val >> 16, priv);
    } else
        *(uint32_t *) &gxm.scratchpad[addr] = val;
}

/* The graphics memory, top of DRAM seen through GX_BASE+800000h. */
static uint8_t
gxm_gmem_read(uint32_t addr, void *priv)
{
    const cyrix_gxm_t *dev = (cyrix_gxm_t *) priv;

    addr &= (GMEM_SIZE - 1);
    if (addr >= dev->gmem_size)
        return 0xff;

    return ram[dev->gmem_base + addr];
}

/* Words and dwords at any byte address; one that runs past the end goes a
   byte at a time. */
static uint16_t
gxm_gmem_readw(uint32_t addr, void *priv)
{
    const cyrix_gxm_t *dev = (cyrix_gxm_t *) priv;

    addr &= (GMEM_SIZE - 1);
    if ((addr + 2) > dev->gmem_size)
        return gxm_gmem_read(addr, priv) | (gxm_gmem_read(addr + 1, priv) << 8);

    return *(uint16_t *) &ram[dev->gmem_base + addr];
}

static uint32_t
gxm_gmem_readl(uint32_t addr, void *priv)
{
    const cyrix_gxm_t *dev = (cyrix_gxm_t *) priv;

    addr &= (GMEM_SIZE - 1);
    if ((addr + 4) > dev->gmem_size)
        return gxm_gmem_readw(addr, priv) | (gxm_gmem_readw(addr + 2, priv) << 16);

    return *(uint32_t *) &ram[dev->gmem_base + addr];
}

static void
gxm_gmem_write(uint32_t addr, uint8_t val, void *priv)
{
    const cyrix_gxm_t *dev = (cyrix_gxm_t *) priv;

    addr &= (GMEM_SIZE - 1);
    if (addr < dev->gmem_size) {
        ram[dev->gmem_base + addr] = val;
        gxm_video_gmem_written(gxm_video, addr, 1);
    }
}

static void
gxm_gmem_writew(uint32_t addr, uint16_t val, void *priv)
{
    const cyrix_gxm_t *dev = (cyrix_gxm_t *) priv;

    addr &= (GMEM_SIZE - 1);
    if ((addr + 2) > dev->gmem_size) {
        gxm_gmem_write(addr, val & 0xff, priv);
        gxm_gmem_write(addr + 1, val >> 8, priv);
    } else {
        *(uint16_t *) &ram[dev->gmem_base + addr] = val;
        gxm_video_gmem_written(gxm_video, addr, 2);
    }
}

static void
gxm_gmem_writel(uint32_t addr, uint32_t val, void *priv)
{
    const cyrix_gxm_t *dev = (cyrix_gxm_t *) priv;

    addr &= (GMEM_SIZE - 1);
    if ((addr + 4) > dev->gmem_size) {
        gxm_gmem_writew(addr, val & 0xffff, priv);
        gxm_gmem_writew(addr + 2, val >> 16, priv);
    } else {
        *(uint32_t *) &ram[dev->gmem_base + addr] = val;
        gxm_video_gmem_written(gxm_video, addr, 4);
    }
}

/* GX_BASE+400000h, with MC_MEM_CNTRL1's VGA Wrap: the DRAM under A0000h-
   BFFFFh, which the A and B regions hide when they go to the graphics
   pipeline. The BIOS puts the SMM code there (Figure 4-2's "SMM System
   Code", in both maps). */
static uint8_t
gxm_smm_read(uint32_t addr, UNUSED(void *priv))
{
    return ram[0xa0000 + (addr & 0x1ffff)];
}

/* Words and dwords at any byte address (a 16-bit stack is only word
   aligned); one at the top wraps a byte at a time. */
static uint16_t
gxm_smm_readw(uint32_t addr, void *priv)
{
    addr &= 0x1ffff;
    if (addr > 0x1fffe)
        return gxm_smm_read(addr, priv) | (gxm_smm_read(addr + 1, priv) << 8);

    return *(uint16_t *) &ram[0xa0000 + addr];
}

static uint32_t
gxm_smm_readl(uint32_t addr, void *priv)
{
    addr &= 0x1ffff;
    if (addr > 0x1fffc)
        return gxm_smm_readw(addr, priv) | (gxm_smm_readw(addr + 2, priv) << 16);

    return *(uint32_t *) &ram[0xa0000 + addr];
}

static void
gxm_smm_write(uint32_t addr, uint8_t val, UNUSED(void *priv))
{
    ram[0xa0000 + (addr & 0x1ffff)] = val;
}

static void
gxm_smm_writew(uint32_t addr, uint16_t val, void *priv)
{
    addr &= 0x1ffff;
    if (addr > 0x1fffe) {
        gxm_smm_write(addr, val & 0xff, priv);
        gxm_smm_write(addr + 1, val >> 8, priv);
    } else
        *(uint16_t *) &ram[0xa0000 + addr] = val;
}

static void
gxm_smm_writel(uint32_t addr, uint32_t val, void *priv)
{
    addr &= 0x1ffff;
    if (addr > 0x1fffc) {
        gxm_smm_writew(addr, val & 0xffff, priv);
        gxm_smm_writew(addr + 2, val >> 16, priv);
    } else
        *(uint32_t *) &ram[0xa0000 + addr] = val;
}

uint32_t
cyrix_gxm_gmem_base(void)
{
    return gxm_dev ? gxm_dev->gmem_base : 0;
}

uint32_t
cyrix_gxm_gmem_size(void)
{
    return gxm_dev ? gxm_dev->gmem_size : 0;
}

/* GX_BASE moves with GCR[1:0], the scratchpad sizes with GCR[3:2]. */
static void
gxm_remap(cyrix_gxm_t *dev)
{
    static const uint32_t sp_size[4] = { 0x0000, 0x0800, 0x0c00, 0x1000 };
    const uint32_t        sp         = sp_size[(gxm.gcr & GCR_SP_SIZE) >> 2];

    dev->gx_base = (gxm.gcr & GCR_GX_BASE) << 30;

    cyrix_gxm_log("MediaGX: GX_BASE = %08X, scratchpad %i KB\n", dev->gx_base, sp >> 10);

    if (dev->gx_base) {
        if (sp)
            mem_mapping_set_addr(&dev->scratch_mapping, dev->gx_base + 0x1000 - sp, sp);
        else
            mem_mapping_disable(&dev->scratch_mapping);
        mem_mapping_set_addr(&dev->reg_mapping, dev->gx_base + 0x8000, 0x1000);
        mem_mapping_set_addr(&dev->gmem_mapping, dev->gx_base + GMEM_SIZE, GMEM_SIZE);
        if (dev->mc_mem_cntrl1 & MC_VGAWRP)
            mem_mapping_set_addr(&dev->smm_mapping, dev->gx_base + 0x400000, 0x20000);
        else
            mem_mapping_disable(&dev->smm_mapping);
    } else {
        mem_mapping_disable(&dev->scratch_mapping);
        mem_mapping_disable(&dev->reg_mapping);
        mem_mapping_disable(&dev->gmem_mapping);
        mem_mapping_disable(&dev->smm_mapping);
    }

    flushmmucache_nopc();
}

static void
gxm_gcr_write(void *priv)
{
    gxm_remap((cyrix_gxm_t *) priv);
}

static void
gxm_gmem_recalc(cyrix_gxm_t *dev)
{
    const uint32_t ram_size = mem_size << 10;

    dev->gmem_base = (dev->mc_gbase_add & 0x7ff) << GBADD_SHIFT;
    if (dev->gmem_base < ram_size) {
        dev->gmem_size = ram_size - dev->gmem_base;
        if (dev->gmem_size > GMEM_SIZE)
            dev->gmem_size = GMEM_SIZE;
    } else
        dev->gmem_size = 0;

    cyrix_gxm_log("MediaGX: Graphics memory at %08X, %i KB\n", dev->gmem_base, dev->gmem_size >> 10);

    gxm_video_set_gmem(gxm_video, dev->gmem_base, dev->gmem_size);
}

/* System memory ends at BC_DRAM_TOP; above it, the processor's cycles go to
   PCI, the graphics memory among them. */
static void
gxm_dram_recalc(cyrix_gxm_t *dev)
{
    const uint32_t ram_size = mem_size << 10;
    uint32_t       top      = (dev->bc_dram_top & 0x3ffe0000) + 0x20000;
    uint32_t       end;

    if (top < 0x00100000)
        top = 0x00100000;

    gxm_prog_decode(dev);
    dev->sizing = !gxm_dimm_equal(&dev->prog[0], &dev->dimm[0]) || !gxm_dimm_equal(&dev->prog[1], &dev->dimm[1]);

    /* What was the array before, that is not now, is PCI again. */
    end = (dev->dram_top > ram_size) ? dev->dram_top : ram_size;
    if (end > top)
        mem_set_mem_state_both(top, end - top, MEM_READ_EXTANY | MEM_WRITE_EXTANY);

    if (dev->sizing) {
        cyrix_gxm_log("MediaGX: MC_BANK_CFG %08X, not the DIMMs: translating up to %08X\n", dev->mc_bank_cfg, top);
        mem_set_mem_state_both(0x00000000, 0x000a0000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
        mem_set_mem_state_both(0x00100000, top - 0x00100000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
        mem_mapping_set_addr(&dev->dram_mapping, 0x00000000, 0x000a0000);
        mem_mapping_set_addr(&dev->dram_mapping_hi, 0x00100000, top - 0x00100000);
    } else {
        mem_mapping_disable(&dev->dram_mapping);
        mem_mapping_disable(&dev->dram_mapping_hi);
        mem_set_mem_state_both(0x00000000, 0x000a0000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        end = (top < ram_size) ? top : ram_size;
        if (end > 0x00100000)
            mem_set_mem_state_both(0x00100000, end - 0x00100000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
    }

    dev->dram_top = top;

    flushmmucache_nopc();
}

static int
gxm_region_state(uint8_t ctl)
{
    return ((ctl & REGION_RE) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) |
           ((ctl & REGION_WE) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY);
}

/* The 19 regions of 640 KB-1 MB (4.2.4). A, B0 and B8 go to the graphics
   pipeline instead, with their graphics enables. */
static void
gxm_regions_recalc(cyrix_gxm_t *dev)
{
    const uint32_t xmap1   = dev->bc_xmap[0];
    uint8_t        enables = 0x00;

    if (xmap1 & XMAP1_GEA)
        enables |= GXM_VGA_A;
    else
        mem_set_mem_state_both(0xa0000, 0x10000, gxm_region_state(xmap1 & 0x0f));

    if (xmap1 & XMAP1_GEB0)
        enables |= GXM_VGA_B0;
    else
        mem_set_mem_state_both(0xb0000, 0x08000, gxm_region_state((xmap1 >> 16) & 0x0f));

    if (xmap1 & XMAP1_GEB8)
        enables |= GXM_VGA_B8;
    else
        mem_set_mem_state_both(0xb8000, 0x08000, gxm_region_state((xmap1 >> 24) & 0x0f));

    if (enables & GXM_VGA_A)
        mem_set_mem_state_both(0xa0000, 0x10000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
    if (enables & GXM_VGA_B0)
        mem_set_mem_state_both(0xb0000, 0x08000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
    if (enables & GXM_VGA_B8)
        mem_set_mem_state_both(0xb8000, 0x08000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);

    gxm_video_set_vga(gxm_video, enables);

    shadowbios       = 0;
    shadowbios_write = 0;
    for (int i = 0; i < 16; i++) {
        const uint8_t  ctl  = (dev->bc_xmap[1 + (i >> 3)] >> ((i & 7) << 2)) & 0x0f;
        const uint32_t base = 0xc0000 + (i << 14);

        mem_set_mem_state_both(base, 0x4000, gxm_region_state(ctl));
        if (base >= 0xe0000) {
            shadowbios |= !!(ctl & REGION_RE);
            shadowbios_write |= !!(ctl & REGION_WE);
        }
    }

    flushmmucache_nopc();
}

/* VGA I/O, trapped into an SMI (4.2.3, 5.2.3.2): the cycle goes nowhere, a
   read floats, and the handler gives the result. */
static void
gxm_vga_io_trap(uint16_t port, int size, int write, uint32_t val)
{
    if (!in_smm) {
        gxm_dev->pm_stat_smi |= GXM_PM_SMI_IO;
        cpu_gxm_smi_io(port, size, write, val);
    }
}

static uint8_t
gxm_vga_io_readb(uint16_t port, UNUSED(void *priv))
{
    gxm_vga_io_trap(port, 1, 0, 0);
    return 0xff;
}

static uint16_t
gxm_vga_io_readw(uint16_t port, UNUSED(void *priv))
{
    gxm_vga_io_trap(port, 2, 0, 0);
    return 0xffff;
}

static uint32_t
gxm_vga_io_readl(uint16_t port, UNUSED(void *priv))
{
    gxm_vga_io_trap(port, 4, 0, 0);
    return 0xffffffff;
}

static void
gxm_vga_io_writeb(uint16_t port, uint8_t val, UNUSED(void *priv))
{
    gxm_vga_io_trap(port, 1, 1, val);
}

static void
gxm_vga_io_writew(uint16_t port, uint16_t val, UNUSED(void *priv))
{
    gxm_vga_io_trap(port, 2, 1, val);
}

static void
gxm_vga_io_writel(uint16_t port, uint32_t val, UNUSED(void *priv))
{
    gxm_vga_io_trap(port, 4, 1, val);
}

static void
gxm_vga_io_recalc(cyrix_gxm_t *dev)
{
    const uint8_t ranges = (dev->bc_xmap[0] >> 13) & 0x07;

    for (int i = 0; i < 3; i++) {
        const uint16_t base = 0x3b0 + (i << 4);

        if ((dev->vga_io ^ ranges) & (1 << i))
            io_handler(!!(ranges & (1 << i)), base, 0x0010,
                       gxm_vga_io_readb, gxm_vga_io_readw, gxm_vga_io_readl,
                       gxm_vga_io_writeb, gxm_vga_io_writew, gxm_vga_io_writel, dev);
    }

    dev->vga_io = ranges;
}

static void
gxm_bc_write(cyrix_gxm_t *dev, int reg, uint32_t val)
{
    switch (reg) {
        case 0: /* BC_DRAM_TOP */
            dev->bc_dram_top = val & 0x3fffffff;
            gxm_dram_recalc(dev);
            break;
        case 1: /* BC_XMAP_1 */
            dev->bc_xmap[0] = val & 0x1f7fe0ff;
            gxm_regions_recalc(dev);
            gxm_vga_io_recalc(dev);
            /* Address bit 20, forced to 0 except in SMM (4.2.2). */
            mem_a20_gate = !(dev->bc_xmap[0] & XMAP1_A20M);
            mem_a20_recalc();
            break;
        case 2: /* BC_XMAP_2 */
        case 3: /* BC_XMAP_3 */
            dev->bc_xmap[reg - 1] = val;
            gxm_regions_recalc(dev);
            break;

        default:
            break;
    }
}

static uint32_t
gxm_reg_readl_internal(cyrix_gxm_t *dev, uint32_t addr)
{
    uint32_t ret = 0x00000000;

    addr &= 0x0ffc;

    switch (addr) {
        case 0x000:
            ret = dev->bc_dram_top;
            break;
        case 0x004 ... 0x00c:
            ret = dev->bc_xmap[(addr - 0x004) >> 2];
            break;

        case 0x100 ... 0x2ff:
            ret = gxm_gp_read(addr + 0x8000, gxm_video);
            break;
        case 0x300 ... 0x3ff:
            ret = gxm_dc_read(addr + 0x8000, gxm_video);
            break;

        case 0x400:
            ret = dev->mc_mem_cntrl1;
            break;
        case 0x404:
            ret = dev->mc_mem_cntrl2;
            break;
        case 0x408:
            ret = dev->mc_bank_cfg;
            break;
        case 0x40c:
            ret = dev->mc_sync_tim1;
            break;
        case 0x414:
            ret = dev->mc_gbase_add;
            break;
        case 0x418:
            ret = dev->mc_dr_add;
            break;
        case 0x41c:
            ret = dev->dirty_ram[dev->mc_dr_add & 0x3ff];
            break;

        case 0x500:
            ret = dev->pm_stat_smi;
            break;
        case 0x504:
            ret = dev->pm_cntrl_ten;
            break;
        case 0x508:
            ret = dev->pm_cntrl_cstp;
            break;
        case 0x50c:
            ret = dev->pm_ser_pack;
            break;

        default:
            break;
    }

    return ret;
}

/* The serial packet goes out to the CS5530's PSERIAL at the end of the
   PM_CNTRL_TEN interval that a set bit starts, and is then cleared
   (6.6.1). */
static void
gxm_ser_timer(void *priv)
{
    cyrix_gxm_t *dev = (cyrix_gxm_t *) priv;

    if (dev->pm_ser_pack && (gxm.serial_packet != NULL))
        gxm.serial_packet(dev->pm_ser_pack);
    dev->pm_ser_pack = 0x00;
}

static void
gxm_ser_event(cyrix_gxm_t *dev, uint8_t bits)
{
    static const double interval[4] = { 0.0, 1000.0, 5000.0, 10000.0 };
    const int           x_freq      = (dev->pm_cntrl_ten >> 3) & 0x03;

    dev->pm_ser_pack |= bits;
    if (dev->pm_ser_pack && x_freq && !timer_is_enabled(&dev->ser_timer))
        timer_on_auto(&dev->ser_timer, interval[x_freq]);
}

/* A write of the bytes of mask to the dword at addr. */
static void
gxm_reg_write_internal(cyrix_gxm_t *dev, uint32_t addr, uint32_t val, uint32_t mask)
{
    uint32_t old;

    addr &= 0x0ffc;

    switch (addr) {
        case 0x100 ... 0x2ff:
            gxm_gp_write(addr + 0x8000, val, mask, gxm_video);
            return;
        case 0x300 ... 0x3ff:
            gxm_dc_write(addr + 0x8000, val, mask, gxm_video);
            return;

        case 0x500: /* PM_STAT_SMI: sticky, cleared by a 1 */
            dev->pm_stat_smi &= ~(val & mask & 0x07);
            return;

        default:
            break;
    }

    old = gxm_reg_readl_internal(dev, addr);
    val = (old & ~mask) | (val & mask);

    cyrix_gxm_log("MediaGX: [W] GX_BASE+%04X = %08X\n", addr + 0x8000, val);

    switch (addr) {
        case 0x000 ... 0x00c:
            gxm_bc_write(dev, addr >> 2, val);
            break;

        case 0x400: /* MC_MEM_CNTRL1 */
            dev->mc_mem_cntrl1 = val & 0xff9fffff;
            gxm_remap(dev);
            break;
        case 0x404: /* MC_MEM_CNTRL2 */
            dev->mc_mem_cntrl2 = val & 0x0003fffb;
            break;
        case 0x408: /* MC_BANK_CFG */
            dev->mc_bank_cfg = val & 0x57705770;
            gxm_dram_recalc(dev);
            break;
        case 0x40c: /* MC_SYNC_TIM1 */
            dev->mc_sync_tim1 = val & 0x7ff7777f;
            break;
        case 0x414: /* MC_GBASE_ADD */
            dev->mc_gbase_add = val & 0x0003f7ff;
            gxm_gmem_recalc(dev);
            break;
        case 0x418: /* MC_DR_ADD */
            dev->mc_dr_add = val & 0x3ff;
            break;
        case 0x41c: /* MC_DR_ACC */
            dev->dirty_ram[dev->mc_dr_add & 0x3ff] = val & 0x03;
            break;

        case 0x504: /* PM_CNTRL_TEN; X_TEST sends the packet now, reads as 0 */
            dev->pm_cntrl_ten = val & 0x1f;
            if (val & 0x20) {
                timer_disable(&dev->ser_timer);
                gxm_ser_timer(dev);
            } else if (!(val & 0x18))
                timer_disable(&dev->ser_timer);
            else
                gxm_ser_event(dev, 0x00);
            break;
        case 0x508: /* PM_CNTRL_CSTP */
            dev->pm_cntrl_cstp = val & 0x01;
            break;
        case 0x50c: /* PM_SER_PACK */
            dev->pm_ser_pack = val & 0xc3;
            gxm_ser_event(dev, 0x00);
            break;

        default:
            break;
    }
}

static uint8_t
gxm_reg_read(uint32_t addr, void *priv)
{
    return gxm_reg_readl_internal((cyrix_gxm_t *) priv, addr) >> ((addr & 3) << 3);
}

static uint16_t
gxm_reg_readw(uint32_t addr, void *priv)
{
    return gxm_reg_readl_internal((cyrix_gxm_t *) priv, addr) >> ((addr & 2) << 3);
}

static uint32_t
gxm_reg_readl(uint32_t addr, void *priv)
{
    return gxm_reg_readl_internal((cyrix_gxm_t *) priv, addr);
}

static void
gxm_reg_write(uint32_t addr, uint8_t val, void *priv)
{
    const int shift = (addr & 3) << 3;

    gxm_reg_write_internal((cyrix_gxm_t *) priv, addr, val << shift, 0xff << shift);
}

static void
gxm_reg_writew(uint32_t addr, uint16_t val, void *priv)
{
    const int shift = (addr & 2) << 3;

    gxm_reg_write_internal((cyrix_gxm_t *) priv, addr, val << shift, 0xffff << shift);
}

static void
gxm_reg_writel(uint32_t addr, uint32_t val, void *priv)
{
    gxm_reg_write_internal((cyrix_gxm_t *) priv, addr, val, 0xffffffff);
}

/* PM_STAT_SMI bits for the SMIs the display hardware and the SMI# pin
   make. */
void
cyrix_gxm_smi_status(uint8_t status)
{
    if (gxm_dev != NULL)
        gxm_dev->pm_stat_smi |= status;
}

/* SMI#, from the CS5530. */
void
cyrix_gxm_smi_pin(void)
{
    cyrix_gxm_smi_status(GXM_PM_SMI_PIN);
    smi_raise();
}

/* VID_IRQ, set with the display controller's VINT (Table 6-2). SoftVGA
   enables the CS5530's SMI on it (F0 83h[2]) and does its deferred work
   there: a redraw that a trap started, and the traps it held off for it. */
void
cyrix_gxm_vid_irq(void)
{
    if (gxm_dev != NULL)
        gxm_ser_event(gxm_dev, 0x80);
}

/* The host bridge, device 0 (Table 4-41). */
static void
gxm_pci_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    cyrix_gxm_t *dev = (cyrix_gxm_t *) priv;

    if (func)
        return;

    cyrix_gxm_log("MediaGX: [W] PCI %02X = %02X\n", addr, val);

    switch (addr) {
        case 0x04:
            /* I/O and memory space are always on; bus master and parity. */
            dev->pci_conf[addr] = (val & 0x44) | 0x03;
            break;
        case 0x05:
            dev->pci_conf[addr] = val & 0x01;
            break;
        case 0x07:
            dev->pci_conf[addr] &= ~(val & 0xf9);
            break;
        case 0x0d:
            dev->pci_conf[addr] = val & 0x1f;
            break;
        case 0x40:
            dev->pci_conf[addr] = val & 0x7e;
            break;
        case 0x41:
            dev->pci_conf[addr] = val & 0x7f;
            break;
        case 0x43:
            dev->pci_conf[addr] = (val & 0xbf) | 0x40;
            break;
        case 0x44:
            dev->pci_conf[addr] = val & 0xf7;
            break;

        default:
            break;
    }
}

static uint8_t
gxm_pci_read(int func, int addr, UNUSED(int len), void *priv)
{
    const cyrix_gxm_t *dev = (cyrix_gxm_t *) priv;

    if (func)
        return 0xff;

    return dev->pci_conf[addr];
}

static void
cyrix_gxm_reset(void *priv)
{
    cyrix_gxm_t *dev = (cyrix_gxm_t *) priv;

    memset(dev->pci_conf, 0x00, sizeof(dev->pci_conf));
    dev->pci_conf[0x00] = 0x78; /* Cyrix */
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x01;
    dev->pci_conf[0x03] = 0x00;
    dev->pci_conf[0x04] = 0x07;
    dev->pci_conf[0x06] = 0x80;
    dev->pci_conf[0x07] = 0x02;
    dev->pci_conf[0x0b] = 0x06; /* Host bridge */
    dev->pci_conf[0x41] = 0x96;
    dev->pci_conf[0x43] = 0x80;

    dev->mc_mem_cntrl1 = 0x248c0040;
    dev->mc_mem_cntrl2 = 0x00000801;
    dev->mc_bank_cfg   = 0x41104110;
    dev->mc_sync_tim1  = 0x2a733225;
    dev->mc_gbase_add  = 0x00000000;
    dev->mc_dr_add     = 0x00000000;

    dev->pm_stat_smi   = 0x00;
    dev->pm_cntrl_ten  = 0x00;
    dev->pm_cntrl_cstp = 0x00;
    dev->pm_ser_pack   = 0x00;
    timer_disable(&dev->ser_timer);

    dev->bc_dram_top = 0x3fffffff;
    dev->bc_xmap[1]  = 0x00000000;
    dev->bc_xmap[2]  = 0x00000000;
    gxm_bc_write(dev, 1, 0x00000000);
    gxm_dram_recalc(dev);
    gxm_gmem_recalc(dev);

    gxm_remap(dev);
}

/* A CPU-only reset (the keyboard controller's, or Setup's exit) resets the
   integrated functions with the core, the display controller included. */
static void
gxm_cpu_reset(void *priv)
{
    cyrix_gxm_reset(priv);

    if (gxm_video != NULL)
        cyrix_gxm_video_device.reset(gxm_video);
}

static void
cyrix_gxm_close(void *priv)
{
    cyrix_gxm_t *dev = (cyrix_gxm_t *) priv;

    gxm.gcr_write = NULL;
    gxm.reset     = NULL;
    gxm.priv      = NULL;
    gxm_dev       = NULL;

    mem_a20_gate = 1;

    free(dev);
}

static void *
cyrix_gxm_init(UNUSED(const device_t *info))
{
    cyrix_gxm_t *dev = (cyrix_gxm_t *) calloc(1, sizeof(cyrix_gxm_t));

    gxm_dev = dev;

    pci_add_card(PCI_ADD_NORTHBRIDGE, gxm_pci_read, gxm_pci_write, dev, &dev->pci_slot);

    mem_mapping_add(&dev->scratch_mapping, 0, 0,
                    gxm_scratch_read, gxm_scratch_readw, gxm_scratch_readl,
                    gxm_scratch_write, gxm_scratch_writew, gxm_scratch_writel,
                    NULL, MEM_MAPPING_EXTERNAL | MEM_MAPPING_CPU_ONLY, dev);
    mem_mapping_add(&dev->reg_mapping, 0, 0,
                    gxm_reg_read, gxm_reg_readw, gxm_reg_readl,
                    gxm_reg_write, gxm_reg_writew, gxm_reg_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_add(&dev->gmem_mapping, 0, 0,
                    gxm_gmem_read, gxm_gmem_readw, gxm_gmem_readl,
                    gxm_gmem_write, gxm_gmem_writew, gxm_gmem_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_add(&dev->dram_mapping, 0, 0,
                    gxm_dram_read, gxm_dram_readw, gxm_dram_readl,
                    gxm_dram_write, gxm_dram_writew, gxm_dram_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_add(&dev->dram_mapping_hi, 0, 0,
                    gxm_dram_read, gxm_dram_readw, gxm_dram_readl,
                    gxm_dram_write, gxm_dram_writew, gxm_dram_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_disable(&dev->scratch_mapping);
    mem_mapping_disable(&dev->reg_mapping);
    mem_mapping_disable(&dev->gmem_mapping);
    mem_mapping_disable(&dev->dram_mapping);
    mem_mapping_disable(&dev->dram_mapping_hi);
    mem_mapping_add(&dev->smm_mapping, 0, 0,
                    gxm_smm_read, gxm_smm_readw, gxm_smm_readl,
                    gxm_smm_write, gxm_smm_writew, gxm_smm_writel,
                    ram + 0xa0000, MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_disable(&dev->smm_mapping);

    gxm_dimms_init(dev);

    timer_add(&dev->ser_timer, gxm_ser_timer, dev, 0);

    device_add(&cyrix_gxm_video_device);

    gxm.gcr_write = gxm_gcr_write;
    gxm.reset     = gxm_cpu_reset;
    gxm.priv      = dev;

    /* Address bit 20 is unmasked for SMI accesses (BC_XMAP_1[21]). */
    unmask_a20_in_smm = 1;

    cyrix_gxm_reset(dev);

    return dev;
}

const device_t cyrix_gxm_device = {
    .name          = "Cyrix MediaGX MMX Integrated Functions",
    .internal_name = "cyrix_gxm",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = cyrix_gxm_init,
    .close         = cyrix_gxm_close,
    .reset         = cyrix_gxm_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
