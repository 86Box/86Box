/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          AD1848 / CS4248 / CS4231 (Windows Sound System) codec emulation.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          TheCollector1995, <mariogplayer@gmail.com>
 *          RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2018-2020 TheCollector1995.
 *          Copyright 2021-2025 RichardG.
 */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/sound.h>
#include <86box/snd_ad1848.h>
#include <86box/plat_fallthrough.h>

#define CS4231 0x80
#define CS4232 0x02
#define CS4236 0x03

#ifdef ENABLE_AD1848_LOG
int ad1848_do_log = ENABLE_AD1848_LOG;

static void
ad1848_log(const char *fmt, ...)
{
    va_list ap;

    if (ad1848_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ad1848_log(fmt, ...)
#endif

static int    ad1848_vols_7bits[128];
static double ad1848_vols_5bits_aux_gain[32];

/* Borrowed from ffmpeg. */
static const int8_t adpcm_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};
static const int16_t adpcm_step_table[89] = {
        7,     8,     9,    10,    11,    12,    13,    14,    16,    17,
       19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
       50,    55,    60,    66,    73,    80,    88,    97,   107,   118,
      130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
      337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
      876,   963,  1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
     2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
     5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

void
ad1848_setirq(ad1848_t *ad1848, int irq)
{
    ad1848_log("AD1848: setirq(%d)\n", irq);
    ad1848->irq = irq;
}

void
ad1848_setdma(ad1848_t *ad1848, int newdma)
{
    ad1848_log("AD1848: setdma(%d)\n", newdma);
    ad1848->dma = newdma;
}

void
ad1848_updatevolmask(ad1848_t *ad1848)
{
    if ((ad1848->type == AD1848_TYPE_CS4236B) && !(ad1848->xregs[4] & 0x10) && !ad1848->wten)
        ad1848->wave_vol_mask = 0x7f;
    else
        ad1848->wave_vol_mask = 0x3f;
    ad1848_log("AD1848: updatevolmask(%02X)\n", ad1848->wave_vol_mask);
}

static double
ad1848_get_default_freq(ad1848_t *ad1848)
{
    double freq = (ad1848->regs[8] & 1) ? 16934400.0 : 24576000.0;

    switch ((ad1848->regs[8] >> 1) & 7) {
        default:
            break;

        case 0:
            freq /= 3072.0;
            break;
        case 1:
            freq /= 1536.0;
            break;
        case 2:
            freq /= 896.0;
            break;
        case 3:
            freq /= 768.0;
            break;
        case 4:
            freq /= 448.0;
            break;
        case 5:
            freq /= 384.0;
            break;
        case 6:
            freq /= 512.0;
            break;
        case 7:
            freq /= 2560.0;
            break;
    }

    ad1848_log("AD1848: Frequency %f through default path\n", freq);

    return freq;
}

static void
ad1848_updatefreq(ad1848_t *ad1848)
{
    double  freq;

    if (ad1848->type >= AD1848_TYPE_CS4232) {
        if (ad1848->xregs[11] & 0x20) { /* CS4236B+ only */
            freq = 16934400.0;
            switch (ad1848->xregs[13]) {
                default:
                    freq /= 16.0 * MAX(ad1848->xregs[13], 21);
                    break;
                case 1:
                    freq /= 353.0;
                    break;
                case 2:
                    freq /= 529.0;
                    break;
                case 3:
                    freq /= 617.0;
                    break;
                case 4:
                    freq /= 1058.0;
                    break;
                case 5:
                    freq /= 1764.0;
                    break;
                case 6:
                    freq /= 2117.0;
                    break;
                case 7:
                    freq /= 2558.0;
                    break;
            }

            ad1848_log("AD1848: Frequency %f through CS4236B+ path\n", freq);
        } else if (ad1848->regs[22] & 0x80) {
            const uint8_t set = (ad1848->regs[22] >> 1) & 0x3f;
            freq = (ad1848->regs[22] & 1) ? 33868800.0 : 49152000.0;
            switch (ad1848->regs[10] & 0x30) {
                default:
                    break;

                case 0x00:
                    freq /= 128 * set;
                    break;
                case 0x10:
                    freq /= 64 * set;
                    break;
                case 0x20:
                    freq /= 256 * set;
                    break;
            }

            ad1848_log("AD1848: Frequency %f through CS4232+ path\n", freq);
        } else
            freq = ad1848_get_default_freq(ad1848);
    } else
        freq = ad1848_get_default_freq(ad1848);

    ad1848->freq        = (int) trunc(freq);
    ad1848->timer_latch = (uint64_t) ((double) TIMER_USEC * (1000000.0 / (double) ad1848->freq));
}

uint8_t
ad1848_read(uint16_t addr, void *priv)
{
    ad1848_t *ad1848 = (ad1848_t *) priv;
    uint8_t   ret    = 0xff;
    uint8_t   temp   = 0;

    switch (addr & 3) {
        case 0: /* Index */
            ret = ad1848->index | ad1848->trd | ad1848->mce;
            break;

        case 1:
            ret = ad1848->regs[ad1848->index];
            switch (ad1848->index) {
                case 11:
                    ret ^= 0x20;
                    ad1848->regs[ad1848->index] = ret;
                    break;

                case 18 ... 19:
                    if (ad1848->type >= AD1848_TYPE_CS4236B) {
                        if ((ad1848->xregs[4] & 0x14) == 0x14)               /* FM remapping */
                            ret = ad1848->xregs[6 | (ad1848->index & 1)];    /* real FM volume on registers 6 and 7 */
                        else if (ad1848->wten && !(ad1848->xregs[4] & 0x08)) /* wavetable remapping */
                            ret = ad1848->xregs[16 | (ad1848->index & 1)];   /* real wavetable volume on registers 16 and 17 */
                    }
                    break;

                case 20 ... 21:
                    /* Backdoor to the Control/RAM registers on CS4235+. */
                    if ((ad1848->type >= AD1848_TYPE_CS4235) && (ad1848->xregs[18] & 0x80))
                        ret = ad1848->cram_read(ad1848->index - 15, ad1848->cram_priv);
                    break;

                case 23:
                    if ((ad1848->type >= AD1848_TYPE_CS4236B) && (ad1848->regs[23] & 0x08)) {
                        ret = ad1848->xregs[ad1848->xindex];
                        switch (ad1848->xindex) {
                            case 0 ... 1:
                                /* Remapped line volume. */
                                ret = ad1848->regs[18 + ad1848->xindex];
                                break;

                            case 23 ... 24:
                            case 29:
                                /* Backdoor to control indirect registers on CS4235+. */
                                if (ad1848->type >= AD1848_TYPE_CS4235) {
                                    temp = ad1848->cram_read(3, ad1848->cram_priv);
                                    ad1848->cram_write(3, (ad1848->xindex == 23) ? 2 : ((ad1848->xindex == 24) ? 8 : 9), ad1848->cram_priv);
                                    ret = ad1848->cram_read(4, ad1848->cram_priv);
                                    ad1848->cram_write(3, temp, ad1848->cram_priv);
                                }
                                break;

                            case 26 ... 28:
                            case 30:
                                /* Backdoor to control registers on CS4235+. */
                                if (ad1848->type >= AD1848_TYPE_CS4235)
                                    ret = ad1848->cram_read((ad1848->xindex == 30) ? 7 : (ad1848->xindex - 26), ad1848->cram_priv);
                                break;

                            default:
                                break;
                        }
                        ad1848_log("AD1848: read(X%d) = %02X\n", ad1848->xindex, ret);
                        return ret;
                    }
                    break;

                default:
                    break;
            }
            ad1848_log("AD1848: read(I%d) = %02X\n", ad1848->index, ret);
            return ret;

        case 2:
            ret = ad1848->status;
            break;

        default:
            break;
    }

    ad1848_log("AD1848: read(%04X) = %02X\n", addr, ret);

    return ret;
}

void
ad1848_write(uint16_t addr, uint8_t val, void *priv)
{
    ad1848_t *ad1848 = (ad1848_t *) priv;
    uint8_t   temp = 0;
    uint8_t   updatefreq = 0;
    double    i8_timebase = 0;

    switch (addr & 3) {
        case 0: /* Index */
            if ((ad1848->regs[12] & 0x40) && (ad1848->type >= AD1848_TYPE_CS4231))
                ad1848->index = val & 0x1f; /* cs4231a extended mode enabled */
            else
                ad1848->index = val & 0x0f; /* ad1848/cs4248 mode TODO: some variants/clones DO NOT mirror, just ignore the writes? */
            if (ad1848->type >= AD1848_TYPE_CS4236B)
                ad1848->regs[23] &= ~0x08; /* clear XRAE */
            ad1848->trd = val & 0x20;
            ad1848->mce = val & 0x40;
            break;

        case 1:
            switch (ad1848->index) {
                case 10:
                    if (ad1848->type < AD1848_TYPE_CS4232)
                        break;
                    fallthrough;

                case 8:
                    updatefreq = 1;
                    break;

                case 9:
                    if (!ad1848->enable && (val & 0x41) == 0x01) {
                        ad1848->adpcm_pos = 0;
                        ad1848->adpcm_predictor[0] = ad1848->adpcm_predictor[1] = 0;
                        ad1848->adpcm_step_index[0] = ad1848->adpcm_step_index[1] = 0;
                        ad1848->dma_ff = 0;
                        if (ad1848->timer_latch)
                            timer_set_delay_u64(&ad1848->timer_count, ad1848->timer_latch);
                        else
                            timer_set_delay_u64(&ad1848->timer_count, TIMER_USEC);
                    }
                    ad1848->enable = ((val & 0x41) == 0x01);
                    if (!ad1848->enable) {
                        timer_disable(&ad1848->timer_count);
                        ad1848->out_l = ad1848->out_r = 0;
                    }
                    break;

                case 11:
                    goto readonly_i;

                case 12:
                    if (ad1848->type >= AD1848_TYPE_CS4248) {
                        ad1848->regs[12] = 0x80 | (val & 0x60) | (ad1848->regs[12] & 0x0f);
                        if ((ad1848->type >= AD1848_TYPE_CS4231) && (ad1848->type < AD1848_TYPE_CS4235)) {
                            if (val & 0x40)
                                ad1848->fmt_mask |= 0x80;
                            else
                                ad1848->fmt_mask &= ~0x80;
                        }
                    }
                    goto readonly_i;

                case 14:
                    ad1848->count = ad1848->regs[15] | (val << 8);
                    break;

                case 16:
                    if ((ad1848->type >= AD1848_TYPE_CS4231) && (ad1848->type < AD1848_TYPE_CS4235)) {
                        if (val & 0x40) {
                            ad1848_log("Timer Enable\n");
                            ad1848_log("Timer value: %04X\n", ((ad1848->regs[21] << 8) + (ad1848->regs[20])));
                            i8_timebase = (ad1848->regs[8] & 1) ? 9.92 : 9.969;
                            timer_set_delay_u64(&ad1848->cs4231a_irq_timer, (((ad1848->regs[21] << 8) + (ad1848->regs[20])) * i8_timebase * TIMER_USEC));
                        }
                        else {
                            ad1848_log("Timer Disable\n");
                            timer_disable(&ad1848->cs4231a_irq_timer);
                        }
                    }
                    break;

                case 17:
                    if (val & 0x08)
                        ad1848->adpcm_predictor[0] = ad1848->adpcm_predictor[1] = 0;
                    break;

                case 18 ... 19:
                    if (ad1848->type >= AD1848_TYPE_CS4236B) {
                        if (ad1848->type >= AD1848_TYPE_CS4235) {
                            if (ad1848->xregs[18] & 0x20)              /* AUX1 remapping */
                                ad1848->regs[ad1848->index & 3] = val; /* also controls AUX1 on registers 2 and 3 */
                        } else {
                            temp = 0;
                            if ((ad1848->xregs[4] & 0x14) == 0x14) {          /* FM remapping */
                                ad1848->xregs[6 | (ad1848->index & 1)] = val; /* real FM volume on extended registers 6 and 7 */
                                temp                                   = 1;

                                if (ad1848->index == 18) {
                                    if (val & 0x80)
                                        ad1848->fm_vol_l = 0;
                                    else
                                        ad1848->fm_vol_l = ad1848_vols_7bits[val & 0x3f];
                                } else {
                                    if (val & 0x80)
                                        ad1848->fm_vol_r = 0;
                                    else
                                        ad1848->fm_vol_r = ad1848_vols_7bits[val & 0x3f];
                                }
                            }
                            if (ad1848->wten && !(ad1848->xregs[4] & 0x08)) {  /* wavetable remapping */
                                ad1848->xregs[16 | (ad1848->index & 1)] = val; /* real wavetable volume on extended registers 16 and 17 */
                                temp                                    = 1;
                            }

                            /* Stop here if any remapping is enabled. */
                            if (temp)
                                goto readonly_i;
                        }

                        /* HACK: the Windows 9x driver's "Synth" control writes to this
                           register with no remapping, even if internal FM is enabled. */
                        if (ad1848->index == 18) {
                            if (val & 0x80)
                                ad1848->fm_vol_l = 0;
                            else
                                ad1848->fm_vol_l = (int) ad1848_vols_5bits_aux_gain[val & 0x1f];
                        } else {
                            if (val & 0x80)
                                ad1848->fm_vol_r = 0;
                            else
                                ad1848->fm_vol_r = (int) ad1848_vols_5bits_aux_gain[val & 0x1f];
                        }
                    }
                    if ((ad1848->type >= AD1848_TYPE_CS4232) && (ad1848->type <= AD1848_TYPE_CS4236)) {
                        if (ad1848->index == 18) {
                            if (val & 0x80)
                                ad1848->fm_vol_l = 0;
                            else
                                ad1848->fm_vol_l = (int) ad1848_vols_5bits_aux_gain[val & 0x1f];
                        } else {
                            if (val & 0x80)
                                ad1848->fm_vol_r = 0;
                            else
                                ad1848->fm_vol_r = (int) ad1848_vols_5bits_aux_gain[val & 0x1f];
                        }
                    }
                    break;

                case 20 ... 21:
                    /* Backdoor to the Control/RAM registers on CS4235+. */
                    if ((ad1848->type >= AD1848_TYPE_CS4235) && (ad1848->xregs[18] & 0x80)) {
                        ad1848->cram_write(ad1848->index - 15, val, ad1848->cram_priv);
                        val = ad1848->regs[ad1848->index];
                    }
                    break;

                case 22:
                    updatefreq = 1;
                    break;

                case 23:
                    if ((ad1848->type >= AD1848_TYPE_CS4236B) && ((ad1848->regs[12] & 0x60) == 0x60)) {
                        if (!(ad1848->regs[23] & 0x08)) { /* existing (not new) XRAE is clear */
                            ad1848->xindex = ((val & 0x04) << 2) | (val >> 4);
                            break;
                        }

                        switch (ad1848->xindex) {
                            case 0 ... 1:
                                if (ad1848->type < AD1848_TYPE_CS4235) {
                                    /* Remapped line volume. */
                                    ad1848->regs[18 | ad1848->xindex] = val;
                                }
                                break;

                            case 6:
                                if (val & 0x80)
                                    ad1848->fm_vol_l = 0;
                                else
                                    ad1848->fm_vol_l = ad1848_vols_7bits[val & 0x3f];
                                break;

                            case 7:
                                if (val & 0x80)
                                    ad1848->fm_vol_r = 0;
                                else
                                    ad1848->fm_vol_r = ad1848_vols_7bits[val & 0x3f];
                                break;

                            case 11:
                            case 13:
                                updatefreq = 1;
                                break;

                            case 23 ... 24:
                            case 29:
                                /* Backdoor to control indirect registers on CS4235+. */
                                if (ad1848->type >= AD1848_TYPE_CS4235) {
                                    temp = ad1848->cram_read(3, ad1848->cram_priv);
                                    ad1848->cram_write(3, (ad1848->xindex == 23) ? 2 : ((ad1848->xindex == 24) ? 8 : 9), ad1848->cram_priv);
                                    ad1848->cram_write(4, val, ad1848->cram_priv);
                                    ad1848->cram_write(3, temp, ad1848->cram_priv);
                                }
                                break;

                            case 25:
                                goto readonly_x;

                            case 26 ... 28:
                            case 30:
                                /* Backdoor to control registers on CS4235+. */
                                if (ad1848->type >= AD1848_TYPE_CS4235)
                                    ad1848->cram_write((ad1848->xindex == 30) ? 7 : (ad1848->xindex - 26), val, ad1848->cram_priv);
                                break;

                            default:
                                break;
                        }
                        ad1848->xregs[ad1848->xindex] = val;

                        if (updatefreq)
                            ad1848_updatefreq(ad1848);

readonly_x:
                        ad1848_log("AD1848: write(X%d, %02X)\n", ad1848->xindex, val);
                        return;
                    }
                    if (ad1848->type == AD1848_TYPE_CS4231) /* I23 is reserved and read-only on CS4231 non-A */
                        goto readonly_i;
                    if ((ad1848->type >= AD1848_TYPE_CS4232) && (ad1848->type <= AD1848_TYPE_CS4236)) /* I23 bits 7-1 are read-only on CS4231A/4232/4236 non-B, Win2k relies on this for detection */
                        val = (val & 0x01);
                    break;

                case 24:
                    val = ad1848->regs[24] & ((val & 0x70) | 0x0f);
                    if (!(val & 0x70)) {
                        ad1848->status &= 0xfe;
                        picintc(1 << ad1848->irq);
                    }
                    break;

                case 25:
                    goto readonly_i;
                case 27:
                    if ((ad1848->type != AD1848_TYPE_CS4232) && (ad1848->type != AD1848_TYPE_CS4236))
                        goto readonly_i;
                    break;
                case 29:
                    if ((ad1848->type != AD1848_TYPE_CS4232) && (ad1848->type != AD1848_TYPE_CS4236))
                        goto readonly_i;
                    break;

                default:
                    break;
            }
            ad1848->regs[ad1848->index] = val;

            if (updatefreq)
                ad1848_updatefreq(ad1848);

            if (ad1848->cd_vol_reg > -1) {
                if (ad1848->regs[ad1848->cd_vol_reg] & 0x80)
                    ad1848->cd_vol_l = 0;
                else
                    ad1848->cd_vol_l = ad1848_vols_5bits_aux_gain[ad1848->regs[ad1848->cd_vol_reg] & 0x1f];
                if (ad1848->regs[ad1848->cd_vol_reg + 1] & 0x80)
                    ad1848->cd_vol_r = 0;
                else
                    ad1848->cd_vol_r = ad1848_vols_5bits_aux_gain[ad1848->regs[ad1848->cd_vol_reg + 1] & 0x1f];
            }

readonly_i:
            ad1848_log("AD1848: write(I%d, %02X)\n", ad1848->index, val);
            return;

        case 2:
            ad1848->status &= 0xfe;
            ad1848->regs[24] &= 0x0f;
            break;

        default:
            break;
    }

    ad1848_log("AD1848: write(%04X, %02X)\n", addr, val);
}

void
ad1848_speed_changed(ad1848_t *ad1848)
{
    ad1848->timer_latch = (uint64_t) ((double) TIMER_USEC * (1000000.0 / (double) ad1848->freq));
}

void
ad1848_update(ad1848_t *ad1848)
{
    for (; ad1848->pos < sound_pos_global; ad1848->pos++) {
        ad1848->buffer[ad1848->pos * 2]     = ad1848->out_l;
        ad1848->buffer[ad1848->pos * 2 + 1] = ad1848->out_r;
    }
}

static int16_t
ad1848_process_mulaw(uint8_t byte)
{
    byte        = ~byte;
    int temp    = (((byte & 0x0f) << 3) + 0x84);
    temp <<= ((byte & 0x70) >> 4);
    temp = (byte & 0x80) ? (0x84 - temp) : (temp - 0x84);
    if (temp > 32767)
        return 32767;
    else if (temp < -32768)
        return -32768;
    return (int16_t) temp;
}

static int16_t
ad1848_process_alaw(uint8_t byte)
{
    byte ^= 0x55;
    int           dec = ((byte & 0x0f) << 4);;
    const int     seg = (int) ((byte & 0x70) >> 4);
    switch (seg) {
        default:
            dec |= 0x108;
            dec <<= seg - 1;
            break;

        case 0:
            dec |= 0x8;
            break;

        case 1:
            dec |= 0x108;
            break;
    }
    return (int16_t) ((byte & 0x80) ? dec : -dec);
}

static uint32_t
ad1848_dma_channel_read(ad1848_t *ad1848, int channel)
{
    uint32_t ret;

    if (channel >= 4) {
       if (ad1848->dma_ff) {
           ret = (ad1848->dma_data & 0xff00) >> 8;
           ret |= (ad1848->dma_data & 0xffff0000);
       } else {
           ad1848->dma_data = dma_channel_read(channel);

           if (ad1848->dma_data == DMA_NODATA)
               return DMA_NODATA;

           ret = ad1848->dma_data & 0xff;
       }

       ad1848->dma_ff = !ad1848->dma_ff;
    } else
        ret = dma_channel_read(channel);

    return ret;
}

static int16_t
ad1848_process_adpcm(ad1848_t *ad1848, int channel)
{
    int temp;
    if (ad1848->adpcm_pos++ & 1) {
        temp = ad1848->adpcm_data >> 4;
    } else {
        ad1848->adpcm_data = ad1848_dma_channel_read(ad1848, ad1848->dma);
        temp               = ad1848->adpcm_data & 0x0f;
    }

    int step       = adpcm_step_table[ad1848->adpcm_step_index[channel]];
    int step_index = ad1848->adpcm_step_index[channel] + adpcm_index_table[temp];
    if (step_index < 0)
        step_index = 0;
    else if (step_index > 88)
        step_index = 88;

    int diff      = ((2 * (temp & 7) + 1) * step) >> 3;
    int predictor = ad1848->adpcm_predictor[channel] + ((temp & 8) ? -diff : diff);
    if (predictor < -32768)
        predictor = -32768;
    else if (predictor > 32767)
        predictor = 32767;
    ad1848->adpcm_predictor[channel]  = predictor;
    ad1848->adpcm_step_index[channel] = step_index;

    return (int16_t) predictor;
}

static void
ad1848_poll(void *priv)
{
    ad1848_t *ad1848 = (ad1848_t *) priv;

    if (ad1848->timer_latch)
        timer_advance_u64(&ad1848->timer_count, ad1848->timer_latch);
    else
        timer_advance_u64(&ad1848->timer_count, TIMER_USEC * 1000);

    ad1848_update(ad1848);

    if (ad1848->enable) {
        int32_t temp;

        switch (ad1848->regs[8] & ad1848->fmt_mask) {
            case 0x00: /* Mono, 8-bit PCM */
                ad1848->out_l = ad1848->out_r = (int16_t) ((ad1848_dma_channel_read(ad1848, ad1848->dma) ^ 0x80) << 8);
                break;

            case 0x10: /* Stereo, 8-bit PCM */
                ad1848->out_l = (int16_t) ((ad1848_dma_channel_read(ad1848, ad1848->dma) ^ 0x80) << 8);
                ad1848->out_r = (int16_t) ((ad1848_dma_channel_read(ad1848, ad1848->dma) ^ 0x80) << 8);
                break;

            case 0x20: /* Mono, 8-bit Mu-Law */
                ad1848->out_l = ad1848->out_r = ad1848_process_mulaw(ad1848_dma_channel_read(ad1848, ad1848->dma));
                break;

            case 0x30: /* Stereo, 8-bit Mu-Law */
                ad1848->out_l = ad1848_process_mulaw(ad1848_dma_channel_read(ad1848, ad1848->dma));
                ad1848->out_r = ad1848_process_mulaw(ad1848_dma_channel_read(ad1848, ad1848->dma));
                break;

            case 0x40: /* Mono, 16-bit PCM little endian */
                temp          = (int32_t) ad1848_dma_channel_read(ad1848, ad1848->dma);
                ad1848->out_l = ad1848->out_r = (int16_t) ((ad1848_dma_channel_read(ad1848, ad1848->dma) << 8) | temp);
                break;

            case 0x50: /* Stereo, 16-bit PCM little endian */
                temp          = (int32_t) ad1848_dma_channel_read(ad1848, ad1848->dma);
                ad1848->out_l = (int16_t) ((ad1848_dma_channel_read(ad1848, ad1848->dma) << 8) | temp);
                temp          = (int32_t) ad1848_dma_channel_read(ad1848, ad1848->dma);
                ad1848->out_r = (int16_t) ((ad1848_dma_channel_read(ad1848, ad1848->dma) << 8) | temp);
                break;

            case 0x60: /* Mono, 8-bit A-Law */
                ad1848->out_l = ad1848->out_r = ad1848_process_alaw(ad1848_dma_channel_read(ad1848, ad1848->dma));
                break;

            case 0x70: /* Stereo, 8-bit A-Law */
                ad1848->out_l = ad1848_process_alaw(ad1848_dma_channel_read(ad1848, ad1848->dma));
                ad1848->out_r = ad1848_process_alaw(ad1848_dma_channel_read(ad1848, ad1848->dma));
                break;

                /* 0x80 and 0x90 reserved */

            case 0xa0: /* Mono, 4-bit ADPCM */
                ad1848->out_l = ad1848->out_r = ad1848_process_adpcm(ad1848, 0);
                break;

            case 0xb0: /* Stereo, 4-bit ADPCM */
                ad1848->out_l = ad1848_process_adpcm(ad1848, 0);
                ad1848->out_r = ad1848_process_adpcm(ad1848, 1);
                break;

            case 0xc0: /* Mono, 16-bit PCM big endian */
                temp          = (int32_t) ad1848_dma_channel_read(ad1848, ad1848->dma);
                ad1848->out_l = ad1848->out_r = (int16_t) (ad1848_dma_channel_read(ad1848, ad1848->dma) | (temp << 8));
                break;

            case 0xd0: /* Stereo, 16-bit PCM big endian */
                temp          = (int32_t) ad1848_dma_channel_read(ad1848, ad1848->dma);
                ad1848->out_l = (int16_t) (ad1848_dma_channel_read(ad1848, ad1848->dma) | (temp << 8));
                temp          = (int32_t) ad1848_dma_channel_read(ad1848, ad1848->dma);
                ad1848->out_r = (int16_t) (ad1848_dma_channel_read(ad1848, ad1848->dma) | (temp << 8));
                break;

                /* 0xe0 and 0xf0 reserved */

            default:
                break;
        }

        if (ad1848->regs[6] & 0x80)
            ad1848->out_l = 0;
        else
            ad1848->out_l = (int16_t) ((ad1848->out_l * ad1848_vols_7bits[ad1848->regs[6] & ad1848->wave_vol_mask]) >> 16);

        if (ad1848->regs[7] & 0x80)
            ad1848->out_r = 0;
        else
            ad1848->out_r = (int16_t) ((ad1848->out_r * ad1848_vols_7bits[ad1848->regs[7] & ad1848->wave_vol_mask]) >> 16);

        if (ad1848->count < 0) {
            ad1848->count     = ad1848->regs[15] | (ad1848->regs[14] << 8);
            ad1848->adpcm_pos = 0;
            if (!(ad1848->status & 0x01)) {
                ad1848->status |= 0x01;
                ad1848->regs[24] |= 0x10;
            }
            if (ad1848->regs[10] & 2)
                picint(1 << ad1848->irq);
            else
                picintc(1 << ad1848->irq);
        }

        if (!(ad1848->adpcm_pos & 7)) /* ADPCM counts down every 4 bytes */
            ad1848->count--;
    } else {
        ad1848->out_l = ad1848->out_r = 0;
        ad1848->cd_vol_l = ad1848->cd_vol_r = 0;
    }
}

void
cs4231a_irq_poll(void *priv)
{
    ad1848_t *ad1848 = (ad1848_t *) priv;
    ad1848_log("Firing timer IRQ\n");
    picint(1 << ad1848->irq);
    ad1848_log("Setting timer interrupt bit in I24\n");
    ad1848->regs[24] |= 0x40;
}

void
ad1848_set_cd_audio_channel(void *priv, int channel)
{
    ad1848_t *ad1848 = (ad1848_t *) priv;

    const int max_channel = (ad1848->type >= AD1848_TYPE_CS4231) ? 31 : 15;
    if (channel > max_channel)
        channel = max_channel;

    ad1848->cd_vol_reg = channel;
}

void
ad1848_filter_cd_audio(int channel, double *buffer, void *priv)
{
    const ad1848_t *ad1848 = (ad1848_t *) priv;
    double          c;
    double          volume = channel ? ad1848->cd_vol_r : ad1848->cd_vol_l;

    c       = ((*buffer) * volume) / 65536.0;
    *buffer = c;
}

void
ad1848_filter_channel(void *priv, int channel, double *out_l, double *out_r)
{
    const ad1848_t *ad1848 = (ad1848_t *) priv;

    const int max_channel = (ad1848->type >= AD1848_TYPE_CS4231) ? 31 : 15;
    if (channel > max_channel)
        channel = max_channel;

    if (ad1848->regs[channel] & 0x80) {
        *out_l = 0.0;
    } else {
        *out_l = ((*out_l) * ad1848_vols_5bits_aux_gain[ad1848->regs[channel] & 0x1f]) / 65536.0;
    }

    if (ad1848->regs[channel + 1] & 0x80) {
        *out_r = 0.0;
    } else {
        *out_r = ((*out_r) * ad1848_vols_5bits_aux_gain[ad1848->regs[channel + 1] & 0x1f]) / 65536.0;
    }
}

void
ad1848_init(ad1848_t *ad1848, uint8_t type)
{
    uint8_t c;
    double  attenuation;

    ad1848_log("AD1848: init(%02X)\n", type);

    ad1848->status = 0xcc;
    ad1848->index = ad1848->trd = 0;
    ad1848->mce                 = 0x40;
    ad1848->wten                = 0;

    ad1848->regs[0] = ad1848->regs[1] = 0;
    ad1848->regs[2] = ad1848->regs[3] = 0x80; /* Line-in */
    ad1848->regs[4] = ad1848->regs[5] = 0x80;
    ad1848->regs[6] = ad1848->regs[7] = 0x80; /* Left/right Output */
    ad1848->regs[8]                   = 0;
    ad1848->regs[9]                   = 0x08;
    ad1848->regs[10] = ad1848->regs[11] = 0;
    ad1848->regs[12] = (type >= AD1848_TYPE_CS4248) ? 0x8a : 0xa;
    ad1848->regs[13] = 0;
    ad1848->regs[14] = ad1848->regs[15] = 0;

    if (type == AD1848_TYPE_CS4231) {
        ad1848->regs[16] = ad1848->regs[17] = 0;
        ad1848->regs[18] = ad1848->regs[19] = 0x88;
        ad1848->regs[22]                    = 0x80;
        ad1848->regs[24]                    = 0;
        ad1848->regs[25]                    = CS4231;
        ad1848->regs[26]                    = 0x80;
        ad1848->regs[29]                    = 0x80;
    } else if (type >= AD1848_TYPE_CS4232) {
        ad1848->regs[16] = ad1848->regs[17] = 0;
        ad1848->regs[18] = ad1848->regs[19] = 0;
        ad1848->regs[20] = ad1848->regs[21] = 0;
        ad1848->regs[22] = ad1848->regs[23] = 0;
        ad1848->regs[24]                    = 0;
        ad1848->regs[25]                    = (type == AD1848_TYPE_CS4232) ? CS4232 : CS4236;
        ad1848->regs[26]                    = 0xa0;
        ad1848->regs[27] = ad1848->regs[29] = 0;
        ad1848->regs[30] = ad1848->regs[31] = 0;

        if (type >= AD1848_TYPE_CS4236B) {
            if (type < AD1848_TYPE_CS4235)
                ad1848->xregs[0] = ad1848->xregs[1] = 0xe8;
            ad1848->xregs[2] = ad1848->xregs[3] = 0xcf;
            ad1848->xregs[4]                    = 0x84;
            ad1848->xregs[5]                    = 0;
            ad1848->xregs[6] = ad1848->xregs[7] = 0x80;
            ad1848->xregs[8] = ad1848->xregs[9] = 0;
            ad1848->xregs[10]                   = 0x3f;
            ad1848->xregs[11]                   = 0xc0;
            ad1848->xregs[14] = ad1848->xregs[15] = 0;
            ad1848->xregs[16] = ad1848->xregs[17] = 0;
        }
    }

    ad1848_updatefreq(ad1848);

    ad1848->out_l = ad1848->out_r = 0;
    ad1848->fm_vol_l = ad1848->fm_vol_r = 65536;
    ad1848->cd_vol_l = ad1848->cd_vol_r = 65536;
    ad1848->cd_vol_reg = -1;
    ad1848_updatevolmask(ad1848);
    if (type >= AD1848_TYPE_CS4235)
        ad1848->fmt_mask = 0x50;
    else
        ad1848->fmt_mask = 0x70;

    for (c = 0; c < 128; c++) {
        attenuation = 0.0;
        if (c & 0x40) {
            if (c < 72)
                attenuation = (c - 72) * -1.5;
        } else {
            if (c & 0x01)
                attenuation -= 1.5;
            if (c & 0x02)
                attenuation -= 3.0;
            if (c & 0x04)
                attenuation -= 6.0;
            if (c & 0x08)
                attenuation -= 12.0;
            if (c & 0x10)
                attenuation -= 24.0;
            if (c & 0x20)
                attenuation -= 48.0;
        }

        attenuation = pow(10, attenuation / 10);

        ad1848_vols_7bits[c] = (int) (attenuation * 65536);
    }

    for (c = 0; c < 32; c++) {
        attenuation = 12.0;
        if (c & 0x01)
            attenuation -= 1.5;
        if (c & 0x02)
            attenuation -= 3.0;
        if (c & 0x04)
            attenuation -= 6.0;
        if (c & 0x08)
            attenuation -= 12.0;
        if (c & 0x10)
            attenuation -= 24.0;

        attenuation = pow(10, attenuation / 10);

        ad1848_vols_5bits_aux_gain[c] = (attenuation * 65536);
    }

    ad1848->type = type;

    timer_add(&ad1848->timer_count, ad1848_poll, ad1848, 0);

    if ((ad1848->type != AD1848_TYPE_DEFAULT) && (ad1848->type != AD1848_TYPE_CS4248))
        sound_set_cd_audio_filter(ad1848_filter_cd_audio, ad1848);

    if ((ad1848->type >= AD1848_TYPE_CS4231) && (ad1848->type < AD1848_TYPE_CS4235))
        timer_add(&ad1848->cs4231a_irq_timer, cs4231a_irq_poll, ad1848, 0);
}
