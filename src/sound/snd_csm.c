/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Covox Sound Master emulation.
 *
 * Authors: Roy Baer, <https://pcem-emulator.co.uk/>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *          win2kgamer
 *
 *          Copyright 2025 Roy Baer.
 *          Copyright 2025 Jasmine Iwanek.
 *          Copyright 2026 win2kgamer
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
#include "ayumi/ayumi.h"
#include <86box/plat_unused.h>
#include <86box/pic.h>
#include <86box/dma.h>
#include <86box/log.h>
#include <86box/timer.h>

typedef struct ay_3_89x0_s {
    int     type;
    int     last_written;
    uint8_t index;
    uint8_t regs[16];
    uint8_t regs_bankb[16];
    struct  ayumi chip;
} ay_3_89x0_t;

typedef struct csm_s {
    ay_3_89x0_t psg;
    uint8_t     pcm_sample;

    uint8_t irq;
    uint8_t dma;
    uint8_t enable_dma;
    uint8_t ay_extended_mode;
    uint8_t ay_extended_bank;
    uint8_t dma_running;
    uint8_t dma_pulse;
    uint8_t dma_mult;
    uint16_t dma_interval;

    pc_timer_t dma_timer;

    int16_t buffer[SOUNDBUFLEN * 10];
    int     pos;
    void *  log; /* New logging system */
} csm_t;

#ifdef ENABLE_CSM_LOG
int csm_do_log = ENABLE_CSM_LOG;

