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
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/mca.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/gameport.h>
#include <86box/pic.h>
#include <86box/sound.h>
#include <86box/midi.h>
#include <86box/filters.h>
#include <86box/isapnp.h>
#include <86box/snd_sb.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>


/* 0 to 7 -> -14dB to 0dB i 2dB steps. 8 to 15 -> 0 to +14dB in 2dB steps.
  Note that for positive dB values, this is not amplitude, it is amplitude-1. */
static const double sb_bass_treble_4bits[]= {
    0.199526231, 0.25, 0.316227766, 0.398107170, 0.5, 0.63095734, 0.794328234, 1, 
    0, 0.25892541, 0.584893192, 1, 1.511886431, 2.16227766, 3, 4.011872336
};

/* Attenuation tables for the mixer. Max volume = 32767 in order to give 6dB of 
 * headroom and avoid integer overflow */
static const double sb_att_2dbstep_5bits[]=
{
       25.0,    32.0,    41.0,    51.0,    65.0,    82.0,   103.0,   130.0,   164.0,   206.0,
      260.0,   327.0,   412.0,   519.0,   653.0,   822.0,  1036.0,  1304.0,  1641.0,  2067.0,
     2602.0,  3276.0,  4125.0,  5192.0,  6537.0,  8230.0, 10362.0, 13044.0, 16422.0, 20674.0,
    26027.0, 32767.0
};

static const double sb_att_4dbstep_3bits[]=
{
      164.0,  2067.0,  3276.0,  5193.0,  8230.0, 13045.0, 20675.0, 32767.0
};

static const double sb_att_7dbstep_2bits[]=
{
      164.0,  6537.0, 14637.0, 32767.0
};


static const uint16_t sb_mcv_addr[8] = {0x200, 0x210, 0x220, 0x230, 0x240, 0x250, 0x260, 0x270};
static const int sb_pro_mcv_irqs[4] = {7, 5, 3, 3};


/* Each card in the SB16 family has a million variants, and it shows in the large variety of device IDs for the PnP models.
   This ROM was reconstructed in a best-effort basis around a pnpdump output log found in a forum. */
