/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Intel 1 Mbit and 2 Mbit, 8-bit and
 *          16-bit flash devices.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Nathan Lewis, <git@nrlewis.dev>
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2016-2020 Miran Grca.
 *          Copyright 2026      Nathan Lewis.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/machine.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/plat.h>
#include <86box/flash.h>

#define FLAG_X00     16
#define FLAG_MICRON   8
#define FLAG_WORD     4
#define FLAG_BXB      2
#define FLAG_INV_A16  1

enum {
    BLOCK_MAIN1,
    BLOCK_MAIN2,
    BLOCK_MAIN3,
    BLOCK_MAIN4,
    BLOCK_DATA1,
    BLOCK_DATA2,
    BLOCK_BOOT,
    BLOCKS_NUM
};

enum {
    CMD_READ_ARRAY        = 0xff,
    CMD_IID               = 0x90,
    CMD_READ_STATUS       = 0x70,
    CMD_CLEAR_STATUS      = 0x50,
    CMD_ERASE_SETUP       = 0x20,
    CMD_ERASE_CONFIRM     = 0xd0,
    CMD_ERASE_SUSPEND     = 0xb0,
    CMD_PROGRAM_SETUP     = 0x40,
    CMD_PROGRAM_SETUP_ALT = 0x10
};

/* The chip. */
struct intel_flash_t {
    uint8_t  command;
    uint8_t  status;
    uint8_t  dirty;
    uint8_t  flags;
    uint8_t  write_protect;
    uint8_t *array;

    uint16_t flash_id;
    uint16_t pad16;

    uint32_t size;
    uint32_t mask;
    uint32_t bb_mask; /* Which address bits pick the boot block. */

    uint32_t program_addr;
    uint32_t block_start[BLOCKS_NUM];
    uint32_t block_end[BLOCKS_NUM];
    uint32_t block_len[BLOCKS_NUM];

    char nvr_path[1024];
};

/* The chip as the boot BIOS. */
typedef struct intel_flash_bios_t {
    intel_flash_t *chip;

    mem_mapping_t mapping[2];
    mem_mapping_t mapping_h[16];
    uint8_t      *exec[2];
    uint8_t      *exec_h[16];
} intel_flash_bios_t;

/* The AMI parts have A16 inverted, which the chip sees as an address in the other 64k. */
static uint32_t
intel_flash_addr(const intel_flash_t *dev, uint32_t addr)
{
    if (dev->flags & FLAG_INV_A16)
        addr ^= 0x10000;

    return addr & dev->mask;
}

static uint8_t
intel_flash_read(uint32_t addr, void *priv)
{
    const intel_flash_t *dev = (intel_flash_t *) priv;
    uint8_t              ret = 0xff;

    addr = intel_flash_addr(dev, addr);

    switch (dev->command) {
        default:
        case CMD_READ_ARRAY:
            ret = dev->array[addr];
            break;

        case CMD_IID:
            if (((addr & 1) && (!(dev->flags & FLAG_X00))) ||
                ((addr & 2) && (dev->flags & FLAG_X00)))
                ret = dev->flash_id & 0xff;
            else
                ret = 0x89;
            break;

        case CMD_READ_STATUS:
            ret = dev->status;
            break;
    }

    return ret;
}

static uint16_t
intel_flash_readw(uint32_t addr, void *priv)
{
    intel_flash_t  *dev = (intel_flash_t *) priv;
    const uint16_t *q;
    uint16_t        ret = 0xffff;

    addr = intel_flash_addr(dev, addr);

    if (dev->flags & FLAG_WORD)
        addr &= 0xfffffffe;

    q   = (uint16_t *) &(dev->array[addr]);
    ret = *q;

    if (dev->flags & FLAG_WORD)
        switch (dev->command) {
            default:
            case CMD_READ_ARRAY:
                break;

            case CMD_IID:
                if (addr & 2)
                    ret = dev->flash_id;
                else
                    ret = 0x0089;
                break;

            case CMD_READ_STATUS:
                ret = dev->status;
                break;
        }

    return ret;
}

