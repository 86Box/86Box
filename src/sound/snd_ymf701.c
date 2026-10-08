/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Yamaha YMF-701 (OPL3-SA) audio controller emulation.
 *
 * Authors: Cacodemon345
 *          Eluan Costa Miranda <eluancm@gmail.com>
 *          win2kgamer
 *
 *          Copyright 2022 Cacodemon345.
 *          Copyright 2020 Eluan Costa Miranda.
 *          Copyright 2025 win2kgamer
 */
#include <math.h>
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
#include <86box/midi.h>
#include <86box/timer.h>
#include <86box/pic.h>
#include <86box/sound.h>
#include <86box/gameport.h>
#include <86box/snd_ad1848.h>
#include <86box/snd_sb.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/plat_unused.h>
#include <86box/log.h>

#ifdef ENABLE_YMF701_LOG
int ymf701_do_log = ENABLE_YMF701_LOG;

static void
ymf701_log(void *priv, const char *fmt, ...)
{
    if (ymf701_do_log) {
        va_list ap;
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define ymf701_log(fmt, ...)
#endif

static int ymf701_wss_dma[4]  = { 0, 0, 1, 3 };
static int ymf701_wss_dma2[4] = { 1, 1, 0, 0 };
static int ymf701_wss_irq[8]  = { 0, 7, 9, 10, 11, 0, 0, 0 };

static double ymf701_input_gain_vols_4bits[16];

typedef struct ymf701_t {
    uint8_t type;

    uint8_t wss_config;
    uint8_t reg_enabled;

    uint16_t cur_sb_addr;
    uint16_t cur_wss_addr;
    uint16_t cur_mpu401_addr;

    int   cur_sb_irq;
    int   cur_sb_dma;
    int   cur_wss_enabled;
    int   cur_wss_irq;
    int   cur_wss_dma;
    int   cur_mpu401_irq;
    int   cur_mpu401_enabled;
    void *gameport;

    uint8_t cur_mode;

    ad1848_t ad1848;
    mpu_t   *mpu;

    sb_t   *sb;
    uint8_t index;
    uint8_t regs[6];
    uint8_t passwd_phase;

    void *    log;  /* New logging system */
} ymf701_t;

static void
ymf701_filter_opl(void *priv, double *out_l, double *out_r)
{
    ymf701_t *ymf701 = (ymf701_t *) priv;

    if (ymf701->cur_wss_enabled) {
        ad1848_filter_channel((void *) &ymf701->ad1848, AD1848_AUX2, out_l, out_r);
    }
}

static uint8_t
ymf701_wss_read(uint16_t addr, void *priv)
{
    ymf701_t *ymf701 = (ymf701_t *) priv;
    uint8_t ret = 0x00;
    uint8_t port = addr - ymf701->cur_wss_addr;

    switch (port) {
        case 0:
            ret = ymf701->wss_config;
            break;
        case 3:
            ret = 0x04 | (ymf701->wss_config & 0x40);
            break;
        default:
            ret = ymf701->wss_config;
            break;
    }
    ymf701_log(ymf701->log, "WSS Read: addr = %02X, val = %02X\n", addr, ret);
    return ret;
}

static void
ymf701_wss_write(uint16_t addr, uint8_t val, void *priv)
{
    ymf701_t *ymf701 = (ymf701_t *) priv;
    uint8_t port = addr - ymf701->cur_wss_addr;

    ymf701_log(ymf701->log, "WSS Write: addr = %02X, val = %02X\n", addr, val);
    switch (port) {
        case 0:
            ymf701->wss_config = val;
            ymf701->cur_wss_dma = ymf701_wss_dma[val & 3];
            ymf701->cur_wss_irq = ymf701_wss_irq[(val >> 3) & 7];
            ad1848_setdma(&ymf701->ad1848, ymf701_wss_dma[val & 3]);
            ad1848_setirq(&ymf701->ad1848, ymf701_wss_irq[(val >> 3) & 7]);
            ymf701_log(ymf701->log, "Set IRQ to %02X\n", ymf701->cur_wss_irq);
            ymf701_log(ymf701->log, "Set DMA to %02X\n", ymf701->cur_wss_dma);

            /* YMF701 supports full-duplex mode */
            if (val & 0x04) {
                ymf701_log(ymf701->log, "WSS: Full-duplex mode enabled\n");
                ad1848_setdma2(&ymf701->ad1848, ymf701_wss_dma2[val & 3]);
            } else {
                ymf701_log(ymf701->log, "WSS: Full-duplex mode disabled\n");
                ad1848_setdma2(&ymf701->ad1848, 4);
            }
            break;
        default:
            break;
    }
}

#define YMF701_RECORD_CLAMP(x) (((x) < -32768) ? -32768 : (((x) > 32767) ? 32767 : (x)))

/* filter when freq < capture rate */
#define YMF701_RECORD_ANTIALIAS 1

/* nyquist anti alias */
#define YMF701_RECORD_AA_NYQ 0.9

/* audio filter called on filter rate change */
static void
ymf701_record_aa_design(ad1848_t *ad1848, int out_rate, int in_rate)
{
    const double fc    = (YMF701_RECORD_AA_NYQ * 0.5) * ((double) out_rate);
    const double w0    = (2.0 * M_PI * fc) / ((double) in_rate);
    const double cw    = cos(w0);
    const double sw    = sin(w0);
    const double alpha = sw / (2.0 * 0.70710678118654752);
    const double a0    = 1.0 + alpha;

    ad1848->record_aa_b0_mic = ((1.0 - cw) / 2.0) / a0;
    ad1848->record_aa_b1_mic = (1.0 - cw) / a0;
    ad1848->record_aa_b2_mic = ad1848->record_aa_b0_mic;
    ad1848->record_aa_a1_mic = (-2.0 * cw) / a0;
    ad1848->record_aa_a2_mic = (1.0 - alpha) / a0;
}

static double
ymf701_record_aa_step(ad1848_t *ad1848, int ch, double x)
{
    const double y = (ad1848->record_aa_b0_mic * x) + ad1848->record_aa_z1_mic[ch];

    ad1848->record_aa_z1_mic[ch] = (ad1848->record_aa_b1_mic * x) - (ad1848->record_aa_a1_mic * y)
                                + ad1848->record_aa_z2_mic[ch];
    ad1848->record_aa_z2_mic[ch] = (ad1848->record_aa_b2_mic * x) - (ad1848->record_aa_a2_mic * y);

    return y;
}

static void
ymf701_put_buffer(int16_t *buffer, int len, void *priv)
{
    ymf701_t *ymf701 = (ymf701_t *) priv;

    /* divisor is rate capture device opened at*/
    const int cap_rate = al_capture_get_rate();
    const int denom    = (cap_rate > 0) ? cap_rate : SOUND_FREQ;
    int rate = 0;
    if (ymf701->ad1848.regs[9] & 0x02)
        rate = ymf701->ad1848.freq;
    else
        rate = ymf701->sb->dsp.sb_freq;

    int c;
    int gain_l;
    int gain_r;
    int sel_l_mic, sel_l_linel;
    int sel_r_mic, sel_r_liner;
    int interp;
    int filt;

    /* freq is 0 until the guest programs a rate  */
    if (rate <= 0)
        return;

    if ((denom != ymf701->ad1848.record_denom_mic) || (rate != ymf701->ad1848.record_rate_mic)) {
        ymf701->ad1848.record_denom_mic      = denom;
        ymf701->ad1848.record_rate_mic       = rate;
        ymf701->ad1848.record_phase_mic      = 0;
        ymf701->ad1848.record_prev_l_mic     = 0;
        ymf701->ad1848.record_prev_r_mic     = 0;
        ymf701->ad1848.record_prev_valid_mic = 0;

        ymf701->ad1848.record_aa_z1_mic[0] = 0.0;
        ymf701->ad1848.record_aa_z1_mic[1] = 0.0;
        ymf701->ad1848.record_aa_z2_mic[0] = 0.0;
        ymf701->ad1848.record_aa_z2_mic[1] = 0.0;
        ymf701->ad1848.record_aa_active_mic = 0;

#if YMF701_RECORD_ANTIALIAS
        /* only when decimating */
        if (rate < denom) {
            ymf701_record_aa_design(&ymf701->ad1848, rate, denom);
            ymf701->ad1848.record_aa_active_mic = 1;
        }
#endif
    }

    interp = (rate != denom);
    filt   = ymf701->ad1848.record_aa_active_mic;

    gain_l = ymf701->ad1848.regs[0] & 0x0f;
    gain_r = ymf701->ad1848.regs[1] & 0x0f;

    sel_l_mic   = (((ymf701->ad1848.regs[0] & 0xc0) == 0x80) ? 1 : 0);
    sel_l_linel = (((ymf701->ad1848.regs[0] & 0xc0) == 0x00) ? 1 : 0);

    sel_r_mic   = (((ymf701->ad1848.regs[1] & 0xc0) == 0x80) ? 1 : 0);
    sel_r_liner = (((ymf701->ad1848.regs[1] & 0xc0) == 0x00) ? 1 : 0);

    for (c = 0; c < len * 2; c += 2) {
        const int32_t cap_l = (int32_t) buffer[c];
        const int32_t cap_r = (int32_t) buffer[c + 1];

        /* mic is the mono sum of line-in. truncating division for dc symmetry */
        const int32_t mic = (cap_l + cap_r) / 2;

        int32_t mix_l = (mic * sel_l_mic) + (cap_l * sel_l_linel);
        int32_t mix_r = (mic * sel_r_mic) + (cap_r * sel_r_liner);
        int32_t in_l;
        int32_t in_r;

        /* run on every input frame*/
        if (filt) {
            mix_l = (int32_t) lrint(ymf701_record_aa_step(&ymf701->ad1848, 0, (double) mix_l));
            mix_r = (int32_t) lrint(ymf701_record_aa_step(&ymf701->ad1848, 1, (double) mix_r));
        }

        in_l = YMF701_RECORD_CLAMP(mix_l * ymf701_input_gain_vols_4bits[gain_l]);
        in_r = YMF701_RECORD_CLAMP(mix_r * ymf701_input_gain_vols_4bits[gain_r]);

        /* start new device change with first frame in interpolartor queue */
        if (!ymf701->ad1848.record_prev_valid_mic) {
            ymf701->ad1848.record_prev_l_mic     = in_l;
            ymf701->ad1848.record_prev_r_mic     = in_r;
            ymf701->ad1848.record_prev_valid_mic = 1;
        }

        /* phase ticks this forward, while-loop for new samples so they arent dropped */
        ymf701->ad1848.record_phase_mic += rate;
        while (ymf701->ad1848.record_phase_mic >= denom) {
            int32_t out_l;
            int32_t out_r;

            ymf701->ad1848.record_phase_mic -= denom; /* denom tracks input frame vs emitted frame , (rate - phase) / rate */

            if (interp) {

                const int32_t num = rate - ymf701->ad1848.record_phase_mic;

                out_l = ymf701->ad1848.record_prev_l_mic
                        + (int32_t) ((((int64_t) (in_l - ymf701->ad1848.record_prev_l_mic)) * num) / rate);
                out_r = ymf701->ad1848.record_prev_r_mic
                        + (int32_t) ((((int64_t) (in_r - ymf701->ad1848.record_prev_r_mic)) * num) / rate);
            } else {
                out_l = in_l;
                out_r = in_r;
            }

            ymf701->ad1848.record_buffer[ymf701->ad1848.record_pos_write_mic]                = (int16_t) out_l;
            ymf701->ad1848.record_buffer[(ymf701->ad1848.record_pos_write_mic + 1) & 0xffff] = (int16_t) out_r;
            ymf701->sb->dsp.record_buffer[ymf701->sb->dsp.record_pos_write_mic]                = (int16_t) out_l;
            ymf701->sb->dsp.record_buffer[(ymf701->sb->dsp.record_pos_write_mic + 1) & 0xffff] = (int16_t) out_r;

            ymf701->ad1848.record_pos_write_mic = (ymf701->ad1848.record_pos_write_mic + 2) & 0xffff;
            ymf701->sb->dsp.record_pos_write_mic = (ymf701->sb->dsp.record_pos_write_mic +2) & 0xffff;
        }

        ymf701->ad1848.record_prev_l_mic = in_l;
        ymf701->ad1848.record_prev_r_mic = in_r;
    }
}

static void
ymf701_get_buffer(int32_t *buffer, uint16_t len, void *priv)
{
    ymf701_t *ymf701 = (ymf701_t *) priv;

    /* wss part */
    ad1848_update(&ymf701->ad1848);
    for (uint16_t c = 0; c < len * 2; c++)
        buffer[c] += (ymf701->ad1848.buffer[c] / 2);

    ymf701->ad1848.pos = 0;
}

static void
ymf701_get_sbpro_buffer(int32_t *buffer, uint16_t len, void *priv)
{
    ymf701_t *ymf701 = (ymf701_t *) priv;

    /* sbprov2 part */
    sb_get_buffer_sbpro(buffer, len, ymf701->sb);
}

static void
ymf701_remove_opl(ymf701_t *ymf701)
{
    io_removehandler(ymf701->cur_sb_addr + 0, 0x0004, ymf701->sb->opl.read, NULL, NULL, ymf701->sb->opl.write, NULL, NULL, ymf701->sb->opl.priv);
    io_removehandler(ymf701->cur_sb_addr + 8, 0x0002, ymf701->sb->opl.read, NULL, NULL, ymf701->sb->opl.write, NULL, NULL, ymf701->sb->opl.priv);
    io_removehandler(0x0388, 0x0004, ymf701->sb->opl.read, NULL, NULL, ymf701->sb->opl.write, NULL, NULL, ymf701->sb->opl.priv);
}

static void
ymf701_add_opl(ymf701_t *ymf701)
{
    /* DSP I/O handler is activated in sb_dsp_setaddr */
    io_sethandler(ymf701->cur_sb_addr + 0, 0x0004, ymf701->sb->opl.read, NULL, NULL, ymf701->sb->opl.write, NULL, NULL, ymf701->sb->opl.priv);
    io_sethandler(ymf701->cur_sb_addr + 8, 0x0002, ymf701->sb->opl.read, NULL, NULL, ymf701->sb->opl.write, NULL, NULL, ymf701->sb->opl.priv);
    io_sethandler(0x0388, 0x0004, ymf701->sb->opl.read, NULL, NULL, ymf701->sb->opl.write, NULL, NULL, ymf701->sb->opl.priv);
}

static void
ymf701_reg_write(uint16_t addr, uint8_t val, void *priv)
{
    ymf701_t *ymf701 = (ymf701_t *) priv;

    if (ymf701->reg_enabled) {
        ymf701_log(ymf701->log, "Write with reg access enabled:\n");
        ymf701_log(ymf701->log, "addr = %02X, val = %02X\n", addr, val);
        switch (addr) {
            case 0xF86:
                ymf701->index = val;
                ymf701->passwd_phase = 0x01;
                ymf701_log(ymf701->log, "Passwd phase 1\n");
                break;
            case 0xF87:
                switch (ymf701->index) {
                    case 0x01: /* WSS Config */
                        ymf701->regs[0x01] = val;
                        ymf701->cur_mode = ymf701->cur_wss_enabled = !!(val & 0x20);

                        sound_set_cd_audio_filter(NULL, NULL);
                        if (ymf701->cur_wss_enabled) /* WSS */
                            sound_set_cd_audio_filter(ad1848_filter_cd_audio, &ymf701->ad1848);
                        else /* SBPro */
                            sound_set_cd_audio_filter(sbpro_filter_cd_audio, ymf701->sb);

                        io_removehandler(ymf701->cur_wss_addr, 0x0004, ymf701_wss_read, NULL, NULL, ymf701_wss_write, NULL, NULL, ymf701);
                        io_removehandler(ymf701->cur_wss_addr + 0x0004, 0x0004, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &ymf701->ad1848);
                        switch ((val >> 3) & 0x3) {
                            case 0: /* WSBase = 0x530 */
                                ymf701_log(ymf701->log, "WSS base is now 530h\n");
                                ymf701->cur_wss_addr = 0x530;
                                break;
                            case 1: /* WSBase = 0xE80 */
                                ymf701_log(ymf701->log, "WSS base is now E80h\n");
                                ymf701->cur_wss_addr = 0xE80;
                                break;
                            case 2: /* WSBase = 0xF40 */
                                ymf701_log(ymf701->log, "WSS base is now F40h\n");
                                ymf701->cur_wss_addr = 0xF40;
                                break;
                            case 3: /* WSBase = 0x604 */
                                ymf701_log(ymf701->log, "WSS base is now 604h\n");
                                ymf701->cur_wss_addr = 0x604;
                                break;
                            default:
                                break;
                        }
                        io_sethandler(ymf701->cur_wss_addr, 0x0004, ymf701_wss_read, NULL, NULL, ymf701_wss_write, NULL, NULL, ymf701);
                        io_sethandler(ymf701->cur_wss_addr + 0x0004, 0x0004, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &ymf701->ad1848);
                        break;
                    case 0x02: /* SB Config */
                        ymf701->regs[0x02] = val;
                        io_removehandler(ymf701->cur_sb_addr + 4, 0x0002, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, ymf701->sb);
                        ymf701_remove_opl(ymf701);
                        ymf701->cur_sb_addr = (val & 0x20) ? 0x240 : 0x220;
                        switch (val & 0x3) {
                            case 0:
                                ymf701->cur_sb_dma = -1;
                                break;
                            case 1:
                                ymf701->cur_sb_dma = 0;
                                break;
                            case 2:
                                ymf701->cur_sb_dma = 1;
                                break;
                            case 3:
                                ymf701->cur_sb_dma = 3;
                                break;
                        }
                        switch ((val >> 2) & 0x7) {
                            case 0:
                                ymf701->cur_sb_irq = -1;
                                break;
                            case 1:
                                ymf701->cur_sb_irq = 5;
                                break;
                            case 2:
                                ymf701->cur_sb_irq = 7;
                                break;
                            case 3:
                                ymf701->cur_sb_irq = 9;
                                break;
                            case 4:
                                ymf701->cur_sb_irq = 10;
                                break;
                            case 5:
                                ymf701->cur_sb_irq = 11;
                                break;
                            default:
                                break;
                        }
                        sb_dsp_setaddr(&ymf701->sb->dsp, ymf701->cur_sb_addr);
                        sb_dsp_setirq(&ymf701->sb->dsp, ymf701->cur_sb_irq);
                        sb_dsp_setdma8(&ymf701->sb->dsp, ymf701->cur_sb_dma);
                        ymf701_add_opl(ymf701);
                        if (ymf701->cur_sb_addr != 0x00)
                            io_sethandler(ymf701->cur_sb_addr + 4, 0x0002, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, ymf701->sb);
                        break;
                    case 0x03: /* MPU/OPL/Gameport Config */
                        ymf701->regs[0x03] = val;
                        switch ((val >> 2) & 0x7) {
                            case 0:
                                ymf701->cur_mpu401_irq = -1;
                                break;
                            case 1:
                                ymf701->cur_mpu401_irq = 5;
                                break;
                            case 2:
                                ymf701->cur_mpu401_irq = 7;
                                break;
                            case 3:
                                ymf701->cur_mpu401_irq = 9;
                                break;
                            case 4:
                                ymf701->cur_mpu401_irq = 10;
                                break;
                            default:
                                break;
                        }
                        switch ((val >> 5) & 0x3) {
                            case 0:
                                ymf701->cur_mpu401_addr = 0x330;
                                break;
                            case 1:
                                ymf701->cur_mpu401_addr = 0x332;
                                break;
                            case 2:
                                ymf701->cur_mpu401_addr = 0x334;
                                break;
                            case 3:
                                ymf701->cur_mpu401_addr = 0x300;
                                break;
                            default:
                                break;
                        }
                        mpu401_change_addr(ymf701->mpu, ymf701->cur_mpu401_addr);
                        mpu401_setirq(ymf701->mpu, ymf701->cur_mpu401_irq);
                        gameport_remap(ymf701->gameport, (ymf701->regs[3] & 0x1) ? 0x200 : 0x00);
                        break;
                    case 0x04: /* LSI Version Register, bit 0 sets SB DSP version */
                        ymf701->regs[0x04] = val & 0x01;
                        ymf701->sb->dsp.opl3sa_dsp_ver = (ymf701->regs[0x04] & 0x01) ? 2 : 3;
                        break;
                    default:
                        break;
                }
                ymf701->passwd_phase = 0x02;
                ymf701_log(ymf701->log, "Passwd phase 2\n");
            default:
                break;
        }
    }
    ymf701_log(ymf701->log, "Write: addr = %02X, val = %02X\n", addr, val);
    if ((ymf701->reg_enabled) && (ymf701->passwd_phase == 0x02)) {
        ymf701->reg_enabled = 0;
        ymf701->passwd_phase = 0x00;
        ymf701_log(ymf701->log, "Disabling reg access\n");
    }
    if ((addr == 0xF86) && (val == 0x1D) && (!ymf701->reg_enabled)) {
        ymf701->reg_enabled = 1;
        ymf701_log(ymf701->log, "Enabling reg access\n");
    }
}

static uint8_t
ymf701_reg_read(uint16_t addr, void *priv)
{
    ymf701_t *ymf701 = (ymf701_t *) priv;
    uint8_t   temp   = 0xFF;

    if (ymf701->reg_enabled) {
        switch (addr) {
            case 0xF86:
                temp = ymf701->index;
                break;
            case 0xF87:
                temp = ymf701->regs[ymf701->index];
                /* Only goes into phase 2 on data reads? */
                ymf701->passwd_phase = 0x02;
                ymf701_log(ymf701->log, "Passwd phase 2\n");
                break;
            default:
                break;
        }
        ymf701_log(ymf701->log, "Read with reg access enabled:\n");
        ymf701_log(ymf701->log, "addr = %02X, ret = %02X\n", addr, temp);
    }
    if ((ymf701->reg_enabled) && (ymf701->passwd_phase == 0x02)) {
        ymf701->reg_enabled = 0;
        ymf701->passwd_phase = 0x00;
        ymf701_log(ymf701->log, "Disabling reg access\n");
    }
    ymf701_log(ymf701->log, "Read: addr = %02X, ret = %02X\n", addr, temp);
    return temp;
}

static void *
ymf701_init(const device_t *info)
{
    ymf701_t *ymf701 = calloc(1, sizeof(ymf701_t));

    ymf701->type = info->local & 0xFF;

    ymf701->cur_wss_addr       = 0x530;
    ymf701->cur_mode           = 1;
    ymf701->cur_sb_addr        = 0x220;
    ymf701->cur_sb_irq         = 5;
    ymf701->cur_wss_enabled    = 1;
    ymf701->cur_sb_dma         = 1;
    ymf701->cur_mpu401_irq     = 9;
    ymf701->cur_mpu401_addr    = 0x330;
    ymf701->cur_mpu401_enabled = 1;
    ymf701->cur_wss_dma        = 0;
    ymf701->cur_wss_irq        = 11;

    /* Power-on default values are unknown, using BIOS-initialized values from an Intel Ruby board */
    ymf701->regs[0] = 0xFF; /* Index 0 is unused, return 0xFF */
    ymf701->regs[1] = 0x24;
    ymf701->regs[2] = 0x46;
    ymf701->regs[3] = 0x87;
    ymf701->regs[4] = 0x00; /* LSI version register, always returns 0 */

    ymf701->log = log_open("YMF701");

    ymf701->gameport = gameport_add(&gameport_pnp_device);
    gameport_remap(ymf701->gameport, (ymf701->regs[3] & 0x1) ? 0x200 : 0x00);

    ad1848_init(&ymf701->ad1848, AD1848_TYPE_CS4231);

    ad1848_setirq(&ymf701->ad1848, ymf701->cur_wss_irq);
    ad1848_setdma(&ymf701->ad1848, ymf701->cur_wss_dma);

    io_sethandler(0xF86, 2, ymf701_reg_read, NULL, NULL, ymf701_reg_write, NULL, NULL, ymf701);

    io_sethandler(ymf701->cur_wss_addr, 0x0004, ymf701_wss_read, NULL, NULL, ymf701_wss_write, NULL, NULL, ymf701);
    io_sethandler(ymf701->cur_wss_addr + 0x0004, 0x0004, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &ymf701->ad1848);

    ymf701->sb              = calloc(1, sizeof(sb_t));
    ymf701->sb->opl_enabled = 1;

    sb_dsp_set_real_opl(&ymf701->sb->dsp, 1);
    sb_dsp_init(&ymf701->sb->dsp, SBPRO_DSP_301, SB_SUBTYPE_YMF7XX, ymf701);
    sb_dsp_setaddr(&ymf701->sb->dsp, ymf701->cur_sb_addr);
    sb_dsp_setirq(&ymf701->sb->dsp, ymf701->cur_sb_irq);
    sb_dsp_setdma8(&ymf701->sb->dsp, ymf701->cur_sb_dma);
    sb_ct1345_mixer_reset(ymf701->sb);
    ymf701->sb->dsp.opl3sa_dsp_ver = 3;

    ymf701->sb->opl_mixer = ymf701;
    ymf701->sb->opl_mix   = ymf701_filter_opl;

    fm_driver_get_cs(FM_YMF289B, &ymf701->sb->opl);
    io_sethandler(ymf701->cur_sb_addr + 0, 0x0004, ymf701->sb->opl.read, NULL, NULL, ymf701->sb->opl.write, NULL, NULL, ymf701->sb->opl.priv);
    io_sethandler(ymf701->cur_sb_addr + 8, 0x0002, ymf701->sb->opl.read, NULL, NULL, ymf701->sb->opl.write, NULL, NULL, ymf701->sb->opl.priv);
    io_sethandler(0x0388, 0x0004, ymf701->sb->opl.read, NULL, NULL, ymf701->sb->opl.write, NULL, NULL, ymf701->sb->opl.priv);

    io_sethandler(ymf701->cur_sb_addr + 4, 0x0002, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, ymf701->sb);

    sound_add_handler(ymf701_get_buffer, ymf701);
    sound_add_handler(ymf701_get_sbpro_buffer, ymf701);
    sound_in_add_handler(ymf701_put_buffer, ymf701);
    sound_in_start_input();

    music_add_handler(sb_get_music_buffer_sbpro, ymf701->sb);
    ad1848_set_cd_audio_channel(&ymf701->ad1848, AD1848_AUX1);
    sound_set_cd_audio_filter(ad1848_filter_cd_audio, &ymf701->ad1848);

    ymf701->mpu = (mpu_t *) calloc(1, sizeof(mpu_t));
    mpu401_init(ymf701->mpu, ymf701->cur_mpu401_addr, ymf701->cur_mpu401_irq, M_UART, device_get_config_int("receive_input401"));

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, sb_dsp_input_remain, &ymf701->sb->dsp);

    double attenuation;
    for (uint8_t c = 0; c < 16; c++) {
        attenuation = 0.0;
        if (c & 0x01)
            attenuation += 1.5;
        if (c & 0x02)
            attenuation += 3.0;
        if (c & 0x04)
            attenuation += 6.0;
        if (c & 0x08)
            attenuation += 12.0;

        attenuation = pow(10, attenuation / 10);

        ymf701_input_gain_vols_4bits[c] = (int) (attenuation);
    }

    return ymf701;
}

static void
ymf701_close(void *priv)
{
    ymf701_t *ymf701 = (ymf701_t *) priv;

    if (ymf701->log != NULL) {
        log_close(ymf701->log);
        ymf701->log = NULL;
    }

    sb_close(ymf701->sb);
    free(ymf701->mpu);
    free(priv);
}

static void
ymf701_speed_changed(void *priv)
{
    ymf701_t *ymf701 = (ymf701_t *) priv;

    ad1848_speed_changed(&ymf701->ad1848);
    sb_speed_changed(ymf701->sb);
}

static const device_config_t ymf701_config[] = {
  // clang-format off
    {
        .name           = "receive_input",
        .description    = "Receive MIDI input",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "receive_input401",
        .description    = "Receive MIDI input (MPU-401)",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t ymf701_device = {
    .name          = "Yamaha YMF-701 (OPL3-SA)",
    .internal_name = "ymf701",
    .flags         = DEVICE_ISA16 | DEVICE_AUDIO_IN,
    .local         = 0x00,
    .init          = ymf701_init,
    .close         = ymf701_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = ymf701_speed_changed,
    .force_redraw  = NULL,
    .config        = ymf701_config
};
