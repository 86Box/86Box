/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		AD1848 / CS4248 / CS4231 (Windows Sound System) codec emulation.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2018-2020 TheCollector1995.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <math.h>
#include <86box/86box.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/sound.h>
#include <86box/snd_ad1848.h>


#define CS4231			0x80
#define CS4236			0x03


static int	ad1848_vols_7bits[128];
static double	ad1848_vols_5bits_aux_gain[32];
static double	ad1848_vols_7bits_debug[128];
static double	ad1848_vols_5bits_debug[32];


void
ad1848_setirq(ad1848_t *ad1848, int irq)
{
    ad1848->irq = irq;
}


void
ad1848_setdma(ad1848_t *ad1848, int dma)
{
    ad1848->dma = dma;
}


void
ad1848_updatevolmask(ad1848_t *ad1848)
{
    if ((ad1848->type == AD1848_TYPE_CS4236) && ((ad1848->xregs[4] & 0x10) || ad1848->wten))
	ad1848->wave_vol_mask = 0x3f;
    else
	ad1848->wave_vol_mask = 0x7f;
}


static void
ad1848_updatefreq(ad1848_t *ad1848)
{
    double freq;
    uint8_t set = 0;

    if (ad1848->type == AD1848_TYPE_CS4236) {
	if (ad1848->xregs[11] & 0x20) {
		freq = 16934400LL;
		switch (ad1848->xregs[13]) {
			case 1: freq /= 353;  break;
			case 2: freq /= 529;  break;
			case 3: freq /= 617;  break;
			case 4: freq /= 1058; break;
			case 5: freq /= 1764; break;
			case 6: freq /= 2117; break;
			case 7: freq /= 2558; break;
			default: freq /= 16 * MAX(ad1848->xregs[13], 21); break;
		}
		set = 1;
	} else if (ad1848->regs[22] & 0x80) {
		freq = (ad1848->regs[22] & 1) ? 33868800LL : 49152000LL;
		set = (ad1848->regs[22] >> 1) & 0x3f;
		switch (ad1848->regs[10] & 0x30) {
			case 0x00: freq /= 128 * set; break;
			case 0x10: freq /= 64  * set; break;
			case 0x20: freq /= 256 * set; break;
		}
		set = 1;
	}
    }

    if (!set) {
	freq = (ad1848->regs[8] & 1) ? 16934400LL : 24576000LL;
	switch ((ad1848->regs[8] >> 1) & 7) {
		case 0: freq /= 3072; break;
		case 1: freq /= 1536; break;
		case 2: freq /= 896;  break;
		case 3: freq /= 768;  break;
		case 4: freq /= 448;  break;
		case 5: freq /= 384;  break;
		case 6: freq /= 512;  break;
		case 7: freq /= 2560; break;
	}
    }

    ad1848->freq = freq;
    ad1848->timer_latch = (uint64_t) ((double) TIMER_USEC * (1000000.0 / (double) ad1848->freq));
}


uint8_t
ad1848_read(uint16_t addr, void *priv)
{
    ad1848_t *ad1848 = (ad1848_t *) priv;
    uint8_t ret = 0xff;

    switch (addr & 3) {
	case 0: /* Index */
		ret = ad1848->index | ad1848->trd | ad1848->mce;
		break;

	case 1:
		ret = ad1848->regs[ad1848->index];
		switch (ad1848->index) {
			case 11:
				ret ^= 0x20;
				ad1848->regs[ad1848->index] = ret;
				break;

			case 18: case 19:
				if (ad1848->type == AD1848_TYPE_CS4236) {
					if (ad1848->xregs[4] & 0x04) /* FM remapping */
						return ad1848->xregs[ad1848->index - 12]; /* real FM volume on registers 6 and 7 */
					else if (ad1848->xregs[4] & 0x08) /* wavetable remapping */
						return ad1848->xregs[ad1848->index - 2]; /* real wavetable volume on registers 16 and 17 */
				}
				break;

			case 23:
				if ((ad1848->type == AD1848_TYPE_CS4236) && (ad1848->regs[23] & 0x08)) {
					if ((ad1848->xindex & 0xfe) == 0x00) /* remapped line volume */
						ret = ad1848->regs[18 + ad1848->xindex];
					else
						ret = ad1848->xregs[ad1848->xindex];
				}
				break;
		}
		break;

	case 2:
		ret = ad1848->status;
		break;
    }

    return ret;
}


