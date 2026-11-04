/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Oak Technology OTI-601 "Mozart" audio controller emulation.
 *
 *          The OTI-601 makes no sound of its own: it decodes the bus for an
 *          external codec and FM synthesizer, and contains a Sound Blaster
 *          compatible DSP whose samples are played through the codec's DAC.
 *
 *          No datasheet is known to exist. The register map below comes from
 *          disassembling Oak's own tools and drivers (MZTINIT.SYS, MZTMODE,
 *          MZTTEST, MZTVOL, INSTALL, VMZTSS.VXD, VMZTMID.VXD) and from the
 *          mad16 driver in Linux. All registers sit behind password 0xE2 at
 *          0xF8F, which stays valid for a single I/O cycle.
 *
 *          MC1 (F8D) 7    Windows Sound System mode, 0 = Sound Blaster mode
 *                    6    Power down
 *                    5:4  WSS base (530, E80, F40, 604); bit 5 also moves
 *                         the SB base from 220 to 240
 *                    3:1  CD-ROM interface type
 *                    0    Game port disable
 *          MC2 (F8E) 7:6  CD-ROM base (340, 330, 360, 320)
 *                    5    OPL4 wave ports enable (OPL4 boards are not emulated,
 *                         this is only latched)
 *                    4:2  CD-ROM IRQ
 *                    1:0  CD-ROM DMA
 *          MC3 (F8F) 7:6  SB IRQ (7, 11, 5)
 *                    5:4  SB DMA (1, 2, 3)
 *                    3    Scratch (used by MZTVOL to disable itself)
 *                    2    Keep the SB DSP mapped in WSS mode, for SB MIDI
 *                    1:0  Write: selects what F90 reads back
 *                         Read: chip ID, 3 = OTI-601E, 0 = OTI-601B or OTI-605
 *          MC4 (F90) Read back through F90 with MC3 selector 0
 *          MC5 (F91) 4    Codec reachable while in SB mode
 *                         Read back through F90 with MC3 selector 1
 *          MC6 (F92), MC7 (F93) Latches, meaning unknown
 *          MC8 (F94) OTI-605 only: 5:3 MPU IRQ (5, 7, 9, 10, 11),
 *                    2:1 MPU base (300, 310, 320, 330), 0 MPU enable
 *          MC9 (F95) OTI-605 only: reads as 0xFF on the OTI-601 chips, which
 *                    is how the drivers tell the OTI-601B and OTI-605 apart
 *          MC10 (F96) OTI-605 only, latch
 *
 *          Output routing, as programmed by the Oak tools: FM is on the
 *          codec's AUX2 input, CD audio on AUX1, and the DSP goes through the
 *          DAC, so all of them are subject to the codec's gains in both modes.
 *          The codec resets muted, so the card stays silent until MZTINIT.SYS
 *          has programmed it. The same driver sets the card's resources, since
 *          the card has no jumpers.
 *
 *          The SB mixer at base+4/5 is storage only: it has no direct effect
 *          on the audio path. MZTVOL.EXE is a TSR that polls it from the timer
 *          interrupt and copies the levels to the codec.
 *
 *          The CD-ROM interfaces are not emulated, their settings are only
 *          latched. A standalone CD-ROM interface can be used instead.
 *
 * Authors: Franck Talbart, <franck.talbart@gmail.com>
 *
 *          Copyright 2026 Franck Talbart.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/midi.h>
#include <86box/timer.h>
#include <86box/sound.h>
#include <86box/gameport.h>
#include <86box/snd_ad1848.h>
#include <86box/snd_opl.h>
#include <86box/snd_sb.h>
#include <86box/plat_unused.h>

#define MOZART_PASSWORD 0xe2

#define MC1 0
#define MC2 1
#define MC3 2
#define MC4 3
#define MC5 4
#define MC8 7

#define MC1_WSS_MODE     0x80
#define MC1_POWER_DOWN   0x40
#define MC1_SB_BASE_240  0x20
#define MC1_GAME_DISABLE 0x01
#define MC3_SB_MIDI      0x04
#define MC5_CODEC_ACCESS 0x10
#define MC8_MPU_ENABLE   0x01

#define MOZART_RECORD_CLAMP(x) (((x) < -32768) ? -32768 : (((x) > 32767) ? 32767 : (x)))
#define MOZART_RECORD_AA_NYQ   0.9

