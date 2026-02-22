/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          OPTi MediaCHIPS 82C929A/82C93x (also known as OPTi MAD16 Pro) audio controller emulation.
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
#include <86box/hdc.h>
#include <86box/isapnp.h>
#include <86box/hdc_ide.h>
#include <86box/log.h>

#define PNP_ROM_OPTI931 "roms/sound/opti931/adsrom.bin"

#ifdef ENABLE_OPTIMC_LOG
int optimc_do_log = ENABLE_OPTIMC_LOG;

static void
optimc_log(void *priv, const char *fmt, ...)
{
    if (optimc_do_log) {
        va_list ap;
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define optimc_log(fmt, ...)
#endif

static int optimc_wss_dma[4] = { 0, 0, 1, 3 };
static int optimc_wss_irq[8] = { 5, 7, 9, 10, 11, 12, 14, 15 };
static int opti930_wss_irq[8] = { 0, 7, 9, 10, 11, 5, 0, 0 };
static double opti930_vols_5bits[32];

enum optimc_types {
    OPTI_929 = 0xE3,
    OPTI_930 = 0xE4,
};

enum optimc_local_flags {
    OPTIMC_CS4231 = 0x100,
    OPTIMC_OPL4   = 0x200,
    OPTI_931      = 0x400,
};

typedef struct optimc_t {
    uint8_t type;
    uint8_t fm_type;

    uint8_t wss_config;
    uint8_t reg_enabled;
    uint8_t passwd_enabled;
    uint8_t index;

    uint16_t cur_addr;
    uint16_t cur_wss_addr;
    uint16_t cur_mpu401_addr;
    uint16_t cur_opti930_addr; /* OPTi 930 relocatable index/data registers */
    uint16_t cur_opl_addr;

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
    uint8_t regs[26];
    uint8_t opti930_mcbase;
    uint8_t lastpw;
    uint8_t max_reg;
    uint8_t pnpidx;
    double master_l;
    double master_r;

    void                   *pnp_card;
    uint8_t                pnp_rom[401];
    isapnp_device_config_t *opti931_pnp_config;

    void *    log;  /* New logging system */
} optimc_t, opti_82c929a_t;

static void
opti931_isapnp_write(uint16_t addr, uint8_t val, void *priv)
{
    optimc_t *optimc = (optimc_t *) priv;

    optimc_log(optimc->log, "OPTi931 ISAPnP Write port = %04X, val = %02X\n", addr, val);

    if (addr == 0x279)
        optimc->pnpidx = val;

    if (addr == 0xA79) {
        if (optimc->pnpidx == 0x00) { /* ISAPnP Set RD_DATA Port */
            optimc_log(optimc->log, "OPTi931 ISAPnP Read Data port set to %04X\n", val);
            optimc->regs[14] = val;
        }
    }
}

static void
opti930_update_mastervol(void *priv)
{
    optimc_t *optimc = (optimc_t *) priv;
    /* Master volume attenuation */
    if (optimc->ad1848.regs[22] & 0x80)
        optimc->master_l = 0;
    else
        optimc->master_l = opti930_vols_5bits[optimc->ad1848.regs[22] & 0x1F] / 65536.0;

    if (optimc->ad1848.regs[23] & 0x80)
        optimc->master_r = 0;
    else
        optimc->master_r = opti930_vols_5bits[optimc->ad1848.regs[23] & 0x1F] / 65536.0;
}

void
opti930_filter_cd_audio(int channel, double *buffer, void *priv)
{
    optimc_t *optimc = (optimc_t *) priv;

    opti930_update_mastervol(optimc);

    const double cd_vol = channel ? optimc->ad1848.cd_vol_r : optimc->ad1848.cd_vol_l;
    double       master = channel ? optimc->master_r : optimc->master_l;
    double       c      = ((*buffer  * cd_vol / 3.0) * master) / 65536.0;

    *buffer = c;
}

static void
opti930_filter_opl(void *priv, double *out_l, double *out_r)
{
    optimc_t *optimc = (optimc_t *) priv;

    if (optimc->cur_wss_enabled) {
        opti930_update_mastervol(optimc);
        *out_l /= optimc->sb->mixer_sbpro.fm_l;
        *out_r /= optimc->sb->mixer_sbpro.fm_r;
        *out_l *= optimc->master_l;
        *out_r *= optimc->master_r;
        ad1848_filter_channel((void *) &optimc->ad1848, AD1848_AUX2, out_l, out_r);
    }
}

static void
optimc_filter_opl(void *priv, double *out_l, double *out_r)
{
    optimc_t *optimc = (optimc_t *) priv;

    if (optimc->cur_wss_enabled) {
        *out_l /= optimc->sb->mixer_sbpro.fm_l;
        *out_r /= optimc->sb->mixer_sbpro.fm_r;
        ad1848_filter_channel((void *) &optimc->ad1848, AD1848_AUX2, out_l, out_r);
    }
}

static uint8_t
optimc_wss_read(UNUSED(uint16_t addr), void *priv)
{
    const optimc_t *optimc = (optimc_t *) priv;

    if (optimc->type == OPTI_929 && !(optimc->regs[4] & 0x10) && optimc->cur_mode == 0)
        return 0xFF;

    optimc_log(optimc->log, "OPTi WSS Read: val = %02X\n", ((optimc->wss_config & 0x40) | 4));
    return 4 | (optimc->wss_config & 0x40);
}

static void
optimc_wss_write(UNUSED(uint16_t addr), uint8_t val, void *priv)
{
    optimc_t *optimc = (optimc_t *) priv;

    optimc_log(optimc->log, "OPTi WSS Write: val = %02X\n", val);
    if (optimc->type == OPTI_929 && !(optimc->regs[4] & 0x10) && optimc->cur_mode == 0)
        return;
    optimc->wss_config = val;
    ad1848_setdma(&optimc->ad1848, optimc_wss_dma[val & 3]);
    if (optimc->type == OPTI_929)
        ad1848_setirq(&optimc->ad1848, optimc_wss_irq[(val >> 3) & 7]);
    else
        ad1848_setirq(&optimc->ad1848, opti930_wss_irq[(val >> 3) & 7]);
}

static void
opti930_get_buffer(int32_t *buffer, int len, void *priv)
{
    optimc_t *optimc = (optimc_t *) priv;

    if (((optimc->max_reg == 11) && (optimc->regs[3] & 0x4)) || ((optimc->max_reg == 25) && !(optimc->regs[3] & 0x4)))
        return;

    /* wss part */
    opti930_update_mastervol(optimc);
    ad1848_update(&optimc->ad1848);
    for (int c = 0; c < len * 2; c++) {
        double out_l = 0.0;
        double out_r = 0.0;

        out_l = (optimc->ad1848.buffer[c] * optimc->master_l);
        out_r = (optimc->ad1848.buffer[c + 1] * optimc->master_r);

        buffer[c] += (int32_t) out_l;
        buffer[c + 1] += (int32_t) out_r;
    }

    optimc->ad1848.pos = 0;

    /* sbprov2 part */
    sb_get_buffer_sbpro(buffer, len, optimc->sb);
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
opti930_reg_write(uint16_t addr, uint8_t val, void *priv)
{
    optimc_t      *optimc           = (optimc_t *) priv;
    uint16_t       idx              = optimc->index;
    uint8_t        old              = optimc->regs[idx];

    if ((addr == optimc->cur_opti930_addr) && (optimc->reg_enabled || !optimc->passwd_enabled)) {
        optimc_log(optimc->log, "OPTi930 Index Write: val = %02X\n", val);
        optimc->index = (val - 1);
    }
    if ((addr == optimc->cur_opti930_addr +1) && (idx > optimc->max_reg)) {
        optimc_log(optimc->log, "OPTi930 Write above max reg!: idx = %02X\n", optimc->index);
        optimc_log(optimc->log, "OPTi930 disable reg access on data write\n");
        optimc->reg_enabled = 0;
        return;
    }
    if ((addr == optimc->cur_opti930_addr +1) && (optimc->reg_enabled || !optimc->passwd_enabled)) {
        switch (idx) {
            case 0: /* MC1 */
                optimc->regs[0] = val;
                if (val != old) {
                    /* Update SBPro address */
                    io_removehandler(optimc->cur_addr + 4, 0x0002, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, optimc->sb);
                    optimc_remove_opl(optimc);
                    optimc->cur_addr = (val & 0x80) ? 0x240 : 0x220;
                    sb_dsp_setaddr(&optimc->sb->dsp, optimc->cur_addr);
                    optimc_add_opl(optimc);
                    io_sethandler(optimc->cur_addr + 4, 0x0002, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, optimc->sb);
                    /* Update WSS address */
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
                    /* Update gameport */
                    if (optimc->max_reg == 11)
                        gameport_remap(optimc->gameport, (optimc->regs[0] & 0x1) ? 0x00 : 0x200);
                    else
                        gameport_remap(optimc->gameport, (optimc->regs[0] & 0x1) ? 0x200 : 0x00);
                }
                break;
            case 1: /* MC2 */
                optimc->regs[1] = val;
                break;
            case 2: /* MC3 */
                optimc->regs[2] = val;
                if (val != old) {
                    switch (val & 0x3) {
                        case 0:
                            break;
                        case 1:
                            optimc->cur_dma = 0;
                            break;
                        case 2:
                            optimc->cur_dma = 1;
                            break;
                        case 3:
                            optimc->cur_dma = 3;
                            break;
                    }
                    switch ((val >> 3) & 0x7) {
                        case 0:
                            break;
                        case 1:
                            optimc->cur_irq = 7;
                            break;
                        case 2:
                            optimc->cur_irq = 9;
                            break;
                        case 3:
                            optimc->cur_irq = 10;
                            break;
                        case 4:
                            optimc->cur_irq = 11;
                            break;
                        case 5:
                        default:
                            optimc->cur_irq = 5;
                            break;
                    }
                    sb_dsp_setirq(&optimc->sb->dsp, optimc->cur_irq);
                    sb_dsp_setdma8(&optimc->sb->dsp, optimc->cur_dma);
                    /* Writes here also set WSS IRQ/DMA, the DOS setup utility and NEC PowerMate V BIOS imply this */
                    /* The OPTi 82c930 driver on the NEC Ready preloads requires this to function properly */
                    ad1848_setdma(&optimc->ad1848, optimc_wss_dma[val & 3]);
                    ad1848_setirq(&optimc->ad1848, opti930_wss_irq[(val >> 3) & 7]);
                }
                break;
            case 3: /* MC4 */
                optimc->regs[3] = val;
                break;
            case 4: /* MC5 */
                optimc->regs[4] = val;
                /* OPTi 930 enables/disables AD1848 MODE2 from here */
                if (val & 0x20) {
                    optimc->ad1848.opti930_mode2 = 1;
                    optimc->ad1848.fmt_mask |= 0x80;
                }
                else {
                    optimc->ad1848.opti930_mode2 = 0;
                    optimc->ad1848.fmt_mask &= ~0x80;
                }
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
                    optimc->cur_mode = optimc->cur_wss_enabled = !!(val & 0x02);
                    sound_set_cd_audio_filter(NULL, NULL);
                    if (optimc->cur_wss_enabled) { /* WSS */
                        sound_set_cd_audio_filter(opti930_filter_cd_audio, optimc);
                        optimc->sb->opl_mixer = optimc;
                        optimc->sb->opl_mix   = opti930_filter_opl;
                    }
                    else { /* SBPro */
                        sound_set_cd_audio_filter(sbpro_filter_cd_audio, optimc->sb);
                        optimc->sb->opl_mixer = NULL;
                        optimc->sb->opl_mix   = NULL;
                    }
                }
                break;
            case 6: /* MC7 */
                optimc->regs[6] = val;
                break;
            case 7: /* MC8 */
                optimc->regs[7] = val;
                break;
            case 8: /* MC9 */
                optimc->regs[8] = val;
                if ((optimc->max_reg == 25) && (val & 0x02)) {
                    optimc_log(optimc->log, "OPTi931 software reset\n");
                    optimc->regs[0] = 0x07;
                    optimc->regs[1] = 0x00;
                    optimc->regs[2] = 0x2A;
                    optimc->regs[3] = 0x14;
                    optimc->regs[4] = 0x00;
                    optimc->regs[5] = 0x81;
                    optimc->regs[6] = 0x00;
                    optimc->regs[7] = 0x00;
                    optimc->regs[8] = 0x00;
                    optimc->regs[9] = 0x00;
                    optimc->regs[10] = 0x00;
                    optimc->regs[11] = 0x00;
                    optimc->regs[12] = 0x11;
                    optimc->regs[13] = 0x00;
                    optimc->regs[14] = 0x00;
                    optimc->regs[15] = 0x00;
                    optimc->regs[16] = 0x00;
                    optimc->regs[17] = 0x88; /* Silicon rev 0.1, 931-AD adapter mode */
                    optimc->regs[18] = 0x01;
                    optimc->regs[19] = 0x00;
                    optimc->regs[20] = 0x00;
                    optimc->regs[21] = 0x00;
                    optimc->regs[22] = 0x00;
                    optimc->regs[23] = 0x00;
                    optimc->regs[24] = 0x00;
                    optimc->regs[25] = 0x00;
                    isapnp_enable_card(optimc, 0);
                    isapnp_enable_card(optimc, 1);
                }
                break;
            case 9: /* MC10 */
                optimc->regs[9] = val;
                break;
            case 10: /* MC11, read-only */
                break;
            case 11: /* MC12 */
                optimc->regs[11] = val;
                break;
            case 12: /* MC13, read-only */
                break;
            case 13: /* MC14, read-only */
                break;
            case 14: /* MC15, read-only */
                break;
            case 15: /* MC16 */
                optimc->regs[15] = val;
                break;
            case 16: /* MC17 */
                optimc->regs[16] = val;
                break;
            case 17: /* MC18 */
                optimc->regs[17] = optimc->regs[17] | (val & 0xc0);
                break;
            case 18: /* MC19 */
                optimc->regs[18] = val;
                break;
            case 19: /* MC20 */
                optimc->regs[19] = val;
                break;
            case 20: /* MC21 */
                optimc->regs[20] = val;
                break;
            case 21: /* MC22 */
                optimc->regs[21] = val;
                break;
            case 22: /* MC23 */
                optimc->regs[22] = val;
                break;
            case 23: /* MC24 */
                optimc->regs[23] = val;
                break;
            case 24: /* MC25 */
                optimc->regs[24] = val;
                break;
            case 25: /* MC26 */
                optimc->regs[25] = val;
                break;
            default:
                break;
        }
        optimc_log(optimc->log, "OPTi930 Data Write: idx = %02X, val = %02X\n", idx, val);
    }
    if ((optimc->cur_opti930_addr != 0xF8E) && optimc->reg_enabled) {
        optimc_log(optimc->log, "OPTi930 disable reg access on data write\n");
        optimc->reg_enabled = 0;
    }
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
    optimc_log(optimc->log, "OPTi929 Write: idx = %02X, val = %02X\n", idx, val);
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
opti930_reg_read(uint16_t addr, void *priv)
{
    optimc_t *optimc = (optimc_t *) priv;
    uint16_t  idx    = optimc->index;
    uint8_t   temp   = 0xFF;

    if ((addr == optimc->cur_opti930_addr) && (optimc->reg_enabled || !optimc->passwd_enabled))
        temp = optimc->index;
    if ((addr == optimc->cur_opti930_addr +1) && (idx > optimc->max_reg)) {
        optimc_log(optimc->log, "OPTi930 Read above max reg!: idx = %02X\n", optimc->index);
        optimc_log(optimc->log, "OPTi930 disable reg access on data read\n");
        optimc->reg_enabled = 0;
        return 0xFF;
    }
    if ((addr == optimc->cur_opti930_addr +1) && (optimc->reg_enabled || !optimc->passwd_enabled)) {
        switch (idx) {
            case 0 ... 9:
                temp = optimc->regs[optimc->index];
                break;
            case 10:
                temp = ((optimc->ad1848.regs[24] & 0x10) >> 2);
                break;
            case 11:
                temp = optimc->regs[11];
                break;
            case 12:
                temp = optimc->regs[12];
                break;
            case 13:
                temp = optimc->regs[13];
                break;
            case 14:
                temp = optimc->regs[14];
                break;
            case 15 ... 16:
                temp = optimc->regs[optimc->index];
                break;
            case 17:
                temp = optimc->regs[17] ^ 0x80;
                break;
            case 18 ... 25:
                temp = optimc->regs[optimc->index];
                break;
        }
    }
    if ((optimc->cur_opti930_addr != 0xF8E) && optimc->reg_enabled) {
        optimc_log(optimc->log, "OPTi930 disable reg access on data read\n");
        optimc->reg_enabled = 0;
    }
    optimc_log(optimc->log, "OPTi930 Read: idx = %02X, val = %02X\n", optimc->index, temp);
    return temp;
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
    optimc_log(optimc->log, "OPTi929 Read: addr = %02X, val = %02X\n", addr, temp);
    return temp;
}

static void
opti930_passwd_write(uint16_t addr, uint8_t val, void *priv)
{
    optimc_t      *optimc           = (optimc_t *) priv;

    optimc_log(optimc->log, "OPTi930 Password Write: val = %02X\n", val);
    optimc_log(optimc->log, "OPTi930 last pw value: %02X\n", optimc->lastpw);
    if ((addr == 0xF8F) && (optimc->reg_enabled)) {
        optimc_log(optimc->log, "OPTi930: Removing old address handler at %04X\n", optimc->cur_opti930_addr);
        io_removehandler(optimc->cur_opti930_addr, 2, opti930_reg_read, NULL, NULL, opti930_reg_write, NULL, NULL, optimc); /* Relocatable MCBase */
        optimc->opti930_mcbase = val;
        optimc->cur_opti930_addr = (((val & 0x1f) << 4) + 0xE0E);
        optimc->passwd_enabled = (val & 0x80) ? 0 : 1;
        optimc_log(optimc->log, "OPTi930 register base now %04X\n", optimc->cur_opti930_addr);
        optimc_log(optimc->log, "OPTi930 password enable: %02X\n", optimc->passwd_enabled);
        io_sethandler(optimc->cur_opti930_addr, 2, opti930_reg_read, NULL, NULL, opti930_reg_write, NULL, NULL, optimc); /* Relocatable MCBase */
        optimc_log(optimc->log, "OPTi930 Disabling reg access\n");
        optimc->reg_enabled = 0;
    }
    if ((addr == 0xF8F) && (val == optimc->type) && !(optimc->reg_enabled)) {
        optimc_log(optimc->log, "OPTi930 Enabling reg access\n");
        optimc->reg_enabled = 1;
    }
    if ((addr == 0xF8F) && (val != optimc->type) && (optimc->lastpw != OPTI_930)) {
        optimc_log(optimc->log, "OPTi930 Disabling reg access\n");
        optimc->reg_enabled = 0;
    }
    optimc->lastpw = val;
}

static void
opti931_passwd_write(uint16_t addr, uint8_t val, void *priv)
{
    optimc_t      *optimc           = (optimc_t *) priv;

    optimc_log(optimc->log, "OPTi931 Password Write: val = %02X\n", val);

    if (optimc->reg_enabled) {
        optimc->opti930_mcbase = val;
        optimc->passwd_enabled = (val & 0x80) ? 0 : 1;
        optimc_log(optimc->log, "OPTi931 password enable: %02X\n", optimc->passwd_enabled);
        optimc_log(optimc->log, "OPTi931 Disabling reg access\n");
        optimc->reg_enabled = 0;
    }
    if ((val == optimc->type) && !(optimc->reg_enabled)) {
        optimc_log(optimc->log, "OPTi931 Enabling reg access\n");
        optimc->reg_enabled = 1;
    }
    if ((val != optimc->type) && (optimc->lastpw != OPTI_930)) {
        optimc_log(optimc->log, "OPTi931 Disabling reg access\n");
        optimc->reg_enabled = 0;
    }
    optimc->lastpw = val;
}

static void
opti931_pnp_csn_changed(uint8_t csn, void *priv)
{
    optimc_t *optimc = (optimc_t *) priv;

    optimc_log(optimc->log, "PnP CSN changed to %02X\n", csn);

    optimc->regs[13] = csn;
    optimc->regs[12] = optimc->regs[12] | 0x80;
}

static void
opti931_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv)
{
    optimc_t *optimc = (optimc_t *) priv;

    optimc_log(optimc->log, "PnP Config changed\n");

    switch (ld) {
        case 0: /* IDE CD-ROM */
            ide_pnp_config_changed_opti931(0, config, (void *) 3);
            break;
        case 1: /* WSS/OPL3/SBPro/Control regs */
            if (optimc->cur_wss_addr) {
                io_removehandler(optimc->cur_wss_addr, 0x0004, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &optimc->ad1848);
                optimc->cur_wss_addr = 0;
                optimc->cur_wss_enabled = 0;
            }

            if (optimc->cur_opl_addr) {
                io_removehandler(optimc->cur_opl_addr, 0x0004, optimc->sb->opl.read, NULL, NULL, optimc->sb->opl.write, NULL, NULL, optimc->sb->opl.priv);
                optimc->cur_opl_addr = 0;
            }

            if (optimc->cur_addr) {
                sb_dsp_setaddr(&optimc->sb->dsp, 0);
                io_removehandler(optimc->cur_addr + 4, 0x0002, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, optimc->sb);
                io_removehandler(optimc->cur_addr + 0, 0x0004, optimc->sb->opl.read, NULL, NULL, optimc->sb->opl.write, NULL, NULL, optimc->sb->opl.priv);
                io_removehandler(optimc->cur_addr + 8, 0x0002, optimc->sb->opl.read, NULL, NULL, optimc->sb->opl.write, NULL, NULL, optimc->sb->opl.priv);
                optimc->cur_addr = 0;
            }

            if (optimc->cur_opti930_addr) {
                io_removehandler(optimc->cur_opti930_addr, 2, opti930_reg_read, NULL, NULL, opti930_reg_write, NULL, NULL, optimc); /* Relocatable MCBase */
                optimc->cur_opti930_addr = 0;
            }

            ad1848_setirq(&optimc->ad1848, 0);
            sb_dsp_setirq(&optimc->sb->dsp, 0);

            ad1848_setdma(&optimc->ad1848, 0);
            sb_dsp_setdma8(&optimc->sb->dsp, 0);

            if (config->activate) {
                if (config->io[0].base != ISAPNP_IO_DISABLED) {
                    optimc->cur_wss_addr = config->io[0].base;
                    optimc->cur_wss_enabled = 1;
                    optimc_log(optimc->log, "Updating WSS I/O port, WSS addr = %04X\n", optimc->cur_wss_addr);
                    io_sethandler(optimc->cur_wss_addr, 0x0004, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &optimc->ad1848);
                }
                if (config->io[1].base != ISAPNP_IO_DISABLED) {
                    optimc->cur_opl_addr = config->io[1].base + 8; /* 12 ports are reserved for OPTiFM, only the top 4 are needed for OPL3 */
                    optimc_log(optimc->log, "Updating OPL I/O port, OPL addr = %04X\n", optimc->cur_opl_addr);
                    io_sethandler(optimc->cur_opl_addr, 0x0004, optimc->sb->opl.read, NULL, NULL, optimc->sb->opl.write, NULL, NULL, optimc->sb->opl.priv);
                }
                if (config->io[2].base != ISAPNP_IO_DISABLED) {
                    optimc->cur_addr = config->io[2].base;
                    optimc_log(optimc->log, "Updating SB DSP I/O port, SB DSP addr = %04X\n", optimc->cur_addr);
                    sb_dsp_setaddr(&optimc->sb->dsp, optimc->cur_addr);
                    io_sethandler(optimc->cur_addr + 4, 0x0002, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, optimc->sb);
                    io_sethandler(optimc->cur_addr + 0, 0x0004, optimc->sb->opl.read, NULL, NULL, optimc->sb->opl.write, NULL, NULL, optimc->sb->opl.priv);
                    io_sethandler(optimc->cur_addr + 8, 0x0002, optimc->sb->opl.read, NULL, NULL, optimc->sb->opl.write, NULL, NULL, optimc->sb->opl.priv);
                }
                if (config->io[3].base != ISAPNP_IO_DISABLED) {
                    optimc->cur_opti930_addr = config->io[3].base + 1;
                    optimc_log(optimc->log, "Updating OPTi930 control I/O port, OPTi930 control addr = %04X\n", optimc->cur_opti930_addr);
                    io_sethandler(optimc->cur_opti930_addr, 2, opti930_reg_read, NULL, NULL, opti930_reg_write, NULL, NULL, optimc); /* Relocatable MCBase */
                }
                if (config->irq[0].irq != ISAPNP_IRQ_DISABLED) {
                    optimc->cur_irq = config->irq[0].irq;
                    optimc->cur_wss_irq = config->irq[0].irq;
                    sb_dsp_setirq(&optimc->sb->dsp, optimc->cur_irq);
                    ad1848_setirq(&optimc->ad1848, optimc->cur_wss_irq);
                    optimc_log(optimc->log, "Updated WSS/SB IRQ to %04X\n", optimc->cur_irq);
                }
                if (config->dma[0].dma != ISAPNP_DMA_DISABLED) {
                    optimc->cur_dma = config->dma[0].dma;
                    optimc->cur_wss_dma = config->dma[0].dma;
                    sb_dsp_setdma8(&optimc->sb->dsp, optimc->cur_dma);
                    ad1848_setdma(&optimc->ad1848, optimc->cur_wss_dma);
                    optimc_log(optimc->log, "Updated WSS Playback/SB DMA to %04X\n", optimc->cur_dma);
                }
            }
            break;
        case 2: /* Gameport */
            gameport_remap(optimc->gameport, (config->activate && (config->io[0].base != ISAPNP_IO_DISABLED)) ? config->io[0].base : 0);
            break;
        case 3: /* MPU401 */
            if (config->activate) {
                if (config->io[0].base != ISAPNP_IO_DISABLED) {
                    optimc->cur_mpu401_addr = config->io[0].base;
                    optimc_log(optimc->log, "Updating MPU401 I/O port, MPU401 addr = %04X\n", optimc->cur_mpu401_addr);
                    mpu401_change_addr(optimc->mpu, optimc->cur_mpu401_addr);
                }
                if (config->irq[0].irq != ISAPNP_IRQ_DISABLED) {
                    optimc->cur_mpu401_irq = config->irq[0].irq;
                    mpu401_setirq(optimc->mpu, optimc->cur_mpu401_irq);
                    optimc_log(optimc->log, "Updated MPU401 IRQ to %04X\n", optimc->cur_mpu401_irq);
                }
            }
            break;
        default:
            break;
    }
}

static void *
optimc_init(const device_t *info)
{
    optimc_t *optimc = calloc(1, sizeof(optimc_t));
    uint8_t c;
    double  attenuation;

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
    optimc->cur_opl_addr       = 0x388;

    if (optimc->type == OPTI_929) {
        optimc->regs[0] = 0x00;
        optimc->regs[1] = 0x03;
        optimc->regs[2] = 0x00;
        optimc->regs[3] = 0x00;
        optimc->regs[4] = 0x3F;
        optimc->regs[5] = 0x83;
        optimc->max_reg = 5;
    }
    if (optimc->type == OPTI_930) {
        optimc->regs[0] = 0x00;
        optimc->regs[1] = 0x00;
        optimc->regs[2] = 0x2A;
        optimc->regs[3] = 0x10;
        optimc->regs[4] = 0x00;
        optimc->regs[5] = 0x81;
        optimc->regs[6] = 0x00;
        optimc->regs[7] = 0x00;
        optimc->regs[8] = 0x00;
        optimc->regs[9] = 0x00;
        optimc->regs[10] = 0x00;
        optimc->regs[11] = 0x00;
        optimc->max_reg = 11;
        optimc->opti930_mcbase = 0x00; /* Password enable, MCBase E0Eh */
        optimc->cur_opti930_addr = 0xE0E;
    }
    if ((optimc->type == OPTI_930) && (info->local & OPTI_931)) {
        optimc->regs[0]  = 0x07;
        optimc->regs[3]  = 0x14;
        optimc->regs[12] = 0x11;
        optimc->regs[13] = 0x00;
        optimc->regs[14] = 0x00;
        optimc->regs[15] = 0x00;
        optimc->regs[16] = 0x00;
        optimc->regs[17] = 0x88; /* Silicon rev 0.1, 931-AD adapter mode */
        optimc->regs[18] = 0x01;
        optimc->regs[19] = 0x00;
        optimc->regs[20] = 0x00;
        optimc->regs[21] = 0x00;
        optimc->regs[22] = 0x00;
        optimc->regs[23] = 0x00;
        optimc->regs[24] = 0x00;
        optimc->regs[25] = 0x00;
        optimc->max_reg = 25;
        optimc->cur_wss_addr = 0x534;
    }

    optimc->log = log_open("OPTIMC");

    optimc->gameport = gameport_add(&gameport_pnp_device);
    gameport_remap(optimc->gameport, (optimc->regs[0] & 0x1) ? 0x00 : 0x200);

    if (optimc->type == OPTI_930)
        ad1848_init(&optimc->ad1848, AD1848_TYPE_OPTI930);
    else if (info->local & 0x100)
        ad1848_init(&optimc->ad1848, AD1848_TYPE_CS4231);
    else
        ad1848_init(&optimc->ad1848, AD1848_TYPE_DEFAULT);

    ad1848_set_cd_audio_channel(&optimc->ad1848, (info->local & 0x100) ? AD1848_LINE_IN : AD1848_AUX1);
    ad1848_setirq(&optimc->ad1848, optimc->cur_wss_irq);
    ad1848_setdma(&optimc->ad1848, optimc->cur_wss_dma);

    if (optimc->type == OPTI_929) {
        io_sethandler(0xF8D, 6, optimc_reg_read, NULL, NULL, optimc_reg_write, NULL, NULL, optimc);
    }
    if ((optimc->type == OPTI_930) && (!(info->local & OPTI_931))) {
        io_sethandler(0xF8F, 1, NULL, NULL, NULL, opti930_passwd_write, NULL, NULL, optimc); /* Fixed password reg, write-only */
        io_sethandler(optimc->cur_opti930_addr, 2, opti930_reg_read, NULL, NULL, opti930_reg_write, NULL, NULL, optimc); /* Relocatable MCBase */
    }
    if ((optimc->type == OPTI_930) && (info->local & OPTI_931)) {
        io_sethandler(0xF8D, 1, NULL, NULL, NULL, opti931_passwd_write, NULL, NULL, optimc); /* Fixed password reg, write-only */
        io_sethandler(optimc->cur_opti930_addr, 2, opti930_reg_read, NULL, NULL, opti930_reg_write, NULL, NULL, optimc); /* Relocatable MCBase */
    }

    if (!(info->local & OPTI_931)) {
        io_sethandler(optimc->cur_wss_addr, 0x0004, optimc_wss_read, NULL, NULL, optimc_wss_write, NULL, NULL, optimc);
        io_sethandler(optimc->cur_wss_addr + 0x0004, 0x0004, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &optimc->ad1848);
    } else
        io_sethandler(optimc->cur_wss_addr, 0x0004, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &optimc->ad1848);

    optimc->sb              = calloc(1, sizeof(sb_t));
    optimc->sb->opl_enabled = 1;

    optimc->fm_type = (info->local & OPTIMC_OPL4) ? FM_YMF278B : FM_YMF262;

    sb_dsp_set_real_opl(&optimc->sb->dsp, optimc->fm_type != FM_YMF278B);
    sb_dsp_init(&optimc->sb->dsp, SBPRO_DSP_302, SB_SUBTYPE_DEFAULT, optimc);
    sb_dsp_setaddr(&optimc->sb->dsp, optimc->cur_addr);
    sb_dsp_setirq(&optimc->sb->dsp, optimc->cur_irq);
    sb_dsp_setdma8(&optimc->sb->dsp, optimc->cur_dma);
    sb_ct1345_mixer_reset(optimc->sb);

    if (optimc->type == OPTI_930) {
        optimc->sb->opl_mixer = optimc;
        optimc->sb->opl_mix   = opti930_filter_opl;
    }
    else {
        optimc->sb->opl_mixer = optimc;
        optimc->sb->opl_mix   = optimc_filter_opl;
    }

    fm_driver_get(optimc->fm_type, &optimc->sb->opl);
    io_sethandler(optimc->cur_addr + 0, 0x0004, optimc->sb->opl.read, NULL, NULL, optimc->sb->opl.write, NULL, NULL, optimc->sb->opl.priv);
    io_sethandler(optimc->cur_addr + 8, 0x0002, optimc->sb->opl.read, NULL, NULL, optimc->sb->opl.write, NULL, NULL, optimc->sb->opl.priv);
    io_sethandler(0x0388, 0x0004, optimc->sb->opl.read, NULL, NULL, optimc->sb->opl.write, NULL, NULL, optimc->sb->opl.priv);
    if (optimc->fm_type == FM_YMF278B)
        io_sethandler(0x380, 2, optimc->sb->opl.read, NULL, NULL, optimc->sb->opl.write, NULL, NULL, optimc->sb->opl.priv);

    io_sethandler(optimc->cur_addr + 4, 0x0002, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, optimc->sb);

    if (optimc->type == OPTI_930)
        sound_add_handler(opti930_get_buffer, optimc);
    else
        sound_add_handler(optimc_get_buffer, optimc);
    if (optimc->fm_type == FM_YMF278B)
        wavetable_add_handler(sb_get_music_buffer_sbpro, optimc->sb);
    else
        music_add_handler(sb_get_music_buffer_sbpro, optimc->sb);
    if (optimc->type == OPTI_930)
        ad1848_set_cd_audio_channel(&optimc->ad1848, AD1848_AUX1);
    sound_set_cd_audio_filter(NULL, NULL); /* Seems to be necessary for the filter below to apply */
    sound_set_cd_audio_filter(sbpro_filter_cd_audio, optimc->sb); /* CD audio filter for the default context */

    optimc->mpu = (mpu_t *) calloc(1, sizeof(mpu_t));
    mpu401_init(optimc->mpu, optimc->cur_mpu401_addr, optimc->cur_mpu401_irq, M_UART, device_get_config_int("receive_input401"));

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &optimc->sb->dsp);

    if (info->local & OPTI_931) {
        const char *pnp_rom_file = NULL;
        uint16_t    pnp_rom_len  = 401;
        pnp_rom_file = PNP_ROM_OPTI931;

        uint8_t *pnp_rom = NULL;
        if (pnp_rom_file) {
            FILE *fp = rom_fopen(pnp_rom_file, "rb");
            if (fp) {
                if (fread(optimc->pnp_rom, 1, pnp_rom_len, fp) == pnp_rom_len)
                    pnp_rom = optimc->pnp_rom;
                fclose(fp);
            }
        }
        optimc->pnp_card = isapnp_add_card(pnp_rom, sizeof(optimc->pnp_rom), opti931_pnp_config_changed,
                                           opti931_pnp_csn_changed, NULL, NULL, optimc);

        /* Set up ISAPnP handlers to intercept Read Data port changes */
        io_sethandler(0x279, 0x0001, NULL, NULL, NULL, opti931_isapnp_write, NULL, NULL, optimc);
        io_sethandler(0xA79, 0x0001, NULL, NULL, NULL, opti931_isapnp_write, NULL, NULL, optimc);

        /* Add ISAPnP quaternary IDE controller */
        device_add(&ide_qua_pnp_device);
        other_ide_present++;
        ide_remove_handlers(3);
    }

    /* OPTi 930 DOS sound test utility starts DMA playback without setting a time constant likely making */
    /* an assumption about the power-on state of the OPTi 930's SBPro DSP, set the power-on default time */
    /* constant for 22KHz Mono so the sound test utility passes the SBPro test */
    if (optimc->type == OPTI_930)
        optimc->sb->dsp.sb_timeo = 211;

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

        opti930_vols_5bits[c] = (attenuation * 65536);
    }

    return optimc;
}

