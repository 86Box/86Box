/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of VLSI 82C311 ("SCAMP") chipset.
 *
 * Note:    The datasheet mentions that the chipset supports up to 8MB
 *          of DRAM. This is intepreted as 'being able to refresh up to
 *          8MB of DRAM chips', because it works fine with bus-based
 *          memory expansion.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *
 *          Copyright 2020 Sarah Walker.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/nmi.h>
#include <86box/plat_unused.h>
#include <86box/port_92.h>
#include <86box/chipset.h>

#define CFG_ID         0x00
#define CFG_SLTPTR     0x02
#define CFG_RAMMAP     0x03
#define CFG_EMSEN1     0x0b
#define CFG_EMSEN2     0x0c
#define CFG_ABAXS      0x0e
#define CFG_CAXS       0x0f
#define CFG_DAXS       0x10
#define CFG_FEAXS      0x11

#define ID_VL82C311    0xd6

#define RAMMAP_ROMMOV  0x60
#define RAMMAP_ROMMOV1 (1 << 6)
#define RAMMAP_ROMMOV0 (1 << 5)
#define RAMMAP_REMP384 (1 << 4)

#define EMSEN1_EMSMAP  (1 << 4)
#define EMSEN1_BFENAB  (1 << 6)
#define EMSEN1_EMSENAB (1 << 7)

#define NR_ELEMS(x)    (sizeof(x) / sizeof(x[0]))

#define EMS_MAXPAGE 4
#define EMS_PGSIZE  16384
#define EMS_PGMASK  16383

#define MEM_FLAG_SLOTBUS    0x40
#define MEM_FLAG_REMAP      0x20
#define MEM_FLAG_MEMCARD    0x10
#define MEM_FLAG_EMS        0x08
#define MEM_FLAG_ROMCS      0x04
#define MEM_FLAG_READ       0x02
#define MEM_FLAG_WRITE      0x01
#define MEM_FMASK_SLOTBUS   0x40
#define MEM_FMASK_REMAP     0x20
#define MEM_FMASK_MEMCARD   0x10
#define MEM_FMASK_EMS       0x08
#define MEM_FMASK_ROMCS     0x04
#define MEM_FMASK_RW        0x03

/*Commodore SL386SX requires proper memory slot decoding to detect memory size.
  Therefore we emulate the SCAMP memory address decoding, and therefore are
  limited to the DRAM combinations supported by the actual chip*/
enum {
    BANK_NONE,
    BANK_256K,
    BANK_256K_INTERLEAVED,
    BANK_1M,
    BANK_1M_INTERLEAVED,
    BANK_4M,
    BANK_4M_INTERLEAVED
};

typedef struct ram_struct_t {
    void *parent;
    int   bank;
} ram_struct_t;

typedef struct card_mem_t {
    int       in_ram;
    uint32_t  virt_addr;
    uint32_t  phys_addr;
    uint8_t  *mem;
} mem_page_t;

typedef struct scamp_t {
    int           cfg_index;
    uint8_t       cfg_regs[256];
    int           cfg_enable;
    int           ram_config;

    int           ems_index;
    int           ems_autoinc;
    uint16_t      ems[64];

    mem_mapping_t ram_mapping[2];
    ram_struct_t  ram_struct[2];

    uint32_t      ram_virt_base[2];
    uint32_t      ram_phys_base[2];
    uint32_t      ram_mask[2];
    int           row_virt_shift[2];
    int           row_phys_shift[2];
    int           ram_interleaved[2];
    int           ibank_shift[2];

    int           mem_flags[64];
    mem_mapping_t mem_mappings[64]; /* The entire first 1 MB of memory space. */
    mem_page_t    mem_pages[64];

    uint32_t      card_mem_size;
    uint8_t      *card_mem;
    mem_page_t    card_pages[4];

    port_92_t    *port_92;
} scamp_t;

static const struct {
    int size_kb;
    int rammap;
    int bank[2];
} ram_configs[] = {
    {  512,  0x0, { BANK_256K, BANK_NONE }                        },
    { 1024,  0x1, { BANK_256K_INTERLEAVED, BANK_NONE }            },
    { 1536,  0x2, { BANK_256K_INTERLEAVED, BANK_256K }            },
    { 2048,  0x3, { BANK_256K_INTERLEAVED, BANK_256K_INTERLEAVED }},
    { 3072,  0xc, { BANK_256K_INTERLEAVED, BANK_1M }              },
    { 4096,  0x5, { BANK_1M_INTERLEAVED, BANK_NONE }              },
    { 5120,  0xd, { BANK_256K_INTERLEAVED, BANK_1M_INTERLEAVED }  },
    { 6144,  0x6, { BANK_1M_INTERLEAVED, BANK_1M }                },
    { 8192,  0x7, { BANK_1M_INTERLEAVED, BANK_1M_INTERLEAVED }    },
    { 12288, 0xe, { BANK_1M_INTERLEAVED, BANK_4M }                },
    { 16384, 0x9, { BANK_4M_INTERLEAVED, BANK_NONE }              },
};

static const struct {
    int bank[2];
    int remapped;
} rammap[16] = {
    { { BANK_256K, BANK_NONE },                         0},
    { { BANK_256K_INTERLEAVED, BANK_NONE },             0},
    { { BANK_256K_INTERLEAVED, BANK_256K },             0},
    { { BANK_256K_INTERLEAVED, BANK_256K_INTERLEAVED }, 0},

    { { BANK_1M, BANK_NONE },                           0},
    { { BANK_1M_INTERLEAVED, BANK_NONE },               0},
    { { BANK_1M_INTERLEAVED, BANK_1M },                 0},
    { { BANK_1M_INTERLEAVED, BANK_1M_INTERLEAVED },     0},

    { { BANK_4M, BANK_NONE },                           0},
    { { BANK_4M_INTERLEAVED, BANK_NONE },               0},
    { { BANK_NONE, BANK_4M },                           1}, /*Bank 2 remapped to 0*/
    { { BANK_NONE, BANK_4M_INTERLEAVED },               1}, /*Banks 2/3 remapped to 0/1*/

    { { BANK_256K_INTERLEAVED, BANK_1M },               0},
    { { BANK_256K_INTERLEAVED, BANK_1M_INTERLEAVED },   0},
    { { BANK_1M_INTERLEAVED, BANK_4M },                 0},
    { { BANK_1M_INTERLEAVED, BANK_4M_INTERLEAVED },     0}, /*Undocumented - probably wrong!*/
};

#ifdef ENABLE_SCAMP_LOG
int scamp_do_log = ENABLE_SCAMP_LOG;

