/*
 * 86Box     A hypervisor and IBM PC system emulator that specializes in
 *           running old operating systems and software designed for IBM
 *           PC systems and compatibles from 1981 through fairly recent
 *           system designs based on the PCI bus.
 *
 *           This file is part of the 86Box distribution.
 *
 *           Sound Blaster emulation.
 *
 *
 *
 * Authors:  Sarah Walker, <https://pcem-emulator.co.uk/>
 *           Miran Grca, <mgrca8@gmail.com>
 *           TheCollector1995, <mariogplayer@gmail.com>
 *
 *           Copyright 2008-2020 Sarah Walker.
 *           Copyright 2016-2020 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/filters.h>
#include <86box/gameport.h>
#include <86box/hdc.h>
#include <86box/isapnp.h>
#include <86box/hdc_ide.h>
#include <86box/io.h>
#include <86box/mca.h>
#include <86box/mem.h>
#include <86box/midi.h>
#include <86box/pic.h>
#include <86box/rom.h>
#include <86box/sound.h>
#include <86box/timer.h>
#include <86box/snd_sb.h>
#include <86box/plat_unused.h>

static const double sb_att_4dbstep_3bits[] = {
      164.0,  2067.0,  3276.0,  5193.0,  8230.0, 13045.0, 20675.0, 32767.0
};

static const double sb_att_7dbstep_2bits[] = {
      164.0,  6537.0, 14637.0, 32767.0
};

/* SB PRO */
typedef struct ess_mixer_t {
    double master_l;
    double master_r;
    double voice_l;
    double voice_r;
    double fm_l;
    double fm_r;
    double cd_l;
    double cd_r;
    double line_l;
    double line_r;
    double mic;
    /*see sb_ct1745_mixer for values for input selector*/
    int32_t input_selector;

    int input_filter;
    int in_filter_freq;
    int output_filter;

    int stereo;
    int stereo_isleft;

    uint8_t index;
    uint8_t regs[256];
} ess_mixer_t;

typedef struct ess_t {
    uint8_t  mixer_enabled;
    fm_drv_t opl;
    sb_dsp_t dsp;
    union {
        ess_mixer_t mixer_sbpro;
    };
    mpu_t  *mpu;
    emu8k_t emu8k;
    void   *gameport;

    int pnp;

    uint8_t pos_regs[8];
    uint8_t pnp_rom[512];

    uint16_t opl_pnp_addr;
    uint16_t gameport_addr;

    void   *opl_mixer;
    void  (*opl_mix)(void*, double*, double*);
} ess_t;

static inline uint8_t expand16to32(const uint8_t t) {
    /* 4-bit -> 5-bit expansion.
     *
     * 0 -> 0
     * 1 -> 2
     * 2 -> 4
     * 3 -> 6
     * ....
     * 7 -> 14
     * 8 -> 17
     * 9 -> 19
     * 10 -> 21
     * 11 -> 23
     * ....
     * 15 -> 31 */
    return (t << 1) | (t >> 3);
}