static uint8_t sb_16_pnp_rom[] = {
    0x0e, 0x8c, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, /* CTL0024, dummy checksum (filled in by isapnp_add_card) */
    0x0a, 0x10, 0x10, /* PnP version 1.0, vendor version 1.0 */
    0x82, 0x11, 0x00, 'C', 'r', 'e', 'a', 't', 'i', 'v', 'e', ' ', 'S', 'B', '1', '6', ' ', 'P', 'n', 'P', /* ANSI identifier */

    0x16, 0x0e, 0x8c, 0x00, 0x31, 0x00, 0x65, /* logical device CTL0031, supports vendor-specific registers 0x39/0x3A/0x3D/0x3F */
	0x82, 0x05, 0x00, 'A', 'u', 'd', 'i', 'o', /* ANSI identifier */
	0x31, 0x00, /* start dependent functions, preferred */
		0x22, 0x20, 0x00, /* IRQ 5 */
		0x2a, 0x02, 0x08, /* DMA 1, compatibility, no count by word, count by byte, not bus master, 8-bit only */
		0x2a, 0x20, 0x12, /* DMA 5, compatibility, count by word, no count by byte, not bus master, 16-bit only */
		0x47, 0x01, 0x20, 0x02, 0x20, 0x02, 0x01, 0x10, /* I/O 0x220, decodes 16-bit, 1-byte alignment, 16 addresses */
		0x47, 0x01, 0x30, 0x03, 0x30, 0x03, 0x01, 0x02, /* I/O 0x330, decodes 16-bit, 1-byte alignment, 2 addresses */
		0x47, 0x01, 0x88, 0x03, 0x88, 0x03, 0x01, 0x04, /* I/O 0x388, decodes 16-bit, 1-byte alignment, 4 addresses */
	0x31, 0x01, /* start dependent functions, acceptable */
		0x22, 0xa0, 0x04, /* IRQ 5/7/10 */
		0x2a, 0x0b, 0x08, /* DMA 0/1/3, compatibility, no count by word, count by byte, not bus master, 8-bit only */
		0x2a, 0xe0, 0x12, /* DMA 5/6/7, compatibility, count by word, no count by byte, not bus master, 16-bit only */
		0x47, 0x01, 0x20, 0x02, 0x80, 0x02, 0x20, 0x10, /* I/O 0x220-0x280, decodes 16-bit, 32-byte alignment, 16 addresses */
		0x47, 0x01, 0x00, 0x03, 0x30, 0x03, 0x30, 0x02, /* I/O 0x300-0x330, decodes 16-bit, 48-byte alignment, 2 addresses */
		0x47, 0x01, 0x88, 0x03, 0x88, 0x03, 0x01, 0x04, /* I/O 0x388, decodes 16-bit, 1-byte alignment, 4 addresses */
	0x31, 0x01, /* start dependent functions, acceptable */
		0x22, 0xa0, 0x04, /* IRQ 5/7/10 */
		0x2a, 0x0b, 0x08, /* DMA 0/1/3, compatibility, no count by word, count by byte, not bus master, 8-bit only */
		0x2a, 0xe0, 0x12, /* DMA 5/6/7, compatibility, count by word, no count by byte, not bus master, 16-bit only */
		0x47, 0x01, 0x20, 0x02, 0x80, 0x02, 0x20, 0x10, /* I/O 0x220-0x280, decodes 16-bit, 32-byte alignment, 16 addresses */
		0x47, 0x01, 0x00, 0x03, 0x30, 0x03, 0x30, 0x02, /* I/O 0x300-0x330, decodes 16-bit, 48-byte alignment, 2 addresses */
	0x31, 0x02, /* start dependent functions, functional */
		0x22, 0xa0, 0x04, /* IRQ 5/7/10 */
		0x2a, 0x0b, 0x08, /* DMA 0/1/3, compatibility, no count by word, count by byte, not bus master, 8-bit only */
		0x2a, 0xe0, 0x12, /* DMA 5/6/7, compatibility, count by word, no count by byte, not bus master, 16-bit only */
		0x47, 0x01, 0x20, 0x02, 0x80, 0x02, 0x20, 0x10, /* I/O 0x220-0x280, decodes 16-bit, 32-byte alignment, 16 addresses */
	0x31, 0x02, /* start dependent functions, functional */
		0x22, 0xa0, 0x04, /* IRQ 5/7/10 */
		0x2a, 0x0b, 0x08, /* DMA 0/1/3, compatibility, no count by word, count by byte, not bus master, 8-bit only */
		0x47, 0x01, 0x20, 0x02, 0x80, 0x02, 0x20, 0x10, /* I/O 0x220-0x280, decodes 16-bit, 32-byte alignment, 16 addresses */
		0x47, 0x01, 0x00, 0x03, 0x30, 0x03, 0x30, 0x02, /* I/O 0x300-0x330, decodes 16-bit, 48-byte alignment, 2 addresses */
		0x47, 0x01, 0x88, 0x03, 0x88, 0x03, 0x01, 0x04, /* I/O 0x388, decodes 16-bit, 1-byte alignment, 4 addresses */
	0x31, 0x02, /* start dependent functions, functional */
		0x22, 0xa0, 0x04, /* IRQ 5/7/10 */
		0x2a, 0x0b, 0x08, /* DMA 0/1/3, compatibility, no count by word, count by byte, not bus master, 8-bit only */
		0x47, 0x01, 0x20, 0x02, 0x80, 0x02, 0x20, 0x10, /* I/O 0x220-0x280, decodes 16-bit, 32-byte alignment, 16 addresses */
		0x47, 0x01, 0x00, 0x03, 0x30, 0x03, 0x30, 0x02, /* I/O 0x300-0x330, decodes 16-bit, 48-byte alignment, 2 addresses */
	0x31, 0x02, /* start dependent functions, functional */
		0x22, 0xa0, 0x04, /* IRQ 5/7/10 */
		0x2a, 0x0b, 0x08, /* DMA 0/1/3, compatibility, no count by word, count by byte, not bus master, 8-bit only */
		0x47, 0x01, 0x20, 0x02, 0x80, 0x02, 0x20, 0x10, /* I/O 0x220-0x280, decodes 16-bit, 32-byte alignment, 16 addresses */
	0x38, /* end dependent functions */

    0x16, 0x0e, 0x8c, 0x20, 0x11, 0x00, 0x5a, /* logical device CTL2011, supports vendor-specific registers 0x39/0x3B/0x3C/0x3E */
	0x1c, 0x41, 0xd0, 0x06, 0x00, /* compatible device PNP0600 */
	0x82, 0x03, 0x00, 'I', 'D', 'E', /* ANSI identifier */
	0x31, 0x00, /* start dependent functions, preferred */
		0x22, 0x00, 0x04, /* IRQ 10 */
		0x47, 0x01, 0x68, 0x01, 0x68, 0x01, 0x01, 0x08, /* I/O 0x168, decodes 16-bit, 1-byte alignment, 8 addresses */
		0x47, 0x01, 0x6e, 0x03, 0x6e, 0x03, 0x01, 0x02, /* I/O 0x36E, decodes 16-bit, 1-byte alignment, 2 addresses */
	0x31, 0x01, /* start dependent functions, acceptable */
		0x22, 0x00, 0x08, /* IRQ 11 */
		0x47, 0x01, 0xe8, 0x01, 0xe8, 0x01, 0x01, 0x08, /* I/O 0x1E8, decodes 16-bit, 1-byte alignment, 8 addresses */
		0x47, 0x01, 0xee, 0x03, 0xee, 0x03, 0x01, 0x02, /* I/O 0x3EE, decodes 16-bit, 1-byte alignment, 2 addresses */
	0x31, 0x01, /* start dependent functions, acceptable */
		0x22, 0x00, 0x8c, /* IRQ 10/11/15 */
		0x47, 0x01, 0x00, 0x01, 0xf8, 0x01, 0x08, 0x08, /* I/O 0x100-0x1F8, decodes 16-bit, 8-byte alignment, 8 addresses */
		0x47, 0x01, 0x00, 0x03, 0xfe, 0x03, 0x02, 0x02, /* I/O 0x300-0x3FE, decodes 16-bit, 2-byte alignment, 2 addresses */
	0x31, 0x02, /* start dependent functions, functional */
		0x22, 0x00, 0x80, /* IRQ 15 */
		0x47, 0x01, 0x70, 0x01, 0x70, 0x01, 0x01, 0x08, /* I/O 0x170, decodes 16-bit, 1-byte alignment, 8 addresses */
		0x47, 0x01, 0x76, 0x03, 0x76, 0x03, 0x01, 0x02, /* I/O 0x376, decodes 16-bit, 1-byte alignment, 1 addresses */
	0x38, /* end dependent functions */

    0x16, 0x41, 0xd0, 0xff, 0xff, 0x00, 0xda, /* logical device PNPFFFF, supports vendor-specific registers 0x38/0x39/0x3B/0x3C/0x3E */
	0x82, 0x08, 0x00, 'R', 'e', 's', 'e', 'r', 'v', 'e', 'd', /* ANSI identifier */
	0x47, 0x01, 0x00, 0x01, 0xf8, 0x03, 0x08, 0x01, /* I/O 0x100-0x3F8, decodes 16-bit, 8-byte alignment, 1 address */

    0x15, 0x0e, 0x8c, 0x70, 0x01, 0x00, /* logical device CTL7001 */
	0x1c, 0x41, 0xd0, 0xb0, 0x2f, /* compatible device PNPB02F */
	0x82, 0x04, 0x00, 'G', 'a', 'm', 'e', /* ANSI identifier */
	0x47, 0x01, 0x00, 0x02, 0x00, 0x02, 0x01, 0x08, /* I/O 0x200, decodes 16-bit, 1-byte alignment, 8 addresses */

    0x79, 0x00 /* end tag, dummy checksum (filled in by isapnp_add_card) */
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


/* SB 1, 1.5, MCV, and 2 do not have a mixer, so signal is hardwired. */
static void
sb_get_buffer_sb2(int32_t *buffer, int len, void *p)
{
    sb_t *sb = (sb_t *) p;
    sb_ct1335_mixer_t *mixer = &sb->mixer_sb2;            
    int c;
    double out_mono = 0.0, out_l = 0.0, out_r = 0.0;

    if (sb->opl_enabled)
	opl2_update(&sb->opl);

    sb_dsp_update(&sb->dsp);

    if (sb->cms_enabled)
	cms_update(&sb->cms);

    for (c = 0; c < len * 2; c += 2) {
	out_mono = 0.0;
	out_l = 0.0;
	out_r = 0.0;

	if (sb->opl_enabled)
		out_mono = ((double) sb->opl.buffer[c]) * 0.7171630859375;

	if (sb->cms_enabled) {
		out_l += sb->cms.buffer[c];
		out_r += sb->cms.buffer[c + 1];
	}
	out_l += out_mono;
	out_r += out_mono;

	if (((sb->opl_enabled) || (sb->cms_enabled)) && sb->mixer_enabled) {
		out_l *= mixer->fm;
		out_r *= mixer->fm;
	}

	/* TODO: Recording: I assume it has direct mic and line in like SB2.
		 It is unclear from the docs if it has a filter, but it probably does. */
	/* TODO: Recording: Mic and line In with AGC. */
	if (sb->mixer_enabled)
		out_mono = (sb_iir(0, 0, (double) sb->dsp.buffer[c]) * mixer->voice) / 3.9;
	else
		out_mono = (((sb_iir(0, 0, (double) sb->dsp.buffer[c]) / 1.3) * 65536.0) / 3.0) / 65536.0;
	out_l += out_mono;
	out_r += out_mono;

	if (sb->mixer_enabled) {
		out_l *= mixer->master;
		out_r *= mixer->master;
	}

	buffer[c]     += (int32_t) out_l;
	buffer[c + 1] += (int32_t) out_r;
    }

    sb->pos = 0;

    if (sb->opl_enabled)
	sb->opl.pos = 0;

    sb->dsp.pos = 0;

    if (sb->cms_enabled)
	sb->cms.pos = 0;
}


static void
sb2_filter_cd_audio(int channel, double *buffer, void *p)
{
    sb_t *sb = (sb_t *)p;
    sb_ct1335_mixer_t *mixer = &sb->mixer_sb2;
    double c;

    if (sb->mixer_enabled) {
	c = ((sb_iir(1, 0, *buffer)     / 1.3) * mixer->cd) / 3.0;
	*buffer = c * mixer->master;
    } else {
	c = (((sb_iir(1, 0, ((double) *buffer)) / 1.3) * 65536) / 3.0) / 65536.0;
	*buffer = c;
    }
}


void
sb_get_buffer_sbpro(int32_t *buffer, int len, void *p)
{
    sb_t *sb = (sb_t *)p;
    sb_ct1345_mixer_t *mixer = &sb->mixer_sbpro;
    int c;
    double out_l = 0.0, out_r = 0.0;

    if (sb->opl_enabled) {
	if (sb->dsp.sb_type == SBPRO) {
		opl2_update(&sb->opl);
		opl2_update(&sb->opl2);
	} else
		opl3_update(&sb->opl);
    }

    sb_dsp_update(&sb->dsp);

    for (c = 0; c < len * 2; c += 2) {
	out_l = 0.0, out_r = 0.0;

	if (sb->opl_enabled) {
		if (sb->dsp.sb_type == SBPRO) {
			/* Two chips for LEFT and RIGHT channels.
			   Each chip stores data into the LEFT channel only (no sample alternating.) */
			out_l = (((double) sb->opl.buffer [c   ]) * mixer->fm_l) * 0.7171630859375;
			out_r = (((double) sb->opl2.buffer[c   ]) * mixer->fm_r) * 0.7171630859375;
		} else {
			out_l = (((double) sb->opl.buffer[c    ]) * mixer->fm_l) * 0.7171630859375;
			out_r = (((double) sb->opl.buffer[c + 1]) * mixer->fm_r) * 0.7171630859375;
		}
	}

	/* TODO: Implement the stereo switch on the mixer instead of on the dsp? */
	if (mixer->output_filter) {
		out_l += (sb_iir(0, 0, (double) sb->dsp.buffer[c])     * mixer->voice_l) / 3.9;
		out_r += (sb_iir(0, 1, (double) sb->dsp.buffer[c + 1]) * mixer->voice_r) / 3.9;
	} else {
		out_l += (sb->dsp.buffer[c]     * mixer->voice_l) / 3.0;
		out_r += (sb->dsp.buffer[c + 1] * mixer->voice_r) / 3.0;
	}
	/* TODO: recording CD, Mic with AGC or line in. Note: mic volume does not affect recording. */

	out_l *= mixer->master_l;
	out_r *= mixer->master_r;

	buffer[c]     += (int32_t) out_l;
	buffer[c + 1] += (int32_t) out_r;
    }

    sb->pos = 0;

    if (sb->opl_enabled) {
	sb->opl.pos = 0;
	if (sb->dsp.sb_type != SBPRO)
		sb->opl2.pos = 0;
    }

    sb->dsp.pos = 0;
}


void
sbpro_filter_cd_audio(int channel, double *buffer, void *p)
{
    sb_t *sb = (sb_t *)p;
    sb_ct1345_mixer_t *mixer = &sb->mixer_sbpro;
    double c;
    double cd = channel ? mixer->cd_r : mixer->cd_l;
    double master = channel ? mixer->master_r : mixer->master_l;

    if (mixer->output_filter)
	c = (sb_iir(1, channel, *buffer) * cd) / 3.9;
    else
	c = (*buffer * cd) / 3.0;
    *buffer = c * master;
}


static void
sb_get_buffer_sb16_awe32(int32_t *buffer, int len, void *p)
{
    sb_t *sb = (sb_t *)p;
    sb_ct1745_mixer_t *mixer = &sb->mixer_sb16;
    int c, dsp_rec_pos = sb->dsp.record_pos_write;
    int c_emu8k, c_record;
    int32_t in_l, in_r;
    double out_l = 0.0, out_r = 0.0;
    double bass_treble;

    if (sb->opl_enabled)
	opl3_update(&sb->opl);

    if (sb->dsp.sb_type > SB16)
	emu8k_update(&sb->emu8k);

    sb_dsp_update(&sb->dsp);

    for (c = 0; c < len * 2; c += 2) {
	out_l = 0.0, out_r = 0.0;

	if (sb->dsp.sb_type > SB16)
		c_emu8k = ((((c / 2) * 44100) / 48000) * 2);

	if (sb->opl_enabled) {
		out_l = ((double) sb->opl.buffer[c    ]) * mixer->fm_l * 0.7171630859375;
		out_r = ((double) sb->opl.buffer[c + 1]) * mixer->fm_r * 0.7171630859375;
	}

	if (sb->dsp.sb_type > SB16) {
		out_l += (((double) sb->emu8k.buffer[c_emu8k])     * mixer->fm_l);
		out_r += (((double) sb->emu8k.buffer[c_emu8k + 1]) * mixer->fm_r);
	}

	/* TODO: Multi-recording mic with agc/+20db, CD, and line in with channel inversion */
	in_l = (mixer->input_selector_left  & INPUT_MIDI_L) ? ((int32_t) out_l) :
	       0 + (mixer->input_selector_left  & INPUT_MIDI_R) ? ((int32_t) out_r) : 0;
	in_r = (mixer->input_selector_right & INPUT_MIDI_L) ? ((int32_t) out_l) :
	       0 + (mixer->input_selector_right & INPUT_MIDI_R) ? ((int32_t) out_r) : 0;

	/* We divide by 3 to get the volume down to normal. */
	out_l += (low_fir_sb16(0, 0, (double) sb->dsp.buffer[c]) * mixer->voice_l) / 3.0;
	out_r += (low_fir_sb16(0, 1, (double) sb->dsp.buffer[c + 1]) * mixer->voice_r) / 3.0;

	out_l *= mixer->master_l;
	out_r *= mixer->master_r;

	/* This is not exactly how one does bass/treble controls, but the end result is like it.
	   A better implementation would reduce the CPU usage. */
	if (mixer->bass_l != 8) {
		bass_treble = sb_bass_treble_4bits[mixer->bass_l];

		if (mixer->bass_l > 8)
			out_l += (low_iir(0, 0, out_l) * bass_treble);
		else if (mixer->bass_l < 8)
			out_l = ((out_l) * bass_treble + low_cut_iir(0, 0, out_l) * (1.0 - bass_treble));
	}

	if (mixer->bass_r != 8) {
		bass_treble = sb_bass_treble_4bits[mixer->bass_r];

		if (mixer->bass_r > 8)
			out_r += (low_iir(0, 1, out_r) * bass_treble);
		else if (mixer->bass_r < 8)
			out_r = ((out_r) * bass_treble + low_cut_iir(0, 1, out_r) * (1.0 - bass_treble));
	}

	if (mixer->treble_l != 8) {
		bass_treble = sb_bass_treble_4bits[mixer->treble_l];

		if (mixer->treble_l > 8)
			out_l += (high_iir(0, 0, out_l) * bass_treble);
		else if (mixer->treble_l < 8)
			out_l = ((out_l) * bass_treble + high_cut_iir(0, 0, out_l) * (1.0 - bass_treble));
	}

	if (mixer->treble_r != 8) {
		bass_treble = sb_bass_treble_4bits[mixer->treble_r];

		if (mixer->treble_r > 8)
			out_r += (high_iir(0, 1, out_r) * bass_treble);
		else if (mixer->treble_r < 8)
			out_r = ((out_l) * bass_treble + high_cut_iir(0, 1, out_r) * (1.0 - bass_treble));
	}

	if (sb->dsp.sb_enable_i) {
		c_record = dsp_rec_pos + ((c * sb->dsp.sb_freq) / 48000);
		in_l <<= mixer->input_gain_L;
		in_r <<= mixer->input_gain_R;

		/* Clip signal */
		if (in_l < -32768)
			in_l = -32768;
		else if (in_l > 32767)
			in_l = 32767;

		if (in_r < -32768)
			in_r = -32768;
		else if (in_r > 32767)
			in_r = 32767;

		sb->dsp.record_buffer[c_record     & 0xffff] = in_l;
		sb->dsp.record_buffer[(c_record+1) & 0xffff] = in_r;
	}

	buffer[c]     += (int32_t) (out_l * mixer->output_gain_L);
	buffer[c + 1] += (int32_t) (out_r * mixer->output_gain_R);
    }

    sb->dsp.record_pos_write += ((len * sb->dsp.sb_freq) / 24000);
    sb->dsp.record_pos_write &= 0xffff;

    sb->pos = 0;

    if (sb->opl_enabled)
	sb->opl.pos = 0;

    sb->dsp.pos = 0;

    if (sb->dsp.sb_type > SB16)
	sb->emu8k.pos = 0;
}


static void
sb16_awe32_filter_cd_audio(int channel, double *buffer, void *p)
{
    sb_t *sb = (sb_t *)p;
    sb_ct1745_mixer_t *mixer = &sb->mixer_sb16;
    double c;
    double cd = channel ? mixer->cd_r : mixer->cd_l /* / 3.0 */;
    double master = channel ? mixer->master_r : mixer->master_l;
    int32_t bass = channel ? mixer->bass_r : mixer->bass_l;
    int32_t treble = channel ? mixer->treble_r : mixer->treble_l;
    double bass_treble;
    double output_gain = (channel ? mixer->output_gain_R : mixer->output_gain_L);

    c = (low_fir_sb16(1, channel, *buffer)     * cd) / 3.0;
    c *= master;

    /* This is not exactly how one does bass/treble controls, but the end result is like it.
       A better implementation would reduce the CPU usage. */
    if (bass != 8) {
	bass_treble = sb_bass_treble_4bits[bass];

	if (bass > 8)
		c += (low_iir(1, channel, c) * bass_treble);
	else if (bass < 8)
		c = (c * bass_treble + low_cut_iir(1, channel, c) * (1.0 - bass_treble));
    }

    if (treble != 8) {
	bass_treble = sb_bass_treble_4bits[treble];

	if (treble > 8)
		c += (high_iir(1, channel, c) * bass_treble);
	else if (treble < 8)
		c = (c * bass_treble + high_cut_iir(1, channel, c) * (1.0 - bass_treble));
    }

    *buffer = c * output_gain;
}


void
sb_ct1335_mixer_write(uint16_t addr, uint8_t val, void *p)
{
    sb_t *sb = (sb_t *)p;
    sb_ct1335_mixer_t *mixer = &sb->mixer_sb2;

    if (!(addr & 1)) {
	mixer->index = val;
	mixer->regs[0x01] = val;
    } else {
	if (mixer->index == 0) {
		/* Reset */
		mixer->regs[0x02] = mixer->regs[0x06] = 0x08;
		mixer->regs[0x08] = 0x00;
		/* Changed default from -46dB to 0dB*/
		mixer->regs[0x0a] = 0x06;
	} else {
		mixer->regs[mixer->index] = val;
		switch (mixer->index) {
			case 0x00: case 0x02: case 0x06: case 0x08: case 0x0a:
				break;

			default:
				sb_log("sb_ct1335: Unknown register WRITE: %02X\t%02X\n", mixer->index, mixer->regs[mixer->index]);
				break;
		}
	}

	mixer->master = sb_att_4dbstep_3bits[(mixer->regs[0x02] >> 1) & 0x7] / 32768.0;
	mixer->fm     = sb_att_4dbstep_3bits[(mixer->regs[0x06] >> 1) & 0x7] / 32768.0;
	mixer->cd     = sb_att_4dbstep_3bits[(mixer->regs[0x08] >> 1) & 0x7] / 32768.0;
	mixer->voice  = sb_att_7dbstep_2bits[(mixer->regs[0x0a] >> 1) & 0x3] / 32768.0;
    }
}


uint8_t
sb_ct1335_mixer_read(uint16_t addr, void *p)
{
    sb_t *sb = (sb_t *)p;
    sb_ct1335_mixer_t *mixer = &sb->mixer_sb2;

    if (!(addr & 1))
	return mixer->index;

    switch (mixer->index) {
	case 0x00: case 0x02: case 0x06: case 0x08: case 0x0A:
		return mixer->regs[mixer->index];
	default:
		sb_log("sb_ct1335: Unknown register READ: %02X\t%02X\n", mixer->index, mixer->regs[mixer->index]);
		break;
    }

    return 0xff;
}


void
sb_ct1335_mixer_reset(sb_t* sb)
{
    sb_ct1335_mixer_write(0x254, 0, sb);
    sb_ct1335_mixer_write(0x255, 0, sb);
}


void
sb_ct1345_mixer_write(uint16_t addr, uint8_t val, void *p)
{
    sb_t *sb = (sb_t *)p;
    sb_ct1345_mixer_t *mixer = &sb->mixer_sbpro;

    if (!(addr & 1)) {
	mixer->index = val;
	mixer->regs[0x01] = val;
    } else {
	if (mixer->index == 0) {
		/* Reset */
		mixer->regs[0x0a] = mixer->regs[0x0c] = 0x00;
		mixer->regs[0x0e] = 0x00;
		/* Changed default from -11dB to 0dB */
		mixer->regs[0x04] = mixer->regs[0x22] = 0xee;
		mixer->regs[0x26] = mixer->regs[0x28] = 0xee;
		mixer->regs[0x2e] = 0x00;
		sb_dsp_set_stereo(&sb->dsp, mixer->regs[0x0e] & 2);
	} else {
		mixer->regs[mixer->index] = val;

		switch (mixer->index) {
			/* Compatibility: chain registers 0x02 and 0x22 as well as 0x06 and 0x26 */
			case 0x02: case 0x06: case 0x08:
				mixer->regs[mixer->index + 0x20] = ((val & 0xe) << 4) | (val&0xe);
				break;

			case 0x22: case 0x26: case 0x28:
				mixer->regs[mixer->index - 0x20] = (val & 0xe);
				break;

			/* More compatibility:
			   SoundBlaster Pro selects register 020h for 030h, 022h for 032h,
			   026h for 036h, and 028h for 038h. */
			case 0x30: case 0x32: case 0x36: case 0x38:
				mixer->regs[mixer->index - 0x10] = (val & 0xee);
				break;

			case 0x00: case 0x04: case 0x0a: case 0x0c: case 0x0e:
				case 0x2e:
				break;

			default:
				sb_log("sb_ct1345: Unknown register WRITE: %02X\t%02X\n", mixer->index, mixer->regs[mixer->index]);
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

	mixer->mic      = sb_att_7dbstep_2bits[(mixer->regs[0x0a] >> 1) & 0x3] / 32768.0;

	mixer->output_filter   = !(mixer->regs[0xe] & 0x20);
	mixer->input_filter    = !(mixer->regs[0xc] & 0x20);
	mixer->in_filter_freq  = ((mixer->regs[0xc] & 0x8) == 0) ? 3200 : 8800;
	mixer->stereo = mixer->regs[0xe] & 2;
	if (mixer->index == 0xe)
		sb_dsp_set_stereo(&sb->dsp, val & 2);

	switch ((mixer->regs[0xc] & 6)) {
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
sb_ct1345_mixer_read(uint16_t addr, void *p)
{
    sb_t *sb = (sb_t *)p;
    sb_ct1345_mixer_t *mixer = &sb->mixer_sbpro;

    if (!(addr & 1))
	return mixer->index;

    switch (mixer->index) {
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


void
sb_ct1345_mixer_reset(sb_t* sb)
{
    sb_ct1345_mixer_write(4, 0, sb);
    sb_ct1345_mixer_write(5, 0, sb);
}


static void
sb_ct1745_mixer_write(uint16_t addr, uint8_t val, void *p)
{
    sb_t *sb = (sb_t *) p;
    sb_ct1745_mixer_t *mixer = &sb->mixer_sb16;

    if (!(addr & 1))
	mixer->index = val;
    else {
	/* DESCRIPTION:
	   Contains previously selected register value.  Mixer Data Register value.
	   NOTES:
	   SoundBlaster 16 sets bit 7 if previous mixer index invalid.
	   Status bytes initially 080h on startup for all but level bytes (SB16). */

	if (mixer->index == 0) {
		/* Reset: Changed defaults from -14dB to 0dB */

		mixer->regs[0x30] = mixer->regs[0x31] = 0xf8;
		mixer->regs[0x32] = mixer->regs[0x33] = 0xf8;
		mixer->regs[0x34] = mixer->regs[0x35] = 0xf8;
		mixer->regs[0x36] = mixer->regs[0x37] = 0xf8;
		mixer->regs[0x38] = mixer->regs[0x39] = 0x00;

		mixer->regs[0x3a] = mixer->regs[0x3b] = 0x00;

		mixer->regs[0x3c] = (OUTPUT_MIC | OUTPUT_CD_R | OUTPUT_CD_L  | OUTPUT_LINE_R | OUTPUT_LINE_L);
		mixer->regs[0x3d] = (INPUT_MIC  | INPUT_CD_L  | INPUT_LINE_L | INPUT_MIDI_L);
		mixer->regs[0x3e] = (INPUT_MIC  | INPUT_CD_R  | INPUT_LINE_R | INPUT_MIDI_R);

		mixer->regs[0x3f] = mixer->regs[0x40] = 0x00;
		mixer->regs[0x41] = mixer->regs[0x42] = 0x00;

		mixer->regs[0x44] = mixer->regs[0x45] = 0x80;
		mixer->regs[0x46] = mixer->regs[0x47] = 0x80;

		mixer->regs[0x43] = 0x00;

		mixer->regs[0x83] = 0xff;
		sb->dsp.sb_irqm8 = 0;
		sb->dsp.sb_irqm16 = 0;
		sb->dsp.sb_irqm401 = 0;
	} else
		mixer->regs[mixer->index] = val;

	switch (mixer->index) {
		/* SB1/2 compatibility? */
		case 0x02:
			mixer->regs[0x30] = ((mixer->regs[0x02] & 0xf) << 4) | 0x8;
			mixer->regs[0x31] = ((mixer->regs[0x02] & 0xf) << 4) | 0x8;
			break;
		case 0x06:
			mixer->regs[0x34] = ((mixer->regs[0x06] & 0xf) << 4) | 0x8;
			mixer->regs[0x35] = ((mixer->regs[0x06] & 0xf) << 4) | 0x8;
			break;
		case 0x08:
			mixer->regs[0x36] = ((mixer->regs[0x08] & 0xf) << 4) | 0x8;
			mixer->regs[0x37] = ((mixer->regs[0x08] & 0xf) << 4) | 0x8;
			break;
		/* SBPro compatibility. Copy values to sb16 registers. */
		case 0x22:
			mixer->regs[0x30] = (mixer->regs[0x22] & 0xf0) | 0x8;
			mixer->regs[0x31] = ((mixer->regs[0x22] & 0xf) << 4) | 0x8;
			break;
		case 0x04:
			mixer->regs[0x32] = (mixer->regs[0x04] & 0xf0) | 0x8;
			mixer->regs[0x33] = ((mixer->regs[0x04] & 0xf) << 4) | 0x8;
			break;
		case 0x26:
			mixer->regs[0x34] = (mixer->regs[0x26] & 0xf0) | 0x8;
			mixer->regs[0x35] = ((mixer->regs[0x26] & 0xf) << 4) | 0x8;
			break;
		case 0x28:
			mixer->regs[0x36] = (mixer->regs[0x28] & 0xf0) | 0x8;
			mixer->regs[0x37] = ((mixer->regs[0x28] & 0xf) << 4) | 0x8;
			break;
		case 0x0A:
			mixer->regs[0x3a] = (mixer->regs[0x0a] << 5) | 0x18;
			break;
		case 0x2e:
			mixer->regs[0x38] = (mixer->regs[0x2e] & 0xf0) | 0x8;
			mixer->regs[0x39] = ((mixer->regs[0x2e] & 0xf) << 4) | 0x8;
                        break;

		/* (DSP 4.xx feature):
		   The Interrupt Setup register, addressed as register 80h on the Mixer register map,
		   is used to configure or determine the Interrupt request line.
		   The DMA setup register, addressed as register 81h on the Mixer register map, is
		   used to configure or determine the DMA channels.

		   Note: Registers 80h and 81h are Read-only for PnP boards. */
		case 0x80:
			if (val & 0x01)
				sb_dsp_setirq(&sb->dsp, 2);
			if (val & 0x02)
				sb_dsp_setirq(&sb->dsp, 5);
			if (val & 0x04)
				sb_dsp_setirq(&sb->dsp, 7);
			if (val & 0x08)
				sb_dsp_setirq(&sb->dsp, 10);
			break;

		case 0x81:
			/* The documentation is confusing. sounds as if multple dma8 channels could
			   be set. */
			if (val & 0x01)
				sb_dsp_setdma8(&sb->dsp, 0);
			if (val & 0x02)
				sb_dsp_setdma8(&sb->dsp, 1);
			if (val & 0x08)
				sb_dsp_setdma8(&sb->dsp, 3);
			if (val & 0x20)
				sb_dsp_setdma16(&sb->dsp, 5);
			if (val & 0x40)
				sb_dsp_setdma16(&sb->dsp, 6);
			if (val & 0x80)
				sb_dsp_setdma16(&sb->dsp, 7);
			break;

		case 0x83:
			/* Interrupt mask. */
			sb_update_mask(&sb->dsp, !(val & 0x01), !(val & 0x02), !(val & 0x04));
			break;

		case 0x84:
			/* MPU Control register, per the Linux source code. */
			if (sb->mpu != NULL) {
				if ((val & 0x06) == 0x00)
					mpu401_change_addr(sb->mpu, 0x330);
				else if ((val & 0x06) == 0x04)
					mpu401_change_addr(sb->mpu, 0x300);
				else if ((val & 0x06) == 0x02)
					mpu401_change_addr(sb->mpu, 0);
			}
			break;
	}

	mixer->output_selector = mixer->regs[0x3c];
	mixer->input_selector_left = mixer->regs[0x3d];
	mixer->input_selector_right = mixer->regs[0x3e];

	mixer->master_l = sb_att_2dbstep_5bits[mixer->regs[0x30] >> 3] / 32768.0;
	mixer->master_r = sb_att_2dbstep_5bits[mixer->regs[0x31] >> 3] / 32768.0;
	mixer->voice_l  = sb_att_2dbstep_5bits[mixer->regs[0x32] >> 3] / 32768.0;
	mixer->voice_r  = sb_att_2dbstep_5bits[mixer->regs[0x33] >> 3] / 32768.0;
	mixer->fm_l     = sb_att_2dbstep_5bits[mixer->regs[0x34] >> 3] / 32768.0;
	mixer->fm_r     = sb_att_2dbstep_5bits[mixer->regs[0x35] >> 3] / 32768.0;
	mixer->cd_l     = (mixer->output_selector & OUTPUT_CD_L) ? (sb_att_2dbstep_5bits[mixer->regs[0x36] >> 3] / 32768.0): 0.0;
	mixer->cd_r     = (mixer->output_selector & OUTPUT_CD_R) ? (sb_att_2dbstep_5bits[mixer->regs[0x37] >> 3] / 32768.0) : 0.0;
	mixer->line_l   = (mixer->output_selector & OUTPUT_LINE_L) ? (sb_att_2dbstep_5bits[mixer->regs[0x38] >> 3] / 32768.0) : 0.0;
	mixer->line_r   = (mixer->output_selector & OUTPUT_LINE_R) ? (sb_att_2dbstep_5bits[mixer->regs[0x39] >> 3] / 32768.0) : 0.0;

	mixer->mic      = sb_att_2dbstep_5bits[mixer->regs[0x3a] >> 3] / 32768.0;
	mixer->speaker  = sb_att_2dbstep_5bits[mixer->regs[0x3b] * 3 + 22] / 32768.0;

	mixer->input_gain_L = (mixer->regs[0x3f] >> 6);
	mixer->input_gain_R = (mixer->regs[0x40] >> 6);
	mixer->output_gain_L = (double) (1 << (mixer->regs[0x41] >> 6));
	mixer->output_gain_R = (double) (1 << (mixer->regs[0x42] >> 6));

	mixer->bass_l   = mixer->regs[0x46] >> 4;
	mixer->bass_r   = mixer->regs[0x47] >> 4;
	mixer->treble_l = mixer->regs[0x44] >> 4;
	mixer->treble_r = mixer->regs[0x45] >> 4;

	/* TODO: PC Speaker volume, with "output_selector" check? or better not? */
	sb_log("sb_ct1745: Received register WRITE: %02X\t%02X\n", mixer->index, mixer->regs[mixer->index]);
    }
}


static uint8_t
sb_ct1745_mixer_read(uint16_t addr, void *p)
{
    sb_t *sb = (sb_t *) p;
    sb_ct1745_mixer_t *mixer = &sb->mixer_sb16;
    uint8_t temp, ret = 0xff;

    if (!(addr & 1))
	ret = mixer->index;

    sb_log("sb_ct1745: received register READ: %02X\t%02X\n", mixer->index, mixer->regs[mixer->index]);

    if ((mixer->index >= 0x30) && (mixer->index <= 0x47))
	ret = mixer->regs[mixer->index];
    else switch (mixer->index) {
	case 0x00:
		ret = mixer->regs[mixer->index];
		break;

	/*SB Pro compatibility*/                        
	case 0x04:
		ret = ((mixer->regs[0x33] >> 4) & 0x0f) | (mixer->regs[0x32] & 0xf0);
		break;
	case 0x0a:
		ret = (mixer->regs[0x3a] >> 5);
		break;
	case 0x02:
		ret = ((mixer->regs[0x30] >> 4) & 0x0f);
		break;
	case 0x06:
		ret = ((mixer->regs[0x34] >> 4) & 0x0f);
		break;
	case 0x08:
		ret = ((mixer->regs[0x36] >> 4) & 0x0f);
		break;
	case 0x0e:
		ret = 0x02;
		break;
	case 0x22:
		ret = ((mixer->regs[0x31] >> 4) & 0x0f) | (mixer->regs[0x30] & 0xf0);
		break;
	case 0x26:
		ret = ((mixer->regs[0x35] >> 4) & 0x0f) | (mixer->regs[0x34] & 0xf0);
		break;
	case 0x28:
		ret = ((mixer->regs[0x37] >> 4) & 0x0f) | (mixer->regs[0x36] & 0xf0);
		break;
	case 0x2e:
		ret = ((mixer->regs[0x39] >> 4) & 0x0f) | (mixer->regs[0x38] & 0xf0);
		break;

	case 0x48: 
		/* Undocumented. The Creative Windows Mixer calls this after calling 3C (input selector),
		   even when writing.
		   Also, the version I have (5.17), does not use the MIDI.L/R input selectors, it uses
		   the volume to mute (Affecting the output, obviously). */
		ret = mixer->regs[mixer->index];
		break;

	case 0x80:
		/* TODO: Unaffected by mixer reset or soft reboot.
			 Enabling multiple bits enables multiple IRQs. */

		switch (sb->dsp.sb_irqnum) {
			case 2: ret = 1; break;
			case 5: ret = 2; break;
			case 7: ret = 4; break;
			case 10: ret = 8; break;
		}
		break;

	case 0x81:
		/* TODO: Unaffected by mixer reset or soft reboot.
			 Enabling multiple 8 or 16-bit DMA bits enables multiple DMA channels.
			 Disabling all 8-bit DMA channel bits disables 8-bit DMA requests,
			 including translated 16-bit DMA requests.
			 Disabling all 16-bit DMA channel bits enables translation of 16-bit DMA
			 requests to 8-bit ones, using the selected 8-bit DMA channel. */

		ret = 0;
		switch (sb->dsp.sb_8_dmanum) {
			case 0: ret |= 1; break;
			case 1: ret |= 2; break;
			case 3: ret |= 8; break;
		}
		switch (sb->dsp.sb_16_dmanum) {
			case 5: ret |= 0x20; break;
			case 6: ret |= 0x40; break;
			case 7: ret |= 0x80; break;
		}                            
		break;

	case 0x82:
		/* The Interrupt status register, addressed as register 82h on the Mixer register map,
		   is used by the ISR to determine whether the interrupt is meant for it or for some other ISR,
		   in which case it should chain to the previous routine. */
		/* 0 = none, 1 =  digital 8bit or SBMIDI, 2 = digital 16bit, 4 = MPU-401 */
		/* 0x02000 DSP v4.04, 0x4000 DSP v4.05, 0x8000 DSP v4.12.
		   I haven't seen this making any difference, but I'm keeping it for now. */
		temp = ((sb->dsp.sb_irq8) ? 1 : 0) | ((sb->dsp.sb_irq16) ? 2 : 0) |
		       ((sb->dsp.sb_irq401) ? 4 : 0) | 0x4000;
		ret = temp;
		break;

	case 0x83:
		/* Interrupt mask. */
		ret = mixer->regs[mixer->index];
		break;

	case 0x84:
		/* MPU Control. */
		if (sb->mpu == NULL)
			ret = 0x02;
		else {
			if (sb->mpu->addr == 0x330)
				ret = 0x00;
			else if (sb->mpu->addr == 0x300)
				ret = 0x04;
			else if (sb->mpu->addr == 0)
				ret = 0x02;
			else
				ret = 0x06;	/* Should never happen. */
		}
		break;

	case 0x90:
		/* 3D Enhancement switch. */
		ret = mixer->regs[mixer->index];
		break;

	/* TODO: creative drivers read and write on 0xFE and 0xFF. not sure what they are supposed to be. */
	case 0xfd:
		ret = 16;
		break;

	case 0xfe:
		ret = 6;
		break;

	default:
		sb_log("sb_ct1745: Unknown register READ: %02X\t%02X\n", mixer->index, mixer->regs[mixer->index]);
		break;
    }

    sb_log("CT1745: read  REG%02X: %02X\n", mixer->index, ret);

    return ret;
}


static void
sb_ct1745_mixer_reset(sb_t* sb)
{
    sb_ct1745_mixer_write(4, 0, sb);
    sb_ct1745_mixer_write(5, 0, sb);

    sb->mixer_sb16.regs[0xfd] = 16;
    sb->mixer_sb16.regs[0xfe] = 6;
}


uint8_t
sb_mcv_read(int port, void *p)
{
    sb_t *sb = (sb_t *)p;

    sb_log("sb_mcv_read: port=%04x\n", port);

    return sb->pos_regs[port & 7];
}


void
sb_mcv_write(int port, uint8_t val, void *p)
{
    uint16_t addr;
    sb_t *sb = (sb_t *)p;

    if (port < 0x102)
	return;

    sb_log("sb_mcv_write: port=%04x val=%02x\n", port, val);

    addr = sb_mcv_addr[sb->pos_regs[4] & 7];
    if (sb->opl_enabled) {
	io_removehandler(addr + 8, 0x0002, opl2_read,    NULL, NULL,
					   opl2_write,   NULL, NULL, &sb->opl);
	io_removehandler(0x0388,   0x0002, opl2_read,    NULL, NULL,
					   opl2_write,   NULL, NULL, &sb->opl);
    }
    /* DSP I/O handler is activated in sb_dsp_setaddr */
    sb_dsp_setaddr(&sb->dsp, 0);

    sb->pos_regs[port & 7] = val;

    if (sb->pos_regs[2] & 1) {
	addr = sb_mcv_addr[sb->pos_regs[4] & 7];

	if (sb->opl_enabled) {
		io_sethandler(addr + 8, 0x0002, opl2_read,    NULL, NULL,
						opl2_write,   NULL, NULL, &sb->opl);
		io_sethandler(0x0388,   0x0002, opl2_read,    NULL, NULL,
						opl2_write,   NULL, NULL, &sb->opl);
	}
	/* DSP I/O handler is activated in sb_dsp_setaddr */
	sb_dsp_setaddr(&sb->dsp, addr);
    }
}


uint8_t
sb_mcv_feedb(void *p)
{
    sb_t *sb = (sb_t *)p;

    return (sb->pos_regs[2] & 1);
}


static uint8_t
sb_pro_mcv_read(int port, void *p)
{
    sb_t *sb = (sb_t *)p;
    uint8_t ret = sb->pos_regs[port & 7];

    sb_log("sb_pro_mcv_read: port=%04x ret=%02x\n", port, ret);

    return ret;
}


static void
sb_pro_mcv_write(int port, uint8_t val, void *p)
{
    uint16_t addr;
    sb_t *sb = (sb_t *)p;

    if (port < 0x102)
	return;

    sb_log("sb_pro_mcv_write: port=%04x val=%02x\n", port, val);

    addr = (sb->pos_regs[2] & 0x20) ? 0x220 : 0x240;

    io_removehandler(addr,     0x0004, opl3_read,    NULL, NULL,
				       opl3_write,   NULL, NULL, &sb->opl);
    io_removehandler(addr + 8, 0x0002, opl3_read,    NULL, NULL,
				       opl3_write,   NULL, NULL, &sb->opl);
    io_removehandler(0x0388,   0x0004, opl3_read,    NULL, NULL,
				       opl3_write,   NULL, NULL, &sb->opl);
    io_removehandler(addr + 4, 0x0002, sb_ct1345_mixer_read,  NULL, NULL,
				       sb_ct1345_mixer_write, NULL, NULL, sb);
    /* DSP I/O handler is activated in sb_dsp_setaddr */
    sb_dsp_setaddr(&sb->dsp, 0);

    sb->pos_regs[port & 7] = val;

    if (sb->pos_regs[2] & 1) {
	addr = (sb->pos_regs[2] & 0x20) ? 0x220 : 0x240;

	io_sethandler(addr,     0x0004, opl3_read,    NULL, NULL,
					opl3_write,   NULL, NULL, &sb->opl);
        io_sethandler(addr + 8, 0x0002, opl3_read,    NULL, NULL,
					opl3_write,   NULL, NULL, &sb->opl);
	io_sethandler(0x0388,   0x0004, opl3_read,    NULL, NULL,
					opl3_write,   NULL, NULL, &sb->opl);
	io_sethandler(addr + 4, 0x0002, sb_ct1345_mixer_read,  NULL, NULL,
					sb_ct1345_mixer_write, NULL, NULL, sb);
	/* DSP I/O handler is activated in sb_dsp_setaddr */
	sb_dsp_setaddr(&sb->dsp, addr);
    }

    sb_dsp_setirq(&sb->dsp, sb_pro_mcv_irqs[(sb->pos_regs[5] >> 4) & 3]);
    sb_dsp_setdma8(&sb->dsp, sb->pos_regs[4] & 3);
}


static void
sb_16_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv)
{
    sb_t *sb = (sb_t *) priv;
    uint16_t addr = sb->dsp.sb_addr;
    uint8_t val;

    switch (ld) {
	case 0: /* Audio */
		io_removehandler(addr,     0x0004, opl3_read,    NULL, NULL,
						   opl3_write,   NULL, NULL, &sb->opl);
		io_removehandler(addr + 8, 0x0002, opl3_read,    NULL, NULL,
						   opl3_write,   NULL, NULL, &sb->opl);
		io_removehandler(addr + 4, 0x0002, sb_ct1745_mixer_read,  NULL, NULL,
						   sb_ct1745_mixer_write, NULL, NULL, sb);

		addr = sb->opl_pnp_addr;
		if (addr) {
			sb->opl_pnp_addr = 0;
			io_removehandler(addr,     0x0004, opl3_read,    NULL, NULL,
							   opl3_write,   NULL, NULL, &sb->opl);
		}

		sb_dsp_setaddr(&sb->dsp, 0);
		sb_dsp_setirq(&sb->dsp, 0);
		sb_dsp_setdma8(&sb->dsp, ISAPNP_DMA_DISABLED);
		sb_dsp_setdma16(&sb->dsp, ISAPNP_DMA_DISABLED);

		mpu401_change_addr(sb->mpu, 0);

		if (config->activate) {
			addr = config->io[0].base;
			if (addr != ISAPNP_IO_DISABLED) {
				io_sethandler(addr,     0x0004, opl3_read,    NULL, NULL,
								opl3_write,   NULL, NULL, &sb->opl);
				io_sethandler(addr + 8, 0x0002, opl3_read,    NULL, NULL,
								opl3_write,   NULL, NULL, &sb->opl);
				io_sethandler(addr + 4, 0x0002, sb_ct1745_mixer_read,  NULL, NULL,
								sb_ct1745_mixer_write, NULL, NULL, sb);

				sb_dsp_setaddr(&sb->dsp, addr);
			}

			addr = config->io[1].base;
			if (addr != ISAPNP_IO_DISABLED)
				mpu401_change_addr(sb->mpu, addr);

			addr = config->io[2].base;
			if (addr != ISAPNP_IO_DISABLED) {
				sb->opl_pnp_addr = addr;
				io_sethandler(addr,	0x0004, opl3_read,    NULL, NULL,
								opl3_write,   NULL, NULL, &sb->opl);
			}

			val = config->irq[0].irq;
			if (val != ISAPNP_IRQ_DISABLED)
				sb_dsp_setirq(&sb->dsp, val);

			val = config->dma[0].dma;
			if (val != ISAPNP_DMA_DISABLED)
				sb_dsp_setdma8(&sb->dsp, val);

			val = config->dma[1].dma;
			if (val != ISAPNP_DMA_DISABLED)
				sb_dsp_setdma16(&sb->dsp, val);
		}

		break;

	case 1: /* IDE */
		ide_pnp_config_changed(0, config, (void *) 2);
		break;

	case 2: /* Reserved (16) / WaveTable (32+) */
		if (sb->dsp.sb_type > SB16)
			emu8k_change_addr(&sb->emu8k, (config->activate && (config->io[0].base != ISAPNP_IO_DISABLED)) ? config->io[0].base : 0);
		break;

	case 3: /* Game */
		gameport_remap(sb->gameport, (config->activate && (config->io[0].base != ISAPNP_IO_DISABLED)) ? config->io[0].base : 0);
		break;

	case 4: /* StereoEnhance (32) */
		break;
    }
}


static void
sb_awe32_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv)
{
    sb_t *sb = (sb_t *) priv;

    switch (ld) {
	case 0: /* Audio */
	case 1: /* IDE */
		sb_16_pnp_config_changed(ld, config, sb);
		break;

	case 2: /* Game */
	case 3: /* WaveTable */
		sb_16_pnp_config_changed(ld ^ 1, config, sb);
		break;
    }
}


static void
sb_awe64_gold_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv)
{
    sb_t *sb = (sb_t *) priv;

    switch (ld) {
	case 0: /* Audio */
	case 2: /* WaveTable */
		sb_16_pnp_config_changed(ld, config, sb);
		break;

	case 1: /* Game */
		sb_16_pnp_config_changed(3, config, sb);
		break;
    }
}


void *
sb_1_init(const device_t *info)
{
    /* SB1/2 port mappings, 210h to 260h in 10h steps
       2x0 to 2x3 -> CMS chip
       2x6, 2xA, 2xC, 2xE -> DSP chip
       2x8, 2x9, 388 and 389 FM chip*/
    sb_t *sb = malloc(sizeof(sb_t));
    uint16_t addr = device_get_config_hex16("base");        
    memset(sb, 0, sizeof(sb_t));

    sb->opl_enabled = device_get_config_int("opl");
    if (sb->opl_enabled)
	opl2_init(&sb->opl);

    sb_dsp_init(&sb->dsp, SB1, SB_SUBTYPE_DEFAULT, sb);
    sb_dsp_setaddr(&sb->dsp, addr);
    sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
    sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
    /* DSP I/O handler is activated in sb_dsp_setaddr */
    if (sb->opl_enabled) {
	io_sethandler(addr + 8, 0x0002, opl2_read,    NULL, NULL,
					opl2_write,   NULL, NULL, &sb->opl);
	io_sethandler(0x0388,   0x0002, opl2_read,    NULL, NULL,
					opl2_write,   NULL, NULL, &sb->opl);
    }

    sb->cms_enabled = 1;
    memset(&sb->cms, 0, sizeof(cms_t));
    io_sethandler(addr, 0x0004, cms_read, NULL, NULL, cms_write, NULL, NULL, &sb->cms);

    sb->mixer_enabled = 0;
    sound_add_handler(sb_get_buffer_sb2, sb);
    sound_set_cd_audio_filter(sb2_filter_cd_audio, sb);

    if (device_get_config_int("receive_input"))
	midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &sb->dsp);

    return sb;
}


void *
sb_15_init(const device_t *info)
{
    /* SB1/2 port mappings, 210h to 260h in 10h steps
       2x0 to 2x3 -> CMS chip
       2x6, 2xA, 2xC, 2xE -> DSP chip
       2x8, 2x9, 388 and 389 FM chip */
    sb_t *sb = malloc(sizeof(sb_t));
    uint16_t addr = device_get_config_hex16("base");
    memset(sb, 0, sizeof(sb_t));

    sb->opl_enabled = device_get_config_int("opl");
    if (sb->opl_enabled)
	opl2_init(&sb->opl);

    sb_dsp_init(&sb->dsp, SB15, SB_SUBTYPE_DEFAULT, sb);
    sb_dsp_setaddr(&sb->dsp, addr);
    sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
    sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
    /* DSP I/O handler is activated in sb_dsp_setaddr */
    if (sb->opl_enabled) {
	io_sethandler(addr + 8, 0x0002, opl2_read,    NULL, NULL,
					opl2_write,   NULL, NULL, &sb->opl);
	io_sethandler(0x0388,   0x0002, opl2_read,    NULL, NULL,
					opl2_write,   NULL, NULL, &sb->opl);
    }

    sb->cms_enabled = device_get_config_int("cms");
    if (sb->cms_enabled) {
	memset(&sb->cms, 0, sizeof(cms_t));
	io_sethandler(addr, 0x0004, cms_read, NULL, NULL, cms_write, NULL, NULL, &sb->cms);
    }

    sb->mixer_enabled = 0;
    sound_add_handler(sb_get_buffer_sb2, sb);
    sound_set_cd_audio_filter(sb2_filter_cd_audio, sb);

    if (device_get_config_int("receive_input"))
	midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &sb->dsp);

    return sb;
}


void *
sb_mcv_init(const device_t *info)
{
    /*SB1/2 port mappings, 210h to 260h in 10h steps
      2x6, 2xA, 2xC, 2xE -> DSP chip
      2x8, 2x9, 388 and 389 FM chip */
    sb_t *sb = malloc(sizeof(sb_t));
    memset(sb, 0, sizeof(sb_t));

    sb->opl_enabled = device_get_config_int("opl");
    if (sb->opl_enabled)
	opl2_init(&sb->opl);

    sb_dsp_init(&sb->dsp, SB15, SB_SUBTYPE_DEFAULT, sb);
    sb_dsp_setaddr(&sb->dsp, 0);
    sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
    sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));

    sb->mixer_enabled = 0;
    sound_add_handler(sb_get_buffer_sb2, sb);
    sound_set_cd_audio_filter(sb2_filter_cd_audio, sb);

    /* I/O handlers activated in sb_mcv_write */
    mca_add(sb_mcv_read, sb_mcv_write, sb_mcv_feedb, NULL, sb);
    sb->pos_regs[0] = 0x84;
    sb->pos_regs[1] = 0x50;

    if (device_get_config_int("receive_input"))
	midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &sb->dsp);

    return sb;
}


