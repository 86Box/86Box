/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the AMD Am29F016D flash chip.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Nathan Lewis, <git@nrlewis.dev>
 *
 *          Copyright 2026 Miran Grca.
 *          Copyright 2026 Nathan Lewis.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/machine.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/plat.h>
#include <86box/flash.h>

#define AM29F016D_SIZE 0x00200000

/* The chip. */
struct am29f016d_t {
    uint8_t cycle;
    uint8_t state;
    uint8_t unlock_bypass;
    uint8_t dirty;
    uint8_t write_protect;

    uint32_t size;

    uint8_t array[AM29F016D_SIZE];

    char nvr_path[1024];
};

/* The chip as the boot BIOS. */
typedef struct am29f016d_bios_t {
    am29f016d_t *chip;

    uint8_t ctrl;
    uint8_t legacy;

    mem_mapping_t low_mapping;
    mem_mapping_t high_mapping;
} am29f016d_bios_t;

#define AMD_RESET                 0xf0
#define AMD_NEXT_CYCLE_HIGH       0xaa
#define AMD_NEXT_CYCLE_LOW        0x55
#define AMD_NEXT_CYCLE_THREE      0x80
#define AMD_AUTOSELECT            0x90
#define AMD_CFI_QUERY             0x98
#define AMD_PROGRAM               0x50
#define AMD_UNLOCK_BYPASS         0x20
#define AMD_UNLOCK_BYPASS_PROGRAM 0xa0
#define AMD_UNLOCK_BYPASS_RESET   0x90
#define AMD_UNLOCK_BYPASS_END     0x00
#define AMD_ERASE                 0x80
#define AMD_CHIP_ERASE            0x10
#define AMD_SECTOR_ERASE          0x30
#define AMD_ERASE_SUSPEND         0xb0
#define AMD_ERASE_RESUME          0x30

enum {
    AMD_STATE_READ_ARRAY    = 0,
    AMD_STATE_AUTOSELECT,
    AMD_STATE_CFI_QUERY,
    AMD_STATE_PROGRAM,
    AMD_STATE_READ_STATUS
};

static uint8_t
am29f016d_read(uint32_t addr, void *priv)
{
    const am29f016d_t *dev  = (const am29f016d_t *) priv;
    uint8_t            ret;
    const uint32_t     calc = addr & (AM29F016D_SIZE - 1);

    switch (dev->state) {
        default:
            ret = 0xff;
            break;
        case AMD_STATE_READ_ARRAY:
            ret = dev->array[calc];
            break;
        case AMD_STATE_AUTOSELECT:
            switch (calc & 0x000000ff) {
                default:
                    ret = 0xff;
                    break;
                case 0x00000000:
                    ret = 0x01;
                    break;
                case 0x00000001:
                    ret = 0xad;
                    break;
                case 0x00000002:
                    /* We Always return unprotected. */
                    ret = 0x00;
                    break;
            }
            break;
        case AMD_STATE_CFI_QUERY:
            switch (calc & 0x000000ff) {
                default:
                    ret = 0xff;
                    break;
                case 0x00000010:
                    ret = 0x51;
                    break;
                case 0x00000011:
                case 0x00000041:
                    ret = 0x52;
                    break;
                case 0x00000012:
                    ret = 0x59;
                    break;
                case 0x00000013:
                    ret = 0x02;
                    break;
                case 0x00000015:
                    ret = 0x40;
                    break;
                case 0x00000014:
                case 0x00000016 ... 0x0000001a:
                case 0x0000001d ... 0x0000001e:
                case 0x00000020:
                case 0x00000022:
                case 0x00000024:
                case 0x00000026:
                case 0x00000045:
                case 0x00000028 ... 0x0000002b:
                case 0x0000002e ... 0x0000002f:
                case 0x0000004a ... 0x0000004f:
                    ret = 0x00;
                    break;
                case 0x0000001b:
                    ret = 0x45;
                    break;
                case 0x0000001c:
                    ret = 0x55;
                    break;
                case 0x0000001f:
                    ret = 0x03;
                    break;
                case 0x00000021:
                    ret = 0x0a;
                    break;
                case 0x00000023:
                    ret = 0x05;
                    break;
                case 0x00000025:
                    ret = 0x04;
                    break;
                case 0x00000027:
                    ret = 0x15;
                    break;
                case 0x0000002c:
                case 0x00000030:
                case 0x00000048:
                    ret = 0x01;
                    break;
                case 0x0000002d:
                    ret = 0x1f;
                    break;
                case 0x00000040:
                    ret = 0x50;
                    break;
                case 0x00000042:
                    ret = 0x49;
                    break;
                case 0x00000043:
                case 0x00000044:
                    ret = 0x31;
                    break;
                case 0x00000046:
                    ret = 0x02;
                    break;
                case 0x00000047:
                case 0x00000049:
                    ret = 0x04;
                    break;
            }
            break;
    }

    return ret;
}

