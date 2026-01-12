/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          XT-IDE controller emulation.
 *
 *          The XT-IDE project is intended to allow 8-bit ("XT") systems
 *          to use regular IDE drives. IDE is a standard based on the
 *          16b PC/AT design, and so a special board (with its own BIOS)
 *          had to be created for this.
 *
 *          XT-IDE is *NOT* the same as XTA, or X-IDE, which is an older
 *          standard where the actual MFM/RLL controller for the PC/XT
 *          was placed on the hard drive (hard drives where its drive
 *          type would end in "X" or "XT", such as the 8425XT.) This was
 *          more or less the original IDE, but since those systems were
 *          already on their way out, the newer IDE standard based on the
 *          PC/AT controller and 16b design became the IDE we now know.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2018 Miran Grca.
 *          Copyright 2017-2018 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/device.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/plat_unused.h>

#define ROM_PATH_TINY   "roms/hdd/xtide/ide_tiny.bin"
#define ROM_PATH_XT     "roms/hdd/xtide/ide_xt.bin"
#define ROM_PATH_XTP    "roms/hdd/xtide/ide_xtp.bin"
#define ROM_PATH_AT     "roms/hdd/xtide/ide_at.bin"
#define ROM_PATH_AT_386 "roms/hdd/xtide/ide_386.bin"
#define ROM_PATH_PS2    "roms/hdd/xtide/SIDE1V12.BIN"
#define ROM_PATH_PS2AT  "roms/hdd/xtide/ide_at_1_1_5.bin"

typedef struct xtide_t {
    void   *ide_board;
    uint8_t data_high;
    rom_t   bios_rom;
    char    nvr_path[64];
} xtide_t;

static void
xtide_write(uint16_t port, uint8_t val, void *priv)
{
    xtide_t *xtide = (xtide_t *) priv;

    switch (port & 0xf) {
        case 0x0:
            ide_writew(0x0, val | (xtide->data_high << 8), xtide->ide_board);
            return;

        case 0x1:
        case 0x2:
        case 0x3:
        case 0x4:
        case 0x5:
        case 0x6:
        case 0x7:
            ide_writeb((port & 0xf), val, xtide->ide_board);
            return;

        case 0x8:
            xtide->data_high = val;
            return;

        case 0xe:
            ide_write_devctl(0x0, val, xtide->ide_board);
            return;

        default:
            break;
    }
}

static uint8_t
xtide_read(uint16_t port, void *priv)
{
    xtide_t *xtide = (xtide_t *) priv;
    uint16_t tempw = 0xffff;

    switch (port & 0xf) {
        case 0x0:
            tempw            = ide_readw(0x0, xtide->ide_board);
            xtide->data_high = tempw >> 8;
            break;

        case 0x1:
        case 0x2:
        case 0x3:
        case 0x4:
        case 0x5:
        case 0x6:
        case 0x7:
            tempw = ide_readb((port & 0xf), xtide->ide_board);
            break;

        case 0x8:
            tempw = xtide->data_high;
            break;

        case 0xe:
            tempw = ide_read_alt_status(0x0, xtide->ide_board);
            break;

        default:
            break;
    }

    return (tempw & 0xff);
}

static void *
xtide_init(const device_t *info)
{
    xtide_t *xtide = calloc(1, sizeof(xtide_t));

    rom_init(&xtide->bios_rom,
             device_get_bios_file(info, device_get_config_bios("bios"), 0),
             device_get_config_hex20("bios_addr"), 0x2000, 0x1fff, 0, MEM_MAPPING_EXTERNAL);

    xtide->ide_board = ide_xtide_init();

    io_sethandler(device_get_config_hex16("base"), 16,
                  xtide_read, NULL, NULL,
                  xtide_write, NULL, NULL, xtide);

    uint8_t rom_writes_enabled = device_get_config_int("rom_writes_enabled");

    if (rom_writes_enabled) {
        mem_mapping_set_write_handler(&xtide->bios_rom.mapping, rom_write, rom_writew, rom_writel);
        sprintf(xtide->nvr_path, "xtide_%i.nvr", device_get_instance());
        FILE *fp = nvr_fopen(xtide->nvr_path, "rb");
        if (fp != NULL) {
            (void) !fread(xtide->bios_rom.rom, 1, 0x2000, fp);
            fclose(fp);
        }
    }

    return xtide;
}