static void
csm_log(void *priv, const char *fmt, ...)
{
    if (csm_do_log) {
        va_list ap;
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define csm_log(fmt, ...)
#endif

void
csm_update(csm_t *csm)
{
    for (; csm->pos < sound_pos_global; csm->pos++) {
        ayumi_process(&csm->psg.chip);

        ayumi_remove_dc(&csm->psg.chip);

        double pcm_scaled = (csm->pcm_sample - 128) / 128.0;
        csm->buffer[csm->pos << 1] = (csm->psg.chip.left + pcm_scaled) * 12000;
        csm->buffer[(csm->pos << 1) + 1] = (csm->psg.chip.right + pcm_scaled) * 12000;
    }
}

void
csm_get_buffer(int32_t *buffer, int len, void *priv)
{
    csm_t *csm = (csm_t *) priv;

    csm_update(csm);

    for (int c = 0; c < len * 2 ; c++)
        buffer[c] += csm->buffer[c];

    csm->pos = 0;
}

static void
csm_dma_poll(void *priv)
{
    csm_t *csm = (csm_t *) priv;

    int dma_data;

    timer_advance_u64(&csm->dma_timer, (uint64_t) ((double) TIMER_USEC * ((16 * csm->dma_interval) * 0.1318) * csm->dma_mult));

    if (csm->dma_pulse == 0)
        csm->dma_pulse = 1;
    else {
        csm->dma_pulse = 0;
        return;
    }

    csm_update(csm);

    dma_data = dma_channel_read(csm->dma);

    if (dma_data == DMA_NODATA)
        csm->pcm_sample = 0x80;
    else
        csm->pcm_sample = (dma_data & 0xff);

    if (dma_data & DMA_OVER) {
        if (!(csm->psg.regs[15] & 0x40)) {
            picint(1 << csm->irq);
        }
        /* Non-autoinit DMA, disable DMA timer */
        csm->dma_running = 0;
        timer_disable(&csm->dma_timer);
    }
}

static void
csm_mode_bits_changed(csm_t *csm)
{
    /* Broderbund games don't seem to write to port B to switch back to PSG mode after DMA sample playback
       so there is another unknown mechanism that returns the Sound Master to this mode. The games do write
       a value of 0 to the tone period register which makes a good heuristic for switching back to PSG mode */
    if (csm->dma_interval <= 1)
        csm->psg.regs[15] |= 0x80;

    uint8_t a = (csm->psg.regs[7] & 0x40) ? csm->psg.regs[14] : 0x88;
    uint8_t b = (csm->psg.regs[7] & 0x80) ? csm->psg.regs[15] : 0xf0;

    // IOB4 1:mono, 0:stereo -- for channels A and B
    // IOB5 1:disable DMA, 0:enable DMA
    // IOB6 1:disable IRQ, 0:enable IRQ
    // IOB7 1:enable, 0:disable -- audio output of channel C
    double vol_left = (a & 0xf) / 15.0;
    double vol_right = (a >> 4) / 15.0;

    // update Ayumi's channel pan values
    csm->psg.chip.channels[0].pan_left = (b & 0x10) ? vol_left : 0.0;
    csm->psg.chip.channels[0].pan_right = vol_right;
    csm->psg.chip.channels[1].pan_left = vol_left;
    csm->psg.chip.channels[1].pan_right = (b & 0x10) ? vol_right : 0.0;
    csm->psg.chip.channels[2].pan_left = (b & 0x80) ? vol_left : 0.0;
    csm->psg.chip.channels[2].pan_right = (b & 0x80) ? vol_right : 0.0;

    /* DMA cannot function with the channel C output used for PSG or with a very small value in the period register */
    if (((csm->psg.regs[15] & 0x20) == 0x20) || ((csm->psg.regs[15] & 0x80) == 0x80) || (csm->dma_interval <= 1))
        csm->enable_dma = 0;
    else
        csm->enable_dma = 1;
    csm_log(csm->log, "SoundMaster DAC DMA is now %sabled\n", csm->enable_dma ? "En" : "Dis");
    if ((csm->psg.regs[7] & 0x04) || !csm->enable_dma) {
        csm->dma_running = 0;
        timer_disable(&csm->dma_timer);
        csm_log(csm->log, "Soundmaster DMA timer stop\n");
    } else if (((csm->psg.regs[7] & 0x04) == 0x00) && csm->enable_dma) {
        csm->dma_running = 1;
        csm_log(csm->log, "Soundmaster DMA timer start\n");
        if (csm->dma_interval == 0)
            csm->dma_interval = 1;
        csm->dma_pulse = 0;
        csm->dma_mult = ((csm->psg.regs[10] & 0x10) || (csm->psg.regs[10] == 0)) ? 1 : 2;
        timer_set_delay_u64(&csm->dma_timer, (uint64_t) ((double) TIMER_USEC * ((16 * csm->dma_interval) * 0.1318) * csm->dma_mult));
        csm_log(csm->log, "SoundMaster DMA timing = %f uS\n", (16 * csm->dma_interval) * 0.1318 * csm->dma_mult);
    }
}

void
csm_clear_ay_regs(void *priv)
{
    csm_t       *csm = (csm_t *) priv;
    ay_3_89x0_t *ay  = &csm->psg;

    uint8_t     modeselect = (ay->regs[13] & 0xf0);
    /* Enable register doesn't clear on modeswitch either? */
    uint8_t     enable = (ay->regs[7]);

    for (int c = 0; c < 16; c++) {
        ay->regs[c] = 0;
        ay->regs_bankb[c] = 0;
    }

    ay->regs[7]  = enable;
    ay->regs[13] = modeselect;

}

void
csm_write(uint16_t addr, uint8_t data, void *priv)
{
    csm_t       *csm = (csm_t *) priv;
    ay_3_89x0_t *ay  = &csm->psg;

    csm_log(csm->log, "CSM Write: addr = %04X, val = %02X\n", addr, data);

    csm_update(csm);

    switch (addr & 0x1f) {
        case 0:
            csm->psg.index = data;
            csm->psg.last_written = -1;
            break;
        case 1:
            if (csm->psg.index <= 15)
                csm->psg.last_written = data;

            switch (ay->index) {
                case 0:
                    if (!csm->ay_extended_bank)
                        ay->regs[0] = data;
                    else
                        ay->regs_bankb[0] = data;
                    if (!csm->ay_extended_bank)
                        ayumi_set_tone(&ay->chip, 0, (ay->regs[1] << 8) | ay->regs[0]);
                    break;
                case 1:
                    if (!csm->ay_extended_mode)
                        ay->regs[1] = data & 0xf;
                    else if (!csm->ay_extended_bank)
                        ay->regs[1] = data;
                    else
                        ay->regs_bankb[1] = data;
                    if (!csm->ay_extended_bank)
                        ayumi_set_tone(&ay->chip, 0, (ay->regs[1] << 8) | ay->regs[0]);
                    break;
                case 2:
                    if (!csm->ay_extended_bank)
                        ay->regs[2] = data;
                    else
                        ay->regs_bankb[2] = data;
                    if (!csm->ay_extended_bank)
                        ayumi_set_tone(&ay->chip, 1, (ay->regs[3] << 8) | ay->regs[2]);
                    break;
                case 3:
                    if (!csm->ay_extended_mode)
                        ay->regs[3] = data & 0xf;
                    else if (!csm->ay_extended_bank)
                        ay->regs[3] = data;
                    else
                        ay->regs_bankb[3] = data;
                    if (!csm->ay_extended_bank)
                        ayumi_set_tone(&ay->chip, 1, (ay->regs[3] << 8) | ay->regs[2]);
                    break;
                case 4:
                    if (!csm->ay_extended_bank)
                        ay->regs[4] = data;
                    else
                        ay->regs_bankb[4] = data;
                    if (!csm->ay_extended_bank) {
                        ayumi_set_tone(&ay->chip, 2, (ay->regs[5] << 8) | ay->regs[4]);
                        csm->dma_interval = ((ay->regs[5] << 8) | (data & 0xff));
                        if (csm->dma_interval == 0)
                            csm->dma_interval = 1;
                    }
                    break;
                case 5:
                    if (!csm->ay_extended_mode)
                        ay->regs[5] = data & 0xf;
                    else if (!csm->ay_extended_bank)
                        ay->regs[5] = data;
                    else
                        ay->regs_bankb[5] = data;
                    if (!csm->ay_extended_bank) {
                        ayumi_set_tone(&ay->chip, 2, (ay->regs[5] << 8) | ay->regs[4]);
                        csm->dma_interval = ((data << 8) | (ay->regs[4] & 0xff));
                        if (csm->dma_interval == 0)
                            csm->dma_interval = 1;
                    }
                    break;
                case 6:
                    if (!csm->ay_extended_mode)
                        ay->regs[6] = data & 0x1f;
                    else if (!csm->ay_extended_bank)
                        ay->regs[6] = data;
                    else
                        ay->regs_bankb[6] = data;
                    if (!csm->ay_extended_bank)
                        ayumi_set_noise(&ay->chip, ay->regs[6]);
                    break;
                case 7:
                    if (!csm->ay_extended_bank)
                        ay->regs[7] = data;
                    else
                        ay->regs_bankb[7] = data;
                    if (!csm->ay_extended_mode) {
                        ayumi_set_mixer(&ay->chip, 0, data & 1, (data >> 3) & 1, (ay->regs[8] >> 4) & 1);
                        ayumi_set_mixer(&ay->chip, 1, (data >> 1) & 1, (data >> 4) & 1, (ay->regs[9] >> 4) & 1);
                        ayumi_set_mixer(&ay->chip, 2, (data >> 2) & 1, (data >> 5) & 1, (ay->regs[10] >> 4) & 1);
                        csm_mode_bits_changed(csm);
                    } else if (!csm->ay_extended_bank) {
                        ayumi_set_mixer(&ay->chip, 0, data & 1, (data >> 3) & 1, (ay->regs[8] >> 5) & 1);
                        ayumi_set_mixer(&ay->chip, 1, (data >> 1) & 1, (data >> 4) & 1, (ay->regs[9] >> 5) & 1);
                        ayumi_set_mixer(&ay->chip, 2, (data >> 2) & 1, (data >> 5) & 1, (ay->regs[10] >> 5) & 1);
                        csm_mode_bits_changed(csm);
                    }
                    break;
                case 8:
                    if (!csm->ay_extended_mode)
                        ay->regs[8] = data & 0x1f;
                    else if (!csm->ay_extended_bank)
                        ay->regs[8] = data & 0x3f;
                    else
                        ay->regs_bankb[8] = data;
                    if (!csm->ay_extended_mode)
                        ayumi_set_volume(&ay->chip, 0, data & 0xf);
                    /* Right-shift volume values for Ayumi */
                    else if (!csm->ay_extended_bank && (data >= 0x02))
                        ayumi_set_volume(&ay->chip, 0, (data >> 1) & 0xf);
                    else if (!csm->ay_extended_bank)
                        ayumi_set_volume(&ay->chip, 0, data & 0xf);
                    if (!csm->ay_extended_mode)
                        ayumi_set_mixer(&ay->chip, 0, ay->regs[7] & 1, (ay->regs[7] >> 3) & 1, (data >> 4) & 1);
                    else if (!csm->ay_extended_bank)
                        ayumi_set_mixer(&ay->chip, 0, ay->regs[7] & 1, (ay->regs[7] >> 3) & 1, (data >> 5) & 1);
                    break;
                case 9:
                    if (!csm->ay_extended_mode)
                        ay->regs[9] = data & 0x1f;
                    else if (!csm->ay_extended_bank)
                        ay->regs[9] = data & 0x3f;
                    else
                        ay->regs_bankb[9] = data;
                    if (!csm->ay_extended_mode)
                        ayumi_set_volume(&ay->chip, 1, data & 0xf);
                    /* Right-shift volume values for Ayumi */
                    else if (!csm->ay_extended_bank && (data >= 0x02))
                        ayumi_set_volume(&ay->chip, 1, (data >> 1) & 0xf);
                    else if (!csm->ay_extended_bank)
                        ayumi_set_volume(&ay->chip, 1, data & 0xf);
                    if (!csm->ay_extended_mode)
                        ayumi_set_mixer(&ay->chip, 1, (ay->regs[7] >> 1) & 1, (ay->regs[7] >> 4) & 1, (data >> 4) & 1);
                    else if (!csm->ay_extended_bank)
                        ayumi_set_mixer(&ay->chip, 1, (ay->regs[7] >> 1) & 1, (ay->regs[7] >> 4) & 1, (data >> 5) & 1);
                    break;
                case 10:
                    if (!csm->ay_extended_mode)
                        ay->regs[10] = data & 0x1f;
                    else if (!csm->ay_extended_bank)
                        ay->regs[10] = data & 0x3f;
                    else
                        ay->regs_bankb[10] = data;
                    if (!csm->ay_extended_mode)
                        ayumi_set_volume(&ay->chip, 2, data & 0xf);
                    /* Right-shift volume values for Ayumi */
                    else if (!csm->ay_extended_bank && (data >= 0x02))
                        ayumi_set_volume(&ay->chip, 2, (data >> 1) & 0xf);
                    else if (!csm->ay_extended_bank)
                        ayumi_set_volume(&ay->chip, 2, data & 0xf);
                    if (!csm->ay_extended_mode)
                        ayumi_set_mixer(&ay->chip, 2, (ay->regs[7] >> 2) & 1, (ay->regs[7] >> 5) & 1, (data >> 4) & 1);
                    else if (!csm->ay_extended_bank)
                        ayumi_set_mixer(&ay->chip, 2, (ay->regs[7] >> 2) & 1, (ay->regs[7] >> 5) & 1, (data >> 5) & 1);
                    break;
                case 11:
                    if (!csm->ay_extended_bank)
                        ay->regs[11] = data;
                    if (!csm->ay_extended_mode)
                        ayumi_set_envelope(&ay->chip, (ay->regs[12] >> 8) | ay->regs[11]);
                    break;
                case 12:
                    if (!csm->ay_extended_bank)
                        ay->regs[12] = data;
                    if (!csm->ay_extended_mode)
                        ayumi_set_envelope(&ay->chip, (ay->regs[12] >> 8) | ay->regs[11]);
                    break;
                case 13:
                    if ((data & 0xa0) == 0xa0) {
                        csm_log(csm->log, "CSM AY8930 extended mode\n");
                        if (!csm->ay_extended_mode)
                            csm_clear_ay_regs(csm);
                        csm->ay_extended_mode = 1;
                        if (data & 0x10)
                            csm->ay_extended_bank = 1; //Bank B
                        else
                            csm->ay_extended_bank = 0; //Bank A
                        ay->regs[13] = data;
                    } else {
                        if (csm->ay_extended_mode)
                            csm_clear_ay_regs(csm);
                        csm->ay_extended_mode = 0;
                        csm->ay_extended_bank = 0;
                        ay->regs[13] = data & 0xf;
                    }
                    if (!csm->ay_extended_mode)
                        ayumi_set_envelope_shape(&ay->chip, data & 0xf);
                    break;
                case 14:
                    if (!csm->ay_extended_bank) {
                        ay->regs[14] = data;
                        csm_mode_bits_changed(csm);
                    }
                    break;
                case 15:
                    if (!csm->ay_extended_bank) {
                        ay->regs[15] = data;
                        csm_mode_bits_changed(csm);
                    }
                    break;
            }
            break;
        case 2:
        case 15:
            csm->pcm_sample = data;
            break;
        case 3:
            if (csm->enable_dma)
                picintc(1 << csm->irq);
            break;
    }
}

uint8_t
csm_read(uint16_t addr, void *priv)
{
    csm_t       *csm = (csm_t *) priv;
    ay_3_89x0_t *ay  = &csm->psg;
    uint8_t     ret = 0x0;

    switch (addr & 0x1f) {
        case 1:
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
                    if (!csm->ay_extended_bank || ay->index == 0x0d)
                        ret = ay->regs[ay->index];
                    else
                        ret = ay->regs_bankb[ay->index];
                    break;
                case 14:
                    /* Should ouptut mode just return FFh on read? */
                    ret = (ay->regs[7] & 0x40) ? ay->regs[14] : 0;
                    if (ay->regs[14] == 0xff)
                        ret = 0xff;
                    if (csm->ay_extended_bank)
                        ret = ay->regs_bankb[ay->index];
                    break;
                case 15:
                    /* Should ouptut mode just return FFh on read? */
                    // there are pull-up resistors on IOB7..IOB4
                    ret = (ay->regs[7] & 0x80) ? ay->regs[15] : 0xf0;
                    if (ay->regs[15] == 0xff)
                        ret = 0xff;
                    if (csm->ay_extended_bank)
                        ret = ay->regs_bankb[ay->index];
                    break;
                default:
                    // PSG data bus should be in high-impedance mode for out-of-bounds
                    // register indices => approximate a common effect of a floating bus
                    ret = ay->index;
                    break;
            }
            break;
        case 4:
            // TODO: read joystick 2?
            ret = 0xff;
            break;
        case 5:
            // TODO: read joystick 1?
            ret = 0xff;
            break;
        case 14:
            ret = 0xff;
            break;
        default:
            ret = 0x0;
            break;
    }

    csm_log(csm->log, "CSM Read: addr = %04X, ret = %02X\n", addr, ret);

    return ret;
}

void *
csm_device_init(UNUSED(const device_t *info))
{
    csm_t *csm = calloc(1, sizeof(csm_t));
    uint16_t base_addr = device_get_config_int("addr");
    csm->dma = device_get_config_int("dma");
    csm->irq = device_get_config_int("irq");

    csm->log = log_open("SoundMaster");

    csm->psg.type = 0; /* Always use the AY-3-8910 type */
    ayumi_configure(&csm->psg.chip, 0, 1789773, 48000);
    csm->psg.regs[15] = 0xe0;
    csm_mode_bits_changed(csm);

    sound_add_handler(csm_get_buffer, csm);

    io_sethandler(base_addr, 0x20,
                  csm_read, NULL, NULL,
                  csm_write, NULL, NULL,
                  csm);

    timer_add(&csm->dma_timer, csm_dma_poll, csm, 0);

    return csm;
}

void
csm_device_close(void *priv)
{
    csm_t *csm = (csm_t *) priv;

    if (csm->log != NULL) {
        log_close(csm->log);
        csm->log = NULL;
    }

    free(csm);
}

static const device_config_t soundmaster_config[] = {
    {
        .name        = "addr",
        .description = "Address",
        .type        = CONFIG_SELECTION,
        .selection   = {
            { .description = "220h", .value = 0x220 },
            { .description = "240h", .value = 0x240 },
            { .description = "280h", .value = 0x280 },
            { .description = "2C0h", .value = 0x2c0 },
            { .description = ""                     }
        },
        .default_int = 0x220
    },
    {
        .name        = "dma",
        .description = "DMA",
        .type        = CONFIG_SELECTION,
        .selection   = {
            { .description = "none", .value = 0 },
            { .description = "1",    .value = 1 },
            { .description = "3",    .value = 3 },
            { .description = ""                 }
        },
        .default_int = 0
    },
    {
        .name        = "irq",
        .description = "IRQ",
        .type        = CONFIG_SELECTION,
        .selection   = {
            { .description = "none", .value = 0 },
            { .description = "3",    .value = 3 },
            { .description = "7",    .value = 7 },
            { .description = ""                 }
        },
        .default_int = 0
    },
    { .type = CONFIG_END }
};

const device_t soundmaster_device = {
    .name          = "Covox Sound Master",
    .internal_name = "csm",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = csm_device_init,
    .close         = csm_device_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = soundmaster_config
};