void
ad1848_write(uint16_t addr, uint8_t val, void *priv)
{
    ad1848_t *ad1848 = (ad1848_t *) priv;
    uint8_t temp, updatefreq = 0;

    switch (addr & 3) {
	case 0: /* Index */
		if ((ad1848->regs[12] & 0x40) && (ad1848->type >= AD1848_TYPE_CS4231))
			ad1848->index = val & 0x1f; /* cs4231a extended mode enabled */
		else
			ad1848->index = val & 0x0f; /* ad1848/cs4248 mode TODO: some variants/clones DO NOT mirror, just ignore the writes? */
		if (ad1848->type == AD1848_TYPE_CS4236)
			ad1848->regs[23] &= ~0x08; /* clear XRAE */
		ad1848->trd = val & 0x20;
		ad1848->mce = val & 0x40;
	break;

	case 1:
		switch (ad1848->index) {
			case 10:
				if (ad1848->type != AD1848_TYPE_CS4236)
					break;
				/* fall-through */

			case 8:
				updatefreq = 1;
				break;
			
			case 9:
				if (!ad1848->enable && (val & 0x41) == 0x01) {
					if (ad1848->timer_latch)
						timer_set_delay_u64(&ad1848->timer_count, ad1848->timer_latch);
					else
						timer_set_delay_u64(&ad1848->timer_count, TIMER_USEC);
				}
				ad1848->enable = ((val & 0x41) == 0x01);
				if (!ad1848->enable) {
					timer_disable(&ad1848->timer_count);
					ad1848->out_l = ad1848->out_r = 0;
				}
				break;

			case 11:
				return;
			
			case 12:
				if (ad1848->type != AD1848_TYPE_DEFAULT)
					ad1848->regs[12] = ((ad1848->regs[12] & 0x0f) + (val & 0xf0)) | 0x80;
				return;

			case 14:
				ad1848->count = ad1848->regs[15] | (val << 8);
				break;

			case 17:
				if (ad1848->type >= AD1848_TYPE_CS4231) /* enable additional data formats on modes 2 and 3 */
					ad1848->fmt_mask = (val & 0x40) ? 0xf0 : 0x70;
				break;

			case 18: case 19:
				if (ad1848->type == AD1848_TYPE_CS4236) {
					if (ad1848->xregs[4] & 0x04) { /* FM remapping */
						temp = val;
						val = ad1848->xregs[ad1848->index - 18]; /* real line volume on registers 0 and 1 */
						ad1848->xregs[ad1848->index - 12] = temp; /* real FM volume on registers 6 and 7 */
					} else if (ad1848->xregs[4] & 0x08) { /* wavetable remapping */
						temp = val;
						val = ad1848->xregs[ad1848->index - 18]; /* real line volume on registers 0 and 1 */
						ad1848->xregs[ad1848->index - 2] = temp; /* real wavetable volume on registers 16 and 17 */
					}
				}
				break;

			case 22:
				updatefreq = 1;
				break;

			case 23:
				if ((ad1848->type == AD1848_TYPE_CS4236) && ((ad1848->regs[12] & 0x60) == 0x60)) {
					if (!(ad1848->regs[23] & 0x08)) { /* existing (not new) XRAE is clear */
						ad1848->xindex = (((val & 0x04) << 2) | (val >> 4)) & 0x1f;
						break;
					}
					switch (ad1848->xindex) {
						case 0: case 1: /* remapped line volume */
							ad1848->regs[18 + ad1848->xindex] = val;
							return;

						case 6:
							if (val & 0x80)
								ad1848->fm_vol_l = 0;
							else
								ad1848->fm_vol_l = ad1848_vols_7bits[val & 0x3f];
							break;

						case 7:
							if (val & 0x80)
								ad1848->fm_vol_r = 0;
							else
								ad1848->fm_vol_r = ad1848_vols_7bits[val & 0x3f];
							break;

						case 11: case 13:
							updatefreq = 1;
							break;

						case 25:
							return;
					}
					ad1848->xregs[ad1848->xindex] = val;

					if (updatefreq)
						ad1848_updatefreq(ad1848);

					return;
				}
				break;

			case 24:
				if (!(val & 0x70))
					ad1848->status &= 0xfe;
				break;

			case 25:
				return;
		}
		ad1848->regs[ad1848->index] = val;

		if (updatefreq)
			ad1848_updatefreq(ad1848);

		if ((ad1848->type == AD1848_TYPE_CS4231) || (ad1848->type == AD1848_TYPE_CS4236)) { /* TODO: configure CD volume for CS4248/AD1848 too */
			temp = (ad1848->type == AD1848_TYPE_CS4231) ? 18 : 4;
			if (ad1848->regs[temp] & 0x80)
				ad1848->cd_vol_l = 0;
			else
				ad1848->cd_vol_l = ad1848_vols_5bits_aux_gain[ad1848->regs[temp] & 0x1f];
			temp++;
			if (ad1848->regs[temp] & 0x80)
				ad1848->cd_vol_r = 0;
			else
				ad1848->cd_vol_r = ad1848_vols_5bits_aux_gain[ad1848->regs[temp] & 0x1f];
		}
		break;

	case 2:
		ad1848->status &= 0xfe;
		break;
    }
}


