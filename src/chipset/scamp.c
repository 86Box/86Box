/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of VLSI 82C311 ("SCAMP") chipset.
 *
 * Note:	The datasheet mentions that the chipset supports up to 8MB
 *		of DRAM. This is intepreted as 'being able to refresh up to
 *		8MB of DRAM chips', because it works fine with bus-based
 *		memory expansion.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *
 *		Copyright 2020 Sarah Walker.
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

#define RAMMAP_REMP386 (1 << 4)

#define EMSEN1_EMSMAP  (1 << 4)
#define EMSEN1_EMSENAB (1 << 7)

#define NR_ELEMS(x)    (sizeof(x) / sizeof(x[0]))

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

typedef struct {
    void *parent;
    int   bank;
} ram_struct_t;

typedef struct {
    void *parent;
    int   segment;
} ems_struct_t;

typedef struct {
    int     cfg_index;
    uint8_t cfg_regs[256];
    int     cfg_enable, ram_config;

    int           ems_index;
    int           ems_autoinc;
    uint16_t      ems[0x24];
    mem_mapping_t ems_mappings[20]; /*a0000-effff*/
    uint32_t      mappings[20];

    mem_mapping_t ram_mapping[2];
    ram_struct_t  ram_struct[2];
    ems_struct_t  ems_struct[20];

    uint32_t ram_virt_base[2], ram_phys_base[2];
    uint32_t ram_mask[2];
    int      row_virt_shift[2], row_phys_shift[2];
    int      ram_interleaved[2], ibank_shift[2];

    port_92_t *port_92;
} scamp_t;