static uint16_t
am29f016d_readw(uint32_t addr, void *priv)
{
    const am29f016d_t *dev  = (const am29f016d_t *) priv;
    uint16_t           ret;
    const uint32_t     calc = addr & (AM29F016D_SIZE - 1);

    switch (dev->state) {
        default:
            ret = 0xffff;
            break;
        case AMD_STATE_READ_ARRAY:
            ret = *(uint16_t *) &dev->array[calc];
            break;
        case AMD_STATE_AUTOSELECT:
        case AMD_STATE_CFI_QUERY:
        case AMD_STATE_READ_STATUS:
            ret = am29f016d_read(addr, priv) |
                  (am29f016d_read(addr + 1, priv) << 8);
            break;
    }

    return ret;
}

static uint32_t
am29f016d_readl(uint32_t addr, void *priv)
{
    const am29f016d_t *dev  = (const am29f016d_t *) priv;
    uint32_t           ret;
    const uint32_t     calc = addr & (AM29F016D_SIZE - 1);

    switch (dev->state) {
        default:
            ret = 0xffffffff;
            break;
        case AMD_STATE_READ_ARRAY:
            ret = *(uint32_t *) &dev->array[calc];
            break;
        case AMD_STATE_AUTOSELECT:
        case AMD_STATE_CFI_QUERY:
        case AMD_STATE_READ_STATUS:
            ret = am29f016d_readw(addr, priv) |
                  (am29f016d_readw(addr + 2, priv) << 16);
            break;
    }

    return ret;
}

static void
am29f016d_write(uint32_t addr, uint8_t val, void *priv)
{
    am29f016d_t   *dev  = (am29f016d_t *) priv;
    const uint32_t calc = addr & (AM29F016D_SIZE - 1);

    if (dev->write_protect)
        return;

    switch (dev->cycle) {
        default:
            break;
        case 0:
            if (dev->state == AMD_STATE_PROGRAM) {
                if (dev->array[calc] != val)
                    dev->dirty = 1;
                dev->array[calc] = val;
                dev->state       = AMD_STATE_READ_ARRAY;
            } else  switch (val) {
                default:
                    break;
                case AMD_RESET:
                    switch (dev->state) {
                        default:
                            break;
                        case AMD_STATE_AUTOSELECT:
                        case AMD_STATE_CFI_QUERY:
                            /* Reset. */
                            dev->state = AMD_STATE_READ_ARRAY;
                            break;
                    }
                    break;
                case AMD_NEXT_CYCLE_HIGH:
                    if ((calc & 0x000007ff) == 0x00000555)
                        dev->cycle++;
                    break;
                case AMD_CFI_QUERY:
                    switch (dev->state) {
                        default:
                            break;
                        case AMD_STATE_READ_ARRAY:
                        case AMD_STATE_AUTOSELECT:
                            /* CFI Query. */
                            dev->state = AMD_STATE_CFI_QUERY;
                            break;
                    }
                    break;
                case AMD_UNLOCK_BYPASS_PROGRAM:
                    if (dev->unlock_bypass)
                        dev->state = AMD_STATE_PROGRAM;
                    break;
                case AMD_UNLOCK_BYPASS_RESET:
                    if (dev->unlock_bypass)
                        dev->cycle++;
                    break;
            }
            break;
        case 1:
            switch (val) {
                case AMD_UNLOCK_BYPASS_END:
                    if (dev->unlock_bypass)
                        dev->unlock_bypass = 0;
                    fallthrough;
                default:
                    dev->cycle = 0;
                    break;
                case AMD_NEXT_CYCLE_LOW:
                    if ((calc & 0x000007ff) == 0x000002aa)
                        dev->cycle++;
                    break;
            }
            break;
        case 2:
            switch (val) {
                case AMD_AUTOSELECT:
                    dev->state = AMD_STATE_AUTOSELECT;
                    fallthrough;
                default:
                    dev->cycle = 0;
                    break;
                case AMD_PROGRAM:
                    dev->state = AMD_STATE_PROGRAM;
                    dev->cycle = 0;
                    break;
                case AMD_UNLOCK_BYPASS:
                    dev->unlock_bypass = 1;
                    dev->cycle = 0;
                    break;
                case AMD_NEXT_CYCLE_THREE:
                    if ((calc & 0x000007ff) == 0x00000555)
                        dev->cycle++;
                    break;
            }
            break;
        case 3:
            if ((val == AMD_NEXT_CYCLE_HIGH) &&
                ((calc & 0x000007ff) == 0x00000555))
                dev->cycle++;
            else
                dev->cycle = 0;
            break;
        case 4:
            if ((val == AMD_NEXT_CYCLE_LOW) &&
                ((calc & 0x000007ff) == 0x000002aa))
                dev->cycle++;
            else
                dev->cycle = 0;
            break;
        case 5:
            switch (val) {
                default:
                    break;
                case AMD_CHIP_ERASE:
                    if ((calc & 0x000007ff) == 0x00000555) {
                        memset(dev->array, 0xff, 0x00200000);
                        dev->dirty = 1;
                    }
                    break;
                case AMD_SECTOR_ERASE:
                    memset(&(dev->array[calc & 0x001c0000]), 0xff, 0x00040000);
                    dev->dirty = 1;
                    break;
            }
            dev->cycle = 0;
            break;
    }
}

