/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          C/MS emulation.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2024-2026 Jasmine Iwanek.
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
#include <86box/sound.h>
#include <86box/snd_cms.h>
#include <86box/sound.h>
#include <86box/plat_unused.h>

#ifdef ENABLE_CMS_LOG
uint8_t cms_do_log = ENABLE_CMS_LOG;

static void
cms_log(const char *fmt, ...)
{
    va_list ap;

    if (cms_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define cms_log(fmt, ...)
#endif

void
cms_update(cms_t *cms)
{
    for (; cms->pos < sound_pos_global; cms->pos++) {
        int16_t out_l = 0;
        int16_t out_r = 0;

        for (uint8_t c = 0; c < 4; c++) {
            switch (cms->noisetype[c >> 1][c & 1]) {
                case 0:
                    cms->noisefreq[c >> 1][c & 1] = CMS_MASTER_CLOCK / 256;
                    break;
                case 1:
                    cms->noisefreq[c >> 1][c & 1] = CMS_MASTER_CLOCK / 512;
                    break;
                case 2:
                    cms->noisefreq[c >> 1][c & 1] = CMS_MASTER_CLOCK / 1024;
                    break;
                case 3:
                    cms->noisefreq[c >> 1][c & 1] = cms->freq[c >> 1][(c & 1) * 3];
                    break;

                default:
                    break;
            }
        }
        for (uint8_t c = 0; c < 2; c++) {
            if (cms->regs[c][0x1C] & 1) {
                for (uint8_t d = 0; d < 6; d++) {
                    if (cms->regs[c][0x14] & (1 << d)) {
                        if (cms->stat[c][d])
                            out_l += (cms->vol[c][d][0] * 90);
                        if (cms->stat[c][d])
                            out_r += (cms->vol[c][d][1] * 90);
                        cms->count[c][d] += cms->freq[c][d];
                        if (cms->count[c][d] >= 24000) {
                            cms->count[c][d] -= 24000;
                            cms->stat[c][d] ^= 1;
                        }
                    } else if (cms->regs[c][0x15] & (1 << d)) {
                        if (cms->noise[c][d / 3] & 1)
                            out_l += (cms->vol[c][d][0] * 90);
                        if (cms->noise[c][d / 3] & 1)
                            out_r += (cms->vol[c][d][0] * 90);
                    }
                }
                for (uint8_t d = 0; d < 2; d++) {
                    cms->noisecount[c][d] += cms->noisefreq[c][d];
                    while (cms->noisecount[c][d] >= 24000) {
                        cms->noisecount[c][d] -= 24000;
                        cms->noise[c][d] <<= 1;
                        if (!(((cms->noise[c][d] & 0x4000) >> 8) ^ (cms->noise[c][d] & 0x40)))
                            cms->noise[c][d] |= 1;
                    }
                }
            }
        }
        cms->buffer[cms->pos << 1]       = out_l;
        cms->buffer[(cms->pos << 1) + 1] = out_r;
    }
}

void
cms_get_buffer(int32_t *buffer, const int len, void *priv)
{
    cms_t *const cms = (cms_t *) priv;

    cms_update(cms);

    for (int c = 0; c < len * 2; c++)
        buffer[c] += cms->buffer[c];

    cms->pos = 0;
}

void
cms_write(uint16_t port, uint8_t val, void *priv)
{
    cms_t        *const cms = (cms_t *) priv;
    uint8_t       voice;
    const uint8_t chip = (port & 2) >> 1;

    cms_log("cms_write: port=%04x val=%02x\n", port, val);

    switch (port & 0x0f) {
        case 0x01: /* SAA #1 Register Select Port */
            cms->ports[0] = val & 31;
            break;
        case 0x03: /* SAA #2 Register Select Port */
            cms->ports[1] = val & 31;
            break;

        case 0x00: /* SAA #1 Data Port */
        case 0x02: /* SAA #2 Data Port */
            cms_update(cms);
            {
            const uint8_t reg = cms->ports[chip] & 31;
            cms->regs[chip][cms->ports[chip] & 31] = val;
            switch (reg) {
                case 0x00 ... 0x05: /*Volume*/
//                    voice                    = cms->ports[chip] & 7;
                    voice                    = reg & 7;
                    cms->vol[chip][voice][0] = val & 0x0f;
                    cms->vol[chip][voice][1] = val >> 4;
                    break;
                case 0x08 ... 0x0D: /*Frequency*/
//                    voice                   = cms->ports[chip] & 7;
                    voice                    = reg & 7;
                    cms->latch[chip][voice] = (cms->latch[chip][voice] & 0x700) | val;
                    cms->freq[chip][voice]  = (CMS_MASTER_CLOCK / 512 << (cms->latch[chip][voice] >> 8)) / (511 - (cms->latch[chip][voice] & 255));
                    break;
                case 0x10 ... 0x12: /*Octave*/
                    voice                       = (reg & 3) << 1;
                    cms->latch[chip][voice]     = (cms->latch[chip][voice] & 0xFF) | ((val & 7) << 8);
                    cms->latch[chip][voice + 1] = (cms->latch[chip][voice + 1] & 0xFF) | ((val & 0x70) << 4);
                    cms->freq[chip][voice]      = (CMS_MASTER_CLOCK / 512 << (cms->latch[chip][voice] >> 8)) / (511 - (cms->latch[chip][voice] & 255));
                    cms->freq[chip][voice + 1]  = (CMS_MASTER_CLOCK / 512 << (cms->latch[chip][voice + 1] >> 8)) / (511 - (cms->latch[chip][voice + 1] & 255));
                    break;
                case 0x16: /*Noise*/
                    cms->noisetype[chip][0] = val & 3;
                    cms->noisetype[chip][1] = (val >> 4) & 3;
                    break;

                default:
                    break;
            }
            }
            break;

        case 0x06: /* GameBlaster Write Port */
        case 0x07: /* GameBlaster Write Port */
            cms->latched_data = val;
            break;

        default:
            break;
    }
}

uint8_t
cms_read(const uint16_t port, void *priv)
{
    const cms_t *cms = (cms_t *) priv;
    uint8_t      ret = 0xff;

    switch (port & 0x0f) {
        case 0x01: /* SAA #1 Register Select Port */
            ret = cms->ports[0];
            break;
        case 0x03: /* SAA #2 Register Select Port */
            ret = cms->ports[1];
            break;
        case 0x04: /* GameBlaster Read port (Always returns 0x7F) */
            ret = 0x7f;
            break;
        case 0x0a: /* GameBlaster Read Port */
        case 0x0b: /* GameBlaster Read Port */
            ret = cms->latched_data;
            break;

        default:
            break;
    }

    cms_log("cms_read: port=%04x ret=%02x\n", port, ret);

    return ret;
}

void *
cms_init(UNUSED(const device_t *info))
{
    cms_t *const cms = calloc(1, sizeof(cms_t));

    cms_log("cms_init\n");

    uint16_t port = device_get_config_hex16("base");

    io_sethandler(port, 0x0010,
                  cms_read, NULL, NULL,
                  cms_write, NULL, NULL,
                  cms);

    sound_add_handler(cms_get_buffer, cms);

    return cms;
}

void
cms_close(void *priv)
{
    cms_t *const cms = (cms_t *) priv;

    cms_log("cms_close\n");

    free(cms);
}

static const device_config_t cms_config[] = {
  // clang-format off
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x220,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x210", .value = 0x210 },
            { .description = "0x220", .value = 0x220 },
            { .description = "0x230", .value = 0x230 },
            { .description = "0x240", .value = 0x240 },
            { .description = "0x250", .value = 0x250 },
            { .description = "0x260", .value = 0x260 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t cms_device = {
    .name          = "Creative Music System / Game Blaster",
    .internal_name = "cms",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = cms_init,
    .close         = cms_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = cms_config
};