static const struct
{
    int size_kb;
    int rammap;
    int bank[2];
} ram_configs[] = {
    {512,    0x0, { BANK_256K, BANK_NONE }                        },
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

static const struct
{
    int bank[2];
    int remapped;
} rammap[16] = {
    {{ BANK_256K, BANK_NONE },                          0},
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

/* The column bits masked when using 256kbit DRAMs in 4Mbit mode aren't contiguous,
   so we use separate routines for that special case */
static uint8_t
ram_mirrored_256k_in_4mi_read(uint32_t addr, void *priv)
{
    ram_struct_t *rs   = (ram_struct_t *) priv;
    scamp_t      *dev  = rs->parent;
    int           bank = rs->bank, byte;
    int           row, column;

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
    ram_struct_t *rs   = (ram_struct_t *) priv;
    scamp_t      *dev  = rs->parent;
    int           bank = rs->bank, byte;
    int           row, column;

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
    ram_struct_t *rs   = (ram_struct_t *) priv;
    scamp_t      *dev  = rs->parent;
    int           bank = rs->bank, byte;
    int           row, column;

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
    ram_struct_t *rs   = (ram_struct_t *) priv;
    scamp_t      *dev  = rs->parent;
    int           bank = rs->bank, byte;
    int           row, column;

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
    ram_struct_t *rs   = (ram_struct_t *) priv;
    scamp_t      *dev  = rs->parent;
    int           bank = rs->bank, byte;
    int           row, column;

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
    ram_struct_t *rs   = (ram_struct_t *) priv;
    scamp_t      *dev  = rs->parent;
    int           bank = rs->bank, byte;
    int           row, column;

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
    int      c;
    uint32_t virt_base  = 0, old_virt_base;
    uint8_t  cur_rammap = dev->cfg_regs[CFG_RAMMAP] & 0xf;
    int      bank_nr    = 0, phys_bank;

    mem_set_mem_state_both((1 << 20), (16256 - 1024) * 1024, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
    mem_set_mem_state(0xfe0000, 0x20000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);

    for (c = 0; c < 2; c++)
        mem_mapping_disable(&dev->ram_mapping[c]);

    /* Once the BIOS programs the correct DRAM configuration, switch to regular
       linear memory mapping */
    if (cur_rammap == ram_configs[dev->ram_config].rammap) {
        mem_mapping_set_handler(&ram_low_mapping,
                                mem_read_ram, mem_read_ramw, mem_read_raml,
                                mem_write_ram, mem_write_ramw, mem_write_raml);
        mem_mapping_set_addr(&ram_low_mapping, 0, 0xa0000);
        if (mem_size > 1024)
            mem_set_mem_state_both((1 << 20), (mem_size - 1024) << 10, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        mem_mapping_enable(&ram_high_mapping);
        return;
    } else {
        mem_mapping_set_handler(&ram_low_mapping,
                                ram_mirrored_read, NULL, NULL,
                                ram_mirrored_write, NULL, NULL);
        mem_mapping_disable(&ram_low_mapping);
    }

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
        }
    }
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
        mem_set_mem_state(0, 0xa0000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        mem_set_mem_state(0x100000, sltptr - 0x100000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        mem_set_mem_state(sltptr, 0x1000000 - sltptr, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
    } else {
        mem_set_mem_state(0, sltptr, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        mem_set_mem_state(sltptr, 0xa0000 - sltptr, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
        mem_set_mem_state(0x100000, 0xf00000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
    }
}

static uint8_t
scamp_ems_read(uint32_t addr, void *priv)
{
    ems_struct_t *ems     = (ems_struct_t *) priv;
    scamp_t      *dev     = ems->parent;
    int           segment = ems->segment;

    addr = (addr & 0x3fff) | dev->mappings[segment];
    return ram[addr];
}

static void
scamp_ems_write(uint32_t addr, uint8_t val, void *priv)
{
    ems_struct_t *ems     = (ems_struct_t *) priv;
    scamp_t      *dev     = ems->parent;
    int           segment = ems->segment;

    addr      = (addr & 0x3fff) | dev->mappings[segment];
    ram[addr] = val;
}

static void
recalc_ems(scamp_t *dev)
{
    int            segment;
    const uint32_t ems_base[12] = {
        0xc0000, 0xc4000, 0xc8000, 0xcc000,
        0xd0000, 0xd4000, 0xd8000, 0xdc000,
        0xe0000, 0xe4000, 0xe8000, 0xec000
    };
    uint32_t new_mappings[20];
    uint16_t ems_enable;

    for (segment = 0; segment < 20; segment++)
        new_mappings[segment] = 0xa0000 + segment * 0x4000;

    if (dev->cfg_regs[CFG_EMSEN1] & EMSEN1_EMSENAB)
        ems_enable = dev->cfg_regs[CFG_EMSEN2] | ((dev->cfg_regs[CFG_EMSEN1] & 0xf) << 8);
    else
        ems_enable = 0;

    for (segment = 0; segment < 12; segment++) {
        if (ems_enable & (1 << segment)) {
            uint32_t phys_addr = dev->ems[segment] << 14;

            /*If physical address is in remapped memory then adjust down to a0000-fffff range*/
            if ((dev->cfg_regs[CFG_RAMMAP] & RAMMAP_REMP386) && phys_addr >= (mem_size * 1024)
                && phys_addr < ((mem_size + 384) * 1024))
                phys_addr = (phys_addr - mem_size * 1024) + 0xa0000;
            new_mappings[(ems_base[segment] - 0xa0000) >> 14] = phys_addr;
        }
    }

    for (segment = 0; segment < 20; segment++) {
        if (new_mappings[segment] != dev->mappings[segment]) {
            dev->mappings[segment] = new_mappings[segment];
            if (new_mappings[segment] < (mem_size * 1024)) {
                mem_mapping_set_exec(&dev->ems_mappings[segment], ram + dev->mappings[segment]);
                mem_mapping_enable(&dev->ems_mappings[segment]);
            } else
                mem_mapping_disable(&dev->ems_mappings[segment]);
        }
    }
}

static void
shadow_control(uint32_t addr, uint32_t size, int state, int ems_enable)
{
    if (ems_enable)
        mem_set_mem_state(addr, size, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
    else
        switch (state) {
            case 0:
                mem_set_mem_state(addr, size, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
                break;
            case 1:
                mem_set_mem_state(addr, size, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
                break;
            case 2:
                mem_set_mem_state(addr, size, MEM_READ_INTERNAL | MEM_WRITE_EXTANY);
                break;
            case 3:
                mem_set_mem_state(addr, size, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                break;
        }

    flushmmucache_nopc();
}

static void
shadow_recalc(scamp_t *dev)
{
    uint8_t  abaxs = (dev->cfg_regs[CFG_RAMMAP] & RAMMAP_REMP386) ? 0 : dev->cfg_regs[CFG_ABAXS];
    uint8_t  caxs  = (dev->cfg_regs[CFG_RAMMAP] & RAMMAP_REMP386) ? 0 : dev->cfg_regs[CFG_CAXS];
    uint8_t  daxs  = (dev->cfg_regs[CFG_RAMMAP] & RAMMAP_REMP386) ? 0 : dev->cfg_regs[CFG_DAXS];
    uint8_t  feaxs = (dev->cfg_regs[CFG_RAMMAP] & RAMMAP_REMP386) ? 0 : dev->cfg_regs[CFG_FEAXS];
    uint32_t ems_enable;

    if (dev->cfg_regs[CFG_EMSEN1] & EMSEN1_EMSENAB) {
        if (dev->cfg_regs[CFG_EMSEN1] & EMSEN1_EMSMAP) /*Axxx/Bxxx/Dxxx*/
            ems_enable = (dev->cfg_regs[CFG_EMSEN2] & 0xf) | ((dev->cfg_regs[CFG_EMSEN1] & 0xf) << 4) | ((dev->cfg_regs[CFG_EMSEN2] & 0xf0) << 8);
        else /*Cxxx/Dxxx/Exxx*/
            ems_enable = (dev->cfg_regs[CFG_EMSEN2] << 8) | ((dev->cfg_regs[CFG_EMSEN1] & 0xf) << 16);
    } else
        ems_enable = 0;

    /*Enabling remapping will disable all shadowing*/
    if (dev->cfg_regs[CFG_RAMMAP] & RAMMAP_REMP386)
        mem_remap_top(384);

    shadow_control(0xa0000, 0x4000, abaxs & 3, ems_enable & 0x00001);
    shadow_control(0xa0000, 0x4000, abaxs & 3, ems_enable & 0x00002);
    shadow_control(0xa8000, 0x4000, (abaxs >> 2) & 3, ems_enable & 0x00004);
    shadow_control(0xa8000, 0x4000, (abaxs >> 2) & 3, ems_enable & 0x00008);

    shadow_control(0xb0000, 0x4000, (abaxs >> 4) & 3, ems_enable & 0x00010);
    shadow_control(0xb0000, 0x4000, (abaxs >> 4) & 3, ems_enable & 0x00020);
    shadow_control(0xb8000, 0x4000, (abaxs >> 6) & 3, ems_enable & 0x00040);
    shadow_control(0xb8000, 0x4000, (abaxs >> 6) & 3, ems_enable & 0x00080);

    shadow_control(0xc0000, 0x4000, caxs & 3, ems_enable & 0x00100);
    shadow_control(0xc4000, 0x4000, (caxs >> 2) & 3, ems_enable & 0x00200);
    shadow_control(0xc8000, 0x4000, (caxs >> 4) & 3, ems_enable & 0x00400);
    shadow_control(0xcc000, 0x4000, (caxs >> 6) & 3, ems_enable & 0x00800);

    shadow_control(0xd0000, 0x4000, daxs & 3, ems_enable & 0x01000);
    shadow_control(0xd4000, 0x4000, (daxs >> 2) & 3, ems_enable & 0x02000);
    shadow_control(0xd8000, 0x4000, (daxs >> 4) & 3, ems_enable & 0x04000);
    shadow_control(0xdc000, 0x4000, (daxs >> 6) & 3, ems_enable & 0x08000);

    shadow_control(0xe0000, 0x4000, feaxs & 3, ems_enable & 0x10000);
    shadow_control(0xe4000, 0x4000, feaxs & 3, ems_enable & 0x20000);
    shadow_control(0xe8000, 0x4000, (feaxs >> 2) & 3, ems_enable & 0x40000);
    shadow_control(0xec000, 0x4000, (feaxs >> 2) & 3, ems_enable & 0x80000);

    shadow_control(0xf0000, 0x8000, (feaxs >> 4) & 3, 0);
    shadow_control(0xf8000, 0x8000, (feaxs >> 6) & 3, 0);
}

static void
scamp_write(uint16_t addr, uint8_t val, void *priv)
{
    scamp_t *dev = (scamp_t *) priv;

    switch (addr) {
        case 0xe8:
            dev->ems_index   = val & 0x1f;
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
                        break;

                    case CFG_RAMMAP:
                        recalc_mappings(dev);
                        mem_mapping_disable(&ram_remapped_mapping);
                        shadow_recalc(dev);
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
scamp_init(const device_t *info)
{
    uint32_t addr;
    int      c;
    scamp_t *dev = (scamp_t *) malloc(sizeof(scamp_t));
    memset(dev, 0x00, sizeof(scamp_t));

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
    for (c = 0; c < NR_ELEMS(ram_configs); c++) {
        if (mem_size < ram_configs[c].size_kb)
            break;

        dev->ram_config = c;
    }

    mem_mapping_set_p(&ram_low_mapping, (void *) &dev->ram_struct[0]);
    mem_mapping_set_handler(&ram_low_mapping,
                            ram_mirrored_read, NULL, NULL,
                            ram_mirrored_write, NULL, NULL);
    mem_mapping_disable(&ram_high_mapping);
    mem_mapping_set_addr(&ram_mid_mapping, 0xf0000, 0x10000);
    mem_mapping_set_exec(&ram_mid_mapping, ram + 0xf0000);

    addr = 0;
    for (c = 0; c < 2; c++) {
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
        }
    }

    mem_set_mem_state(0xfe0000, 0x20000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);

    for (c = 0; c < 20; c++) {
        dev->ems_struct[c].parent  = dev;
        dev->ems_struct[c].segment = c;
        mem_mapping_add(&dev->ems_mappings[c],
                        0xa0000 + c * 0x4000, 0x4000,
                        scamp_ems_read, NULL, NULL,
                        scamp_ems_write, NULL, NULL,
                        ram + 0xa0000 + c * 0x4000, MEM_MAPPING_INTERNAL, (void *) &dev->ems_struct[c]);
        dev->mappings[c] = 0xa0000 + c * 0x4000;
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
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
