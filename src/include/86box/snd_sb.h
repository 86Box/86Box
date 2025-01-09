/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Sound Blaster emulation.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          TheCollector1995, <mariogplayer@gmail.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2018 Miran Grca.
 *          Copyright 2024-2025 Jasmine Iwanek.
 */

#ifndef SOUND_SND_SB_H
#define SOUND_SND_SB_H

#include <86box/snd_cms.h>
#include <86box/snd_emu8k.h>
#include <86box/snd_mpu401.h>
#include <86box/snd_opl.h>
#include <86box/snd_sb_dsp.h>

enum {
    SADLIB  = 1,     /* No DSP */
    SB_DSP_105,      /* DSP v1.05, Original CT1320 (Also known as CT1310) */
    SB_DSP_200,      /* DSP v2.00 */
    SB_DSP_201,      /* DSP v2.01 - needed for high-speed DMA, Seen on CT1350B with CT1336 */
    SB_DSP_202,      /* DSP v2.02 - Seen on CT1350B with CT1336A */
    SBPRO_DSP_300,   /* DSP v3.00 */
    SBPRO2_DSP_302,  /* DSP v3.02 + OPL3 */
    SB16_DSP_404,    /* DSP v4.05 + OPL3 */
    SB16_DSP_405,    /* DSP v4.05 + OPL3 */
    SB16_DSP_406,    /* DSP v4.06 + OPL3 */
    SB16_DSP_411,    /* DSP v4.11 + OPL3 */
    SBAWE32_DSP_412, /* DSP v4.12 + OPL3 */
    SBAWE32_DSP_413, /* DSP v4.13 + OPL3 */
    SBAWE64_DSP_416  /* DSP v4.16 + OPL3 */
};

/* SB 2.0 CD version */
typedef struct sb_ct1335_mixer_t {
    double master;
    double voice;
    double fm;
    double cd;

    uint8_t index;
    uint8_t regs[256];
} sb_ct1335_mixer_t;

/* SB PRO */
typedef struct sb_ct1345_mixer_t {
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
} sb_ct1345_mixer_t;

/* SB16 and AWE32 */
typedef struct sb_ct1745_mixer_t {
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
    double speaker;

    int bass_l;
    int bass_r;
    int treble_l;
    int treble_r;

    int output_selector;
#define OUTPUT_MIC    1
#define OUTPUT_CD_R   2
#define OUTPUT_CD_L   4
#define OUTPUT_LINE_R 8
#define OUTPUT_LINE_L 16

    int input_selector_left;
    int input_selector_right;
#define INPUT_MIC    1
#define INPUT_CD_R   2
#define INPUT_CD_L   4
#define INPUT_LINE_R 8
#define INPUT_LINE_L 16
#define INPUT_MIDI_R 32
#define INPUT_MIDI_L 64

    int mic_agc;

    int32_t input_gain_L;
    int32_t input_gain_R;
    double  output_gain_L;
    double  output_gain_R;

    uint8_t index;
    uint8_t regs[256];

    int output_filter; /* for clones */
} sb_ct1745_mixer_t;

/* ESS AudioDrive */
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
    double mic_l;
    double mic_r;
    double auxb_l;
    double auxb_r;
    double speaker;
    /*see sb_ct1745_mixer for values for input selector*/
    int32_t input_selector;
    /* extra values for input selector */
    #define INPUT_MIXER_L 128
    #define INPUT_MIXER_R 256

    int input_filter;
    int in_filter_freq;
    int output_filter;

    int stereo;
    int stereo_isleft;

    uint8_t index;
    uint8_t regs[256];

    uint8_t ess_id_str[4];
    uint8_t ess_id_str_pos;
} ess_mixer_t;

typedef struct sb_t {
    uint8_t  cms_enabled;
    uint8_t  opl_enabled;
    uint8_t  mixer_enabled;
    cms_t    cms;
    fm_drv_t opl;
    fm_drv_t opl2;
    sb_dsp_t dsp;
    union {
        sb_ct1335_mixer_t mixer_sb2;
        sb_ct1345_mixer_t mixer_sbpro;
        sb_ct1745_mixer_t mixer_sb16;
        ess_mixer_t       mixer_ess;
    };
    mpu_t  *mpu;
    emu8k_t emu8k;
    void   *gameport;

    int pnp;
    int has_ide;

    uint8_t pos_regs[8];
    uint8_t pnp_rom[512];

    uint16_t opl_pnp_addr;

    uint16_t midi_addr;
    uint16_t gameport_addr;

    void   *opl_mixer;
    void  (*opl_mix)(void*, double*, double*);
} sb_t;

typedef struct goldfinch_t {
    emu8k_t emu8k;

    uint8_t pnp_rom[512];
} goldfinch_t;

extern void    sb_ct1345_mixer_write(uint16_t addr, uint8_t val, void *priv);
extern uint8_t sb_ct1345_mixer_read(uint16_t addr, void *priv);
extern void    sb_ct1345_mixer_reset(sb_t *sb);

extern void    sb_ct1745_mixer_write(uint16_t addr, uint8_t val, void *priv);
extern uint8_t sb_ct1745_mixer_read(uint16_t addr, void *priv);
extern void    sb_ct1745_mixer_reset(sb_t *sb);

extern void    sb_ess_mixer_write(uint16_t addr, uint8_t val, void *priv);
extern uint8_t sb_ess_mixer_read(uint16_t addr, void *priv);
extern void    sb_ess_mixer_reset(sb_t *sb);

extern void sb_get_buffer_sbpro(int32_t *buffer, int len, void *priv);
extern void sb_get_music_buffer_sbpro(int32_t *buffer, int len, void *priv);
extern void sbpro_filter_cd_audio(int channel, double *buffer, void *priv);
extern void sb16_awe32_filter_cd_audio(int channel, double *buffer, void *priv);
extern void sb_close(void *priv);
extern void sb_speed_changed(void *priv);

#endif /*SOUND_SND_SB_H*/
