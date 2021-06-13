/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Sound Blaster emulation.
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */
#ifndef SOUND_SND_SB_H
# define SOUND_SND_SB_H

#include <86box/snd_emu8k.h>
#include <86box/snd_mpu401.h>
#include <86box/snd_opl.h>
#include <86box/snd_sb_dsp.h>

#define SADLIB		1	/* No DSP */
#define SB1		2	/* DSP v1.05 */
#define SB15		3	/* DSP v2.00 */
#define SB2		4	/* DSP v2.01 - needed for high-speed DMA */
#define SBPRO		5	/* DSP v3.00 */
#define SBPRO2		6	/* DSP v3.02 + OPL3 */
#define SB16		7	/* DSP v4.05 + OPL3 */
#define SADGOLD		8	/* AdLib Gold */
#define SND_WSS		9	/* Windows Sound System */
#define SND_PAS16	10	/* Pro Audio Spectrum 16 */

/* SB 2.0 CD version */
typedef struct sb_ct1335_mixer_t
{
        double master;
        double voice;
        double fm;
        double cd;

        uint8_t index;
        uint8_t regs[256];
} sb_ct1335_mixer_t;
/* SB PRO */
typedef struct sb_ct1345_mixer_t
{
        double master_l, master_r;
        double voice_l,  voice_r;
        double fm_l,     fm_r;
        double cd_l,     cd_r;
        double line_l,   line_r;
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
typedef struct sb_ct1745_mixer_t
{
        double master_l, master_r;
        double voice_l,  voice_r;
        double fm_l,     fm_r;
        double cd_l,     cd_r;
        double line_l,   line_r;
        double mic;
        double speaker;

        int bass_l,   bass_r;
        int treble_l, treble_r;
        
        int output_selector;
        #define OUTPUT_MIC 1
        #define OUTPUT_CD_R 2
        #define OUTPUT_CD_L 4
        #define OUTPUT_LINE_R 8
        #define OUTPUT_LINE_L 16

        int input_selector_left;
        int input_selector_right;
        #define INPUT_MIC 1
        #define INPUT_CD_R 2
        #define INPUT_CD_L 4
        #define INPUT_LINE_R 8
        #define INPUT_LINE_L 16
        #define INPUT_MIDI_R 32
        #define INPUT_MIDI_L 64

        int mic_agc;
        
        int32_t input_gain_L;
        int32_t input_gain_R;
        double output_gain_L;
        double output_gain_R;
        
        uint8_t index;
        uint8_t regs[256];
} sb_ct1745_mixer_t;

typedef struct sb_t
{
	uint8_t		opl_enabled, mixer_enabled;
        opl_t           opl, opl2;
        sb_dsp_t        dsp;
        union {
                sb_ct1335_mixer_t mixer_sb2;
                sb_ct1345_mixer_t mixer_sbpro;
                sb_ct1745_mixer_t mixer_sb16;
        };
        mpu_t		*mpu;
        emu8k_t         emu8k;
        void		*gameport;

        int pos;
        
        uint8_t pos_regs[8];

        uint16_t opl_pnp_addr;
} sb_t;

extern void sb_ct1345_mixer_write(uint16_t addr, uint8_t val, void *p);
extern uint8_t sb_ct1345_mixer_read(uint16_t addr, void *p);
extern void sb_ct1345_mixer_reset(sb_t* sb);

extern void sb_get_buffer_sbpro(int32_t *buffer, int len, void *p);
extern void sbpro_filter_cd_audio(int channel, double *buffer, void *p);
extern void sb_close(void *p);
extern void sb_speed_changed(void *p);

#endif	/*SOUND_SND_SB_H*/