void *
sb_2_init(const device_t *info)
{
    /* SB2 port mappings. 220h or 240h.
       2x0 to 2x3 -> CMS chip
       2x6, 2xA, 2xC, 2xE -> DSP chip
       2x8, 2x9, 388 and 389 FM chip
       "CD version" also uses 250h or 260h for
       2x0 to 2x3 -> CDROM interface
       2x4 to 2x5 -> Mixer interface */
    /* My SB 2.0 mirrors the OPL2 at ports 2x0/2x1. Presumably this mirror is disabled when the
       CMS chips are present.
       This mirror may also exist on SB 1.5 & MCV, however I am unable to test this. It shouldn't
       exist on SB 1.0 as the CMS chips are always present there. Syndicate requires this mirror
       for music to play.*/
    sb_t *sb = malloc(sizeof(sb_t));
    uint16_t addr = device_get_config_hex16("base");
    uint16_t mixer_addr = device_get_config_int("mixaddr");

    memset(sb, 0, sizeof(sb_t));

    sb->opl_enabled = device_get_config_int("opl");
    if (sb->opl_enabled)
	opl2_init(&sb->opl);
    
    sb_dsp_init(&sb->dsp, SB2, SB_SUBTYPE_DEFAULT, sb);
    sb_dsp_setaddr(&sb->dsp, addr);
    sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
    sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
    if (mixer_addr > 0x000)
	sb_ct1335_mixer_reset(sb);

    sb->cms_enabled = device_get_config_int("cms");
    /* DSP I/O handler is activated in sb_dsp_setaddr */
    if (sb->opl_enabled) {
	if (!sb->cms_enabled) {
		io_sethandler(addr,     0x0002, opl2_read,    NULL, NULL,
						opl2_write,   NULL, NULL, &sb->opl);
	}
	io_sethandler(addr + 8, 0x0002, opl2_read,    NULL, NULL,
					opl2_write,   NULL, NULL, &sb->opl);
	io_sethandler(0x0388,   0x0002, opl2_read,    NULL, NULL,
					opl2_write,   NULL, NULL, &sb->opl);
    }

    if (sb->cms_enabled) {
	memset(&sb->cms, 0, sizeof(cms_t));
	io_sethandler(addr, 0x0004, cms_read, NULL, NULL, cms_write, NULL, NULL, &sb->cms);
    }

    if (mixer_addr > 0x000) {
	sb->mixer_enabled = 1;
	io_sethandler(mixer_addr + 4, 0x0002, sb_ct1335_mixer_read,  NULL, NULL,
		      sb_ct1335_mixer_write, NULL, NULL, sb);
    } else
	sb->mixer_enabled = 0;
    sound_add_handler(sb_get_buffer_sb2, sb);
    sound_set_cd_audio_filter(sb2_filter_cd_audio, sb);

    if (device_get_config_int("receive_input"))
	midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &sb->dsp);

    return sb;
}