static uint32_t
intel_flash_readl(uint32_t addr, void *priv)
{
    intel_flash_t  *dev = (intel_flash_t *) priv;
    const uint32_t *q;

    addr = intel_flash_addr(dev, addr);

    q = (uint32_t *) &(dev->array[addr]);

    return *q;
}

static void
intel_flash_write(uint32_t addr, uint8_t val, void *priv)
{
    intel_flash_t *dev = (intel_flash_t *) priv;

    if (dev->write_protect)
        return;

    addr = intel_flash_addr(dev, addr);

    switch (dev->command) {
        case CMD_ERASE_SETUP:
            if (val == CMD_ERASE_CONFIRM) {
                for (uint8_t i = 0; i < 6; i++) {
                    if ((i == dev->program_addr) && (addr >= dev->block_start[i]) && (addr <= dev->block_end[i])) {
                        memset(&(dev->array[dev->block_start[i]]), 0xff, dev->block_len[i]);
                        dev->dirty = 1;
                    }
                }

                dev->status = 0x80;
            }
            dev->command = CMD_READ_STATUS;
            break;

        case CMD_PROGRAM_SETUP:
        case CMD_PROGRAM_SETUP_ALT:
            if (((addr & dev->bb_mask) != (dev->block_start[6] & dev->bb_mask)) && (addr == dev->program_addr)) {
                dev->array[addr] = val;
                dev->dirty = 1;
            }
            dev->command = CMD_READ_STATUS;
            dev->status  = 0x80;
            break;

        default:
            dev->command = val;
            switch (val) {
                case CMD_CLEAR_STATUS:
                    dev->status = 0;
                    break;
                case CMD_ERASE_SETUP:
                    for (uint8_t i = 0; i < 7; i++) {
                        if ((addr >= dev->block_start[i]) && (addr <= dev->block_end[i]))
                            dev->program_addr = i;
                    }
                    break;
                case CMD_PROGRAM_SETUP:
                case CMD_PROGRAM_SETUP_ALT:
                    dev->program_addr = addr;
                    break;

                default:
                    break;
            }
    }
}

static void
intel_flash_writew(uint32_t addr, uint16_t val, void *priv)
{
    intel_flash_t *dev = (intel_flash_t *) priv;

    if (dev->write_protect)
        return;

    addr = intel_flash_addr(dev, addr);

    if (dev->flags & FLAG_WORD)
        switch (dev->command) {
            case CMD_ERASE_SETUP:
                if (val == CMD_ERASE_CONFIRM) {
                    for (uint8_t i = 0; i < 6; i++) {
                        if ((i == dev->program_addr) && (addr >= dev->block_start[i]) && (addr <= dev->block_end[i])) {
                            memset(&(dev->array[dev->block_start[i]]), 0xff, dev->block_len[i]);
                            dev->dirty = 1;
                        }
                    }

                    dev->status = 0x80;
                }
                dev->command = CMD_READ_STATUS;
                break;

            case CMD_PROGRAM_SETUP:
            case CMD_PROGRAM_SETUP_ALT:
                if (((addr & dev->bb_mask) != (dev->block_start[6] & dev->bb_mask)) && (addr == dev->program_addr))
                    *(uint16_t *) (&dev->array[addr]) = val;
                dev->command = CMD_READ_STATUS;
                dev->status  = 0x80;
                break;

            default:
                dev->command = val & 0xff;
                switch (val) {
                    case CMD_CLEAR_STATUS:
                        dev->status = 0;
                        break;
                    case CMD_ERASE_SETUP:
                        for (uint8_t i = 0; i < 7; i++) {
                            if ((addr >= dev->block_start[i]) && (addr <= dev->block_end[i])) {
                                dev->program_addr = i;
                                dev->dirty = 1;
                            }
                        }
                        break;
                    case CMD_PROGRAM_SETUP:
                    case CMD_PROGRAM_SETUP_ALT:
                        dev->program_addr = addr;
                        break;

                    default:
                        break;
                }
        }
}

static void
intel_flash_writel(UNUSED(uint32_t addr), UNUSED(uint32_t val), UNUSED(void *priv))
{
#if 0
    intel_flash_writew(addr, val & 0xffff, priv);
    intel_flash_writew(addr + 2, (val >> 16) & 0xffff, priv);
#endif
}