static void
am29f016d_writew(uint32_t addr, uint16_t val, void *priv)
{
    am29f016d_t   *dev  = (am29f016d_t *) priv;
    const uint32_t calc = addr & (AM29F016D_SIZE - 1);

    if (dev->write_protect)
        return;

    if ((dev->cycle == 0) && (dev->state == AMD_STATE_PROGRAM)) {
        *(uint16_t *) &(dev->array[calc]) = val;
        dev->state = AMD_STATE_READ_ARRAY;
    } else {
        am29f016d_write(addr, val & 0xff, priv);
        am29f016d_write(addr + 1, val >> 8, priv);
    }
}

static void
am29f016d_writel(uint32_t addr, uint32_t val, void *priv)
{
    am29f016d_t   *dev  = (am29f016d_t *) priv;
    const uint32_t calc = addr & (AM29F016D_SIZE - 1);

    if (dev->write_protect)
        return;

    if ((dev->cycle == 0) && (dev->state == AMD_STATE_PROGRAM)) {
        *(uint32_t *) &(dev->array[calc]) = val;
        dev->state = AMD_STATE_READ_ARRAY;
    } else {
        am29f016d_writew(addr, val & 0xffff, priv);
        am29f016d_writew(addr + 2, val >> 16, priv);
    }
}

/*
   Where a window of the chip at base starts in the chip, and how the window
   repeats it if it is bigger than the chip.
 */
static void
am29f016d_window(const am29f016d_t *dev, uint32_t base, uint32_t size, uint8_t **exec, uint32_t *mask)
{
    const uint32_t chip_mask = dev->size - 1;
    const uint32_t offset    = base & chip_mask;

    if (size > dev->size) {
        if (offset != 0)
            fatal("AMD Am29F016D: %08X bytes at %08X do not start on a %08X byte boundary\n",
                  size, base, dev->size);

        *exec = (uint8_t *) dev->array;
        *mask = chip_mask;
    } else {
        if ((offset + size) > dev->size)
            fatal("AMD Am29F016D: %08X bytes at %08X are past the end of the %08X byte chip\n",
                  size, base, dev->size);

        *exec = (uint8_t *) dev->array + offset;
        *mask = 0xffffffff;
    }
}

