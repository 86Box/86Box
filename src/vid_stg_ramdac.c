/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
/*STG1702 true colour RAMDAC emulation*/
#include "ibm.h"
#include "mem.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_stg_ramdac.h"

static int stg_state_read[2][8] = {{1,2,3,4,0,0,0,0}, {1,2,3,4,5,6,7,7}};
static int stg_state_write[8] = {0,0,0,0,0,6,7,7};
static int stg_state_indexed = 0;

void stg_ramdac_set_bpp(svga_t *svga, stg_ramdac_t *ramdac)
{
	int oldbpp = svga->bpp;
	if (ramdac->command & 0x8)
	{
	        switch (ramdac->regs[3])
        	{
	                case 0: case 5: case 7: svga->bpp = 8;  break;
        	        case 1: case 2: case 8: svga->bpp = 15; break;
	                case 3: case 6:         svga->bpp = 16; break;
        	        case 4: case 9:         svga->bpp = 24; break;
	                default:                svga->bpp = 8; break;
	        }
	}
	else
	{
                switch (ramdac->command >> 5)
                {
                        case 0:  svga->bpp =  8; break;
                        case 5:  svga->bpp = 15; break;
                        case 6:  svga->bpp = 16; break;
                        case 7:  svga->bpp = 24; break;
                        default: svga->bpp =  8; break;
                }
	}
	svga_recalctimings(svga);
}

void stg_ramdac_out(uint16_t addr, uint8_t val, stg_ramdac_t *ramdac, svga_t *svga)
{
        int didwrite, old;
        //if (CS!=0xC000) pclog("OUT RAMDAC %04X %02X %i %04X:%04X\n",addr,val,stg_ramdac.magic_count,CS,pc);
        switch (addr)
        {
                case 0x3c6:
                switch (ramdac->magic_count)
                {
                        case 0: case 1: case 2: case 3: 
                        break;
                        case 4: 
			old = ramdac->command;
                        ramdac->command = val; 
			if ((old ^ val) & 8)
			{
				stg_ramdac_set_bpp(svga, ramdac);
			}
			else
			{
				if ((old ^ val) & 0xE0)
				{
					stg_ramdac_set_bpp(svga, ramdac);
				}
			}
                        // pclog("Write RAMDAC command %02X\n",val);
                        break;
                        case 5: 
                        ramdac->index = (ramdac->index & 0xff00) | val; 
                        break;
                        case 6: 
                        ramdac->index = (ramdac->index & 0xff) | (val << 8); 
                        break;
                        case 7:
                        // pclog("Write RAMDAC reg %02X %02X\n", ramdac->index, val);
                        if (ramdac->index < 0x100)
			{ 
                                ramdac->regs[ramdac->index] = val;
			}
			if ((ramdac->index == 3) && (ramdac->command & 8))  stg_ramdac_set_bpp(svga, ramdac);
                        ramdac->index++;
                        break;
                }
                didwrite = (ramdac->magic_count >= 4);
                ramdac->magic_count = stg_state_write[ramdac->magic_count & 7];
                if (didwrite) return;
                break;
                case 0x3c7: case 0x3c8: case 0x3c9:
                ramdac->magic_count=0;
                break;
        }
        svga_out(addr, val, svga);
}

uint8_t stg_ramdac_in(uint16_t addr, stg_ramdac_t *ramdac, svga_t *svga)
{
        uint8_t temp;
        //if (CS!=0xC000) pclog("IN RAMDAC %04X %04X:%04X\n",addr,CS,pc);
        switch (addr)
        {
                case 0x3c6:
                switch (ramdac->magic_count)
                {
                        case 0: case 1: case 2: case 3: 
                        temp = 0xff;
                        break;
                        case 4: 
                        temp = ramdac->command; 
                        break;
                        case 5: 
                        temp = ramdac->index & 0xff; 
                        break;
                        case 6: 
                        temp = ramdac->index >> 8; 
                        break;
                        case 7:
                                // pclog("Read RAMDAC index %04X\n",ramdac->index);
                        switch (ramdac->index)
                        {
                                case 0: 
                                temp = 0x44; 
                                break;
                                case 1: 
                                temp = 0x03; 
                                break;
				case 7:
				temp = 0x88;
				break;
                                default:
                                if (ramdac->index < 0x100) temp = ramdac->regs[ramdac->index];
                                else                       temp = 0xff;
                                break;
                        }
                        ramdac->index++;
                        break;
                }
                ramdac->magic_count = stg_state_read[(ramdac->command & 0x10) ? 1 : 0][ramdac->magic_count & 7];
                return temp;
                case 0x3c7: case 0x3c8: case 0x3c9:
                ramdac->magic_count=0;
                break;
        }
        return svga_in(addr, svga);
}

float stg_getclock(int clock, void *p)
{
        stg_ramdac_t *ramdac = (stg_ramdac_t *)p;
        float t;
        int m, n1, n2;
	float d;
//        pclog("STG_Getclock %i %04X\n", clock, ramdac->regs[clock]);
        if (clock == 0) return 25175000.0;
        if (clock == 1) return 28322000.0;
        clock ^= 1; /*Clocks 2 and 3 seem to be reversed*/
        m  =  (ramdac->regs[clock] & 0x7f) + 2;		/* B+2 */
        n1 = ((ramdac->regs[clock] >>  8) & 0x1f) + 2;	/* N1+2 */
        n2 = ((ramdac->regs[clock] >> 13) & 0x07);	/* D */
	switch (n2)
	{
		case 0:
			d = 1.0;
			break;
		case 1:
			d = 2.0;
			break;
		case 2:
			d = 4.0;
			break;
		case 3:
			d = 8.0;
			break;
	}
        // t = (14318184.0 * ((float)m / (float)n1)) / (float)(1 << n2);
	t = (14318184.0 * ((float)m / d)) / (float)n1;
//        pclog("STG clock %i %i %i %f %04X  %f %i\n", m, n1, n2, t, ramdac->regs[2], 14318184.0 * ((float)m / (float)n1), 1 << n2);
        return t;
}