/*
   Where a window of the chip at base starts in the chip, and how the window
   repeats it if it is bigger than the chip. With A16 inverted, the chip's
   two halves of each 128k are swapped, so only a window that stays in one
   64k has an exec pointer.
 */
static void
intel_flash_window(const intel_flash_t *dev, uint32_t base, uint32_t size, uint8_t **exec, uint32_t *mask)
{
    const uint32_t inv    = (dev->flags & FLAG_INV_A16) ? 0x10000 : 0x00000;
    const uint32_t offset = base & dev->mask;

    if (size > (dev->mask + 1)) {
        if (offset != 0)
            fatal("Intel Flash: %08X bytes at %08X do not start on a %08X byte boundary\n",
                  size, base, dev->mask + 1);

        *exec = inv ? NULL : dev->array;
        *mask = dev->mask;
    } else {
        if ((offset + size) > (dev->mask + 1))
            fatal("Intel Flash: %08X bytes at %08X are past the end of the %08X byte chip\n",
                  size, base, dev->mask + 1);

        if (inv && ((offset ^ (offset + size - 1)) & 0x10000))
            *exec = NULL;
        else
            *exec = dev->array + (offset ^ inv);
        *mask = 0xffffffff;
    }
}

/* Map a window of the chip at base, which may be bigger than the chip. */
void
intel_flash_map(intel_flash_t *dev, mem_mapping_t *mapping, uint32_t base, uint32_t size)
{
    uint8_t *exec;
    uint32_t mask;

    intel_flash_window(dev, base, size, &exec, &mask);

    mem_mapping_add(mapping, base, size,
                    intel_flash_read, intel_flash_readw, intel_flash_readl,
                    intel_flash_write, intel_flash_writew, intel_flash_writel,
                    exec, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM | MEM_MAPPING_ROMCS | MEM_MAPPING_ROM_WS, (void *) dev);
    if (mask != 0xffffffff)
        mem_mapping_set_mask(mapping, mask);
}

/* Move a window added by intel_flash_map() elsewhere, under the same rules. */
void
intel_flash_remap(intel_flash_t *dev, mem_mapping_t *mapping, uint32_t base, uint32_t size)
{
    uint8_t *exec;
    uint32_t mask;

    intel_flash_window(dev, base, size, &exec, &mask);

    mem_mapping_set_addr(mapping, base, size);
    mem_mapping_set_exec(mapping, exec);
    mem_mapping_set_mask(mapping, mask);
}

void
intel_flash_set_write_protect(intel_flash_t *dev, int protect)
{
    dev->write_protect = !!protect;
}

uint32_t
intel_flash_get_size(const intel_flash_t *dev)
{
    return dev->size;
}

void
intel_flash_reset(intel_flash_t *dev)
{
    dev->command = CMD_READ_ARRAY;
    dev->status  = 0;
}

/* The backing file holds the main and data blocks, in that order, but not the boot block. */
static void
intel_flash_file(intel_flash_t *dev, FILE *fp, int write)
{
    static const uint8_t order[] = { BLOCK_MAIN1, BLOCK_MAIN2, BLOCK_MAIN3, BLOCK_MAIN4, BLOCK_DATA1, BLOCK_DATA2 };

    for (uint8_t i = 0; i < sizeof(order); i++) {
        const uint8_t b = order[i];

        /* Main block 1 and the data blocks always go in, even if empty. */
        if ((dev->block_len[b] == 0) && (b != BLOCK_MAIN1) && (b != BLOCK_DATA1) && (b != BLOCK_DATA2))
            continue;

        if (write)
            fwrite(&(dev->array[dev->block_start[b]]), dev->block_len[b], 1, fp);
        else
            (void) !fread(&(dev->array[dev->block_start[b]]), dev->block_len[b], 1, fp);
    }
}