void
ad1848_speed_changed(ad1848_t *ad1848)
{
    ad1848->timer_latch = (uint64_t) ((double) TIMER_USEC * (1000000.0 / (double) ad1848->freq));
}


void
ad1848_update(ad1848_t *ad1848)
{
    for (; ad1848->pos < sound_pos_global; ad1848->pos++) {
	ad1848->buffer[ad1848->pos*2]     = ad1848->out_l;
	ad1848->buffer[ad1848->pos*2 + 1] = ad1848->out_r;
    }
}


static void
ad1848_poll(void *priv)
{
    ad1848_t *ad1848 = (ad1848_t *) priv;

    if (ad1848->timer_latch)
	timer_advance_u64(&ad1848->timer_count, ad1848->timer_latch);
    else
	timer_advance_u64(&ad1848->timer_count, TIMER_USEC * 1000);

    ad1848_update(ad1848);

    if (ad1848->enable) {
	int32_t temp;

	switch (ad1848->regs[8] & ad1848->fmt_mask) {
		case 0x00: /* Mono, 8-bit PCM */
			ad1848->out_l = ad1848->out_r = (dma_channel_read(ad1848->dma) ^ 0x80) * 256;
			break;

		case 0x10: /* Stereo, 8-bit PCM */
			ad1848->out_l = (dma_channel_read(ad1848->dma) ^ 0x80) * 256;
			ad1848->out_r = (dma_channel_read(ad1848->dma) ^ 0x80) * 256;
			break;
	
		case 0x40: /* Mono, 16-bit PCM little endian */
			temp = dma_channel_read(ad1848->dma);
			ad1848->out_l = ad1848->out_r = (dma_channel_read(ad1848->dma) << 8) | temp;
			break;

		case 0x50: /* Stereo, 16-bit PCM little endian */
			temp = dma_channel_read(ad1848->dma);
			ad1848->out_l = (dma_channel_read(ad1848->dma) << 8) | temp;
			temp = dma_channel_read(ad1848->dma);
			ad1848->out_r = (dma_channel_read(ad1848->dma) << 8) | temp;
			break;

		case 0xc0: /* Mono, 16-bit PCM big endian */
			temp = dma_channel_read(ad1848->dma);
			ad1848->out_l = ad1848->out_r = dma_channel_read(ad1848->dma) | (temp << 8);
			break;

		case 0xd0: /* Stereo, 16-bit PCM big endian */
			temp = dma_channel_read(ad1848->dma);
			ad1848->out_l = dma_channel_read(ad1848->dma) | (temp << 8);
			temp = dma_channel_read(ad1848->dma);
			ad1848->out_r = dma_channel_read(ad1848->dma) | (temp << 8);
			break;
	}

	if (ad1848->regs[6] & 0x80)
		ad1848->out_l = 0;
	else
		ad1848->out_l = (ad1848->out_l * ad1848_vols_7bits[ad1848->regs[6] & ad1848->wave_vol_mask]) >> 16;

	if (ad1848->regs[7] & 0x80)
		ad1848->out_r = 0;
	else
		ad1848->out_r = (ad1848->out_r * ad1848_vols_7bits[ad1848->regs[7] & ad1848->wave_vol_mask]) >> 16;
	
	if (ad1848->count < 0) {
		ad1848->count = ad1848->regs[15] | (ad1848->regs[14] << 8);
		if (!(ad1848->status & 0x01)) {
			ad1848->status |= 0x01;
			if (ad1848->regs[10] & 2)
				picint(1 << ad1848->irq);
		}				
	}

	ad1848->count--;
    } else {
	ad1848->out_l = ad1848->out_r = 0;
	ad1848->cd_vol_l = ad1848->cd_vol_r = 0;
    }
}


void
ad1848_filter_cd_audio(int channel, double *buffer, void *priv)
{
    ad1848_t *ad1848 = (ad1848_t *) priv;
    double c;
    double volume = channel ? ad1848->cd_vol_r : ad1848->cd_vol_l;

    c = ((*buffer) * volume) / 65536.0;
    *buffer = c;
}


