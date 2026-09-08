/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the IBM PS/2 Memory Expansion Option (MXO, 
 *          a.k.a. "Holster"), and the 1-8MB 286 Memory Expansion Adapter
 *          (XMA/A, a.k.a. "Catskill") with its 2-8MB 286/386SX variant.
 *
 *          NOTE: The register and translate table layout are from MS-DOS 4.0
 *                XMA2EMS.ASM device driver source. For copyright information, 
 *                see https://github.com/microsoft/MS-DOS/blob/main/v4.0/LICENSE
 *                for more details.
 *
 * Authors: WNT50
 * 
 *          Copyright 2026 WNT50.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mca.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/plat_unused.h>

/* On-board 8KB init ROM. Its window is selected by the reference disk through
   POS 104h bits 7-4 as listed by @F7FE.ADF: code 4-15 map ROM to the twelve 8KB
   spaces C8000h-DFFFFh, code 1 means the ROM is not mapped. The 2-8MB variant
   uses the same register logic with a different init ROM (57F2905, V3). */
#define XMA_BIOS_FILE_F7FE   "roms/memcard/ibm_xma/11F8060.BIN"
#define XMA_BIOS_FILE_F7F7   "roms/memcard/ibm_xma/57F2905.BIN"
#define XMA_BIOS_SIZE        0x2000U
#define XMA_BIOS_MASK        (XMA_BIOS_SIZE - 1)

/* MXO translate table: 1K x 8-bit entries, 16 KB blocks. */
#define MXO_TT_ENTRIES      1024U
#define MXO_BLOCK_SHIFT     14

/* XMA/A translate table: 4K entries holding 12-bit entries, 4 KB blocks. */
#define XMA_TT_ENTRIES      4096U
#define XMA_BLOCK_SHIFT     12

/* EMS page-frame scan window (A0000h-E0000h), one mapping per slot. */
#define MXO_PF_SCAN_FIRST   (0xa0000U >> MXO_BLOCK_SHIFT)   /* 0x28 */
#define MXO_PF_SCAN_LAST    (0xe0000U >> MXO_BLOCK_SHIFT)   /* 0x38 */
#define MXO_PF_SLOTS        (MXO_PF_SCAN_LAST - MXO_PF_SCAN_FIRST) /* per-16K page-frame slots */

#define XMA_PF_SCAN_FIRST   (0xa0000U >> XMA_BLOCK_SHIFT)   /* 0xA0 */
#define XMA_PF_SCAN_LAST    (0xe0000U >> XMA_BLOCK_SHIFT)   /* 0xE0 */
#define XMA_PF_SLOTS        (XMA_PF_SCAN_LAST - XMA_PF_SCAN_FIRST) /* per-4K page-frame slots */

/* Extended-memory home; follows the planar memory size. */
#define MXO_EXT_BASE        0x160000U /* 1M + 384K: default card home */
#define MXO_EXT_FIRST_TT    (MXO_EXT_BASE >> MXO_BLOCK_SHIFT) /* 0x58 */

#define XMA_EXT_BASE        0x160000U /* 1M + 384K: default card home */
#define XMA_EXT_FIRST_TT    (XMA_EXT_BASE >> XMA_BLOCK_SHIFT) /* 0x160 */

typedef struct ram_t {
    uint32_t  size_kb;  /* RAM size in KB */
    uint8_t  *ptr;      /* card RAM buffer */
} ram_t;

typedef struct mxo_t {
    ram_t         ram;     /* fitted card memory */
    uint32_t      blocks;  /* usable capacity in blocks */

    uint16_t      tt_ptr;  /* 10-bit translate table pointer */
    uint8_t       tt[MXO_TT_ENTRIES];

    mem_mapping_t pf_map[MXO_PF_SLOTS]; /* per-16K EMS page-frame slots */
    mem_mapping_t ext_mapping;          /* extended memory at 1M + 384K */

    uint8_t       pos_regs[8];
} mxo_t;

typedef struct xma_t {
    ram_t         ram;     /* fitted card memory */
    uint32_t      blocks;  /* usable capacity in 4K blocks */

    uint16_t      tt[XMA_TT_ENTRIES];
    uint16_t      tt_ptr;  /* 12-bit translate table pointer */
    uint8_t       idreg;   /* selected bank/task (virtual mode) */
    uint8_t       mode;    /* 31A7 mode register */
    uint8_t       tt_hi;   /* pending TT data high byte */
    uint8_t       tt_lo;   /* pending TT data low byte */
    uint8_t       tt_hv;   /* high byte latched */
    uint8_t       tt_lv;   /* low byte latched */

    mem_mapping_t pf_map[XMA_PF_SLOTS]; /* per-4K EMS page-frame slots */
    mem_mapping_t ext_mapping;          /* extended memory at 1M + 384K */

    uint8_t       pf_state[XMA_PF_SLOTS]; /* last programmed pf slot state (0 = off, 1 = on) */
    uint16_t      ext_lo;                 /* last programmed ext window base TT index */
    uint16_t      ext_hi;                 /* last programmed ext window top TT index */

    rom_t         bios_rom;   /* on-board 8KB init ROM */
    uint32_t      bios_base;  /* ROM window base (0 = disabled) */
    uint8_t       rom_space;  /* ROM space code from 104h bits 7-4 (1 = none, 4-15 = spaces) */

    uint8_t       slot;       /* MCA slot index assigned by mca_add() */
    uint8_t       pos_regs[8];
} xma_t;

/* MXO entries: bit 7 = mapping enabled, bits 6-0 = 16 KB block number. */
#define MXO_BLOCK_SIZE      (1U << MXO_BLOCK_SHIFT)
#define MXO_TT_ENABLE       0x80U   /* bit 7 set = mapping enabled */
#define MXO_TT_BLOCK_MASK   0x7fU   /* bits 6-0 = 16 KB block number */

/* Memory capacity, fitted as 512 KB kits (banks). */
#define MXO_SIZE_2MB        (2 * 1024 * 1024U)
#define MXO_KB_PER_BANK     512U    /* each 512 KB memory kit / bank */
#define MXO_MAX_BANKS       (MXO_SIZE_2MB / MXO_BLOCK_SIZE / (MXO_KB_PER_BANK >> 4)) /* 4 */