intel_flash_t *
intel_flash_create(const device_t *model, const flash_cfg_t *cfg)
{
    FILE          *fp;
    intel_flash_t *dev  = calloc(1, sizeof(intel_flash_t));
    uint8_t        type = model->local & 0xff;

    if (cfg->size == 0)
        fatal("Intel Flash: the chip's size is required\n");

    dev->flags = model->local & 0xff;
    dev->size  = cfg->size;
    dev->mask  = cfg->size - 1;

    dev->bb_mask = dev->mask & 0xffffe000;
    if ((dev->mask == 0x3ffff) || (dev->mask == 0x7ffff))
        dev->bb_mask &= 0xffffc000;

    dev->array = (uint8_t *) calloc(1, dev->size);
    memset(dev->array, 0xff, dev->size);

    /* The image is what the CPU sees, so with A16 inverted, each 64k goes in the other half. */
    if (cfg->image != NULL) {
        const uint32_t inv = (dev->flags & FLAG_INV_A16) ? 0x10000 : 0x00000;

        for (uint32_t i = 0; (i < dev->size) && ((i + 0x10000) <= cfg->image_len); i += 0x10000)
            memcpy(&dev->array[(i ^ inv) & dev->mask], &cfg->image[i], 0x10000);
    }

    switch (dev->mask) {
        case 0x7ffff:
            if (dev->flags & FLAG_X00)
                dev->flash_id = (dev->flags & FLAG_BXB) ? 0x71 : 0x70;
            else if (dev->flags & FLAG_MICRON)
                dev->flash_id = (dev->flags & FLAG_BXB) ? 0x79 : 0x78;
            else if (dev->flags & FLAG_WORD)
                dev->flash_id = (dev->flags & FLAG_BXB) ? 0x4471 : 0x4470;
            else
                dev->flash_id = (dev->flags & FLAG_BXB) ? 0x8A : 0x89;

            /* The block lengths are the same both flash types. */
            dev->block_len[BLOCK_MAIN1] = 0x20000;
            dev->block_len[BLOCK_MAIN2] = 0x20000;
            dev->block_len[BLOCK_MAIN3] = 0x20000;
            dev->block_len[BLOCK_MAIN4] = 0x18000;
            dev->block_len[BLOCK_DATA1] = 0x02000;
            dev->block_len[BLOCK_DATA2] = 0x02000;
            dev->block_len[BLOCK_BOOT]  = 0x04000;

            if (dev->flags & FLAG_BXB) {                 /* 28F004BX-T/28F400BX-B */
                dev->block_start[BLOCK_BOOT]  = 0x00000; /* MAIN BLOCK 1 */
                dev->block_end[BLOCK_BOOT]    = 0x1ffff;
                dev->block_start[BLOCK_DATA2] = 0x20000; /* MAIN BLOCK 2 */
                dev->block_end[BLOCK_DATA2]   = 0x3ffff;
                dev->block_start[BLOCK_DATA1] = 0x40000; /* MAIN BLOCK 3 */
                dev->block_end[BLOCK_DATA1]   = 0x5ffff;
                dev->block_start[BLOCK_MAIN4] = 0x60000; /* MAIN BLOCK 4 */
                dev->block_end[BLOCK_MAIN4]   = 0x77fff;
                dev->block_start[BLOCK_MAIN3] = 0x78000; /* DATA AREA 1 BLOCK */
                dev->block_end[BLOCK_MAIN3]   = 0x79fff;
                dev->block_start[BLOCK_MAIN2] = 0x7a000; /* DATA AREA 2 BLOCK */
                dev->block_end[BLOCK_MAIN2]   = 0x7bfff;
                dev->block_start[BLOCK_MAIN1] = 0x7c000; /* BOOT BLOCK */
                dev->block_end[BLOCK_MAIN1]   = 0x7ffff;
            } else {
                dev->block_start[BLOCK_MAIN1] = 0x00000; /* MAIN BLOCK 1 */
                dev->block_end[BLOCK_MAIN1]   = 0x1ffff;
                dev->block_start[BLOCK_MAIN2] = 0x20000; /* MAIN BLOCK 2 */
                dev->block_end[BLOCK_MAIN2]   = 0x3ffff;
                dev->block_start[BLOCK_MAIN3] = 0x40000; /* MAIN BLOCK 3 */
                dev->block_end[BLOCK_MAIN3]   = 0x5ffff;
                dev->block_start[BLOCK_MAIN4] = 0x60000; /* MAIN BLOCK 4 */
                dev->block_end[BLOCK_MAIN4]   = 0x77fff;
                dev->block_start[BLOCK_DATA1] = 0x78000; /* DATA AREA 1 BLOCK */
                dev->block_end[BLOCK_DATA1]   = 0x79fff;
                dev->block_start[BLOCK_DATA2] = 0x7a000; /* DATA AREA 2 BLOCK */
                dev->block_end[BLOCK_DATA2]   = 0x7bfff;
                dev->block_start[BLOCK_BOOT]  = 0x7c000; /* BOOT BLOCK */
                dev->block_end[BLOCK_BOOT]    = 0x7ffff;
            }
            break;

        case 0x3ffff:
            if (dev->flags & FLAG_X00)
                dev->flash_id = (dev->flags & FLAG_BXB) ? 0x75 : 0x74;
            else if (dev->flags & FLAG_WORD)
                dev->flash_id = (dev->flags & FLAG_BXB) ? 0x2275 : 0x2274;
            else
                dev->flash_id = (dev->flags & FLAG_BXB) ? 0x7D : 0x7C;

            /* The block lengths are the same both flash types. */
            dev->block_len[BLOCK_MAIN1] = 0x20000;
            dev->block_len[BLOCK_MAIN2] = 0x18000;
            dev->block_len[BLOCK_MAIN3] = 0x00000;
            dev->block_len[BLOCK_MAIN4] = 0x00000;
            dev->block_len[BLOCK_DATA1] = 0x02000;
            dev->block_len[BLOCK_DATA2] = 0x02000;
            dev->block_len[BLOCK_BOOT]  = 0x04000;

            if (dev->flags & FLAG_BXB) {                 /* 28F002BX-B/28F200BX-B */
                dev->block_start[BLOCK_MAIN1] = 0x20000; /* MAIN BLOCK 1 */
                dev->block_end[BLOCK_MAIN1]   = 0x3ffff;
                dev->block_start[BLOCK_MAIN2] = 0x08000; /* MAIN BLOCK 2 */
                dev->block_end[BLOCK_MAIN2]   = 0x1ffff;
                dev->block_start[BLOCK_MAIN3] = 0xfffff; /* MAIN BLOCK 3 */
                dev->block_end[BLOCK_MAIN3]   = 0xfffff;
                dev->block_start[BLOCK_MAIN4] = 0xfffff; /* MAIN BLOCK 4 */
                dev->block_end[BLOCK_MAIN4]   = 0xfffff;
                dev->block_start[BLOCK_DATA1] = 0x06000; /* DATA AREA 1 BLOCK */
                dev->block_end[BLOCK_DATA1]   = 0x07fff;
                dev->block_start[BLOCK_DATA2] = 0x04000; /* DATA AREA 2 BLOCK */
                dev->block_end[BLOCK_DATA2]   = 0x05fff;
                dev->block_start[BLOCK_BOOT]  = 0x00000; /* BOOT BLOCK */
                dev->block_end[BLOCK_BOOT]    = 0x03fff;
            } else {                                     /* 28F002BX-T/28F200BX-T */
                dev->block_start[BLOCK_MAIN1] = 0x00000; /* MAIN BLOCK 1 */
                dev->block_end[BLOCK_MAIN1]   = 0x1ffff;
                dev->block_start[BLOCK_MAIN2] = 0x20000; /* MAIN BLOCK 2 */
                dev->block_end[BLOCK_MAIN2]   = 0x37fff;
                dev->block_start[BLOCK_MAIN3] = 0xfffff; /* MAIN BLOCK 3 */
                dev->block_end[BLOCK_MAIN3]   = 0xfffff;
                dev->block_start[BLOCK_MAIN4] = 0xfffff; /* MAIN BLOCK 4 */
                dev->block_end[BLOCK_MAIN4]   = 0xfffff;
                dev->block_start[BLOCK_DATA1] = 0x38000; /* DATA AREA 1 BLOCK */
                dev->block_end[BLOCK_DATA1]   = 0x39fff;
                dev->block_start[BLOCK_DATA2] = 0x3a000; /* DATA AREA 2 BLOCK */
                dev->block_end[BLOCK_DATA2]   = 0x3bfff;
                dev->block_start[BLOCK_BOOT]  = 0x3c000; /* BOOT BLOCK */
                dev->block_end[BLOCK_BOOT]    = 0x3ffff;
            }
            break;

        default:
            dev->flash_id = (type & FLAG_BXB) ? 0x95 : 0x94;

            /* The block lengths are the same both flash types. */
            dev->block_len[BLOCK_MAIN1] = 0x1c000;
            dev->block_len[BLOCK_MAIN2] = 0x00000;
            dev->block_len[BLOCK_MAIN3] = 0x00000;
            dev->block_len[BLOCK_MAIN4] = 0x00000;
            dev->block_len[BLOCK_DATA1] = 0x01000;
            dev->block_len[BLOCK_DATA2] = 0x01000;
            dev->block_len[BLOCK_BOOT]  = 0x02000;

            if (dev->flags & FLAG_BXB) {                 /* 28F001BX-B/28F100BX-B */
                dev->block_start[BLOCK_MAIN1] = 0x04000; /* MAIN BLOCK 1 */
                dev->block_end[BLOCK_MAIN1]   = 0x1ffff;
                dev->block_start[BLOCK_MAIN2] = 0xfffff; /* MAIN BLOCK 2 */
                dev->block_end[BLOCK_MAIN2]   = 0xfffff;
                dev->block_start[BLOCK_MAIN3] = 0xfffff; /* MAIN BLOCK 3 */
                dev->block_end[BLOCK_MAIN3]   = 0xfffff;
                dev->block_start[BLOCK_MAIN4] = 0xfffff; /* MAIN BLOCK 4 */
                dev->block_end[BLOCK_MAIN4]   = 0xfffff;
                dev->block_start[BLOCK_DATA1] = 0x02000; /* DATA AREA 1 BLOCK */
                dev->block_end[BLOCK_DATA1]   = 0x02fff;
                dev->block_start[BLOCK_DATA2] = 0x03000; /* DATA AREA 2 BLOCK */
                dev->block_end[BLOCK_DATA2]   = 0x03fff;
                dev->block_start[BLOCK_BOOT]  = 0x00000; /* BOOT BLOCK */
                dev->block_end[BLOCK_BOOT]    = 0x01fff;
            } else {                                     /* 28F001BX-T/28F100BX-T */
                dev->block_start[BLOCK_MAIN1] = 0x00000; /* MAIN BLOCK 1 */
                dev->block_end[BLOCK_MAIN1]   = 0x1bfff;
                dev->block_start[BLOCK_MAIN2] = 0xfffff; /* MAIN BLOCK 2 */
                dev->block_end[BLOCK_MAIN2]   = 0xfffff;
                dev->block_start[BLOCK_MAIN3] = 0xfffff; /* MAIN BLOCK 3 */
                dev->block_end[BLOCK_MAIN3]   = 0xfffff;
                dev->block_start[BLOCK_MAIN4] = 0xfffff; /* MAIN BLOCK 4 */
                dev->block_end[BLOCK_MAIN4]   = 0xfffff;
                dev->block_start[BLOCK_DATA1] = 0x1c000; /* DATA AREA 1 BLOCK */
                dev->block_end[BLOCK_DATA1]   = 0x1cfff;
                dev->block_start[BLOCK_DATA2] = 0x1d000; /* DATA AREA 2 BLOCK */
                dev->block_end[BLOCK_DATA2]   = 0x1dfff;
                dev->block_start[BLOCK_BOOT]  = 0x1e000; /* BOOT BLOCK */
                dev->block_end[BLOCK_BOOT]    = 0x1ffff;
            }
            break;
    }

    intel_flash_reset(dev);

    if (cfg->nvr_name != NULL) {
        if (strlen(cfg->nvr_name) == 0)
            fatal("Attempting to open the Flash file for reading with an empty invalid name\n");

        snprintf(dev->nvr_path, sizeof(dev->nvr_path), "%s.bin", cfg->nvr_name);

        fp = nvr_fopen(dev->nvr_path, "rb");
        if (fp != NULL) {
            if (!dump_missing)
                intel_flash_file(dev, fp, 0);
            fclose(fp);
        } else if (!dump_missing)
            dev->dirty = 1;
    }

    return dev;
}