enum {
    MOZART_CHIP_601E = 0,
    MOZART_CHIP_601B = 1,
    MOZART_CHIP_605  = 2
};

static const uint16_t mozart_wss_base[4] = { 0x530, 0xe80, 0xf40, 0x604 };
static const int      mozart_sb_irq[4]   = { 7, 11, 5, 5 };
static const int      mozart_sb_dma[4]   = { 1, 2, 3, 1 };
static const int      mozart_wss_irq[8]  = { -1, 7, 9, 10, 11, -1, -1, -1 };
static const int      mozart_wss_dma[4]  = { 0, 0, 1, 3 };
static const int      mozart_wss_dma2[4] = { 1, 1, 0, 0 };
static const uint16_t mozart_mpu_base[4] = { 0x300, 0x310, 0x320, 0x330 };
static const int      mozart_mpu_irq[8]  = { 0, 5, 7, 9, 10, 11, 0, 0 }; /* 0 = none */

static double mozart_dac_vols[64];
static double mozart_input_gain_vols[16];

typedef struct mozart_t {
    int     chip;
    uint8_t regs[10];
    uint8_t unlocked;
    uint8_t wss_config;
    uint8_t mixer_index;
    uint8_t mixer_regs[256];

    /* Currently mapped resources, 0 = unmapped. */
    uint16_t cur_sb_base;
    uint16_t cur_config_base;
    uint16_t cur_codec_base;

    ad1848_t ad1848;
    sb_t    *sb;
    mpu_t   *mpu;
    void    *gameport;
} mozart_t;

