/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
/*It is unknown exactly what RAMDAC this is
  It is possibly a Sierra 1502x
  It's addressed by the TLIVESA1 driver for ET4000*/
/* Note by Tenshi: Not possibly, this *IS* a Sierra 1502x. */
#include "../ibm.h"
#include "../mem.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_sc1502x_ramdac.h"


void sc1502x_ramdac_out(uint16_t addr, uint8_t val, sc1502x_ramdac_t *ramdac, svga_t *svga)
{
	int oldbpp = 0;
        switch (addr)
        {
                case 0x3C6:
                if (ramdac->state == 4)
                {
                        ramdac->state = 0;
			if (val == 0xFF)  break;
                        ramdac->ctrl = val;
			oldbpp = svga->bpp;
                        switch ((val&1)|((val&0xC0)>>5))
                        {
                                case 0:
                                svga->bpp = 8;
                                break;
                                case 2: case 3:
				switch (val & 0x20)
				{
					case 0x00: svga->bpp = 32; break;
	                                case 0x20: svga->bpp = 24; break;
				}
                                break;
                                case 4: case 5:
                                svga->bpp = 15;
                                break;
                                case 6:
                                svga->bpp = 16;
                                break;
                                case 7:
                                switch (val & 4)
                                {
                                        case 4:
				                switch (val & 0x20)
				                {
					                case 0x00: svga->bpp = 32; break;
	                                                case 0x20: svga->bpp = 24; break;
				                }
                                                break;
                                        case 0: default:
                                                svga->bpp = 16;
                                                break;
                                }
				case 1: default:
				break;
                        }
			if (oldbpp != svga->bpp)
			{
				svga_recalctimings(svga);
			}
                        return;
                }
                ramdac->state = 0;
                break;
                case 0x3C7: case 0x3C8: case 0x3C9:
                ramdac->state = 0;
                break;
        }
        svga_out(addr, val, svga);
}

uint8_t sc1502x_ramdac_in(uint16_t addr, sc1502x_ramdac_t *ramdac, svga_t *svga)
{
        switch (addr)
        {
                case 0x3C6:
                if (ramdac->state == 4)
                {
                        ramdac->state = 0;
                        return ramdac->ctrl;
                }
                ramdac->state++;
                break;
                case 0x3C7: case 0x3C8: case 0x3C9:
                ramdac->state = 0;
                break;
        }
        return svga_in(addr, svga);
}