/* Map a window of the chip at base, which may be bigger than the chip. */
void
am29f016d_map(am29f016d_t *dev, mem_mapping_t *mapping, uint32_t base, uint32_t size)
{
    uint8_t *exec;
    uint32_t mask;

    am29f016d_window(dev, base, size, &exec, &mask);

    mem_mapping_add(mapping, base, size,
                    am29f016d_read, am29f016d_readw, am29f016d_readl,
                    am29f016d_write, am29f016d_writew, am29f016d_writel,
                    exec, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM | MEM_MAPPING_ROMCS | MEM_MAPPING_ROM_WS, (void *) dev);
    if (mask != 0xffffffff)
        mem_mapping_set_mask(mapping, mask);
}

/* Move a window added by am29f016d_map() elsewhere, under the same rules. */
void
am29f016d_remap(am29f016d_t *dev, mem_mapping_t *mapping, uint32_t base, uint32_t size)
{
    uint8_t *exec;
    uint32_t mask;

    am29f016d_window(dev, base, size, &exec, &mask);

    mem_mapping_set_addr(mapping, base, size);
    mem_mapping_set_exec(mapping, exec);
    mem_mapping_set_mask(mapping, mask);
}

void
am29f016d_set_write_protect(am29f016d_t *dev, int protect)
{
    dev->write_protect = !!protect;
}

uint32_t
am29f016d_get_size(const am29f016d_t *dev)
{
    return dev->size;
}

void
am29f016d_reset(am29f016d_t *dev)
{
    dev->state = AMD_STATE_READ_ARRAY;
    dev->cycle = 0;
}

am29f016d_t *
am29f016d_create(UNUSED(const device_t *model), const flash_cfg_t *cfg)
{
    am29f016d_t *dev = calloc(1, sizeof(am29f016d_t));

    dev->size = AM29F016D_SIZE;

    memset(dev->array, 0xff, dev->size);

    if (cfg->image != NULL)
        memcpy(dev->array, cfg->image, MIN(cfg->image_len, dev->size));

    if (cfg->nvr_name != NULL) {
        if (strlen(cfg->nvr_name) == 0)
            fatal("Attempting to open the Flash file for reading with an empty invalid name\n");

        snprintf(dev->nvr_path, sizeof(dev->nvr_path), "%s.bin", cfg->nvr_name);

        FILE *fp = nvr_fopen(dev->nvr_path, "rb");
        if (fp != NULL) {
            if (!dump_missing)
                (void) !fread(&(dev->array[0x00000]), 1, dev->size, fp);
            fclose(fp);
        } else if (!dump_missing)
            dev->dirty = 1; /* It is by definition dirty on creation. */
    }

    return dev;
}

void
am29f016d_destroy(am29f016d_t *dev)
{
    if (dev->dirty && (dev->nvr_path[0] != '\0')) {
        FILE *fp = nvr_fopen(dev->nvr_path, "wb");
        if (fp != NULL) {
            if (!dump_missing)
                fwrite(&(dev->array[0x00000]), dev->size, 1, fp);
            fclose(fp);
        } else if (!dump_missing)
            warning("Unable to open %s for writing, please make sure your NVR folder is writable\n", dev->nvr_path);
    }

    free(dev);
}

/*
   The boot BIOS: the chip behind the BIOS chip select, with the BIOS at its
   top. The low mapping reaches the chip's upper megabyte. On the ALi M6117
   boards (legacy), the board banks the chip instead: I/O port 0x50 bit 1
   picks which of the two 128k at the top of it appears, low and high.
 */
static uint32_t
am29f016d_bios_addr(const am29f016d_bios_t *dev, uint32_t addr)
{
    uint32_t ret = addr &= 0x001fffff;

    if (dev->legacy)
        ret = ((dev->ctrl & 0x02) ? 0x001e0000 : 0x001c0000) | (addr & 0x0001ffff);
    else {
        /*
           If we are accessing the low mapping, make explicit
           that we are accessing the upper megabyte.
         */
        if (addr <= 0x00100000)
            ret |= 0x00100000;
    }

    return ret;
}

/* The board decides where each byte goes, so wider accesses that are not plain reads or writes go a byte at a time. */
static uint8_t
am29f016d_bios_read(uint32_t addr, void *priv)
{
    const am29f016d_bios_t *dev = (const am29f016d_bios_t *) priv;

    if (!flash_bios_read_selected(addr))
        return 0xff;

    return am29f016d_read(am29f016d_bios_addr(dev, addr), dev->chip);
}

