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
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2018 Miran Grca.
 *          Copyright 2017-2018 Fred N. van Kempen.
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#define ENABLE_XTIDE_LOG 1
#ifdef ENABLE_XTIDE_LOG
#include <stdarg.h>
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/nvr.h>
#include <86box/rom.h>
#ifdef ENABLE_XTIDE_LOG
#include <86box/log.h>
#endif
#include <86box/plat_unused.h>

#define ROM_PATH_TINY   "roms/hdd/xtide/ide_tiny.bin"
#define ROM_PATH_XT     "roms/hdd/xtide/ide_xt.bin"
#define ROM_PATH_XTP    "roms/hdd/xtide/ide_xtp.bin"
#define ROM_PATH_AT     "roms/hdd/xtide/ide_at.bin"
#define ROM_PATH_AT_386 "roms/hdd/xtide/ide_386.bin"
#define ROM_PATH_PS2    "roms/hdd/xtide/SIDE1V12.BIN"
#define ROM_PATH_PS2AT  "roms/hdd/xtide/ide_at_1_1_5.bin"
#define ROM_PATH_JRIDE  "roms/hdd/xtide/jride.bin"

#define JRIDE_ROM_WINDOW_SIZE      0x3a00 // 14.5KB for ROM
#define JRIDE_NON_ROM_WINDOW_SIZE  0x0600 // 1.5KB for data window IDE registers and scratch RAM
#define JRIDE_DATA_WINDOW_OFFSET   0x3a00 // Offset of the IDE data window
#define JRIDE_DATA_WINDOW_SIZE     0x0200 // 512 bytes for IDE registers
#define JRIDE_CS0_OFFSET           0x3c00 // Offset of the IDE CS0 register
#define JRIDE_CS1_OFFSET           0x3c08 // Offset of the IDE CS1 register
#define JRIDE_FILL_ENABLE_OFFSET   0x3c10 // Offset of the RAM fill enable register
#define JRIDE_WINDOW_ENABLE_OFFSET 0x3c11 // Offset of the ROM window enable register
#define JRIDE_SCRATCH_OFFSET       0x3c12 // Offset of the scratch register
#define JRIDE_SCRATCH_SIZE         0x3ee  // 1006 bytes

#ifdef ENABLE_XTIDE_LOG
uint8_t xtide_do_log = ENABLE_XTIDE_LOG;

