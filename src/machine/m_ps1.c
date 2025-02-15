/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the IBM PS/1 models 2011, 2121.
 *
 * Model 2011: The initial model, using a 10MHz 80286.
 *
 * Model 2121: This is similar to model 2011 but some of the functionality
 *             has moved to a chip at ports 0xe0 (index)/0xe1 (data). The
 *             only functions I have identified are enables for the first
 *             512K and next 128K of RAM, in bits 0 of registers 0 and 1
 *             respectively.
 *
 *             Port 0x105 has bit 7 forced high. Without this 128K of
 *             memory will be missed by the BIOS on cold boots.
 *
 *             The reserved 384K is remapped to the top of extended memory.
 *             If this is not done then you get an error on startup.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2017-2019 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/nmi.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/sio.h>
#include <86box/nvr.h>
#include <86box/gameport.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/keyboard.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/port_6x.h>
#include <86box/video.h>
#include <86box/machine.h>
#include <86box/sound.h>
#include <86box/plat_unused.h>

typedef struct {
    int model;

    rom_t mid_rom, high_rom;

    uint8_t ps1_91,
        ps1_92,
        ps1_94,
        ps1_102,
        ps1_103,
        ps1_104,
        ps1_105,
        ps1_190;
    int     ps1_e0_addr;
    uint8_t ps1_e0_regs[256];

    serial_t *uart;
} ps1_t;

static void
recalc_memory(ps1_t *ps)
{
    /* Enable first 512K */
    mem_set_mem_state(0x00000, 0x80000,
                      (ps->ps1_e0_regs[0] & 0x01) ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));

    /* Enable 512-640K */
    mem_set_mem_state(0x80000, 0x20000,
                      (ps->ps1_e0_regs[1] & 0x01) ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
}

static void
ps1_write(uint16_t port, uint8_t val, void *priv)
{
    ps1_t *ps = (ps1_t *) priv;

    switch (port) {
        case 0x0092:
            if (ps->model != 2011) {
                if (val & 1) {
                    softresetx86();
                    cpu_set_edx();
                }
                ps->ps1_92 = val & ~1;
            } else {
                ps->ps1_92 = val;
            }
            mem_a20_alt = val & 2;
            mem_a20_recalc();
            break;

        case 0x0094:
            ps->ps1_94 = val;
            break;

        case 0x00e0:
            if (ps->model != 2011) {
                ps->ps1_e0_addr = val;
            }
            break;

        case 0x00e1:
            if (ps->model != 2011) {
                ps->ps1_e0_regs[ps->ps1_e0_addr] = val;
                recalc_memory(ps);
            }
            break;

        case 0x0102:
            if (!(ps->ps1_94 & 0x80)) {
                lpt1_remove();
                serial_remove(ps->uart);
                if (val & 0x04) {
                    if (val & 0x08)
                        serial_setup(ps->uart, COM1_ADDR, COM1_IRQ);
                    else
                        serial_setup(ps->uart, COM2_ADDR, COM2_IRQ);
                }
                if (val & 0x10) {
                    switch ((val >> 5) & 3) {
                        case 0:
                            lpt1_setup(LPT_MDA_ADDR);
                            break;
                        case 1:
                            lpt1_setup(LPT1_ADDR);
                            break;
                        case 2:
                            lpt1_setup(LPT2_ADDR);
                            break;

                        default:
                            break;
                    }
                }
                ps->ps1_102 = val;
            }
            break;

        case 0x0103:
            ps->ps1_103 = val;
            break;

        case 0x0104:
            ps->ps1_104 = val;
            break;

        case 0x0105:
            ps->ps1_105 = val;
            break;

        case 0x0190:
            ps->ps1_190 = val;
            break;

        default:
            break;
    }
}

static uint8_t
ps1_read(uint16_t port, void *priv)
{
    ps1_t  *ps  = (ps1_t *) priv;
    uint8_t ret = 0xff;

    switch (port) {
        case 0x0091:
            ret        = ps->ps1_91;
            ps->ps1_91 = 0;
            break;

        case 0x0092:
            ret = ps->ps1_92;
            break;

        case 0x0094:
            ret = ps->ps1_94;
            break;

        case 0x00e1:
            if (ps->model != 2011) {
                ret = ps->ps1_e0_regs[ps->ps1_e0_addr];
            }
            break;

        case 0x0102:
            if (ps->model == 2011)
                ret = ps->ps1_102 | 0x08;
            else
                ret = ps->ps1_102;
            break;

        case 0x0103:
            ret = ps->ps1_103;
            break;

        case 0x0104:
            ret = ps->ps1_104;
            break;

        case 0x0105:
            if (ps->model == 2011)
                ret = ps->ps1_105;
            else
                ret = ps->ps1_105 | 0x80;
            break;

        case 0x0190:
            ret = ps->ps1_190;
            break;

        default:
            break;
    }

    return ret;
}