static uint16_t
am29f016d_bios_readw(uint32_t addr, void *priv)
{
    const am29f016d_bios_t *dev = (const am29f016d_bios_t *) priv;
    uint16_t                ret;

    if (!flash_bios_read_selected(addr))
        return 0xffff;

    switch (dev->chip->state) {
        default:
            ret = 0xffff;
            break;
        case AMD_STATE_READ_ARRAY:
            ret = *(uint16_t *) &dev->chip->array[am29f016d_bios_addr(dev, addr)];
            break;
        case AMD_STATE_AUTOSELECT:
        case AMD_STATE_CFI_QUERY:
        case AMD_STATE_READ_STATUS:
            ret = am29f016d_bios_read(addr, priv) | (am29f016d_bios_read(addr + 1, priv) << 8);
            break;
    }

    return ret;
}

static uint32_t
am29f016d_bios_readl(uint32_t addr, void *priv)
{
    const am29f016d_bios_t *dev = (const am29f016d_bios_t *) priv;
    uint32_t                ret;

    if (!flash_bios_read_selected(addr))
        return 0xffffffff;

    switch (dev->chip->state) {
        default:
            ret = 0xffffffff;
            break;
        case AMD_STATE_READ_ARRAY:
            ret = *(uint32_t *) &dev->chip->array[am29f016d_bios_addr(dev, addr)];
            break;
        case AMD_STATE_AUTOSELECT:
        case AMD_STATE_CFI_QUERY:
        case AMD_STATE_READ_STATUS:
            ret = am29f016d_bios_readw(addr, priv) | (am29f016d_bios_readw(addr + 2, priv) << 16);
            break;
    }

    return ret;
}

static void
am29f016d_bios_write(uint32_t addr, uint8_t val, void *priv)
{
    const am29f016d_bios_t *dev = (am29f016d_bios_t *) priv;

    if (!flash_bios_write_selected(addr))
        return;

    am29f016d_write(am29f016d_bios_addr(dev, addr), val, dev->chip);
}

static void
am29f016d_bios_writew(uint32_t addr, uint16_t val, void *priv)
{
    const am29f016d_bios_t *dev = (am29f016d_bios_t *) priv;

    if (!flash_bios_write_selected(addr))
        return;

    if ((dev->chip->cycle == 0) && (dev->chip->state == AMD_STATE_PROGRAM))
        am29f016d_writew(am29f016d_bios_addr(dev, addr), val, dev->chip);
    else {
        am29f016d_bios_write(addr, val & 0xff, priv);
        am29f016d_bios_write(addr + 1, val >> 8, priv);
    }
}

static void
am29f016d_bios_writel(uint32_t addr, uint32_t val, void *priv)
{
    const am29f016d_bios_t *dev = (am29f016d_bios_t *) priv;

    if (!flash_bios_write_selected(addr))
        return;

    if ((dev->chip->cycle == 0) && (dev->chip->state == AMD_STATE_PROGRAM))
        am29f016d_writel(am29f016d_bios_addr(dev, addr), val, dev->chip);
    else {
        am29f016d_bios_writew(addr, val & 0xffff, priv);
        am29f016d_bios_writew(addr + 2, val >> 16, priv);
    }
}

static uint8_t
am29f16d_ctrl_in(UNUSED(uint16_t port), void *priv)
{
    const am29f016d_bios_t *dev = (am29f016d_bios_t *) priv;
    const uint8_t           ret = dev->ctrl;

    return ret;
}

static void
am29f16d_ctrl_out(UNUSED(uint16_t port), uint8_t val, void *priv)
{
    am29f016d_bios_t *dev   = (am29f016d_bios_t *) priv;
    uint32_t          fbase = 0x00200000 - (biosmask + 1);

    dev->ctrl = val;

    if (val & 0x02)
        fbase += 0x00020000;

    mem_mapping_set_exec(&dev->low_mapping, &dev->chip->array[fbase]);
    mem_mapping_set_exec(&dev->high_mapping, &dev->chip->array[fbase]);

    flushmmucache_nopc();
}

