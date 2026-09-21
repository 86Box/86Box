/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          Media Vision Pro Sonic 16 / Jazz16 (MVD1216B) emulation.
 *
 * Authors: mw308
 *
 *          Copyright 2026 mw308
 *
 * MVD1216B configuration/detection, SB-compatible DSP 3.01,
 * Yamaha YMF262 OPL3, MPU-401 UART and game port.  Uses parts of the existing SB emulation.
 * The Pro Sonic 16 comes with a Panasonic/MKE CD interface which is already emulated in 86box.
 * Built and tested with MSDOS 6.2/Windows 3.1/Windows 95/Windows NT.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/gameport.h>
#include <86box/io.h>
#include <86box/sound.h>
#include <86box/timer.h>
#include <86box/snd_sb.h>
#include <86box/plat_unused.h>

#define JAZZ16_CFG_PORT       0x0201
#define JAZZ16_WAKEUP         0xaf
#define JAZZ16_SET_PORTS      0x50

typedef struct jazz16_t {
    sb_t sb;

    uint8_t cfg_state;
    uint8_t cfg_index;
    uint16_t base;
    uint16_t mpu_base;
} jazz16_t;

static void
jazz16_remap_opl(jazz16_t *jazz, uint16_t new_base)
{
    sb_t *sb = &jazz->sb;

    if (!sb->opl_enabled)
        return;

    if (jazz->base) {
        io_removehandler(jazz->base, 0x0004,
                         sb->opl.read, NULL, NULL,
                         sb->opl.write, NULL, NULL, sb->opl.priv);
        io_removehandler(jazz->base + 8, 0x0002,
                         sb->opl.read, NULL, NULL,
                         sb->opl.write, NULL, NULL, sb->opl.priv);
    }

    io_sethandler(new_base, 0x0004,
                  sb->opl.read, NULL, NULL,
                  sb->opl.write, NULL, NULL, sb->opl.priv);
    io_sethandler(new_base + 8, 0x0002,
                  sb->opl.read, NULL, NULL,
                  sb->opl.write, NULL, NULL, sb->opl.priv);
}

static void
jazz16_set_ports(jazz16_t *jazz, uint8_t val)
{
    sb_t *sb = &jazz->sb;
    const uint16_t new_base = 0x0200 | (val & 0x70);
    const uint16_t new_mpu  = 0x0300 | ((val & 0x03) << 4);

    if (new_base >= 0x0210 && new_base <= 0x0260) {
        if (sb->mixer_enabled && jazz->base)
            io_removehandler(jazz->base + 4, 0x0002,
                             sb_ct1345_mixer_read, NULL, NULL,
                             sb_ct1345_mixer_write, NULL, NULL, sb);

        jazz16_remap_opl(jazz, new_base);
        sb_dsp_setaddr(&sb->dsp, new_base);

        if (sb->mixer_enabled)
            io_sethandler(new_base + 4, 0x0002,
                          sb_ct1345_mixer_read, NULL, NULL,
                          sb_ct1345_mixer_write, NULL, NULL, sb);

        jazz->base = new_base;
    }

    if (sb->mpu) {
        /*
		 * Fix for Windows 95 jumping MPU port from 330 to 310
         */
        if (!((jazz->mpu_base == 0x0330) && (new_mpu == 0x0310) &&
              (new_base == jazz->base))) {
            mpu401_change_addr(sb->mpu, new_mpu);
            jazz->mpu_base = new_mpu;
        }
    }
}

static void
jazz16_config_write(UNUSED(uint16_t addr), uint8_t val, void *priv)
{
    jazz16_t *jazz = (jazz16_t *) priv;

    switch (jazz->cfg_state) {
        case 0:
            if (val >= (JAZZ16_WAKEUP - 3) && val <= JAZZ16_WAKEUP) {
                jazz->cfg_index = JAZZ16_WAKEUP - val;
                jazz->cfg_state = 1;
            }
            break;

        case 1:
            if (val == (uint8_t) (JAZZ16_SET_PORTS + jazz->cfg_index))
                jazz->cfg_state = 2;
            else
                jazz->cfg_state = 0;
            break;

        case 2:
            jazz16_set_ports(jazz, val);
            jazz->cfg_state = 0;
            break;

        default:
            jazz->cfg_state = 0;
            break;
    }
}

static uint8_t
jazz16_config_read(UNUSED(uint16_t addr), UNUSED(void *priv))
{
    return 0xff;
}

static void *
jazz16_init(UNUSED(const device_t *info))
{
    jazz16_t *jazz = calloc(1, sizeof(jazz16_t));
    sb_t *sb = &jazz->sb;

    /*
     * The MVD1216B chip is software configured.  Force unmapped resources so the 
	 * driver can assign them via port 201h and Jazz DSP command FBh.
     */
    jazz->base     = 0;
    jazz->mpu_base = 0;

    /* Fixed Pro Sonic 16 analogue output gain to ensure PCM audio is similar in level to PC speaker and external midi*/
    sb->mvd_1216_output_gain = 100.0;

    /* Enable the Yamaha 262 OPL3. */
    sb->opl_enabled = 1;
    fm_driver_get_cs(FM_YMF262, &sb->opl);

    sb_dsp_set_real_opl(&sb->dsp, 1);
    sb_dsp_init(&sb->dsp, SBPRO_DSP_301, SB_SUBTYPE_MVD1216, sb);
    sb_dsp_setaddr(&sb->dsp, 0);

    sb_dsp_setdma16_supported(&sb->dsp, 1);
    sb_dsp_setdma16_enabled(&sb->dsp, 1);

    sb_ct1345_mixer_reset(sb);
    sb->mixer_enabled = 1;

    io_sethandler(0x0388, 0x0004,
                  sb->opl.read, NULL, NULL,
                  sb->opl.write, NULL, NULL, sb->opl.priv);

    sound_add_handler(sb_get_buffer_sbpro, sb);
    music_add_handler(sb_get_music_buffer_sbpro, sb);
    sound_set_cd_audio_filter(sbpro_filter_cd_audio, sb);

    if (device_get_config_int("mpu401")) {
        sb->mpu = calloc(1, sizeof(mpu_t));

        /* Address 0 keeps the UART physically present but undecoded until the
           201h configuration sequence assigns 300h/310h/320h/330h.  FBh then
           supplies its IRQ. */
        mpu401_init(sb->mpu, 0, 0, M_UART, 1);
    }
    sb_dsp_set_mpu(&sb->dsp, sb->mpu);

    if (device_get_config_int("gameport")) {
        sb->gameport      = gameport_add(&gameport_200_device);
        sb->gameport_addr = 0x200;
    }

    /* MVD1216B software configuration port. */
    io_sethandler(JAZZ16_CFG_PORT, 1,
                  jazz16_config_read, NULL, NULL,
                  jazz16_config_write, NULL, NULL, jazz);

    return jazz;
}

static const device_config_t jazz16_config[] = {
    {
        .name = "mpu401", .description = "Enable MPU-401",
        .type = CONFIG_BINARY, .default_int = 1
    },
    {
        .name = "gameport", .description = "Enable Game port",
        .type = CONFIG_BINARY, .default_int = 1
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

const device_t jazz16_device = {
    .name          = "Media Vision Pro Sonic 16",
    .internal_name = "jazz16",
    .flags         = DEVICE_ISA16,
    .local         = 0,
    .init          = jazz16_init,
    .close         = sb_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = sb_speed_changed,
    .force_redraw  = NULL,
    .config        = jazz16_config
};
