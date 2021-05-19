/*
  AD1848 / CS4248 / CS4231 CODEC emulation (Windows Sound System compatible)*/

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

#define CS4231	  		0x80
#define CS4236	  		0x03

static int ad1848_vols_6bits[64];
static double ad1848_vols_5bits_aux_gain[32];


void ad1848_setirq(ad1848_t *ad1848, int irq)
{
        ad1848->irq = irq;
}

void ad1848_setdma(ad1848_t *ad1848, int dma)
{
        ad1848->dma = dma;
}

uint8_t ad1848_read(uint16_t addr, void *p)
{
        ad1848_t *ad1848 = (ad1848_t *)p;
        uint8_t temp = 0xff;
        switch (addr & 3)
        {
                case 0: /*Index*/
                temp = ad1848->index | ad1848->trd | ad1848->mce;
                break;
                case 1:
                temp = ad1848->regs[ad1848->index];
                if (ad1848->index == 0x0b) {
			temp ^= 0x20;
			ad1848->regs[ad1848->index] = temp;
		}
                        break;
                case 2:
                temp = ad1848->status;
                break;
        }
        return temp;
}

void ad1848_write(uint16_t addr, uint8_t val, void *p)
{
        ad1848_t *ad1848 = (ad1848_t *)p;
        double freq;
        switch (addr & 3)
        {
                case 0: /*Index*/
                if ((ad1848->regs[12] & 0x40) && (ad1848->type >= AD1848_TYPE_CS4231))
                        ad1848->index = val & 0x1f; /* cs4231a extended mode enabled */
                else
                        ad1848->index = val & 0x0f; /* ad1848/cs4248 mode TODO: some variants/clones DO NOT mirror, just ignore the writes? */
                ad1848->trd   = val & 0x20;
                ad1848->mce   = val & 0x40;
                break;
                case 1:
                switch (ad1848->index)
                {
                        case 8:
                        freq = (val & 1) ? 16934400LL : 24576000LL;
                        switch ((val >> 1) & 7)
                        {
                                case 0: freq /= 3072; break;
                                case 1: freq /= 1536; break;
                                case 2: freq /= 896;  break;
                                case 3: freq /= 768;  break;
                                case 4: freq /= 448;  break;
                                case 5: freq /= 384;  break;
                                case 6: freq /= 512;  break;
                                case 7: freq /= 2560; break;
                        }
                        ad1848->freq = freq;
                        ad1848->timer_latch = (uint64_t)((double)TIMER_USEC * (1000000.0 / (double)ad1848->freq));
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
                        break;
                        
                        case 12:
			if (ad1848->type != AD1848_TYPE_DEFAULT)
				ad1848->regs[12] = ((ad1848->regs[12] & 0x0f) + (val & 0xf0)) | 0x80;
                        return;
                        
                        case 14:
                        ad1848->count = ad1848->regs[15] | (val << 8);
                        break;

                        case 24:
                        if (! (val & 0x70))
                                ad1848->status &= 0xfe;
			break;

                        case 25:
				break;
                }
                ad1848->regs[ad1848->index] = val;

                if (ad1848->type == AD1848_TYPE_CS4231) { /* TODO: configure CD volume for CS4248/AD1848 too */
                        if (ad1848->regs[0x12] & 0x80)
                                ad1848->cd_vol_l = 0;
                        else
                                ad1848->cd_vol_l = ad1848_vols_5bits_aux_gain[ad1848->regs[0x12] & 0x1f];
                        if (ad1848->regs[0x13] & 0x80)
                                ad1848->cd_vol_r = 0;
                        else
                                ad1848->cd_vol_r = ad1848_vols_5bits_aux_gain[ad1848->regs[0x13] & 0x1f];
                }
                break;
                case 2:
                ad1848->status &= 0xfe;
                break;              
        }
}

void ad1848_speed_changed(ad1848_t *ad1848)
{
        ad1848->timer_latch = (uint64_t)((double)TIMER_USEC * (1000000.0 / (double)ad1848->freq));
}

void ad1848_update(ad1848_t *ad1848)
{
        for (; ad1848->pos < sound_pos_global; ad1848->pos++)
        {
                ad1848->buffer[ad1848->pos*2]     = ad1848->out_l;
                ad1848->buffer[ad1848->pos*2 + 1] = ad1848->out_r;
        }
}

