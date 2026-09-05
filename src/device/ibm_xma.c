/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the IBM Memory Expansion Option (MXO, a.k.a.
 *          "Holster") memory card for PS/2 Model 50 and 60 systems.
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
#include <86box/mca.h>
#include <86box/mem.h>
#include <86box/plat_unused.h>

/* MXO translate table: 1K x 8-bit entries, 16 KB blocks. */
#define MXO_TT_ENTRIES      1024U
#define MXO_BLOCK_SHIFT     14
#define MXO_BLOCK_SIZE      (1U << MXO_BLOCK_SHIFT)
#define MXO_TT_ENABLE       0x80U   /* bit 7 set = mapping enabled */
#define MXO_TT_BLOCK_MASK   0x7fU   /* bits 6-0 = 16 KB block number */

/* Memory capacity, fitted as 512 KB kits (banks). */
#define MXO_SIZE_2MB        (2 * 1024 * 1024U)
#define MXO_KB_PER_BANK     512U    /* each 512 KB memory kit / bank */
#define MXO_MAX_BANKS       (MXO_SIZE_2MB / MXO_BLOCK_SIZE / (MXO_KB_PER_BANK >> 4)) /* 4 */

/* Extended-memory home; follows the planar memory size. */
#define MXO_EXT_BASE        0x160000U /* 1M + 384K: default card home */
#define MXO_EXT_FIRST_TT    (MXO_EXT_BASE >> MXO_BLOCK_SHIFT) /* 0x58 */

/* EMS page-frame scan window (A0000h-E0000h), one 16K mapping per slot. */
#define MXO_PF_SCAN_FIRST   (0xa0000U >> MXO_BLOCK_SHIFT)   /* 0x28 */
#define MXO_PF_SCAN_LAST    (0xe0000U >> MXO_BLOCK_SHIFT)   /* 0x38 */
#define MXO_PF_SLOTS        (MXO_PF_SCAN_LAST - MXO_PF_SCAN_FIRST) /* per-16K page-frame slots */

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

/* Card RAM buffer and its fitted size. */
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

static const device_config_t ibm_xma_mca_config[] = {
    {
        .name           = "size",
        .description    = "Memory size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 2048,
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
