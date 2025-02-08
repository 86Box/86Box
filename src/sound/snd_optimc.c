/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          OPTi MediaCHIPS 82C929A (also known as OPTi MAD16 Pro) audio controller emulation.
 *
 *
 *
 * Authors: Cacodemon345
 *          Eluan Costa Miranda <eluancm@gmail.com>
 *
 *          Copyright 2022 Cacodemon345.
 *          Copyright 2020 Eluan Costa Miranda.
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

static int optimc_wss_dma[4] = { 0, 0, 1, 3 };
static int optimc_wss_irq[8] = { 5, 7, 9, 10, 11, 12, 14, 15 };

enum optimc_local_flags {
    OPTIMC_CS4231 = 0x100,
    OPTIMC_OPL4   = 0x200,
};

typedef struct optimc_t {
    uint8_t type;
    uint8_t fm_type;

    uint8_t wss_config;
    uint8_t reg_enabled;

    uint16_t cur_addr;
    uint16_t cur_wss_addr;
    uint16_t cur_mpu401_addr;

    int   cur_irq;
    int   cur_dma;
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
    uint8_t regs[6];
} optimc_t, opti_82c929a_t;

static void
optimc_filter_opl(void *priv, double *out_l, double *out_r)
{
    optimc_t *optimc = (optimc_t *) priv;

    if (optimc->cur_wss_enabled) {
        *out_l /= optimc->sb->mixer_sbpro.fm_l;
        *out_r /= optimc->sb->mixer_sbpro.fm_r;
        ad1848_filter_aux2((void *) &optimc->ad1848, out_l, out_r);
    }
}

static uint8_t
optimc_wss_read(UNUSED(uint16_t addr), void *priv)
{
    const optimc_t *optimc = (optimc_t *) priv;

    if (!(optimc->regs[4] & 0x10) && optimc->cur_mode == 0)
        return 0xFF;

    return 4 | (optimc->wss_config & 0x40);
}

static void
optimc_wss_write(UNUSED(uint16_t addr), uint8_t val, void *priv)
{
    optimc_t *optimc = (optimc_t *) priv;

    if (!(optimc->regs[4] & 0x10) && optimc->cur_mode == 0)
        return;
    optimc->wss_config = val;
    ad1848_setdma(&optimc->ad1848, optimc_wss_dma[val & 3]);
    ad1848_setirq(&optimc->ad1848, optimc_wss_irq[(val >> 3) & 7]);
}

static void
optimc_get_buffer(int32_t *buffer, int len, void *priv)
{
    optimc_t *optimc = (optimc_t *) priv;

    if (optimc->regs[3] & 0x4)
        return;

    /* wss part */
    ad1848_update(&optimc->ad1848);
    for (int c = 0; c < len * 2; c++)
        buffer[c] += (optimc->ad1848.buffer[c] / 2);

    optimc->ad1848.pos = 0;

    /* sbprov2 part */
    sb_get_buffer_sbpro(buffer, len, optimc->sb);
}

static void
optimc_remove_opl(optimc_t *optimc)
{
    io_removehandler(optimc->cur_addr + 0, 0x0004, optimc->sb->opl.read, NULL, NULL, optimc->sb->opl.write, NULL, NULL, optimc->sb->opl.priv);
    io_removehandler(optimc->cur_addr + 8, 0x0002, optimc->sb->opl.read, NULL, NULL, optimc->sb->opl.write, NULL, NULL, optimc->sb->opl.priv);
    io_removehandler(0x0388, 0x0004, optimc->sb->opl.read, NULL, NULL, optimc->sb->opl.write, NULL, NULL, optimc->sb->opl.priv);
}

static void
optimc_add_opl(optimc_t *optimc)
{
    /* DSP I/O handler is activated in sb_dsp_setaddr */
    io_sethandler(optimc->cur_addr + 0, 0x0004, optimc->sb->opl.read, NULL, NULL, optimc->sb->opl.write, NULL, NULL, optimc->sb->opl.priv);
    io_sethandler(optimc->cur_addr + 8, 0x0002, optimc->sb->opl.read, NULL, NULL, optimc->sb->opl.write, NULL, NULL, optimc->sb->opl.priv);
    io_sethandler(0x0388, 0x0004, optimc->sb->opl.read, NULL, NULL, optimc->sb->opl.write, NULL, NULL, optimc->sb->opl.priv);
}

