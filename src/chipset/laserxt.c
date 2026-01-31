/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the VTech LaserXT chipset.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2008-2025 Sarah Walker.
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2017-2025 Fred N. van Kempen.
 *          Copyright 2025      Jasmine Iwanek.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/nmi.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/rom.h>
#include <86box/machine.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/gameport.h>
#include <86box/keyboard.h>
#include <86box/plat_unused.h>

#define EMS_TOTAL_MAX 0x00100000

typedef struct
{
    uint8_t          page;
    uint8_t          ctrl;

    uint32_t         phys;
    uint32_t         virt;

    mem_mapping_t    mapping;

    uint8_t         *ram;

    void            *parent;
} lxt_ems_t;

typedef struct
{
    int              ems_base_idx;

    lxt_ems_t        ems[4];

    uint16_t         io_base;
    uint32_t         base;

    uint32_t         mem_size;

    uint8_t         *ram;

    void            *parent;
} lxt_ems_board_t;

typedef struct
{
    int              is_lxt3;

    lxt_ems_board_t *ems_boards[2];
} lxt_t;

static void
ems_update_virt(lxt_ems_t *dev, uint8_t new_page)
{
    lxt_ems_board_t *board = (lxt_ems_board_t *) dev->parent;
    lxt_t           *lxt   = (lxt_t *) board->parent;

    dev->page = new_page;

    if (new_page & 0x80) {
        if (lxt->is_lxt3) {
            /* Point invalid pages at 1 MB which is outside the maximum. */
            if ((new_page & 0x7f) >= 0x40)
                dev->virt = EMS_TOTAL_MAX;
           else
                dev->virt = ((new_page & 0x7f) << 14);
        } else
            dev->virt = ((new_page & 0x0f) << 14) + ((new_page & 0x40) << 12);

        if (dev->virt >= board->mem_size)
            dev->virt = EMS_TOTAL_MAX;
    } else
        dev->virt = EMS_TOTAL_MAX;

    dev->ram = board->ram + dev->virt;

    if ((new_page & 0x80) && (dev->virt != EMS_TOTAL_MAX)) {
        mem_mapping_enable(&dev->mapping);

        mem_mapping_set_exec(&dev->mapping, dev->ram);
        mem_mapping_set_p(&dev->mapping, dev->ram);
    } else
        mem_mapping_disable(&dev->mapping);

    flushmmucache();
}

static void
lxt_ems_out(uint16_t port, uint8_t val, void *priv)
{
    lxt_ems_board_t *dev       = (lxt_ems_board_t *) priv;
    uint8_t          reg       = port >> 14;
    uint32_t         saddrs[8] = { 0xc4000, 0xc8000, 0xcc000, 0xd0000,
                                   0xd4000, 0xd8000, 0xdc000, 0xe0000 };
    uint32_t saddr;

    if (port & 0x0001) {
        dev->ems[reg].ctrl = val;

        if (reg < 0x03) {
            dev->ems_base_idx  = (dev->ems_base_idx & ~(0x04 >> (2 - reg))) |
                                 ((dev->ems[reg].ctrl & 0x80) >> (7 - reg));

            saddr = saddrs[dev->ems_base_idx];
        
            for (uint8_t i = 0; i < 4; i++) {
                uint32_t base = saddr + (i * 0x4000);
                mem_mapping_set_addr(&dev->ems[i].mapping, base, 0x4000);
                if (!(dev->ems[i].page & 0x80) || (dev->ems[i].virt == EMS_TOTAL_MAX))
                    mem_mapping_disable(&dev->ems[i].mapping);
            }
        }

        flushmmucache();
    } else if (!(port & 0x0001)) {
        dev->ems[reg].page = val;
        ems_update_virt(&dev->ems[reg], val);
    }
}

static uint8_t
lxt_ems_in(uint16_t port, void *priv)
{
    lxt_ems_board_t *dev = (lxt_ems_board_t *) priv;
    uint8_t          reg = port >> 14;
    uint8_t          ret = 0xff;

    if (port & 0x0001)
        ret = dev->ems[reg].ctrl;
    else
        ret = dev->ems[reg].page;

    return ret;
}

