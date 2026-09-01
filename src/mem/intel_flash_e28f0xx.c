/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Intel 4-, 8-, and 16-mbit Smart Flash
 *          devices.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2020 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/machine.h>
#include <86box/nvr.h>
#include <86box/plat.h>

#define FLAG_WORD    1

enum {
    CMD_READ_ARRAY        = 0xff,
    CMD_IID               = 0x90,
    CMD_READ_STATUS       = 0x70,
    CMD_CLEAR_STATUS      = 0x50,
    CMD_ERASE_SETUP       = 0x20,
    CMD_ERASE_CONFIRM     = 0xd0,
    CMD_ERASE_SUSPEND     = 0xb0,
    CMD_PROGRAM_SETUP     = 0x40,
    CMD_PROGRAM_SETUP_ALT = 0x10,
    CMD_SPECIAL           = 0x60,
    CMD_SET_BLOCK_LOCK    = 0x01,
    CMD_SET_MASTER_LOCK   = 0xF1,
    CMD_CLEAR_BLOCK_LOCKS = 0xD0
};

typedef struct flash_t {
    uint8_t  command;
    uint8_t  status;
    uint8_t  master_lock;
    uint8_t  flags;
    uint8_t *array;

    uint8_t  block_locks[32];

    uint16_t flash_id;
    uint16_t pad16;

    uint32_t program_addr;

    mem_mapping_t mapping[4];
    mem_mapping_t mapping_h[64];
} flash_t;

static char flash_path[1024];

static uint8_t
flash_read(uint32_t addr, void *priv)
{
    const flash_t *dev = (flash_t *) priv;
    uint8_t        ret;

    addr &= biosmask;

    switch (dev->command) {
        default:
        case CMD_READ_ARRAY:
            ret = dev->array[addr];
            break;

        case CMD_IID:
            switch (addr & 0x0000ffff) {
                default:
                    ret = 0xff;
                    break;
                case 0x00000000:
                    ret = 0x89;
                    break;
                case 0x00000001:
                    ret = dev->flash_id;
                    break;
                case 0x00000002:
                    ret = dev->block_locks[addr >> 16];
                    break;
                case 0x00000003:
                    ret = dev->master_lock;
                    break;
            }
            break;

        case CMD_READ_STATUS:
            ret = dev->status;
            pclog("Read status: %02X\n", ret);
            break;
    }

    return ret;
}

static uint16_t
flash_readw(uint32_t addr, void *priv)
{
    const flash_t  *dev = (flash_t *) priv;

    addr &= biosmask;

    if (dev->flags & FLAG_WORD)
        addr &= 0xfffffffe;

    const uint16_t *q   = (uint16_t *) &(dev->array[addr]);
    uint16_t        ret = *q;

    if (dev->flags & FLAG_WORD)
        switch (dev->command) {
            default:
            case CMD_READ_ARRAY:
                break;

            case CMD_IID:
                switch (addr & 0x0000ffff) {
                default:
                        ret = 0xff;
                        break;
                case 0x00000000:
                        ret = 0x89;
                        break;
                case 0x00000001:
                        ret = dev->flash_id;
                        break;
                case 0x00000002:
                        ret = dev->block_locks[addr >> 16];
                        break;
                case 0x00000003:
                        ret = dev->master_lock;
                        break;
                }
                break;

            case CMD_READ_STATUS:
                ret = dev->status;
                break;
        }

    return ret;
}

static uint32_t
flash_readl(uint32_t addr, void *priv)
{
    const flash_t  *dev = (flash_t *) priv;

    addr &= biosmask;

    const uint32_t *q   = (uint32_t *) &(dev->array[addr]);

    return *q;
}