static void
am29f016d_add_mappings(am29f016d_bios_t *dev)
{
    const uint32_t fbase     = 0x00200000 - (biosmask + 1);
    uint32_t       high_base = cpu_16bitbus ? 0x00e00000 : 0xffe00000;
    const uint32_t high_size = dev->legacy ? 0x00020000 : 0x00200000;
    uint8_t       *priv      = dev->legacy ? (dev->chip->array + fbase) : dev->chip->array;

    if (dev->legacy) {
        high_base = cpu_16bitbus ? 0x00fe0000 : 0xfffe0000;

        io_sethandler(0x0050, 0x0001,
                      am29f16d_ctrl_in, NULL, NULL,
                      am29f16d_ctrl_out, NULL, NULL,
                      dev);
    }

    if (cpu_16bitbus) {
        mem_mapping_add(&(dev->low_mapping), 0x000e0000, 0x00020000,
                        am29f016d_bios_read, am29f016d_bios_readw, NULL,
                        am29f016d_bios_write, am29f016d_bios_writew, NULL,
                        dev->chip->array + fbase,
                        MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM |
                        MEM_MAPPING_ROMCS | MEM_MAPPING_ROM_WS,
                        (void *) dev);

        mem_mapping_add(&(dev->high_mapping), high_base, high_size,
                        am29f016d_bios_read, am29f016d_bios_readw, NULL,
                        am29f016d_bios_write, am29f016d_bios_writew, NULL,
                        priv,
                        MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM |
                        MEM_MAPPING_ROMCS | MEM_MAPPING_ROM_WS,
                        (void *) dev);
    } else {
        mem_mapping_add(&(dev->low_mapping), 0x000e0000, 0x00020000,
                        am29f016d_bios_read, am29f016d_bios_readw, am29f016d_bios_readl,
                        am29f016d_bios_write, am29f016d_bios_writew, am29f016d_bios_writel,
                        dev->chip->array + fbase,
                        MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM |
                        MEM_MAPPING_ROMCS | MEM_MAPPING_ROM_WS,
                        (void *) dev);

        mem_mapping_add(&(dev->high_mapping), high_base, high_size,
                        am29f016d_bios_read, am29f016d_bios_readw, am29f016d_bios_readl,
                        am29f016d_bios_write, am29f016d_bios_writew, am29f016d_bios_writel,
                        priv,
                        MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM |
                        MEM_MAPPING_ROMCS | MEM_MAPPING_ROM_WS,
                        (void *) dev);
    }
}

static void
am29f016d_bios_reset(void *priv)
{
    am29f016d_bios_t *dev = (am29f016d_bios_t *) priv;

    am29f016d_reset(dev->chip);

    if (dev->legacy)
        dev->ctrl = 0;
}

static void *
am29f016d_init(const device_t *info)
{
    am29f016d_bios_t *dev   = calloc(1, sizeof(am29f016d_bios_t));
    flash_cfg_t       cfg   = { 0 };
    const uint32_t    fbase = 0x00200000 - (biosmask + 1);
    uint8_t          *image = malloc(AM29F016D_SIZE);

    /* The BIOS goes at the top of the chip. */
    memset(image, 0xff, AM29F016D_SIZE);
    memcpy(&image[fbase], rom, biosmask + 1);

    cfg.nvr_name  = machine_get_nvr_name_ex(machine);
    cfg.image     = image;
    cfg.image_len = AM29F016D_SIZE;

    mem_mapping_disable(&bios_mapping);
    mem_mapping_disable(&bios_high_mapping);

    dev->legacy = (info->local & AMD_FLAG_LEGACY);

    dev->chip = am29f016d_create(info, &cfg);
    free(image);

    am29f016d_add_mappings(dev);

    return dev;
}

static void
am29f016d_close(void *priv)
{
    am29f016d_bios_t *dev = (am29f016d_bios_t *) priv;

    am29f016d_destroy(dev->chip);

    free(dev);
}

const device_t amd_flash_am29f016d_device = {
    .name          = "AMD Am29F016D Flash BIOS",
    .internal_name = "amd_flash_29f016d",
    .flags         = 0,
    .local         = 0,
    .init          = am29f016d_init,
    .close         = am29f016d_close,
    .reset         = am29f016d_bios_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