static uint8_t
sb_pro_v1_opl_read(uint16_t port, void *priv)
{
    sb_t *sb = (sb_t *)priv;

    cycles -= ((int) (isa_timing * 8));

    (void)opl2_read(port, &sb->opl2);	// read, but ignore
    return(opl2_read(port, &sb->opl));
}


static void
sb_pro_v1_opl_write(uint16_t port, uint8_t val, void *priv)
{
    sb_t *sb = (sb_t *)priv;

    opl2_write(port, val, &sb->opl);
    opl2_write(port, val, &sb->opl2);
}


static void *
sb_pro_v1_init(const device_t *info)
{
    /* SB Pro port mappings. 220h or 240h.
       2x0 to 2x3 -> FM chip, Left and Right (9*2 voices)
       2x4 to 2x5 -> Mixer interface
       2x6, 2xA, 2xC, 2xE -> DSP chip
       2x8, 2x9, 388 and 389 FM chip (9 voices)
       2x0+10 to 2x0+13 CDROM interface. */
    sb_t *sb = malloc(sizeof(sb_t));
    uint16_t addr = device_get_config_hex16("base");
    memset(sb, 0, sizeof(sb_t));

    sb->opl_enabled = device_get_config_int("opl");
    if (sb->opl_enabled) {
	opl2_init(&sb->opl);
	opl_set_do_cycles(&sb->opl, 0);
	opl2_init(&sb->opl2);
	opl_set_do_cycles(&sb->opl2, 0);
    }

    sb_dsp_init(&sb->dsp, SBPRO, SB_SUBTYPE_DEFAULT, sb);
    sb_dsp_setaddr(&sb->dsp, addr);
    sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
    sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
    sb_ct1345_mixer_reset(sb);
     /* DSP I/O handler is activated in sb_dsp_setaddr */
    if (sb->opl_enabled) {
	io_sethandler(addr,     0x0002, opl2_read,    NULL, NULL,
					opl2_write,   NULL, NULL, &sb->opl);
	io_sethandler(addr + 2, 0x0002, opl2_read,    NULL, NULL,
					opl2_write,   NULL, NULL, &sb->opl2);
        io_sethandler(addr + 8, 0x0002, sb_pro_v1_opl_read,    NULL, NULL,
					sb_pro_v1_opl_write,   NULL, NULL, sb);
        io_sethandler(0x0388,   0x0002, sb_pro_v1_opl_read,    NULL, NULL,
					sb_pro_v1_opl_write,   NULL, NULL, sb);
    }

    sb->mixer_enabled = 1;
    io_sethandler(addr + 4, 0x0002, sb_ct1345_mixer_read,  NULL, NULL,
				    sb_ct1345_mixer_write, NULL, NULL, sb);
    sound_add_handler(sb_get_buffer_sbpro, sb);
    sound_set_cd_audio_filter(sbpro_filter_cd_audio, sb);

    if (device_get_config_int("receive_input"))
	midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &sb->dsp);

    return sb;
}


