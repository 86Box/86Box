/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Yamaha YMF71x (OPL3-SA2/3) audio controller emulation.
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
#include <86box/i2c.h>
#include <86box/isapnp.h>
#include <86box/nvr.h>
#include <86box/snd_opl.h>
#include <86box/filters.h>
#include "cpu.h"

#define PNP_ROM_YMF718 "roms/sound/ymf71x/UFC-101.BIN"
#define PNP_ROM_YMF719 "roms/sound/ymf71x/PnP1.BIN"

#define YMF71X_NO_EEPROM 0x100

#ifdef ENABLE_YMF71X_LOG
int ymf71x_do_log = ENABLE_YMF71X_LOG;

static void
ymf71x_log(void *priv, const char *fmt, ...)
{
    if (ymf71x_do_log) {
        va_list ap;
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define ymf71x_log(fmt, ...)
#endif

static const uint8_t ymf71x_init_key[32] = { 0xB1, 0xD8, 0x6C, 0x36, 0x9B, 0x4D, 0xA6, 0xD3,   /* "YAMAHA Key" from the datasheet */
                                             0x69, 0xB4, 0x5A, 0xAD, 0xD6, 0xEB, 0x75, 0xBA,
                                             0xDD, 0xEE, 0xF7, 0x7B, 0x3D, 0x9E, 0xCF, 0x67,
                                             0x33, 0x19, 0x8C, 0x46, 0xA3, 0x51, 0xA8, 0x54 };

/* Reversed attenuation values borrowed from snd_sb.c */
/* YMF71x master volume attenuation is -30dB when all bits are 1, 0dB when all bits are 0 */
static const double ymf71x_att_2dbstep_4bits[] = {
      32767.0, 26027.0, 20674.0, 16422.0, 13044.0, 10362.0, 8230.0, 6537.0,
       5192.0,  4125.0,  3276.0,  2602.0,  2067.0,  1641.0, 1304.0,  164.0
};

/* Taken from the SoundBlaster code, not quite correct but provides the desired effect
   without causing distortion when applied to CD audio (at lower settings, highest settings
   still do this to CD audio) */
static const double ymf71x_bass_treble_3bits[] = {
    0, 0.25892541, 0.584893192, 1, 1.511886431, 2.16227766, 3, 4.011872336
};


typedef struct ymf71x_t {
    uint8_t type;

    uint16_t cur_sb_addr;
    uint16_t cur_wss_addr;
    uint16_t cur_mpu401_addr;
    uint16_t cur_opl_addr;
    uint16_t cur_ctrl_addr;

    int   cur_sb_irq;
    int   cur_sb_dma;
    int   cur_wss_enabled;
    int   cur_wss_irq;
    int   cur_wss_dma;
    int   cur_mpu401_irq;
    void *gameport;

    ad1848_t ad1848;
    mpu_t   *mpu;

    sb_t   *sb;
    uint8_t index;
    uint8_t regs[0x20];
    uint8_t max_reg;
    double  master_l;
    double  master_r;

    void                   *pnp_card;
    uint8_t                 pnp_rom[512];
    uint8_t                 key_pos : 5;
    uint8_t                 configidx;
    uint8_t                 ramwrite_enable;
    uint8_t                 ram_data[512];
    uint16_t                ram_addr;
    isapnp_device_config_t *ymf71x_pnp_config;

    void *    log;  /* New logging system */
} ymf71x_t;

static void
ymf71x_config_write(uint16_t addr, uint8_t val, void *priv)
{
    ymf71x_t *ymf71x = (ymf71x_t *) priv;

    ymf71x_log(ymf71x->log, "Config Write port = %04X, val = %02X\n", addr, val);

    if (addr == 0x279) {
        if (ymf71x->key_pos == 0x00)
            ymf71x->key_pos = 0x01;
        /* Check for YAMAHA Key */
        if (val == ymf71x_init_key[ymf71x->key_pos]) {
            ymf71x->key_pos++;
            if (!ymf71x->key_pos) {
                ymf71x_log(ymf71x->log, "YMF71x: Config unlocked\n");
                /* Force CSN to 0x81 */
                isapnp_set_csn(ymf71x->pnp_card, 0x81);
                /* Set card to SLEEP state */
                isapnp_enable_card(ymf71x->pnp_card, ISAPNP_CARD_FORCE_SLEEP);
            }
        }
        ymf71x->configidx = val;
    }
    if (addr == 0xA79) {
        if ((ymf71x->configidx == 0x21) && (val & 0x01)) {
            ymf71x_log(ymf71x->log, "Enable internal RAM write\n");
            ymf71x->ramwrite_enable = 1;
        }
        if ((ymf71x->configidx == 0x21) && (val == 0x00)) {
            ymf71x_log(ymf71x->log, "Disable internal RAM write\n");
            isapnp_update_card_rom(ymf71x->pnp_card, &ymf71x->ram_data[0], 512);
        }
        if ((ymf71x->configidx == 0x20) && (ymf71x->ramwrite_enable == 0x01)) {
            ymf71x_log(ymf71x->log, "Write to internal RAM addr %04X, val %02X\n", ymf71x->ram_addr, val);
            ymf71x->ram_data[ymf71x->ram_addr++] = val;
        }
    }
}

static uint8_t
ymf71x_wss_read(uint16_t addr, void *priv)
{
    ymf71x_t *ymf71x = (ymf71x_t *) priv;
    uint8_t   ret = 0x00;
    uint8_t   port = addr - ymf71x->cur_wss_addr;

    switch (port) {
        case 0:
            switch (ymf71x->cur_wss_irq) {
                case 7:
                    ret |= 0x08;
                    break;
                case 9:
                    ret |= 0x10;
                    break;
                case 10:
                    ret |= 0x18;
                    break;
                case 11:
                    ret |= 0x20;
                    break;
                default:
                    break;
            }
            switch (ymf71x->cur_wss_dma) {
                case 0:
                    ret |= 0x01;
                    break;
                case 1:
                    ret |= 0x02;
                    break;
                case 3:
                    ret |= 0x03;
                    break;
                default:
                    break;
            }
            break;
        case 3:
            ret = 0x04;
            break;
        default:
            ret = 0x04;
            break;
    }
    ymf71x_log(ymf71x->log, "WSS Read: addr = %02X, ret = %02X\n", addr, ret);
    return ret;

}

static void
ymf71x_wss_write(uint16_t addr, uint8_t val, void *priv)
{
    ymf71x_t *ymf71x = (ymf71x_t *) priv;
    uint8_t   port   = addr - ymf71x->cur_wss_addr;

    ymf71x_log(ymf71x->log, "WSS Write: addr = %02X, val = %02X\n", addr, val);
    switch (port) {
        case 0:
            break;
        default:
            break;
    }
}

static void
ymf71x_update_mastervol(void *priv)
{
    ymf71x_t *ymf71x = (ymf71x_t *) priv;
    /* Master volume attenuation */
    if (ymf71x->regs[0x07] & 0x80)
        ymf71x->master_l = 0;
    else
        ymf71x->master_l = ymf71x_att_2dbstep_4bits[ymf71x->regs[0x07] & 0x0F] / 32767.0;

    if (ymf71x->regs[0x08] & 0x80)
        ymf71x->master_r = 0;
    else
        ymf71x->master_r = ymf71x_att_2dbstep_4bits[ymf71x->regs[0x08] & 0x0F] / 32767.0;
}

static void
ymf71x_reg_write(uint16_t addr, uint8_t val, void *priv)
{
    ymf71x_t *ymf71x = (ymf71x_t *) priv;
    uint8_t   port   = addr - ymf71x->cur_ctrl_addr;

    switch (port) {
        case 0x00: /* Index */
            ymf71x->index = val;
            break;
        case 0x01: /* Data */
            if (ymf71x->index <= ymf71x->max_reg) {
                switch (ymf71x->index) {
                    case 0x01: /* Power Management */
                        ymf71x->regs[0x01] = val;
                        break;
                    case 0x02: /* System Control */
                        ymf71x->regs[0x02] = val;
                        break;
                    case 0x03: /* Interrupt Channel Config */
                        ymf71x->regs[0x03] = val;
                        break;
                    case 0x04: /* IRQ-A Status (RO) */
                        break;
                    case 0x05: /* IRQ-B Status (RO) */
                        break;
                    case 0x06: /* DMA Config */
                        ymf71x->regs[0x06] = val;
                        break;
                    case 0x07: /* Master Volume Left Channel */
                        ymf71x->regs[0x07] = val;
                        ymf71x_update_mastervol(ymf71x);
                        break;
                    case 0x08: /* Master Volume Right Channel */
                        ymf71x->regs[0x08] = val;
                        ymf71x_update_mastervol(ymf71x);
                        break;
                    case 0x09: /* Mic Volume */
                        ymf71x->regs[0x09] = val;
                        break;
                    case 0x0A: /* Miscellaneous */
                        ymf71x->regs[0x0A] = ((val & 0xf0) | ymf71x->type);
                        break;
                    case 0x0B ... 0x0E: /* WSS DMA Base Counter */
                        ymf71x->regs[ymf71x->index] = val;
                        break;
                    case 0x0F: /* WSS Interrupt Scan (SA3) */
                        ymf71x->regs[0x0F] = val;
                        break;
                    case 0x10: /* SB Internal State Scan (SA3) */
                        ymf71x->regs[0x10] = val;
                        break;
                    case 0x11: /* SB Internal State Scan (SA3) */
                        ymf71x->regs[0x11] = val;
                        break;
                    case 0x12: /* Digital Block Partial Power Down (SA3) */
                        ymf71x->regs[0x12] = val;
                        break;
                    case 0x13: /* Analog Block Partial Power Down (SA3) */
                        ymf71x->regs[0x13] = val;
                        break;
                    case 0x14: /* 3D Enhanced Control Wide (SA3) */
                        ymf71x->regs[0x14] = val;
                        break;
                    case 0x15: /* 3D Enhanced Control Bass (SA3) */
                        ymf71x->regs[0x15] = val;
                        break;
                    case 0x16: /* 3D Enhanced Control Treble (SA3) */
                        ymf71x->regs[0x16] = val;
                        break;
                    case 0x17: /* Hardware Volume Interrupt Channel Config (SA3) */
                        ymf71x->regs[0x17] = val;
                        break;
                    default:
                        break;
                }
            }
            break;
        default:
            break;
    }
    ymf71x_log(ymf71x->log, "Write: addr = %02X, val = %02X\n", addr, val);
}

static uint8_t
ymf71x_reg_read(uint16_t addr, void *priv)
{
    ymf71x_t *ymf71x = (ymf71x_t *) priv;
    uint8_t   temp   = 0xFF;
    uint8_t   port   = addr - ymf71x->cur_ctrl_addr;

    switch (port) {
        case 0x00: /* Index */
            temp = ymf71x->index;
            break;
        case 0x01: /* Data */
            if (ymf71x->index <= ymf71x->max_reg) {
                if (ymf71x->index == 0x04) { /* Read IRQ-A status reg */
                    temp = 0;
                    if (ymf71x->regs[0x03] & 0x01)
                        temp |= ((ymf71x->ad1848.regs[24] >> 4) & 0x07);
                    if (ymf71x->regs[0x03] & 0x02)
                        temp |= ((ymf71x->sb->dsp.sb_irq8) ? 8: 0);
                }
                else if (ymf71x->index == 0x05) { /* Read IRQ-B status reg */
                    temp = 0;
                    if (ymf71x->regs[0x03] & 0x10)
                        temp |= ((ymf71x->ad1848.regs[24] >> 4) & 0x07);
                    if (ymf71x->regs[0x03] & 0x20)
                        temp |= ((ymf71x->sb->dsp.sb_irq8) ? 8: 0);
                }
                else
                    temp = ymf71x->regs[ymf71x->index];
            }
            break;
        default:
            break;
    }

    ymf71x_log(ymf71x->log, "Read: addr = %02X, ret = %02X\n", addr, temp);
    return temp;
}

static void
ymf71x_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv)
{
    ymf71x_t *ymf71x = (ymf71x_t *) priv;

    ymf71x_log(ymf71x->log, "PnP Config changed\n");

    switch (ld) {
        case 0: /* WSS/OPL3/SBPro/MPU401/CTRL */
            if (ymf71x->cur_wss_addr) {
                io_removehandler(ymf71x->cur_wss_addr, 0x0004, ymf71x_wss_read, NULL, NULL, ymf71x_wss_write, NULL, NULL, ymf71x);
                io_removehandler(ymf71x->cur_wss_addr + 0x0004, 0x0004, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &ymf71x->ad1848);
                ymf71x->cur_wss_addr = 0;
                ymf71x->cur_wss_enabled = 0;
            }

            if (ymf71x->cur_opl_addr) {
                io_removehandler(ymf71x->cur_opl_addr, 4, ymf71x->sb->opl.read, NULL, NULL, ymf71x->sb->opl.write, NULL, NULL, ymf71x->sb->opl.priv);
                ymf71x->cur_opl_addr = 0;
            }

            if (ymf71x->cur_sb_addr) {
                sb_dsp_setaddr(&ymf71x->sb->dsp, 0);
                io_removehandler(ymf71x->cur_sb_addr, 4, ymf71x->sb->opl.read, NULL, NULL, ymf71x->sb->opl.write, NULL, NULL, ymf71x->sb->opl.priv);
                io_removehandler(ymf71x->cur_sb_addr + 8, 2, ymf71x->sb->opl.read, NULL, NULL, ymf71x->sb->opl.write, NULL, NULL, ymf71x->sb->opl.priv);
                io_removehandler(ymf71x->cur_sb_addr + 4, 2, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, ymf71x->sb);
                ymf71x->cur_sb_addr = 0;
            }

            if (ymf71x->cur_ctrl_addr) {
                io_removehandler(ymf71x->cur_ctrl_addr, 2, ymf71x_reg_read, NULL, NULL, ymf71x_reg_write, NULL, NULL, ymf71x);
            }

            ad1848_setirq(&ymf71x->ad1848, 0);
            sb_dsp_setirq(&ymf71x->sb->dsp, 0);

            ad1848_setdma(&ymf71x->ad1848, 0);
            sb_dsp_setdma8(&ymf71x->sb->dsp, 0);

            if (config->activate) {
                if (config->io[0].base != ISAPNP_IO_DISABLED) {
                    ymf71x->cur_sb_addr = config->io[0].base;
                    ymf71x_log(ymf71x->log, "Updating SB DSP I/O port, SB Addr = %04X\n", ymf71x->cur_sb_addr);
                    sb_dsp_setaddr(&ymf71x->sb->dsp, ymf71x->cur_sb_addr);
                    io_sethandler(ymf71x->cur_sb_addr, 4, ymf71x->sb->opl.read, NULL, NULL, ymf71x->sb->opl.write, NULL, NULL, ymf71x->sb->opl.priv);
                    io_sethandler(ymf71x->cur_sb_addr + 8, 2, ymf71x->sb->opl.read, NULL, NULL, ymf71x->sb->opl.write, NULL, NULL, ymf71x->sb->opl.priv);
                    io_sethandler(ymf71x->cur_sb_addr + 4, 2, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, ymf71x->sb);
                }

                if (config->io[1].base != ISAPNP_IO_DISABLED) {
                    ymf71x->cur_wss_addr = config->io[1].base;
                    ymf71x->cur_wss_enabled = 1;
                    ymf71x_log(ymf71x->log, "Updating WSS I/O port, WSS Addr = %04X\n", ymf71x->cur_wss_addr);
                    io_sethandler(ymf71x->cur_wss_addr, 0x0004, ymf71x_wss_read, NULL, NULL, ymf71x_wss_write, NULL, NULL, ymf71x);
                    io_sethandler(ymf71x->cur_wss_addr + 0x0004, 0x0004, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &ymf71x->ad1848);
                }

                if (config->io[2].base != ISAPNP_IO_DISABLED) {
                    ymf71x->cur_opl_addr = config->io[2].base;
                    ymf71x_log(ymf71x->log, "Updating OPL I/O port, OPL Addr = %04X\n", ymf71x->cur_opl_addr);
                    io_sethandler(ymf71x->cur_opl_addr, 4, ymf71x->sb->opl.read, NULL, NULL, ymf71x->sb->opl.write, NULL, NULL, ymf71x->sb->opl.priv);
                }

                if (config->io[3].base != ISAPNP_IO_DISABLED) {
                    ymf71x->cur_mpu401_addr = config->io[3].base;
                    ymf71x_log(ymf71x->log, "Updating MPU401 I/O port, MPU Addr = %04X\n", ymf71x->cur_mpu401_addr);
                    mpu401_change_addr(ymf71x->mpu, ymf71x->cur_mpu401_addr);
                }

                if (config->io[4].base != ISAPNP_IO_DISABLED) {
                    ymf71x->cur_ctrl_addr = config->io[4].base;
                    ymf71x_log(ymf71x->log, "Updating CTRL I/O port, CTRL Addr = %04X\n", ymf71x->cur_ctrl_addr);
                    io_sethandler(ymf71x->cur_ctrl_addr, 2, ymf71x_reg_read, NULL, NULL, ymf71x_reg_write, NULL, NULL, ymf71x);
                }

                if (config->irq[0].irq != ISAPNP_IRQ_DISABLED) {
                    if (ymf71x->regs[0x03] & 0x01) {
                        ad1848_setirq(&ymf71x->ad1848, config->irq[0].irq);
                        ymf71x->cur_wss_irq = config->irq[0].irq;
                        ymf71x_log(ymf71x->log, "Setting WSS IRQ to IRQ-A (%04X)\n", ymf71x->cur_wss_irq);
                    }
                    if (ymf71x->regs[0x03] & 0x02) {
                        sb_dsp_setirq(&ymf71x->sb->dsp, config->irq[0].irq);
                        ymf71x->cur_sb_irq = config->irq[0].irq;
                        ymf71x_log(ymf71x->log, "Setting SB IRQ to IRQ-A (%04X)\n", ymf71x->cur_sb_irq);
                    }
                    if (ymf71x->regs[0x03] & 0x04) {
                        mpu401_setirq(ymf71x->mpu, config->irq[0].irq);
                        ymf71x->cur_mpu401_irq = config->irq[0].irq;
                        ymf71x_log(ymf71x->log, "Setting MPU401 IRQ to IRQ-A (%04X)\n", ymf71x->cur_mpu401_irq);
                    }
                }

                if (config->irq[1].irq != ISAPNP_IRQ_DISABLED) {
                    if (ymf71x->regs[0x03] & 0x10) {
                        ad1848_setirq(&ymf71x->ad1848, config->irq[1].irq);
                        ymf71x->cur_wss_irq = config->irq[1].irq;
                        ymf71x_log(ymf71x->log, "Setting WSS IRQ to IRQ-B (%04X)\n", ymf71x->cur_wss_irq);
                    }
                    if (ymf71x->regs[0x03] & 0x20) {
                        sb_dsp_setirq(&ymf71x->sb->dsp, config->irq[1].irq);
                        ymf71x->cur_sb_irq = config->irq[1].irq;
                        ymf71x_log(ymf71x->log, "Setting SB IRQ to IRQ-B (%04X)\n", ymf71x->cur_sb_irq);
                    }
                    if (ymf71x->regs[0x03] & 0x40) {
                        mpu401_setirq(ymf71x->mpu, config->irq[1].irq);
                        ymf71x->cur_mpu401_irq = config->irq[1].irq;
                        ymf71x_log(ymf71x->log, "Setting MPU401 IRQ to IRQ-B (%04X)\n", ymf71x->cur_mpu401_irq);
                    }
                }

                if (config->dma[0].dma != ISAPNP_DMA_DISABLED) {
                    if (ymf71x->regs[0x06] & 0x01) {
                        ad1848_setdma(&ymf71x->ad1848, config->dma[0].dma);
                        ymf71x->cur_wss_dma = config->dma[0].dma;
                        ymf71x_log(ymf71x->log, "Setting WSS DMA to DMA-A (%04X)\n", ymf71x->cur_wss_dma);
                    }
                    if (ymf71x->regs[0x06] & 0x04) {
                        sb_dsp_setdma8(&ymf71x->sb->dsp, config->dma[0].dma);
                        ymf71x->cur_sb_dma = config->dma[0].dma;
                        ymf71x_log(ymf71x->log, "Setting SB DMA to DMA-A (%04X)\n", ymf71x->cur_sb_dma);
                    }
                }

                if (config->dma[1].dma != ISAPNP_DMA_DISABLED) {
                    if (ymf71x->regs[0x06] & 0x10) {
                        ad1848_setdma(&ymf71x->ad1848, config->dma[1].dma);
                        ymf71x->cur_wss_dma = config->dma[1].dma;
                        ymf71x_log(ymf71x->log, "Setting WSS DMA to DMA-B (%04X)\n", ymf71x->cur_wss_dma);
                    }
                    if (ymf71x->regs[0x06] & 0x40) {
                        sb_dsp_setdma8(&ymf71x->sb->dsp, config->dma[1].dma);
                        ymf71x->cur_sb_dma = config->dma[1].dma;
                        ymf71x_log(ymf71x->log, "Setting SB DMA to DMA-B (%04X)\n", ymf71x->cur_sb_dma);
                    }
                }
            }
            break;

        case 1: /* Game Port */
            if (ymf71x->gameport)
                gameport_remap(ymf71x->gameport, (config->activate && (config->io[0].base != ISAPNP_IO_DISABLED)) ? config->io[0].base : 0);
            break;
        default:
            break;
    }
}

void
ymf71x_filter_cd_audio(int channel, double *buffer, void *priv)
{
    ymf71x_t    *ymf71x = (ymf71x_t *) priv;
    const double cd_vol = channel ? ymf71x->ad1848.cd_vol_r : ymf71x->ad1848.cd_vol_l;
    double       master = channel ? ymf71x->master_r : ymf71x->master_l;
    double       c      = ((*buffer  * cd_vol / 3.0) * master) / 65536.0;
    double       bass_treble;

    if ((ymf71x->regs[0x15] & 0x07) != 0x00) {
        bass_treble = ymf71x_bass_treble_3bits[(ymf71x->regs[0x15] & 0x07)];

        c += (low_iir(2, 0, c) * bass_treble);
    }

    if (((ymf71x->regs[0x15] >> 4) & 0x07) != 0x00) {
        bass_treble = ymf71x_bass_treble_3bits[((ymf71x->regs[0x15] >> 4) & 0x07)];

        c += (low_iir(2, 1, c) * bass_treble);
    }

    if ((ymf71x->regs[0x16] & 0x07) != 0x00) {
        bass_treble = ymf71x_bass_treble_3bits[ymf71x->regs[0x16] & 0x07];

        c += (high_iir(2, 0, c) * bass_treble);
    }

    if (((ymf71x->regs[0x16] >> 4) & 0x07) != 0x00) {
        bass_treble = ymf71x_bass_treble_3bits[(ymf71x->regs[0x16] >> 4) & 0x07];

        c += (high_iir(2, 1, c) * bass_treble);
    }

    *buffer = c;
}

static void
ymf71x_filter_opl(void *priv, double *out_l, double *out_r)
{
    ymf71x_t *ymf71x = (ymf71x_t *) priv;
    double    bass_treble;

    /* Don't play audio if the FM DAC or OPL3 digital sections are powered down */
    if ( (!(ymf71x->regs[0x01] & 0x23)) && (!(ymf71x->regs[0x12] & 0x10)) && (!(ymf71x->regs[0x13] & 0x10)) ) {
        if ((ymf71x->regs[0x15] & 0x07) != 0x00) {
            bass_treble = ymf71x_bass_treble_3bits[(ymf71x->regs[0x15] & 0x07)];

            *out_l += (low_iir(1, 0, *out_l) * bass_treble);
        }

        if (((ymf71x->regs[0x15] >> 4) & 0x07) != 0x00) {
            bass_treble = ymf71x_bass_treble_3bits[((ymf71x->regs[0x15] >> 4) & 0x07)];

            *out_r += (low_iir(1, 1, *out_r) * bass_treble);
        }

        if ((ymf71x->regs[0x16] & 0x07) != 0x00) {
            bass_treble = ymf71x_bass_treble_3bits[ymf71x->regs[0x16] & 0x07];

            *out_l += (high_iir(1, 0, *out_l) * bass_treble);
        }

        if (((ymf71x->regs[0x16] >> 4) & 0x07) != 0x00) {
            bass_treble = ymf71x_bass_treble_3bits[(ymf71x->regs[0x16] >> 4) & 0x07];

            *out_r += (high_iir(1, 1, *out_r) * bass_treble);
        }

        *out_l *= ymf71x->master_l;
        *out_r *= ymf71x->master_r;

        if (ymf71x->cur_wss_enabled) {
            ad1848_filter_channel((void *) &ymf71x->ad1848, AD1848_AUX2, out_l, out_r);
        }
    }
}

static void
ymf71x_get_buffer(int32_t *buffer, int len, void *priv)
{
    ymf71x_t *ymf71x = (ymf71x_t *) priv;

    /* wss part */

    /* Don't play audio if the WSS Playback analog or digital sections are powered down */
    if ( (!(ymf71x->regs[0x01] & 0x23)) && (!(ymf71x->regs[0x12] & 0x04)) && (!(ymf71x->regs[0x13] & 0x04)) ) {
        ad1848_update(&ymf71x->ad1848);
        for (int c = 0; c < len * 2; c += 2) {
            double out_l = 0.0;
            double out_r = 0.0;
            double bass_treble;

            out_l += (ymf71x->ad1848.buffer[c] * ymf71x->master_l);
            out_r += (ymf71x->ad1848.buffer[c +1] * ymf71x->master_r);

            if ((ymf71x->regs[0x15] & 0x07) != 0x00) {
                bass_treble = ymf71x_bass_treble_3bits[(ymf71x->regs[0x15] & 0x07)];

                out_l += (low_iir(0, 0, out_l) * bass_treble);
            }

            if (((ymf71x->regs[0x15] >> 4) & 0x07) != 0x00) {
                bass_treble = ymf71x_bass_treble_3bits[((ymf71x->regs[0x15] >> 4) & 0x07)];

                out_r += (low_iir(0, 1, out_r) * bass_treble);
            }

            if ((ymf71x->regs[0x16] & 0x07) != 0x00) {
                bass_treble = ymf71x_bass_treble_3bits[ymf71x->regs[0x16] & 0x07];

                out_l += (high_iir(0, 0, out_l) * bass_treble);
            }

            if (((ymf71x->regs[0x16] >> 4) & 0x07) != 0x00) {
                bass_treble = ymf71x_bass_treble_3bits[(ymf71x->regs[0x16] >> 4) & 0x07];

                out_r += (high_iir(0, 1, out_r) * bass_treble);
            }

            out_l *= ymf71x->master_l;
            out_r *= ymf71x->master_r;

            buffer[c] += (int32_t) out_l;
            buffer[c + 1] += (int32_t) out_r;
        }

        ymf71x->ad1848.pos = 0;
    }

    /* sbprov2 part */
    /* Don't play audio if the SB Compatibility analog or digital sections are powered down */
    if ( (!(ymf71x->regs[0x01] & 0x23)) && (!(ymf71x->regs[0x12] & 0x02)) && (!(ymf71x->regs[0x13] & 0x02)) ) {
        sb_get_buffer_sbpro(buffer, len, ymf71x->sb);
    }
}

static void *
ymf71x_init(const device_t *info)
{
    ymf71x_t *ymf71x = calloc(1, sizeof(ymf71x_t));

    ymf71x->type = (info->local & 0x0F);

    ymf71x->cur_wss_addr       = 0;
    ymf71x->cur_sb_addr        = 0;
    ymf71x->cur_sb_irq         = 5;
    ymf71x->cur_wss_enabled    = 0;
    ymf71x->cur_sb_dma         = 1;
    ymf71x->cur_mpu401_irq     = 5;
    ymf71x->cur_mpu401_addr    = 0;
    ymf71x->cur_wss_dma        = 0;
    ymf71x->cur_wss_irq        = 11;

    ymf71x->regs[0x00] = 0xFF;
    ymf71x->regs[0x01] = 0x00;
    ymf71x->regs[0x02] = 0x00;
    ymf71x->regs[0x03] = 0x69; /* IRQ-A = WSS + OPL3, IRQ-B = SB+MPU401 */
    ymf71x->regs[0x04] = 0x00;
    ymf71x->regs[0x05] = 0x00;
    ymf71x->regs[0x06] = 0x61; /* DMA-A = WSS Playback, DMA-B = WSS Capture + SBPro */
    ymf71x->regs[0x07] = 0x07;
    ymf71x->regs[0x08] = 0x07;
    ymf71x->regs[0x09] = 0x88;
    ymf71x->regs[0x0A] = (0x80 | ymf71x->type);
    ymf71x->regs[0x0B] = 0xFF;
    ymf71x->regs[0x0C] = 0xFF;
    ymf71x->regs[0x0D] = 0xFF;
    ymf71x->regs[0x0E] = 0xFF;
    ymf71x->regs[0x0F] = 0x00;
    ymf71x->regs[0x10] = 0x00;
    ymf71x->regs[0x11] = 0x00;
    ymf71x->regs[0x12] = 0x00;
    ymf71x->regs[0x13] = 0x00;
    ymf71x->regs[0x14] = 0x00;
    ymf71x->regs[0x15] = 0x00;
    ymf71x->regs[0x16] = 0x00;
    ymf71x->regs[0x17] = 0x00;

    if (ymf71x->type == 0x02)
        ymf71x->max_reg = 0x17;
    else
        ymf71x->max_reg = 0x0E;

    ymf71x->log = log_open("YMF71x");

    ymf71x->gameport = gameport_add(&gameport_pnp_device);

    ad1848_init(&ymf71x->ad1848, AD1848_TYPE_CS4231);

    ymf71x->sb              = calloc(1, sizeof(sb_t));
    ymf71x->sb->opl_enabled = 1;

    sb_dsp_set_real_opl(&ymf71x->sb->dsp, 1);
    sb_dsp_init(&ymf71x->sb->dsp, SBPRO_DSP_302, SB_SUBTYPE_DEFAULT, ymf71x);
    sb_ct1345_mixer_reset(ymf71x->sb);

    ymf71x->sb->opl_mixer = ymf71x;
    ymf71x->sb->opl_mix   = ymf71x_filter_opl;

    fm_driver_get(FM_YMF289B, &ymf71x->sb->opl);

    sound_add_handler(ymf71x_get_buffer, ymf71x);
    music_add_handler(sb_get_music_buffer_sbpro, ymf71x->sb);
    ad1848_set_cd_audio_channel(&ymf71x->ad1848, AD1848_AUX1);
    sound_set_cd_audio_filter(NULL, NULL); /* Seems to be necessary for the filter below to apply */
    sound_set_cd_audio_filter(ymf71x_filter_cd_audio, ymf71x);

    ymf71x->mpu = (mpu_t *) calloc(1, sizeof(mpu_t));
    mpu401_init(ymf71x->mpu, ymf71x->cur_mpu401_addr, ymf71x->cur_mpu401_irq, M_UART, device_get_config_int("receive_input401"));

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &ymf71x->sb->dsp);

    if (!(info->local & YMF71X_NO_EEPROM)) {
        const char *pnp_rom_file = NULL;
        uint16_t    pnp_rom_len  = 512;
        switch (info->local) {
            case 0x01:
                pnp_rom_file = PNP_ROM_YMF718;
                break;

            case 0x02:
                pnp_rom_file = PNP_ROM_YMF719;
                break;

            default:
                break;
        }

        uint8_t *pnp_rom = NULL;
        if (pnp_rom_file) {
            FILE *fp = rom_fopen(pnp_rom_file, "rb");
            if (fp) {
                if (fread(ymf71x->pnp_rom, 1, pnp_rom_len, fp) == pnp_rom_len)
                    pnp_rom = ymf71x->pnp_rom;
                fclose(fp);
            }
        }
        ymf71x->pnp_card = isapnp_add_card(pnp_rom, sizeof(ymf71x->pnp_rom), ymf71x_pnp_config_changed,
                                           NULL, NULL, NULL, ymf71x);
    }
    else
        ymf71x->pnp_card = isapnp_add_card(NULL, 0, ymf71x_pnp_config_changed, NULL, NULL, NULL, ymf71x);

    io_sethandler(0x0279, 0x0001, NULL, NULL, NULL, ymf71x_config_write, NULL, NULL, ymf71x);
    io_sethandler(0x0A79, 0x0001, NULL, NULL, NULL, ymf71x_config_write, NULL, NULL, ymf71x);

    ymf71x_update_mastervol(ymf71x);

    return ymf71x;
}