/* XMA entries: bit 11 = no translate, bits 10-0 = 4K block pointer. */
#define XMA_BLOCK_SIZE      (1U << XMA_BLOCK_SHIFT)
#define XMA_TT_INHIBIT      0x0800U /* bit 11 set = no translate */
#define XMA_TT_MASK         0x0fffU /* keep the 12-bit entry */
#define XMA_TT_BLOCK_MASK   0x07ffU /* bits 10-0 = 4K block pointer */

/* Memory capacity, fitted as half-meg kits across up to four banks. */
#define XMA_KB_PER_HALFM    512U    /* half-meg in KB */
#define XMA_MAX_BLOCKS      2048U   /* 8 MB in 4K blocks */
#define XMA_MAX_BANKS       4U      /* banks 1-4, 2 MB each max */
#define XMA_ROM_SLEEP       0x20U   /* 102h bit 5 */

/* Virtual (banked) mode registers, a fixed 16-bit window shared by the
   XMA family (used for XMA/A when the WSP INDXMAA.SYS driver banks it):
   31A0h word  TT pointer (bank in bits 11-8, 4K block in bits 7-0)
   31A2h word  TT data (no pointer advance)
   31A4h word  TT data with auto-increment of the pointer
   31A6h       bank/task ID register
   31A7h       mode register (bit 1 = virtual mode)
   31A8h       DMA capture register (diagnostics only)
   In virtual mode the 4096-entry TT is addressed as 16 banks x 256
   4K entries, so each bank owns a 1 MB window; CPU accesses to the
   first megabyte are translated through the currently selected bank. */
#define XMA_TT_POINTER       0x31A0U
#define XMA_TT_DATA          0x31A2U
#define XMA_TT_AIDATA        0x31A4U
#define XMA_ID_REG           0x31A6U
#define XMA_MODE_REG         0x31A7U
#define XMA_DMA_CAPT         0x31A8U
#define XMA_VIRT_BIT         0x02U /* 31A7 bit 1 = banked (virtual) mode */
#define XMA_TASKS            16U   /* bank/task contexts, 256 x 4K each */

#ifdef ENABLE_XMA_LOG
int xma_do_log = ENABLE_XMA_LOG;