static void
mozart_record_aa_design(ad1848_t *ad1848, int out_rate, int in_rate)
{
    const double fc    = (MOZART_RECORD_AA_NYQ * 0.5) * ((double) out_rate);
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
mozart_record_aa_step(ad1848_t *ad1848, int ch, double x)
{
    const double y = (ad1848->record_aa_b0_mic * x) + ad1848->record_aa_z1_mic[ch];

    ad1848->record_aa_z1_mic[ch] = (ad1848->record_aa_b1_mic * x) - (ad1848->record_aa_a1_mic * y)
                                   + ad1848->record_aa_z2_mic[ch];
    ad1848->record_aa_z2_mic[ch] = (ad1848->record_aa_b2_mic * x) - (ad1848->record_aa_a2_mic * y);

    return y;
}

/* Both the codec and the DSP sample through the codec's ADC. Only the line and
   microphone sources are available: AUX1 and post-mix record silence. */
static void
mozart_put_buffer(int16_t *buffer, int len, void *priv)
{
    mozart_t *dev      = (mozart_t *) priv;
    ad1848_t *ad1848   = &dev->ad1848;
    sb_dsp_t *dsp      = &dev->sb->dsp;
    const int cap_rate = al_capture_get_rate();
    const int denom    = (cap_rate > 0) ? cap_rate : SOUND_FREQ;
    const int rate     = (ad1848->regs[9] & 0x02) ? ad1848->freq : dsp->sb_freq;

    /* The frequency is 0 until the guest programs a rate. */
    if (rate <= 0)
        return;

    if ((denom != ad1848->record_denom_mic) || (rate != ad1848->record_rate_mic)) {
        ad1848->record_denom_mic      = denom;
        ad1848->record_rate_mic       = rate;
        ad1848->record_phase_mic      = 0;
        ad1848->record_prev_l_mic     = 0;
        ad1848->record_prev_r_mic     = 0;
        ad1848->record_prev_valid_mic = 0;
        ad1848->record_aa_z1_mic[0]   = 0.0;
        ad1848->record_aa_z1_mic[1]   = 0.0;
        ad1848->record_aa_z2_mic[0]   = 0.0;
        ad1848->record_aa_z2_mic[1]   = 0.0;
        ad1848->record_aa_active_mic  = 0;

        /* Only needed when decimating. */
        if (rate < denom) {
            mozart_record_aa_design(ad1848, rate, denom);
            ad1848->record_aa_active_mic = 1;
        }
    }

    const int    interp = (rate != denom);
    const int    filt   = ad1848->record_aa_active_mic;
    const double gain_l = mozart_input_gain_vols[ad1848->regs[0] & 0x0f];
    const double gain_r = mozart_input_gain_vols[ad1848->regs[1] & 0x0f];
    const int    mic_l  = (ad1848->regs[0] & 0xc0) == 0x80;
    const int    line_l = (ad1848->regs[0] & 0xc0) == 0x00;
    const int    mic_r  = (ad1848->regs[1] & 0xc0) == 0x80;
    const int    line_r = (ad1848->regs[1] & 0xc0) == 0x00;

    for (int c = 0; c < len * 2; c += 2) {
        const int32_t cap_l = (int32_t) buffer[c];
        const int32_t cap_r = (int32_t) buffer[c + 1];
        const int32_t mic   = (cap_l + cap_r) / 2;
        int32_t       mix_l = (mic * mic_l) + (cap_l * line_l);
        int32_t       mix_r = (mic * mic_r) + (cap_r * line_r);

        if (filt) {
            mix_l = (int32_t) lrint(mozart_record_aa_step(ad1848, 0, (double) mix_l));
            mix_r = (int32_t) lrint(mozart_record_aa_step(ad1848, 1, (double) mix_r));
        }

        const int32_t in_l = MOZART_RECORD_CLAMP(mix_l * gain_l);
        const int32_t in_r = MOZART_RECORD_CLAMP(mix_r * gain_r);

        if (!ad1848->record_prev_valid_mic) {
            ad1848->record_prev_l_mic     = in_l;
            ad1848->record_prev_r_mic     = in_r;
            ad1848->record_prev_valid_mic = 1;
        }

        ad1848->record_phase_mic += rate;
        while (ad1848->record_phase_mic >= denom) {
            int32_t out_l = in_l;
            int32_t out_r = in_r;

            ad1848->record_phase_mic -= denom;

            if (interp) {
                const int32_t num = rate - ad1848->record_phase_mic;

                out_l = ad1848->record_prev_l_mic
                        + (int32_t) ((((int64_t) (in_l - ad1848->record_prev_l_mic)) * num) / rate);
                out_r = ad1848->record_prev_r_mic
                        + (int32_t) ((((int64_t) (in_r - ad1848->record_prev_r_mic)) * num) / rate);
            }

            ad1848->record_buffer[ad1848->record_pos_write_mic]                = (int16_t) out_l;
            ad1848->record_buffer[(ad1848->record_pos_write_mic + 1) & 0xffff] = (int16_t) out_r;
            ad1848->record_pos_write_mic = (ad1848->record_pos_write_mic + 2) & 0xffff;

            dsp->record_buffer[dsp->record_pos_write_mic]                = (int16_t) out_l;
            dsp->record_buffer[(dsp->record_pos_write_mic + 1) & 0xffff] = (int16_t) out_r;
            dsp->record_pos_write_mic = (dsp->record_pos_write_mic + 2) & 0xffff;
        }

        ad1848->record_prev_l_mic = in_l;
        ad1848->record_prev_r_mic = in_r;
    }
}

static void
mozart_get_buffer(int32_t *buffer, uint16_t len, void *priv)
{
    mozart_t      *dev    = (mozart_t *) priv;
    ad1848_t      *ad1848 = &dev->ad1848;
    sb_dsp_t      *dsp    = &dev->sb->dsp;
    const int      muted  = dev->regs[MC1] & MC1_POWER_DOWN;
    /* The codec mutes its DAC while in Mode Change Enable. */
    const double   dac_l  = (ad1848->mce || (ad1848->regs[6] & 0x80)) ? 0.0 : mozart_dac_vols[ad1848->regs[6] & 0x3f];
    const double   dac_r  = (ad1848->mce || (ad1848->regs[7] & 0x80)) ? 0.0 : mozart_dac_vols[ad1848->regs[7] & 0x3f];

    ad1848_update(ad1848);
    sb_dsp_update(dsp);

    if (!muted) {
        for (uint16_t c = 0; c < len * 2; c += 2) {
            /* The DSP is mono. */
            const double dsp_out = dsp->buffer[c] / 2.0;

            buffer[c] += (ad1848->buffer[c] / 2) + (int32_t) (dsp_out * dac_l);
            buffer[c + 1] += (ad1848->buffer[c + 1] / 2) + (int32_t) (dsp_out * dac_r);
        }
    }

    ad1848->pos = 0;
    dsp->pos    = 0;
}

static void
mozart_get_music_buffer(int32_t *buffer, uint16_t len, void *priv)
{
    mozart_t      *dev     = (mozart_t *) priv;
    sb_t          *sb      = dev->sb;
    const int32_t *opl_buf = sb->opl.update(sb->opl.priv);

    if (!(dev->regs[MC1] & MC1_POWER_DOWN)) {
        for (uint16_t c = 0; c < len * 2; c += 2) {
            double out_l = ((double) opl_buf[c]) * 0.7171630859375;
            double out_r = ((double) opl_buf[c + 1]) * 0.7171630859375;

            ad1848_filter_channel(&dev->ad1848, AD1848_AUX2, &out_l, &out_r);

            buffer[c] += (int32_t) out_l;
            buffer[c + 1] += (int32_t) out_r;
        }
    }

    sb->opl.reset_buffer(sb->opl.priv);
}

static void
mozart_filter_cd_audio(int channel, double *buffer, void *priv)
{
    mozart_t *dev = (mozart_t *) priv;

    if (dev->regs[MC1] & MC1_POWER_DOWN)
        *buffer = 0.0;
    else
        ad1848_filter_cd_audio(channel, buffer, &dev->ad1848);
}

static uint8_t
mozart_wss_read(UNUSED(uint16_t addr), void *priv)
{
    const mozart_t *dev = (mozart_t *) priv;

    return 0x04 | (dev->wss_config & 0x40);
}

static void
mozart_wss_write(UNUSED(uint16_t addr), uint8_t val, void *priv)
{
    mozart_t *dev = (mozart_t *) priv;
    const int irq = mozart_wss_irq[(val >> 3) & 7];

    dev->wss_config = val;

    ad1848_setdma(&dev->ad1848, mozart_wss_dma[val & 3]);
    /* The codec emulation cannot disable its IRQ, so the previous one stays. */
    if (irq != -1)
        ad1848_setirq(&dev->ad1848, irq);
    ad1848_setdma2(&dev->ad1848, (val & 0x04) ? mozart_wss_dma2[val & 3] : 4);
}

static uint8_t
mozart_mixer_read(uint16_t addr, void *priv)
{
    const mozart_t *dev = (mozart_t *) priv;

    return (addr & 1) ? dev->mixer_regs[dev->mixer_index] : dev->mixer_index;
}

static void
mozart_mixer_write(uint16_t addr, uint8_t val, void *priv)
{
    mozart_t *dev = (mozart_t *) priv;

    if (!(addr & 1))
        dev->mixer_index = val;
    else if (dev->mixer_index == 0x00) {
        memset(dev->mixer_regs, 0x00, sizeof(dev->mixer_regs));
        dev->mixer_regs[0x04] = dev->mixer_regs[0x22] = 0xee;
        dev->mixer_regs[0x26] = dev->mixer_regs[0x28] = 0xee;
    } else
        dev->mixer_regs[dev->mixer_index] = val;
}

/* Applies the port decoding selected by MC1, MC3 and MC5. */
static void
mozart_remap(mozart_t *dev)
{
    sb_t          *sb       = dev->sb;
    const uint8_t  mc1      = dev->regs[MC1];
    const int      wss_mode = !!(mc1 & MC1_WSS_MODE);
    const uint16_t wss_base = mozart_wss_base[(mc1 >> 4) & 3];
    const uint16_t sb_base  = (!wss_mode || (dev->regs[MC3] & MC3_SB_MIDI)) ?
                              ((mc1 & MC1_SB_BASE_240) ? 0x240 : 0x220) : 0;
    const uint16_t config   = wss_mode ? wss_base : 0;
    const uint16_t codec    = (wss_mode || (dev->regs[MC5] & MC5_CODEC_ACCESS)) ? (wss_base + 4) : 0;

    if (sb_base != dev->cur_sb_base) {
        if (dev->cur_sb_base) {
            io_removehandler(dev->cur_sb_base + 0, 4, sb->opl.read, NULL, NULL, sb->opl.write, NULL, NULL, sb->opl.priv);
            io_removehandler(dev->cur_sb_base + 8, 2, sb->opl.read, NULL, NULL, sb->opl.write, NULL, NULL, sb->opl.priv);
            io_removehandler(dev->cur_sb_base + 4, 2, mozart_mixer_read, NULL, NULL, mozart_mixer_write, NULL, NULL, dev);
        }
        /* Handles the DSP ports, and unmaps them when passed 0. */
        sb_dsp_setaddr(&sb->dsp, sb_base);
        if (sb_base) {
            io_sethandler(sb_base + 0, 4, sb->opl.read, NULL, NULL, sb->opl.write, NULL, NULL, sb->opl.priv);
            io_sethandler(sb_base + 8, 2, sb->opl.read, NULL, NULL, sb->opl.write, NULL, NULL, sb->opl.priv);
            io_sethandler(sb_base + 4, 2, mozart_mixer_read, NULL, NULL, mozart_mixer_write, NULL, NULL, dev);
        }
        dev->cur_sb_base = sb_base;
    }

    if (config != dev->cur_config_base) {
        if (dev->cur_config_base)
            io_removehandler(dev->cur_config_base, 4, mozart_wss_read, NULL, NULL, mozart_wss_write, NULL, NULL, dev);
        if (config)
            io_sethandler(config, 4, mozart_wss_read, NULL, NULL, mozart_wss_write, NULL, NULL, dev);
        dev->cur_config_base = config;
    }

    if (codec != dev->cur_codec_base) {
        if (dev->cur_codec_base)
            io_removehandler(dev->cur_codec_base, 4, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &dev->ad1848);
        if (codec)
            io_sethandler(codec, 4, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &dev->ad1848);
        dev->cur_codec_base = codec;
    }

    gameport_remap(dev->gameport, (mc1 & MC1_GAME_DISABLE) ? 0x000 : 0x200);
}

static void
mozart_update_mpu(mozart_t *dev)
{
    const uint8_t mc8 = dev->regs[MC8];

    if (dev->mpu == NULL)
        return;

    mpu401_setirq(dev->mpu, mozart_mpu_irq[(mc8 >> 3) & 7]);
    mpu401_change_addr(dev->mpu, (mc8 & MC8_MPU_ENABLE) ? mozart_mpu_base[(mc8 >> 1) & 3] : 0);
}

static void
mozart_reg_write(uint16_t addr, uint8_t val, void *priv)
{
    mozart_t      *dev = (mozart_t *) priv;
    const uint16_t idx = addr - 0xf8d;

    /* The password only opens the registers for the next I/O cycle. */
    if (!dev->unlocked) {
        if ((addr == 0xf8f) && (val == MOZART_PASSWORD))
            dev->unlocked = 1;
        return;
    }
    dev->unlocked = 0;

    if ((idx >= MC8) && (dev->chip != MOZART_CHIP_605))
        return;

    dev->regs[idx] = val;

    switch (idx) {
        case MC3:
            sb_dsp_setirq(&dev->sb->dsp, mozart_sb_irq[val >> 6]);
            sb_dsp_setdma8(&dev->sb->dsp, mozart_sb_dma[(val >> 4) & 3]);
            mozart_remap(dev);
            break;

        case MC1:
        case MC5:
            mozart_remap(dev);
            break;

        case MC8:
            mozart_update_mpu(dev);
            break;

        default:
            break;
    }
}

static uint8_t
mozart_reg_read(uint16_t addr, void *priv)
{
    mozart_t      *dev = (mozart_t *) priv;
    const uint16_t idx = addr - 0xf8d;

    if (!dev->unlocked)
        return 0xff;
    dev->unlocked = 0;

    if ((idx >= MC8) && (dev->chip != MOZART_CHIP_605))
        return 0xff;

    switch (idx) {
        case MC3:
            return (dev->regs[MC3] & 0xfc) | ((dev->chip == MOZART_CHIP_601E) ? 0x03 : 0x00);

        case MC4:
            return dev->regs[((dev->regs[MC3] & 0x03) == 1) ? MC5 : MC4];

        default:
            return dev->regs[idx];
    }
}

static void *
mozart_init(UNUSED(const device_t *info))
{
    mozart_t  *dev        = calloc(1, sizeof(mozart_t));
    sb_t      *sb         = calloc(1, sizeof(sb_t));
    const int  codec_type = device_get_config_int("codec");

    dev->sb   = sb;
    dev->chip = device_get_config_int("chip");

    /* MZTINIT.SYS sets all of these. Power-on values are not known, these
       give the settings that the installer proposes by default. */
    dev->regs[MC2]   = 0x03;
    dev->regs[MC3]   = 0x80;

    ad1848_init(&dev->ad1848, codec_type);
    ad1848_setirq(&dev->ad1848, 7);
    ad1848_setdma(&dev->ad1848, 1);
    ad1848_setdma2(&dev->ad1848, 4);
    ad1848_set_cd_audio_channel(&dev->ad1848, AD1848_AUX1);
    /* The CD gains are only recalculated on a codec write, but AUX1 resets muted. */
    dev->ad1848.cd_vol_l = dev->ad1848.cd_vol_r = 0;

    fm_driver_get_cs(FM_YMF262, &sb->opl);
    io_sethandler(0x0388, 4, sb->opl.read, NULL, NULL, sb->opl.write, NULL, NULL, sb->opl.priv);

    /* The driver disks never query the DSP version, and the card is mono. After
       a DSP reset, MZTINIT.SYS 2.2 waits forever for base+C to read 0x2A, which
       is what the DSP emulation returns from version 2.01 onwards. */
    sb_dsp_init(&sb->dsp, SB_DSP_201, SB_SUBTYPE_DEFAULT, sb);
    sb_dsp_setirq(&sb->dsp, mozart_sb_irq[dev->regs[MC3] >> 6]);
    sb_dsp_setdma8(&sb->dsp, mozart_sb_dma[(dev->regs[MC3] >> 4) & 3]);

    dev->gameport = gameport_add(&gameport_pnp_device);

    if (dev->chip == MOZART_CHIP_605) {
        dev->mpu = (mpu_t *) calloc(1, sizeof(mpu_t));
        mpu401_init(dev->mpu, 0, mozart_mpu_irq[0], M_UART, device_get_config_int("receive_input401"));
    }

    mozart_remap(dev);

    io_sethandler(0xf8d, 10, mozart_reg_read, NULL, NULL, mozart_reg_write, NULL, NULL, dev);

    sound_add_handler(mozart_get_buffer, dev);
    music_add_handler(mozart_get_music_buffer, dev);
    sound_in_add_handler(mozart_put_buffer, dev);
    sound_in_start_input();

    sound_set_cd_audio_filter(NULL, NULL); /* A filter is only set if none is in place. */
    sound_set_cd_audio_filter(mozart_filter_cd_audio, dev);

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, sb_dsp_input_remain, &sb->dsp);

    /* -1.5 dB per step. */
    for (uint8_t c = 0; c < 64; c++)
        mozart_dac_vols[c] = pow(10.0, (c * -1.5) / 20.0);

    /* +1.5 dB per step. */
    for (uint8_t c = 0; c < 16; c++)
        mozart_input_gain_vols[c] = pow(10.0, (c * 1.5) / 20.0);

    return dev;
}