static void *
sb_pro_v2_init(const device_t *info)
{
    /* SB Pro port mappings. 220h or 240h.
       2x0 to 2x3 -> FM chip (18 voices)
       2x4 to 2x5 -> Mixer interface
       2x6, 2xA, 2xC, 2xE -> DSP chip
       2x8, 2x9, 388 and 389 FM chip (9 voices)
       2x0+10 to 2x0+13 CDROM interface. */
    sb_t *sb = malloc(sizeof(sb_t));
    uint16_t addr = device_get_config_hex16("base");
    memset(sb, 0, sizeof(sb_t));

    sb->opl_enabled = device_get_config_int("opl");
    if (sb->opl_enabled)
	opl3_init(&sb->opl);

    sb_dsp_init(&sb->dsp, SBPRO2, SB_SUBTYPE_DEFAULT, sb);
    sb_dsp_setaddr(&sb->dsp, addr);
    sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
    sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
    sb_ct1345_mixer_reset(sb);
    /* DSP I/O handler is activated in sb_dsp_setaddr */
    if (sb->opl_enabled) {
	io_sethandler(addr,     0x0004, opl3_read,    NULL, NULL,
					opl3_write,   NULL, NULL, &sb->opl);
        io_sethandler(addr + 8, 0x0002, opl3_read,    NULL, NULL,
					opl3_write,   NULL, NULL, &sb->opl);
	io_sethandler(0x0388,   0x0004, opl3_read,    NULL, NULL,
					opl3_write,   NULL, NULL, &sb->opl);
    }

    sb->mixer_enabled = 1;
    io_sethandler(addr + 4, 0x0002, sb_ct1345_mixer_read,  NULL, NULL,
				    sb_ct1345_mixer_write, NULL, NULL, sb);
    sound_add_handler(sb_get_buffer_sbpro, sb);
    sound_set_cd_audio_filter(sbpro_filter_cd_audio, sb);

    if (device_get_config_int("receive_input"))
	midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &sb->dsp);

    return sb;
}