static void
optimc_reg_write(uint16_t addr, uint8_t val, void *priv)
{
    optimc_t      *optimc           = (optimc_t *) priv;
    uint16_t       idx              = addr - 0xF8D;
    uint8_t        old              = optimc->regs[idx];
    static uint8_t reg_enable_phase = 0;

    if (optimc->reg_enabled) {
        switch (idx) {
            case 0: /* MC1 */
                optimc->regs[0] = val;
                if (val != old) {
                    optimc->cur_mode = optimc->cur_wss_enabled = !!(val & 0x80);

                    sound_set_cd_audio_filter(NULL, NULL);
                    if (optimc->cur_wss_enabled) /* WSS */
                        sound_set_cd_audio_filter(ad1848_filter_cd_audio, &optimc->ad1848);
                    else /* SBPro */
                        sound_set_cd_audio_filter(sbpro_filter_cd_audio, optimc->sb);

                    io_removehandler(optimc->cur_wss_addr, 0x0004, optimc_wss_read, NULL, NULL, optimc_wss_write, NULL, NULL, optimc);
                    io_removehandler(optimc->cur_wss_addr + 0x0004, 0x0004, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &optimc->ad1848);
                    switch ((val >> 4) & 0x3) {
                        case 0: /* WSBase = 0x530 */
                            optimc->cur_wss_addr = 0x530;
                            break;
                        case 1: /* WSBase = 0xE80 */
                            optimc->cur_wss_addr = 0xE80;
                            break;
                        case 2: /* WSBase = 0xF40 */
                            optimc->cur_wss_addr = 0xF40;
                            break;
                        case 3: /* WSBase = 0x604 */
                            optimc->cur_wss_addr = 0x604;
                            break;

                        default:
                            break;
                    }
                    io_sethandler(optimc->cur_wss_addr, 0x0004, optimc_wss_read, NULL, NULL, optimc_wss_write, NULL, NULL, optimc);
                    io_sethandler(optimc->cur_wss_addr + 0x0004, 0x0004, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &optimc->ad1848);

                    gameport_remap(optimc->gameport, (optimc->regs[0] & 0x1) ? 0x00 : 0x200);
                }
                break;
            case 1: /* MC2 */
                optimc->regs[1] = val;
                break;
            case 2: /* MC3 */
                if (val == optimc->type)
                    break;
                optimc->regs[2] = val;
                if (old != val) {
                    io_removehandler(optimc->cur_addr + 4, 0x0002, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, optimc->sb);
                    optimc_remove_opl(optimc);
                    optimc->cur_addr = (val & 0x4) ? 0x240 : 0x220;
                    switch ((val >> 4) & 0x3) {
                        case 0:
                            optimc->cur_dma = 1;
                            break;
                        case 1:
                            optimc->cur_dma = 0;
                            break;
                        case 2:
                        default:
                            optimc->cur_dma = 3;
                            break;
                    }
                    switch ((val >> 6) & 0x3) {
                        case 0:
                            optimc->cur_irq = 7;
                            break;
                        case 1:
                            optimc->cur_irq = 10;
                            break;
                        case 2:
                        default:
                            optimc->cur_irq = 5;
                            break;
                    }
                    sb_dsp_setaddr(&optimc->sb->dsp, optimc->cur_addr);
                    sb_dsp_setirq(&optimc->sb->dsp, optimc->cur_irq);
                    sb_dsp_setdma8(&optimc->sb->dsp, optimc->cur_dma);
                    optimc_add_opl(optimc);
                    io_sethandler(optimc->cur_addr + 4, 0x0002, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, optimc->sb);
                }
                break;
            case 3: /* MC4 */
                optimc->regs[3] = val;
                break;
            case 4: /* MC5 */
                optimc->regs[4] = val;
                break;
            case 5: /* MC6 */
                optimc->regs[5] = val;
                if (old != val) {
                    switch ((val >> 3) & 0x3) {
                        case 0:
                            optimc->cur_mpu401_irq = 9;
                            break;
                        case 1:
                            optimc->cur_mpu401_irq = 10;
                            break;
                        case 2:
                            optimc->cur_mpu401_irq = 5;
                            break;
                        case 3:
                            optimc->cur_mpu401_irq = 7;
                            break;

                        default:
                            break;
                    }
                    switch ((val >> 5) & 0x3) {
                        case 0:
                            optimc->cur_mpu401_addr = 0x330;
                            break;
                        case 1:
                            optimc->cur_mpu401_addr = 0x320;
                            break;
                        case 2:
                            optimc->cur_mpu401_addr = 0x310;
                            break;
                        case 3:
                            optimc->cur_mpu401_addr = 0x300;
                            break;

                        default:
                            break;
                    }
                    mpu401_change_addr(optimc->mpu, optimc->cur_mpu401_addr);
                    mpu401_setirq(optimc->mpu, optimc->cur_mpu401_irq);
                }
                break;

            default:
                break;
        }
    }
    if (optimc->reg_enabled)
        optimc->reg_enabled = 0;
    if ((addr == 0xF8F) && ((val == optimc->type) || (val == 0x00))) {
        if ((addr == 0xF8F) && (val == optimc->type) && !optimc->reg_enabled)
            optimc->reg_enabled = 1;
        if (reg_enable_phase) {
            switch (reg_enable_phase) {
                case 1:
                    if (val == optimc->type) {
                        reg_enable_phase++;
                    }
                    break;
                case 2:
                    if (val == 0x00) {
                        reg_enable_phase++;
                    }
                    break;
                case 3:
                    if (val == optimc->type) {
                        optimc->regs[2]  = 0x2;
                        reg_enable_phase = 1;
                    }
                    break;

                default:
                    break;
            }
        } else
            reg_enable_phase = 1;
        return;
    }
}