void
intel_flash_destroy(intel_flash_t *dev)
{
    FILE *fp;

    if (dev->dirty && (dev->nvr_path[0] != '\0')) {
        fp = nvr_fopen(dev->nvr_path, "wb");
        if (fp != NULL) {
            if (!dump_missing)
                intel_flash_file(dev, fp, 1);
            fclose(fp);
        } else if (!dump_missing)
            warning("Unable to open %s for writing, please make sure your NVR folder is writable\n", dev->nvr_path);
    }

    free(dev->array);
    dev->array = NULL;

    free(dev);
}

/* The boot BIOS: the chip behind the BIOS chip select. */
static uint8_t
intel_flash_bios_read(uint32_t addr, void *priv)
{
    const intel_flash_bios_t *dev = (intel_flash_bios_t *) priv;

    if (!flash_bios_read_selected(addr))
        return 0xff;

    return intel_flash_read(addr, dev->chip);
}

static uint16_t
intel_flash_bios_readw(uint32_t addr, void *priv)
{
    const intel_flash_bios_t *dev = (intel_flash_bios_t *) priv;

    if (!flash_bios_read_selected(addr))
        return 0xffff;

    return intel_flash_readw(addr, dev->chip);
}

static uint32_t
intel_flash_bios_readl(uint32_t addr, void *priv)
{
    const intel_flash_bios_t *dev = (intel_flash_bios_t *) priv;

    if (!flash_bios_read_selected(addr))
        return 0xffffffff;

    return intel_flash_readl(addr, dev->chip);
}

