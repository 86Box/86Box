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
 *          Nathan Lewis, <git@nrlewis.dev>
 *
 *          Copyright 2016-2020 Miran Grca.
 *          Copyright 2026      Nathan Lewis.
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
#include <86box/flash.h>

#define FLAG_WORD     1
#define FLAG_COBALT3K 2

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

/* The chip. */
struct e28f0xx_t {
    uint8_t  command;
    uint8_t  status;
    uint8_t  master_lock;
    uint8_t  flags;
    uint8_t  write_protect;
    uint8_t *array;

    uint8_t  block_locks[32];

    uint16_t flash_id;
    uint16_t dirty;

    uint32_t size;
    uint32_t mask;
    uint32_t program_addr;

    char nvr_path[1024];
};

/* The chip as the boot BIOS. */
typedef struct e28f0xx_bios_t {
    e28f0xx_t *chip;

    uint8_t cobalt3k;
    uint8_t ctrl; /* The Cobalt Qube 3's bank control. */

    mem_mapping_t mapping[2];
    mem_mapping_t mapping_h[64];
} e28f0xx_bios_t;

static uint8_t
e28f0xx_read(uint32_t addr, void *priv)
{
    const e28f0xx_t *dev = (e28f0xx_t *) priv;
    uint8_t          ret;

    addr &= dev->mask;

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
            break;
    }

    return ret;
}