static void
lxt_ems_write(uint32_t addr, uint8_t val, void *priv)
{
    uint8_t *mem = (uint8_t *) priv;

    mem[addr & 0x3fff] = val;
}

static void
lxt_ems_writew(uint32_t addr, uint16_t val, void *priv)
{
    uint8_t *mem = (uint8_t *) priv;

    *(uint16_t *) &(mem[addr & 0x3fff]) = val;
}

static uint8_t
lxt_ems_read(uint32_t addr, void *priv)
{
    uint8_t *mem = (uint8_t *) priv;
    uint8_t  ret = 0xff;

    ret = mem[addr & 0x3fff];

    return ret;
}

static uint16_t
lxt_ems_readw(uint32_t addr, void *priv)
{
    uint8_t  *mem = (uint8_t *) priv;
    uint16_t  ret = 0xff;

    ret = *(uint16_t *) &(mem[addr & 0x3fff]);

    return ret;
}

static lxt_ems_board_t *
lxt_ems_init(lxt_t *parent, int en, uint16_t io, uint32_t mem)
{
    lxt_ems_board_t *dev = (lxt_ems_board_t *) calloc(1, sizeof(lxt_ems_board_t));

    if (en) {
        dev->parent = parent;

        if (io != 0x0000) {
            io_sethandler(io         , 0x0002, lxt_ems_in, NULL, NULL, lxt_ems_out, NULL, NULL, dev);
            io_sethandler(io | 0x4000, 0x0002, lxt_ems_in, NULL, NULL, lxt_ems_out, NULL, NULL, dev);
            io_sethandler(io | 0x8000, 0x0002, lxt_ems_in, NULL, NULL, lxt_ems_out, NULL, NULL, dev);
            io_sethandler(io | 0xc000, 0x0002, lxt_ems_in, NULL, NULL, lxt_ems_out, NULL, NULL, dev);
        }

        dev->ram      = (uint8_t *) calloc(mem, sizeof(uint8_t));
        dev->mem_size = mem;

        for (uint8_t i = 0; i < 4; i++) {
            uint8_t *ptr = dev->ram + (i << 14);

            if (parent->is_lxt3)
                mem_mapping_add(&dev->ems[i].mapping, 0xe0000 + (i << 14), 0x4000,
                                lxt_ems_read,  lxt_ems_readw,  NULL,
                                lxt_ems_write, lxt_ems_writew, NULL,
                                ptr, 0, ptr);
            else
                mem_mapping_add(&dev->ems[i].mapping, 0xe0000 + (i << 14), 0x4000,
                                lxt_ems_read,  NULL,           NULL,
                                lxt_ems_write, NULL,           NULL,
                                ptr, 0, ptr);

            mem_mapping_disable(&dev->ems[i].mapping);

            dev->ems[i].page = 0x7f;
            dev->ems[i].ctrl = (i == 3) ? 0x00 : 0x80;

            dev->ems[i].parent = dev;

            ems_update_virt(&(dev->ems[i]), dev->ems[i].page);
        }
    }

    return dev;
}

static void
lxt_close(void *priv)
{
    lxt_t *dev        = (lxt_t *) priv;
    int    ems_boards = (1 - dev->is_lxt3) + 1;

    for (int i = 0; i < ems_boards; i++)
        if (dev->ems_boards[i] != NULL) {
            if (dev->ems_boards[i]->ram != NULL)
                free(dev->ems_boards[i]->ram);
            free(dev->ems_boards[i]);
        }

    free(dev);
}