void
ess_mixer_write(uint16_t addr, uint8_t val, void *priv)
{
    ess_t       *ess   = (ess_t *) priv;
    ess_mixer_t *mixer = &ess->mixer_sbpro;

    if (!(addr & 1)) {
        mixer->index      = val;
        mixer->regs[0x01] = val;
    } else {
        if (mixer->index == 0) {
            /* Reset */
            mixer->regs[0x0a] = mixer->regs[0x0c] = 0x00;
            mixer->regs[0x0e]                     = 0x00;
            /* Changed default from -11dB to 0dB */
            mixer->regs[0x04] = mixer->regs[0x22] = 0xee;
            mixer->regs[0x26] = mixer->regs[0x28] = 0xee;
            mixer->regs[0x2e]                     = 0x00;
            sb_dsp_set_stereo(&ess->dsp, mixer->regs[0x0e] & 2);
        } else {
            mixer->regs[mixer->index] = val;

            switch (mixer->index) {
                /* Compatibility: chain registers 0x02 and 0x22 as well as 0x06 and 0x26 */
                case 0x02:
                case 0x06:
                case 0x08:
                    mixer->regs[mixer->index + 0x20] = ((val & 0xe) << 4) | (val & 0xe);
                    break;

                case 0x22:
                case 0x26:
                case 0x28:
                    mixer->regs[mixer->index - 0x20] = (val & 0xe);
                    break;

                /* More compatibility:
                   SoundBlaster Pro selects register 020h for 030h, 022h for 032h,
                   026h for 036h, and 028h for 038h. */
                case 0x30:
                case 0x32:
                case 0x36:
                case 0x38:
                    mixer->regs[mixer->index - 0x10] = (val & 0xee);
                    break;

                case 0x00:
                case 0x04:
                case 0x0a:
                case 0x0c:
                case 0x0e:
                case 0x2e:
                    break;

                default:
                    //sb_log("ess: Unknown register WRITE: %02X\t%02X\n", mixer->index, mixer->regs[mixer->index]);
                    break;
            }
        }

        mixer->voice_l  = sb_att_4dbstep_3bits[(mixer->regs[0x04] >> 5) & 0x7] / 32768.0;
        mixer->voice_r  = sb_att_4dbstep_3bits[(mixer->regs[0x04] >> 1) & 0x7] / 32768.0;
        mixer->master_l = sb_att_4dbstep_3bits[(mixer->regs[0x22] >> 5) & 0x7] / 32768.0;
        mixer->master_r = sb_att_4dbstep_3bits[(mixer->regs[0x22] >> 1) & 0x7] / 32768.0;
        mixer->fm_l     = sb_att_4dbstep_3bits[(mixer->regs[0x26] >> 5) & 0x7] / 32768.0;
        mixer->fm_r     = sb_att_4dbstep_3bits[(mixer->regs[0x26] >> 1) & 0x7] / 32768.0;
        mixer->cd_l     = sb_att_4dbstep_3bits[(mixer->regs[0x28] >> 5) & 0x7] / 32768.0;
        mixer->cd_r     = sb_att_4dbstep_3bits[(mixer->regs[0x28] >> 1) & 0x7] / 32768.0;
        mixer->line_l   = sb_att_4dbstep_3bits[(mixer->regs[0x2e] >> 5) & 0x7] / 32768.0;
        mixer->line_r   = sb_att_4dbstep_3bits[(mixer->regs[0x2e] >> 1) & 0x7] / 32768.0;

        mixer->mic = sb_att_7dbstep_2bits[(mixer->regs[0x0a] >> 1) & 0x3] / 32768.0;

        mixer->output_filter  = !(mixer->regs[0xe] & 0x20);
        mixer->input_filter   = !(mixer->regs[0xc] & 0x20);
        mixer->in_filter_freq = ((mixer->regs[0xc] & 0x8) == 0) ? 3200 : 8800;
        mixer->stereo         = mixer->regs[0xe] & 2;
        if (mixer->index == 0xe)
            sb_dsp_set_stereo(&ess->dsp, val & 2);

        switch (mixer->regs[0xc] & 6) {
            case 2:
                mixer->input_selector = INPUT_CD_L | INPUT_CD_R;
                break;
            case 6:
                mixer->input_selector = INPUT_LINE_L | INPUT_LINE_R;
                break;
            default:
                mixer->input_selector = INPUT_MIC;
                break;
        }

        /* TODO: pcspeaker volume? Or is it not worth? */
    }
}

uint8_t
ess_mixer_read(uint16_t addr, void *priv)
{
    const ess_t       *ess   = (ess_t *) priv;
    const ess_mixer_t *mixer = &ess->mixer_sbpro;

    if (!(addr & 1))
        return mixer->index;

    switch (mixer->index) {
        case 0x00:
        case 0x04:
        case 0x0a:
        case 0x0c:
        case 0x0e:
        case 0x22:
        case 0x26:
        case 0x28:
        case 0x2e:
        case 0x02:
        case 0x06:
        case 0x30:
        case 0x32:
        case 0x36:
        case 0x38:
            return mixer->regs[mixer->index];

        default:
            //sb_log("ess: Unknown register READ: %02X\t%02X\n", mixer->index, mixer->regs[mixer->index]);
            break;
    }

    return 0xff;
}

void
ess_mixer_reset(ess_t *ess)
{
    ess_mixer_write(4, 0, ess);
    ess_mixer_write(5, 0, ess);
}