static uint16_t
e28f0xx_readw(uint32_t addr, void *priv)
{
    const e28f0xx_t *dev = (e28f0xx_t *) priv;

    addr &= dev->mask;

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
e28f0xx_readl(uint32_t addr, void *priv)
{
    const e28f0xx_t *dev = (e28f0xx_t *) priv;

    addr &= dev->mask;

    const uint32_t *q = (uint32_t *) &(dev->array[addr]);

    return *q;
}

static void
e28f0xx_write(uint32_t addr, uint8_t val, void *priv)
{
    e28f0xx_t *dev = (e28f0xx_t *) priv;

    if (dev->write_protect)
        return;

    addr &= dev->mask;

    const uint32_t block_start = addr & 0xffff0000;

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
                    memset(&(dev->array[block_start]), 0xff, 0x00010000);
                    dev->dirty = 1;
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
                (addr == dev->program_addr)) {
                dev->array[addr] = val;
                dev->dirty = 1;
            }
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
e28f0xx_writew(uint32_t addr, uint16_t val, void *priv)
{
    e28f0xx_t *dev = (e28f0xx_t *) priv;

    if (dev->write_protect)
        return;

    addr &= dev->mask;

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
                        memset(&(dev->array[block_start]), 0xff, 0x00010000);
                        dev->dirty = 1;
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
                    (addr == dev->program_addr)) {
                    *(uint16_t *) (&dev->array[addr]) = val;
                    dev->dirty = 1;
                }
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
e28f0xx_writel(UNUSED(uint32_t addr), UNUSED(uint32_t val), UNUSED(void *priv))
{
    //
}

/*
   Where a window of the chip at base starts in the chip, and how the window
   repeats it if it is bigger than the chip.
 */
static void
e28f0xx_window(const e28f0xx_t *dev, uint32_t base, uint32_t size, uint8_t **exec, uint32_t *mask)
{
    const uint32_t offset = base & dev->mask;

    if (size > (dev->mask + 1)) {
        if (offset != 0)
            fatal("Intel Smart Flash: %08X bytes at %08X do not start on a %08X byte boundary\n",
                  size, base, dev->mask + 1);

        *exec = dev->array;
        *mask = dev->mask;
    } else {
        if ((offset + size) > (dev->mask + 1))
            fatal("Intel Smart Flash: %08X bytes at %08X are past the end of the %08X byte chip\n",
                  size, base, dev->mask + 1);

        *exec = dev->array + offset;
        *mask = 0xffffffff;
    }
}

/* Map a window of the chip at base, which may be bigger than the chip. */
void
e28f0xx_map(e28f0xx_t *dev, mem_mapping_t *mapping, uint32_t base, uint32_t size)
{
    uint8_t *exec;
    uint32_t mask;

    e28f0xx_window(dev, base, size, &exec, &mask);

    mem_mapping_add(mapping, base, size,
                    e28f0xx_read, e28f0xx_readw, e28f0xx_readl,
                    e28f0xx_write, e28f0xx_writew, e28f0xx_writel,
                    exec, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM | MEM_MAPPING_ROMCS | MEM_MAPPING_ROM_WS, (void *) dev);
    if (mask != 0xffffffff)
        mem_mapping_set_mask(mapping, mask);
}

/* Move a window added by e28f0xx_map() elsewhere, under the same rules. */
void
e28f0xx_remap(e28f0xx_t *dev, mem_mapping_t *mapping, uint32_t base, uint32_t size)
{
    uint8_t *exec;
    uint32_t mask;

    e28f0xx_window(dev, base, size, &exec, &mask);

    mem_mapping_set_addr(mapping, base, size);
    mem_mapping_set_exec(mapping, exec);
    mem_mapping_set_mask(mapping, mask);
}

void
e28f0xx_set_write_protect(e28f0xx_t *dev, int protect)
{
    dev->write_protect = !!protect;
}

uint32_t
e28f0xx_get_size(const e28f0xx_t *dev)
{
    return dev->size;
}

void
e28f0xx_reset(e28f0xx_t *dev)
{
    dev->command = CMD_READ_ARRAY;
    dev->status  = 0;
}

e28f0xx_t *
e28f0xx_create(const device_t *model, const flash_cfg_t *cfg)
{
    FILE      *fp;
    e28f0xx_t *dev = calloc(1, sizeof(e28f0xx_t));

    if (cfg->size == 0)
        fatal("Intel Smart Flash: the chip's size is required\n");

    dev->flags = model->local & 0xff;
    dev->size  = cfg->size;
    dev->mask  = cfg->size - 1;

    dev->array = (uint8_t *) calloc(1, dev->size);
    memset(dev->array, 0xff, dev->size);

    if (cfg->image != NULL)
        memcpy(dev->array, cfg->image, MIN(cfg->image_len, dev->size));

    switch (dev->mask) {
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

    e28f0xx_reset(dev);

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
e28f0xx_destroy(e28f0xx_t *dev)
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

/*
   The boot BIOS: the chip behind the BIOS chip select. On the Cobalt Qube 3,
   the board banks the chip: its bank control picks the 64k that appears, or,
   with bit 6 set, none of it.
 */
static int
e28f0xx_bios_addr(const e28f0xx_bios_t *dev, uint32_t *addr)
{
    if (dev->cobalt3k) {
        if (dev->ctrl & 0x40) /* bank 1 absent */
            return 0;
        *addr = ((dev->ctrl & 0x0f) << 16) | (*addr & 0x0000ffff);
    }

    return 1;
}

static uint8_t
e28f0xx_bios_read(uint32_t addr, void *priv)
{
    const e28f0xx_bios_t *dev = (e28f0xx_bios_t *) priv;

    if (!flash_bios_read_selected(addr))
        return 0xff;

    if (!e28f0xx_bios_addr(dev, &addr))
        return 0xff;

    return e28f0xx_read(addr, dev->chip);
}

static uint16_t
e28f0xx_bios_readw(uint32_t addr, void *priv)
{
    const e28f0xx_bios_t *dev = (e28f0xx_bios_t *) priv;

    if (!flash_bios_read_selected(addr))
        return 0xffff;

    if (!e28f0xx_bios_addr(dev, &addr))
        return 0xffff;

    return e28f0xx_readw(addr, dev->chip);
}

static uint32_t
e28f0xx_bios_readl(uint32_t addr, void *priv)
{
    const e28f0xx_bios_t *dev = (e28f0xx_bios_t *) priv;

    if (!flash_bios_read_selected(addr))
        return 0xffffffff;

    if (!e28f0xx_bios_addr(dev, &addr))
        return 0xffffffff;

    return e28f0xx_readl(addr, dev->chip);
}

static void
e28f0xx_bios_write(uint32_t addr, uint8_t val, void *priv)
{
    const e28f0xx_bios_t *dev = (e28f0xx_bios_t *) priv;

    if (!flash_bios_write_selected(addr))
        return;

    if (!e28f0xx_bios_addr(dev, &addr))
        return;

    e28f0xx_write(addr, val, dev->chip);
}

static void
e28f0xx_bios_writew(uint32_t addr, uint16_t val, void *priv)
{
    const e28f0xx_bios_t *dev = (e28f0xx_bios_t *) priv;

    if (!flash_bios_write_selected(addr))
        return;

    if (!e28f0xx_bios_addr(dev, &addr))
        return;

    e28f0xx_writew(addr, val, dev->chip);
}

static void
e28f0xx_bios_writel(uint32_t addr, uint32_t val, void *priv)
{
    const e28f0xx_bios_t *dev = (e28f0xx_bios_t *) priv;

    e28f0xx_writel(addr, val, dev->chip);
}

void
flash_e28f0xx_cobalt3k_update(uint8_t val)
{
    e28f0xx_bios_t *dev = (e28f0xx_bios_t *) device_get_priv(&intel_flash_e28f0xx_cobalt3k_device);
    if (!dev)
        return;

    dev->ctrl = val;

    uint8_t *exec = (dev->ctrl & 0x40) ? NULL : &dev->chip->array[(dev->ctrl & 0x0f) << 16];
    mem_mapping_set_exec(&(dev->mapping[0]), exec);
    mem_mapping_set_exec(&(dev->mapping_h[0]), exec);
}

static void
e28f0xx_bios_map(e28f0xx_bios_t *dev, mem_mapping_t *mapping, uint8_t *exec, uint32_t base)
{
    mem_mapping_add(mapping, base, 0x10000,
                    e28f0xx_bios_read, e28f0xx_bios_readw, e28f0xx_bios_readl,
                    e28f0xx_bios_write, e28f0xx_bios_writew, e28f0xx_bios_writel,
                    exec, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM | MEM_MAPPING_ROMCS | MEM_MAPPING_ROM_WS, (void *) dev);
}

static void
e28f0xx_add_mappings(e28f0xx_bios_t *dev)
{
    const uint32_t mask = dev->chip->mask;
    uint8_t        max  = 2;
    uint32_t       base;
    uint32_t       sub = 0x20000;

    if (dev->cobalt3k) {
        /* Bank 0 until the bank control is written. */
        e28f0xx_bios_map(dev, &(dev->mapping[0]), dev->chip->array, 0x000f0000);
        e28f0xx_bios_map(dev, &(dev->mapping_h[0]), dev->chip->array, 0xffff0000);
        return;
    }

    switch (mask) {
        default:
            fatal("Invalid BIOS mask for Intel E82F0xx flash: %08X!\n", mask);
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
        switch (mask) {
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

        uint8_t *exec = dev->chip->array + (base & mask);

        if ((max == 2) || (i >= (max - 2)))
            e28f0xx_bios_map(dev, &(dev->mapping[i & 1]), exec, base);
        e28f0xx_bios_map(dev, &(dev->mapping_h[i]), exec, (base | 0xfff00000) - sub);
        e28f0xx_bios_map(dev, &(dev->mapping_h[i + max]), exec, base | 0xfff00000);
    }
}

static void
e28f0xx_bios_reset(void *priv)
{
    e28f0xx_bios_t *dev = (e28f0xx_bios_t *) priv;

    e28f0xx_reset(dev->chip);
}

static void *
e28f0xx_init(const device_t *info)
{
    e28f0xx_bios_t *dev = calloc(1, sizeof(e28f0xx_bios_t));
    flash_cfg_t     cfg = { 0 };

    cfg.nvr_name  = machine_get_nvr_name_ex(machine);
    cfg.image     = rom;
    cfg.image_len = biosmask + 1;
    cfg.size      = biosmask + 1;

    dev->cobalt3k = !!(info->local & FLAG_COBALT3K);

    mem_mapping_disable(&bios_mapping);
    mem_mapping_disable(&bios_high_mapping);

    dev->chip = e28f0xx_create(info, &cfg);

    e28f0xx_add_mappings(dev);

    return dev;
}

static void
e28f0xx_close(void *priv)
{
    e28f0xx_bios_t *dev = (e28f0xx_bios_t *) priv;

    e28f0xx_destroy(dev->chip);

    free(dev);
}

const device_t intel_flash_e28f0xx_device = {
    .name          = "Intel E82F0xx Flash BIOS",
    .internal_name = "intel_flash_e28f0xx",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = e28f0xx_init,
    .close         = e28f0xx_close,
    .reset         = e28f0xx_bios_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t intel_flash_e28f0xx_cobalt3k_device = {
    .name          = "Intel E28F008S5 Flash BIOS (Cobalt Qube 3)",
    .internal_name = "intel_flash_e28f0xx_cobalt3k",
    .flags         = DEVICE_PCI,
    .local         = FLAG_COBALT3K,
    .init          = e28f0xx_init,
    .close         = e28f0xx_close,
    .reset         = e28f0xx_bios_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