static void *
xtide_at_init(const device_t *info)
{
    xtide_t *xtide = calloc(1, sizeof(xtide_t));

    rom_init(&xtide->bios_rom,
             device_get_bios_file(info, device_get_config_bios("bios"), 0),
             0xc8000, 0x2000, 0x1fff, 0, MEM_MAPPING_EXTERNAL);

    if (info->local == 1)
        device_add(&ide_isa_2ch_device);
    else
        device_add(&ide_isa_device);

    return xtide;
}

static void *
xtide_acculogic_init(UNUSED(const device_t *info))
{
    xtide_t *xtide = calloc(1, sizeof(xtide_t));

    rom_init(&xtide->bios_rom, ROM_PATH_PS2,
             0xc8000, 0x2000, 0x1fff, 0, MEM_MAPPING_EXTERNAL);

    xtide->ide_board = ide_xtide_init();

    io_sethandler(0x0360, 16,
                  xtide_read, NULL, NULL,
                  xtide_write, NULL, NULL, xtide);

    return xtide;
}

static int
xtide_acculogic_available(void)
{
    return (rom_present(ROM_PATH_PS2));
}

static void
xtide_close(void *priv)
{
    xtide_t *xtide = (xtide_t *) priv;

    if (xtide->nvr_path[0] != 0x00) {
        FILE *fp = nvr_fopen(xtide->nvr_path, "wb");
        if (fp != NULL) {
            fwrite(xtide->bios_rom.rom, 1, 0x2000, fp);
            fclose(fp);
        }
    }

    free(xtide);

    ide_xtide_close();
}

static void *
xtide_at_ps2_init(UNUSED(const device_t *info))
{
    xtide_t *xtide = calloc(1, sizeof(xtide_t));

    rom_init(&xtide->bios_rom, ROM_PATH_PS2AT,
             0xc8000, 0x2000, 0x1fff, 0, MEM_MAPPING_EXTERNAL);

    if (info->local == 1)
        device_add(&ide_isa_2ch_device);
    else
        device_add(&ide_isa_device);

    return xtide;
}

static int
xtide_at_ps2_available(void)
{
    return (rom_present(ROM_PATH_PS2AT));
}

static void
xtide_at_close(void *priv)
{
    xtide_t *xtide = (xtide_t *) priv;

    free(xtide);
}

// clang-format off
static const device_config_t xtide_config[] = {
    {
        .name           = "bios",
        .description    = "BIOS Revision",
        .type           = CONFIG_BIOS,
        .default_string = "xt",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Regular XT",
                .internal_name = "xt",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 8192,
                .files         = { ROM_PATH_XT, "" }
            },
            {
                .name          = "XT+ (V20/V30/8018x)",
                .internal_name = "xt_plus",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 8192,
                .files         = { ROM_PATH_XTP, "" }
            },
            { .files_no = 0 }
        },
    },
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x300,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "200H", .value = 0x200 },
            { .description = "210H", .value = 0x210 },
            { .description = "220H", .value = 0x220 },
            { .description = "230H", .value = 0x230 },
            { .description = "240H", .value = 0x240 },
            { .description = "250H", .value = 0x250 },
            { .description = "260H", .value = 0x260 },
            { .description = "270H", .value = 0x270 },
            { .description = "280H", .value = 0x280 },
            { .description = "290H", .value = 0x290 },
            { .description = "2A0H", .value = 0x2a0 },
            { .description = "2B0H", .value = 0x2b0 },
            { .description = "2C0H", .value = 0x2c0 },
            { .description = "2D0H", .value = 0x2d0 },
            { .description = "2E0H", .value = 0x2e0 },
            { .description = "2F0H", .value = 0x2f0 },
            { .description = "300H", .value = 0x300 },
            { .description = "310H", .value = 0x310 },
            { .description = "320H", .value = 0x320 },
            { .description = "330H", .value = 0x330 },
            { .description = "340H", .value = 0x340 },
            { .description = "350H", .value = 0x350 },
            { .description = "360H", .value = 0x360 },
            { .description = "370H", .value = 0x370 },
            { .description = "380H", .value = 0x380 },
            { .description = "390H", .value = 0x390 },
            { .description = "3A0H", .value = 0x3a0 },
            { .description = "3B0H", .value = 0x3b0 },
            { .description = "3C0H", .value = 0x3c0 },
            { .description = "3D0H", .value = 0x3d0 },
            { .description = "3E0H", .value = 0x3e0 },
            { .description = "3F0H", .value = 0x3f0 },
            { NULL                                  }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "rom_writes_enabled",
        .description    = "Enable BIOS extension ROM Writes",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_addr",
        .description    = "BIOS address",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0xd0000,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value = 0x00000 },
