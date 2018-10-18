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
 * Version:	@(#)sound_sb.c	1.0.14	2018/10/18
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../io.h"
#include "../mca.h"
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "sound.h"
#include "filters.h"
#include "snd_dbopl.h"
#include "snd_emu8k.h"
#include "snd_mpu401.h"
#include "snd_opl.h"
#include "snd_sb.h"
#include "snd_sb_dsp.h"

//#define SB_DSP_RECORD_DEBUG

#ifdef SB_DSP_RECORD_DEBUG
FILE* soundfsb = 0/*NULL*/;
FILE* soundfsbin = 0/*NULL*/;
#endif



/* SB 2.0 CD version */
typedef struct sb_ct1335_mixer_t
{
        int32_t master;
        int32_t voice;
        int32_t fm;
        int32_t cd;

        uint8_t index;
        uint8_t regs[256];
} sb_ct1335_mixer_t;
/* SB PRO */
typedef struct sb_ct1345_mixer_t
{
        int32_t master_l, master_r;
        int32_t voice_l,  voice_r;
        int32_t fm_l,     fm_r;
        int32_t cd_l,     cd_r;
        int32_t line_l,   line_r;
        int32_t mic;
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
        int32_t master_l, master_r;
        int32_t voice_l,  voice_r;
        int32_t fm_l,     fm_r;
        int32_t cd_l,     cd_r;
        int32_t line_l,   line_r;
        int32_t mic;
        int32_t speaker;

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
        int32_t output_gain_L;
        int32_t output_gain_R;
        
        uint8_t index;
        uint8_t regs[256];
} sb_ct1745_mixer_t;

typedef struct sb_t
{
	uint8_t		opl_enabled;
        opl_t           opl;
        sb_dsp_t        dsp;
        union {
                sb_ct1335_mixer_t mixer_sb2;
                sb_ct1345_mixer_t mixer_sbpro;
                sb_ct1745_mixer_t mixer_sb16;
        };
        mpu_t		*mpu;
        emu8k_t         emu8k;
#if 0
	sb_ct1745_mixer_t temp_mixer_sb16;
#endif

        int pos;
        
        uint8_t pos_regs[8];
        
        int opl_emu;
} sb_t;
/* 0 to 7 -> -14dB to 0dB i 2dB steps. 8 to 15 -> 0 to +14dB in 2dB steps.
  Note that for positive dB values, this is not amplitude, it is amplitude-1. */
const float sb_bass_treble_4bits[]= {
   0.199526231, 0.25, 0.316227766, 0.398107170, 0.5, 0.63095734, 0.794328234, 1, 
    0, 0.25892541, 0.584893192, 1, 1.511886431, 2.16227766, 3, 4.011872336
};

/* Attenuation tables for the mixer. Max volume = 32767 in order to give 6dB of 
 * headroom and avoid integer overflow */
const int32_t sb_att_2dbstep_5bits[]=
{
        25,32,41,51,65,82,103,130,164,206,260,327,412,519,653,
        822,1036,1304,1641,2067,2602,3276,4125,5192,6537,8230,10362,13044,
        16422,20674,26027,32767
};
const int32_t sb_att_4dbstep_3bits[]=
{
        164,2067,3276,5193,8230,13045,20675,32767
};
const int32_t sb_att_7dbstep_2bits[]=
{
        164,6537,14637,32767
};


#ifdef ENABLE_SB_LOG
int sb_do_log = ENABLE_SB_LOG;


static void
sb_log(const char *fmt, ...)
{
    va_list ap;

    if (sb_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define sb_log(fmt, ...)
#endif


/* sb 1, 1.5, 2, 2 mvc do not have a mixer, so signal is hardwired */
static void sb_get_buffer_sb2(int32_t *buffer, int len, void *p)
{
        sb_t *sb = (sb_t *)p;
                
        int c;

	if (sb->opl_enabled)
        	opl2_update2(&sb->opl);
        sb_dsp_update(&sb->dsp);
        for (c = 0; c < len * 2; c += 2)
        {
                int32_t out = 0;
		if (sb->opl_enabled)
                	out = ((sb->opl.buffer[c]     * 51000) >> 16);
                //TODO: Recording: Mic and line In with AGC
                out += (int32_t)(((sb_iir(0, (float)sb->dsp.buffer[c]) / 1.3) * 65536) / 3) >> 16;
        
                buffer[c]     += out;
                buffer[c + 1] += out;
        }

        sb->pos = 0;
        sb->opl.pos = 0;
        sb->dsp.pos = 0;
}

static void sb_get_buffer_sb2_mixer(int32_t *buffer, int len, void *p)
{
        sb_t *sb = (sb_t *)p;
        sb_ct1335_mixer_t *mixer = &sb->mixer_sb2;
                
        int c;

	if (sb->opl_enabled)
        	opl2_update2(&sb->opl);
        sb_dsp_update(&sb->dsp);
        for (c = 0; c < len * 2; c += 2)
        {
                int32_t out = 0;
                
		if (sb->opl_enabled)
                	out = ((((sb->opl.buffer[c]     * mixer->fm) >> 16) * 51000) >> 15);
                /* TODO: Recording : I assume it has direct mic and line in like sb2 */
                /* It is unclear from the docs if it has a filter, but it probably does */
                out += (int32_t)(((sb_iir(0, (float)sb->dsp.buffer[c])     / 1.3) * mixer->voice) / 3) >> 15;
                
                out = (out * mixer->master) >> 15;
                        
                buffer[c]     += out;
                buffer[c + 1] += out;
        }

        sb->pos = 0;
        sb->opl.pos = 0;
        sb->dsp.pos = 0;
}

static void sb_get_buffer_sbpro(int32_t *buffer, int len, void *p)
{
        sb_t *sb = (sb_t *)p;
        sb_ct1345_mixer_t *mixer = &sb->mixer_sbpro;
                
        int c;

	if (sb->opl_enabled) {
        	if (sb->dsp.sb_type == SBPRO)
                	opl2_update2(&sb->opl);
        	else
                	opl3_update2(&sb->opl);
	}

        sb_dsp_update(&sb->dsp);
        for (c = 0; c < len * 2; c += 2)
        {
                int32_t out_l = 0, out_r = 0;
                
		if (sb->opl_enabled) {
                	out_l = ((((sb->opl.buffer[c]     * mixer->fm_l) >> 16) * (sb->opl_emu ? 47000 : 51000)) >> 15);
                	out_r = ((((sb->opl.buffer[c + 1] * mixer->fm_r) >> 16) * (sb->opl_emu ? 47000 : 51000)) >> 15);
		}
                
                /*TODO: Implement the stereo switch on the mixer instead of on the dsp? */
                if (mixer->output_filter)
                {
                        out_l += (int32_t)(((sb_iir(0, (float)sb->dsp.buffer[c])     / 1.3) * mixer->voice_l) / 3) >> 15;
                        out_r += (int32_t)(((sb_iir(1, (float)sb->dsp.buffer[c + 1]) / 1.3) * mixer->voice_r) / 3) >> 15;
                }
                else
                {
                        out_l += ((int32_t)(sb->dsp.buffer[c]     * mixer->voice_l) / 3) >> 15;
                        out_r += ((int32_t)(sb->dsp.buffer[c + 1] * mixer->voice_r) / 3) >> 15;
                }
                //TODO: recording CD, Mic with AGC or line in. Note: mic volume does not affect recording.
                
                out_l = (out_l * mixer->master_l) >> 15;
                out_r = (out_r * mixer->master_r) >> 15;

                buffer[c]     += out_l;
                buffer[c + 1] += out_r;
        }

        sb->pos = 0;
        sb->opl.pos = 0;
        sb->dsp.pos = 0;
}

// FIXME: See why this causes weird audio glitches in some situations.
#if 0
static void sb_process_buffer_sb16(int32_t *buffer, int len, void *p)
{
        sb_t *sb = (sb_t *)p;
        sb_ct1745_mixer_t *mixer = &sb->temp_mixer_sb16;
                
        int c;

        for (c = 0; c < len * 2; c += 2)
        {
                int32_t out_l = 0, out_r = 0;

                out_l = ((int32_t)(low_fir_sb16(0, (float)buffer[c])     * mixer->cd_l) / 3) >> 15;
                out_r = ((int32_t)(low_fir_sb16(1, (float)buffer[c + 1]) * mixer->cd_r) / 3) >> 15;

                out_l = (out_l * mixer->master_l) >> 15;
                out_r = (out_r * mixer->master_r) >> 15;

                if (mixer->bass_l != 8 || mixer->bass_r != 8 || mixer->treble_l != 8 || mixer->treble_r != 8)
                {
                        /* This is not exactly how one does bass/treble controls, but the end result is like it. A better implementation would reduce the cpu usage */
                        if (mixer->bass_l>8) out_l += (int32_t)(low_iir(0, (float)out_l)*sb_bass_treble_4bits[mixer->bass_l]);
                        if (mixer->bass_r>8)  out_r += (int32_t)(low_iir(1, (float)out_r)*sb_bass_treble_4bits[mixer->bass_r]);
                        if (mixer->treble_l>8) out_l += (int32_t)(high_iir(0, (float)out_l)*sb_bass_treble_4bits[mixer->treble_l]);
                        if (mixer->treble_r>8) out_r += (int32_t)(high_iir(1, (float)out_r)*sb_bass_treble_4bits[mixer->treble_r]);
                        if (mixer->bass_l<8)   out_l = (int32_t)((out_l )*sb_bass_treble_4bits[mixer->bass_l] + low_cut_iir(0, (float)out_l)*(1.f-sb_bass_treble_4bits[mixer->bass_l]));
                        if (mixer->bass_r<8)   out_r = (int32_t)((out_r )*sb_bass_treble_4bits[mixer->bass_r] + low_cut_iir(1, (float)out_r)*(1.f-sb_bass_treble_4bits[mixer->bass_r])); 
                        if (mixer->treble_l<8) out_l = (int32_t)((out_l )*sb_bass_treble_4bits[mixer->treble_l] + high_cut_iir(0, (float)out_l)*(1.f-sb_bass_treble_4bits[mixer->treble_l]));
                        if (mixer->treble_r<8) out_r = (int32_t)((out_r )*sb_bass_treble_4bits[mixer->treble_r] + high_cut_iir(1, (float)out_r)*(1.f-sb_bass_treble_4bits[mixer->treble_r]));
                }

                buffer[c]     = (out_l << mixer->output_gain_L);
                buffer[c + 1] = (out_r << mixer->output_gain_R);
	}
}
#endif

static void sb_get_buffer_sb16(int32_t *buffer, int len, void *p)
{
        sb_t *sb = (sb_t *)p;
        sb_ct1745_mixer_t *mixer = &sb->mixer_sb16;
                
        int c;

	if (sb->opl_enabled)
        	opl3_update2(&sb->opl);
        sb_dsp_update(&sb->dsp);
        const int dsp_rec_pos = sb->dsp.record_pos_write;
        for (c = 0; c < len * 2; c += 2)
        {
                int32_t out_l = 0, out_r = 0, in_l, in_r;
                
		if (sb->opl_enabled) {
                	out_l = ((((sb->opl.buffer[c]     * mixer->fm_l) >> 16) * (sb->opl_emu ? 47000 : 51000)) >> 15);
                	out_r = ((((sb->opl.buffer[c + 1] * mixer->fm_r) >> 16) * (sb->opl_emu ? 47000 : 51000)) >> 15);
		}

                /*TODO: multi-recording mic with agc/+20db, cd and line in with channel inversion */
                in_l = (mixer->input_selector_left&INPUT_MIDI_L) ? out_l : 0 + (mixer->input_selector_left&INPUT_MIDI_R) ? out_r : 0;
                in_r = (mixer->input_selector_right&INPUT_MIDI_L) ? out_l : 0 + (mixer->input_selector_right&INPUT_MIDI_R) ? out_r : 0;
        
                out_l += ((int32_t)(low_fir_sb16(0, (float)sb->dsp.buffer[c])     * mixer->voice_l) / 3) >> 15;
                out_r += ((int32_t)(low_fir_sb16(1, (float)sb->dsp.buffer[c + 1]) * mixer->voice_r) / 3) >> 15;

                out_l = (out_l * mixer->master_l) >> 15;
                out_r = (out_r * mixer->master_r) >> 15;
                
                if (mixer->bass_l != 8 || mixer->bass_r != 8 || mixer->treble_l != 8 || mixer->treble_r != 8)
                {
                        /* This is not exactly how one does bass/treble controls, but the end result is like it. A better implementation would reduce the cpu usage */
                        if (mixer->bass_l>8) out_l += (int32_t)(low_iir(0, (float)out_l)*sb_bass_treble_4bits[mixer->bass_l]);
                        if (mixer->bass_r>8)  out_r += (int32_t)(low_iir(1, (float)out_r)*sb_bass_treble_4bits[mixer->bass_r]);
                        if (mixer->treble_l>8) out_l += (int32_t)(high_iir(0, (float)out_l)*sb_bass_treble_4bits[mixer->treble_l]);
                        if (mixer->treble_r>8) out_r += (int32_t)(high_iir(1, (float)out_r)*sb_bass_treble_4bits[mixer->treble_r]);
                        if (mixer->bass_l<8)   out_l = (int32_t)((out_l )*sb_bass_treble_4bits[mixer->bass_l] + low_cut_iir(0, (float)out_l)*(1.f-sb_bass_treble_4bits[mixer->bass_l]));
                        if (mixer->bass_r<8)   out_r = (int32_t)((out_r )*sb_bass_treble_4bits[mixer->bass_r] + low_cut_iir(1, (float)out_r)*(1.f-sb_bass_treble_4bits[mixer->bass_r])); 
                        if (mixer->treble_l<8) out_l = (int32_t)((out_l )*sb_bass_treble_4bits[mixer->treble_l] + high_cut_iir(0, (float)out_l)*(1.f-sb_bass_treble_4bits[mixer->treble_l]));
                        if (mixer->treble_r<8) out_r = (int32_t)((out_r )*sb_bass_treble_4bits[mixer->treble_r] + high_cut_iir(1, (float)out_r)*(1.f-sb_bass_treble_4bits[mixer->treble_r]));
                }
                if (sb->dsp.sb_enable_i)
                {
                        int c_record = dsp_rec_pos;
                        c_record +=(((c/2) * sb->dsp.sb_freq) / 48000)*2;
                        in_l <<= mixer->input_gain_L;
                        in_r <<= mixer->input_gain_R;
                        // Clip signal
                        if (in_l < -32768)
                                in_l = -32768;
                        else if (in_l > 32767)
                                in_l = 32767;
                
                        if (in_r < -32768)
                                in_r = -32768;
                        else if (in_r > 32767)
                                in_r = 32767;
                        sb->dsp.record_buffer[c_record&0xFFFF] = in_l;
                        sb->dsp.record_buffer[(c_record+1)&0xFFFF] = in_r;
                }

                buffer[c]     += (out_l << mixer->output_gain_L);
                buffer[c + 1] += (out_r << mixer->output_gain_R);
        }
        sb->dsp.record_pos_write+=((len * sb->dsp.sb_freq) / 48000)*2;
        sb->dsp.record_pos_write&=0xFFFF;

        sb->pos = 0;
        sb->opl.pos = 0;
        sb->dsp.pos = 0;
#if 0
	memcpy(&sb->temp_mixer_sb16, &sb->mixer_sb16, sizeof(sb_ct1745_mixer_t));
#endif
}
#ifdef SB_DSP_RECORD_DEBUG
int old_dsp_rec_pos=0;
int buf_written=0;
int last_crecord=0;
#endif
static void sb_get_buffer_emu8k(int32_t *buffer, int len, void *p)
{
        sb_t *sb = (sb_t *)p;
        sb_ct1745_mixer_t *mixer = &sb->mixer_sb16;
                
        int c;

	if (sb->opl_enabled)
        	opl3_update2(&sb->opl);
        emu8k_update(&sb->emu8k);
        sb_dsp_update(&sb->dsp);
        const int dsp_rec_pos = sb->dsp.record_pos_write;
        for (c = 0; c < len * 2; c += 2)
        {
                int32_t out_l = 0, out_r = 0, in_l, in_r;
                int c_emu8k = (((c/2) * 44100) / 48000)*2;
                
		if (sb->opl_enabled) {
                	out_l = ((((sb->opl.buffer[c]     * mixer->fm_l) >> 15) * (sb->opl_emu ? 47000 : 51000)) >> 16);
                	out_r = ((((sb->opl.buffer[c + 1] * mixer->fm_r) >> 15) * (sb->opl_emu ? 47000 : 51000)) >> 16);
		}

               	out_l += ((sb->emu8k.buffer[c_emu8k]     * mixer->fm_l) >> 15);
               	out_r += ((sb->emu8k.buffer[c_emu8k + 1] * mixer->fm_r) >> 15);
                
                /*TODO: multi-recording mic with agc/+20db, cd and line in with channel inversion  */
                in_l = (mixer->input_selector_left&INPUT_MIDI_L) ? out_l : 0 + (mixer->input_selector_left&INPUT_MIDI_R) ? out_r : 0;
                in_r = (mixer->input_selector_right&INPUT_MIDI_L) ? out_l : 0 + (mixer->input_selector_right&INPUT_MIDI_R) ? out_r : 0;
                
                out_l += ((int32_t)(low_fir_sb16(0, (float)sb->dsp.buffer[c])     * mixer->voice_l) / 3) >> 15;
                out_r += ((int32_t)(low_fir_sb16(1, (float)sb->dsp.buffer[c + 1]) * mixer->voice_r) / 3) >> 15;

                out_l = (out_l * mixer->master_l) >> 15;
                out_r = (out_r * mixer->master_r) >> 15;

                if (mixer->bass_l != 8 || mixer->bass_r != 8 || mixer->treble_l != 8 || mixer->treble_r != 8)
                {
                        /* This is not exactly how one does bass/treble controls, but the end result is like it. A better implementation would reduce the cpu usage */
                        if (mixer->bass_l>8) out_l += (int32_t)(low_iir(0, (float)out_l)*sb_bass_treble_4bits[mixer->bass_l]);
                        if (mixer->bass_r>8)  out_r += (int32_t)(low_iir(1, (float)out_r)*sb_bass_treble_4bits[mixer->bass_r]);
                        if (mixer->treble_l>8) out_l += (int32_t)(high_iir(0, (float)out_l)*sb_bass_treble_4bits[mixer->treble_l]);
                        if (mixer->treble_r>8) out_r += (int32_t)(high_iir(1, (float)out_r)*sb_bass_treble_4bits[mixer->treble_r]);
                        if (mixer->bass_l<8)   out_l = (int32_t)(out_l *sb_bass_treble_4bits[mixer->bass_l] + low_cut_iir(0, (float)out_l)*(1.f-sb_bass_treble_4bits[mixer->bass_l]));
                        if (mixer->bass_r<8)   out_r = (int32_t)(out_r *sb_bass_treble_4bits[mixer->bass_r] + low_cut_iir(1, (float)out_r)*(1.f-sb_bass_treble_4bits[mixer->bass_r])); 
                        if (mixer->treble_l<8) out_l = (int32_t)(out_l *sb_bass_treble_4bits[mixer->treble_l] + high_cut_iir(0, (float)out_l)*(1.f-sb_bass_treble_4bits[mixer->treble_l]));
                        if (mixer->treble_r<8) out_r = (int32_t)(out_r *sb_bass_treble_4bits[mixer->treble_r] + high_cut_iir(1, (float)out_r)*(1.f-sb_bass_treble_4bits[mixer->treble_r]));
                }
                if (sb->dsp.sb_enable_i)
                {
//                      in_l += (mixer->input_selector_left&INPUT_CD_L) ? audio_cd_buffer[cd_read_pos+c_emu8k] : 0 + (mixer->input_selector_left&INPUT_CD_R) ? audio_cd_buffer[cd_read_pos+c_emu8k+1] : 0;
//                      in_r += (mixer->input_selector_right&INPUT_CD_L) ? audio_cd_buffer[cd_read_pos+c_emu8k]: 0 + (mixer->input_selector_right&INPUT_CD_R) ? audio_cd_buffer[cd_read_pos+c_emu8k+1] : 0;

                        int c_record = dsp_rec_pos;
                        c_record +=(((c/2) * sb->dsp.sb_freq) / 48000)*2;
                        #ifdef SB_DSP_RECORD_DEBUG
                                if (c_record > 0xFFFF && !buf_written)
                                {
                                        if (!soundfsb) soundfsb=plat_fopen(L"sound_sb.pcm",L"wb");
                                        fwrite(sb->dsp.record_buffer,2,0x10000,soundfsb);
                                        old_dsp_rec_pos = dsp_rec_pos;
                                        buf_written=1;
                                }
                        #endif
                        in_l <<= mixer->input_gain_L;
                        in_r <<= mixer->input_gain_R;
                        // Clip signal
                        if (in_l < -32768)
                                in_l = -32768;
                        else if (in_l > 32767)
                                in_l = 32767;
                
                        if (in_r < -32768)
                                in_r = -32768;
                        else if (in_r > 32767)
                                in_r = 32767;
                        sb->dsp.record_buffer[c_record&0xFFFF] = in_l;
                        sb->dsp.record_buffer[(c_record+1)&0xFFFF] = in_r;
                        #ifdef SB_DSP_RECORD_DEBUG
                                if (c_record != last_crecord)
                                {
                                        if (!soundfsbin) soundfsbin=plat_fopen(L"sound_sb_in.pcm",L"wb");
                                        fwrite(&sb->dsp.record_buffer[c_record&0xFFFF],2,2,soundfsbin);
                                        last_crecord=c_record;
                                }
                        #endif
                }

                buffer[c]     += (out_l << mixer->output_gain_L);
                buffer[c + 1] += (out_r << mixer->output_gain_R);
        }
        #ifdef SB_DSP_RECORD_DEBUG
        if (old_dsp_rec_pos > dsp_rec_pos)
        {
                buf_written=0;
                old_dsp_rec_pos=dsp_rec_pos;
        }
        #endif
        
        sb->dsp.record_pos_write+=((len * sb->dsp.sb_freq) / 48000)*2;
        sb->dsp.record_pos_write&=0xFFFF;
        sb->pos = 0;
        sb->opl.pos = 0;
        sb->dsp.pos = 0;
        sb->emu8k.pos = 0;
#if 0
	memcpy(&sb->temp_mixer_sb16, &sb->mixer_sb16, sizeof(sb_ct1745_mixer_t));
#endif
}


void sb_ct1335_mixer_write(uint16_t addr, uint8_t val, void *p)
{
        sb_t *sb = (sb_t *)p;
        sb_ct1335_mixer_t *mixer = &sb->mixer_sb2;
        
        if (!(addr & 1))
        {
                mixer->index = val;
                mixer->regs[0x01] = val;
        }
        else
        {
                if (mixer->index == 0)
                {
                        /* Reset */
                        mixer->regs[0x02] = 4 << 1;
                        mixer->regs[0x06] = 4 << 1;
                        mixer->regs[0x08] = 0 << 1;
                        /* changed default from -46dB to 0dB*/
                        mixer->regs[0x0A] = 3 << 1;
                }
                else
                {
                        mixer->regs[mixer->index] = val;
                        switch (mixer->index)
                        {
                                case 0x00: case 0x02: case 0x06: case 0x08: case 0x0A:
                                break;

                                default:
                                sb_log("sb_ct1335: Unknown register WRITE: %02X\t%02X\n", mixer->index, mixer->regs[mixer->index]);
                                break;
                        }
                }
                mixer->master = sb_att_4dbstep_3bits[(mixer->regs[0x02] >> 1)&0x7];
                mixer->fm     = sb_att_4dbstep_3bits[(mixer->regs[0x06] >> 1)&0x7];
                mixer->cd     = sb_att_4dbstep_3bits[(mixer->regs[0x08] >> 1)&0x7];
                mixer->voice  = sb_att_7dbstep_2bits[(mixer->regs[0x0A] >> 1)&0x3];

                sound_set_cd_volume(((uint32_t)mixer->master * (uint32_t)mixer->cd) / 65535,
                                    ((uint32_t)mixer->master * (uint32_t)mixer->cd) / 65535);
        }
}

uint8_t sb_ct1335_mixer_read(uint16_t addr, void *p)
{
        sb_t *sb = (sb_t *)p;
        sb_ct1335_mixer_t *mixer = &sb->mixer_sb2;

        if (!(addr & 1))
                return mixer->index;

        switch (mixer->index)
        {
                case 0x00: case 0x02: case 0x06: case 0x08: case 0x0A:
                return mixer->regs[mixer->index];
                default:
                sb_log("sb_ct1335: Unknown register READ: %02X\t%02X\n", mixer->index, mixer->regs[mixer->index]);
                break;
        }

        return 0xff;
}

void sb_ct1335_mixer_reset(sb_t* sb)
{
        sb_ct1335_mixer_write(0x254,0,sb);
        sb_ct1335_mixer_write(0x255,0,sb);
}

void sb_ct1345_mixer_write(uint16_t addr, uint8_t val, void *p)
{
        sb_t *sb = (sb_t *)p;
        sb_ct1345_mixer_t *mixer = &sb->mixer_sbpro;
        
        if (!(addr & 1))
        {
                mixer->index = val;
                mixer->regs[0x01] = val;
        }
        else
        {
                if (mixer->index == 0)
                {
                        /* Reset */
                        mixer->regs[0x0A] = 0 << 1;
                        mixer->regs[0x0C] = (0 << 5) | (0 << 3) | (0 << 1);
                        mixer->regs[0x0E] = (0 << 5) | (0 << 1);
                        /* changed default from -11dB to 0dB */
                        mixer->regs[0x04] = (7 << 5) | (7 << 1);
                        mixer->regs[0x22] = (7 << 5) | (7 << 1);
                        mixer->regs[0x26] = (7 << 5) | (7 << 1);
                        mixer->regs[0x28] = (7 << 5) | (7 << 1);
                        mixer->regs[0x2E] = (0 << 5) | (0 << 1);
                        sb_dsp_set_stereo(&sb->dsp, mixer->regs[0x0E] & 2);
                }
                else
                {
                        mixer->regs[mixer->index] = val;
                        switch (mixer->index)
                        {
                                /* Compatibility: chain registers 0x02 and 0x22 as well as 0x06 and 0x26 */
                                case 0x02: case 0x06: case 0x08:
                                mixer->regs[mixer->index+0x20]=((val&0xE) << 4)|(val&0xE) << 4;
                                break;
                                
                                case 0x22: case 0x26: case 0x28:
                                mixer->regs[mixer->index-0x20]=(val&0xE);
                                break;
                                
                                /* More compatibility:  SoundBlaster Pro selects register 020h for 030h, 022h for 032h, 026h for 036h,028h for 038h. */
                                case 0x30: case 0x32: case 0x36: case 0x38:
                                mixer->regs[mixer->index-0x10]=(val&0xEE);
                                break;

                                case 0x00: case 0x04: case 0x0a: case 0x0c: case 0x0e:
                                case 0x2e:
                                break;
                                
                                
                                default:
                                sb_log("sb_ct1345: Unknown register WRITE: %02X\t%02X\n", mixer->index, mixer->regs[mixer->index]);
                                break;
                        }
                }
  
                mixer->voice_l  = sb_att_4dbstep_3bits[(mixer->regs[0x04] >> 5)&0x7];
                mixer->voice_r  = sb_att_4dbstep_3bits[(mixer->regs[0x04] >> 1)&0x7];
                mixer->master_l = sb_att_4dbstep_3bits[(mixer->regs[0x22] >> 5)&0x7];
                mixer->master_r = sb_att_4dbstep_3bits[(mixer->regs[0x22] >> 1)&0x7];
                mixer->fm_l     = sb_att_4dbstep_3bits[(mixer->regs[0x26] >> 5)&0x7];
                mixer->fm_r     = sb_att_4dbstep_3bits[(mixer->regs[0x26] >> 1)&0x7];
                mixer->cd_l     = sb_att_4dbstep_3bits[(mixer->regs[0x28] >> 5)&0x7];
                mixer->cd_r     = sb_att_4dbstep_3bits[(mixer->regs[0x28] >> 1)&0x7];
                mixer->line_l     = sb_att_4dbstep_3bits[(mixer->regs[0x2E] >> 5)&0x7];
                mixer->line_r     = sb_att_4dbstep_3bits[(mixer->regs[0x2E] >> 1)&0x7];

                mixer->mic      = sb_att_7dbstep_2bits[(mixer->regs[0x0A] >> 1)&0x3];

                mixer->output_filter   = !(mixer->regs[0xE] & 0x20);
                mixer->input_filter    = !(mixer->regs[0xC] & 0x20);
                mixer->in_filter_freq  = ((mixer->regs[0xC] & 0x8) == 0) ? 3200 : 8800;
                mixer->stereo = mixer->regs[0xE] & 2;
                if (mixer->index == 0xE)
                        sb_dsp_set_stereo(&sb->dsp, val & 2);

                switch ((mixer->regs[0xc]&6))
                {
                        case 2: 
                                mixer->input_selector = INPUT_CD_L|INPUT_CD_R;
                                break;
                        case 6: 
                                mixer->input_selector = INPUT_LINE_L|INPUT_LINE_R; 
                                break;
                        default: 
                                mixer->input_selector = INPUT_MIC;
                                break;
                }
                
                /* TODO: pcspeaker volume? Or is it not worth? */
                sound_set_cd_volume(((uint32_t)mixer->master_l * (uint32_t)mixer->cd_l) / 65535,
                                    ((uint32_t)mixer->master_r * (uint32_t)mixer->cd_r) / 65535);
        }
}

uint8_t sb_ct1345_mixer_read(uint16_t addr, void *p)
{
        sb_t *sb = (sb_t *)p;
        sb_ct1345_mixer_t *mixer = &sb->mixer_sbpro;

        if (!(addr & 1))
                return mixer->index;

        switch (mixer->index)
        {
                case 0x00: case 0x04: case 0x0a: case 0x0c: case 0x0e:
                case 0x22: case 0x26: case 0x28: case 0x2e: case 0x02: case 0x06:
                case 0x30: case 0x32: case 0x36: case 0x38:
                return mixer->regs[mixer->index];
                
                default:
                sb_log("sb_ct1345: Unknown register READ: %02X\t%02X\n", mixer->index, mixer->regs[mixer->index]);
                break;
        }
        
        return 0xff;
}
void sb_ct1345_mixer_reset(sb_t* sb)
{
        sb_ct1345_mixer_write(4,0,sb);
        sb_ct1345_mixer_write(5,0,sb);
}

void sb_ct1745_mixer_write(uint16_t addr, uint8_t val, void *p)
{
        sb_t *sb = (sb_t *)p;
        sb_ct1745_mixer_t *mixer = &sb->mixer_sb16;
        
        if (!(addr & 1))
        {
                mixer->index = val;
        }
        else
        {
            // TODO: and this?  001h:
            /*DESCRIPTION
     Contains previously selected register value.  Mixer Data Register value
         NOTES
     * SoundBlaster 16 sets bit 7 if previous mixer index invalid.
     * Status bytes initially 080h on startup for all but level bytes (SB16)
             */
            
                if (mixer->index == 0)
                {
                        /* Reset */
                        /* Changed defaults from -14dB to 0dB*/
                        mixer->regs[0x30]=31 << 3;
                        mixer->regs[0x31]=31 << 3;
                        mixer->regs[0x32]=31 << 3;
                        mixer->regs[0x33]=31 << 3;
                        mixer->regs[0x34]=31 << 3;
                        mixer->regs[0x35]=31 << 3;
                        mixer->regs[0x36]=31 << 3;
                        mixer->regs[0x37]=31 << 3;
                        mixer->regs[0x38]=0 << 3;
                        mixer->regs[0x39]=0 << 3;

                        mixer->regs[0x3A]=0 << 3;

                        mixer->regs[0x3B]=0 << 6;
                        mixer->regs[0x3C] = OUTPUT_MIC|OUTPUT_CD_R|OUTPUT_CD_L|OUTPUT_LINE_R|OUTPUT_LINE_L;
                        mixer->regs[0x3D] = INPUT_MIC|INPUT_CD_L|INPUT_LINE_L|INPUT_MIDI_L;
                        mixer->regs[0x3E] = INPUT_MIC|INPUT_CD_R|INPUT_LINE_R|INPUT_MIDI_R;
        
                        mixer->regs[0x3F] = mixer->regs[0x40] = 0 << 6;
                        mixer->regs[0x41] = mixer->regs[0x42] = 0 << 6;

                        mixer->regs[0x44] = mixer->regs[0x45] = 8 << 4;
                        mixer->regs[0x46] = mixer->regs[0x47] = 8 << 4;
                        
                        mixer->regs[0x43] = 0;
                }
                else
                {
                        mixer->regs[mixer->index] = val;
                }
                switch (mixer->index)
                {
                        /* SBPro compatibility. Copy values to sb16 registers. */
                        case 0x22:
                        mixer->regs[0x30] = (mixer->regs[0x22] & 0xF0) | 0x8;
                        mixer->regs[0x31] = ((mixer->regs[0x22] & 0xf) << 4) | 0x8;
                        break;
                        case 0x04:
                        mixer->regs[0x32] = (mixer->regs[0x04] & 0xF0) | 0x8;
                        mixer->regs[0x33] = ((mixer->regs[0x04] & 0xf) << 4) | 0x8;
                        break;
                        case 0x26:
                        mixer->regs[0x34] = (mixer->regs[0x26] & 0xF0) | 0x8;
                        mixer->regs[0x35] = ((mixer->regs[0x26] & 0xf) << 4) | 0x8;
                        break;
                        case 0x28:
                        mixer->regs[0x36] = (mixer->regs[0x28] & 0xF0) | 0x8;
                        mixer->regs[0x37] = ((mixer->regs[0x28] & 0xf) << 4) | 0x8;
                        break;
                        case 0x0A:
                        mixer->regs[0x3A] = (mixer->regs[0x0A]*3)+10;
                        break;
                        case 0x2E:
                        mixer->regs[0x38] = (mixer->regs[0x2E] & 0xF0) | 0x8;
                        mixer->regs[0x39] = ((mixer->regs[0x2E] & 0xf) << 4) | 0x8;
                        break;

                        /*
                         (DSP 4.xx feature) The Interrupt Setup register, addressed as register 80h on the Mixer register map, is used to configure or determine the Interrupt request line. The DMA setup register, addressed as register 81h on the Mixer register map, is used to configure or determine the DMA channels.
                          
                         Note: Registers 80h and 81h are Read-only for PnP boards.
                         */
                        case 0x80:
                        if (val & 1) sb_dsp_setirq(&sb->dsp,2);
                        if (val & 2) sb_dsp_setirq(&sb->dsp,5);
                        if (val & 4) sb_dsp_setirq(&sb->dsp,7);
                        if (val & 8) sb_dsp_setirq(&sb->dsp,10);
                        break;

                        case 0x81:
                        /* The documentation is confusing. sounds as if multple dma8 channels could be set. */
                        if (val & 1) sb_dsp_setdma8(&sb->dsp,0);
                        if (val & 2) sb_dsp_setdma8(&sb->dsp,1);
                        if (val & 8) sb_dsp_setdma8(&sb->dsp,3);
                        if (val & 0x20) sb_dsp_setdma16(&sb->dsp,5);
                        if (val & 0x40) sb_dsp_setdma16(&sb->dsp,6);
                        if (val & 0x80) sb_dsp_setdma16(&sb->dsp,7);
                        break;
                }

                mixer->output_selector = mixer->regs[0x3C];
                mixer->input_selector_left = mixer->regs[0x3D];
                mixer->input_selector_right = mixer->regs[0x3E];

                mixer->master_l = sb_att_2dbstep_5bits[mixer->regs[0x30] >> 3];
                mixer->master_r = sb_att_2dbstep_5bits[mixer->regs[0x31] >> 3];
                mixer->voice_l  = sb_att_2dbstep_5bits[mixer->regs[0x32] >> 3];
                mixer->voice_r  = sb_att_2dbstep_5bits[mixer->regs[0x33] >> 3];
                mixer->fm_l     = sb_att_2dbstep_5bits[mixer->regs[0x34] >> 3];
                mixer->fm_r     = sb_att_2dbstep_5bits[mixer->regs[0x35] >> 3];
                mixer->cd_l     = (mixer->output_selector&OUTPUT_CD_L) ? sb_att_2dbstep_5bits[mixer->regs[0x36] >> 3] : 0;
                mixer->cd_r     = (mixer->output_selector&OUTPUT_CD_R) ? sb_att_2dbstep_5bits[mixer->regs[0x37] >> 3] : 0;
                mixer->line_l   = (mixer->output_selector&OUTPUT_LINE_L) ? sb_att_2dbstep_5bits[mixer->regs[0x38] >> 3] : 0;
                mixer->line_r   = (mixer->output_selector&OUTPUT_LINE_R) ? sb_att_2dbstep_5bits[mixer->regs[0x39] >> 3] : 0;

                mixer->mic      = sb_att_2dbstep_5bits[mixer->regs[0x3A] >> 3];
                mixer->speaker  = sb_att_2dbstep_5bits[mixer->regs[0x3B]*3 + 22];
                

                mixer->input_gain_L = (mixer->regs[0x3F] >> 6);
                mixer->input_gain_R = (mixer->regs[0x40] >> 6);
                mixer->output_gain_L = (mixer->regs[0x41] >> 6);
                mixer->output_gain_R = (mixer->regs[0x42] >> 6);

                mixer->bass_l   = mixer->regs[0x46] >> 4;
                mixer->bass_r   = mixer->regs[0x47] >> 4;
                mixer->treble_l = mixer->regs[0x44] >> 4;
                mixer->treble_r = mixer->regs[0x45] >> 4;

                /*TODO: pcspeaker volume, with "output_selector" check? or better not? */
                sound_set_cd_volume(((uint32_t)mixer->master_l * (uint32_t)mixer->cd_l) / 65535,
                                    ((uint32_t)mixer->master_r * (uint32_t)mixer->cd_r) / 65535);
                sb_log("sb_ct1745: Received register WRITE: %02X\t%02X\n", mixer->index, mixer->regs[mixer->index]);
        }
}

uint8_t sb_ct1745_mixer_read(uint16_t addr, void *p)
{
        sb_t *sb = (sb_t *)p;
        sb_ct1745_mixer_t *mixer = &sb->mixer_sb16;
	uint8_t temp;

        if (!(addr & 1))
                return mixer->index;

        sb_log("sb_ct1745: received register READ: %02X\t%02X\n", mixer->index, mixer->regs[mixer->index]);

        if (mixer->index>=0x30 && mixer->index<=0x47)
        {
                return mixer->regs[mixer->index];
        }
        switch (mixer->index)
        {
                case 0x00:
                return mixer->regs[mixer->index];

                /*SB Pro compatibility*/                        
                case 0x04:
                return ((mixer->regs[0x33] >> 4) & 0x0f) | (mixer->regs[0x32] & 0xf0);
                case 0x0a:
                return (mixer->regs[0x3a] - 10) / 3;
                case 0x22:
                return ((mixer->regs[0x31] >> 4) & 0x0f) | (mixer->regs[0x30] & 0xf0);
                case 0x26:
                return ((mixer->regs[0x35] >> 4) & 0x0f) | (mixer->regs[0x34] & 0xf0);
                case 0x28:
                return ((mixer->regs[0x37] >> 4) & 0x0f) | (mixer->regs[0x36] & 0xf0);
                case 0x2e:
                return ((mixer->regs[0x39] >> 4) & 0x0f) | (mixer->regs[0x38] & 0xf0);
                
                case 0x48: 
                // Undocumented. The Creative Windows Mixer calls this after calling 3C (input selector). even when writing.
                // Also, the version I have (5.17) does not use the MIDI.L/R input selectors. it uses the volume to mute (Affecting the output, obviously)
                return mixer->regs[mixer->index];

                case 0x80:
                /*TODO: Unaffected by mixer reset or soft reboot.
                 * Enabling multiple bits enables multiple IRQs.
                 */
                    
                switch (sb->dsp.sb_irqnum)
                {
                        case 2: return 1;
                        case 5: return 2;
                        case 7: return 4;
                        case 10: return 8;
                }
                break;
                
                case 0x81:
                {                
                /* TODO: Unaffected by mixer reset or soft reboot.
                * Enabling multiple 8 or 16-bit DMA bits enables multiple DMA channels.
                * Disabling all 8-bit DMA channel bits disables 8-bit DMA requests,
                    including translated 16-bit DMA requests.
                * Disabling all 16-bit DMA channel bits enables translation of 16-bit DMA
                    requests to 8-bit ones, using the selected 8-bit DMA channel.*/
                
                        uint8_t result=0;
                        switch (sb->dsp.sb_8_dmanum)
                        {
                            case 0: result |= 1; break;
                            case 1: result |= 2; break;
                            case 3: result |= 8; break;
                        }
                        switch (sb->dsp.sb_16_dmanum)
                        {
                            case 5: result |= 0x20; break;
                            case 6: result |= 0x40; break;
                            case 7: result |= 0x80; break;
                        }                            
                        return result;
                }

                /* The Interrupt status register, addressed as register 82h on the Mixer register map,
                 is used by the ISR to determine whether the interrupt is meant for it or for some other ISR,
                 in which case it should chain to the previous routine.
                 */
                case 0x82:
                /* 0 = none, 1 =  digital 8bit or SBMIDI, 2 = digital 16bit, 4 = MPU-401 */
                /* 0x02000 DSP v4.04, 0x4000 DSP v4.05 0x8000 DSP v4.12. I haven't seen this making any difference, but I'm keeping it for now. */
		temp = ((sb->dsp.sb_irq8) ? 1 : 0) | ((sb->dsp.sb_irq16) ? 2 : 0) | 0x4000;
		if (sb->mpu)
			temp |= ((sb->mpu->state.irq_pending) ? 4 : 0);
                return temp;

                /* TODO: creative drivers read and write on 0xFE and 0xFF. not sure what they are supposed to be. */
                
                
                default:
                sb_log("sb_ct1745: Unknown register READ: %02X\t%02X\n", mixer->index, mixer->regs[mixer->index]);
                break;
        }

        return 0xff;
}

void sb_ct1745_mixer_reset(sb_t* sb)
{
        sb_ct1745_mixer_write(4,0,sb);
        sb_ct1745_mixer_write(5,0,sb);
}


static uint16_t sb_mcv_addr[8] = {0x200, 0x210, 0x220, 0x230, 0x240, 0x250, 0x260, 0x270};

uint8_t sb_mcv_read(int port, void *p)
{
        sb_t *sb = (sb_t *)p;

        sb_log("sb_mcv_read: port=%04x\n", port);

        return sb->pos_regs[port & 7];
}

void sb_mcv_write(int port, uint8_t val, void *p)
{
        uint16_t addr;
        sb_t *sb = (sb_t *)p;

        if (port < 0x102)
                return;
        
        sb_log("sb_mcv_write: port=%04x val=%02x\n", port, val);

        addr = sb_mcv_addr[sb->pos_regs[4] & 7];
	if (sb->opl_enabled) {
        	io_removehandler(addr+8, 0x0002, opl2_read, NULL, NULL, opl2_write, NULL, NULL, &sb->opl);
        	io_removehandler(0x0388, 0x0002, opl2_read, NULL, NULL, opl2_write, NULL, NULL, &sb->opl);
	}
        /* DSP I/O handler is activated in sb_dsp_setaddr */
        sb_dsp_setaddr(&sb->dsp, 0);

        sb->pos_regs[port & 7] = val;

        if (sb->pos_regs[2] & 1)
        {
                addr = sb_mcv_addr[sb->pos_regs[4] & 7];
                
		if (sb->opl_enabled) {
                	io_sethandler(addr+8, 0x0002, opl2_read, NULL, NULL, opl2_write, NULL, NULL, &sb->opl);
                	io_sethandler(0x0388, 0x0002, opl2_read, NULL, NULL, opl2_write, NULL, NULL, &sb->opl);
		}
                /* DSP I/O handler is activated in sb_dsp_setaddr */
                sb_dsp_setaddr(&sb->dsp, addr);
        }
}

static int sb_pro_mcv_irqs[4] = {7, 5, 3, 3};

uint8_t sb_pro_mcv_read(int port, void *p)
{
        sb_t *sb = (sb_t *)p;

        sb_log("sb_pro_mcv_read: port=%04x\n", port);

        return sb->pos_regs[port & 7];
}

void sb_pro_mcv_write(int port, uint8_t val, void *p)
{
        uint16_t addr;
        sb_t *sb = (sb_t *)p;

        if (port < 0x102)
                return;

        sb_log("sb_pro_mcv_write: port=%04x val=%02x\n", port, val);

        addr = (sb->pos_regs[2] & 0x20) ? 0x220 : 0x240;
        io_removehandler(addr+0, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        io_removehandler(addr+8, 0x0002, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        io_removehandler(0x0388, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        io_removehandler(addr+4, 0x0002, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, sb);
        /* DSP I/O handler is activated in sb_dsp_setaddr */
        sb_dsp_setaddr(&sb->dsp, 0);

        sb->pos_regs[port & 7] = val;

        if (sb->pos_regs[2] & 1)
        {
                addr = (sb->pos_regs[2] & 0x20) ? 0x220 : 0x240;

                io_sethandler(addr+0, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
                io_sethandler(addr+8, 0x0002, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
                io_sethandler(0x0388, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
                io_sethandler(addr+4, 0x0002, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, sb);
                /* DSP I/O handler is activated in sb_dsp_setaddr */
                sb_dsp_setaddr(&sb->dsp, addr);
        }
        sb_dsp_setirq(&sb->dsp, sb_pro_mcv_irqs[(sb->pos_regs[5] >> 4) & 3]);
        sb_dsp_setdma8(&sb->dsp, sb->pos_regs[4] & 3);
}

void *sb_1_init()
{
        /*sb1/2 port mappings, 210h to 260h in 10h steps
          2x0 to 2x3 -> CMS chip
          2x6, 2xA, 2xC, 2xE -> DSP chip
          2x8, 2x9, 388 and 389 FM chip*/
        sb_t *sb = malloc(sizeof(sb_t));
        uint16_t addr = device_get_config_hex16("base");        
        memset(sb, 0, sizeof(sb_t));
        
	sb->opl_enabled = device_get_config_int("opl");
	if (sb->opl_enabled)
        	opl2_init(&sb->opl);
        sb_dsp_init(&sb->dsp, SB1);
        sb_dsp_setaddr(&sb->dsp, addr);
        sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
        sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
        /* CMS I/O handler is activated on the dedicated sound_cms module
           DSP I/O handler is activated in sb_dsp_setaddr */
	if (sb->opl_enabled) {
        	io_sethandler(addr+8, 0x0002, opl2_read, NULL, NULL, opl2_write, NULL, NULL, &sb->opl);
        	io_sethandler(0x0388, 0x0002, opl2_read, NULL, NULL, opl2_write, NULL, NULL, &sb->opl);
	}
        sound_add_handler(sb_get_buffer_sb2, sb);
        return sb;
}
void *sb_15_init()
{
        /*sb1/2 port mappings, 210h to 260h in 10h steps
          2x0 to 2x3 -> CMS chip
          2x6, 2xA, 2xC, 2xE -> DSP chip
          2x8, 2x9, 388 and 389 FM chip*/
        sb_t *sb = malloc(sizeof(sb_t));
        uint16_t addr = device_get_config_hex16("base");
        memset(sb, 0, sizeof(sb_t));

	sb->opl_enabled = device_get_config_int("opl");
	if (sb->opl_enabled)
        	opl2_init(&sb->opl);
        sb_dsp_init(&sb->dsp, SB15);
        sb_dsp_setaddr(&sb->dsp, addr);
        sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
        sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
        /* CMS I/O handler is activated on the dedicated sound_cms module
           DSP I/O handler is activated in sb_dsp_setaddr */
	if (sb->opl_enabled) {
        	io_sethandler(addr+8, 0x0002, opl2_read, NULL, NULL, opl2_write, NULL, NULL, &sb->opl);
        	io_sethandler(0x0388, 0x0002, opl2_read, NULL, NULL, opl2_write, NULL, NULL, &sb->opl);
	}
        sound_add_handler(sb_get_buffer_sb2, sb);
        return sb;
}

void *sb_mcv_init()
{
        /*sb1/2 port mappings, 210h to 260h in 10h steps
          2x6, 2xA, 2xC, 2xE -> DSP chip
          2x8, 2x9, 388 and 389 FM chip*/
        sb_t *sb = malloc(sizeof(sb_t));
        memset(sb, 0, sizeof(sb_t));

	sb->opl_enabled = device_get_config_int("opl");
	if (sb->opl_enabled)
        	opl2_init(&sb->opl);
        sb_dsp_init(&sb->dsp, SB15);
        sb_dsp_setaddr(&sb->dsp, 0);//addr);
        sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
        sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
        sound_add_handler(sb_get_buffer_sb2, sb);
        /* I/O handlers activated in sb_mcv_write */
        mca_add(sb_mcv_read, sb_mcv_write, sb);
        sb->pos_regs[0] = 0x84;
        sb->pos_regs[1] = 0x50;
        return sb;
}
void *sb_2_init()
{
        /*sb2 port mappings. 220h or 240h.
          2x0 to 2x3 -> CMS chip
          2x6, 2xA, 2xC, 2xE -> DSP chip
          2x8, 2x9, 388 and 389 FM chip
        "CD version" also uses 250h or 260h for
          2x0 to 2x3 -> CDROM interface
          2x4 to 2x5 -> Mixer interface*/
        /*My SB 2.0 mirrors the OPL2 at ports 2x0/2x1. Presumably this mirror is
          disabled when the CMS chips are present.
          This mirror may also exist on SB 1.5 & MCV, however I am unable to
          test this. It shouldn't exist on SB 1.0 as the CMS chips are always
          present there.
          Syndicate requires this mirror for music to play.*/
        sb_t *sb = malloc(sizeof(sb_t));
        uint16_t addr = device_get_config_hex16("base");
        memset(sb, 0, sizeof(sb_t));

	sb->opl_enabled = device_get_config_int("opl");
	if (sb->opl_enabled)
        	opl2_init(&sb->opl);
        sb_dsp_init(&sb->dsp, SB2);
        sb_dsp_setaddr(&sb->dsp, addr);
        sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
        sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
        sb_ct1335_mixer_reset(sb);
        /* CMS I/O handler is activated on the dedicated sound_cms module
           DSP I/O handler is activated in sb_dsp_setaddr */
	if (sb->opl_enabled) {
		if (!GAMEBLASTER)
			io_sethandler(addr, 0x0002, opl2_read, NULL, NULL, opl2_write, NULL, NULL, &sb->opl);
        	io_sethandler(addr+8, 0x0002, opl2_read, NULL, NULL, opl2_write, NULL, NULL, &sb->opl);
        	io_sethandler(0x0388, 0x0002, opl2_read, NULL, NULL, opl2_write, NULL, NULL, &sb->opl);
	}
        
        int mixer_addr = device_get_config_int("mixaddr");
        if (mixer_addr > 0)
        {
                io_sethandler(mixer_addr+4, 0x0002, sb_ct1335_mixer_read, NULL, NULL, sb_ct1335_mixer_write, NULL, NULL, sb);
                sound_add_handler(sb_get_buffer_sb2_mixer, sb);
        }
        else
                sound_add_handler(sb_get_buffer_sb2, sb);    

        return sb;
}

void *sb_pro_v1_init()
{
        /*sbpro port mappings. 220h or 240h.
          2x0 to 2x3 -> FM chip, Left and Right (9*2 voices)
          2x4 to 2x5 -> Mixer interface
          2x6, 2xA, 2xC, 2xE -> DSP chip
          2x8, 2x9, 388 and 389 FM chip (9 voices)
          2x0+10 to 2x0+13 CDROM interface.*/
        sb_t *sb = malloc(sizeof(sb_t));
        uint16_t addr = device_get_config_hex16("base");
        memset(sb, 0, sizeof(sb_t));

	sb->opl_enabled = device_get_config_int("opl");
	if (sb->opl_enabled)
        	opl2_init(&sb->opl);
        sb_dsp_init(&sb->dsp, SBPRO);
        sb_dsp_setaddr(&sb->dsp, addr);
        sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
        sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
        sb_ct1345_mixer_reset(sb);
        /* DSP I/O handler is activated in sb_dsp_setaddr */
	if (sb->opl_enabled) {
        	io_sethandler(addr+0, 0x0002, opl2_l_read, NULL, NULL, opl2_l_write, NULL, NULL, &sb->opl);
        	io_sethandler(addr+2, 0x0002, opl2_r_read, NULL, NULL, opl2_r_write, NULL, NULL, &sb->opl);
        	io_sethandler(addr+8, 0x0002, opl2_read,   NULL, NULL, opl2_write,   NULL, NULL, &sb->opl);
        	io_sethandler(0x0388, 0x0002, opl2_read,   NULL, NULL, opl2_write,   NULL, NULL, &sb->opl);
	}
        io_sethandler(addr+4, 0x0002, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, sb);
        sound_add_handler(sb_get_buffer_sbpro, sb);

        return sb;
}

void *sb_pro_v2_init()
{
        /*sbpro port mappings. 220h or 240h.
          2x0 to 2x3 -> FM chip (18 voices)
          2x4 to 2x5 -> Mixer interface
          2x6, 2xA, 2xC, 2xE -> DSP chip
          2x8, 2x9, 388 and 389 FM chip (9 voices)
          2x0+10 to 2x0+13 CDROM interface.*/
        sb_t *sb = malloc(sizeof(sb_t));
        uint16_t addr = device_get_config_hex16("base");
        memset(sb, 0, sizeof(sb_t));

	sb->opl_enabled = device_get_config_int("opl");
	if (sb->opl_enabled)
        	opl3_init(&sb->opl);
        sb_dsp_init(&sb->dsp, SBPRO2);
        sb_dsp_setaddr(&sb->dsp, addr);
        sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
        sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
        sb_ct1345_mixer_reset(sb);
        /* DSP I/O handler is activated in sb_dsp_setaddr */
	if (sb->opl_enabled) {
        	io_sethandler(addr+0, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        	io_sethandler(addr+8, 0x0002, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        	io_sethandler(0x0388, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
	}
        io_sethandler(addr+4, 0x0002, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, sb);
        sound_add_handler(sb_get_buffer_sbpro, sb);

        return sb;
}

void *sb_pro_mcv_init()
{
        /*sbpro port mappings. 220h or 240h.
          2x0 to 2x3 -> FM chip, Left and Right (18 voices)
          2x4 to 2x5 -> Mixer interface
          2x6, 2xA, 2xC, 2xE -> DSP chip
          2x8, 2x9, 388 and 389 FM chip (9 voices)*/
        sb_t *sb = malloc(sizeof(sb_t));
        memset(sb, 0, sizeof(sb_t));

	sb->opl_enabled = 1;
        opl3_init(&sb->opl);
        sb_dsp_init(&sb->dsp, SBPRO2);
        sb_ct1345_mixer_reset(sb);
        /* I/O handlers activated in sb_mcv_write */
        sound_add_handler(sb_get_buffer_sbpro, sb);

        /* I/O handlers activated in sb_pro_mcv_write */
        mca_add(sb_pro_mcv_read, sb_pro_mcv_write, sb);
        sb->pos_regs[0] = 0x03;
        sb->pos_regs[1] = 0x51;

        return sb;
}

void *sb_16_init()
{
        sb_t *sb = malloc(sizeof(sb_t));
        uint16_t addr = device_get_config_hex16("base");
        uint16_t mpu_addr = device_get_config_hex16("base401");
        memset(sb, 0, sizeof(sb_t));

	sb->opl_enabled = device_get_config_int("opl");
	if (sb->opl_enabled)
	        opl3_init(&sb->opl);
        sb_dsp_init(&sb->dsp, SB16);
        sb_dsp_setaddr(&sb->dsp, addr);
        sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
        sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
        sb_dsp_setdma16(&sb->dsp, device_get_config_int("dma16"));
        sb_ct1745_mixer_reset(sb);
	if (sb->opl_enabled) {
        	io_sethandler(addr, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        	io_sethandler(addr+8, 0x0002, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        	io_sethandler(0x0388, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
	}
        io_sethandler(addr+4, 0x0002, sb_ct1745_mixer_read, NULL, NULL, sb_ct1745_mixer_write, NULL, NULL, sb);
        sound_add_handler(sb_get_buffer_sb16, sb);
#if 0
        sound_add_process_handler(sb_process_buffer_sb16, sb);
#endif
	if (mpu_addr) {
		sb->mpu = (mpu_t *) malloc(sizeof(mpu_t));
		memset(sb->mpu, 0, sizeof(mpu_t));
        	mpu401_init(sb->mpu, device_get_config_hex16("base401"), device_get_config_int("irq"), M_UART);
		sb_dsp_set_mpu(sb->mpu);
	} else
		sb->mpu = NULL;
#if 0
	memcpy(&sb->temp_mixer_sb16, &sb->mixer_sb16, sizeof(sb_ct1745_mixer_t));
#endif

        return sb;
}

int sb_awe32_available()
{
        return rom_present(L"roms/sound/awe32.raw");
}

void *sb_awe32_init()
{
        sb_t *sb = malloc(sizeof(sb_t));
        uint16_t addr = device_get_config_hex16("base");
        uint16_t mpu_addr = device_get_config_hex16("base401");
        uint16_t emu_addr = device_get_config_hex16("emu_base");
        int onboard_ram = device_get_config_int("onboard_ram");
        memset(sb, 0, sizeof(sb_t));


	sb->opl_enabled = device_get_config_int("opl");
	if (sb->opl_enabled)
        	opl3_init(&sb->opl);

        sb_dsp_init(&sb->dsp, SB16 + 1);
        sb_dsp_setaddr(&sb->dsp, addr);
        sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
        sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
        sb_dsp_setdma16(&sb->dsp, device_get_config_int("dma16"));
        sb_ct1745_mixer_reset(sb);
	if (sb->opl_enabled) {
        	io_sethandler(addr, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
	        io_sethandler(addr+8, 0x0002, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        	io_sethandler(0x0388, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
	}
        io_sethandler(addr+4, 0x0002, sb_ct1745_mixer_read, NULL, NULL, sb_ct1745_mixer_write, NULL, NULL, sb);
        sound_add_handler(sb_get_buffer_emu8k, sb);
#if 0
        sound_add_process_handler(sb_process_buffer_sb16, sb);
#endif
	if (mpu_addr) {
		sb->mpu = (mpu_t *) malloc(sizeof(mpu_t));
		memset(sb->mpu, 0, sizeof(mpu_t));
	        mpu401_init(sb->mpu, device_get_config_hex16("base401"), device_get_config_int("irq"), M_UART);
		sb_dsp_set_mpu(sb->mpu);
	} else
		sb->mpu = NULL;
        emu8k_init(&sb->emu8k, emu_addr, onboard_ram);
#if 0
	memcpy(&sb->temp_mixer_sb16, &sb->mixer_sb16, sizeof(sb_ct1745_mixer_t));
#endif

        return sb;
}

void sb_close(void *p)
{
        sb_t *sb = (sb_t *)p;
        sb_dsp_close(&sb->dsp);
        #ifdef SB_DSP_RECORD_DEBUG
            if (soundfsb != 0)
            {
                fclose(soundfsb);
                soundfsb=0;
            }
            if (soundfsbin!= 0)
            {
                fclose(soundfsbin);
                soundfsbin=0;
            }            
        #endif
        
        free(sb);
}

void sb_awe32_close(void *p)
{
        sb_t *sb = (sb_t *)p;
        
        emu8k_close(&sb->emu8k);

        sb_close(sb);
}

void sb_speed_changed(void *p)
{
        sb_t *sb = (sb_t *)p;
        
        sb_dsp_speed_changed(&sb->dsp);
}

static const device_config_t sb_config[] =
{
        {
                "base", "Address", CONFIG_HEX16, "", 0x220,
                {
                        {
                                "0x220", 0x220
                        },
                        {
                                "0x240", 0x240
                        },
                        {
                                ""
                        }
                }
        },
        {
                "irq", "IRQ", CONFIG_SELECTION, "", 7,
                {
                        {
                                "IRQ 2", 2
                        },
                        {
                                "IRQ 3", 3
                        },
                        {
                                "IRQ 5", 5
                        },
                        {
                                "IRQ 7", 7
                        },
                        {
                                ""
                        }
                }
        },
        {
                "dma", "DMA", CONFIG_SELECTION, "", 1,
                {
                        {
                                "DMA 1", 1
                        },
                        {
                                "DMA 3", 3
                        },
                        {
                                ""
                        }
                }
        },
	{
		"opl", "Enable OPL", CONFIG_BINARY, "", 1
	},
        {
                "", "", -1
        }
};

static const device_config_t sb_mcv_config[] =
{
        {
                "irq", "IRQ", CONFIG_SELECTION, "", 7,
                {
                        {
                                "IRQ 3", 3
                        },
                        {
                                "IRQ 5", 5
                        },
                        {
                                "IRQ 7", 7
                        },
                        {
                                ""
                        }
                }
        },
        {
                "dma", "DMA", CONFIG_SELECTION, "", 1,
                {
                        {
                                "DMA 1", 1
                        },
                        {
                                "DMA 3", 3
                        },
                        {
                                ""
                        }
                }
        },
	{
		"opl", "Enable OPL", CONFIG_BINARY, "", 1
	},
        {
                "", "", -1
        }
};

static const device_config_t sb_pro_config[] =
{
        {
                "base", "Address", CONFIG_HEX16, "", 0x220,
                {
                        {
                                "0x220", 0x220
                        },
                        {
                                "0x240", 0x240
                        },
                        {
                                ""
                        }
                }
        },
        {
                "irq", "IRQ", CONFIG_SELECTION, "", 7,
                {
                        {
                                "IRQ 2", 2
                        },
                        {
                                "IRQ 5", 5
                        },
                        {
                                "IRQ 7", 7
                        },
                        {
                                "IRQ 10", 10
                        },
                        {
                                ""
                        }
                }
        },
        {
                "dma", "DMA", CONFIG_SELECTION, "", 1,
                {
                        {
                                "DMA 1", 1
                        },
                        {
                                "DMA 3", 3
                        },
                        {
                                ""
                        }
                }
        },
	{
		"opl", "Enable OPL", CONFIG_BINARY, "", 1
	},
        {
                "", "", -1
        }
};

static const device_config_t sb_16_config[] =
{
        {
                "base", "Address", CONFIG_HEX16, "", 0x220,
                {
                        {
                                "0x220", 0x220
                        },
                        {
                                "0x240", 0x240
                        },
                        {
                                "0x260", 0x260
                        },
                        {
                                "0x280", 0x280
                        },
                        {
                                ""
                        }
                }
        },
        {
                "base401", "MPU-401 Address", CONFIG_HEX16, "", 0x330,
                {
                        {
                                "Disabled", 0
                        },
                        {
                                "0x300", 0x300
                        },
                        {
                                "0x330", 0x330
                        },
                        {
                                ""
                        }
                }
        },
        {
                "irq", "IRQ", CONFIG_SELECTION, "", 5,
                {
                        {
                                "IRQ 2", 2
                        },
                        {
                                "IRQ 5", 5
                        },
                        {
                                "IRQ 7", 7
                        },
                        {
                                "IRQ 10", 10
                        },
                        {
                                ""
                        }
                }
        },
        {
                "dma", "Low DMA channel", CONFIG_SELECTION, "", 1,
                {
                        {
                                "DMA 0", 0
                        },
                        {
                                "DMA 1", 1
                        },
                        {
                                "DMA 3", 3
                        },
                        {
                                ""
                        }
                }
        },
        {
                "dma16", "High DMA channel", CONFIG_SELECTION, "", 5,
                {
                        {
                                "DMA 5", 5
                        },
                        {
                                "DMA 6", 6
                        },
                        {
                                "DMA 7", 7
                        },
                        {
                                ""
                        }
                }
        },
	{
		"opl", "Enable OPL", CONFIG_BINARY, "", 1
	},
        {
                "", "", -1
        }
};

static const device_config_t sb_awe32_config[] =
{
        {
                "base", "Address", CONFIG_HEX16, "", 0x220,
                {
                        {
                                "0x220", 0x220
                        },
                        {
                                "0x240", 0x240
                        },
                        {
                                "0x260", 0x260
                        },
                        {
                                "0x280", 0x280
                        },
                        {
                                ""
                        }
                }
        },
        {
                "emu_base", "EMU8000 Address", CONFIG_HEX16, "", 0x620,
                {
                        {
                                "0x620", 0x620
                        },
                        {
                                "0x640", 0x640
                        },
                        {
                                "0x660", 0x660
                        },
                        {
                                "0x680", 0x680
                        },
                        {
                                .description = ""
                        }
                }
        },
        {
                "base401", "MPU-401 Address", CONFIG_HEX16, "", 0x330,
                {
                        {
                                "Disabled", 0
                        },
                        {
                                "0x300", 0x300
                        },
                        {
                                "0x330", 0x330
                        },
                        {
                                ""
                        }
                }
        },
        {
                "irq", "IRQ", CONFIG_SELECTION, "", 5,
                {
                        {
                                "IRQ 2", 2
                        },
                        {
                                "IRQ 5", 5
                        },
                        {
                                "IRQ 7", 7
                        },
                        {
                                "IRQ 10", 10
                        },
                        {
                                ""
                        }
                }
        },
        {
                "dma", "Low DMA channel", CONFIG_SELECTION, "", 1,
                {
                        {
                                "DMA 0", 0
                        },
                        {
                                "DMA 1", 1
                        },
                        {
                                "DMA 3", 3
                        },
                        {
                                ""
                        }
                }
        },
        {
                "dma16", "High DMA channel", CONFIG_SELECTION, "", 5,
                {
                        {
                                "DMA 5", 5
                        },
                        {
                                "DMA 6", 6
                        },
                        {
                                "DMA 7", 7
                        },
                        {
                                ""
                        }
                }
        },
        {
                "onboard_ram", "Onboard RAM", CONFIG_SELECTION, "", 512,
                {
                        {
                                "None", 0
                        },
                        {
                                "512 KB", 512
                        },
                        {
                                "2 MB", 2048
                        },
                        {
                                "8 MB", 8192
                        },
                        {
                                "28 MB", 28*1024
                        },
                        {
                                ""
                        }
                }
        },
	{
		"opl", "Enable OPL", CONFIG_BINARY, "", 1
	},
        {
                "", "", -1
        }
};

const device_t sb_1_device =
{
        "Sound Blaster v1.0",
        DEVICE_ISA,
	0,
        sb_1_init, sb_close, NULL, NULL,
        sb_speed_changed,
        NULL,
        sb_config
};
const device_t sb_15_device =
{
        "Sound Blaster v1.5",
        DEVICE_ISA,
	0,
        sb_15_init, sb_close, NULL, NULL,
        sb_speed_changed,
        NULL,
        sb_config
};
const device_t sb_mcv_device =
{
        "Sound Blaster MCV",
        DEVICE_MCA,
	0,
        sb_mcv_init, sb_close, NULL, NULL,
        sb_speed_changed,
        NULL,
        sb_mcv_config
};
const device_t sb_2_device =
{
        "Sound Blaster v2.0",
        DEVICE_ISA,
	0,
        sb_2_init, sb_close, NULL, NULL,
        sb_speed_changed,
        NULL,
        sb_config
};
const device_t sb_pro_v1_device =
{
        "Sound Blaster Pro v1",
        DEVICE_ISA,
	0,
        sb_pro_v1_init, sb_close, NULL, NULL,
        sb_speed_changed,
        NULL,
        sb_pro_config
};
const device_t sb_pro_v2_device =
{
        "Sound Blaster Pro v2",
        DEVICE_ISA,
	0,
        sb_pro_v2_init, sb_close, NULL, NULL,
        sb_speed_changed,
        NULL,
        sb_pro_config
};
const device_t sb_pro_mcv_device =
{
        "Sound Blaster Pro MCV",
        DEVICE_MCA,
	0,
        sb_pro_mcv_init, sb_close, NULL, NULL,
        sb_speed_changed,
        NULL,
        NULL
};
const device_t sb_16_device =
{
        "Sound Blaster 16",
        DEVICE_ISA,
	0,
        sb_16_init, sb_close, NULL, NULL,
        sb_speed_changed,
        NULL,
        sb_16_config
};
const device_t sb_awe32_device =
{
        "Sound Blaster AWE32",
        DEVICE_ISA,
	0,
        sb_awe32_init, sb_close, NULL,
        sb_awe32_available,
        sb_speed_changed,
        NULL,
        sb_awe32_config
};
