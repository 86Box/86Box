/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Aztech AZT2320 PnP audio controller emulation.
 *
 * Authors: Cacodemon345
 *          win2kgamer
 *
 *          Copyright 2022 Cacodemon345.
 *          Copyright 2025-2026 win2kgamer
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
#include <86box/nvr.h>
#include <86box/pic.h>
#include <86box/sound.h>
#include <86box/gameport.h>
#include <86box/snd_ad1848.h>
#include <86box/snd_azt2320.h>
#include <86box/snd_sb.h>
#include <86box/plat_unused.h>
#include <86box/log.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/isapnp.h>

#define PNP_ROM_AZT2320 "roms/sound/azt2320/azt2320.bin"

#ifdef ENABLE_AZT2320_LOG
int azt2320_do_log = ENABLE_AZT2320_LOG;

static void
azt2320_log(void *priv, const char *fmt, ...)
{
    if (azt2320_do_log) {
        va_list ap;
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define azt2320_log(fmt, ...)
#endif

typedef struct azt2320_t {
    uint16_t cur_addr;
    uint16_t cur_wss_addr;
    uint16_t cur_mpu401_addr;
    uint16_t cur_opl_addr;

    int cur_irq, cur_dma;
    int cur_wss_enabled;
    int cur_wss_irq;
    int cur_wss_dma;
    int cur_mpu401_irq;
    int cur_mpu401_enabled;
    int gameport_enabled;
    void *gameport;

    uint8_t cur_mode;

    ad1848_t ad1848;
    mpu_t   *mpu;

    sb_t *sb;

    void                   *pnp_card;
    uint8_t                pnp_rom[378];
    isapnp_device_config_t *azt2320_pnp_config;

    void * log; /* New logging system */
} azt2320_t;

static void
azt2320_filter_opl(void *priv, double *out_l, double *out_r)
{
    azt2320_t *azt2320 = (azt2320_t *) priv;

    ad1848_filter_channel((void *) &azt2320->ad1848, AD1848_AUX2, out_l, out_r);
}

void
azt2320_enable_wss(uint8_t enable, void *priv)
{
    azt2320_t *azt2320 = (azt2320_t *) priv;

    sound_set_cd_audio_filter(NULL, NULL);

    if (enable) {
        azt2320->cur_mode = 1;
        sound_set_cd_audio_filter(ad1848_filter_cd_audio, &azt2320->ad1848);
        azt2320->sb->opl_mixer = azt2320;
        azt2320->sb->opl_mix   = azt2320_filter_opl;
    }
    else {
        azt2320->cur_mode = 0;
        sound_set_cd_audio_filter(sbpro_filter_cd_audio, azt2320->sb);
        azt2320->sb->opl_mixer = NULL;
        azt2320->sb->opl_mix   = NULL;
    }
}

static void
azt2320_get_buffer(int32_t *buffer, int len, void *priv)
{
    azt2320_t *azt2320 = (azt2320_t *) priv;

    /* wss part */
    ad1848_update(&azt2320->ad1848);
    for (int c = 0; c < len * 2; c++)
        buffer[c] += (azt2320->ad1848.buffer[c] / 2);

    azt2320->ad1848.pos = 0;

    /* sbprov2 part */
    sb_get_buffer_sbpro(buffer, len, azt2320->sb);
}

static void
azt2320_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv)
{
    azt2320_t *azt2320 = (azt2320_t *) priv;

    azt2320_log(azt2320->log, "PnP Config changed\n");

    switch (ld) {
        case 0: /* IDE CD-ROM, disabled on the current PnP dump */
            break;
        case 1: /* WSS/OPL3/SBPro */
            if (azt2320->cur_wss_addr) {
                io_removehandler(azt2320->cur_wss_addr, 0x0004, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &azt2320->ad1848);
                azt2320->cur_wss_addr = 0;
                azt2320->cur_wss_enabled = 0;
                azt2320_log(azt2320->log, "Removed WSS I/O handler\n");
            }

            if (azt2320->cur_opl_addr) {
                io_removehandler(azt2320->cur_opl_addr, 0x0004, azt2320->sb->opl.read, NULL, NULL, azt2320->sb->opl.write, NULL, NULL, azt2320->sb->opl.priv);
                azt2320->cur_opl_addr = 0;
                azt2320_log(azt2320->log, "Removed OPL I/O handler\n");
            }

            if (azt2320->cur_addr) {
                sb_dsp_setaddr(&azt2320->sb->dsp, 0);
                io_removehandler(azt2320->cur_addr + 4, 0x0002, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, azt2320->sb);
                io_removehandler(azt2320->cur_addr + 0, 0x0004, azt2320->sb->opl.read, NULL, NULL, azt2320->sb->opl.write, NULL, NULL, azt2320->sb->opl.priv);
                io_removehandler(azt2320->cur_addr + 8, 0x0002, azt2320->sb->opl.read, NULL, NULL, azt2320->sb->opl.write, NULL, NULL, azt2320->sb->opl.priv);
                azt2320->cur_addr = 0;
                azt2320_log(azt2320->log, "Removed SB DSP I/O handler\n");
            }

            ad1848_setirq(&azt2320->ad1848, 0);
            sb_dsp_setirq(&azt2320->sb->dsp, 0);

            ad1848_setdma(&azt2320->ad1848, 0);
            sb_dsp_setdma8(&azt2320->sb->dsp, 0);

            if (config->activate) {
                if (config->io[0].base != ISAPNP_IO_DISABLED) {
                    azt2320->cur_addr = config->io[0].base;
                    azt2320_log(azt2320->log, "Updating SB DSP I/O port, SB DSP addr = %04X\n", azt2320->cur_addr);
                    sb_dsp_setaddr(&azt2320->sb->dsp, azt2320->cur_addr);
                    io_sethandler(azt2320->cur_addr + 4, 0x0002, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, azt2320->sb);
                    io_sethandler(azt2320->cur_addr + 0, 0x0004, azt2320->sb->opl.read, NULL, NULL, azt2320->sb->opl.write, NULL, NULL, azt2320->sb->opl.priv);
                    io_sethandler(azt2320->cur_addr + 8, 0x0002, azt2320->sb->opl.read, NULL, NULL, azt2320->sb->opl.write, NULL, NULL, azt2320->sb->opl.priv);
                }
                if (config->io[1].base != ISAPNP_IO_DISABLED) {
                    azt2320->cur_opl_addr = config->io[1].base;
                    azt2320_log(azt2320->log, "Updating OPL I/O port, OPL addr = %04X\n", azt2320->cur_opl_addr);
                    io_sethandler(azt2320->cur_opl_addr, 0x0004, azt2320->sb->opl.read, NULL, NULL, azt2320->sb->opl.write, NULL, NULL, azt2320->sb->opl.priv);
                }
                if (config->io[2].base != ISAPNP_IO_DISABLED) {
                    azt2320->cur_wss_addr = config->io[2].base;
                    azt2320_log(azt2320->log, "Updating WSS I/O port, WSS addr = %04X\n", azt2320->cur_wss_addr);
                    io_sethandler(azt2320->cur_wss_addr, 0x0004, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &azt2320->ad1848);
                }
                if (config->irq[0].irq != ISAPNP_IRQ_DISABLED) {
                    azt2320->cur_irq = config->irq[0].irq;
                    azt2320->cur_wss_irq = config->irq[0].irq;
                    sb_dsp_setirq(&azt2320->sb->dsp, azt2320->cur_irq);
                    ad1848_setirq(&azt2320->ad1848, azt2320->cur_wss_irq);
                    azt2320_log(azt2320->log, "Updated WSS/SB IRQ to %04X\n", azt2320->cur_irq);
                }
                if (config->dma[0].dma != ISAPNP_DMA_DISABLED) {
                    azt2320->cur_dma = config->dma[0].dma;
                    azt2320->cur_wss_dma = config->dma[0].dma;
                    sb_dsp_setdma8(&azt2320->sb->dsp, azt2320->cur_dma);
                    ad1848_setdma(&azt2320->ad1848, azt2320->cur_wss_dma);
                    azt2320_log(azt2320->log, "Updated WSS Playback/SB DMA to %04X\n", azt2320->cur_dma);
                }
            }
            break;
        case 2: /* MPU401 */
            if (config->activate) {
                if (config->io[0].base != ISAPNP_IO_DISABLED) {
                    azt2320->cur_mpu401_addr = config->io[0].base;
                    azt2320_log(azt2320->log, "Updating MPU401 I/O port, MPU401 addr = %04X\n", azt2320->cur_mpu401_addr);
                    mpu401_change_addr(azt2320->mpu, azt2320->cur_mpu401_addr);
                }
                if (config->irq[0].irq != ISAPNP_IRQ_DISABLED) {
                    azt2320->cur_mpu401_irq = config->irq[0].irq;
                    mpu401_setirq(azt2320->mpu, azt2320->cur_mpu401_irq);
                    azt2320_log(azt2320->log, "Updated MPU401 IRQ to %04X\n", azt2320->cur_mpu401_irq);
                }
            }
            break;
        case 3: /* Gameport */
            gameport_remap(azt2320->gameport, (config->activate && (config->io[0].base != ISAPNP_IO_DISABLED)) ? config->io[0].base : 0);
            break;
        default:
            break;
    }
}

static void *
azt2320_init(const device_t *info)
{
    azt2320_t *azt2320 = calloc(1, sizeof(azt2320_t));

    azt2320->log = log_open("AZT2320");

    /* Sane defaults for AZT2320 config */
    azt2320->cur_addr = 0x220;
    azt2320->cur_irq = 5;
    azt2320->cur_dma = 1;
    azt2320->cur_wss_addr = 0x534;
    azt2320->cur_wss_enabled = 1;
    azt2320->gameport_enabled = 1;
    azt2320->cur_mpu401_addr = 0x330;
    azt2320->cur_mpu401_enabled = 1;
    azt2320->cur_mpu401_irq = 9;
    azt2320->cur_wss_irq = 10;
    azt2320->cur_wss_dma = 1;
    azt2320->cur_mode = 0;
    azt2320->cur_opl_addr = 0x388;

    /* Init WSS */
    ad1848_init(&azt2320->ad1848, AD1848_TYPE_CS4231);
    ad1848_set_cd_audio_channel(&azt2320->ad1848, AD1848_LINE_IN);
    ad1848_setirq(&azt2320->ad1848, azt2320->cur_wss_irq);
    ad1848_setdma(&azt2320->ad1848, azt2320->cur_wss_dma);
    io_sethandler(azt2320->cur_wss_addr, 0x0004, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &azt2320->ad1848);

    /* Init SBProV2 */
    azt2320->sb = calloc(1, sizeof(sb_t));

    azt2320->sb->opl_enabled = device_get_config_int("opl");

    if (azt2320->sb->opl_enabled)
        fm_driver_get(FM_YMF262, &azt2320->sb->opl);

    sb_dsp_set_real_opl(&azt2320->sb->dsp, 1);
    sb_dsp_init(&azt2320->sb->dsp, SBPRO_DSP_302, SB_SUBTYPE_CLONE_AZT2320_0X13, azt2320);
    sb_dsp_setaddr(&azt2320->sb->dsp, azt2320->cur_addr);
    sb_dsp_setirq(&azt2320->sb->dsp, azt2320->cur_irq);
    sb_dsp_setdma8(&azt2320->sb->dsp, azt2320->cur_dma);
    sb_ct1345_mixer_reset(azt2320->sb);
    /* DSP I/O handler is activated in sb_dsp_setaddr */
    if (azt2320->sb->opl_enabled) {
        io_sethandler(azt2320->cur_addr + 0, 0x0004, azt2320->sb->opl.read, NULL, NULL, azt2320->sb->opl.write, NULL, NULL, azt2320->sb->opl.priv);
        io_sethandler(azt2320->cur_addr + 8, 0x0002, azt2320->sb->opl.read, NULL, NULL, azt2320->sb->opl.write, NULL, NULL, azt2320->sb->opl.priv);
        io_sethandler(0x0388, 0x0004, azt2320->sb->opl.read, NULL, NULL, azt2320->sb->opl.write, NULL, NULL, azt2320->sb->opl.priv);
    }

    io_sethandler(azt2320->cur_addr + 4, 0x0002, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, azt2320->sb);

    sound_add_handler(azt2320_get_buffer, azt2320);

    if (azt2320->sb->opl_enabled) {
        music_add_handler(sb_get_music_buffer_sbpro, azt2320->sb);
    }

    sound_set_cd_audio_filter(NULL, NULL);
    sound_set_cd_audio_filter(sbpro_filter_cd_audio, azt2320->sb);


    if (azt2320->cur_mpu401_enabled) {
        azt2320->mpu = (mpu_t *) calloc(1, sizeof(mpu_t));
        mpu401_init(azt2320->mpu, azt2320->cur_mpu401_addr, azt2320->cur_mpu401_irq, M_UART, device_get_config_int("receive_input401"));
    } else
        azt2320->mpu = NULL;

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &azt2320->sb->dsp);

    azt2320->gameport = gameport_add(&gameport_pnp_device);

    const char *pnp_rom_file = NULL;
    uint16_t    pnp_rom_len  = 378;
    pnp_rom_file = PNP_ROM_AZT2320;

    uint8_t *pnp_rom = NULL;
    if (pnp_rom_file) {
        FILE *fp = rom_fopen(pnp_rom_file, "rb");
        if (fp) {
            if (fread(azt2320->pnp_rom, 1, pnp_rom_len, fp) == pnp_rom_len)
                pnp_rom = azt2320->pnp_rom;
            fclose(fp);
        }
    }
    azt2320->pnp_card = isapnp_add_card(pnp_rom, sizeof(azt2320->pnp_rom), azt2320_pnp_config_changed,
                                         NULL, NULL, NULL, azt2320);
    gameport_remap(azt2320->gameport, 0x00);

    /* Card possibly inits the internal WSS codec in MODE2 at power-on */
    /* Windows 98 and 2000 WDM drivers expect to be able to write to MODE2 registers without setting I12 bit 6 */
    azt2320->ad1848.regs[12] |= 0x40;
    /* WDM drivers also expect reading port WSSBase+1 without writing an index value to return 0xFF */
    azt2320->ad1848.regs[0] = 0xff;

    return azt2320;
}