static uint8_t
optimc_reg_read(uint16_t addr, void *priv)
{
    optimc_t *optimc = (optimc_t *) priv;
    uint8_t   temp   = 0xFF;

    if (optimc->reg_enabled) {
        switch (addr - 0xF8D) {
            case 0: /* MC1 */
            case 1: /* MC2 */
            case 3: /* MC4 */
            case 4: /* MC5 */
            case 5: /* MC6 */
                temp = optimc->regs[addr - 0xF8D];
                break;
            case 2: /* MC3 */
                temp = (optimc->regs[2] & ~0x3) | 0x2;
                break;

            default:
                break;
        }
        optimc->reg_enabled = 0;
    }
    return temp;
}

static void *
optimc_init(const device_t *info)
{
    optimc_t *optimc = calloc(1, sizeof(optimc_t));

    optimc->type = info->local & 0xFF;

    optimc->cur_wss_addr       = 0x530;
    optimc->cur_mode           = 0;
    optimc->cur_addr           = 0x220;
    optimc->cur_irq            = 5;
    optimc->cur_wss_enabled    = 0;
    optimc->cur_dma            = 1;
    optimc->cur_mpu401_irq     = 9;
    optimc->cur_mpu401_addr    = 0x330;
    optimc->cur_mpu401_enabled = 1;

    optimc->regs[0] = 0x00;
    optimc->regs[1] = 0x03;
    optimc->regs[2] = 0x00;
    optimc->regs[3] = 0x00;
    optimc->regs[4] = 0x3F;
    optimc->regs[5] = 0x83;

    optimc->gameport = gameport_add(&gameport_pnp_device);
    gameport_remap(optimc->gameport, (optimc->regs[0] & 0x1) ? 0x00 : 0x200);

    if (info->local & 0x100)
        ad1848_init(&optimc->ad1848, AD1848_TYPE_CS4231);
    else
        ad1848_init(&optimc->ad1848, AD1848_TYPE_DEFAULT);

    ad1848_setirq(&optimc->ad1848, optimc->cur_wss_irq);
    ad1848_setdma(&optimc->ad1848, optimc->cur_wss_dma);

    io_sethandler(0xF8D, 6, optimc_reg_read, NULL, NULL, optimc_reg_write, NULL, NULL, optimc);

    io_sethandler(optimc->cur_wss_addr, 0x0004, optimc_wss_read, NULL, NULL, optimc_wss_write, NULL, NULL, optimc);
    io_sethandler(optimc->cur_wss_addr + 0x0004, 0x0004, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &optimc->ad1848);

    optimc->sb              = calloc(1, sizeof(sb_t));
    optimc->sb->opl_enabled = 1;

    optimc->fm_type = (info->local & OPTIMC_OPL4) ? FM_YMF278B : FM_YMF262;

    sb_dsp_set_real_opl(&optimc->sb->dsp, optimc->fm_type != FM_YMF278B);
    sb_dsp_init(&optimc->sb->dsp, SBPRO2_DSP_302, SB_SUBTYPE_DEFAULT, optimc);
    sb_dsp_setaddr(&optimc->sb->dsp, optimc->cur_addr);
    sb_dsp_setirq(&optimc->sb->dsp, optimc->cur_irq);
    sb_dsp_setdma8(&optimc->sb->dsp, optimc->cur_dma);
    sb_ct1345_mixer_reset(optimc->sb);

    optimc->sb->opl_mixer = optimc;
    optimc->sb->opl_mix   = optimc_filter_opl;

    fm_driver_get(optimc->fm_type, &optimc->sb->opl);
    io_sethandler(optimc->cur_addr + 0, 0x0004, optimc->sb->opl.read, NULL, NULL, optimc->sb->opl.write, NULL, NULL, optimc->sb->opl.priv);
    io_sethandler(optimc->cur_addr + 8, 0x0002, optimc->sb->opl.read, NULL, NULL, optimc->sb->opl.write, NULL, NULL, optimc->sb->opl.priv);
    io_sethandler(0x0388, 0x0004, optimc->sb->opl.read, NULL, NULL, optimc->sb->opl.write, NULL, NULL, optimc->sb->opl.priv);
    if (optimc->fm_type == FM_YMF278B)
        io_sethandler(0x380, 2, optimc->sb->opl.read, NULL, NULL, optimc->sb->opl.write, NULL, NULL, optimc->sb->opl.priv);

    io_sethandler(optimc->cur_addr + 4, 0x0002, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, optimc->sb);

    sound_add_handler(optimc_get_buffer, optimc);
    if (optimc->fm_type == FM_YMF278B)
        wavetable_add_handler(sb_get_music_buffer_sbpro, optimc->sb);
    else
        music_add_handler(sb_get_music_buffer_sbpro, optimc->sb);
    sound_set_cd_audio_filter(sbpro_filter_cd_audio, optimc->sb); /* CD audio filter for the default context */

    optimc->mpu = (mpu_t *) calloc(1, sizeof(mpu_t));
    mpu401_init(optimc->mpu, optimc->cur_mpu401_addr, optimc->cur_mpu401_irq, M_UART, device_get_config_int("receive_input401"));

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &optimc->sb->dsp);

    return optimc;
}