static void
scamp_log(const char *fmt, ...)
{
    va_list ap;

    if (scamp_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define scamp_log(fmt, ...)
#endif

/* Read one byte from paged RAM. */
static uint8_t
scamp_mem_readb(uint32_t addr, void *priv)
{
    mem_page_t *dev = (mem_page_t *) priv;
    uint8_t     ret = 0xff;

    if (dev->mem != NULL)
        ret = *(uint8_t *) &(dev->mem[addr & EMS_PGMASK]);

    return ret;
}

/* Read one word from paged RAM. */
static uint16_t
scamp_mem_readw(uint32_t addr, void *priv)
{
    mem_page_t *dev = (mem_page_t *) priv;
    uint16_t    ret = 0xffff;

    if (dev->mem != NULL)
        ret = *(uint16_t *) &(dev->mem[addr & EMS_PGMASK]);

    return ret;
}

/* Write one byte to paged RAM. */
static void
scamp_mem_writeb(uint32_t addr, uint8_t val, void *priv)
{
    mem_page_t *dev = (mem_page_t *) priv;

    if (dev->mem != NULL)
        *(uint8_t *) &(dev->mem[addr & EMS_PGMASK]) = val;
}

/* Write one word to paged RAM. */
static void
scamp_mem_writew(uint32_t addr, uint16_t val, void *priv)
{
    mem_page_t *dev = (mem_page_t *) priv;

    if (dev->mem != NULL)
        *(uint16_t *) &(dev->mem[addr & EMS_PGMASK]) = val;
}

/* The column bits masked when using 256kbit DRAMs in 4Mbit mode aren't contiguous,
   so we use separate routines for that special case */
static uint8_t
ram_mirrored_256k_in_4mi_read(uint32_t addr, void *priv)
{
    const ram_struct_t *rs   = (ram_struct_t *) priv;
    const scamp_t      *dev  = rs->parent;
    int                 bank = rs->bank;
    int                 byte;
    int                 row;
    int                 column;

    addr -= dev->ram_virt_base[bank];
    byte = addr & 1;
    if (!dev->ram_interleaved[bank]) {
        if (addr & 0x400)
            return 0xff;

        addr   = (addr & 0x3ff) | ((addr & ~0x7ff) >> 1);
        column = (addr >> 1) & dev->ram_mask[bank];
        row    = ((addr & 0xff000) >> 13) | (((addr & 0x200000) >> 22) << 9);

        addr = byte | (column << 1) | (row << dev->row_phys_shift[bank]);
    } else {
        column = (addr >> 1) & ((dev->ram_mask[bank] << 1) | 1);
        row    = ((addr & 0x1fe000) >> 13) | (((addr & 0x400000) >> 22) << 9);

        addr = byte | (column << 1) | (row << (dev->row_phys_shift[bank] + 1));
    }

    return ram[addr + dev->ram_phys_base[bank]];
}

static void
ram_mirrored_256k_in_4mi_write(uint32_t addr, uint8_t val, void *priv)
{
    const ram_struct_t *rs   = (ram_struct_t *) priv;
    const scamp_t      *dev  = rs->parent;
    int                 bank = rs->bank;
    int                 byte;
    int                 row;
    int                 column;

    addr -= dev->ram_virt_base[bank];
    byte = addr & 1;
    if (!dev->ram_interleaved[bank]) {
        if (addr & 0x400)
            return;

        addr   = (addr & 0x3ff) | ((addr & ~0x7ff) >> 1);
        column = (addr >> 1) & dev->ram_mask[bank];
        row    = ((addr & 0xff000) >> 13) | (((addr & 0x200000) >> 22) << 9);

        addr = byte | (column << 1) | (row << dev->row_phys_shift[bank]);
    } else {
        column = (addr >> 1) & ((dev->ram_mask[bank] << 1) | 1);
        row    = ((addr & 0x1fe000) >> 13) | (((addr & 0x400000) >> 22) << 9);

        addr = byte | (column << 1) | (row << (dev->row_phys_shift[bank] + 1));
    }

    ram[addr + dev->ram_phys_base[bank]] = val;
}

/*Read/write handlers for interleaved memory banks. We must keep CPU and ram array
  mapping linear, otherwise we won't be able to execute code from interleaved banks*/
static uint8_t
ram_mirrored_interleaved_read(uint32_t addr, void *priv)
{
    const ram_struct_t *rs   = (ram_struct_t *) priv;
    const scamp_t      *dev  = rs->parent;
    int                 bank = rs->bank;
    int                 byte;
    int                 row;
    int                 column;

    addr -= dev->ram_virt_base[bank];
    byte = addr & 1;
    if (!dev->ram_interleaved[bank]) {
        if (addr & 0x400)
            return 0xff;

        addr   = (addr & 0x3ff) | ((addr & ~0x7ff) >> 1);
        column = (addr >> 1) & dev->ram_mask[bank];
        row    = (addr >> dev->row_virt_shift[bank]) & dev->ram_mask[bank];

        addr = byte | (column << 1) | (row << dev->row_phys_shift[bank]);
    } else {
        column = (addr >> 1) & ((dev->ram_mask[bank] << 1) | 1);
        row    = (addr >> (dev->row_virt_shift[bank] + 1)) & dev->ram_mask[bank];

        addr = byte | (column << 1) | (row << (dev->row_phys_shift[bank] + 1));
    }

    return ram[addr + dev->ram_phys_base[bank]];
}

static void
ram_mirrored_interleaved_write(uint32_t addr, uint8_t val, void *priv)
{
    const ram_struct_t *rs   = (ram_struct_t *) priv;
    const scamp_t      *dev  = rs->parent;
    int                 bank = rs->bank;
    int                 byte;
    int                 row;
    int                 column;

    addr -= dev->ram_virt_base[bank];
    byte = addr & 1;
    if (!dev->ram_interleaved[bank]) {
        if (addr & 0x400)
            return;

        addr   = (addr & 0x3ff) | ((addr & ~0x7ff) >> 1);
        column = (addr >> 1) & dev->ram_mask[bank];
        row    = (addr >> dev->row_virt_shift[bank]) & dev->ram_mask[bank];

        addr = byte | (column << 1) | (row << dev->row_phys_shift[bank]);
    } else {
        column = (addr >> 1) & ((dev->ram_mask[bank] << 1) | 1);
        row    = (addr >> (dev->row_virt_shift[bank] + 1)) & dev->ram_mask[bank];

        addr = byte | (column << 1) | (row << (dev->row_phys_shift[bank] + 1));
    }

    ram[addr + dev->ram_phys_base[bank]] = val;
}

static uint8_t
ram_mirrored_read(uint32_t addr, void *priv)
{
    const ram_struct_t *rs   = (ram_struct_t *) priv;
    const scamp_t      *dev  = rs->parent;
    int                 bank = rs->bank;
    int                 byte;
    int                 row;
    int                 column;

    addr -= dev->ram_virt_base[bank];
    byte   = addr & 1;
    column = (addr >> 1) & dev->ram_mask[bank];
    row    = (addr >> dev->row_virt_shift[bank]) & dev->ram_mask[bank];
    addr   = byte | (column << 1) | (row << dev->row_phys_shift[bank]);

    return ram[addr + dev->ram_phys_base[bank]];
}

static void
ram_mirrored_write(uint32_t addr, uint8_t val, void *priv)
{
    const ram_struct_t *rs   = (ram_struct_t *) priv;
    const scamp_t      *dev  = rs->parent;
    int                 bank = rs->bank;
    int                 byte;
    int                 row;
    int                 column;

    addr -= dev->ram_virt_base[bank];
    byte   = addr & 1;
    column = (addr >> 1) & dev->ram_mask[bank];
    row    = (addr >> dev->row_virt_shift[bank]) & dev->ram_mask[bank];
    addr   = byte | (column << 1) | (row << dev->row_phys_shift[bank]);

    ram[addr + dev->ram_phys_base[bank]] = val;
}

static void
recalc_mappings(void *priv)
{
    scamp_t *dev = (scamp_t *) priv;
    uint32_t virt_base  = 0;
    uint32_t old_virt_base;
    uint8_t  cur_rammap = dev->cfg_regs[CFG_RAMMAP] & 0xf;
    int      bank_nr    = 0;
    int      phys_bank;

    mem_set_mem_state_both((1 << 20), (16256 - 1024) * 1024, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
    mem_set_mem_state(0xfe0000, 0x20000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);

    for (uint8_t c = 0; c < 2; c++)
        mem_mapping_disable(&dev->ram_mapping[c]);

    /* Once the BIOS programs the correct DRAM configuration, switch to regular
       linear memory mapping */
    if (cur_rammap == ram_configs[dev->ram_config].rammap) {
        mem_mapping_disable(&ram_low_mapping);

        for (uint8_t i = 0; i < 40; i++)
            mem_mapping_enable(&(dev->mem_mappings[i]));

        if (mem_size > 1024)
            mem_set_mem_state_both((1 << 20), (mem_size - 1024) << 10, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        mem_mapping_enable(&ram_high_mapping);
    } else {
        mem_mapping_set_handler(&ram_low_mapping,
                                ram_mirrored_read, NULL, NULL,
                                ram_mirrored_write, NULL, NULL);

        mem_mapping_disable(&ram_low_mapping);

        for (uint8_t i = 0; i < 40; i++)
            mem_mapping_disable(&(dev->mem_mappings[i]));

        if (rammap[cur_rammap].bank[0] == BANK_NONE)
            bank_nr = 1;

        for (; bank_nr < 2; bank_nr++) {
            old_virt_base = virt_base;
            phys_bank     = ram_configs[dev->ram_config].bank[bank_nr];

            dev->ram_virt_base[bank_nr] = virt_base;

            if (virt_base == 0) {
                switch (rammap[cur_rammap].bank[bank_nr]) {
                    case BANK_NONE:
                        fatal("        Bank %i is empty!\n    }\n}\n", bank_nr);
                        break;

                    case BANK_256K:
                        if (phys_bank != BANK_NONE) {
                            mem_mapping_set_addr(&ram_low_mapping, 0, 0x80000);
                            mem_mapping_set_p(&ram_low_mapping, (void *) &dev->ram_struct[bank_nr]);
                        }
                        virt_base += (1 << 19);
                        dev->row_virt_shift[bank_nr] = 10;
                        break;

                    case BANK_256K_INTERLEAVED:
                        if (phys_bank != BANK_NONE) {
                            mem_mapping_set_addr(&ram_low_mapping, 0, 0xa0000);
                            mem_mapping_set_p(&ram_low_mapping, (void *) &dev->ram_struct[bank_nr]);
                        }
                        virt_base += (1 << 20);
                        dev->row_virt_shift[bank_nr] = 10;
                        break;

                    case BANK_1M:
                        if (phys_bank != BANK_NONE) {
                            mem_mapping_set_addr(&ram_low_mapping, 0, 0xa0000);
                            mem_mapping_set_p(&ram_low_mapping, (void *) &dev->ram_struct[bank_nr]);
                            mem_mapping_set_addr(&dev->ram_mapping[bank_nr], 0x100000, 0x100000);
                            mem_mapping_set_exec(&dev->ram_mapping[bank_nr], &ram[dev->ram_phys_base[bank_nr] + 0x100000]);
                            mem_set_mem_state_both((1 << 20), (1 << 20), MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                        }
                        virt_base += (1 << 21);
                        dev->row_virt_shift[bank_nr] = 11;
                        break;

                    case BANK_1M_INTERLEAVED:
                        if (phys_bank != BANK_NONE) {
                            mem_mapping_set_addr(&ram_low_mapping, 0, 0xa0000);
                            mem_mapping_set_p(&ram_low_mapping, (void *) &dev->ram_struct[bank_nr]);
                            mem_mapping_set_addr(&dev->ram_mapping[bank_nr], 0x100000, 0x300000);
                            mem_mapping_set_exec(&dev->ram_mapping[bank_nr], &ram[dev->ram_phys_base[bank_nr] + 0x100000]);
                            mem_set_mem_state_both((1 << 20), (3 << 20), MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                        }
                        virt_base += (1 << 22);
                        dev->row_virt_shift[bank_nr] = 11;
                        break;

                    case BANK_4M:
                        if (phys_bank != BANK_NONE) {
                            mem_mapping_set_addr(&ram_low_mapping, 0, 0xa0000);
                            mem_mapping_set_p(&ram_low_mapping, (void *) &dev->ram_struct[bank_nr]);
                            mem_mapping_set_addr(&dev->ram_mapping[bank_nr], 0x100000, 0x700000);
                            mem_mapping_set_exec(&dev->ram_mapping[bank_nr], &ram[dev->ram_phys_base[bank_nr] + 0x100000]);
                            mem_set_mem_state_both((1 << 20), (7 << 20), MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                        }
                        virt_base += (1 << 23);
                        dev->row_virt_shift[bank_nr] = 12;
                        break;

                    case BANK_4M_INTERLEAVED:
                        if (phys_bank != BANK_NONE) {
                            mem_mapping_set_addr(&ram_low_mapping, 0, 0xa0000);
                            mem_mapping_set_p(&ram_low_mapping, (void *) &dev->ram_struct[bank_nr]);
                            mem_mapping_set_addr(&dev->ram_mapping[bank_nr], 0x100000, 0xf00000);
                            mem_mapping_set_exec(&dev->ram_mapping[bank_nr], &ram[dev->ram_phys_base[bank_nr] + 0x100000]);
                            mem_set_mem_state_both((1 << 20), (15 << 20), MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                        }
                        virt_base += (1 << 24);
                        dev->row_virt_shift[bank_nr] = 12;
                        break;

                    default:
                        break;
                }
            } else {
                switch (rammap[cur_rammap].bank[bank_nr]) {
                    case BANK_NONE:
                        break;

                    case BANK_256K:
                        if (phys_bank != BANK_NONE) {
                            mem_mapping_set_addr(&dev->ram_mapping[bank_nr], virt_base, 0x80000);
                            mem_mapping_set_exec(&dev->ram_mapping[bank_nr], &ram[dev->ram_phys_base[bank_nr]]);
                            mem_set_mem_state_both(virt_base, (1 << 19), MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                        }
                        virt_base += (1 << 19);
                        dev->row_virt_shift[bank_nr] = 10;
                        break;

                    case BANK_256K_INTERLEAVED:
                        if (phys_bank != BANK_NONE) {
                            mem_mapping_set_addr(&dev->ram_mapping[bank_nr], virt_base, 0x100000);
                            mem_mapping_set_exec(&dev->ram_mapping[bank_nr], &ram[dev->ram_phys_base[bank_nr]]);
                            mem_set_mem_state_both(virt_base, (1 << 20), MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                        }
                        virt_base += (1 << 20);
                        dev->row_virt_shift[bank_nr] = 10;
                        break;

                    case BANK_1M:
                        if (phys_bank != BANK_NONE) {
                            mem_mapping_set_addr(&dev->ram_mapping[bank_nr], virt_base, 0x200000);
                            mem_mapping_set_exec(&dev->ram_mapping[bank_nr], &ram[dev->ram_phys_base[bank_nr]]);
                            mem_set_mem_state_both(virt_base, (1 << 21), MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                        }
                        virt_base += (1 << 21);
                        dev->row_virt_shift[bank_nr] = 11;
                        break;

                    case BANK_1M_INTERLEAVED:
                        if (phys_bank != BANK_NONE) {
                            mem_mapping_set_addr(&dev->ram_mapping[bank_nr], virt_base, 0x400000);
                            mem_mapping_set_exec(&dev->ram_mapping[bank_nr], &ram[dev->ram_phys_base[bank_nr]]);
                            mem_set_mem_state_both(virt_base, (1 << 22), MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                        }
                        virt_base += (1 << 22);
                        dev->row_virt_shift[bank_nr] = 11;
                        break;

                    case BANK_4M:
                        if (phys_bank != BANK_NONE) {
                            mem_mapping_set_addr(&dev->ram_mapping[bank_nr], virt_base, 0x800000);
                            mem_mapping_set_exec(&dev->ram_mapping[bank_nr], &ram[dev->ram_phys_base[bank_nr]]);
                            mem_set_mem_state_both(virt_base, (1 << 23), MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                        }
                        virt_base += (1 << 23);
                        dev->row_virt_shift[bank_nr] = 12;
                        break;

                    case BANK_4M_INTERLEAVED:
                        if (phys_bank != BANK_NONE) {
                            mem_mapping_set_addr(&dev->ram_mapping[bank_nr], virt_base, 0x1000000);
                            mem_mapping_set_exec(&dev->ram_mapping[bank_nr], &ram[dev->ram_phys_base[bank_nr]]);
                            mem_set_mem_state_both(virt_base, (1 << 24), MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                        }
                        virt_base += (1 << 24);
                        dev->row_virt_shift[bank_nr] = 12;
                        break;

                    default:
                        break;
                }
            }

            switch (rammap[cur_rammap].bank[bank_nr]) {
                case BANK_256K:
                case BANK_1M:
                case BANK_4M:
                    mem_mapping_set_handler(&dev->ram_mapping[bank_nr],
                                            ram_mirrored_read, NULL, NULL,
                                            ram_mirrored_write, NULL, NULL);
                    if (!old_virt_base)
                        mem_mapping_set_handler(&ram_low_mapping,
                                                ram_mirrored_read, NULL, NULL,
                                                ram_mirrored_write, NULL, NULL);
                    break;

                case BANK_256K_INTERLEAVED:
                case BANK_1M_INTERLEAVED:
                    mem_mapping_set_handler(&dev->ram_mapping[bank_nr],
                                            ram_mirrored_interleaved_read, NULL, NULL,
                                            ram_mirrored_interleaved_write, NULL, NULL);
                    if (!old_virt_base)
                        mem_mapping_set_handler(&ram_low_mapping,
                                                ram_mirrored_interleaved_read, NULL, NULL,
                                                ram_mirrored_interleaved_write, NULL, NULL);
                    break;

                case BANK_4M_INTERLEAVED:
                    if (phys_bank == BANK_256K || phys_bank == BANK_256K_INTERLEAVED) {
                        mem_mapping_set_handler(&dev->ram_mapping[bank_nr],
                                                ram_mirrored_256k_in_4mi_read, NULL, NULL,
                                                ram_mirrored_256k_in_4mi_write, NULL, NULL);
                        if (!old_virt_base)
                            mem_mapping_set_handler(&ram_low_mapping,
                                                    ram_mirrored_256k_in_4mi_read, NULL, NULL,
                                                    ram_mirrored_256k_in_4mi_write, NULL, NULL);
                    } else {
                        mem_mapping_set_handler(&dev->ram_mapping[bank_nr],
                                                ram_mirrored_interleaved_read, NULL, NULL,
                                                ram_mirrored_interleaved_write, NULL, NULL);
                        if (!old_virt_base)
                            mem_mapping_set_handler(&ram_low_mapping,
                                                    ram_mirrored_interleaved_read, NULL, NULL,
                                                    ram_mirrored_interleaved_write, NULL, NULL);
                    }
                    break;

                default:
                    break;
            }
        }
    }
}

static void
scamp_mem_update_state(scamp_t *dev, uint32_t addr, uint32_t size, uint8_t new_flags, uint8_t mask)
{
    int read_ext  = MEM_READ_EXTERNAL;
    int write_ext = MEM_WRITE_EXTERNAL;

    if ((addr < 0x00100000) && ((new_flags ^ dev->mem_flags[addr / EMS_PGSIZE]) & mask)) {
        dev->mem_flags[addr / EMS_PGSIZE] &= ~mask;
        dev->mem_flags[addr / EMS_PGSIZE] |= new_flags;

        new_flags = dev->mem_flags[addr / EMS_PGSIZE];

        if (new_flags & MEM_FLAG_ROMCS) {
            read_ext  = MEM_READ_ROMCS;
            write_ext = MEM_WRITE_ROMCS;
        }

        if (new_flags & (MEM_FLAG_REMAP | MEM_FLAG_SLOTBUS)) {
            scamp_log("scamp_mem_update_state(): %08X-%08X: %02X (REMAP)\n", addr, addr + size - 1, new_flags);
            mem_set_mem_state(addr, size, read_ext | write_ext);
        } else if (new_flags & (MEM_FLAG_EMS | MEM_FLAG_MEMCARD)) {
            scamp_log("scamp_mem_update_state(): %08X-%08X: %02X (EMS)\n", addr, addr + size - 1, new_flags);
            mem_set_mem_state(addr, size, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        } else  switch (new_flags & (MEM_FLAG_READ | MEM_FLAG_WRITE)) {
            case 0:
                scamp_log("scamp_mem_update_state(): %08X-%08X: %02X (RE | WE)\n", addr, addr + size - 1, new_flags);
                mem_set_mem_state(addr, size, read_ext | write_ext);
                break;
            case 1:
                scamp_log("scamp_mem_update_state(): %08X-%08X: %02X (RE | WI)\n", addr, addr + size - 1, new_flags);
                mem_set_mem_state(addr, size, read_ext | MEM_WRITE_INTERNAL);
                break;
            case 2:
                scamp_log("scamp_mem_update_state(): %08X-%08X: %02X (RI | WE)\n", addr, addr + size - 1, new_flags);
                mem_set_mem_state(addr, size, MEM_READ_INTERNAL | write_ext);
                break;
            case 3:
                scamp_log("scamp_mem_update_state(): %08X-%08X: %02X (RI | WI)\n", addr, addr + size - 1, new_flags);
                mem_set_mem_state(addr, size, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                break;
            default:
                break;
        }
    }

    flushmmucache_nopc();
}

static int
is_seg_in_ram(scamp_t *dev, uint8_t s)
{
    mem_page_t *mp  = (mem_page_t *) dev->mem_mappings[s].priv;
    const int   ret = mp->in_ram;

    return ret;
}

static void
recalc_ems(scamp_t *dev)
{
    const uint8_t  seg_xlat[12] = { 40, 41, 42, 43, 52, 53, 54, 55, 44, 45, 46, 47 };
    const uint16_t seg_enable   = dev->cfg_regs[CFG_EMSEN2] | ((dev->cfg_regs[CFG_EMSEN1] & 0xf) << 8);

    for (uint8_t s = 40; s < 60; s++) {
        dev->mem_pages[s].phys_addr = dev->mem_pages[s].virt_addr;
        dev->mem_pages[s].mem       = ram + dev->mem_pages[s].phys_addr;

       if (is_seg_in_ram(dev, s))
           mem_mapping_set_exec(&(dev->mem_mappings[s]), dev->mem_pages[s].mem);

       scamp_mem_update_state(dev, s * EMS_PGSIZE, EMS_PGSIZE, 0x00, MEM_FMASK_EMS);
    }

    for (uint8_t i = 0; i < 36; i++) {
        uint8_t  s         = (i < 12) ? (i + 48) : (i + 4);
        uint8_t  on        = (dev->cfg_regs[CFG_EMSEN1] & EMSEN1_EMSENAB);
        uint32_t phys_addr = dev->ems[i] << 14;

        if (i < 12) {
            if (dev->cfg_regs[CFG_EMSEN1] & EMSEN1_EMSMAP)
                s = seg_xlat[i];

            on = on && (seg_enable & (1 << i));
        } else
            on = on && (dev->cfg_regs[CFG_EMSEN1] & EMSEN1_BFENAB);

        if (on) {
            dev->mem_pages[s].phys_addr = phys_addr;
            dev->mem_pages[s].mem       = ram + dev->mem_pages[s].phys_addr;

            if (is_seg_in_ram(dev, s))
                mem_mapping_set_exec(&(dev->mem_mappings[s]), dev->mem_pages[s].mem);

            scamp_mem_update_state(dev, s * EMS_PGSIZE, EMS_PGSIZE, MEM_FLAG_EMS, MEM_FMASK_EMS);
        } else if (i >= 12) {
            dev->mem_pages[s].phys_addr = dev->mem_pages[s].virt_addr;
            dev->mem_pages[s].mem       = ram + dev->mem_pages[s].phys_addr;

            if (is_seg_in_ram(dev, s))
                mem_mapping_set_exec(&(dev->mem_mappings[s]), dev->mem_pages[s].mem);

            scamp_mem_update_state(dev, s * EMS_PGSIZE, EMS_PGSIZE, 0x00, MEM_FMASK_EMS);
        }
    }

    flushmmucache_nopc();
}

static void
shadow_control(scamp_t *dev, uint32_t addr, uint32_t size, int state)
{
    if (size == 0x8000) {
        scamp_mem_update_state(dev, addr, EMS_PGSIZE, state, MEM_FMASK_RW);
        scamp_mem_update_state(dev, addr + EMS_PGSIZE, EMS_PGSIZE, state, MEM_FMASK_RW);
    } else
        scamp_mem_update_state(dev, addr, size, state, MEM_FMASK_RW);

    flushmmucache_nopc();
}

static void
shadow_recalc(scamp_t *dev)
{
    uint8_t  abaxs = (dev->cfg_regs[CFG_RAMMAP] & RAMMAP_REMP384) ? 0 : dev->cfg_regs[CFG_ABAXS];
    uint8_t  caxs  = (dev->cfg_regs[CFG_RAMMAP] & RAMMAP_REMP384) ? 0 : dev->cfg_regs[CFG_CAXS];
    uint8_t  daxs  = (dev->cfg_regs[CFG_RAMMAP] & RAMMAP_REMP384) ? 0 : dev->cfg_regs[CFG_DAXS];
    uint8_t  feaxs = (dev->cfg_regs[CFG_RAMMAP] & RAMMAP_REMP384) ? 0 : dev->cfg_regs[CFG_FEAXS];

    /*Enabling remapping will disable all shadowing*/
    if (dev->cfg_regs[CFG_RAMMAP] & RAMMAP_REMP384)
        mem_remap_top_nomid(384);
    else
        mem_remap_top_nomid(0);

    for (uint8_t i = 40; i < 64; i++) {
        if (dev->cfg_regs[CFG_RAMMAP] & RAMMAP_REMP384)
            scamp_mem_update_state(dev, (i * EMS_PGSIZE), EMS_PGSIZE, MEM_FLAG_REMAP, MEM_FMASK_REMAP);
        else
            scamp_mem_update_state(dev, (i * EMS_PGSIZE), EMS_PGSIZE, 0x00, MEM_FMASK_REMAP);
    }

    shadow_control(dev, 0xa0000, 0x4000, abaxs & 3);
    shadow_control(dev, 0xa0000, 0x4000, abaxs & 3);
    shadow_control(dev, 0xa8000, 0x4000, (abaxs >> 2) & 3);
    shadow_control(dev, 0xa8000, 0x4000, (abaxs >> 2) & 3);

    shadow_control(dev, 0xb0000, 0x4000, (abaxs >> 4) & 3);
    shadow_control(dev, 0xb0000, 0x4000, (abaxs >> 4) & 3);
    shadow_control(dev, 0xb8000, 0x4000, (abaxs >> 6) & 3);
    shadow_control(dev, 0xb8000, 0x4000, (abaxs >> 6) & 3);

    shadow_control(dev, 0xc0000, 0x4000, caxs & 3);
    shadow_control(dev, 0xc4000, 0x4000, (caxs >> 2) & 3);
    shadow_control(dev, 0xc8000, 0x4000, (caxs >> 4) & 3);
    shadow_control(dev, 0xcc000, 0x4000, (caxs >> 6) & 3);

    shadow_control(dev, 0xd0000, 0x4000, daxs & 3);
    shadow_control(dev, 0xd4000, 0x4000, (daxs >> 2) & 3);
    shadow_control(dev, 0xd8000, 0x4000, (daxs >> 4) & 3);
    shadow_control(dev, 0xdc000, 0x4000, (daxs >> 6) & 3);

    shadow_control(dev, 0xe0000, 0x4000, feaxs & 3);
    shadow_control(dev, 0xe4000, 0x4000, feaxs & 3);
    shadow_control(dev, 0xe8000, 0x4000, (feaxs >> 2) & 3);
    shadow_control(dev, 0xec000, 0x4000, (feaxs >> 2) & 3);

    shadow_control(dev, 0xf0000, 0x8000, (feaxs >> 4) & 3);
    shadow_control(dev, 0xf8000, 0x8000, (feaxs >> 6) & 3);
}

static void
recalc_sltptr(scamp_t *dev)
{
    uint32_t sltptr = dev->cfg_regs[CFG_SLTPTR] << 16;

    if (sltptr >= 0xa0000 && sltptr < 0x100000)
        sltptr = 0x100000;
    if (sltptr > 0xfe0000)
        sltptr = 0xfe0000;

    if (sltptr >= 0xa0000) {
        for (uint8_t i = 0; i < 40; i++)
            scamp_mem_update_state(dev, i * EMS_PGSIZE, EMS_PGSIZE, 0x00, MEM_FMASK_SLOTBUS);

        mem_set_mem_state(0x100000, sltptr - 0x100000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        mem_set_mem_state(sltptr, 0x1000000 - sltptr, MEM_READ_EXTANY | MEM_WRITE_EXTANY);

        if ((sltptr >= 0x40000) && (sltptr <= 0x90000)) {
            dev->cfg_regs[CFG_EMSEN1] &= ~EMSEN1_BFENAB;
            recalc_ems(dev);
        }
    } else {
        for (uint8_t i = 0; i < (sltptr / EMS_PGSIZE); i++)
            scamp_mem_update_state(dev, i * EMS_PGSIZE, EMS_PGSIZE, 0x00, MEM_FMASK_SLOTBUS);

        for (uint8_t i = (sltptr / EMS_PGSIZE); i < 40; i++)
            scamp_mem_update_state(dev, i * EMS_PGSIZE, EMS_PGSIZE, MEM_FLAG_SLOTBUS, MEM_FMASK_SLOTBUS);

        mem_set_mem_state(sltptr, 0xa0000 - sltptr, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
        mem_set_mem_state(0x100000, 0xf00000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
    }

    flushmmucache_nopc();
}

static void
recalc_rommov(scamp_t *dev)
{
    switch ((dev->cfg_regs[CFG_RAMMAP] & RAMMAP_ROMMOV) >> 5) {
        case 0x00:
            scamp_mem_update_state(dev, 0x000c0000, EMS_PGSIZE,           0x00, MEM_FMASK_ROMCS);
            scamp_mem_update_state(dev, 0x000c4000, EMS_PGSIZE,           0x00, MEM_FMASK_ROMCS);
            scamp_mem_update_state(dev, 0x000c8000, EMS_PGSIZE,           0x00, MEM_FMASK_ROMCS);
            scamp_mem_update_state(dev, 0x000cc000, EMS_PGSIZE,           0x00, MEM_FMASK_ROMCS);

            scamp_mem_update_state(dev, 0x000e0000, EMS_PGSIZE, MEM_FLAG_ROMCS, MEM_FMASK_ROMCS);
            scamp_mem_update_state(dev, 0x000e4000, EMS_PGSIZE, MEM_FLAG_ROMCS, MEM_FMASK_ROMCS);
            scamp_mem_update_state(dev, 0x000e8000, EMS_PGSIZE, MEM_FLAG_ROMCS, MEM_FMASK_ROMCS);
            scamp_mem_update_state(dev, 0x000ec000, EMS_PGSIZE, MEM_FLAG_ROMCS, MEM_FMASK_ROMCS);
            break;

        case 0x01:
            scamp_mem_update_state(dev, 0x000c0000, EMS_PGSIZE,           0x00, MEM_FMASK_ROMCS);
            scamp_mem_update_state(dev, 0x000c4000, EMS_PGSIZE,           0x00, MEM_FMASK_ROMCS);
            scamp_mem_update_state(dev, 0x000c8000, EMS_PGSIZE,           0x00, MEM_FMASK_ROMCS);
            scamp_mem_update_state(dev, 0x000cc000, EMS_PGSIZE,           0x00, MEM_FMASK_ROMCS);

            scamp_mem_update_state(dev, 0x000e0000, EMS_PGSIZE,           0x00, MEM_FMASK_ROMCS);
            scamp_mem_update_state(dev, 0x000e4000, EMS_PGSIZE,           0x00, MEM_FMASK_ROMCS);
            scamp_mem_update_state(dev, 0x000e8000, EMS_PGSIZE,           0x00, MEM_FMASK_ROMCS);
            scamp_mem_update_state(dev, 0x000ec000, EMS_PGSIZE,           0x00, MEM_FMASK_ROMCS);
            break;

        case 0x02:
            scamp_mem_update_state(dev, 0x000c0000, EMS_PGSIZE, MEM_FLAG_ROMCS, MEM_FMASK_ROMCS);
            scamp_mem_update_state(dev, 0x000c4000, EMS_PGSIZE, MEM_FLAG_ROMCS, MEM_FMASK_ROMCS);
            scamp_mem_update_state(dev, 0x000c8000, EMS_PGSIZE,           0x00, MEM_FMASK_ROMCS);
            scamp_mem_update_state(dev, 0x000cc000, EMS_PGSIZE,           0x00, MEM_FMASK_ROMCS);

            scamp_mem_update_state(dev, 0x000e0000, EMS_PGSIZE,           0x00, MEM_FMASK_ROMCS);
            scamp_mem_update_state(dev, 0x000e4000, EMS_PGSIZE,           0x00, MEM_FMASK_ROMCS);
            scamp_mem_update_state(dev, 0x000e8000, EMS_PGSIZE, MEM_FLAG_ROMCS, MEM_FMASK_ROMCS);
            scamp_mem_update_state(dev, 0x000ec000, EMS_PGSIZE, MEM_FLAG_ROMCS, MEM_FMASK_ROMCS);
            break;

        case 0x03:
            scamp_mem_update_state(dev, 0x000c0000, EMS_PGSIZE, MEM_FLAG_ROMCS, MEM_FMASK_ROMCS);
            scamp_mem_update_state(dev, 0x000c4000, EMS_PGSIZE, MEM_FLAG_ROMCS, MEM_FMASK_ROMCS);
            scamp_mem_update_state(dev, 0x000c8000, EMS_PGSIZE, MEM_FLAG_ROMCS, MEM_FMASK_ROMCS);
            scamp_mem_update_state(dev, 0x000cc000, EMS_PGSIZE, MEM_FLAG_ROMCS, MEM_FMASK_ROMCS);

            scamp_mem_update_state(dev, 0x000e0000, EMS_PGSIZE,           0x00, MEM_FMASK_ROMCS);
            scamp_mem_update_state(dev, 0x000e4000, EMS_PGSIZE,           0x00, MEM_FMASK_ROMCS);
            scamp_mem_update_state(dev, 0x000e8000, EMS_PGSIZE,           0x00, MEM_FMASK_ROMCS);
            scamp_mem_update_state(dev, 0x000ec000, EMS_PGSIZE,           0x00, MEM_FMASK_ROMCS);
            break;
    }

    flushmmucache_nopc();
}

static void
scamp_write(uint16_t addr, uint8_t val, void *priv)
{
    scamp_t *dev = (scamp_t *) priv;

    switch (addr) {
        case 0xe8:
            dev->ems_index   = val & 0x3f;
            dev->ems_autoinc = val & 0x40;
            break;

        case 0xea:
            if (dev->ems_index < 0x24) {
                dev->ems[dev->ems_index] = (dev->ems[dev->ems_index] & 0x300) | val;
                recalc_ems(dev);
            }
            break;
        case 0xeb:
            if (dev->ems_index < 0x24) {
                dev->ems[dev->ems_index] = (dev->ems[dev->ems_index] & 0x0ff) | ((val & 3) << 8);
                recalc_ems(dev);
            }
            if (dev->ems_autoinc)
                dev->ems_index = (dev->ems_index + 1) & 0x3f;
            break;

        case 0xec:
            if (dev->cfg_enable)
                dev->cfg_index = val;
            break;

        case 0xed:
            if (dev->cfg_enable && (dev->cfg_index >= 0x02) && (dev->cfg_index <= 0x16)) {
                dev->cfg_regs[dev->cfg_index] = val;
                switch (dev->cfg_index) {
                    case CFG_SLTPTR:
                        recalc_sltptr(dev);
                        recalc_ems(dev);
                        break;

                    case CFG_RAMMAP:
                        recalc_mappings(dev);
                        mem_mapping_disable(&ram_remapped_mapping);
                        shadow_recalc(dev);
                        recalc_rommov(dev);
                        break;

                    case CFG_EMSEN1:
                    case CFG_EMSEN2:
                        shadow_recalc(dev);
                        recalc_ems(dev);
                        break;

                    case CFG_ABAXS:
                    case CFG_CAXS:
                    case CFG_DAXS:
                    case CFG_FEAXS:
                        shadow_recalc(dev);
                        break;
                    default:
                        break;
                }
            }
            break;

        case 0xee:
            if (dev->cfg_enable && mem_a20_alt) {
                dev->port_92->reg &= 0xfd;
                mem_a20_alt = 0;
                mem_a20_recalc();
            }
            break;

        default:
            break;
    }
}

static uint8_t
scamp_read(uint16_t addr, void *priv)
{
    scamp_t *dev = (scamp_t *) priv;
    uint8_t  ret = 0xff;

    switch (addr) {
        case 0xe8:
            ret = dev->ems_index | dev->ems_autoinc;
            break;

        case 0xea:
            if (dev->ems_index < 0x24)
                ret = dev->ems[dev->ems_index] & 0xff;
            break;
        case 0xeb:
            if (dev->ems_index < 0x24)
                ret = (dev->ems[dev->ems_index] >> 8) | 0xfc;
            if (dev->ems_autoinc)
                dev->ems_index = (dev->ems_index + 1) & 0x3f;
            break;

        case 0xed:
            if (dev->cfg_enable && (dev->cfg_index >= 0x00) && (dev->cfg_index <= 0x16))
                ret = (dev->cfg_regs[dev->cfg_index]);
            break;

        case 0xee:
            if (!mem_a20_alt) {
                dev->port_92->reg |= 0x02;
                mem_a20_alt = 1;
                mem_a20_recalc();
            }
            break;

        case 0xef:
            softresetx86();
            cpu_set_edx();
            break;

        default:
            break;
    }

    return ret;
}

static void
scamp_close(void *priv)
{
    scamp_t *dev = (scamp_t *) priv;

    free(dev);
}

static void *
scamp_init(UNUSED(const device_t *info))
{
    uint32_t addr;
    scamp_t *dev = (scamp_t *) calloc(1, sizeof(scamp_t));

    dev->cfg_regs[CFG_ID] = ID_VL82C311;
    dev->cfg_enable       = 1;

    io_sethandler(0x00e8, 0x0001,
                  scamp_read, NULL, NULL, scamp_write, NULL, NULL, dev);
    io_sethandler(0x00ea, 0x0006,
                  scamp_read, NULL, NULL, scamp_write, NULL, NULL, dev);
    io_sethandler(0x00f4, 0x0002,
                  scamp_read, NULL, NULL, scamp_write, NULL, NULL, dev);
    io_sethandler(0x00f9, 0x0001,
                  scamp_read, NULL, NULL, scamp_write, NULL, NULL, dev);
    io_sethandler(0x00fb, 0x0001,
                  scamp_read, NULL, NULL, scamp_write, NULL, NULL, dev);

    dev->ram_config = 0;

    /* Find best fit configuration for the requested memory size */
    for (uint8_t c = 0; c < NR_ELEMS(ram_configs); c++) {
        if (mem_size < ram_configs[c].size_kb)
            break;

        dev->ram_config = c;
    }

    mem_mapping_set_p(&ram_low_mapping, (void *) &dev->ram_struct[0]);
    mem_mapping_set_handler(&ram_low_mapping,
                            ram_mirrored_read, NULL, NULL,
                            ram_mirrored_write, NULL, NULL);
    mem_mapping_disable(&ram_mid_mapping);
    mem_mapping_disable(&ram_high_mapping);

    addr = 0;
    for (uint8_t c = 0; c < 2; c++) {
        dev->ram_struct[c].parent = dev;
        dev->ram_struct[c].bank   = c;
        mem_mapping_add(&dev->ram_mapping[c], 0, 0,
                        ram_mirrored_read, NULL, NULL,
                        ram_mirrored_write, NULL, NULL,
                        &ram[addr], MEM_MAPPING_INTERNAL, (void *) &dev->ram_struct[c]);
        mem_mapping_disable(&dev->ram_mapping[c]);

        dev->ram_phys_base[c] = addr;

        switch (ram_configs[dev->ram_config].bank[c]) {
            case BANK_NONE:
                dev->ram_mask[c]        = 0;
                dev->ram_interleaved[c] = 0;
                break;

            case BANK_256K:
                addr += (1 << 19);
                dev->ram_mask[c]        = 0x1ff;
                dev->row_phys_shift[c]  = 10;
                dev->ram_interleaved[c] = 0;
                break;

            case BANK_256K_INTERLEAVED:
                addr += (1 << 20);
                dev->ram_mask[c]        = 0x1ff;
                dev->row_phys_shift[c]  = 10;
                dev->ibank_shift[c]     = 19;
                dev->ram_interleaved[c] = 1;
                break;

            case BANK_1M:
                addr += (1 << 21);
                dev->ram_mask[c]        = 0x3ff;
                dev->row_phys_shift[c]  = 11;
                dev->ram_interleaved[c] = 0;
                break;

            case BANK_1M_INTERLEAVED:
                addr += (1 << 22);
                dev->ram_mask[c]        = 0x3ff;
                dev->row_phys_shift[c]  = 11;
                dev->ibank_shift[c]     = 21;
                dev->ram_interleaved[c] = 1;
                break;

            case BANK_4M:
                addr += (1 << 23);
                dev->ram_mask[c]        = 0x7ff;
                dev->row_phys_shift[c]  = 12;
                dev->ram_interleaved[c] = 0;
                break;

            case BANK_4M_INTERLEAVED:
                addr += (1 << 24);
                dev->ram_mask[c]        = 0x7ff;
                dev->row_phys_shift[c]  = 12;
                dev->ibank_shift[c]     = 23;
                dev->ram_interleaved[c] = 1;
                break;

            default:
                break;
        }
    }

    mem_set_mem_state(0xfe0000, 0x20000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);

    for (uint8_t i = 0; i < 12; i++)
        dev->ems[i]      = (0x000c0000 + (i * EMS_PGSIZE)) >> 14;

    for (uint8_t i = 0; i < 24; i++)
        dev->ems[i + 12] = (0x00040000 + (i * EMS_PGSIZE)) >> 14;

    for (uint8_t i = 0; i < 64; i++) {
        dev->mem_pages[i].in_ram    = 1;
        dev->mem_pages[i].virt_addr = i * EMS_PGSIZE;
        dev->mem_pages[i].phys_addr = dev->mem_pages[i].virt_addr;
        dev->mem_pages[i].mem       = ram + dev->mem_pages[i].phys_addr;

        mem_mapping_add(&(dev->mem_mappings[i]),
                        i * EMS_PGSIZE, EMS_PGSIZE,
                        scamp_mem_readb, scamp_mem_readw, NULL,
                        scamp_mem_writeb, scamp_mem_writew, NULL,
                        dev->mem_pages[i].mem, MEM_MAPPING_INTERNAL,
                        &(dev->mem_pages[i]));

        if (i < 40) {
            mem_mapping_disable(&(dev->mem_mappings[i]));

            scamp_mem_update_state(dev, i * EMS_PGSIZE, EMS_PGSIZE, MEM_FLAG_READ | MEM_FLAG_WRITE, MEM_FMASK_RW);
        } else {
            /* This is needed to the state update actually occurs. */
            dev->mem_flags[i] = MEM_FLAG_READ | MEM_FLAG_WRITE;
            scamp_mem_update_state(dev, i * EMS_PGSIZE, EMS_PGSIZE, 0x00, MEM_FMASK_RW);

            if (i >= 60)
                scamp_mem_update_state(dev, i * EMS_PGSIZE, EMS_PGSIZE, MEM_FLAG_ROMCS, MEM_FMASK_ROMCS);
        }
    }

    dev->card_mem = NULL;

    for (uint8_t i = 0; i < 4; i++) {
        dev->card_pages[i].virt_addr = i * EMS_PGSIZE;
        dev->card_pages[i].phys_addr = dev->card_pages[i].virt_addr;
        dev->card_pages[i].mem       = dev->card_mem + dev->card_pages[i].phys_addr;
    }

    dev->port_92 = device_add(&port_92_device);

    return dev;
}

const device_t vlsi_scamp_device = {
    .name          = "VLSI SCAMP",
    .internal_name = "vlsi_scamp",
    .flags         = 0,
    .local         = 0,
    .init          = scamp_init,
    .close         = scamp_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
