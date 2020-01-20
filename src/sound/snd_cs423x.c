#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <math.h>
#include "../86box.h"
#include "../dma.h"
#include "../pic.h"
#include "../timer.h"
#include "sound.h"
#include "snd_cs423x.h"

static int cs423x_vols[64];

void cs423x_setirq(cs423x_t *cs423x, int irq)
{
        cs423x->irq = irq;
}

void cs423x_setdma(cs423x_t *cs423x, int dma)
{
        cs423x->dma = dma;
}

uint8_t cs423x_read(uint16_t addr, void *p)
{
        cs423x_t *cs423x = (cs423x_t *)p;
        uint8_t temp = 0xff;

		if(cs423x->initb)
			return 0x80;
        switch (addr & 3)
        {
                case 0: /*Index*/
		if(cs423x->mode2)
			temp = cs423x->index | cs423x->trd | cs423x->mce | cs423x->ia4 | cs423x->initb;
			
		else 
			temp = cs423x->index | cs423x->trd | cs423x->mce | cs423x->initb;			
                break;
                
		case 1:
                temp = cs423x->regs[cs423x->index];
				if (cs423x->index == 0x0b) { 
					temp ^= 0x20;
					cs423x->regs[cs423x->index] = temp;					
				}
                break;
                
		case 2:
                temp = cs423x->status;
                break;
		
		case 3: // Capture I/O read
		break;
        }
        return temp;
}

void cs423x_write(uint16_t addr, uint8_t val, void *p)
{
        cs423x_t *cs423x = (cs423x_t *)p;
        double freq;

        switch (addr & 3)
        {
                case 0: /*Index*/
		cs423x->index = val & (cs423x->mode2 ? 0x1F : 0x0F);
                cs423x->trd   = val & 0x20;
                cs423x->mce   = val & 0x40;
		cs423x->ia4  = val & 0x10;
		cs423x->initb = val & 0x80;
                break;
		
                case 1:
                switch (cs423x->index) {
                        case 8:
                        freq = (val & 1) ? 16934400 : 24576000;
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
                        cs423x->freq = freq;
                        cs423x->timer_latch = (uint64_t)((double)TIMER_USEC * (1000000.0 / (double)cs423x->freq));
                        break;
                        
                        case 9:
			if (!cs423x->enable && (val & 0x41) == 0x01) {
				if (cs423x->timer_latch)
					timer_set_delay_u64(&cs423x->timer_count, cs423x->timer_latch);
				else
					timer_set_delay_u64(&cs423x->timer_count, TIMER_USEC);
			}
			cs423x->enable = ((val & 0x41) == 0x01);
                        if (!cs423x->enable) {
				timer_disable(&cs423x->timer_count);
                                cs423x->out_l = cs423x->out_r = 0;
			}
                        break;
                                
                        case 12:
			val |= 0x8a; 
			cs423x->mode2 = (val >> 6) & 1;
			break;
                        
                        case 14:
                        cs423x->count = cs423x->regs[15] | (val << 8);
                        break;
						
			case 11:
			break;
						
			case 24:
			if (!(val & 0x70)) {
				cs423x->status &= 0xfe;
			}
			break;
						
			case 25:
			break;
                }
                cs423x->regs[cs423x->index] = val;
                break;
                case 2:
                cs423x->status &= 0xfe;
                break;              
		case 3: // Playback I/O Write
		break;
        }
}

void cs423x_speed_changed(cs423x_t *cs423x)
{
        cs423x->timer_latch = (uint64_t)((double)TIMER_USEC * (1000000.0 / (double)cs423x->freq));
}

void cs423x_update(cs423x_t *cs423x)
{
	for (; cs423x->pos < sound_pos_global; cs423x->pos++)
        {
                cs423x->buffer[cs423x->pos*2]     = cs423x->out_l;
                cs423x->buffer[cs423x->pos*2 + 1] = cs423x->out_r;
        }
}

