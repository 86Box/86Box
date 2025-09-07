/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Mindscape Music Board emulation.
 *
 * Authors: Roy Baer, <https://pcem-emulator.co.uk/>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025 Roy Baer.
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
#include <86box/sound.h>
//#i nclude "cpu.h"
#include "ayumi/ayumi.h"
#include <86box/snd_mmb.h>
#include <86box/plat_unused.h>

#ifdef ENABLE_MMB_LOG
int mmb_do_log = ENABLE_MMB_LOG;

static void
mmb_log(const char *fmt, ...)
{
    va_list ap;

    if (mmb_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define mmb_log(fmt, ...)
#endif

void
mmb_update(mmb_t *mmb)
{
    for (; mmb->pos < sound_pos_global; mmb->pos++) {
        ayumi_process(&mmb->first.chip);
        ayumi_process(&mmb->second.chip);

        ayumi_remove_dc(&mmb->first.chip);
        ayumi_remove_dc(&mmb->second.chip);

        mmb->buffer[mmb->pos << 1]       = (mmb->first.chip.left + mmb->second.chip.left) * 16000;
        mmb->buffer[(mmb->pos << 1) + 1] = (mmb->first.chip.right + mmb->second.chip.right) * 16000;
    }
}

void
mmb_get_buffer(int32_t *buffer, int len, void *priv)
{
    mmb_t *mmb = (mmb_t *) priv;

    mmb_update(mmb);

    for (int c = 0; c < len * 2; c++)
        buffer[c] += mmb->buffer[c];

    mmb->pos = 0;
}

void
mmb_write(uint16_t addr, uint8_t val, void *priv)
{
    mmb_t *mmb = (mmb_t *) priv;

    mmb_update(mmb);

    mmb_log("mmb_write(%04X): activity now: %02X\n", addr, val);

    switch (addr & 3) {
        case 0:
            mmb->first.index = val;
            break;
        case 2:
            mmb->second.index = val;
            break;
        case 1:
        case 3:
            {
                ay_3_891x_t *ay = ((addr & 2) == 0) ? &mmb->first : &mmb->second;

                switch (ay->index) {
                    case 0:
                        ay->regs[0] = val;
                        ayumi_set_tone(&ay->chip, 0, (ay->regs[1] << 8) | ay->regs[0]);
                        break;
                    case 1:
                        ay->regs[1] = val & 0xf;
                        ayumi_set_tone(&ay->chip, 0, (ay->regs[1] << 8) | ay->regs[0]);
                        break;
                    case 2:
                        ay->regs[2] = val;
                        ayumi_set_tone(&ay->chip, 1, (ay->regs[3] << 8) | ay->regs[2]);
                        break;
                    case 3:
                        ay->regs[3] = val & 0xf;
                        ayumi_set_tone(&ay->chip, 1, (ay->regs[3] << 8) | ay->regs[2]);
                        break;
                    case 4:
                        ay->regs[4] = val;
                        ayumi_set_tone(&ay->chip, 2, (ay->regs[5] << 8) | ay->regs[4]);
                        break;
                    case 5:
                        ay->regs[5] = val & 0xf;
                        ayumi_set_tone(&ay->chip, 2, (ay->regs[5] << 8) | ay->regs[4]);
                        break;
                    case 6:
                        ay->regs[6] = val & 0x1f;
                        ayumi_set_noise(&ay->chip, ay->regs[6]);
                        break;
                    case 7:
                        ay->regs[7] = val;
                        ayumi_set_mixer(&ay->chip, 0, val & 1, (val >> 3) & 1, (ay->regs[8] >> 4) & 1);
                        ayumi_set_mixer(&ay->chip, 1, (val >> 1) & 1, (val >> 4) & 1, (ay->regs[9] >> 4) & 1);
                        ayumi_set_mixer(&ay->chip, 2, (val >> 2) & 1, (val >> 5) & 1, (ay->regs[10] >> 4) & 1);
                        break;
                    case 8:
                        ay->regs[8] = val;
                        ayumi_set_volume(&ay->chip, 0, val & 0xf);
                        ayumi_set_mixer(&ay->chip, 0, ay->regs[7] & 1, (ay->regs[7] >> 3) & 1, (val >> 4) & 1);
                        break;
                    case 9:
                        ay->regs[9] = val;
                        ayumi_set_volume(&ay->chip, 1, val & 0xf);
                        ayumi_set_mixer(&ay->chip, 1, (ay->regs[7] >> 1) & 1, (ay->regs[7] >> 4) & 1, (val >> 4) & 1);
                        break;
                    case 10:
                        ay->regs[10] = val;
                        ayumi_set_volume(&ay->chip, 2, val & 0xf);
                        ayumi_set_mixer(&ay->chip, 2, (ay->regs[7] >> 2) & 1, (ay->regs[7] >> 5) & 1, (val >> 4) & 1);
                        break;
                    case 11:
                        ay->regs[11] = val;
                        ayumi_set_envelope(&ay->chip, (ay->regs[12] >> 8) | ay->regs[11]);
                        break;
                    case 12:
                        ay->regs[12] = val;
                        ayumi_set_envelope(&ay->chip, (ay->regs[12] >> 8) | ay->regs[11]);
                        break;
                    case 13:
                        ay->regs[13] = val;
                        ayumi_set_envelope_shape(&ay->chip, val & 0xf);
                        break;
                    case 14:
                        ay->regs[14] = val;
                        break;
                    case 15:
                        ay->regs[15] = val;
                        break;

                    default:
                        break;
                }
                break;
            }

        default:
            break;
    }
}

uint8_t
mmb_read(uint16_t addr, void *priv)
{
    mmb_t       *mmb = (mmb_t *) priv;
    ay_3_891x_t *ay  = ((addr & 2) == 0) ? &mmb->first : &mmb->second;
    uint8_t      ret = 0;

    switch (ay->index) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
        case 13:
            ret = ay->regs[ay->index];
            break;
        case 14:
            if (ay->regs[7] & 0x40)
                ret = ay->regs[14];
            break;
        case 15:
            if (ay->regs[7] & 0x80)
                ret = ay->regs[15];
            break;

        default:
            break;
    }

    mmb_log("mmb_read(%04X): activity now: %02X\n", addr, ret);

    return ret;
}

void *
mmb_init(UNUSED(const device_t *info))
{
    mmb_t   *mmb  = calloc(1, sizeof(mmb_t));
# if 0
    uint16_t addr = (device_get_config_int("addr96") << 6) | (device_get_config_int("addr52") << 2);
#else
    uint16_t addr = 0x300;

#endif
    sound_add_handler(mmb_get_buffer, mmb);

    ayumi_configure(&mmb->first.chip, 0, MMB_CLOCK, MMB_FREQ);
    ayumi_configure(&mmb->second.chip, 0, MMB_CLOCK, MMB_FREQ);

    for (uint8_t i = 0; i < 3; i++) {
        ayumi_set_pan(&mmb->first.chip, i, 0.5, 1);
        ayumi_set_pan(&mmb->second.chip, i, 0.5, 1);
    }

    io_sethandler(addr, 0x0004,
                  mmb_read, NULL, NULL,
                  mmb_write, NULL, NULL,
                  mmb);

    return mmb;
}

void
mmb_close(void *priv)
{
    mmb_t *mmb = (mmb_t *) priv;

    free(mmb);
}

// clang-format off
#if 0
static device_config_t mmb_config[] = {
    {
        .name           = "addr96",
        .description    = "Base address A9...A6",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 12,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection   = {
            { .description = "0000", .value =  0 },
            { .description = "0001", .value =  1 },
            { .description = "0010", .value =  2 },
            { .description = "0011", .value =  3 },
            { .description = "0100", .value =  4 },
            { .description = "0101", .value =  5 },
            { .description = "0110", .value =  6 },
            { .description = "0111", .value =  7 },
            { .description = "1000", .value =  8 },
            { .description = "1001", .value =  9 },
            { .description = "1010", .value = 10 },
            { .description = "1011", .value = 11 },
            { .description = "1100", .value = 12 },
            { .description = "1101", .value = 13 },
            { .description = "1110", .value = 14 },
            { .description = "1111", .value = 15 },
            { .description = ""                  }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "addr52",
        .description    = "Base address A5...A2",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection   = {
            { .description = "0000", .value =  0 },
            { .description = "0001", .value =  1 },
            { .description = "0010", .value =  2 },
            { .description = "0011", .value =  3 },
            { .description = "0100", .value =  4 },
            { .description = "0101", .value =  5 },
            { .description = "0110", .value =  6 },
            { .description = "0111", .value =  7 },
            { .description = "1000", .value =  8 },
            { .description = "1001", .value =  9 },
            { .description = "1010", .value = 10 },
            { .description = "1011", .value = 11 },
            { .description = "1100", .value = 12 },
            { .description = "1101", .value = 13 },
            { .description = "1110", .value = 14 },
            { .description = "1111", .value = 15 },
            { .description = ""                  }
        },
        .bios           = { { 0 } }
    },
    { .type = CONFIG_END }
};
#endif
// clang-format on

const device_t mmb_device = {
    .name          = "Mindscape Music Board",
    .internal_name = "mmb",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = mmb_init,
    .close         = mmb_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
#if 0
    .config        = mmb_config
#else
    .config        = NULL
#endif
};