static void *
sb_pro_mcv_init(const device_t *info)
{
    /*SB Pro port mappings. 220h or 240h.
      2x0 to 2x3 -> FM chip, Left and Right (18 voices)
      2x4 to 2x5 -> Mixer interface
      2x6, 2xA, 2xC, 2xE -> DSP chip
      2x8, 2x9, 388 and 389 FM chip (9 voices) */
    sb_t *sb = malloc(sizeof(sb_t));
    memset(sb, 0, sizeof(sb_t));

    sb->opl_enabled = 1;
    opl3_init(&sb->opl);

    sb_dsp_init(&sb->dsp, SBPRO2, SB_SUBTYPE_DEFAULT, sb);
    sb_ct1345_mixer_reset(sb);

    sb->mixer_enabled = 1;
    sound_add_handler(sb_get_buffer_sbpro, sb);
    sound_set_cd_audio_filter(sbpro_filter_cd_audio, sb);

    /* I/O handlers activated in sb_pro_mcv_write */
    mca_add(sb_pro_mcv_read, sb_pro_mcv_write, sb_mcv_feedb, NULL, sb);
    sb->pos_regs[0] = 0x03;
    sb->pos_regs[1] = 0x51;

    if (device_get_config_int("receive_input"))
	midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &sb->dsp);

    return sb;
}