static void
xma_log(const char *fmt, ...)
{
    va_list ap;

    if (xma_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define xma_log(fmt, ...)
#endif

/*
 * MXO - IBM Memory Expansion Option (a.k.a. "Holster"), 512 KB - 2 MB
 * for the PS/2 Model 50/60.
 *
 * The MXO uses a 1K-entry translate table holding 8-bit entries that
 * select 16 KB blocks of card memory; bit 7 of an entry enables it.
 * XMA2EMS.SYS drives the card through the real-mode 10X register set:
 *
 *   100h/101h  card ID (FE/FE, read-only)
 *   102h       control register: bit 0 = awake/enable, bits 1-7 encode
 *              which 512 KB banks are fitted
 *   103h       translate table data: a write enters the byte selected
 *              by the TT pointer
 *   105h       channel check / bank presence (read-only)
 *   106h/107h  translate table pointer (low/high byte)
 *
 * On reset the table maps the fitted memory identity-style at 1M + 384K
 * (extended-memory home); the EMS page-frame slots A0000h-E0000h can be
 * re-pointed at any 16 KB block of the card.
 *
 * The register and translate table layout follow the MS-DOS 4.0 XMA2EMS 
 * device driver source (XMA2EMS.ASM / PS2_5060.INC).
 */

/* Translate table entry check: enabled and a valid block number. */
static uint8_t
mxo_tt_enabled(const mxo_t *dev, uint16_t idx)
{
    uint8_t entry;

    if (idx >= MXO_TT_ENTRIES)
        return 0;

    entry = dev->tt[idx];
    return ((entry & MXO_TT_ENABLE) && ((entry & MXO_TT_BLOCK_MASK) < dev->blocks));
}

/* TT-gated access: reads/writes only reach card RAM if the TT entry
   covering the address is enabled and points to a valid block. */
static uint8_t
mxo_mem_read(uint32_t addr, void *priv)
{
    mxo_t *dev = (mxo_t *) priv;
    uint16_t idx  = addr >> MXO_BLOCK_SHIFT;
    uint8_t entry = dev->tt[idx];

    if (!(entry & MXO_TT_ENABLE) || ((entry & MXO_TT_BLOCK_MASK) >= dev->blocks))
        return 0xff;

    return dev->ram.ptr[((uint32_t) (entry & MXO_TT_BLOCK_MASK) << MXO_BLOCK_SHIFT) + (addr & (MXO_BLOCK_SIZE - 1))];
}

static void
mxo_mem_write(uint32_t addr, uint8_t val, void *priv)
{
    mxo_t *dev = (mxo_t *) priv;
    uint16_t idx  = addr >> MXO_BLOCK_SHIFT;
    uint8_t entry = dev->tt[idx];

    if (!(entry & MXO_TT_ENABLE) || ((entry & MXO_TT_BLOCK_MASK) >= dev->blocks))
        return;

    dev->ram.ptr[((uint32_t) (entry & MXO_TT_BLOCK_MASK) << MXO_BLOCK_SHIFT) + (addr & (MXO_BLOCK_SIZE - 1))] = val;
}

/* Enable or disable the individual 16K page-frame mappings based on
   TT entries. Each 16K slot is mapped separately so that a card never
   claims an address whose own TT entry is disabled - this is what keeps
   multiple memory cards from stealing each other's page-frame segments
   (a single min..max window would span disabled holes and swallow the
   other card's mappings). */
static void
mxo_pf_update(mxo_t *dev)
{
    for (uint16_t i = MXO_PF_SCAN_FIRST; i < MXO_PF_SCAN_LAST; i++) {
        uint8_t k = i - MXO_PF_SCAN_FIRST;

        if (mxo_tt_enabled(dev, i))
            mem_mapping_enable(&dev->pf_map[k]);
        else
            mem_mapping_disable(&dev->pf_map[k]);
    }
}

/* Position the extended-memory mapping over the currently enabled home
   entries. The card's home depends on the planar memory size (1M+384K
   with a 1MB planar, 2M+384K with a 2MB planar, etc.), so the mapping
   follows wherever the BIOS/driver actually sets the translate table. */
static void
mxo_ext_update(mxo_t *dev)
{
    uint16_t first = 0;
    uint16_t last  = 0;
    uint8_t  any   = 0;

    for (uint16_t i = MXO_EXT_FIRST_TT; i < MXO_TT_ENTRIES; i++) {
        if (mxo_tt_enabled(dev, i)) {
            if (!any)
                first = i;
            last = i;
            any  = 1;
        }
    }

    if (any) {
        mem_mapping_set_addr(&dev->ext_mapping,
                             (uint32_t) first << MXO_BLOCK_SHIFT,
                             (uint32_t) (last - first + 1) << MXO_BLOCK_SHIFT);
        mem_mapping_enable(&dev->ext_mapping);
    } else
        mem_mapping_disable(&dev->ext_mapping);
}

static uint8_t
mxo_mca_read(const uint16_t port, void *priv)
{
    const mxo_t *dev = (const mxo_t *) priv;
    uint8_t      ret;

    switch (port & 7) {
    /* The TT data port (0x03) is a live window into the translate table;
       every other register echoes its stored pos_regs[] value. */
        case 0x03: /* TT data: entry selected by the TT pointer */
            ret = dev->tt[dev->tt_ptr & (MXO_TT_ENTRIES - 1)];
            break;
        default: 
            ret = dev->pos_regs[port & 7];
            break;
    }

    xma_log("mxo_mca_read: port=%04x ret=%02x\n", port, ret);
    return ret;
}

static void
mxo_mca_write(const uint16_t port, uint8_t val, void *priv)
{
    mxo_t    *dev = (mxo_t *) priv;
    uint16_t  idx;
#ifdef ENABLE_XMA_LOG
    uint8_t   old;
#endif

    if ((port < 0x102) || (port == 0x102) || (port == 0x105))
        return;

    xma_log("mxo_mca_write: port=%04x val=%02x\n", port, val);

    /* Save the new value. */
    dev->pos_regs[port & 7] = val;

    switch (port & 7) {
        case 0x03: /* TT data: write into the entry the pointer selects */
            idx = dev->tt_ptr & (MXO_TT_ENTRIES - 1);
#ifdef ENABLE_XMA_LOG
            old = dev->tt[idx];
#endif
            dev->tt[idx] = val;
            mxo_ext_update(dev);
            mxo_pf_update(dev); /* Update memory mappings with current TT contents */
            xma_log("mxo_mca_write: TT [%03X] %02X -> %02X\n", idx, old, val);
            break;

        case 0x06: /* TT pointer (low byte) */
            dev->tt_ptr = (uint16_t) ((dev->tt_ptr & 0xff00) | val);
            break;

        case 0x07: /* TT pointer (high byte) */
            dev->tt_ptr = (uint16_t) ((dev->tt_ptr & 0x00ff) | (val << 8));
            break;
    }
}

static uint8_t
mxo_mca_feedb(void *priv)
{
    const mxo_t *dev = (const mxo_t *) priv;

    return (dev->pos_regs[2] & 1);
}

static void
mxo_reset(void *priv)
{
    mxo_t *dev = (mxo_t *) priv;

    dev->tt_ptr      = 0;
    dev->pos_regs[6] = 0;
    dev->pos_regs[7] = 0;

    mxo_ext_update(dev);
    mxo_pf_update(dev);
}

static void *
mxo_init(UNUSED(const device_t *info))
{
    mxo_t *dev;
    int    size_kb;

    dev = (mxo_t *) calloc(1, sizeof(mxo_t));

    size_kb = device_get_config_int("size");

    dev->ram.size_kb  = (uint32_t) size_kb;
    dev->ram.ptr      = (uint8_t *) calloc((size_t) size_kb << 10, 1);
    dev->blocks       = (uint32_t) size_kb >> 4; /* KB -> 16 KB blocks */

    /* POS registers: adapter card ID 0xFEFE. */
    dev->pos_regs[0] = 0xfe;
    dev->pos_regs[1] = 0xfe;

    /* POS 102h describes the fitted memory: two bits per 512 KB bank,
       '10' = fitted, '11' = empty. Bit 0 is the 'awake' enable bit: a
       board reading 0 here is treated as absent by the BIOS and QEMM,
       so all four banks fitted decode to 0xAB. */
    dev->pos_regs[2] = 0x01; /* awake/enable */
    {
        uint32_t banks = dev->ram.size_kb / MXO_KB_PER_BANK;
        dev->pos_regs[2] |= 0x02; /* bank 1 is always fitted */
        for (uint32_t b = 1; b < MXO_MAX_BANKS; b++)
            dev->pos_regs[2] |= (b < banks) ? (0x02U << (b << 1))   /* '10': fitted  */
                                            : (0x03U << (b << 1));  /* '11': no mem  */
    }
    dev->pos_regs[4] = 0xff; /* reserved POS register */

    /* POS 105h: bit 7 set for the Model 50Z BIOS to accept the card as
       a valid memory adapter, bit 0 clear (bank 1 low presence bit). */
    dev->pos_regs[5] = 0x80; /* channel check / bank-1 presence */

    /* Default TT: identity-map the card's memory at its extended-memory
       home, starting at 1M+384K. */
    for (uint32_t i = 0; i < dev->blocks; i++)
        dev->tt[MXO_EXT_FIRST_TT + i] = MXO_TT_ENABLE | i;

    /* Register the card on the MCA bus. */
    mca_add(mxo_mca_read, mxo_mca_write, mxo_mca_feedb, mxo_reset, dev);

    /* Extended memory home, provided by the card through its TT.  The
       default TT maps it at 1M+384K, so the memory is present right after
       boot; mxo_ext_update() repositions the mapping wherever the BIOS
       actually sets the translate table up. */
    mem_mapping_add(&dev->ext_mapping,
                    MXO_EXT_BASE,
                    MXO_SIZE_2MB,
                    mxo_mem_read,
                    NULL,
                    NULL,
                    mxo_mem_write,
                    NULL,
                    NULL,
                    NULL,
                    0,
                    dev);
    mxo_ext_update(dev);

    /* EMS page frame slots (A0000h-E0000h), one 16K mapping per slot.
       mxo_pf_update() enables only the slots whose TT entry is active, 
       so a card never claims a page-frame address it has not mapped. */
    for (uint8_t k = 0; k < MXO_PF_SLOTS; k++) {
        mem_mapping_add(&dev->pf_map[k],
                        (uint32_t) (MXO_PF_SCAN_FIRST + k) << MXO_BLOCK_SHIFT,
                        MXO_BLOCK_SIZE,
                        mxo_mem_read,
                        NULL,
                        NULL,
                        mxo_mem_write,
                        NULL,
                        NULL,
                        NULL,
                        0,
                        dev);
        mem_mapping_disable(&dev->pf_map[k]);
    }
    mxo_pf_update(dev);

    return dev;
}

static void
mxo_close(void *priv)
{
    mxo_t *dev = (mxo_t *) priv;

    free(dev->ram.ptr);
    free(dev);
}

/*
 * XMA/A - IBM Expanded Memory Adapter, 0-8 MB XMA (a.k.a. "Catskill").
 *
 * The XMA/A uses a 4K-entry translate table holding 12-bit entries for
 * 4 KB blocks.  On a PS/2 Model 50/60 the XMA2EMS.SYS driver drives the
 * card through the real-mode 10X register set:
 *
 *   100h/101h  card ID (FE/F7 or F7/F7, read-only)
 *   102h       control register: bits 7-6 = bank 4 descriptor, bit 5 =
 *              ROM sleep (the driver clears bit 5 to put the init ROM to
 *              sleep before touching the TT data high byte at 104h)
 *   103h/104h  translate table data (low/high byte): the two accesses
 *              latch one 12-bit entry which is written on the second
 *              access, after which the TT pointer auto-increments
 *   105h       config register: bits 5-4/3-2/1-0 = banks 3/2/1, upper
 *              two bits are channel check (ignored for sizing)
 *   106h/107h  translate table pointer (low/high byte)
 *
 * Each two-bit bank descriptor codes the two SIPs in that bank:
 * '11' no memory, '10' 256K, '01' 512K, '00' 1M SIPs.  Flipping the
 * descriptor gives the bank size in half-megabytes (128 x 4K blocks
 * each), with '00' meaning 4 half-megs instead of 3.
 *
 * The register and translate table layout follow the MS-DOS 4.0 XMA2EMS 
 * device driver source (XMA2EMS.ASM / PS2_5060.INC).
 */

/* The init ROMs claim 8KB windows inside C8000h-DFFFFh, the same range the
   translate table's page-frame slots (A0000h-E0000h) can map card RAM onto.
   On real hardware the two decodes coexist: while a TT entry covers the ROM
   window the ROM keeps answering, and once that entry is unmapped the ROM
   is simply reachable again. 86Box has a single flat mapping per address
   however, so register a new mapping removes the old one, and card RAM
   would shadow the init ROM while running its translate table self-test
   (which writes entries covering the very window the ROM executes from).
   Every mapped ROM window is registered and skipped by xma_pf_update()
   to keep the card RAM from ever shadowing it. */
static struct {
    const xma_t *dev;
    uint32_t     base;
} xma_rom_windows[8];
static uint8_t xma_rom_windows_nr;

static uint8_t
xma_addr_in_rom(uint32_t addr)
{
    for (uint8_t i = 0; i < xma_rom_windows_nr; i++) {
        if ((addr >= xma_rom_windows[i].base) && (addr < (xma_rom_windows[i].base + XMA_BIOS_SIZE)))
            return 1;
    }

    return 0;
}

static void
xma_rom_register(const xma_t *dev, uint32_t base)
{
    /* Drop any previous window of this card first. */
    for (uint8_t i = 0; i < xma_rom_windows_nr; i++) {
        if (xma_rom_windows[i].dev == dev) {
            xma_rom_windows[i] = xma_rom_windows[--xma_rom_windows_nr];
            break;
        }
    }

    if (base && (xma_rom_windows_nr < 8)) {
        xma_rom_windows[xma_rom_windows_nr].dev  = dev;
        xma_rom_windows[xma_rom_windows_nr].base = base;
        xma_rom_windows_nr++;
    }
}

/* Translate table entry check: not inhibited and a valid block number. */
static uint8_t
xma_tt_enabled(const xma_t *dev, uint16_t idx)
{
    uint16_t entry;

    if (idx >= XMA_TT_ENTRIES)
        return 0;

    entry = dev->tt[idx];
    return (!(entry & XMA_TT_INHIBIT) && ((entry & XMA_TT_BLOCK_MASK) < dev->blocks));
}

/* CPU address to TT index.  In real mode the table is addressed
   linearly over the whole 16 MB; in virtual (banked) mode accesses to
   the first megabyte go through the selected bank's 256-entry window
   ((bank << 8) | block), matching how XMA2EMS.SYS writes the table. */
static uint16_t
xma_tt_index(const xma_t *dev, uint32_t addr)
{
    uint16_t idx = (uint16_t) (addr >> XMA_BLOCK_SHIFT);

    if ((dev->mode & XMA_VIRT_BIT) && (idx < 256))
        return (uint16_t) ((dev->idreg << 8) | idx);

    return idx;
}

/* TT-gated access: reads/writes only reach card RAM if the TT entry
   covering the address is enabled and points to a valid block. */
static uint8_t
xma_mem_read(uint32_t addr, void *priv)
{
    xma_t *dev = (xma_t *) priv;
    uint16_t idx  = xma_tt_index(dev, addr);
    uint16_t entry;

    if (idx >= XMA_TT_ENTRIES)
        return 0xff;

    entry = dev->tt[idx];
    if ((entry & XMA_TT_INHIBIT) || ((entry & XMA_TT_BLOCK_MASK) >= dev->blocks))
        return 0xff;

    return dev->ram.ptr[((uint32_t) (entry & XMA_TT_BLOCK_MASK) << XMA_BLOCK_SHIFT) + (addr & (XMA_BLOCK_SIZE - 1))];
}

static void
xma_mem_write(uint32_t addr, uint8_t val, void *priv)
{
    xma_t *dev = (xma_t *) priv;
    uint16_t idx  = xma_tt_index(dev, addr);
    uint16_t entry;

    if (idx >= XMA_TT_ENTRIES)
        return;

    entry = dev->tt[idx];
    if ((entry & XMA_TT_INHIBIT) || ((entry & XMA_TT_BLOCK_MASK) >= dev->blocks))
        return;

    dev->ram.ptr[((uint32_t) (entry & XMA_TT_BLOCK_MASK) << XMA_BLOCK_SHIFT) + (addr & (XMA_BLOCK_SIZE - 1))] = val;
}

/* Enable the per-4K page-frame slots whose TT entry is active. In
   virtual mode a slot is claimed if any of the 16 banks maps it; the
   actual access is still gated by the currently selected bank.

   The init ROM and the drivers program the translate table in bulk
   loops (thousands of entries per run), and each of those writes
   lands here. mem_mapping_enable/disable() each recalculate the whole
   memory map and flush the TLB, so a slot is only touched when its
   state actually changed - that keeps a table-fill pass from costing
   one full recalculation per entry. */
static void
xma_pf_update(xma_t *dev)
{
    for (uint16_t i = XMA_PF_SCAN_FIRST; i < XMA_PF_SCAN_LAST; i++) {
        uint8_t k  = i - XMA_PF_SCAN_FIRST;
        uint8_t en;

        if (dev->mode & XMA_VIRT_BIT) {
            en = 0;
            for (uint16_t t = 0; t < XMA_TASKS && !en; t++)
                en = xma_tt_enabled(dev, (uint16_t) ((t << 8) | i));
        } else
            en = xma_tt_enabled(dev, i);

        /* Real hardware lets the ROM and the TT RAM coexist; 86Box cannot,
           so a page-frame slot falling inside any card's init ROM window
           stays unmapped and the ROM keeps executing while its own
           self-test writes TT entries covering the window. */
        if (en && xma_addr_in_rom((uint32_t) i << XMA_BLOCK_SHIFT))
            en = 0;

        if (en == dev->pf_state[k])
            continue;

        dev->pf_state[k] = en;
        if (en)
            mem_mapping_enable(&dev->pf_map[k]);
        else
            mem_mapping_disable(&dev->pf_map[k]);
    }
}

/* Re-map the linear extended-memory window so it covers exactly the
   enabled (non-inhibited) TT entries at or above the card's power-on
   home.  The init ROM repositions the card's memory by rewriting the
   translate table (at POST the blocks live at the default 1M+384K home
   only until the ROM moves them above the system memory), so the
   window must follow the table instead of being fixed at xma_init.

   During the ROM's bulk table fill the window changes only once per
   reposition, so the last computed (lo, hi) pair is kept and the
   mapping is only rewritten when the window actually moved -
   mem_mapping_set_addr() recalculates the whole memory map and would
   otherwise repeat that cost for every one of the thousands of writes
   that make up a single window. */
static void
xma_map_update(xma_t *dev)
{
    uint16_t lo = XMA_TT_ENTRIES;
    uint16_t hi = 0;

    for (uint16_t i = XMA_EXT_FIRST_TT; i < XMA_TT_ENTRIES; i++) {
        if (xma_tt_enabled(dev, i)) {
            if (lo == XMA_TT_ENTRIES)
                lo = i;
            hi = i;
        }
    }

    if ((lo == dev->ext_lo) && (hi == dev->ext_hi))
        return; /* window unchanged - mapping is already correct */

    dev->ext_lo = lo;
    dev->ext_hi = hi;

    if (lo <= hi) {
        mem_mapping_set_addr(&dev->ext_mapping,
                             (uint32_t) lo << XMA_BLOCK_SHIFT,
                             ((uint32_t) (hi - lo + 1)) << XMA_BLOCK_SHIFT);
        mem_mapping_enable(&dev->ext_mapping);
        xma_log("xma_map_update: ext window %05X-%05X\n",
                (uint32_t) lo << XMA_BLOCK_SHIFT,
                ((uint32_t) (hi + 1)) << XMA_BLOCK_SHIFT);
    } else {
        mem_mapping_disable(&dev->ext_mapping);
        xma_log("xma_map_update: ext window disabled\n");
    }
}

/* Commit a full 12-bit TT entry at the pointer and advance it. */
static void
xma_tt_commit(xma_t *dev, uint16_t entry)
{
    uint16_t idx = dev->tt_ptr & (XMA_TT_ENTRIES - 1);

    dev->tt[idx] = entry & XMA_TT_MASK;
    dev->tt_ptr  = (uint16_t) ((dev->tt_ptr + 1) & (XMA_TT_ENTRIES - 1));
    xma_pf_update(dev);
    xma_map_update(dev);
}

static void xma_bios_update(xma_t *dev);

static uint8_t
xma_mca_read(const uint16_t port, void *priv)
{
    const xma_t *dev = (const xma_t *) priv;
    uint8_t      ret;

    switch (port & 7) {
        case 0x03: /* TT data low byte */
            ret = (uint8_t) (dev->tt[dev->tt_ptr & (XMA_TT_ENTRIES - 1)] & 0xff);
            break;
        case 0x04: /* TT data high byte: low nibble = TT bits 8-11, high
                       nibble = on-board ROM space code (ADF pos[2]) */
            ret = (uint8_t) (((dev->rom_space & 0x0f) << 4) |
                             ((dev->tt[dev->tt_ptr & (XMA_TT_ENTRIES - 1)] >> 8) & 0x0f));
            break;
        case 0x06: /* TT pointer low byte */
            ret = (uint8_t) dev->tt_ptr;
            break;
        case 0x07: /* TT pointer high byte */
            ret = (uint8_t) (dev->tt_ptr >> 8);
            break;
        default:   /* 100h/101h ID, 102h control, 105h config */
            ret = dev->pos_regs[port & 7];
            break;
    }

    xma_log("xma_mca_read: port=%04x ret=%02x\n", port, ret);
    return ret;
}

static void
xma_mca_write(const uint16_t port, uint8_t val, void *priv)
{
    xma_t *dev = (xma_t *) priv;

    /* Card ID and config registers are read-only; the control register
       (102h) is written by the driver only to clear the ROM-sleep bit. */
    if ((port < 0x102) || (port == 0x105))
        return;

    xma_log("xma_mca_write: port=%04x val=%02x\n", port, val);

    switch (port & 7) {
        case 0x02: /* control register: bits 7-6 (bank 4 descriptor) are
                       read-only hardware, the rest is writable (bit 2 is
                       the module/enable bit the init ROM toggles) */
            dev->pos_regs[2] = (uint8_t) ((dev->pos_regs[2] & 0xc0) | (val & 0x3f));
            break;

        case 0x03: /* TT data low byte: finish an entry if high is pending */
            if (dev->tt_hv) {
                xma_tt_commit(dev, (uint16_t) ((dev->tt_hi << 8) | val));
                dev->tt_hv = 0;
            } else {
                dev->tt_lo = val;
                dev->tt_lv = 1;
            }
            break;

        case 0x04: /* TT data high byte: finish an entry if low is pending.
                       The upper nibble also carries the on-board ROM space
                       code (ADF pos[2]): 1 = none, 4-15 = spaces 1-12, so
                       the reference disk repositions the ROM this way. */
            if ((val >> 4) != 0) {
                dev->rom_space = (uint8_t) (val >> 4);
                xma_bios_update(dev);
            }
            if (dev->tt_lv) {
                xma_tt_commit(dev, (uint16_t) ((val << 8) | dev->tt_lo));
                dev->tt_lv = 0;
            } else {
                dev->tt_hi = val;
                dev->tt_hv = 1;
            }
            break;

        case 0x06: /* TT pointer low byte */
            dev->tt_ptr = (uint16_t) ((dev->tt_ptr & 0xff00) | val);
            break;

        case 0x07: /* TT pointer high byte */
            dev->tt_ptr = (uint16_t) ((dev->tt_ptr & 0x00ff) | (val << 8));
            break;
    }
}

static uint8_t
xma_mca_feedb(void *priv)
{
    const xma_t *dev = (const xma_t *) priv;

    return (dev->pos_regs[2] & 1);
}

/* Virtual (banked) mode ports, 0x31A0h-0x31A8h. On real hardware this
   fixed window is shared by every XMA-family adapter, but only the card
   whose MCA slot is currently selected (port 96h) responds to it - so a
   multi-card machine keeps the cards' translate tables from interfering
   with each other during the init-ROM memory test. A card that is not
   selected must answer reads with 0xFF/0xFFFF and ignore writes. */
static uint16_t
xma_tt_data16(const xma_t *dev)
{
    return (dev->tt[dev->tt_ptr & (XMA_TT_ENTRIES - 1)] & XMA_TT_MASK);
}

static uint8_t
xma_io_readb(const uint16_t port, void *priv)
{
    xma_t    *dev = (xma_t *) priv;
    uint16_t off = port & 0x000f;
    uint8_t  ret;

    if (mca_get_index() != dev->slot)
        return 0xff;

    switch (off) {
        case 0x00: /* TT pointer low byte */
            return (uint8_t) dev->tt_ptr;
        case 0x01: /* TT pointer high byte (bank + block top) */
            return (uint8_t) (dev->tt_ptr >> 8);
        case 0x02: /* TT data low byte */
            return (uint8_t) xma_tt_data16(dev);
        case 0x03: /* TT data high byte */
            return (uint8_t) (xma_tt_data16(dev) >> 8);
        case 0x04: /* auto-increment data low byte (word access advances) */
            return (uint8_t) xma_tt_data16(dev);
        case 0x05: /* auto-increment data high byte: reading it advances */
            ret = (uint8_t) (xma_tt_data16(dev) >> 8);
            dev->tt_ptr = (uint16_t) ((dev->tt_ptr + 1) & (XMA_TT_ENTRIES - 1));
            return ret;
        case 0x06: /* bank/task ID */
            return dev->idreg;
        case 0x07: /* mode register */
            return dev->mode;
        case 0x08: /* DMA capture (diagnostics only) */
            return 0xff;
    }

    return 0xff;
}

static uint16_t
xma_io_readw(const uint16_t port, void *priv)
{
    xma_t    *dev = (xma_t *) priv;
    uint16_t data;

    if (mca_get_index() != dev->slot)
        return 0xffff;

    switch (port) {
        case XMA_TT_POINTER:
            return dev->tt_ptr;
        case XMA_TT_DATA:
            return xma_tt_data16(dev);
        case XMA_TT_AIDATA:
            data = xma_tt_data16(dev);
            dev->tt_ptr = (uint16_t) ((dev->tt_ptr + 1) & (XMA_TT_ENTRIES - 1));
            return data;
        case XMA_ID_REG:
            return dev->idreg;
        case XMA_MODE_REG:
            return dev->mode;
        case XMA_DMA_CAPT:
            return 0xffff;
    }

    return 0xffff;
}

static void
xma_io_writeb(const uint16_t port, uint8_t val, void *priv)
{
    xma_t    *dev = (xma_t *) priv;
    uint16_t off = port & 0x000f;

    if (mca_get_index() != dev->slot)
        return;

    switch (off) {
        case 0x00: /* TT pointer low byte */
            dev->tt_ptr = (uint16_t) ((dev->tt_ptr & 0xff00) | val);
            break;
        case 0x01: /* TT pointer high byte */
            dev->tt_ptr = (uint16_t) ((dev->tt_ptr & 0x00ff) | ((val & 0x0f) << 8));
            break;
        case 0x02: /* TT data low byte */
            dev->tt[dev->tt_ptr & (XMA_TT_ENTRIES - 1)] =
                (uint16_t) ((dev->tt[dev->tt_ptr & (XMA_TT_ENTRIES - 1)] & 0xf00) | val);
            xma_pf_update(dev);
            xma_map_update(dev);
            break;
        case 0x03: /* TT data high byte */
            dev->tt[dev->tt_ptr & (XMA_TT_ENTRIES - 1)] =
                (uint16_t) (((uint16_t) (val & 0x0f) << 8) | (dev->tt[dev->tt_ptr & (XMA_TT_ENTRIES - 1)] & 0x0ff));
            xma_pf_update(dev);
            xma_map_update(dev);
            break;
        case 0x04: /* auto-increment data low byte */
            dev->tt[dev->tt_ptr & (XMA_TT_ENTRIES - 1)] =
                (uint16_t) ((dev->tt[dev->tt_ptr & (XMA_TT_ENTRIES - 1)] & 0xf00) | val);
            xma_pf_update(dev);
            xma_map_update(dev);
            break;
        case 0x05: /* auto-increment data high byte: write advances */
            dev->tt[dev->tt_ptr & (XMA_TT_ENTRIES - 1)] =
                (uint16_t) (((uint16_t) (val & 0x0f) << 8) | (dev->tt[dev->tt_ptr & (XMA_TT_ENTRIES - 1)] & 0x0ff));
            dev->tt_ptr = (uint16_t) ((dev->tt_ptr + 1) & (XMA_TT_ENTRIES - 1));
            xma_pf_update(dev);
            xma_map_update(dev);
            break;
        case 0x06: /* bank/task ID */
            dev->idreg = val & 0x0f;
            xma_pf_update(dev);
            break;
        case 0x07: /* mode register */
            dev->mode = val & 0x03;
            xma_pf_update(dev);
            break;
        case 0x08: /* DMA capture: no effect */
            break;
    }
}

static void
xma_io_writew(const uint16_t port, uint16_t val, void *priv)
{
    xma_t *dev = (xma_t *) priv;

    if (mca_get_index() != dev->slot)
        return;

    switch (port) {
        case XMA_TT_POINTER:
            dev->tt_ptr = val & (XMA_TT_ENTRIES - 1);
            break;
        case XMA_TT_DATA:
            dev->tt[dev->tt_ptr & (XMA_TT_ENTRIES - 1)] = val & XMA_TT_MASK;
            xma_pf_update(dev);
            xma_map_update(dev);
            break;
        case XMA_TT_AIDATA:
            dev->tt[dev->tt_ptr & (XMA_TT_ENTRIES - 1)] = val & XMA_TT_MASK;
            dev->tt_ptr = (uint16_t) ((dev->tt_ptr + 1) & (XMA_TT_ENTRIES - 1));
            xma_pf_update(dev);
            xma_map_update(dev);
            break;
        case XMA_ID_REG:
            dev->idreg = val & 0x0f;
            xma_pf_update(dev);
            break;
        case XMA_MODE_REG:
            dev->mode = val & 0x03;
            xma_pf_update(dev);
            break;
    }
}

static uint8_t
xma_bios_read(const uint32_t addr, void *priv)
{
    xma_t *dev = (xma_t *) priv;

    return rom_read(addr, &dev->bios_rom);
}

static uint16_t
xma_bios_readw(const uint32_t addr, void *priv)
{
    xma_t *dev = (xma_t *) priv;

    return rom_readw(addr, &dev->bios_rom);
}

static uint32_t
xma_bios_readl(const uint32_t addr, void *priv)
{
    xma_t *dev = (xma_t *) priv;

    return rom_readl(addr, &dev->bios_rom);
}

/* Put the init ROM at the window selected by the ROM space code
   (4-15 = C8000h + (code-4)*2000h; anything else disables it). */
static void
xma_bios_update(xma_t *dev)
{
    if ((dev->rom_space >= 4) && (dev->rom_space <= 15) && dev->bios_rom.rom) {
        dev->bios_base = 0xc8000U + ((uint32_t) (dev->rom_space - 4) << 13);
        mem_mapping_enable(&dev->bios_rom.mapping);
        mem_mapping_set_addr(&dev->bios_rom.mapping, dev->bios_base, XMA_BIOS_SIZE);
        xma_log("xma_bios_update: ROM space %u -> %05X\n", dev->rom_space, dev->bios_base);
        xma_rom_register(dev, dev->bios_base);
    } else {
        dev->bios_base = 0;
        mem_mapping_disable(&dev->bios_rom.mapping);
        xma_log("xma_bios_update: ROM disabled\n");
        xma_rom_register(dev, 0);
    }

    /* Re-evaluate the page-frame slots: slots leaving the ROM window may
       become available, slots entering it must be masked off. */
    xma_pf_update(dev);
}

static void
xma_reset(void *priv)
{
    xma_t *dev = (xma_t *) priv;

    /* A reset takes the card back to the power-on state: the translate
       table is cleared, so the card presents no memory until its init
       ROM programs it again. This keeps the adapter invisible to the
       POST memory count at the start of every boot. */
    for (uint32_t i = 0; i < XMA_TT_ENTRIES; i++)
        dev->tt[i] = XMA_TT_INHIBIT;
    dev->tt_ptr = 0;
    dev->tt_hv  = 0;
    dev->tt_lv  = 0;
    dev->idreg  = 0;
    dev->mode  &= ~XMA_VIRT_BIT;

    xma_pf_update(dev);
    xma_map_update(dev);
}

static void *
xma_init(const device_t *info)
{
    xma_t *dev;
    int   size_kb;
    uint32_t banks;
    const char *bios_file = (info->local == 1) ? XMA_BIOS_FILE_F7F7 : XMA_BIOS_FILE_F7FE;

    dev = (xma_t *) calloc(1, sizeof(xma_t));

    /* Memory size comes from the config dialog: 1 - 8 MB in 1 MB steps.
       Each 1 MB step is two half-megs, so every bank stays on a whole
       MB boundary and the sizing math below always divides evenly. */
    size_kb = device_get_config_int("size");

    dev->ram.size_kb = (uint32_t) size_kb;
    dev->ram.ptr     = (uint8_t *) calloc((size_t) size_kb << 10, 1);
    dev->blocks      = (uint32_t) size_kb >> 2; /* KB -> 4 KB blocks */

    /* POS registers: 1-8MB XMA/A (local 0) is FEF7, the 2-8MB 286/386SX
       variant (local 1) is F7F7 (100h=F7, 101h=F7). */
    dev->pos_regs[0] = (info->local == 1) ? 0xf7 : 0xfe;
    dev->pos_regs[1] = 0xf7;

    /* Encode the fitted memory into the config (105h, banks 1-3) and control
       (102h bits 7-6, bank 4) registers.  The two card variants have different
       socket layouts and use different bank descriptor tables in their init ROMs:
       - 2-8MB 286/386SX (F7F7): four SIMM sockets, one per bank, holding 1 MB or 
         2 MB modules - descriptor '00' = 1 MB, '10' = 2 MB, '01'/'11' = empty
         (the ROM sums the descriptors directly).
       - 1-8MB 286 (F7FE): eight SIP sockets as four banks of SIPs, holding
         up to 1 MB per SIP - descriptor '00' = 2 x 1 MB, '01' = 2 x 512 KB, 
         '10' = 2 x 256 KB and '11' = empty. The ROM doubles the accumulated 
         sum (shl 1), so '00' means 2 MB per bank. */
    banks = (uint32_t) size_kb / XMA_KB_PER_HALFM;
    {
        uint8_t hm[XMA_MAX_BANKS] = { 0, 0, 0, 0 };
        uint8_t desc[XMA_MAX_BANKS];

        for (uint32_t b = 0; b < XMA_MAX_BANKS && banks; b++) {
            uint32_t t = (banks > 4) ? 4 : banks;

            hm[b]    = (uint8_t) t;
            banks   -= t;
        }
        if (info->local == 1)
            for (uint32_t b = 0; b < XMA_MAX_BANKS; b++)
                desc[b] = (hm[b] == 0) ? 3 : ((hm[b] == 2) ? 0 : 2);
        else
            for (uint32_t b = 0; b < XMA_MAX_BANKS; b++)
                desc[b] = (hm[b] == 0) ? 3 : ((hm[b] == 2) ? 1 : 0);

        dev->pos_regs[2] = (uint8_t) ((desc[3] << 6) | XMA_ROM_SLEEP);
        /* 105h: bank 1-3 descriptors in bits 5-0; bits 7-6 read as 1s
           (channel-check clear), which the init ROM checks after writing. */
        dev->pos_regs[5] = (uint8_t) (0xc0 | desc[0] | (desc[1] << 2) | (desc[2] << 4));
    }

    /* Default TT: every entry inhibited.  The card presents no memory
       to the system until its init ROM programs the translate table, so
       the POST memory count does not see (and mis-size) the adapter. */
    for (uint32_t i = 0; i < XMA_TT_ENTRIES; i++)
        dev->tt[i] = XMA_TT_INHIBIT;

    /* Register the card on the MCA bus. */
    dev->slot = mca_add(xma_mca_read, xma_mca_write, xma_mca_feedb, xma_reset, dev);

    /* Extended-memory home at 1M+384K; xma_mem_read/write() gate access
       through the TT so inhibited entries simply read empty. The mapping 
       starts out disabled - the default all-inhibited table leaves no 
       window - and xma_map_update() sizes and enables it as the init
       ROM programs the table. */
    mem_mapping_add(&dev->ext_mapping,
                    XMA_EXT_BASE,
                    (uint32_t) dev->blocks << XMA_BLOCK_SHIFT,
                    xma_mem_read,
                    NULL,
                    NULL,
                    xma_mem_write,
                    NULL,
                    NULL,
                    NULL,
                    0,
                    dev);
    mem_mapping_disable(&dev->ext_mapping);

    /* EMS page frame slots (A0000h-E0000h), one 4K mapping per slot.
       Only slots whose TT entry is active get enabled, so the card
       never claims an address it has not mapped. */
    for (uint8_t k = 0; k < XMA_PF_SLOTS; k++) {
        mem_mapping_add(&dev->pf_map[k],
                        (uint32_t) (XMA_PF_SCAN_FIRST + k) << XMA_BLOCK_SHIFT,
                        XMA_BLOCK_SIZE,
                        xma_mem_read,
                        NULL,
                        NULL,
                        xma_mem_write,
                        NULL,
                        NULL,
                        NULL,
                        0,
                        dev);
        mem_mapping_disable(&dev->pf_map[k]);
    }
    xma_pf_update(dev);
    xma_map_update(dev); /* size the linear window to the default TT map */

    /* Load the 8KB init ROM but keep it disabled until the reference disk
       selects a window through POS 104h bits 7-4 (xma_bios_update). The ROM 
       must not be mapped at power-on so that two adapters in one machine do
       not shadow each other at a fixed address; the card only presents its
       ROM at the POS-assigned window. */
    dev->rom_space = 1; /* space 1 = ROM not mapped */
    rom_init(&dev->bios_rom, bios_file,
             0xc8000U, XMA_BIOS_SIZE, XMA_BIOS_MASK, 0, MEM_MAPPING_EXTERNAL);
    mem_mapping_set_handler(&dev->bios_rom.mapping,
                            xma_bios_read, xma_bios_readw, xma_bios_readl,
                            NULL, NULL, NULL);
    mem_mapping_set_p(&dev->bios_rom.mapping, dev);
    mem_mapping_set_exec(&dev->bios_rom.mapping, dev->bios_rom.rom);
    xma_log("xma_init: on-board init ROM loaded from %s (waiting for POS)\n", bios_file);
    mem_mapping_disable(&dev->bios_rom.mapping);

    /* Register the fixed virtual-mode window (0x31A0h-0x31A8h).  Only a
       single card can be used in virtual mode, so a second XMA/A in the
       same machine simply takes over the same window, like the hardware. */
    io_sethandler(XMA_TT_POINTER, 0x000a,
                  xma_io_readb, xma_io_readw, NULL,
                  xma_io_writeb, xma_io_writew, NULL,
                  dev);

    return dev;
}

static void
xma_close(void *priv)
{
    xma_t *dev = (xma_t *) priv;

    io_removehandler(XMA_TT_POINTER, 0x000a,
                     xma_io_readb, xma_io_readw, NULL,
                     xma_io_writeb, xma_io_writew, NULL,
                     dev);
    xma_rom_register(dev, 0);
    free(dev->ram.ptr);
    free(dev);
}

static int
xma_f7fe_available(void)
{
    return rom_present(XMA_BIOS_FILE_F7FE);
}

static int
xma_f7f7_available(void)
{
    return rom_present(XMA_BIOS_FILE_F7F7);
}

static const device_config_t ibm_xma_mca_config[] = {
    {
        .name           = "size",
        .description    = "Memory size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 512,
        .file_filter    = NULL,
        .spinner        = {
            .min  = 512,
            .max  = 2048,
            .step = 512
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

const device_t ibm_xma_mca_2mb_device = {
    .name          = "IBM 512KB/2MB 286 Memory Expansion Adapter",
    .internal_name = "ibm_xma_mca_2mb_device",
    .flags         = DEVICE_MCA,
    .local         = 0,
    .init          = mxo_init,
    .close         = mxo_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ibm_xma_mca_config
};

static const device_config_t ibm_xma_mca_8mb_f7fe_config[] = {
    {
        .name           = "size",
        .description    = "Memory size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 1024,
        .file_filter    = NULL,
        .spinner        = {
            .min  = 1024,
            .max  = 8192,
            .step = 1024
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

const device_t ibm_xma_mca_8mb_f7fe_device = {
    .name          = "IBM 1-8MB 286 Memory Expansion Adapter",
    .internal_name = "ibm_xma_mca_8mb_f7fe",
    .flags         = DEVICE_MCA,
    .local         = 0,
    .init          = xma_init,
    .close         = xma_close,
    .reset         = NULL,
    .available     = xma_f7fe_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .alias         = "IBM Expanded Memory Adapter 0-8MB XMA",
    .config        = ibm_xma_mca_8mb_f7fe_config
};

static const device_config_t ibm_xma_mca_8mb_f7f7_config[] = {
    {
        .name           = "size",
        .description    = "Memory size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 2048,
        .file_filter    = NULL,
        .spinner        = {
            .min  = 2048,
            .max  = 8192,
            .step = 2048
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

const device_t ibm_xma_mca_8mb_f7f7_device = {
    .name          = "IBM 2-8MB Memory Expansion Adapter",
    .internal_name = "ibm_xma_mca_8mb_f7f7",
    .flags         = DEVICE_MCA,
    .local         = 1,
    .init          = xma_init,
    .close         = xma_close,
    .reset         = NULL,
    .available     = xma_f7f7_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .alias         = "IBM 2-8MB 286/386SX Memory Expansion Option",
    .config        = ibm_xma_mca_8mb_f7f7_config
};
