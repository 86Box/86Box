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
 *
 *          Copyright 2026 Miran Grca.
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

typedef struct am29f016d_t {
    uint8_t       cycle;
    uint8_t       state;
    uint8_t       ctrl;
    uint8_t       legacy;
    uint8_t       unlock_bypass;
    uint8_t       dirty;

    uint32_t      size;
    uint32_t      mask;

    uint8_t       array[0x00200000];

    char          flash_path[1024];

    mem_mapping_t low_mapping;
    mem_mapping_t high_mapping;
} am29f016d_t;

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

static uint32_t
am29f016d_calc_addr(const am29f016d_t *dev, uint32_t addr)
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

int log_suppr = 0;

static uint8_t
am29f016d_read(uint32_t addr, void *priv)
{
    const am29f016d_t *dev  = (const am29f016d_t *) priv;
    uint8_t            ret;
    const uint32_t     calc = am29f016d_calc_addr(dev, addr);

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

    if (!log_suppr)
        pclog("[R08] %08X (%08X) = %02X\n", addr, calc, ret);

    return ret;
}

static uint16_t
am29f016d_readw(uint32_t addr, void *priv)
{
    const am29f016d_t *dev  = (const am29f016d_t *) priv;
    uint16_t           ret;
    const uint32_t     calc = am29f016d_calc_addr(dev, addr);

    switch (dev->state) {
        default:
            ret = 0xffff;
            pclog("[X16] %08X (%08X) = FFFF\n", addr, calc);
            break;
        case AMD_STATE_READ_ARRAY:
            ret = *(uint16_t *) &dev->array[calc];
            pclog("[R16] %08X (%08X) = %04X\n", addr, calc, ret);
            break;
        case AMD_STATE_AUTOSELECT:
        case AMD_STATE_CFI_QUERY:
        case AMD_STATE_READ_STATUS:
            log_suppr = 0;
            ret = am29f016d_read(addr, priv) |
                  (am29f016d_read(addr + 1, priv) << 8);
            log_suppr = 1;
            pclog("[r16] %08X (%08X) = %04X\n", addr, calc, ret);
            break;
    }

    return ret;
}

static uint32_t
am29f016d_readl(uint32_t addr, void *priv)
{
    const am29f016d_t *dev  = (const am29f016d_t *) priv;
    uint32_t           ret;
    const uint32_t     calc = am29f016d_calc_addr(dev, addr);

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
    am29f016d_t *  dev  = (am29f016d_t *) priv;
    const uint32_t calc = am29f016d_calc_addr(dev, addr);

    if (!log_suppr)
        pclog("[W08] %08X (%08X) = %02X\n", addr, calc, val);

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
    am29f016d_t *  dev  = (am29f016d_t *) priv;
    const uint32_t calc = am29f016d_calc_addr(dev, addr);

    if ((dev->cycle == 0) && (dev->state == AMD_STATE_PROGRAM)) {
        pclog("[W16] %08X (%08X) = %04X\n", addr, calc, val);
        *(uint16_t *) &(dev->array[calc]) = val;
        dev->state = AMD_STATE_READ_ARRAY;
    } else {
        pclog("[w16] %08X (%08X) = %04X\n", addr, calc, val);
        log_suppr = 1;
        am29f016d_write(addr, val & 0xff, priv);
        am29f016d_write(addr + 1, val >> 8, priv);
        log_suppr = 0;
    }
}

static void
am29f016d_writel(uint32_t addr, uint32_t val, void *priv)
{
    am29f016d_t *  dev  = (am29f016d_t *) priv;
    const uint32_t calc = am29f016d_calc_addr(dev, addr);

    if ((dev->cycle == 0) && (dev->state == AMD_STATE_PROGRAM)) {
        *(uint32_t *) &(dev->array[calc]) = val;
        dev->state = AMD_STATE_READ_ARRAY;
    } else {
        am29f016d_writew(addr, val & 0xffff, priv);
        am29f016d_writew(addr + 2, val >> 16, priv);
    }
}

static uint8_t
am29f16d_ctrl_in(uint16_t port, void *priv)
{
    const am29f016d_t *dev = (const am29f016d_t *) priv;
    const uint8_t      ret = dev->ctrl;

    return ret;
}

static void
am29f16d_ctrl_out(uint16_t port, uint8_t val, void *priv)
{
    am29f016d_t *dev = (am29f016d_t *) priv;
    uint32_t fbase   = 0x00200000 - (biosmask + 1);

    dev->ctrl = val;

    if (val & 0x02)
        fbase += 0x00020000;

    mem_mapping_set_exec(&dev->low_mapping,  &dev->array[fbase]);
    mem_mapping_set_exec(&dev->high_mapping, &dev->array[fbase]);

    flushmmucache_nopc();
}

