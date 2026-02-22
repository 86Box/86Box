/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Analog Devices AD1816 SoundPort audio controller emulation.
 *
 *          Many similarities to the AD1848/CS423x WSS codec family but
 *          heavily modified and not register-compatible
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          TheCollector1995, <mariogplayer@gmail.com>
 *          RichardG, <richardg867@gmail.com>
 *          win2kgamer
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2018-2020 TheCollector1995.
 *          Copyright 2021-2025 RichardG.
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
#include <86box/dma.h>
#include <86box/sound.h>
#include <86box/gameport.h>
#include <86box/snd_sb.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/plat_unused.h>
#include <86box/isapnp.h>
#include <86box/log.h>

#define PNP_ROM_AD1816 "roms/sound/ad1816/ad1816.bin"

#ifdef ENABLE_AD1816_LOG
int ad1816_do_log = ENABLE_AD1816_LOG;

static void
ad1816_log(void *priv, const char *fmt, ...)
{
    if (ad1816_do_log) {
        va_list ap;
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define ad1816_log(fmt, ...)
#endif

static double ad1816_vols_5bits[32];
static double ad1816_vols_5bits_aux_gain[32];
static int ad1816_vols_6bits[64];

typedef struct ad1816_t {
    uint8_t  index;
    uint8_t  regs[16];
    uint16_t iregs[64];

    uint16_t cur_ad1816_addr;
    uint16_t cur_sb_addr;
    uint16_t cur_opl_addr;
    uint16_t cur_mpu_addr;
    uint16_t cur_js_addr;
    uint8_t  cur_irq;
    uint8_t  cur_mpu_irq;
    uint8_t  cur_dma;

    int freq;

    pc_timer_t timer_count;
    uint64_t   timer_latch;

    pc_timer_t ad1816_irq_timer;

    uint8_t status;
    int count;
    int16_t out_l;
    int16_t out_r;
    uint8_t fmt_mask;
    uint8_t dma_ff;
    uint32_t dma_data;
    int16_t buffer[SOUNDBUFLEN * 2];
    int pos;
    uint8_t playback_pos : 2;
    uint8_t enable;
    uint8_t codec_enable;

    double master_l;
    double master_r;
    double cd_vol_l;
    double cd_vol_r;
    int    fm_vol_l;
    int    fm_vol_r;

    void  *gameport;
    mpu_t *mpu;
    sb_t  *sb;

    void                   *pnp_card;
    uint8_t                pnp_rom[512];
    isapnp_device_config_t *ad1816_pnp_config;

    void * log; /* New logging system */
} ad1816_t;

static void
ad1816_update_mastervol(void *priv)
{
    ad1816_t *ad1816 = (ad1816_t *) priv;

    if (ad1816->iregs[14] & 0x8000)
        ad1816->master_l = 0;
    else
        ad1816->master_l = ad1816_vols_5bits[((ad1816->iregs[14] >> 8) & 0x1f) >> 1] / 65536.0;

    if (ad1816->iregs[14] & 0x0080)
        ad1816->master_r = 0;
    else
        ad1816->master_r = ad1816_vols_5bits[((ad1816->iregs[14]) & 0x1f) >> 1] / 65536.0;
}

void
ad1816_filter_cd_audio(int channel, double *buffer, void *priv)
{
    ad1816_t *ad1816 = (ad1816_t *) priv;

    ad1816_update_mastervol(ad1816);

    const double cd_vol = channel ? ad1816->cd_vol_r : ad1816->cd_vol_l;
    double       master = channel ? ad1816->master_r : ad1816->master_l;
    double       c      = ((*buffer  * cd_vol / 1.0) * master) / 65536.0;

    *buffer = c;
}

static void
ad1816_filter_opl(void *priv, double *out_l, double *out_r)
{
    ad1816_t *ad1816 = (ad1816_t *) priv;

    ad1816_update_mastervol(ad1816);
    *out_l *= ad1816->fm_vol_l;
    *out_r *= ad1816->fm_vol_r;
    *out_l *= ad1816->master_l;
    *out_r *= ad1816->master_r;

}

void
ad1816_update(ad1816_t *ad1816)
{
    for (; ad1816->pos < sound_pos_global; ad1816->pos++) {
        ad1816->buffer[ad1816->pos * 2]     = ad1816->out_l;
        ad1816->buffer[ad1816->pos * 2 + 1] = ad1816->out_r;
    }
}

static int16_t
ad1816_process_mulaw(uint8_t byte)
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
ad1816_process_alaw(uint8_t byte)
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
ad1816_dma_channel_read(ad1816_t *ad1816, int channel)
{
    uint32_t ret;

    if (channel >= 4) {
       if (ad1816->dma_ff) {
           ret = (ad1816->dma_data & 0xff00) >> 8;
           ret |= (ad1816->dma_data & 0xffff0000);
       } else {
           ad1816->dma_data = dma_channel_read(channel);

           if (ad1816->dma_data == DMA_NODATA)
               return DMA_NODATA;

           ret = ad1816->dma_data & 0xff;
       }

       ad1816->dma_ff = !ad1816->dma_ff;
    } else
        ret = dma_channel_read(channel);

    return ret;
}

static void
ad1816_poll(void *priv)
{
    ad1816_t *ad1816 = (ad1816_t *) priv;

    if (ad1816->timer_latch)
        timer_advance_u64(&ad1816->timer_count, ad1816->timer_latch);
    else
        timer_advance_u64(&ad1816->timer_count, TIMER_USEC * 1000);

    ad1816_update(ad1816);

    if (ad1816->enable) {
        int32_t temp;
        uint8_t format;

        format = (ad1816->regs[8] << 2) & 0xf0;
        ad1816_log(ad1816->log, "AD1816 format = %04X\n", format);
        ad1816_log(ad1816->log, "count = %04X, pos = %02X\n", ad1816->count, ad1816->playback_pos);

        switch (format) {
            case 0x00: /* Mono, 8-bit PCM */
                ad1816->out_l = ad1816->out_r = (int16_t) ((ad1816_dma_channel_read(ad1816, ad1816->cur_dma) ^ 0x80) << 8);
                ad1816->playback_pos++;
                break;

            case 0x10: /* Stereo, 8-bit PCM */
                ad1816->out_l = (int16_t) ((ad1816_dma_channel_read(ad1816, ad1816->cur_dma) ^ 0x80) << 8);
                ad1816->out_r = (int16_t) ((ad1816_dma_channel_read(ad1816, ad1816->cur_dma) ^ 0x80) << 8);
                ad1816->playback_pos += 2;
                break;

            case 0x20: /* Mono, 8-bit Mu-Law */
                ad1816->out_l = ad1816->out_r = ad1816_process_mulaw(ad1816_dma_channel_read(ad1816, ad1816->cur_dma));
                ad1816->playback_pos++;
                break;

            case 0x30: /* Stereo, 8-bit Mu-Law */
                ad1816->out_l = ad1816_process_mulaw(ad1816_dma_channel_read(ad1816, ad1816->cur_dma));
                ad1816->out_r = ad1816_process_mulaw(ad1816_dma_channel_read(ad1816, ad1816->cur_dma));
                ad1816->playback_pos += 2;
                break;

            case 0x40: /* Mono, 16-bit PCM little endian */
                temp          = (int32_t) ad1816_dma_channel_read(ad1816, ad1816->cur_dma);
                ad1816->out_l = ad1816->out_r = (int16_t) ((ad1816_dma_channel_read(ad1816, ad1816->cur_dma) << 8) | temp);
                ad1816->playback_pos += 2;
                break;

            case 0x50: /* Stereo, 16-bit PCM little endian */
                temp          = (int32_t) ad1816_dma_channel_read(ad1816, ad1816->cur_dma);
                ad1816->out_l = (int16_t) ((ad1816_dma_channel_read(ad1816, ad1816->cur_dma) << 8) | temp);
                temp          = (int32_t) ad1816_dma_channel_read(ad1816, ad1816->cur_dma);
                ad1816->out_r = (int16_t) ((ad1816_dma_channel_read(ad1816, ad1816->cur_dma) << 8) | temp);
                ad1816->playback_pos += 4;
                break;

            case 0x60: /* Mono, 8-bit A-Law */
                ad1816->out_l = ad1816->out_r = ad1816_process_alaw(ad1816_dma_channel_read(ad1816, ad1816->cur_dma));
                ad1816->playback_pos++;
                break;

            case 0x70: /* Stereo, 8-bit A-Law */
                ad1816->out_l = ad1816_process_alaw(ad1816_dma_channel_read(ad1816, ad1816->cur_dma));
                ad1816->out_r = ad1816_process_alaw(ad1816_dma_channel_read(ad1816, ad1816->cur_dma));
                ad1816->playback_pos += 2;
                break;

                /* 0x80, 0x90, 0xa0, 0xb0 reserved */

            case 0xc0: /* Mono, 16-bit PCM big endian */
                temp          = (int32_t) ad1816_dma_channel_read(ad1816, ad1816->cur_dma);
                ad1816->out_l = ad1816->out_r = (int16_t) (ad1816_dma_channel_read(ad1816, ad1816->cur_dma) | (temp << 8));
                ad1816->playback_pos += 2;
                break;

            case 0xd0: /* Stereo, 16-bit PCM big endian */
                temp          = (int32_t) ad1816_dma_channel_read(ad1816, ad1816->cur_dma);
                ad1816->out_l = (int16_t) (ad1816_dma_channel_read(ad1816, ad1816->cur_dma) | (temp << 8));
                temp          = (int32_t) ad1816_dma_channel_read(ad1816, ad1816->cur_dma);
                ad1816->out_r = (int16_t) (ad1816_dma_channel_read(ad1816, ad1816->cur_dma) | (temp << 8));
                ad1816->playback_pos += 4;
                break;

                /* 0xe0 and 0xf0 reserved */

            default:
                break;
        }
        if (ad1816->iregs[4] & 0x8000)
            ad1816->out_l = 0;
        else
            ad1816->out_l = (int16_t) ((ad1816->out_l * ad1816_vols_6bits[((ad1816->iregs[4] >> 8) & 0x3f) >> 1]) >> 16);

        if (ad1816->iregs[4] & 0x0080)
            ad1816->out_r = 0;
        else
            ad1816->out_r = (int16_t) ((ad1816->out_r * ad1816_vols_6bits[(ad1816->iregs[4] & 0x3f) >> 1]) >> 16);

        if (ad1816->count < 0) {
            ad1816->count     = ad1816->iregs[8];
            ad1816->regs[1] |= 0x80;
            if (ad1816->iregs[1] & 0x8000) {
                ad1816_log(ad1816->log, "AD1816 Playback interrupt fired\n");
                picint(1 << ad1816->cur_irq);
            }
            else {
                ad1816_log(ad1816->log, "AD1816 Playback interrupt cleared\n");
                picintc(1 << ad1816->cur_irq);
            }
        }
        /* AD1816 count decrements every 4 bytes */
        if (!(ad1816->playback_pos & 3))
            ad1816->count--;
    } else {
        ad1816->out_l = ad1816->out_r = 0;
    }
}

static void
ad1816_get_buffer(int32_t *buffer, int len, void *priv)
{
    ad1816_t *ad1816 = (ad1816_t *) priv;

    /* Don't play audio if the DAC mute or chip powerdown bits are set */
    if ((ad1816->iregs[44] & 0x8000) || (ad1816->iregs[44] & 0x0008))
        return;

    ad1816_update_mastervol(ad1816);
    ad1816_update(ad1816);
    for (int c = 0; c < len * 2; c++) {
        double out_l = 0.0;
        double out_r = 0.0;

        out_l = (ad1816->buffer[c] * ad1816->master_l);
        out_r = (ad1816->buffer[c + 1] * ad1816->master_r);

        buffer[c] += (int32_t) out_l;
        buffer[c + 1] += (int32_t) out_r;
    }

    ad1816->pos = 0;

    /* sbprov2 part */
    sb_get_buffer_sbpro(buffer, len, ad1816->sb);
}

static void
ad1816_updatefreq(ad1816_t *ad1816)
{
    double freq;

    if (ad1816->iregs[2] > 55200)
        ad1816->iregs[2] = 55200;
    freq = ad1816->iregs[2];

    ad1816->freq        = (int) trunc(freq);
    ad1816->timer_latch = (uint64_t) ((double) TIMER_USEC * (1000000.0 / (double) ad1816->freq));

    ad1816_log(ad1816->log, "AD1816: Frequency set to %f\n", freq);
}

static void
ad1816_reg_write(uint16_t addr, uint8_t val, void *priv)
{
    ad1816_t      *ad1816           = (ad1816_t *) priv;
    uint16_t       iridx            = ad1816->index;
    uint8_t        port             = addr - ad1816->cur_ad1816_addr;
    double         timebase         = 0;

    switch (port) {
        case 0: /* Status/Indirect address */
            ad1816->regs[0] = val | 0x80;
            ad1816->index = val & 0x3f;
            break;
        case 1: /* Interrupt Status */
            ad1816->regs[1] = 0x00; /* Sticky read/clear */
            break;
        case 2: /* Indirect data low byte */
            ad1816->regs[2] = val;
            ad1816->iregs[0] = val; /* Indirect low byte temp */
            break;
        case 3: /* Indirect data high byte */
            ad1816->regs[3] = val;
            switch (iridx) {
                case 1: /* Interrupt Enable/External Control */
                    ad1816->iregs[1] = ((val << 8) | ad1816->regs[2]);
                    if (ad1816->iregs[1] & 0x0080) {
                        ad1816_log(ad1816->log, "Timer Enable\n");
                        timebase = (ad1816->iregs[44] & 0x0100) ? 100000 : 10;
                        timer_set_delay_u64(&ad1816->ad1816_irq_timer, (ad1816->iregs[13] * timebase * TIMER_USEC));
                    }
                    else {
                        ad1816_log(ad1816->log, "Timer Disable\n");
                        timer_disable(&ad1816->ad1816_irq_timer);
                    }
                    break;
                case 2: /* Voice Playback Sample Rate */
                    ad1816->iregs[2] = ((val << 8) | ad1816->regs[2]);
                    ad1816_updatefreq(ad1816);
                    break;
                case 3: /* Voice Capture Sample Rate */
                    ad1816->iregs[3] = ((val << 8) | ad1816->regs[2]);
                    break;
                case 4: /* Voice Attenuation */
                    ad1816->iregs[4] = ((val << 8) | ad1816->regs[2]);
                    break;
                case 5: /* FM Attenuation */
                    ad1816->iregs[5] = ((val << 8) | ad1816->regs[2]);
                    /* FM has distortion problems at the highest volume setting and the Win95 driver only uses the upper
                       16 values, currently needs a small hack to use lower volumes if the FM mixer is set too high */
                    if (ad1816->iregs[5] & 0x8000)
                        ad1816->fm_vol_l = 0;
                    else {
                        if (((ad1816->iregs[5] >> 8) & 0x3F) <= 0x0F)
                            ad1816->fm_vol_l = ad1816_vols_6bits[((ad1816->iregs[5] >> 8) & 0x3f) + 0x10];
                        else
                            ad1816->fm_vol_l = ad1816_vols_6bits[((ad1816->iregs[5] >> 8) & 0x3f)];
                    }
                    if (ad1816->iregs[5] & 0x0080)
                        ad1816->fm_vol_r = 0;
                    else {
                        if ((ad1816->iregs[5] & 0x3F) <= 0x0F)
                            ad1816->fm_vol_r = ad1816_vols_6bits[(ad1816->iregs[5] & 0x3f) + 0x10];
                        else
                            ad1816->fm_vol_r = ad1816_vols_6bits[(ad1816->iregs[5] & 0x3f)];
                    }
                    break;
                case 6: /* I2S(1) Attenuation */
                    ad1816->iregs[6] = ((val << 8) | ad1816->regs[2]);
                    break;
                case 7: /* I2S(0) Attenuation */
                    ad1816->iregs[7] = ((val << 8) | ad1816->regs[2]);
                    break;
                case 8: /* Playback Base Count */
                    ad1816->iregs[8] = ((val << 8) | ad1816->regs[2]);
                    ad1816->iregs[9] = ((val << 8) | ad1816->regs[2]);
                    ad1816->count = ad1816->iregs[8];
                    break;
                case 9: /* Playback Current Count */
                    ad1816->iregs[9] = ((val << 8) | ad1816->regs[2]);
                    break;
                case 10: /* Capture Base Count */
                    ad1816->iregs[10] = ((val << 8) | ad1816->regs[2]);
                    ad1816->iregs[11] = ((val << 8) | ad1816->regs[2]);
                    break;
                case 11: /* Capture Current Count */
                    ad1816->iregs[11] = ((val << 8) | ad1816->regs[2]);
                    break;
                case 12: /* Timer Base Count */
                    ad1816->iregs[12] = ((val << 8) | ad1816->regs[2]);
                    ad1816->iregs[13] = ((val << 8) | ad1816->regs[2]);
                    break;
                case 13: /* Timer Current Count */
                    ad1816->iregs[13] = ((val << 8) | ad1816->regs[2]);
                    break;
                case 14: /* Master Volume Attenuation */
                    ad1816->iregs[14] = ((val << 8) | ad1816->regs[2]);
                    break;
                case 15: /* CD Gain/Attenuation */
                    ad1816->iregs[15] = ((val << 8) | ad1816->regs[2]);
                    if (ad1816->iregs[15] & 0x8000)
                        ad1816->cd_vol_l = 0;
                    else
                        ad1816->cd_vol_l = ad1816_vols_5bits_aux_gain[(ad1816->iregs[15] >> 8) & 0x1f];
                    if (ad1816->iregs[15] & 0x0080)
                        ad1816->cd_vol_r = 0;
                    else
                        ad1816->cd_vol_r = ad1816_vols_5bits_aux_gain[ad1816->iregs[15] & 0x1f];
                    break;
                case 16: /* Synth Gain/Attenuation */
                    ad1816->iregs[16] = ((val << 8) | ad1816->regs[2]);
                    break;
                case 17: /* Vid Gain/Attenuation */
                    ad1816->iregs[17] = ((val << 8) | ad1816->regs[2]);
                    break;
                case 18: /* Line Gain/Attenuation */
                    ad1816->iregs[18] = ((val << 8) | ad1816->regs[2]);
                    break;
                case 19: /* Mic/Phone_In Gain/Attenuation */
                    ad1816->iregs[19] = ((val << 8) | ad1816->regs[2]);
                    break;
                case 20: /* ADC Source Select/ADC PGA */
                    ad1816->iregs[20] = ((val << 8) | ad1816->regs[2]);
                    break;
                case 32: /* Chip Configuration */
                    ad1816->iregs[32] = ((val << 8) | ad1816->regs[2]);
                    if (ad1816->iregs[32] & 0x8000) {
                        ad1816->codec_enable = 1;
                        sound_set_cd_audio_filter(NULL, NULL); /* Seems to be necessary for the filter below to apply */
                        sound_set_cd_audio_filter(ad1816_filter_cd_audio, ad1816);
                        ad1816->sb->opl_mixer = ad1816;
                        ad1816->sb->opl_mix   = ad1816_filter_opl;
                    }
                    else {
                        ad1816->codec_enable = 0;
                        sound_set_cd_audio_filter(NULL, NULL); /* Seems to be necessary for the filter below to apply */
                        sound_set_cd_audio_filter(sbpro_filter_cd_audio, ad1816->sb); /* Use SBPro to filter when codec is disabled */
                        ad1816->sb->opl_mixer = NULL;
                        ad1816->sb->opl_mix   = NULL;
                    }
                    break;
                case 33: /* DSP Configuration */
                    ad1816->iregs[33] = ((val << 8) | ad1816->regs[2]);
                    if ((ad1816->iregs[1] & 0x0800) && (ad1816->iregs[33] & 0x2000)) {
                        ad1816_log(ad1816->log, "Firing DSP interrupt\n");
                        ad1816->regs[1] |= 0x08;
                        picint(1 << ad1816->cur_irq);
                    }
                    break;
                case 34: /* FM Sample Rate */
                    ad1816->iregs[34] = ((val << 8) | ad1816->regs[2]);
                    break;
                case 35: /* I2S(1) Sample Rate */
                    ad1816->iregs[35] = ((val << 8) | ad1816->regs[2]);
                    break;
                case 36: /* I2S(0) Sample Rate */
                    ad1816->iregs[36] = ((val << 8) | ad1816->regs[2]);
                    break;
                case 37: /* Reserved on AD1816, Modem sample rate on AD1815 */
                    break;
                case 38: /* Programmable Clock Rate */
                    ad1816->iregs[38] = ((val << 8) | ad1816->regs[2]);
                    break;
                case 39: /* 3D Phat Stereo Control/Phone_Out Attenuation on AD1816, Modem DAC/ADC attenuation on AD1815 */
                    ad1816->iregs[39] = ((val << 8) | ad1816->regs[2]);
                    break;
                case 40: /* Reserved on AD1816, Modem mix attenuation on AD1815 */
                    break;
                case 41: /* Hardware Volume Button Modifier */
                    ad1816->iregs[41] = ((val << 8) | ad1816->regs[2]);
                    break;
                case 42: /* DSP Mailbox 0 */
                    ad1816->iregs[42] = ((val << 8) | ad1816->regs[2]);
                    break;
                case 43: /* DSP Mailbox 1 */
                    ad1816->iregs[43] = ((val << 8) | ad1816->regs[2]);
                    break;
                case 44: /* Powerdown and Timer Control */
                    ad1816->iregs[44] = ((val << 8) | ad1816->regs[2]);
                    break;
                case 45: /* Version/ID */
                    break;
                case 46: /* Reserved test register */
                    break;
                default:
                    break;
            }
            ad1816_log(ad1816->log, "AD1816 Indirect Register write: idx = %02X, val = %04X\n", ad1816->index, ad1816->iregs[iridx]);
            break;
        case 4: /* PIO Debug */
            ad1816->regs[4] = 0x00; /* Sticky read/clear */
            break;
        case 5: /* PIO Status (RO) */
            break;
        case 6: /* PIO Data */
            ad1816->regs[6] = val;
            break;
        case 7: /* Reserved */
            break;
        case 8: /* Playback Config */
            ad1816->regs[8] = val;
            if (!ad1816->enable && val & 0x01) {
                ad1816->playback_pos = 0;
                ad1816->dma_ff = 0;
                if (ad1816->timer_latch)
                    timer_set_delay_u64(&ad1816->timer_count, ad1816->timer_latch);
                else
                    timer_set_delay_u64(&ad1816->timer_count, TIMER_USEC);
            }
            ad1816->enable = (val & 0x01);
            if (!ad1816->enable) {
                timer_disable(&ad1816->timer_count);
                ad1816->out_l = ad1816->out_r = 0;
            }
            break;
        case 9: /* Capture Config */
            ad1816->regs[9] = val;
            break;
        case 10: /* Reserved on AD1816, PIO modem out/in bits 7-0 on AD1815 */
            break;
        case 11: /* Reserved on AD1816, PIO modem out/in bits 15-8 on AD1815 */
            break;
        case 12: /* Joystick Raw Data (RO) */
            break;
        case 13: /* Joystick Control */
            ad1816->regs[13] = ((val & 0x7f) | (ad1816->regs[13] & 0x80));
            break;
        case 14: /* Joystick Position Low Byte (RO) */
        case 15: /* Joystick Position Low Byte (RO) */
            break;
    }
    ad1816_log(ad1816->log, "AD1816 Write: idx = %02X, val = %02X\n", port, val);
}

static uint8_t
ad1816_reg_read(uint16_t addr, void *priv)
{
    ad1816_t *ad1816 = (ad1816_t *) priv;
    uint16_t  iridx  = ad1816->index;
    uint8_t   temp   = 0xFF;
    uint8_t   port   = addr - ad1816->cur_ad1816_addr;

    switch (port) {
        case 0:
            temp = ad1816->regs[port];
            break;
        case 1:
            temp = ad1816->regs[port];
            temp |= ((ad1816->sb->dsp.sb_irq8) ? 1 : 0);
            break;
        case 2:
            temp = (ad1816->iregs[iridx] & 0x00ff);
            break;
        case 3:
            temp = (ad1816->iregs[iridx] & 0xff00) >> 8;
            break;
        case 4 ... 15:
            temp = ad1816->regs[port];
            break;
        default:
            break;
    }

    ad1816_log(ad1816->log, "AD1816 Read: idx = %02X, val = %02X\n", port, temp);
    return temp;
}

static void
ad1816_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv)
{
    ad1816_t *ad1816 = (ad1816_t *) priv;

    ad1816_log(ad1816->log, "PnP Config changed\n");

    switch(ld) {
        case 0: /* Audio */
            if (ad1816->cur_ad1816_addr) {
                io_removehandler(ad1816->cur_ad1816_addr, 0x10, ad1816_reg_read, NULL, NULL, ad1816_reg_write, NULL, NULL, ad1816);
                ad1816->cur_ad1816_addr = 0;
            }

            if (ad1816->cur_opl_addr) {
                io_removehandler(ad1816->cur_opl_addr, 0x0004, ad1816->sb->opl.read, NULL, NULL, ad1816->sb->opl.write, NULL, NULL, ad1816->sb->opl.priv);
                ad1816->cur_opl_addr = 0;
            }

            if (ad1816->cur_sb_addr) {
                sb_dsp_setaddr(&ad1816->sb->dsp, 0);
                io_removehandler(ad1816->cur_sb_addr + 4, 0x0002, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, ad1816->sb);
                io_removehandler(ad1816->cur_sb_addr + 0, 0x0004, ad1816->sb->opl.read, NULL, NULL, ad1816->sb->opl.write, NULL, NULL, ad1816->sb->opl.priv);
                io_removehandler(ad1816->cur_sb_addr + 8, 0x0002, ad1816->sb->opl.read, NULL, NULL, ad1816->sb->opl.write, NULL, NULL, ad1816->sb->opl.priv);
                ad1816->cur_sb_addr = 0;
            }

            sb_dsp_setirq(&ad1816->sb->dsp, 0);
            sb_dsp_setdma8(&ad1816->sb->dsp, 0);

            if (config->activate) {
                if (config->io[0].base != ISAPNP_IO_DISABLED) {
                    ad1816->cur_sb_addr = config->io[0].base;
                    ad1816_log(ad1816->log, "Updating SB DSP I/O port, SB DSP addr = %04X\n", ad1816->cur_sb_addr);
                    sb_dsp_setaddr(&ad1816->sb->dsp, ad1816->cur_sb_addr);
                    io_sethandler(ad1816->cur_sb_addr + 4, 0x0002, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, ad1816->sb);
                    io_sethandler(ad1816->cur_sb_addr + 0, 0x0004, ad1816->sb->opl.read, NULL, NULL, ad1816->sb->opl.write, NULL, NULL, ad1816->sb->opl.priv);
                    io_sethandler(ad1816->cur_sb_addr + 8, 0x0002, ad1816->sb->opl.read, NULL, NULL, ad1816->sb->opl.write, NULL, NULL, ad1816->sb->opl.priv);
                }
                if (config->io[1].base != ISAPNP_IO_DISABLED) {
                    ad1816->cur_opl_addr = config->io[1].base;
                    ad1816_log(ad1816->log, "Updating OPL I/O port, OPL addr = %04X\n", ad1816->cur_opl_addr);
                    io_sethandler(ad1816->cur_opl_addr, 0x0004, ad1816->sb->opl.read, NULL, NULL, ad1816->sb->opl.write, NULL, NULL, ad1816->sb->opl.priv);

                }
                if (config->io[2].base != ISAPNP_IO_DISABLED) {
                    ad1816->cur_ad1816_addr = config->io[2].base;
                    ad1816_log(ad1816->log, "Updating AD1816 I/O port, AD1816 addr = %04X\n", ad1816->cur_ad1816_addr);
                    io_sethandler(ad1816->cur_ad1816_addr, 0x10, ad1816_reg_read, NULL, NULL, ad1816_reg_write, NULL, NULL, ad1816);
                }
                if (config->irq[0].irq != ISAPNP_IRQ_DISABLED) {
                    ad1816->cur_irq = config->irq[0].irq;
                    sb_dsp_setirq(&ad1816->sb->dsp, ad1816->cur_irq);
                    ad1816_log(ad1816->log, "Updated AD1816/SB IRQ to %02X\n", ad1816->cur_irq);
                }
                if (config->dma[0].dma != ISAPNP_DMA_DISABLED) {
                    ad1816->cur_dma = config->dma[0].dma;
                    sb_dsp_setdma8(&ad1816->sb->dsp, ad1816->cur_dma);
                    ad1816_log(ad1816->log, "Updated AD1816/SB DMA to %02X\n", ad1816->cur_dma);
                }
            }
            break;
        case 1: /* MPU401 */
            if (config->activate) {
                if (config->io[0].base != ISAPNP_IO_DISABLED) {
                    ad1816->cur_mpu_addr = config->io[0].base;
                    ad1816_log(ad1816->log, "Updating MPU401 I/O port, MPU401 addr = %04X\n", ad1816->cur_mpu_addr);
                    mpu401_change_addr(ad1816->mpu, ad1816->cur_mpu_addr);
                }
                if (config->irq[0].irq != ISAPNP_IRQ_DISABLED) {
                    ad1816->cur_mpu_irq = config->irq[0].irq;
                    mpu401_setirq(ad1816->mpu, ad1816->cur_mpu_irq);
                    ad1816_log(ad1816->log, "Updated MPU401 IRQ to %02X\n", ad1816->cur_mpu_irq);
                }
            }
            break;
        case 2: /* Gameport */
            ad1816->cur_js_addr = config->io[0].base;
            gameport_remap(ad1816->gameport, (config->activate && (config->io[0].base != ISAPNP_IO_DISABLED)) ? config->io[0].base : 0);
            break;
        default:
            break;
    }
}

void
ad1816_irq_poll(void *priv)
{
    ad1816_t *ad1816 = (ad1816_t *) priv;
    if (ad1816->iregs[1] & 0x2000) {
        ad1816_log(ad1816->log, "Firing timer IRQ\n");
        picint(1 << ad1816->cur_irq);
        ad1816_log(ad1816->log, "Setting TI bit\n");
        ad1816->regs[1] = ad1816->regs[1] | 0x20;
        ad1816_log(ad1816->log, "Reloading Timer Count\n");
        ad1816->iregs[13] = ad1816->iregs[12];
    }
}

static void *
ad1816_init(const device_t *info)
{
    ad1816_t *ad1816 = calloc(1, sizeof(ad1816_t));
    uint8_t c;
    double  attenuation;

    ad1816->cur_ad1816_addr = 0x530;
    ad1816->cur_sb_addr     = 0x220;
    ad1816->cur_opl_addr    = 0x388;
    ad1816->cur_mpu_addr    = 0x330;
    ad1816->cur_js_addr     = 0x200;
    ad1816->cur_irq         = 5;
    ad1816->cur_mpu_irq     = 9;
    ad1816->cur_dma         = 1;
    ad1816->enable          = 1;

    ad1816->regs[0]  = 0x80;
    ad1816->regs[13] = 0xf0;
    ad1816->regs[14] = 0xff;
    ad1816->regs[15] = 0xff;

    ad1816->iregs[01] = 0x0102;
    ad1816->iregs[02] = 0x1f40;
    ad1816->iregs[03] = 0x1f40;
    ad1816->iregs[04] = 0x8080;
    ad1816->iregs[05] = 0x8080;
    ad1816->iregs[06] = 0x8080;
    ad1816->iregs[07] = 0x8080;
    ad1816->iregs[15] = 0x8888;
    ad1816->iregs[16] = 0x8888;
    ad1816->iregs[17] = 0x8888;
    ad1816->iregs[18] = 0x8888;
    ad1816->iregs[19] = 0x8888;
    ad1816->iregs[32] = 0x00f0;
    ad1816->iregs[34] = 0x5622;
    ad1816->iregs[35] = 0xac44;
    ad1816->iregs[36] = 0xac44;
    ad1816->iregs[38] = 0xac44;
    ad1816->iregs[39] = 0x8000;
    ad1816->iregs[41] = 0x001b;
    ad1816->iregs[45] = 0x0000; /* Version/ID */

    ad1816->log = log_open("AD1816");

    ad1816->gameport = gameport_add(&gameport_pnp_device);
    gameport_remap(ad1816->gameport, ad1816->cur_js_addr);

    /* Set up Sound System direct registers */
    io_sethandler(ad1816->cur_ad1816_addr, 0x10, ad1816_reg_read, NULL,NULL, ad1816_reg_write, NULL, NULL, ad1816);

    ad1816_updatefreq(ad1816);

    ad1816->sb = calloc(1, sizeof(sb_t));
    ad1816->sb->opl_enabled = 1;

    sb_dsp_set_real_opl(&ad1816->sb->dsp, FM_YMF262);
    sb_dsp_init(&ad1816->sb->dsp, SBPRO_DSP_302, SB_SUBTYPE_DEFAULT, ad1816);
    sb_dsp_setaddr(&ad1816->sb->dsp, ad1816->cur_sb_addr);
    sb_dsp_setirq(&ad1816->sb->dsp, ad1816->cur_irq);
    sb_dsp_setirq(&ad1816->sb->dsp, ad1816->cur_dma);
    sb_ct1345_mixer_reset(ad1816->sb);

    fm_driver_get(FM_YMF262, &ad1816->sb->opl);
    io_sethandler(ad1816->cur_sb_addr + 0, 0x0004, ad1816->sb->opl.read, NULL, NULL, ad1816->sb->opl.write, NULL, NULL, ad1816->sb->opl.priv);
    io_sethandler(ad1816->cur_sb_addr + 8, 0x0002, ad1816->sb->opl.read, NULL, NULL, ad1816->sb->opl.write, NULL, NULL, ad1816->sb->opl.priv);
    io_sethandler(ad1816->cur_opl_addr, 0x0004, ad1816->sb->opl.read, NULL, NULL, ad1816->sb->opl.write, NULL, NULL, ad1816->sb->opl.priv);

    io_sethandler(ad1816->cur_sb_addr + 4, 0x0002, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, ad1816->sb);

    sound_add_handler(ad1816_get_buffer, ad1816);
    music_add_handler(sb_get_music_buffer_sbpro, ad1816->sb);

    sound_set_cd_audio_filter(NULL, NULL); /* Seems to be necessary for the filter below to apply */
    sound_set_cd_audio_filter(sbpro_filter_cd_audio, ad1816->sb); /* Default SBPro mode CD audio filter */

    ad1816->mpu = (mpu_t *) calloc(1, sizeof(mpu_t));
    mpu401_init(ad1816->mpu, ad1816->cur_mpu_addr, ad1816->cur_mpu_irq, M_UART, device_get_config_int("receive_input401"));

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &ad1816->sb->dsp);

    const char *pnp_rom_file = NULL;
    uint16_t   pnp_rom_len   = 512;
    pnp_rom_file = PNP_ROM_AD1816;

    uint8_t *pnp_rom = NULL;
    if (pnp_rom_file) {
        FILE *fp = rom_fopen(pnp_rom_file, "rb");
        if (fp) {
            if (fread(ad1816->pnp_rom, 1, pnp_rom_len, fp) == pnp_rom_len)
                pnp_rom = ad1816->pnp_rom;
            fclose(fp);
        }
    }
    ad1816->pnp_card = isapnp_add_card(pnp_rom, sizeof(ad1816->pnp_rom), ad1816_pnp_config_changed,
                                       NULL, NULL, NULL, ad1816);

    timer_add(&ad1816->timer_count, ad1816_poll, ad1816, 0);

    timer_add(&ad1816->ad1816_irq_timer, ad1816_irq_poll, ad1816, 0);

    /* Calculate attenuation values for both the 6-bit and 5-bit volume controls */
    for (c = 0; c < 64; c++) {
        attenuation = 0.0;
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

        attenuation = pow(10, attenuation / 10);

        ad1816_vols_6bits[c] = (int) (attenuation * 65536);
    }

    for (c = 0; c < 32; c++) {
        attenuation = 0.0;
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

        ad1816_vols_5bits[c] = (attenuation * 65536);
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

        ad1816_vols_5bits_aux_gain[c] = (attenuation * 65536);
    }

    return ad1816;
}

static void
ad1816_close(void *priv)
{
    ad1816_t *ad1816 = (ad1816_t *) priv;

    if (ad1816->log != NULL) {
        log_close(ad1816->log);
        ad1816->log = NULL;
    }

    sb_close(ad1816->sb);
    free(ad1816->mpu);
    free(priv);
}

static int
ad1816_available(void)
{
    return rom_present(PNP_ROM_AD1816);
}

static void
ad1816_speed_changed(void *priv)
{
    ad1816_t *ad1816 = (ad1816_t *) priv;

    ad1816->timer_latch = (uint64_t) ((double) TIMER_USEC * (1000000.0 / (double) ad1816->freq));

    sb_speed_changed(ad1816->sb);
}

static const device_config_t ad1816_config[] = {
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

const device_t ad1816_device = {
    .name          = "Analog Devices AD1816",
    .internal_name = "ad1816",
    .flags         = DEVICE_ISA16,
    .local         = 0,
    .init          = ad1816_init,
    .close         = ad1816_close,
    .reset         = NULL,
    .available     = ad1816_available,
    .speed_changed = ad1816_speed_changed,
    .force_redraw  = NULL,
    .config        = ad1816_config
};