static const device_config_t ps1_2011_config[] = {
    // clang-format off
    {
        .name = "bios_language",
        .description = "BIOS Language",
        .type = CONFIG_BIOS,
        .default_string = "english_us",
        .default_int = 0,
        .file_filter = "",
        .spinner = { 0 },
        .bios = {
            { .name = "English (US)", .internal_name = "english_us", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 262144, .files = { "roms/machines/ibmps1es/FC0000_US.BIN", "" } },
            { .name = "English (UK)", .internal_name = "english_uk", .bios_type = BIOS_NORMAL,
              .files_no = 2, .local = 0, .size = 262144, .files = { "roms/machines/ibmps1es/F80000_UK.BIN", "roms/machines/ibmps1es/FC0000_UK.BIN", "" } },
            { .name = "English (Canada)", .internal_name = "english_ca", .bios_type = BIOS_NORMAL,
              .files_no = 2, .local = 0, .size = 262144, .files = { "roms/machines/ibmps1es/F80000_CA.BIN", "roms/machines/ibmps1es/FC0000_CA.BIN", "" } },
            { .name = "Portuguese", .internal_name = "portuguese", .bios_type = BIOS_NORMAL,
              .files_no = 2, .local = 0, .size = 262144, .files = { "roms/machines/ibmps1es/F80000_PT.BIN", "roms/machines/ibmps1es/FC0000_PT.BIN", "" } },
            { .name = "German", .internal_name = "german", .bios_type = BIOS_NORMAL,
              .files_no = 2, .local = 0, .size = 262144, .files = { "roms/machines/ibmps1es/F80000_DE.BIN", "roms/machines/ibmps1es/FC0000_DE.BIN", "" } },
            { .name = "Swedish", .internal_name = "swedish", .bios_type = BIOS_NORMAL,
              .files_no = 2, .local = 0, .size = 262144, .files = { "roms/machines/ibmps1es/F80000_SE.BIN", "roms/machines/ibmps1es/FC0000_SE.BIN", "" } },
            { .name = "French", .internal_name = "french", .bios_type = BIOS_NORMAL,
              .files_no = 2, .local = 0, .size = 262144, .files = { "roms/machines/ibmps1es/F80000_FR.BIN", "roms/machines/ibmps1es/FC0000_FR.BIN", "" } },
            { .name = "Italian", .internal_name = "italian", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 524288, .files = { "roms/machines/ibmps1es/f80000.bin", "" } },
            { .name = "Spanish", .internal_name = "spanish", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 524288, .files = { "roms/machines/ibmps1es/F80000_ES.bin", "" } },
            { .files_no = 0 }
        },
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ps1_2011_device = {
    .name          = "PS/1 2011",
    .internal_name = "ps/1_2011",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = &ps1_2011_config[0]
};

static void
ps1_setup(int model)
{
    ps1_t *ps;
    void  *priv;

    ps = (ps1_t *) calloc(1, sizeof(ps1_t));
    ps->model = model;

    io_sethandler(0x0091, 1,
                  ps1_read, NULL, NULL, ps1_write, NULL, NULL, ps);
    io_sethandler(0x0092, 1,
                  ps1_read, NULL, NULL, ps1_write, NULL, NULL, ps);
    io_sethandler(0x0094, 1,
                  ps1_read, NULL, NULL, ps1_write, NULL, NULL, ps);
    io_sethandler(0x0102, 4,
                  ps1_read, NULL, NULL, ps1_write, NULL, NULL, ps);
    io_sethandler(0x0190, 1,
                  ps1_read, NULL, NULL, ps1_write, NULL, NULL, ps);

    ps->uart = device_add_inst(&ns16450_device, 1);

    lpt1_remove();
    lpt1_setup(LPT_MDA_ADDR);

    mem_remap_top(384);

    device_add(&ps_nvr_device);

    device_add(&fdc_ps2_device);

    if (model == 2011) {
        if (!strcmp("english_us", device_get_config_bios("bios_language"))) {
            /* US English */
            rom_init(&ps->high_rom,
                    device_get_bios_file(device_context_get_device(), device_get_config_bios("bios_language"), 0),
                    0xfc0000, 0x40000, 0x3ffff, 0, MEM_MAPPING_EXTERNAL);

        } else if ((device_get_bios_file(device_context_get_device(), device_get_config_bios("bios_language"), 1)) == NULL) {
            /* Combined ROM. */
            rom_init(&ps->high_rom,
                        device_get_bios_file(device_context_get_device(), device_get_config_bios("bios_language"), 0),
                        0xf80000, 0x80000, 0x7ffff, 0, MEM_MAPPING_EXTERNAL);
        } else {
            /* Split ROM. */
            rom_init(&ps->mid_rom,
                    device_get_bios_file(device_context_get_device(), device_get_config_bios("bios_language"), 0),
                    0xf80000, 0x40000, 0x3ffff, 0, MEM_MAPPING_EXTERNAL);

            rom_init(&ps->high_rom,
                    device_get_bios_file(device_context_get_device(), device_get_config_bios("bios_language"), 1),
                    0xfc0000, 0x40000, 0x3ffff, 0, MEM_MAPPING_EXTERNAL);
        }

        lpt2_remove();

        device_add(&ps1snd_device);

        /* Enable the builtin HDC. */
        if (hdc_current[0] == HDC_INTERNAL) {
            priv = device_add(&ps1_hdc_device);

            ps1_hdc_inform(priv, &ps->ps1_91);
        }

        /* Enable the PS/1 VGA controller. */
        device_add(&ps1vga_device);
    } else if (model == 2121) {
        io_sethandler(0x00e0, 2,
                      ps1_read, NULL, NULL, ps1_write, NULL, NULL, ps);

        if (rom_present("roms/machines/ibmps1_2121/F80000.BIN")) {
            rom_init(&ps->mid_rom,
                     "roms/machines/ibmps1_2121/F80000.BIN",
                     0xf80000, 0x40000, 0x3ffff, 0, MEM_MAPPING_EXTERNAL);
        }
        rom_init(&ps->high_rom,
                 "roms/machines/ibmps1_2121/FC0000.BIN",
                 0xfc0000, 0x40000, 0x3ffff, 0, MEM_MAPPING_EXTERNAL);

        /* Initialize the video controller. */
        if (gfxcard[0] == VID_INTERNAL)
            device_add(&ibm_ps1_2121_device);

        device_add(&ide_isa_device);

        device_add(&ps1snd_device);
    }
}

static void
ps1_common_init(const machine_t *model)
{
    machine_common_init(model);

    refresh_at_enable = 1;
    pit_devs[0].set_out_func(pit_devs[0].data, 1, pit_refresh_timer_at);

    dma16_init();
    pic2_init();

    device_add(&keyboard_ps2_ps1_device);
    device_add(&port_6x_device);

    /* Audio uses ports 200h and 202-207h, so only initialize gameport on 201h. */
    standalone_gameport_type = &gameport_201_device;
}

int
machine_ps1_m2011_init(const machine_t *model)
{
    int ret;
    const char* fn;
    uint32_t offset;

    if (!device_available(model->device)) {
        /* No ROMs available. */
        return 0;
    }

    device_context(model->device);
    if ((fn = device_get_bios_file(model->device, device_get_config_bios("bios_language"), 1)) == NULL) {
        /* Combined ROM or US English. */
        fn = device_get_bios_file(model->device, device_get_config_bios("bios_language"), 0);
        offset = (!strcmp("english_us", device_get_config_bios("bios_language"))) ? 0x20000 : 0x60000;
    } else {
        /* Separated ROM. */
        offset = 0x20000;
    }

    if (!fn) {
        fn = device_get_bios_file(model->device, "us_english", 0);
        offset = 0x20000;
    }

    ret = bios_load_linear(fn, 0x000e0000, 131072, offset);
    device_context_restore();

    if (bios_only || !ret) {
        return ret;
    }

    ps1_common_init(model);

    device_context(model->device);

    ps1_setup(2011);    

    device_context_restore();

    return ret;
}

int
machine_ps1_m2121_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ibmps1_2121/FC0000.BIN",
                           0x000e0000, 131072, 0x20000);

    if (bios_only || !ret)
        return ret;

    ps1_common_init(model);

    ps1_setup(2121);

    return ret;
}