static void
intel_flash_bios_write(uint32_t addr, uint8_t val, void *priv)
{
    const intel_flash_bios_t *dev = (intel_flash_bios_t *) priv;

    if (!flash_bios_write_selected(addr))
        return;

    intel_flash_write(addr, val, dev->chip);
}

static void
intel_flash_bios_writew(uint32_t addr, uint16_t val, void *priv)
{
    const intel_flash_bios_t *dev = (intel_flash_bios_t *) priv;

    if (!flash_bios_write_selected(addr))
        return;

    intel_flash_writew(addr, val, dev->chip);
}

static void
intel_flash_bios_writel(uint32_t addr, uint32_t val, void *priv)
{
    const intel_flash_bios_t *dev = (intel_flash_bios_t *) priv;

    intel_flash_writel(addr, val, dev->chip);
}

/* The chip select changed: keep an exec pointer only on a mapping the
   chipset decodes whole (flash_bios_mapping_update). */
static void
intel_flash_decode_hook(void *priv)
{
    intel_flash_bios_t *dev = (intel_flash_bios_t *) priv;

    for (uint8_t i = 0; i < 2; i++) {
        if (dev->mapping[i].size)
            flash_bios_mapping_update(&dev->mapping[i], dev->exec[i]);
    }
    for (uint8_t i = 0; i < 16; i++) {
        if (dev->mapping_h[i].size)
            flash_bios_mapping_update(&dev->mapping_h[i], dev->exec_h[i]);
    }
}