static void *
lxt_init(const device_t *info)
{
    lxt_t *  dev           = (lxt_t *) calloc(1, sizeof(lxt_t));
    int      ems_boards    = (1 - info->local) + 1;
    int      ems_en[2]     = { 0 };
    uint16_t ems_io[2]     = { 0 };
    uint32_t ems_mem[2]    = { 0 };
    char     conf_str[512] = { 0 };

    dev->is_lxt3 = info->local;

    for (int i = 0; i < ems_boards; i++) {
        sprintf(conf_str, "ems_%i_enable", i + 1);
        ems_en[i]  = device_get_config_int(conf_str);

        sprintf(conf_str, "ems_%i_base", i + 1);
        ems_io[i]  = device_get_config_hex16(conf_str);

        sprintf(conf_str, "ems_%i_mem_size", i + 1);
        ems_mem[i] = device_get_config_int(conf_str) << 10;

        dev->ems_boards[i] = lxt_ems_init(dev, ems_en[i], ems_io[i], ems_mem[i]);
    }

    mem_set_mem_state(0x0c0000, 0x40000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);

    return dev;
}

static const device_config_t laserxt_config[] = {
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "laserxt_126",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "1.08",
                .internal_name = "laserxt_108",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 8192,
                .files         = { "roms/machines/ltxt/ltxt-v1.08.bin", "" }
            },
            {
                .name          = "1.26",
                .internal_name = "laserxt_126",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 8192,
                .files         = { "roms/machines/ltxt/27c64.bin", "" }
            },
            { .files_no = 0 }
        }
    },
    {
        .name           = "ems_1_base",
        .description    = "EMS 1 Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value =     0 },
            { .description = "0x208",    .value = 0x208 },
            { .description = "0x218",    .value = 0x218 },
            { .description = "0x258",    .value = 0x258 },
            { .description = "0x268",    .value = 0x268 },
            { .description = "0x2A8",    .value = 0x2a8 },
            { .description = "0x2B8",    .value = 0x2b8 },
            { .description = "0x2E8",    .value = 0x2e8 },
            { .description = ""                         }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "ems_2_base",
        .description    = "EMS 2 Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value =     0 },
            { .description = "0x208",    .value = 0x208 },
            { .description = "0x218",    .value = 0x218 },
            { .description = "0x258",    .value = 0x258 },
            { .description = "0x268",    .value = 0x268 },
            { .description = "0x2A8",    .value = 0x2a8 },
            { .description = "0x2B8",    .value = 0x2b8 },
            { .description = "0x2E8",    .value = 0x2e8 },
            { .description = ""                         }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "ems_1_mem_size",
        .description    = "EMS 1 Memory Size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner = {
            .min  =    0,
            .max  =  512,
            .step =   32
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "ems_2_mem_size",
        .description    = "EMS 2 Memory Size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner = {
            .min  =    0,
            .max  =  512,
            .step =   32
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "ems_1_enable",
        .description    = "Enable EMS 1",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "ems_2_enable",
        .description    = "Enable EMS 2",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

const device_t laserxt_device = {
    .name          = "VTech Laser Turbo XT",
    .internal_name = "laserxt",
    .flags         = 0,
    .local         = 0,
    .init          = lxt_init,
    .close         = lxt_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = laserxt_config
};

static const device_config_t lxt3_config[] = {
    {
        .name           = "ems_1_base",
        .description    = "EMS Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value =     0 },
            { .description = "0x208",    .value = 0x208 },
            { .description = "0x218",    .value = 0x218 },
            { .description = "0x258",    .value = 0x258 },
            { .description = "0x268",    .value = 0x268 },
            { .description = "0x2A8",    .value = 0x2a8 },
            { .description = "0x2B8",    .value = 0x2b8 },
            { .description = "0x2E8",    .value = 0x2e8 },
            { .description = ""                         }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "ems_1_mem_size",
        .description    = "EMS Memory Size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner = {
            .min  =    0,
            .max  = 1024,
            .step =   32
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "ems_1_enable",
        .description    = "Enable EMS",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

const device_t lxt3_device = {
    .name          = "VTech Laser Turbo XT",
    .internal_name = "laserxt",
    .flags         = 0,
    .local         = 1,
    .init          = lxt_init,
    .close         = lxt_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = lxt3_config
};
