/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          TexElec Saaym Emulation.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *          win2kgamer
 *
 *          Copyright 2024-2026 Jasmine Iwanek.
 *          Copyright 2026 win2kgamer
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/snd_cms.h>
#include <86box/snd_saaym.h>
#include <86box/sound.h>
#include <86box/plat_unused.h>

#ifdef ENABLE_SAAYM_LOG
int saaym_do_log = ENABLE_SAAYM_LOG;

static void
saaym_log(void *priv, const char *fmt, ...)
{
    if (saaym_do_log) {
        va_list ap;
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define saaym_log(fmt, ...)
#endif

void
saaym_write(uint16_t addr, uint8_t val, void *priv)
{
    saaym_t *saaym = (saaym_t *) priv;

    switch (addr & 0xf) {
        case 0xe: /* YM2151 Register Select Port */
            saaym->opm.write(addr, val, saaym->opm.priv);
            break;

        case 0xf: /* YM2151 Data Port */
            saaym->opm.write(addr, val, saaym->opm.priv);
            break;

        case 0x8: /* SAAYM Write Port */
        case 0x9: /* SAAYM Write Port */
            saaym->latched_data = val;
            break;

        default:
            break;
    }

    saaym_log(saaym->log, "SAAYM write: port = %04X, val = %02X\n", addr, val);
}

uint8_t
saaym_read(uint16_t addr, void *priv)
{
    const saaym_t *saaym = (saaym_t *) priv;
    uint8_t ret = 0xff;

    switch (addr & 0xf) {
        case 0xc: /* SAAYM Read Port */
        case 0xd: /* SAAYM Read Port */
            ret = saaym->latched_data;
            break;
        case 0xe: /* YM2151 Status Port */
            ret = saaym->opm.read(addr + 1, saaym->opm.priv);
            break;

        default:
            break;
    }
    saaym_log(saaym->log, "SAAYM read: port = %04X, ret = %02X\n", addr, ret);
    return ret;
}

static void
saaym_get_music_buffer(int32_t *buffer, int len, void *priv)
{
    const saaym_t *saaym   = (const saaym_t *) priv;
    const int32_t *opm_buf = NULL;

    opm_buf = saaym->opm.update(saaym->opm.priv);

    for (int c = 0; c < len * 2; c += 2) {
        double out_l = 0.0;
        double out_r = 0.0;

        out_l = ((double) opm_buf[c]);
        out_r = ((double) opm_buf[c + 1]);

        buffer[c] += (int32_t) out_l;
        buffer[c + 1] += (int32_t) out_r;
    }

    saaym->opm.reset_buffer(saaym->opm.priv);
}

void *
saaym_init(UNUSED(const device_t *info))
{
    saaym_t *saaym = calloc(1, sizeof(saaym_t));

    memset(&saaym->cms, 0, sizeof(cms_t));

    saaym->log = log_open("SAAYM");

    uint16_t addr = device_get_config_hex16("base");
    io_sethandler(addr, 0x0008, cms_read, NULL, NULL, cms_write, NULL, NULL, &saaym->cms);
    io_sethandler(addr + 0x08, 0x02, saaym_read, NULL, NULL, saaym_write, NULL, NULL, saaym);
    io_sethandler(addr + 0x0a, 0x02, cms_read, NULL, NULL, cms_write, NULL, NULL, &saaym->cms);
    io_sethandler(addr + 0x0c, 0x04, saaym_read, NULL, NULL, saaym_write, NULL, NULL, saaym);
    sound_add_handler(cms_get_buffer, &saaym->cms);

    /* OPM init */
    fm_driver_get(FM_YM2151, &saaym->opm);
    music_add_handler(saaym_get_music_buffer, saaym);

    return saaym;
}

void
saaym_close(void *priv)
{
    saaym_t *saaym = (saaym_t *) priv;

    if (saaym->log != NULL) {
        log_close(saaym->log);
        saaym->log = NULL;
    }

    free(saaym);
}

static const device_config_t saaym_config[] = {
  // clang-format off
    {
        .name = "base",
        .description = "Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0x220,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "0x200",
                .value = 0x200
            },
            {
                .description = "0x210",
                .value = 0x210
            },
            {
                .description = "0x220",
                .value = 0x220
            },
            {
                .description = "0x230",
                .value = 0x230
            },
            {
                .description = "0x240",
                .value = 0x240
            },
            {
                .description = "0x250",
                .value = 0x250
            },
            {
                .description = "0x260",
                .value = 0x260
            },
            {
                .description = "0x270",
                .value = 0x270
            },
            {
                .description = ""
            }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t saaym_device = {
    .name          = "TexElec SAAYM",
    .internal_name = "saaym",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = saaym_init,
    .close         = saaym_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = saaym_config
};