static void *
sb_pro_compat_init(const device_t *info)
{
    sb_t *sb = malloc(sizeof(sb_t));
    memset(sb, 0, sizeof(sb_t));

    opl3_init(&sb->opl);

    sb_dsp_init(&sb->dsp, SBPRO2, SB_SUBTYPE_DEFAULT, sb);
    sb_ct1345_mixer_reset(sb);
 
    sb->mixer_enabled = 1;
    sound_add_handler(sb_get_buffer_sbpro, sb);
    sound_set_cd_audio_filter(sbpro_filter_cd_audio, sb);

    sb->mpu = (mpu_t *) malloc(sizeof(mpu_t));
    memset(sb->mpu, 0, sizeof(mpu_t));
    mpu401_init(sb->mpu, 0, 0, M_UART, 1);
    sb_dsp_set_mpu(&sb->dsp, sb->mpu);

    return sb;
}


static void *
sb_16_init(const device_t *info)
{
    sb_t *sb = malloc(sizeof(sb_t));
    uint16_t addr = device_get_config_hex16("base");
    uint16_t mpu_addr = device_get_config_hex16("base401");

    memset(sb, 0x00, sizeof(sb_t));

    sb->opl_enabled = device_get_config_int("opl");
    if (sb->opl_enabled)
	opl3_init(&sb->opl);

    sb_dsp_init(&sb->dsp, SB16, SB_SUBTYPE_DEFAULT, sb);
    sb_dsp_setaddr(&sb->dsp, addr);
    sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
    sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
    sb_dsp_setdma16(&sb->dsp, device_get_config_int("dma16"));
    sb_ct1745_mixer_reset(sb);

    if (sb->opl_enabled) {
	io_sethandler(addr,     0x0004, opl3_read,    NULL, NULL,
					opl3_write,   NULL, NULL, &sb->opl);
        io_sethandler(addr + 8, 0x0002, opl3_read,    NULL, NULL,
					opl3_write,   NULL, NULL, &sb->opl);
	io_sethandler(0x0388,   0x0004, opl3_read,    NULL, NULL,
					opl3_write,   NULL, NULL, &sb->opl);
    }

    sb->mixer_enabled = 1;
    io_sethandler(addr + 4, 0x0002, sb_ct1745_mixer_read,  NULL, NULL,
				    sb_ct1745_mixer_write, NULL, NULL, sb);
    sound_add_handler(sb_get_buffer_sb16_awe32, sb);
    sound_set_cd_audio_filter(sb16_awe32_filter_cd_audio, sb);

    if (mpu_addr) {
	sb->mpu = (mpu_t *) malloc(sizeof(mpu_t));
	memset(sb->mpu, 0, sizeof(mpu_t));
	mpu401_init(sb->mpu, device_get_config_hex16("base401"), device_get_config_int("irq"), M_UART, device_get_config_int("receive_input401"));
    } else
	sb->mpu = NULL;
    sb_dsp_set_mpu(&sb->dsp, sb->mpu);

    if (device_get_config_int("receive_input"))
	midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &sb->dsp);

    return sb;
}


static void *
sb_16_pnp_init(const device_t *info)
{
    sb_t *sb = malloc(sizeof(sb_t));
    memset(sb, 0x00, sizeof(sb_t));

    sb->opl_enabled = 1;
    opl3_init(&sb->opl);

    sb_dsp_init(&sb->dsp, SB16, SB_SUBTYPE_DEFAULT, sb);
    sb_ct1745_mixer_reset(sb);

    sb->mixer_enabled = 1;
    sound_add_handler(sb_get_buffer_sb16_awe32, sb);
    sound_set_cd_audio_filter(sb16_awe32_filter_cd_audio, sb);

    sb->mpu = (mpu_t *) malloc(sizeof(mpu_t));
    memset(sb->mpu, 0, sizeof(mpu_t));
    mpu401_init(sb->mpu, 0, 0, M_UART, device_get_config_int("receive_input401"));
    sb_dsp_set_mpu(&sb->dsp, sb->mpu);

    if (device_get_config_int("receive_input"))
	midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &sb->dsp);

    sb->gameport = gameport_add(&gameport_pnp_device);

    device_add(&ide_ter_pnp_device);

    isapnp_add_card(sb_16_pnp_rom, sizeof(sb_16_pnp_rom), sb_16_pnp_config_changed, NULL, NULL, NULL, sb);

    return sb;
}


static int
sb_awe32_available()
{
    return rom_present("roms/sound/awe32.raw");
}


static int
sb_32_pnp_available()
{
    return sb_awe32_available() && rom_present("roms/sound/CT3600 PnP.BIN");
}


static int
sb_awe32_pnp_available()
{
    return sb_awe32_available() && rom_present("roms/sound/CT3980 PnP.BIN");
}


static int
sb_awe64_gold_available()
{
    return sb_awe32_available() && rom_present("roms/sound/CT4540 PnP.BIN");
}


static void *
sb_awe32_init(const device_t *info)
{
    sb_t *sb = malloc(sizeof(sb_t));
    uint16_t addr = device_get_config_hex16("base");
    uint16_t mpu_addr = device_get_config_hex16("base401");
    uint16_t emu_addr = device_get_config_hex16("emu_base");
    int onboard_ram = device_get_config_int("onboard_ram");

    memset(sb, 0x00, sizeof(sb_t));

    sb->opl_enabled = device_get_config_int("opl");
    if (sb->opl_enabled)
	opl3_init(&sb->opl);

    sb_dsp_init(&sb->dsp, SBAWE32, SB_SUBTYPE_DEFAULT, sb);
    sb_dsp_setaddr(&sb->dsp, addr);
    sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
    sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
    sb_dsp_setdma16(&sb->dsp, device_get_config_int("dma16"));
    sb_ct1745_mixer_reset(sb);

    if (sb->opl_enabled) {
	io_sethandler(addr,     0x0004, opl3_read,    NULL, NULL,
					opl3_write,   NULL, NULL, &sb->opl);
        io_sethandler(addr + 8, 0x0002, opl3_read,    NULL, NULL,
					opl3_write,   NULL, NULL, &sb->opl);
	io_sethandler(0x0388,   0x0004, opl3_read,    NULL, NULL,
					opl3_write,   NULL, NULL, &sb->opl);
    }

    sb->mixer_enabled = 1;
    io_sethandler(addr + 4, 0x0002, sb_ct1745_mixer_read,  NULL, NULL,
				    sb_ct1745_mixer_write, NULL, NULL, sb);
    sound_add_handler(sb_get_buffer_sb16_awe32, sb);
    sound_set_cd_audio_filter(sb16_awe32_filter_cd_audio, sb);

    if (mpu_addr) {
	sb->mpu = (mpu_t *) malloc(sizeof(mpu_t));
	memset(sb->mpu, 0, sizeof(mpu_t));
	mpu401_init(sb->mpu, device_get_config_hex16("base401"), device_get_config_int("irq"), M_UART, device_get_config_int("receive_input401"));
    } else
	sb->mpu = NULL;
    sb_dsp_set_mpu(&sb->dsp, sb->mpu);

    emu8k_init(&sb->emu8k, emu_addr, onboard_ram);

    if (device_get_config_int("receive_input"))
	midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &sb->dsp);

    return sb;
}


static void *
sb_awe32_pnp_init(const device_t *info)
{
    sb_t *sb = malloc(sizeof(sb_t));
    int onboard_ram = device_get_config_int("onboard_ram");

    memset(sb, 0x00, sizeof(sb_t));

    sb->opl_enabled = 1;
    opl3_init(&sb->opl);

    sb_dsp_init(&sb->dsp, (info->local == 2) ? SBAWE64 : SBAWE32, SB_SUBTYPE_DEFAULT, sb);
    sb_ct1745_mixer_reset(sb);

    sb->mixer_enabled = 1;
    sound_add_handler(sb_get_buffer_sb16_awe32, sb);
    sound_set_cd_audio_filter(sb16_awe32_filter_cd_audio, sb);

    sb->mpu = (mpu_t *) malloc(sizeof(mpu_t));
    memset(sb->mpu, 0, sizeof(mpu_t));
    mpu401_init(sb->mpu, 0, 0, M_UART, device_get_config_int("receive_input401"));
    sb_dsp_set_mpu(&sb->dsp, sb->mpu);

    emu8k_init(&sb->emu8k, 0, onboard_ram);

    if (device_get_config_int("receive_input"))
	midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &sb->dsp);

    sb->gameport = gameport_add(&gameport_pnp_device);

    if (info->local != 2)
	device_add(&ide_ter_pnp_device);

    char *pnp_rom_file = NULL;
    switch (info->local) {
	case 0:
		pnp_rom_file = "roms/sound/CT3600 PnP.BIN";
		break;

	case 1:
		pnp_rom_file = "roms/sound/CT3980 PnP.BIN";
		break;

	case 2:
		pnp_rom_file = "roms/sound/CT4540 PnP.BIN";
		break;
    }

    uint8_t *pnp_rom = NULL;
    if (pnp_rom_file) {
	FILE *f = rom_fopen(pnp_rom_file, "rb");
	if (f) {
		if (fread(sb->pnp_rom, 1, 512, f) == 512)
			pnp_rom = sb->pnp_rom;
		fclose(f);
	}
    }

    switch (info->local) {
	case 0:
		isapnp_add_card(pnp_rom, sizeof(sb->pnp_rom), sb_16_pnp_config_changed, NULL, NULL, NULL, sb);
		break;

	case 1:
		isapnp_add_card(pnp_rom, sizeof(sb->pnp_rom), sb_awe32_pnp_config_changed, NULL, NULL, NULL, sb);
		break;

	case 2:
		isapnp_add_card(pnp_rom, sizeof(sb->pnp_rom), sb_awe64_gold_pnp_config_changed, NULL, NULL, NULL, sb);
		break;
    }

    return sb;
}


