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
  *          Copyright 2026 Xeon3D.
*/
#include <math.h>
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
#include "cpu.h"
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

#define COVOX_IN_RING 16384 /* host input frames held for the ADC */

/* the host's input is closed after this many 20 ms input blocks
   without a read of the ADC. */
#define COVOX_IN_IDLE_BLOCKS 50

typedef struct covox_s {
    fm_drv_t opl;

    uint8_t  dac_val;

    int16_t  buffer[2][SOUNDBUFLEN];
    int      pos;

    /* the Voice Master Key's digitizer. Reading the DAC port gives
       the conversion the previous read started and starts the next one;
       the host's input frames (mono, at in_rate) wait in in_ring and are
       taken by emulated time. */
    uint16_t adc_port;
    uint8_t  adc_val;
    int16_t  in_ring[COVOX_IN_RING];
    uint32_t in_wr;
    uint32_t in_rd;
    double   in_frac;
    int      in_rate;
    int      in_started;
    int      in_primed;
    int      in_idle;
    uint64_t in_tsc;
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

/* the host's input is opened only while the digitizer is being read. */
static void
covox_adc_input(covox_t *covox, int on)
{
    if (on && !covox->in_started) {
        covox->in_wr = covox->in_rd = 0;
        covox->in_frac              = 0.0;
        covox->in_primed            = 0;
        covox->in_idle              = 0;
        covox->in_tsc               = tsc;
        sound_in_start_input();
        covox->in_started = 1;
    } else if (!on && covox->in_started) {
        sound_in_stop_input();
        covox->in_started = 0;
    }
}

/* the host's input at this emulated moment: the frames of the time
   since the last conversion are passed over, keeping 40 ms in hand (the
   input comes in 20 ms blocks); silence while none has come. */
static int
covox_adc_input_sample(covox_t *covox)
{
#ifdef USE_DYNAREC
    /* The recompiler only brings the TSC up to date between blocks. */
    if (cpu_use_dynarec)
        update_tsc();
#endif

    const double elapsed_us = (double) (tsc - covox->in_tsc) * 4294967296.0 / (double) TIMER_USEC;

    covox->in_tsc = tsc;
    if (covox->in_rate <= 0)
        return 0;

    if (!covox->in_primed && ((covox->in_wr - covox->in_rd) >= (uint32_t) (covox->in_rate / 25)))
        covox->in_primed = 1;
    if (!covox->in_primed)
        return 0;

    covox->in_frac += (elapsed_us * (double) covox->in_rate) / 1000000.0;
    while ((covox->in_frac >= 1.0) && ((covox->in_wr - covox->in_rd) > 1)) {
        covox->in_frac -= 1.0;
        covox->in_rd++;
    }
    if (covox->in_frac >= 1.0) {
        /* Ran dry: wait for 40 ms in hand again. */
        covox->in_frac   = 0.0;
        covox->in_primed = 0;
    }

    /* Between two frames: linear interpolation, so the sampling instant is
       not rounded to the host's frame clock. */
    const int s0 = covox->in_ring[covox->in_rd % COVOX_IN_RING];
    const int s1 = covox->in_ring[(covox->in_rd + 1) % COVOX_IN_RING];

    if ((covox->in_wr - covox->in_rd) < 2)
        return s0;
    return s0 + (int) lrint((s1 - s0) * covox->in_frac);
}

/* the host's input, as sound.c hands it over (interleaved stereo at
   the capture rate), kept mono and within a quarter second of the ADC. */
static void
covox_put_buffer(int16_t *buffer, int len, void *priv)
{
    covox_t  *covox    = (covox_t *) priv;
    const int cap_rate = al_capture_get_rate();

    if (!covox->in_started)
        return;

    /* Nothing read the digitizer for a second: give the host's input back. */
    if (++covox->in_idle > COVOX_IN_IDLE_BLOCKS) {
        covox_adc_input(covox, 0);
        return;
    }

    covox->in_rate = (cap_rate > 0) ? cap_rate : SOUND_FREQ;

    for (int c = 0; c < len; c++)
        covox->in_ring[covox->in_wr++ % COVOX_IN_RING] = (int16_t) ((buffer[c * 2] + buffer[c * 2 + 1]) / 2);

    uint32_t keep = (uint32_t) (covox->in_rate / 4);
    if (keep > (COVOX_IN_RING / 2))
        keep = COVOX_IN_RING / 2;
    if ((covox->in_wr - covox->in_rd) > keep) {
        covox->in_rd   = covox->in_wr - (keep / 2);
        covox->in_frac = 0.0;
    }
}

uint8_t
covox_read(uint16_t addr, void *priv)
{
    covox_t *covox = (covox_t *) priv;
    uint8_t  ret   = 0xff;

    covox_log("covox_read: addr=%04x\n", addr);

    /* the Voice Master Key's digitizer reads at its DAC port (22Fh,
       24Fh, 28Fh or 2CFh): Covox's recordByte()/record8() read a byte there
       per sample, the first one only to start a conversion -- a read gives
       the previous conversion and starts the next. 8-bit offset binary,
       80h for silence (the Developer's Toolkit's silence ranges centre on
       it). */
    if (covox->adc_port && (addr == covox->adc_port)) {
        int s;

        covox_adc_input(covox, 1);
        covox->in_idle = 0;

        ret = covox->adc_val;
        s   = (covox_adc_input_sample(covox) + 128) >> 8;
        if (s > 127)
            s = 127;
        covox->adc_val = (uint8_t) (s + 128);
    }

    return ret;
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
covox_get_buffer(int32_t *buffer, uint16_t len, void *priv)
{
    covox_t *covox = (covox_t *) priv;

    covox_update(covox);

    for (uint16_t c = 0; c < len; c++) {
        buffer[c * 2]     += dac_iir(0, covox->buffer[0][c]);
        buffer[c * 2 + 1] += dac_iir(1, covox->buffer[1][c]);
    }
    covox->pos = 0;
}

static void
covox_get_music_buffer(int32_t *buffer, uint16_t len, void *priv)
{
    covox_t *covox = (covox_t *) priv;

    const int32_t *opl_buf = covox->opl.update(covox->opl.priv);

    for (uint16_t c = 0; c < len * 2; c++)
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

    /* the Voice Master Key's digitizer. */
    covox->adc_val = 0x80;
    if (info->local == COVOX_VOICEMASTERKEY) {
        covox->adc_port = device_get_config_hex16("base");
        sound_in_add_handler(covox_put_buffer, covox);
    }

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

    if (covox) {
        covox_adc_input(covox, 0); /* */
        free(covox);
    }
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

/* the digitizer (ADC) reads at the DAC port. */
const device_t voicemasterkey_device = {
    .name          = "Covox Voice Master Key",
    .internal_name = "voicemasterkey",
    .flags         = DEVICE_ISA | DEVICE_SIDECAR | DEVICE_AUDIO_IN,
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