static void
mozart_close(void *priv)
{
    mozart_t *dev = (mozart_t *) priv;

    sb_close(dev->sb);
    free(dev->mpu);
    free(dev);
}

static void
mozart_speed_changed(void *priv)
{
    mozart_t *dev = (mozart_t *) priv;

    ad1848_speed_changed(&dev->ad1848);
    sb_speed_changed(dev->sb);
}

static const device_config_t mozart_config[] = {
  // clang-format off
    {
        .name           = "chip",
        .description    = "Chip revision",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = MOZART_CHIP_601E,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "OTI-601E",            .value = MOZART_CHIP_601E },
            { .description = "OTI-601B",            .value = MOZART_CHIP_601B },
            { .description = "OTI-605 (MPU-401)",   .value = MOZART_CHIP_605  },
            { .description = ""                                               }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "codec",
        .description    = "Codec",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = AD1848_TYPE_DEFAULT,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Analog Devices AD1848", .value = AD1848_TYPE_DEFAULT },
            { .description = "Crystal CS4231",        .value = AD1848_TYPE_CS4231  },
            { .description = ""                                                    }
        },
        .bios           = { { 0 } }
    },
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

const device_t mozart_device = {
    .name          = "Oak Mozart (OTI-601)",
    .internal_name = "mozart",
    .flags         = DEVICE_ISA16 | DEVICE_AUDIO_IN,
    .local         = 0,
    .init          = mozart_init,
    .close         = mozart_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = mozart_speed_changed,
    .force_redraw  = NULL,
    .config        = mozart_config
};