void
ad1848_init(ad1848_t *ad1848, int type)
{
    int c;
    double attenuation;
			
    ad1848->status = 0xcc;
    ad1848->index = ad1848->trd = 0;
    ad1848->mce = 0x40;
    ad1848->wten = 0;

    ad1848->regs[0] = ad1848->regs[1] = 0;
    ad1848->regs[2] = ad1848->regs[3] = 0x80; /* Line-in */
    ad1848->regs[4] = ad1848->regs[5] = 0x80;
    ad1848->regs[6] = ad1848->regs[7] = 0x80; /* Left/right Output */
    ad1848->regs[8] = 0;
    ad1848->regs[9] = 0x08;
    ad1848->regs[10] = ad1848->regs[11] = 0;
    if ((type == AD1848_TYPE_CS4248) || (type == AD1848_TYPE_CS4231) || (type == AD1848_TYPE_CS4236))
	ad1848->regs[12] = 0x8a;
    else
	ad1848->regs[12] = 0xa;
    ad1848->regs[13] = 0;
    ad1848->regs[14] = ad1848->regs[15] = 0;

    if (type == AD1848_TYPE_CS4231) {
	ad1848->regs[16] = ad1848->regs[17] = 0;
	ad1848->regs[18] = ad1848->regs[19] = 0x88;
	ad1848->regs[22] = 0x80;
	ad1848->regs[24] = 0;
	ad1848->regs[25] = CS4231;
	ad1848->regs[26] = 0x80;
	ad1848->regs[29] = 0x80;
    } else if (type == AD1848_TYPE_CS4236) {
	ad1848->regs[16] = ad1848->regs[17] = 0;
	ad1848->regs[18] = ad1848->regs[19] = 0;
	ad1848->regs[20] = ad1848->regs[21] = 0;
	ad1848->regs[22] = ad1848->regs[23] = 0;
	ad1848->regs[24] = 0;
	ad1848->regs[25] = CS4236;
	ad1848->regs[26] = 0xa0;
	ad1848->regs[27] = ad1848->regs[29] = 0;
	ad1848->regs[30] = ad1848->regs[31] = 0;

	ad1848->xregs[0] = ad1848->xregs[1] = 0xe8;
	ad1848->xregs[2] = ad1848->xregs[3] = 0xcf;
	ad1848->xregs[4] = 0x84;
	ad1848->xregs[5] = 0;
	ad1848->xregs[6] = ad1848->xregs[7] = 0x80;
	ad1848->xregs[8] = ad1848->xregs[9] = 0;
	ad1848->xregs[10] = 0x3f;
	ad1848->xregs[11] = 0xc0;
	ad1848->xregs[14] = ad1848->xregs[15] = 0;
	ad1848->xregs[16] = ad1848->xregs[17] = 0;
    }

    ad1848->out_l = ad1848->out_r = 0;
    ad1848->fm_vol_l = ad1848->fm_vol_r = 1;
    ad1848_updatevolmask(ad1848);
    ad1848->fmt_mask = 0x70;

    for (c = 0; c < 128; c++) {
	attenuation = 0.0;
	if (c & 0x40) {
		if (c < 72)   attenuation  = (c - 72) * -1.5;
	} else {
		if (c & 0x01) attenuation -= 1.5;
		if (c & 0x02) attenuation -= 3.0;
		if (c & 0x04) attenuation -= 6.0;
		if (c & 0x08) attenuation -= 12.0;
		if (c & 0x10) attenuation -= 24.0;
		if (c & 0x20) attenuation -= 48.0;
	}
ad1848_vols_7bits_debug[c] = attenuation;
	attenuation = pow(10, attenuation / 10);

	ad1848_vols_7bits[c] = (int) (attenuation * 65536);
    }

    for (c = 0; c < 32; c++) {
	attenuation = 12.0;
	if (c & 0x01) attenuation -= 1.5;
	if (c & 0x02) attenuation -= 3.0;
	if (c & 0x04) attenuation -= 6.0;
	if (c & 0x08) attenuation -= 12.0;
	if (c & 0x10) attenuation -= 24.0;
ad1848_vols_5bits_debug[c] = attenuation;
	attenuation = pow(10, attenuation / 10);

	ad1848_vols_5bits_aux_gain[c] = (attenuation * 65536);
    }

    ad1848->type = type;

    timer_add(&ad1848->timer_count, ad1848_poll, ad1848, 0);

    if ((ad1848->type != AD1848_TYPE_DEFAULT) && (ad1848->type != AD1848_TYPE_CS4248))
	sound_set_cd_audio_filter(ad1848_filter_cd_audio, ad1848);
}