static void
optimc_close(void *priv)
{
    optimc_t *optimc = (optimc_t *) priv;

    if (optimc->log != NULL) {
        log_close(optimc->log);
        optimc->log = NULL;
    }

    sb_close(optimc->sb);
    free(optimc->mpu);
    free(priv);
}

static int
opti931_available(void)
{
    return rom_present(PNP_ROM_OPTI931);
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
    .flags         = DEVICE_ISA16,
    .local         = OPTI_929 | OPTIMC_CS4231,
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
    .flags         = DEVICE_ISA16,
    .local         = OPTI_929 | OPTIMC_OPL4,
    .init          = optimc_init,
    .close         = optimc_close,
    .reset         = NULL,
    .available     = mirosound_pcm10_available,
    .speed_changed = optimc_speed_changed,
    .force_redraw  = NULL,
    .config        = optimc_config
};

const device_t opti_82c930_device = {
    .name          = "OPTi 82C930",
    .internal_name = "opti_82c930",
    .flags         = DEVICE_ISA16,
    .local         = OPTI_930 | OPTIMC_CS4231,
    .init          = optimc_init,
    .close         = optimc_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = optimc_speed_changed,
    .force_redraw  = NULL,
    .config        = optimc_config
};

const device_t opti_82c931_device = {
    .name          = "OPTi 82C931",
    .internal_name = "opti_82c931",
    .flags         = DEVICE_ISA16,
    .local         = OPTI_930 | OPTIMC_CS4231 | OPTI_931,
    .init          = optimc_init,
    .close         = optimc_close,
    .reset         = NULL,
    .available     = opti931_available,
    .speed_changed = optimc_speed_changed,
    .force_redraw  = NULL,
    .config        = optimc_config
};
