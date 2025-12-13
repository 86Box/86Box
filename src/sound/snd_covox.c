/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Rainbow Arts PC-Soundman Emulation
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025 Jasmine Iwanek.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mca.h>
#include <86box/sound.h>
#include <86box/filters.h>
#include <86box/timer.h>
#include <86box/snd_opl.h>
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>

#define  COVOX_SOUNDMAN        0
#define  COVOX_VOICEMASTERKEY  1
#define  COVOX_SOUNDMASTERPLUS 2
#define  COVOX_ISADACR0        3
#define  COVOX_ISADACR1        4

#ifdef ENABLE_COVOX_LOG
int covox_do_log = ENABLE_COVOX_LOG;

static void
covox_log(const char *fmt, ...)
{
    va_list ap;

    if (covox_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define covox_log(fmt, ...)
#endif

typedef struct covox_s {
    fm_drv_t opl;

    uint8_t  dac_val;

    int16_t  buffer[2][SOUNDBUFLEN];
    int      pos;
} covox_t;

// TODO: Can this be rolled into covox_get_buffer?
static void
covox_update(covox_t *covox)
{
    for (; covox->pos < sound_pos_global; covox->pos++) {
        covox->buffer[0][covox->pos] = (int8_t) (covox->dac_val ^ 0x80) * 0x40;
        covox->buffer[1][covox->pos] = (int8_t) (covox->dac_val ^ 0x80) * 0x40;
    }
}

uint8_t
covox_read(uint16_t addr, void *priv)
{
#if 0
    const covox_t *covox = (covox_t *) priv;
#endif

    covox_log("covox_read: addr=%04x\n", addr);

    return 0xff;
}

void
covox_write(uint16_t addr, uint8_t val, void *priv)
{
    covox_t *covox = (covox_t *) priv;

    covox_log("covox_write: addr=%04x val=%02x\n", addr, val);

    switch (addr) {
        case 0x221: // Soundman
        case 0x229: // Soundman
        case 0x22f: // Soundman, voicemasterkey soundmasterplus
        case 0x231: //                                           isadac-r1?
        case 0x24f: //           voicemasterkey soundmasterplus
        case 0x279: //                                           isadac-r0 (lPT2)
        case 0x28f: //           voicemasterkey
        case 0x2cf: //           voicemasterkey
        case 0x301: // Soundman
        case 0x309: // Soundman
        case 0x30f: // soundman
        case 0x331: //
        case 0x339: //
        case 0x371: //                                           isadac-r0
        case 0x379: //                                           isadac-r0 (lPT1)
        case 0x381: //                                           isadac-r0
        case 0x3bd: //                                           isadac-r0 (lPT1-Mono)
            covox->dac_val = val;
            // TODO: Is this needed here?
            covox_update(covox);
            break;

        default:
            break;
    }
}

static void
covox_get_buffer(int32_t *buffer, int len, void *priv)
{
    covox_t *covox = (covox_t *) priv;

    covox_update(covox);

    for (int c = 0; c < len; c++) {
        buffer[c * 2]     += dac_iir(0, covox->buffer[0][c]);
        buffer[c * 2 + 1] += dac_iir(1, covox->buffer[1][c]);
    }
    covox->pos = 0;
}

static void
covox_get_music_buffer(int32_t *buffer, int len, void *priv)
{
    covox_t *covox = (covox_t *) priv;

    const int32_t *opl_buf = covox->opl.update(covox->opl.priv);

    for (int c = 0; c < len * 2; c++)
        buffer[c] += opl_buf[c];

    if (covox->opl.reset_buffer)
        covox->opl.reset_buffer(covox->opl.priv);
}

#define IO_SETHANDLER_COVOX_DAC(addr, len) \
    io_sethandler((addr), (len),           \
                  covox_read, NULL, NULL,  \
                  covox_write, NULL, NULL, \
                  covox)

#define IO_SETHANDLER_COVOX_ADLIB(addr, len)    \
    io_sethandler((addr), (len),                \
                  covox->opl.read, NULL, NULL,  \
                  covox->opl.write, NULL, NULL, \
                  covox->opl.priv)

void *
covox_init(UNUSED(const device_t *info))
{
    covox_t *covox         = calloc(1, sizeof(covox_t));
    uint8_t  has_adlib     = 0;
    uint8_t  has_stereo    = 0;
    uint8_t  fixed_address = 0;
    if (!covox)
        return NULL;

    covox_log("covox_init\n");
    switch (info->local) {
        case COVOX_SOUNDMAN:
            fixed_address = 1;
            fallthrough;
        case COVOX_SOUNDMASTERPLUS:
            has_adlib = 1;
            break;

        case COVOX_ISADACR0:
            has_stereo = 1;
            break;

        case COVOX_ISADACR1:
            has_stereo = 2;
            break;

        default:
            break;
    }

    if (fixed_address) {
        IO_SETHANDLER_COVOX_DAC(0x220, 0x0002);
        IO_SETHANDLER_COVOX_DAC(0x228, 0x0002);
        IO_SETHANDLER_COVOX_DAC(0x22e, 0x0002);
#if 0
        // According to vgmpf, this is the address
        IO_SETHANDLER_COVOX_DAC(0x22f, 0x0001);
#endif
        IO_SETHANDLER_COVOX_DAC(0x300, 0x0002);
        IO_SETHANDLER_COVOX_DAC(0x308, 0x0002);
        IO_SETHANDLER_COVOX_DAC(0x30e, 0x0002);
    } else {
        IO_SETHANDLER_COVOX_DAC(device_get_config_hex16("base"), 0x0002);

        // TODO: Needs more work
        if (has_stereo)
            IO_SETHANDLER_COVOX_DAC(device_get_config_hex16("base2"), 0x0002);
    }
    sound_add_handler(covox_get_buffer, covox);

    if (has_adlib) {
        fm_driver_get(FM_YM3812, &covox->opl);
        if (fixed_address) {
            // Adlib Clone part
            IO_SETHANDLER_COVOX_ADLIB(0x380, 0x0002);
            IO_SETHANDLER_COVOX_ADLIB(0x388, 0x0002);
            IO_SETHANDLER_COVOX_ADLIB(0x38e, 0x0002);
        } else
            IO_SETHANDLER_COVOX_ADLIB(device_get_config_hex16("adlibbase"), 0x0002);

        music_add_handler(covox_get_music_buffer, covox);
    }

    return covox;
}

void
covox_close(void *priv)
{
    covox_t *covox = (covox_t *) priv;

    if (covox)
        free(covox);
}

// clang-format off
static const device_config_t voicemasterkey_config[] = {
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x22f,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x22f", .value = 0x22f },
            { .description = "0x24f", .value = 0x24f },
            { .description = "0x28f", .value = 0x28f },
            { .description = "0x2cf", .value = 0x2cf },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

// Note: We don't support sound input on this yet
const device_t voicemasterkey_device = {
    .name          = "Covox Voice Master Key",
    .internal_name = "voicemasterkey",
    .flags         = DEVICE_ISA | DEVICE_SIDECAR,
    .local         = COVOX_VOICEMASTERKEY,
    .init          = covox_init,
    .close         = covox_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = voicemasterkey_config
};

// clang-format off
static const device_config_t soundmasterplus_config[] = {
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x22e,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x22e", .value = 0x22e },
            { .description = "0x24e", .value = 0x24e },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "adlibbase",
        .description    = "Adlib Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x388,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x388", .value = 0x388 },
            { .description = "0x380", .value = 0x380 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t soundmasterplus_device = {
    .name          = "Covox Sound Master Plus",
    .internal_name = "soundmasterplus",
    .flags         = DEVICE_ISA | DEVICE_SIDECAR,
    .local         = COVOX_SOUNDMASTERPLUS,
    .init          = covox_init,
    .close         = covox_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = soundmasterplus_config
};

// clang-format off
static const device_config_t isadacr0_config[] = {
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x380,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x220", .value = 0x220 },
            { .description = "0x228", .value = 0x228 },
            { .description = "0x22e", .value = 0x22e },
            { .description = "0x230", .value = 0x230 },
            { .description = "0x24e", .value = 0x24e },
            { .description = "0x278", .value = 0x278 },
            { .description = "0x28e", .value = 0x28e },
            { .description = "0x2ce", .value = 0x2ce },
            { .description = "0x300", .value = 0x300 },
            { .description = "0x308", .value = 0x308 },
            { .description = "0x303", .value = 0x30e },
            { .description = "0x330", .value = 0x330 },
            { .description = "0x338", .value = 0x338 },
            { .description = "0x370", .value = 0x370 },
            { .description = "0x378", .value = 0x378 },
            { .description = "0x380", .value = 0x380 },
            { .description = "0x3bc", .value = 0x3bc },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "base2",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x370,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x220", .value = 0x220 },
            { .description = "0x228", .value = 0x228 },
            { .description = "0x22e", .value = 0x22e },
            { .description = "0x230", .value = 0x230 },
            { .description = "0x24e", .value = 0x24e },
            { .description = "0x278", .value = 0x278 },
            { .description = "0x28e", .value = 0x28e },
            { .description = "0x2ce", .value = 0x2ce },
            { .description = "0x300", .value = 0x300 },
            { .description = "0x308", .value = 0x308 },
            { .description = "0x303", .value = 0x30e },
            { .description = "0x330", .value = 0x330 },
            { .description = "0x338", .value = 0x338 },
            { .description = "0x370", .value = 0x370 },
            { .description = "0x378", .value = 0x378 },
            { .description = "0x380", .value = 0x380 },
            { .description = "0x3bc", .value = 0x3bc },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

// Note: We don't support stereo on this yet
const device_t isadacr0_device = {
    .name          = "ISA DAC-r0",
    .internal_name = "isadacr0",
    .flags         = DEVICE_ISA | DEVICE_SIDECAR,
    .local         = COVOX_ISADACR0,
    .init          = covox_init,
    .close         = covox_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = isadacr0_config
};

// clang-format off
static const device_config_t isadacr1_config[] = {
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x378,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x378", .value = 0x378 },
            { .description = "0x3bc", .value = 0x3bc },
            { .description = "0x278", .value = 0x278 },
            { .description = "0x230", .value = 0x230 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "base2",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x278,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x378", .value = 0x378 },
            { .description = "0x3bc", .value = 0x3bc },
            { .description = "0x278", .value = 0x278 },
            { .description = "0x230", .value = 0x230 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

// Note: We don't support stereo on this yet
const device_t isadacr1_device = {
    .name          = "ISA DAC-r1",
    .internal_name = "isadacr1",
    .flags         = DEVICE_ISA | DEVICE_SIDECAR,
    .local         = COVOX_ISADACR1,
    .init          = covox_init,
    .close         = covox_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = isadacr1_config
};

const device_t soundman_device = {
    .name          = "Rainbow Arts PC-Soundman",
    .internal_name = "soundman",
    .flags         = DEVICE_ISA | DEVICE_SIDECAR,
    .local         = COVOX_SOUNDMAN,
    .init          = covox_init,
    .close         = covox_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