static void
optimc_close(void *priv)
{
    optimc_t *optimc = (optimc_t *) priv;

    sb_close(optimc->sb);
    free(optimc->mpu);
    free(priv);
}

static void
optimc_speed_changed(void *priv)
{
    optimc_t *optimc = (optimc_t *) priv;

    ad1848_speed_changed(&optimc->ad1848);
    sb_speed_changed(optimc->sb);
}

static int
mirosound_pcm10_available(void)
{
    return rom_present("roms/sound/yamaha/yrw801.rom");
}

static const device_config_t optimc_config[] = {
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

const device_t acermagic_s20_device = {
    .name          = "AcerMagic S20",
    .internal_name = "acermagic_s20",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 0xE3 | OPTIMC_CS4231,
    .init          = optimc_init,
    .close         = optimc_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = optimc_speed_changed,
    .force_redraw  = NULL,
    .config        = optimc_config
};

const device_t mirosound_pcm10_device = {
    .name          = "miroSOUND PCM10",
    .internal_name = "mirosound_pcm10",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 0xE3 | OPTIMC_OPL4,
    .init          = optimc_init,
    .close         = optimc_close,
    .reset         = NULL,
    .available     = mirosound_pcm10_available,
    .speed_changed = optimc_speed_changed,
    .force_redraw  = NULL,
    .config        = optimc_config
};