void
sb_close(void *p)
{
    sb_t *sb = (sb_t *)p;
    sb_dsp_close(&sb->dsp);

    free(sb);
}


static void
sb_awe32_close(void *p)
{
    sb_t *sb = (sb_t *)p;

    emu8k_close(&sb->emu8k);

    sb_close(sb);
}


void
sb_speed_changed(void *p)
{
    sb_t *sb = (sb_t *)p;

    sb_dsp_speed_changed(&sb->dsp);
}


static const device_config_t sb_config[] =
{
        {
                "base", "Address", CONFIG_HEX16, "", 0x220, "", { 0 },
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
				""
			}
                }
        },
        {
                "irq", "IRQ", CONFIG_SELECTION, "", 7, "", { 0 },
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
                "dma", "DMA", CONFIG_SELECTION, "", 1, "", { 0 },
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
		"receive_input", "Receive input (SB MIDI)", CONFIG_BINARY, "", 1
	},
        {
                "", "", -1
        }
};


static const device_config_t sb15_config[] =
{
        {
                "base", "Address", CONFIG_HEX16, "", 0x220, "", { 0 },
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
				""
			}
                }
        },
        {
                "irq", "IRQ", CONFIG_SELECTION, "", 7, "", { 0 },
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
                "dma", "DMA", CONFIG_SELECTION, "", 1, "", { 0 },
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
		"cms", "Enable CMS", CONFIG_BINARY, "", 0
	},
	{
		"receive_input", "Receive input (SB MIDI)", CONFIG_BINARY, "", 1
	},
        {
                "", "", -1
        }
};


static const device_config_t sb2_config[] =
{
        {
                "base", "Address", CONFIG_HEX16, "", 0x220, "", { 0 },
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
				""
			}
                }
        },
        {
                "mixaddr", "Mixer", CONFIG_HEX16, "", 0, "", { 0 },
                {
                        {
                                "Disabled", 0
                        },
                        {
                                "0x220", 0x220
                        },
                        {
                                "0x240", 0x240
                        },
                        {
                                "0x250", 0x250
                        },
                        {
                                "0x260", 0x260
                        },
	                {
				""
			}
                }
        },
        {
                "irq", "IRQ", CONFIG_SELECTION, "", 7, "", { 0 },
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
                "dma", "DMA", CONFIG_SELECTION, "", 1, "", { 0 },
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
		"cms", "Enable CMS", CONFIG_BINARY, "", 0
	},
	{
		"receive_input", "Receive input (SB MIDI)", CONFIG_BINARY, "", 1
	},
        {
                "", "", -1
        }
};

static const device_config_t sb_mcv_config[] =
{
        {
                "irq", "IRQ", CONFIG_SELECTION, "", 7, "", { 0 },
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
                "dma", "DMA", CONFIG_SELECTION, "", 1, "", { 0 },
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
		"receive_input", "Receive input (SB MIDI)", CONFIG_BINARY, "", 1
	},
        {
                "", "", -1
        }
};

static const device_config_t sb_pro_config[] =
{
        {
                "base", "Address", CONFIG_HEX16, "", 0x220, "", { 0 },
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
                "irq", "IRQ", CONFIG_SELECTION, "", 7, "", { 0 },
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
                "dma", "DMA", CONFIG_SELECTION, "", 1, "", { 0 },
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
		"receive_input", "Receive input (SB MIDI)", CONFIG_BINARY, "", 1
	},
        {
                "", "", -1
        }
};

static const device_config_t sb_16_config[] =
{
        {
                "base", "Address", CONFIG_HEX16, "", 0x220, "", { 0 },
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
                "base401", "MPU-401 Address", CONFIG_HEX16, "", 0x330, "", { 0 },
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
                "irq", "IRQ", CONFIG_SELECTION, "", 5, "", { 0 },
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
                "dma", "Low DMA channel", CONFIG_SELECTION, "", 1, "", { 0 },
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
                "dma16", "High DMA channel", CONFIG_SELECTION, "", 5, "", { 0 },
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
		"receive_input", "Receive input (SB MIDI)", CONFIG_BINARY, "", 1
	},
	{
		"receive_input401", "Receive input (MPU-401)", CONFIG_BINARY, "", 0
	},
        {
                "", "", -1
        }
};

static const device_config_t sb_16_pnp_config[] =
{
	{
		"receive_input", "Receive input (SB MIDI)", CONFIG_BINARY, "", 1
	},
	{
		"receive_input401", "Receive input (MPU-401)", CONFIG_BINARY, "", 0
	},
        {
                "", "", -1
        }
};

static const device_config_t sb_32_pnp_config[] =
{
        {
                "onboard_ram", "Onboard RAM", CONFIG_SELECTION, "", 0, "", { 0 },
                {
                        {
                                "None", 0
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
		"receive_input", "Receive input (SB MIDI)", CONFIG_BINARY, "", 1
	},
	{
		"receive_input401", "Receive input (MPU-401)", CONFIG_BINARY, "", 0
	},
        {
                "", "", -1
        }
};

static const device_config_t sb_awe32_config[] =
{
        {
                "base", "Address", CONFIG_HEX16, "", 0x220, "", { 0 },
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
                "emu_base", "EMU8000 Address", CONFIG_HEX16, "", 0x620, "", { 0 },
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
                "base401", "MPU-401 Address", CONFIG_HEX16, "", 0x330, "", { 0 },
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
                "irq", "IRQ", CONFIG_SELECTION, "", 5, "", { 0 },
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
                "dma", "Low DMA channel", CONFIG_SELECTION, "", 1, "", { 0 },
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
                "dma16", "High DMA channel", CONFIG_SELECTION, "", 5, "", { 0 },
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
                "onboard_ram", "Onboard RAM", CONFIG_SELECTION, "", 512, "", { 0 },
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
		"receive_input", "Receive input (SB MIDI)", CONFIG_BINARY, "", 1
	},
	{
		"receive_input401", "Receive input (MPU-401)", CONFIG_BINARY, "", 0
	},
        {
                "", "", -1
        }
};

static const device_config_t sb_awe32_pnp_config[] =
{
        {
                "onboard_ram", "Onboard RAM", CONFIG_SELECTION, "", 512, "", { 0 },
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
		"receive_input", "Receive input (SB MIDI)", CONFIG_BINARY, "", 1
	},
	{
		"receive_input401", "Receive input (MPU-401)", CONFIG_BINARY, "", 0
	},
        {
                "", "", -1
        }
};

static const device_config_t sb_awe64_gold_config[] =
{
        {
                "onboard_ram", "Onboard RAM", CONFIG_SELECTION, "", 4096, "", { 0 },
                {
                        {
                                "4 MB", 4096
                        },
                        {
                                "8 MB", 8192
                        },
                        {
                                "12 MB", 12288
                        },
                        {
                                "16 MB", 16384
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
		"receive_input", "Receive input (SB MIDI)", CONFIG_BINARY, "", 1
	},
	{
		"receive_input401", "Receive input (MPU-401)", CONFIG_BINARY, "", 0
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
        sb_1_init, sb_close, NULL, { NULL },
        sb_speed_changed,
        NULL,
        sb_config
};

const device_t sb_15_device =
{
        "Sound Blaster v1.5",
        DEVICE_ISA,
	0,
        sb_15_init, sb_close, NULL, { NULL },
        sb_speed_changed,
        NULL,
        sb15_config
};

const device_t sb_mcv_device =
{
        "Sound Blaster MCV",
        DEVICE_MCA,
	0,
        sb_mcv_init, sb_close, NULL, { NULL },
        sb_speed_changed,
        NULL,
        sb_mcv_config
};

const device_t sb_2_device =
{
        "Sound Blaster v2.0",
        DEVICE_ISA,
	0,
        sb_2_init, sb_close, NULL, { NULL },
        sb_speed_changed,
        NULL,
        sb2_config
};

const device_t sb_pro_v1_device =
{
        "Sound Blaster Pro v1",
        DEVICE_ISA,
	0,
        sb_pro_v1_init, sb_close, NULL, { NULL },
        sb_speed_changed,
        NULL,
        sb_pro_config
};

const device_t sb_pro_v2_device =
{
        "Sound Blaster Pro v2",
        DEVICE_ISA,
	0,
        sb_pro_v2_init, sb_close, NULL, { NULL },
        sb_speed_changed,
        NULL,
        sb_pro_config
};

const device_t sb_pro_mcv_device =
{
        "Sound Blaster Pro MCV",
        DEVICE_MCA,
	0,
        sb_pro_mcv_init, sb_close, NULL, { NULL },
        sb_speed_changed,
        NULL,
        NULL
};

const device_t sb_pro_compat_device =
{
        "Sound Blaster Pro (Compatibility)",
        DEVICE_ISA | DEVICE_AT,
	0,
        sb_pro_compat_init, sb_close, NULL, { NULL },
        sb_speed_changed,
        NULL,
        NULL
};

const device_t sb_16_device =
{
        "Sound Blaster 16",
        DEVICE_ISA | DEVICE_AT,
	0,
        sb_16_init, sb_close, NULL, { NULL },
        sb_speed_changed,
        NULL,
        sb_16_config
};

const device_t sb_16_pnp_device =
{
        "Sound Blaster 16 PnP",
        DEVICE_ISA | DEVICE_AT,
	0,
        sb_16_pnp_init, sb_close, NULL, { NULL },
        sb_speed_changed,
        NULL,
        sb_16_pnp_config
};

const device_t sb_32_pnp_device =
{
        "Sound Blaster 32 PnP",
        DEVICE_ISA | DEVICE_AT,
	0,
        sb_awe32_pnp_init, sb_awe32_close, NULL,
        { sb_32_pnp_available },
        sb_speed_changed,
        NULL,
        sb_32_pnp_config
};


const device_t sb_awe32_device =
{
        "Sound Blaster AWE32",
        DEVICE_ISA | DEVICE_AT,
	0,
        sb_awe32_init, sb_awe32_close, NULL,
        { sb_awe32_available },
        sb_speed_changed,
        NULL,
        sb_awe32_config
};

const device_t sb_awe32_pnp_device =
{
        "Sound Blaster AWE32 PnP",
        DEVICE_ISA | DEVICE_AT,
	1,
        sb_awe32_pnp_init, sb_awe32_close, NULL,
        { sb_awe32_pnp_available },
        sb_speed_changed,
        NULL,
        sb_awe32_pnp_config
};

const device_t sb_awe64_gold_device =
{
        "Sound Blaster AWE64 Gold",
        DEVICE_ISA | DEVICE_AT,
	2,
        sb_awe32_pnp_init, sb_awe32_close, NULL,
        { sb_awe64_gold_available },
        sb_speed_changed,
        NULL,
        sb_awe64_gold_config
};