static void
flash_write(uint32_t addr, uint8_t val, void *priv)
{
    flash_t *      dev         = (flash_t *) priv;

    addr &= biosmask;

    const uint32_t block_start = addr & 0xffff0000;

    pclog("Write %02X at %08X\n", val, addr);

    switch (dev->command) {
        case CMD_SPECIAL:
            switch (val) {
                default:
                    break;
                case CMD_SET_BLOCK_LOCK:
                    if (block_start < 0x00100000)
                        dev->block_locks[block_start >> 16] = 1;
                    break;
                case CMD_SET_MASTER_LOCK:
                    dev->master_lock = 1;
                    break;
                case CMD_CLEAR_BLOCK_LOCKS:
                    for (int i = 0; i < 16; i++)
                        dev->block_locks[i] = 0;
                    break;
            }
            dev->command = CMD_READ_STATUS;
            break;

        case CMD_ERASE_SETUP:
            if (val == CMD_ERASE_CONFIRM) {
                if (!dev->master_lock && !dev->block_locks[block_start >> 16] &&
                    !((addr ^ dev->program_addr) & 0xffff0000)) {
                    pclog("Erasing block %02X\n", block_start >> 16);
                    memset(&(dev->array[block_start]), 0xff, 0x00010000);
                }
                if ((dev->master_lock || dev->block_locks[block_start >> 16]) &&
                    !((addr ^ dev->program_addr) & 0xffff0000))
                    dev->status = 0x82;
                else
                    dev->status = 0x80;
                if ((addr ^ dev->program_addr) & 0xffff0000)
                    dev->status |= 0x20;
            }
            dev->command = CMD_READ_STATUS;
            break;

        case CMD_PROGRAM_SETUP:
        case CMD_PROGRAM_SETUP_ALT:
            pclog("Programming value %02X in block %02X\n", val, block_start >> 16);
            if (!dev->master_lock && !dev->block_locks[block_start >> 16] &&
                (addr == dev->program_addr))
                dev->array[addr] = val;
            dev->command = CMD_READ_STATUS;
            if ((addr == dev->program_addr) &&
                (dev->master_lock || dev->block_locks[block_start >> 16]))
                dev->status = 0x82;
            else
                dev->status = 0x80;
            if (addr != dev->program_addr)
                dev->status |= 0x10;
            break;

        default:
            dev->command = val;
            switch (val) {
                case CMD_CLEAR_STATUS:
                    dev->status = 0;
                    break;
                case CMD_ERASE_SETUP:
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
flash_writew(uint32_t addr, uint16_t val, void *priv)
{
    flash_t *      dev         = (flash_t *) priv;

    addr &= biosmask;

    const uint32_t block_start = addr & 0xffff0000;

    if (dev->flags & FLAG_WORD)
        switch (dev->command) {
            case CMD_SPECIAL:
                switch (val) {
                    default:
                        break;
                    case CMD_SET_BLOCK_LOCK:
                        if (block_start < 0x00100000)
                            dev->block_locks[block_start >> 16] = 1;
                        break;
                    case CMD_SET_MASTER_LOCK:
                        dev->master_lock = 1;
                        break;
                    case CMD_CLEAR_BLOCK_LOCKS:
                        for (int i = 0; i < 16; i++)
                            dev->block_locks[i] = 0;
                        break;
                }
                dev->command = CMD_READ_STATUS;
                break;

            case CMD_ERASE_SETUP:
                if (val == CMD_ERASE_CONFIRM) {
                    if (!dev->master_lock && !dev->block_locks[block_start >> 16] &&
                        !((addr ^ dev->program_addr) & 0xffff0000)) {
                        pclog("Erasing block %02X\n", block_start >> 16);
                        memset(&(dev->array[block_start]), 0xff, 0x00010000);
                    }
                    if ((dev->master_lock || dev->block_locks[block_start >> 16]) &&
                        !((addr ^ dev->program_addr) & 0xffff0000))
                        dev->status = 0x82;
                    else
                        dev->status = 0x80;
                    if ((addr ^ dev->program_addr) & 0xffff0000)
                        dev->status |= 0x20;
                }
                dev->command = CMD_READ_STATUS;
                break;

            case CMD_PROGRAM_SETUP:
            case CMD_PROGRAM_SETUP_ALT:
                if (!dev->master_lock && !dev->block_locks[block_start >> 16] &&
                    (addr == dev->program_addr))
                    *(uint16_t *) (&dev->array[addr]) = val;
                dev->command = CMD_READ_STATUS;
                if ((addr == dev->program_addr) &&
                    (dev->master_lock || dev->block_locks[block_start >> 16]))
                    dev->status = 0x82;
                else
                    dev->status = 0x80;
                if (addr != dev->program_addr)
                    dev->status |= 0x10;
                break;

            default:
                dev->command = val & 0xff;
                switch (val) {
                    case CMD_CLEAR_STATUS:
                        dev->status = 0;
                        break;
                    case CMD_ERASE_SETUP:
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
flash_writel(UNUSED(uint32_t addr), UNUSED(uint32_t val), UNUSED(void *priv))
{
    //
}

static void
flash_add_mappings(flash_t *dev)
{
    uint8_t  max = 2;
    uint32_t base;
    uint32_t sub = 0x20000;

    switch (biosmask) {
        default:
            fatal("Invalid BIOS mask for Intel E82F0xx flash: %08X!\n", biosmask);
            break;
        case 0x0007ffff:
            sub = 0x00080000;
            max = 8;
            break;
        case 0x000fffff:
            sub = 0x00100000;
            max = 16;
            break;
        case 0x001fffff:
            sub = 0x00200000;
            max = 32;
            break;
    }

    for (uint8_t i = 0; i < max; i++) {
        switch (biosmask) {
            default:
                base = 0x00040000 + (i << 16);
                break;
            case 0x0007ffff:
                base = 0x00080000 + (i << 16);
                break;
            case 0x000fffff:
                base = 0x00100000 + (i << 16);
                break;
            case 0x001fffff:
                base = 0x00200000 + (i << 16);
                break;
        }

        uint32_t fbase = base & biosmask;

        pclog("From ROM @ %08X to array @ %08X\n", base & biosmask, fbase);
        memcpy(&dev->array[fbase], &rom[base & biosmask], 0x10000);

        if ((max == 2) || (i >= 2)) {
            pclog("Low  mapping %2i at %08X-%08X\n", i, base, base + 0x0000ffff);
            mem_mapping_add(&(dev->mapping[i]), base, 0x10000,
                            flash_read, flash_readw, flash_readl,
                            flash_write, flash_writew, flash_writel,
                            dev->array + fbase, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM | MEM_MAPPING_ROMCS | MEM_MAPPING_ROM_WS, (void *) dev);
        }
        pclog("High mapping %2i at %08X-%08X\n", i, (base | 0xfff00000) - sub, (base | 0xfff00000) - sub + 0x0000ffff);
        mem_mapping_add(&(dev->mapping_h[i]), (base | 0xfff00000) - sub, 0x10000,
                        flash_read, flash_readw, flash_readl,
                        flash_write, flash_writew, flash_writel,
                        dev->array + fbase, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM | MEM_MAPPING_ROMCS | MEM_MAPPING_ROM_WS, (void *) dev);
        pclog("High mapping %2i at %08X-%08X\n", i + max, (base | 0xfff00000), (base | 0xfff00000) + 0x0000ffff);
        mem_mapping_add(&(dev->mapping_h[i + max]), (base | 0xfff00000), 0x10000,
                        flash_read, flash_readw, flash_readl,
                        flash_write, flash_writew, flash_writel,
                        dev->array + fbase, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM | MEM_MAPPING_ROMCS | MEM_MAPPING_ROM_WS, (void *) dev);
    }
}

static void
flash_reset(void *priv)
{
    flash_t *dev = (flash_t *) priv;

    dev->command = CMD_READ_ARRAY;
    dev->status  = 0;
}

static void *
flash_init(const device_t *info)
{
    FILE    *fp;
    flash_t *dev;

    dev = calloc(1, sizeof(flash_t));

    sprintf(flash_path, "%s.bin", machine_get_nvr_name_ex(machine));

    dev->flags = info->local & 0xff;

    mem_mapping_disable(&bios_mapping);
    mem_mapping_disable(&bios_high_mapping);

    dev->array = (uint8_t *) calloc(1, biosmask + 1);
    memset(dev->array, 0xff, biosmask + 1);

    switch (biosmask) {
        default:
        case 0x7ffff:
            dev->flash_id = 0xa7;
            break;
        case 0xfffff:
            dev->flash_id = 0xa6;
            break;
        case 0x1fffff:
            dev->flash_id = 0xaa;
            break;
    }

    flash_add_mappings(dev);

    dev->command = CMD_READ_ARRAY;
    dev->status  = 0;

    fp = nvr_fopen(flash_path, "rb");
    if (!dump_missing && (fp != NULL)) {
        (void) !fread(dev->array, biosmask + 1, 1, fp);
        fclose(fp);
    }

    return dev;
}

static void
flash_close(void *priv)
{
    FILE    *fp;
    flash_t *dev = (flash_t *) priv;

    fp = nvr_fopen(flash_path, "wb");
    if (!dump_missing)
        fwrite(dev->array, biosmask + 1, 1, fp);
    fclose(fp);

    free(dev->array);
    dev->array = NULL;

    free(dev);
}

const device_t intel_flash_e28f0xx_device = {
    .name          = "Intel E82F0xx Flash BIOS",
    .internal_name = "intel_flash_e28f0xx",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = flash_init,
    .close         = flash_close,
    .reset         = flash_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