static void cs423x_poll(void *p)
{
        cs423x_t *cs423x = (cs423x_t *)p;
 
        if (cs423x->timer_latch)
                timer_advance_u64(&cs423x->timer_count, cs423x->timer_latch);
        else
                timer_advance_u64(&cs423x->timer_count, TIMER_USEC * 1000);
        
        cs423x_update(cs423x);
        
        if (cs423x->enable)
        {
                int32_t temp;
                
                 if(cs423x->mode2)	{
				switch (cs423x->regs[8] & 0xf0)
				{
				case 0x00: /*Mono, 8-bit PCM*/
				cs423x->out_l = cs423x->out_r = (dma_channel_read(cs423x->dma) ^ 0x80) * 256;
				break;
				
				case 0x10: /*Stereo, 8-bit PCM*/
				cs423x->out_l = (dma_channel_read(cs423x->dma) ^ 0x80)  * 256;
				cs423x->out_r = (dma_channel_read(cs423x->dma) ^ 0x80)  * 256;
				break;
                
				case 0x40: /*Mono, 16-bit PCM*/
				case 0xc0: /*Mono, 16-bit PCM Big-Endian should not happen on x86*/
				temp = dma_channel_read(cs423x->dma);
				cs423x->out_l = cs423x->out_r = (dma_channel_read(cs423x->dma) << 8) | temp;
				break;
				
				case 0x50: /*Stereo, 16-bit PCM*/
				case 0xd0: /*Stereo, 16-bit PCM Big-Endian. Should not happen on x86*/
				temp = dma_channel_read(cs423x->dma);
				cs423x->out_l = (dma_channel_read(cs423x->dma) << 8) | temp;
				temp = dma_channel_read(cs423x->dma);
				cs423x->out_r = (dma_channel_read(cs423x->dma) << 8) | temp;
				break;
							
				case 0xa0: /*Mono, ADPCM, 4-bit*/
				case 0xb0: /*Stereo, ADPCM, 4-bit*/
				break;
				}
				
			}
		else {
				switch (cs423x->regs[8] & 0x70)
				{
				case 0x00: /*Mono, 8-bit PCM*/
				cs423x->out_l = cs423x->out_r = (dma_channel_read(cs423x->dma) ^ 0x80) * 256;
				break;
				
				case 0x10: /*Stereo, 8-bit PCM*/
				cs423x->out_l = (dma_channel_read(cs423x->dma) ^ 0x80)  * 256;
				cs423x->out_r = (dma_channel_read(cs423x->dma) ^ 0x80)  * 256;
				break;
                
				case 0x40: /*Mono, 16-bit PCM*/
				temp = dma_channel_read(cs423x->dma);
				cs423x->out_l = cs423x->out_r = (dma_channel_read(cs423x->dma) << 8) | temp;
				break;
				
				case 0x50: /*Stereo, 16-bit PCM*/
				temp = dma_channel_read(cs423x->dma);
				cs423x->out_l = (dma_channel_read(cs423x->dma) << 8) | temp;
				temp = dma_channel_read(cs423x->dma);
				cs423x->out_r = (dma_channel_read(cs423x->dma) << 8) | temp;
				break;

				}
		}

                if (cs423x->regs[6] & 0x80)
                        cs423x->out_l = 0;
                else
                        cs423x->out_l = (cs423x->out_l * cs423x_vols[cs423x->regs[6] & 0x3f]) >> 16;

                if (cs423x->regs[7] & 0x80)
                        cs423x->out_r = 0;
                else
                        cs423x->out_r = (cs423x->out_r * cs423x_vols[cs423x->regs[7] & 0x3f]) >> 16;
	
		if (cs423x->count < 0)  {
                        cs423x->count = cs423x->regs[15] | (cs423x->regs[14] << 8);
			if (!(cs423x->status & 0x01))	{
				cs423x->status |= 0x01;
				if (cs423x->regs[0xa] & 2)
					picint(1 << cs423x->irq);
			}
                }
                cs423x->count--;
        }
        else  {
                cs423x->out_l = cs423x->out_r = 0;
                //pclog("cs423x_poll : not enable\n");
        }
}

void cs423x_init(cs423x_t *cs423x)
{
        int c;
        double attenuation;

        cs423x->enable = 0;
                        
        cs423x->status = 0xcc;
        cs423x->index = cs423x->trd = 0;
        cs423x->mce = 0x40;
	cs423x->initb = 0;
		
	cs423x->mode2 = 0;
        
        cs423x->regs[0] = cs423x->regs[1] = 0;
        cs423x->regs[2] = cs423x->regs[3] = 0x88;
        cs423x->regs[4] = cs423x->regs[5] = 0x88;
        cs423x->regs[6] = cs423x->regs[7] = 0x80;
        cs423x->regs[8] = 0;
        cs423x->regs[9] = 0x08;
        cs423x->regs[10] = cs423x->regs[11] = 0;
	cs423x->regs[12] = 0x8a;
        cs423x->regs[13] = 0;
        cs423x->regs[14] = cs423x->regs[15] = 0;
	cs423x->regs[16] = cs423x->regs[17] = 0;
	cs423x->regs[18] = cs423x->regs[19] = 0x88;
	cs423x->regs[22] = 0x80;
	cs423x->regs[24] = 0;
	cs423x->regs[25] = 0x80; /*CS4231 for GUS MAX*/
	cs423x->regs[26] = 0x80;
	cs423x->regs[29] = 0x80;        
        
        cs423x->out_l = 0;
        cs423x->out_r = 0;
        
        for (c = 0; c < 64; c++)
        {
                attenuation = 0.0;
                if (c & 0x01) attenuation -= 1.5;
                if (c & 0x02) attenuation -= 3.0;
                if (c & 0x04) attenuation -= 6.0;
                if (c & 0x08) attenuation -= 12.0;
                if (c & 0x10) attenuation -= 24.0;
                if (c & 0x20) attenuation -= 48.0;
                
                attenuation = pow(10, attenuation / 10);      
                cs423x_vols[c] = (int)(attenuation * 65536);
        }       
        timer_add(&cs423x->timer_count, cs423x_poll, cs423x, 0);
}