static void
intel_flash_bios_map(intel_flash_bios_t *dev, mem_mapping_t *mapping, uint8_t *exec, uint32_t base)
{
    mem_mapping_add(mapping, base, 0x10000,
                    intel_flash_bios_read, intel_flash_bios_readw, intel_flash_bios_readl,
                    intel_flash_bios_write, intel_flash_bios_writew, intel_flash_bios_writel,
                    exec, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM | MEM_MAPPING_ROMCS | MEM_MAPPING_ROM_WS, (void *) dev);
}

/*
   The top of the chip at the top of the first megabyte, as much of it as
   fits below 1 MB down to 0xE0000, and all of it twice at the top of the 4 GB
   space. With A16 inverted, each 64k of the bus is in the other half of the
   chip.
 */
static void
intel_flash_add_mappings(intel_flash_bios_t *dev)
{
    const uint32_t mask = dev->chip->mask;
    uint8_t        max  = 2;
    uint32_t       base;
    uint32_t       fbase;
    uint32_t       sub = 0x20000;

    if (mask == 0x7ffff) {
        sub = 0x80000;
        max = 8;
    } else if (mask == 0x3ffff) {
        sub = 0x40000;
        max = 4;
    }

    for (uint8_t i = 0; i < max; i++) {
        if (mask == 0x7ffff)
            base = 0x80000 + (i << 16);
        else if (mask == 0x3ffff)
            base = 0xc0000 + (i << 16);
        else
            base = 0xe0000 + (i << 16);

        fbase = base & mask;
        if (dev->chip->flags & FLAG_INV_A16)
            fbase ^= 0x10000;

        if ((max == 2) || (i >= (max - 2))) {
            dev->exec[i & 1] = dev->chip->array + fbase;
            intel_flash_bios_map(dev, &(dev->mapping[i & 1]), dev->exec[i & 1], base);
        }
        dev->exec_h[i]       = dev->chip->array + fbase;
        dev->exec_h[i + max] = dev->chip->array + fbase;
        intel_flash_bios_map(dev, &(dev->mapping_h[i]), dev->exec_h[i], (base | 0xfff00000) - sub);
        intel_flash_bios_map(dev, &(dev->mapping_h[i + max]), dev->exec_h[i + max], base | 0xfff00000);
    }
}

