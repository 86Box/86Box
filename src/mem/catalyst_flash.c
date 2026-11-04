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
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
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
#include <86box/plat_fallthrough.h>
#include <86box/flash.h>

#define FLAG_WORD    4
#define FLAG_BXB     2
#define FLAG_INV_A16 1

#define CATALYST_SIZE 0x20000

enum {
    BLOCK_MAIN1,
    BLOCK_MAIN2,
    BLOCK_DATA1,
    BLOCK_DATA2,
    BLOCK_BOOT,
    BLOCKS_NUM
};

enum {
    CMD_SET_READ         = 0x00,
    CMD_READ_AUTO_SELECT = 0x80,
    CMD_READ_SIGNATURE   = 0x90,
    CMD_ERASE            = 0x20,
    CMD_ERASE_CONFIRM    = 0x20,
    CMD_ERASE_VERIFY     = 0xA0,
    CMD_PROGRAM          = 0x40,
    CMD_PROGRAM_VERIFY   = 0xC0,
    CMD_RESET            = 0xFF
};

/* The chip. */
struct catalyst_t {
    uint8_t  command;
    uint8_t  is_amd;
    uint8_t  dirty;
    uint8_t  write_protect;
    uint8_t *array;

    uint32_t size;
    uint32_t mask;

    char nvr_path[1024];
};

/* The chip as the boot BIOS. */
typedef struct catalyst_bios_t {
    catalyst_t *chip;

    mem_mapping_t mapping;
    mem_mapping_t mapping_h[2];
} catalyst_bios_t;

static uint8_t
catalyst_read(uint32_t addr, void *priv)
{
    const catalyst_t *dev = (catalyst_t *) priv;
    uint8_t           ret = 0xff;

    addr &= dev->mask;

    switch (dev->command) {
        case CMD_ERASE_VERIFY:
        case CMD_PROGRAM_VERIFY:
        case CMD_RESET:
        case CMD_SET_READ:
            ret = dev->array[addr];
            break;

        case CMD_READ_AUTO_SELECT:
            if (!dev->is_amd)
                break;
            fallthrough;
        case CMD_READ_SIGNATURE:
            if (dev->is_amd) {
                if (addr == 0x00000)
                    ret = 0x01; /* AMD */
                else if (addr == 0x00001)
                    ret = 0xa7; /* Am28F010 */
            } else {
                if (addr == 0x00000)
                    ret = 0x31; /* CATALYST */
                else if (addr == 0x00001)
                    ret = 0xb4; /* 28F010 */
            }
            break;

        default:
            break;
    }

    return ret;
}

static uint16_t
catalyst_readw(uint32_t addr, void *priv)
{
    catalyst_t     *dev = (catalyst_t *) priv;
    const uint16_t *q;

    addr &= dev->mask;

    q = (uint16_t *) &(dev->array[addr]);

    return *q;
}

static uint32_t
catalyst_readl(uint32_t addr, void *priv)
{
    catalyst_t     *dev = (catalyst_t *) priv;
    const uint32_t *q;

    addr &= dev->mask;

    q = (uint32_t *) &(dev->array[addr]);

    return *q;
}

static void
catalyst_write(uint32_t addr, uint8_t val, void *priv)
{
    catalyst_t *dev = (catalyst_t *) priv;

    if (dev->write_protect)
        return;

    addr &= dev->mask;

    switch (dev->command) {
        case CMD_ERASE:
            if (val == CMD_ERASE_CONFIRM) {
                memset(dev->array, 0xff, dev->size);
                dev->dirty = 1;
            }
            break;

        case CMD_PROGRAM:
            dev->array[addr] = val;
            dev->dirty = 1;
            break;

        default:
            dev->command = val;
            break;
    }
}

static void
catalyst_writew(UNUSED(uint32_t addr), UNUSED(uint16_t val), UNUSED(void *priv))
{
    //
}

static void
catalyst_writel(UNUSED(uint32_t addr), UNUSED(uint32_t val), UNUSED(void *priv))
{
    //
}

/*
   Where a window of the chip at base starts in the chip, and how the window
   repeats it if it is bigger than the chip.
 */
static void
catalyst_window(const catalyst_t *dev, uint32_t base, uint32_t size, uint8_t **exec, uint32_t *mask)
{
    const uint32_t offset = base & dev->mask;

    if (size > (dev->mask + 1)) {
        if (offset != 0)
            fatal("Catalyst Flash: %08X bytes at %08X do not start on a %08X byte boundary\n",
                  size, base, dev->mask + 1);

        *exec = dev->array;
        *mask = dev->mask;
    } else {
        if ((offset + size) > (dev->mask + 1))
            fatal("Catalyst Flash: %08X bytes at %08X are past the end of the %08X byte chip\n",
                  size, base, dev->mask + 1);

        *exec = dev->array + offset;
        *mask = 0xffffffff;
    }
}

/* Map a window of the chip at base, which may be bigger than the chip. */
void
catalyst_flash_map(catalyst_t *dev, mem_mapping_t *mapping, uint32_t base, uint32_t size)
{
    uint8_t *exec;
    uint32_t mask;

    catalyst_window(dev, base, size, &exec, &mask);

    mem_mapping_add(mapping, base, size,
                    catalyst_read, catalyst_readw, catalyst_readl,
                    catalyst_write, catalyst_writew, catalyst_writel,
                    exec, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM | MEM_MAPPING_ROMCS | MEM_MAPPING_ROM_WS, (void *) dev);
    if (mask != 0xffffffff)
        mem_mapping_set_mask(mapping, mask);
}