static void
ymf71x_close(void *priv)
{
    ymf71x_t *ymf71x = (ymf71x_t *) priv;

    if (ymf71x->log != NULL) {
        log_close(ymf71x->log);
        ymf71x->log = NULL;
    }

    sb_close(ymf71x->sb);
    free(ymf71x->mpu);
    free(priv);
}

static int
ymf718_available(void)
{
    return rom_present(PNP_ROM_YMF718);
}

static int
ymf719_available(void)
{
    return rom_present(PNP_ROM_YMF719);
}

static void
ymf71x_speed_changed(void *priv)
{
    ymf71x_t *ymf71x = (ymf71x_t *) priv;

    ad1848_speed_changed(&ymf71x->ad1848);
    sb_speed_changed(ymf71x->sb);
}

static const device_config_t ymf71x_config[] = {
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

const device_t ymf715_onboard_device = {
    .name          = "Yamaha YMF715 (OPL3-SA3) (On-Board)",
    .internal_name = "ymf715_onboard",
    .flags         = DEVICE_ISA16,
    .local         = 0x102,
    .init          = ymf71x_init,
    .close         = ymf71x_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = ymf71x_speed_changed,
    .force_redraw  = NULL,
    .config        = ymf71x_config
};

const device_t ymf718_device = {
    .name          = "Yamaha YMF718 (OPL3-SA2)",
    .internal_name = "ymf718",
    .flags         = DEVICE_ISA16,
    .local         = 0x01,
    .init          = ymf71x_init,
    .close         = ymf71x_close,
    .reset         = NULL,
    .available     = ymf718_available,
    .speed_changed = ymf71x_speed_changed,
    .force_redraw  = NULL,
    .config        = ymf71x_config
};

const device_t ymf719_device = {
    .name          = "Yamaha YMF719 (OPL3-SA3)",
    .internal_name = "ymf719",
    .flags         = DEVICE_ISA16,
    .local         = 0x02,
    .init          = ymf71x_init,
    .close         = ymf71x_close,
    .reset         = NULL,
    .available     = ymf719_available,
    .speed_changed = ymf71x_speed_changed,
    .force_redraw  = NULL,
    .config        = ymf71x_config
};