#if 0
            // Supported on XT IDE Deluxe By Monotech
            { .description = "C000H",    .value = 0xc0000 },
            { .description = "C200H",    .value = 0xc2000 },
            { .description = "C400H",    .value = 0xc4000 },
            { .description = "C600H",    .value = 0xc6000 },
#endif
            { .description = "C800H",    .value = 0xc8000 },
            { .description = "CA00H",    .value = 0xca000 },
            { .description = "CC00H",    .value = 0xcc000 },
            { .description = "CE00H",    .value = 0xce000 },
            { .description = "D000H",    .value = 0xd0000 },
            { .description = "D200H",    .value = 0xd2000 },
            { .description = "D400H",    .value = 0xd4000 },
            { .description = "D600H",    .value = 0xd6000 },
            { .description = "D800H",    .value = 0xd8000 },
            { .description = "DA00H",    .value = 0xda000 },
            { .description = "DC00H",    .value = 0xdc000 },
            { .description = "DE00H",    .value = 0xde000 },
#if 0
            // Supported on VCFed rev 2
            { .description = "E000H",    .value = 0xe0000 },
            { .description = "E400H",    .value = 0xe4000 },
            { .description = "E800H",    .value = 0xe8000 },
            { .description = "EC00H",    .value = 0xec000 },
#endif
            { .description = ""                           }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t xtide_at_config[] = {
    {
        .name           = "bios",
        .description    = "BIOS Revision",
        .type           = CONFIG_BIOS,
        .default_string = "at",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Regular AT",
                .internal_name = "at",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 8192,
                .files         = { ROM_PATH_AT, "" }
            },
            {
                .name          = "386",
                .internal_name = "at_386",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 8192,
                .files         = { ROM_PATH_AT_386, "" }
            },
            { .files_no = 0 }
        },
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t xtide_device = {
    .name          = "PC/XT XTIDE",
    .internal_name = "xtide",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = xtide_init,
    .close         = xtide_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = xtide_config
};

const device_t xtide_at_device = {
    .name          = "PC/AT XTIDE (Primary Only)",
    .internal_name = "xtide_at_1ch",
    .flags         = DEVICE_ISA16,
    .local         = 0,
    .init          = xtide_at_init,
    .close         = xtide_at_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = xtide_at_config
};

const device_t xtide_at_2ch_device = {
    .name          = "PC/AT XTIDE",
    .internal_name = "xtide_at",
    .flags         = DEVICE_ISA16,
    .local         = 1,
    .init          = xtide_at_init,
    .close         = xtide_at_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = xtide_at_config
};

const device_t xtide_acculogic_device = {
    .name          = "Acculogic XT IDE",
    .internal_name = "xtide_acculogic",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = xtide_acculogic_init,
    .close         = xtide_close,
    .reset         = NULL,
    .available     = xtide_acculogic_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t xtide_at_ps2_device = {
    .name          = "PS/2 AT XTIDE (1.1.5) (Primary Only)",
    .internal_name = "xtide_at_ps2_1ch",
    .flags         = DEVICE_ISA16,
    .local         = 0,
    .init          = xtide_at_ps2_init,
    .close         = xtide_at_close,
    .reset         = NULL,
    .available     = xtide_at_ps2_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t xtide_at_ps2_2ch_device = {
    .name          = "PS/2 AT XTIDE (1.1.5)",
    .internal_name = "xtide_at_ps2",
    .flags         = DEVICE_ISA16,
    .local         = 1,
    .init          = xtide_at_ps2_init,
    .close         = xtide_at_close,
    .reset         = NULL,
    .available     = xtide_at_ps2_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