static void
intel_flash_bios_reset(void *priv)
{
    intel_flash_bios_t *dev = (intel_flash_bios_t *) priv;

    intel_flash_reset(dev->chip);
}

static void *
intel_flash_init(const device_t *info)
{
    intel_flash_bios_t *dev = calloc(1, sizeof(intel_flash_bios_t));
    flash_cfg_t         cfg = { 0 };

    cfg.nvr_name  = machine_get_nvr_name_ex(machine);
    cfg.image     = rom;
    cfg.image_len = biosmask + 1;
    cfg.size      = biosmask + 1;

    mem_mapping_disable(&bios_mapping);
    mem_mapping_disable(&bios_high_mapping);

    dev->chip = intel_flash_create(info, &cfg);

    intel_flash_add_mappings(dev);
    flash_bios_set_decode_hook(intel_flash_decode_hook, dev);
    intel_flash_decode_hook(dev);

    return dev;
}

static void
intel_flash_close(void *priv)
{
    intel_flash_bios_t *dev = (intel_flash_bios_t *) priv;

    flash_bios_set_decode_hook(NULL, NULL);

    intel_flash_destroy(dev->chip);

    free(dev);
}

/* For AMI BIOS'es - Intel 28F001BXT with A16 pin inverted. */
const device_t intel_flash_bxt_ami_device = {
    .name          = "Intel 28F001BXT/28F002BXT/28F004BXT Flash BIOS",
    .internal_name = "intel_flash_bxt_ami",
    .flags         = DEVICE_PCI,
    .local         = FLAG_INV_A16,
    .init          = intel_flash_init,
    .close         = intel_flash_close,
    .reset         = intel_flash_bios_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t intel_flash_bxt_device = {
    .name          = "Intel 28F001BXT/28F002BXT/28F004BXT Flash BIOS",
    .internal_name = "intel_flash_bxt",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = intel_flash_init,
    .close         = intel_flash_close,
    .reset         = intel_flash_bios_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t intel_flash_bxb_device = {
    .name          = "Intel 28F001BXB/28F002BXB/28F004BXB Flash BIOS",
    .internal_name = "intel_flash_bxb",
    .flags         = DEVICE_PCI,
    .local         = FLAG_BXB,
    .init          = intel_flash_init,
    .close         = intel_flash_close,
    .reset         = intel_flash_bios_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t micron_flash_t_device = {
    .name          = "Micron 28F00xB5-T Flash BIOS",
    .internal_name = "micron_flash_t",
    .flags         = DEVICE_PCI,
    .local         = FLAG_MICRON,
    .init          = intel_flash_init,
    .close         = intel_flash_close,
    .reset         = intel_flash_bios_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t micron_flash_x00_t_device = {
    .name          = "Micron 28FX00B5-T Flash BIOS",
    .internal_name = "micron_flash_x00_t",
    .flags         = DEVICE_PCI,
    .local         = FLAG_MICRON | FLAG_X00,
    .init          = intel_flash_init,
    .close         = intel_flash_close,
    .reset         = intel_flash_bios_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