static void
am29f016d_add_mappings(am29f016d_t *dev)
{
    const uint32_t fbase     = 0x00200000 - (biosmask + 1);
    uint32_t       high_base = cpu_16bitbus ? (is6117 ? 0x03e00000 : 0x00e00000) : 0xffe00000;
    const uint32_t high_size = dev->legacy ? 0x00020000 : 0x00200000;
    void *         priv     = dev->legacy ? (dev->array + fbase) : dev->array;

    memcpy(&dev->array[fbase], rom, biosmask + 1);

    if (dev->legacy) {
        high_base = cpu_16bitbus ? (is6117 ? 0x03fe0000 : 0x00fe0000) : 0xfffe0000;

        io_sethandler(0x0050, 0x0001,
                      am29f16d_ctrl_in,  NULL, NULL,
                      am29f16d_ctrl_out, NULL, NULL,
                      dev);
    }

    if (cpu_16bitbus) {
        mem_mapping_add(&(dev->low_mapping), 0x000e0000, 0x00020000,
                        am29f016d_read, am29f016d_readw, NULL,
                        am29f016d_write, am29f016d_writew, NULL,
                        dev->array + fbase,
                        MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM |
                        MEM_MAPPING_ROMCS | MEM_MAPPING_ROM_WS,
                        (void *) dev);

        mem_mapping_add(&(dev->high_mapping), high_base, high_size,
                        am29f016d_read, am29f016d_readw, NULL,
                        am29f016d_write, am29f016d_writew, NULL,
                        priv,
                        MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM |
                        MEM_MAPPING_ROMCS | MEM_MAPPING_ROM_WS,
                        (void *) dev);
    } else {
        mem_mapping_add(&(dev->low_mapping), 0x000e0000, 0x00020000,
                        am29f016d_read, am29f016d_readw, am29f016d_readl,
                        am29f016d_write, am29f016d_writew, am29f016d_writel,
                        dev->array + fbase,
                        MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM |
                        MEM_MAPPING_ROMCS | MEM_MAPPING_ROM_WS,
                        (void *) dev);

        mem_mapping_add(&(dev->high_mapping), high_base, high_size,
                        am29f016d_read, am29f016d_readw, am29f016d_readl,
                        am29f016d_write, am29f016d_writew, am29f016d_writel,
                        priv,
                        MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM |
                        MEM_MAPPING_ROMCS | MEM_MAPPING_ROM_WS,
                        (void *) dev);
    }
}

static void
am29f016d_reset(void *priv)
{
    am29f016d_t *dev = (am29f016d_t *) priv;

    dev->state = AMD_STATE_READ_ARRAY;
    dev->cycle = 0;

    if (dev->legacy)
        dev->ctrl = 0;
}

static void *
am29f016d_init(const device_t *info)
{
    am29f016d_t *dev = calloc(1, sizeof(am29f016d_t));

    sprintf(dev->flash_path, "%s.bin", machine_get_nvr_name_ex(machine));

    mem_mapping_disable(&bios_mapping);
    mem_mapping_disable(&bios_high_mapping);

    memset(dev->array, 0xff, 0x00200000);

    dev->legacy = (info->local & AMD_FLAG_LEGACY);

    am29f016d_add_mappings(dev);

    FILE *fp = nvr_fopen(dev->flash_path, "rb");
    if (fp) {
        if (fread(&(dev->array[0x00000]), 1, 0x00200000, fp) != 0x00200000)
            pclog("Less than %i bytes read from the Am29F016D Flash ROM file\n", dev->size);
        fclose(fp);
    } else
        dev->dirty = 1; /* It is by definition dirty on creation. */

    return dev;
}

static void
am29f016d_close(void *priv)
{
    am29f016d_t *dev = (am29f016d_t *) priv;

    if (dev->dirty) {
        FILE *fp = nvr_fopen(dev->flash_path, "wb");
        if (fp != NULL) {
            fwrite(&(dev->array[0x00000]), 0x00200000, 1, fp);
            fclose(fp);
        }
    }

    free(dev);
}

const device_t amd_flash_am29f016d_device = {
    .name          = "AMD Am29F016D Flash BIOS",
    .internal_name = "amd_flash_29f016d",
    .flags         = 0,
    .local         = 0,
    .init          = am29f016d_init,
    .close         = am29f016d_close,
    .reset         = am29f016d_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