static void
azt2320_close(void *priv)
{
    azt2320_t *azt2320 = (azt2320_t *) priv;

    sb_close(azt2320->sb);

    free(azt2320->mpu);

    if (azt2320->log != NULL) {
        log_close(azt2320->log);
        azt2320->log = NULL;
    }

    free(azt2320);
}

static void
azt2320_speed_changed(void *priv)
{
    azt2320_t *azt2320 = (azt2320_t *) priv;

    ad1848_speed_changed(&azt2320->ad1848);
    sb_speed_changed(azt2320->sb);
}

static int
azt2320_available(void)
{
    return rom_present(PNP_ROM_AZT2320);
}

static const device_config_t azt2320_config[] = {
  // clang-format off
    {
        .name           = "opl",
        .description    = "Enable OPL",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
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

const device_t azt2320_device = {
    .name          = "HP Multimedia Pro 16V-A",
    .internal_name = "azt2320",
    .flags         = DEVICE_ISA16,
    .local         = SB_SUBTYPE_CLONE_AZT2320_0X13,
    .init          = azt2320_init,
    .close         = azt2320_close,
    .reset         = NULL,
    .available     = azt2320_available,
    .speed_changed = azt2320_speed_changed,
    .force_redraw  = NULL,
    .alias         = "AZT2320",
    .config        = azt2320_config
};