/* Move a window added by catalyst_flash_map() elsewhere, under the same rules. */
void
catalyst_flash_remap(catalyst_t *dev, mem_mapping_t *mapping, uint32_t base, uint32_t size)
{
    uint8_t *exec;
    uint32_t mask;

    catalyst_window(dev, base, size, &exec, &mask);

    mem_mapping_set_addr(mapping, base, size);
    mem_mapping_set_exec(mapping, exec);
    mem_mapping_set_mask(mapping, mask);
}

void
catalyst_flash_set_write_protect(catalyst_t *dev, int protect)
{
    dev->write_protect = !!protect;
}

uint32_t
catalyst_flash_get_size(const catalyst_t *dev)
{
    return dev->size;
}

void
catalyst_flash_reset(catalyst_t *dev)
{
    dev->command = CMD_RESET;
}

catalyst_t *
catalyst_flash_create(const device_t *model, const flash_cfg_t *cfg)
{
    FILE       *fp;
    catalyst_t *dev = calloc(1, sizeof(catalyst_t));

    dev->size   = CATALYST_SIZE;
    dev->mask   = dev->size - 1;
    dev->is_amd = model->local;

    dev->array = (uint8_t *) calloc(1, dev->size);
    memset(dev->array, 0xff, dev->size);

    if (cfg->image != NULL)
        memcpy(dev->array, cfg->image, MIN(cfg->image_len, dev->size));

    catalyst_flash_reset(dev);

    if (cfg->nvr_name != NULL) {
        if (strlen(cfg->nvr_name) == 0)
            fatal("Attempting to open the Flash file for reading with an empty invalid name\n");

        snprintf(dev->nvr_path, sizeof(dev->nvr_path), "%s.bin", cfg->nvr_name);

        fp = nvr_fopen(dev->nvr_path, "rb");
        if (fp != NULL) {
            if (!dump_missing)
                (void) !fread(dev->array, dev->size, 1, fp);
            fclose(fp);
        } else if (!dump_missing)
            dev->dirty = 1;
    }

    return dev;
}

void
catalyst_flash_destroy(catalyst_t *dev)
{
    FILE *fp;

    if (dev->dirty && (dev->nvr_path[0] != '\0')) {
        fp = nvr_fopen(dev->nvr_path, "wb");
        if (fp != NULL) {
            if (!dump_missing)
                fwrite(dev->array, dev->size, 1, fp);
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
catalyst_bios_read(uint32_t addr, void *priv)
{
    const catalyst_bios_t *dev = (catalyst_bios_t *) priv;

    if (!flash_bios_read_selected(addr))
        return 0xff;

    return catalyst_read(addr, dev->chip);
}

static uint16_t
catalyst_bios_readw(uint32_t addr, void *priv)
{
    const catalyst_bios_t *dev = (catalyst_bios_t *) priv;

    if (!flash_bios_read_selected(addr))
        return 0xffff;

    return catalyst_readw(addr, dev->chip);
}

static uint32_t
catalyst_bios_readl(uint32_t addr, void *priv)
{
    const catalyst_bios_t *dev = (catalyst_bios_t *) priv;

    if (!flash_bios_read_selected(addr))
        return 0xffffffff;

    return catalyst_readl(addr, dev->chip);
}

static void
catalyst_bios_write(uint32_t addr, uint8_t val, void *priv)
{
    const catalyst_bios_t *dev = (catalyst_bios_t *) priv;

    if (!flash_bios_write_selected(addr))
        return;

    catalyst_write(addr, val, dev->chip);
}

static void
catalyst_bios_map(catalyst_bios_t *dev, mem_mapping_t *mapping, uint32_t base)
{
    mem_mapping_add(mapping, base, 0x20000,
                    catalyst_bios_read, catalyst_bios_readw, catalyst_bios_readl,
                    catalyst_bios_write, catalyst_writew, catalyst_writel,
                    dev->chip->array, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM | MEM_MAPPING_ROMCS | MEM_MAPPING_ROM_WS, (void *) dev);
}

static void
catalyst_flash_bios_reset(void *priv)
{
    catalyst_bios_t *dev = (catalyst_bios_t *) priv;

    catalyst_flash_reset(dev->chip);
}

static void *
catalyst_flash_init(const device_t *info)
{
    catalyst_bios_t *dev = calloc(1, sizeof(catalyst_bios_t));
    flash_cfg_t      cfg = { 0 };

    cfg.nvr_name  = machine_get_nvr_name_ex(machine);
    cfg.image     = rom;
    cfg.image_len = biosmask + 1;

    mem_mapping_disable(&bios_mapping);
    mem_mapping_disable(&bios_high_mapping);

    dev->chip = catalyst_flash_create(info, &cfg);

    catalyst_bios_map(dev, &dev->mapping, 0xe0000);
    catalyst_bios_map(dev, &(dev->mapping_h[0]), 0xfffc0000);
    catalyst_bios_map(dev, &(dev->mapping_h[1]), 0xfffe0000);

    return dev;
}

static void
catalyst_flash_close(void *priv)
{
    catalyst_bios_t *dev = (catalyst_bios_t *) priv;

    catalyst_flash_destroy(dev->chip);

    free(dev);
}

const device_t catalyst_flash_device = {
    .name          = "Catalyst 28F010-D Flash BIOS",
    .internal_name = "catalyst_flash",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = catalyst_flash_init,
    .close         = catalyst_flash_close,
    .reset         = catalyst_flash_bios_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t amd_am28f010_flash_device = {
    .name          = "AMD Am28F010-D Flash BIOS",
    .internal_name = "amd_am28f010_flash",
    .flags         = DEVICE_PCI,
    .local         = 1,
    .init          = catalyst_flash_init,
    .close         = catalyst_flash_close,
    .reset         = catalyst_flash_bios_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