static void ad1848_poll(void *p)
{
        ad1848_t *ad1848 = (ad1848_t *)p;
 
        if (ad1848->timer_latch)
		timer_advance_u64(&ad1848->timer_count, ad1848->timer_latch);
        else
		timer_advance_u64(&ad1848->timer_count, TIMER_USEC * 1000);

        ad1848_update(ad1848);
        
        if (ad1848->enable)
        {
                int32_t temp;
                
                switch (ad1848->regs[8] & 0x70)
                {
                        case 0x00: /*Mono, 8-bit PCM*/
                        ad1848->out_l = ad1848->out_r = (dma_channel_read(ad1848->dma) ^ 0x80) * 256;
                        break;
                        case 0x10: /*Stereo, 8-bit PCM*/
                        ad1848->out_l = (dma_channel_read(ad1848->dma) ^ 0x80)  * 256;
                        ad1848->out_r = (dma_channel_read(ad1848->dma) ^ 0x80)  * 256;
                        break;
                
                        case 0x40: /*Mono, 16-bit PCM*/
                        temp = dma_channel_read(ad1848->dma);
                        ad1848->out_l = ad1848->out_r = (dma_channel_read(ad1848->dma) << 8) | temp;
                        break;
                        case 0x50: /*Stereo, 16-bit PCM*/
                        temp = dma_channel_read(ad1848->dma);
                        ad1848->out_l = (dma_channel_read(ad1848->dma) << 8) | temp;
                        temp = dma_channel_read(ad1848->dma);
                        ad1848->out_r = (dma_channel_read(ad1848->dma) << 8) | temp;
                        break;
                }

                if (ad1848->regs[6] & 0x80)
                        ad1848->out_l = 0;
                else
                        ad1848->out_l = (ad1848->out_l * ad1848_vols_6bits[ad1848->regs[6] & 0x3f]) >> 16;

                if (ad1848->regs[7] & 0x80)
                        ad1848->out_r = 0;
                else
                        ad1848->out_r = (ad1848->out_r * ad1848_vols_6bits[ad1848->regs[7] & 0x3f]) >> 16;
                
                if (ad1848->count < 0)
                {
                        ad1848->count = ad1848->regs[15] | (ad1848->regs[14] << 8);
                        if (!(ad1848->status & 0x01))
                        {
                                ad1848->status |= 0x01;
                                if (ad1848->regs[10] & 2)
                                        picint(1 << ad1848->irq);
                        }                                
                }

                ad1848->count--;
        }
        else
        {
                ad1848->out_l = ad1848->out_r = 0;
		ad1848->cd_vol_l = ad1848->cd_vol_r = 0;
        }
}

static void ad1848_filter_cd_audio(int channel, double *buffer, void *p)
{
        ad1848_t *ad1848 = (ad1848_t *)p;
	double c;
	double volume = channel ? ad1848->cd_vol_r : ad1848->cd_vol_l;

        c = ((*buffer) * volume) / 65536.0;
	*buffer = c;
}

void ad1848_init(ad1848_t *ad1848, int type)
{
        int c;
        double attenuation;
                        
        ad1848->status = 0xcc;
        ad1848->index = ad1848->trd = 0;
        ad1848->mce = 0x40;
        
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
        }
	
        ad1848->out_l = 0;
        ad1848->out_r = 0;
        
        for (c = 0; c < 64; c++) {
                attenuation = 0.0;
                if (c & 0x01) attenuation -= 1.5;
                if (c & 0x02) attenuation -= 3.0;
                if (c & 0x04) attenuation -= 6.0;
                if (c & 0x08) attenuation -= 12.0;
                if (c & 0x10) attenuation -= 24.0;
                if (c & 0x20) attenuation -= 48.0;
                
                attenuation = pow(10, attenuation / 10);
                
                ad1848_vols_6bits[c] = (int)(attenuation * 65536);
        }
        
        for (c = 0; c < 32; c++) {
                attenuation = 12.0;
                if (c & 0x01) attenuation -= 1.5;
                if (c & 0x02) attenuation -= 3.0;
                if (c & 0x04) attenuation -= 6.0;
                if (c & 0x08) attenuation -= 12.0;
                if (c & 0x10) attenuation -= 24.0;

                attenuation = pow(10, attenuation / 10);

                ad1848_vols_5bits_aux_gain[c] = (attenuation * 65536);
        }	
	
	ad1848->type = type;
	
	timer_add(&ad1848->timer_count, ad1848_poll, ad1848, 0);

	if (ad1848->type != AD1848_TYPE_DEFAULT && ad1848->type != AD1848_TYPE_CS4248)
		sound_set_cd_audio_filter(ad1848_filter_cd_audio, ad1848);
}