static void
xtide_log(void *priv, const char *fmt, ...)
{
    if (xtide_do_log) {
        va_list ap;
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define xtide_log(fmt, ...)
#endif

typedef struct xtide_t {
    void   *ide_board;
    uint8_t data_high;
    rom_t   bios_rom;

    mem_mapping_t jride_window_mapping;
    uint8_t       jride_scratch[JRIDE_SCRATCH_SIZE];

    char nvr_path[64];

    void   *log;
} xtide_t;

static void
xtide_write(uint16_t port, uint8_t val, void *priv)
{
    xtide_t *xtide = (xtide_t *) priv;

    uint8_t reg = (port & 0xf);

    xtide_log(xtide->log, "[%04X:%08X] [W] %04X = %02X\n", CS, cpu_state.pc, reg, val);

    switch (reg) {
        case 0x0:
            ide_writew(0x0, val | (xtide->data_high << 8), xtide->ide_board);
            break;

        case 0x1 ... 0x7:
            ide_writeb(reg, val, xtide->ide_board);
            break;

        case 0x8:
            xtide->data_high = val;
            break;

        case 0xe:
            ide_write_devctl(0x0, val, xtide->ide_board);
            break;

        default:
            break;
    }
}

static uint8_t
xtide_read(uint16_t port, void *priv)
{
    xtide_t *xtide = (xtide_t *) priv;

    uint8_t reg = (port & 0xf);
    uint16_t tempw = 0xffff;

    switch (reg) {
        case 0x0:
            tempw            = ide_readw(0x0, xtide->ide_board);
            xtide->data_high = tempw >> 8;
            break;

        case 0x1 ... 0x7:
            tempw = ide_readb(reg, xtide->ide_board);
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

    xtide_log(xtide->log, "[%04X:%08X] [R] %04X = %02X\n", CS, cpu_state.pc, port, (tempw & 0xff));

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
                  xtide_write, NULL, NULL,
                  xtide);

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
                  xtide_write, NULL, NULL,
                  xtide);

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

static uint8_t *
jride_get_data_window(xtide_t *xtide)
{
    if (!xtide->ide_board)
        return NULL;

    return ide_get_pio_buffer(xtide->ide_board);
}

static int
jride_should_log_access(uint32_t offset)
{
    return offset >= JRIDE_DATA_WINDOW_OFFSET;
}

static void
jride_write(uint32_t addr, uint8_t val, void *priv)
{
    xtide_t *xtide  = (xtide_t *) priv;
    uint32_t offset = addr & 0x3fff;
    uint8_t *window = NULL;

    if (jride_should_log_access(offset))
        xtide_log(xtide->log, "[%04X:%08X] [W] %04X = %02X\n", CS, cpu_state.pc, offset, val);

    if (offset < JRIDE_DATA_WINDOW_OFFSET) {
        /* 0x0000-0x39FF: Flash ROM BIOS code - only writable when flash write is enabled. */
#if 0
        if (xtide->flash_write_enable)
            xtide->bios_rom.rom[offset] = val;
#endif
        return;
    } else if (offset < JRIDE_CS0_OFFSET) {
        /* 0x3A00-0x3BFF: JRIDE exposes the current 512-byte PIO buffer as memory. */
        window = jride_get_data_window(xtide);
        if (window != NULL) {
            window[offset - JRIDE_DATA_WINDOW_OFFSET] = val;
            if (offset == (JRIDE_DATA_WINDOW_OFFSET + JRIDE_DATA_WINDOW_SIZE - 1))
                ide_complete_pio_buffer_write(xtide->ide_board);
        }
    } else if (offset <= 0x3c07) {
        /* 0x3C00-0x3C07: IDE CS0 - command block registers 0-7. */
        ide_writeb(offset & 0x7, val, xtide->ide_board);
    } else if (offset <= 0x3c0f) {
        /* 0x3C08-0x3C0F: IDE CS1 - device control / alternate status. */
        ide_write_devctl(0, val, xtide->ide_board);
    } else if (offset == JRIDE_FILL_ENABLE_OFFSET) {
        /* RAM fill enable register - not actively used; ignore writes.
         * Bit	Memory Range	Notes
         * 0	RAM fill enable 0xC0000 -> 0xC3FFF
         * 1	RAM fill enable 0xC4000 -> 0xC7FFF
         * 2	RAM fill enable 0xC8000 -> 0xCBFFF
         * 3	RAM fill enable 0xCC000 -> 0xCFFFF
         * 4	RAM fill enable 0xD0000 -> 0xD7FFF
         * 5	RAM fill enable 0xD8000 -> 0xDFFFF
         * 6	RAM fill enable 0xE0000 -> 0xE7FFF
         * 7	RAM fill enable 0xE8000 -> 0xEFFFF
         */
        return;
    } else if (offset == JRIDE_WINDOW_ENABLE_OFFSET) {
        /* ROM window enable - not actively used; ignore writes.
         *  Bit	Description
         *  0	Target page bit 0 (A14)
         *  1	Target page bit 1 (A15)
         *  2	Target page bit 2 (A16)
         *  3	Target page bit 3 (A17)
         *  4	Target page bit 4 (A18)
         *  5	ROM window address (A14 comparison)
         *  6	ROM window address (A15 comparison)
         *  7	ROM window enable (1) / disable (0)
         */
        return;
    } else {
        /* 0x3C12+: Scratch RAM. */
        xtide->jride_scratch[offset - JRIDE_SCRATCH_OFFSET] = val;
    }
}

static uint8_t
jride_read(uint32_t addr, void *priv)
{
    xtide_t       *xtide  = (xtide_t *) priv;
    uint32_t       offset = addr & 0x3fff;
    uint8_t        ret    = 0xff;
    const uint8_t *window = NULL;

    if (!xtide->ide_board) {
        if (offset < JRIDE_DATA_WINDOW_OFFSET)
            ret = xtide->bios_rom.rom[offset];
        else if (offset >= JRIDE_SCRATCH_OFFSET)
            ret = xtide->jride_scratch[offset - JRIDE_SCRATCH_OFFSET];
        else
            ret = 0xff;

        if (jride_should_log_access(offset))
            xtide_log(xtide->log, "[%04X:%08X] [R] %04X = %02X\n", CS, cpu_state.pc, offset, ret);
        return ret;
    }

    if (offset < JRIDE_DATA_WINDOW_OFFSET) {
        /* 0x0000-0x39FF: Option ROM BIOS code. */
        ret = xtide->bios_rom.rom[offset];
    } else if (offset < JRIDE_CS0_OFFSET) {
        /* 0x3A00-0x3BFF: JRIDE exposes the current 512-byte PIO buffer as memory. */
        window = jride_get_data_window(xtide);
        ret    = (window != NULL) ? window[offset - JRIDE_DATA_WINDOW_OFFSET] : 0xff;
    } else if (offset <= 0x3c07) {
        /* 0x3C00-0x3C07: IDE CS0 - command block registers 0-7. */
        ret = ide_readb(offset & 0x7, xtide->ide_board);
    } else if (offset <= 0x3c0f) {
        /* 0x3C08-0x3C0F: IDE CS1 - alternate status. */
        ret = ide_read_alt_status(0, xtide->ide_board);
    } else if (offset == JRIDE_FILL_ENABLE_OFFSET) {
        /* RAM fill enable register - returns 0. */
        ret = 0x00;
    } else if (offset == JRIDE_WINDOW_ENABLE_OFFSET) {
        /* ROM window enable - returns 0 */
        ret = 0x00;
    } else {
        /* 0x3C12+: Scratch RAM. */
        ret = xtide->jride_scratch[offset - JRIDE_SCRATCH_OFFSET];
    }

    if (offset == (JRIDE_DATA_WINDOW_OFFSET + JRIDE_DATA_WINDOW_SIZE - 1)) {
        ide_complete_pio_buffer_read(xtide->ide_board);
    }

    if (jride_should_log_access(offset))
        xtide_log(xtide->log, "[%04X:%08X] [R] %04X = %02X\n", CS, cpu_state.pc, offset, ret);

    return ret;
}

static void
jride_close(void *priv)
{
    xtide_t *xtide = (xtide_t *) priv;

    if (!xtide)
        return;

    mem_mapping_disable(&xtide->jride_window_mapping);
    mem_mapping_disable(&xtide->bios_rom.mapping);

    if (xtide->ide_board)
        ide_xtide_close();

    if (xtide->log)
        log_close(xtide->log);

    free(xtide);
}

static void *
jride_init(const device_t *info)
{
    if (device_get_instance() > 1)
        return NULL;

    xtide_t *xtide     = calloc(1, sizeof(xtide_t));
    uint32_t bios_addr = device_get_config_hex20("bios_addr");
    const char *bios_sel = device_get_config_bios("bios");
    const char *bios_file = device_get_bios_file(info, bios_sel, 0);

    xtide->log = log_open("XTIDE-jrIDE");

    /* Load the 8KB flash ROM image.  rom_init() adds the mapping to the global list
     * with read-only handlers; we then replace those handlers with the jrIDE-specific
     * ones that interleave ROM, IDE registers, and scratch RAM in the same window. */
    if (rom_init(&xtide->bios_rom,
                 bios_file,
                 bios_addr, JRIDE_ROM_WINDOW_SIZE, (JRIDE_ROM_WINDOW_SIZE - 1), 0, MEM_MAPPING_EXTERNAL) != 0) {
        xtide_log(xtide->log, "Rom_init failed for %s\n", bios_file ? bios_file : "(null)");
    }

    mem_mapping_set_addr(&xtide->bios_rom.mapping, bios_addr, JRIDE_ROM_WINDOW_SIZE); // 14.5k
    mem_mapping_set_handler(&xtide->bios_rom.mapping,
                            jride_read, NULL, NULL,
                            jride_write, NULL, NULL);
    mem_mapping_set_p(&xtide->bios_rom.mapping, xtide);

    mem_mapping_add(&xtide->jride_window_mapping,
                    bios_addr + JRIDE_ROM_WINDOW_SIZE, JRIDE_NON_ROM_WINDOW_SIZE,
                    jride_read, NULL, NULL,
                    jride_write, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, xtide);

#if 0
    xtide->flash_write_enable = device_get_config_int("flash_write_enabled");
#endif

    xtide->ide_board = ide_xtide_init();
    ide_xtide_set_is_jride(1);

    return xtide;
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

static const device_config_t jride_config[] = {
    {
        .name           = "bios",
        .description    = "BIOS Revision",
        .type           = CONFIG_BIOS,
        .default_string = "jride",
        .default_int    = 0,
        .bios           = {
            {
                .name          = "JR-IDE Universal BIOS",
                .internal_name = "jride",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .size          = 8192,
                .files         = { ROM_PATH_JRIDE, "" }
            },
            { .files_no = 0 }
        },
    },
    {
        .name           = "bios_addr",
        .description    = "BIOS Address",
        .type           = CONFIG_HEX20,
        .default_int    = 0xc0000,
        .selection      = {
#if 0
            { .description = "Disabled", .value = 0x00000 },
#endif
            { .description = "C0000H",   .value = 0xc0000 },
            { .description = "C4000H",   .value = 0xc4000 },
            { .description = "C8000H",   .value = 0xc8000 },
            { .description = "CC000H",   .value = 0xcc000 },
            { .description = ""                           }
        }
    },
#if 0
    {
        .name           = "flash_write_enabled",
        .description    = "Allow Flash BIOS Updates",
        .type           = CONFIG_BINARY,
        .default_int    = 0
    },
#endif
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
    .name          = "Acculogic sIDE-1/16 (IDE)",
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

const device_t jride_device = {
    .name          = "jr-IDE",
    .internal_name = "jride",
    .flags         = DEVICE_SIDECAR,
    .local         = 0,
    .init          = jride_init,
    .close         = jride_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = jride_config
};
