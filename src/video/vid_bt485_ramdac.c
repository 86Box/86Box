/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Brooktree BT485 true colour RAMDAC emulation.
 *
 *
 * Version:	@(#)vid_bt485_ramdac.c	1.0.4	2018/09/30
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		TheCollector1995,
 *
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2018 TheCollector1995.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../mem.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_bt485_ramdac.h"


void
bt485_ramdac_out(uint16_t addr, int rs2, int rs3, uint8_t val, bt485_ramdac_t *ramdac, svga_t *svga)
{
    uint32_t o32;
    uint8_t *cd;

    switch (addr) {
	case 0x3C6:
		if (rs2) {
			if (rs3) { /*REG0E, Hardware Cursor Y-position*/
				ramdac->hwc_y = (ramdac->hwc_y & 0x0f00) | val;
				svga->hwcursor.y = ramdac->hwc_y - svga->hwcursor.ysize;
				/* pclog("BT485 0E Y=%d\n", ramdac->hwc_y); */
				break;
			} else { /*REG06, Command Reg 0*/
				ramdac->cr0 = val;
				svga->ramdac_type = (val & 0x01) ? RAMDAC_8BIT : RAMDAC_6BIT;
				break;
			}
		} else {
			if (rs3) { /*REG0A*/
				switch (ramdac->set_reg0a) {
					case 0: /*Status, read-only*/
						break;
					
					case 1: /*Command Reg 3*/
						ramdac->cr3 = val;
						svga->hwcursor.xsize = svga->hwcursor.ysize = (val & 4) ? 64 : 32;
						svga->hwcursor.yoff = (svga->hwcursor.ysize == 32) ? 32 : 0;
						svga->hwcursor.x = ramdac->hwc_x - svga->hwcursor.xsize;
						svga->hwcursor.y = ramdac->hwc_y - svga->hwcursor.ysize;
						if (svga->hwcursor.xsize == 64)
							svga->dac_pos = (svga->dac_pos & 0x00ff) | ((val & 0x03) << 8);
						svga_recalctimings(svga);
						break;
				}
				break;
			} else { /*REG02*/
				svga_out(addr, val, svga);
				break;
			}
		}
		break;

	case 0x3C7:
		if (!rs2 && !rs3) { /*REG03*/
			svga_out(addr, val, svga);
			break;
		} else if (rs2 && !rs3) { /*REG07, Cursor/Overscan Read Address*/
			svga->dac_read = val;
			svga->dac_pos = 0;
			break;
		} else if (!rs2 && rs3) { /*REG0B, Cursor Ram Data*/
			if (svga->hwcursor.xsize == 64)
				cd = (uint8_t *) ramdac->cursor64_data;
			else
				cd = (uint8_t *) ramdac->cursor32_data;

			cd[svga->dac_pos] = val;
			svga->dac_pos++;
			if (svga->hwcursor.xsize == 32)
				svga->dac_pos &= 0x00ff;
			else
				svga->dac_pos &= 0x03ff;
			break;
		} else { /*REG0F, Hardware Cursor Y-position*/
			ramdac->hwc_y = (ramdac->hwc_y & 0x00ff) | ((val & 0x0f) << 8);
			svga->hwcursor.y = ramdac->hwc_y - svga->hwcursor.ysize;
			/* pclog("BT485 0F Y=%d\n", ramdac->hwc_y); */
			break;				
		}
		break;

	case 0x3C8:
		ramdac->set_reg0a = (ramdac->cr0 & 0x80) ? 1 : 0;
		if (rs2) {
			if (rs3) { /*REG0C, Hardware Cursor X-position*/
				ramdac->hwc_x = (ramdac->hwc_x & 0x0f00) | val;
				svga->hwcursor.x = ramdac->hwc_x - svga->hwcursor.xsize;
				/* pclog("BT485 0C X=%d\n", ramdac->hwc_x); */
				break;
			}
			else { /*REG04, Cursor/Overscan Write Address*/
				svga->dac_write = val;
				svga->dac_read = val - 1;
				svga->dac_pos = 0;
				break;
			}
		} else {
			if (rs3) { /*REG08, Command Reg 1*/
				ramdac->cr1 = val;
				switch (val >> 5) {
					case 0:
						if ((svga->gdcreg[5] & 0x40) && (svga->crtc[0x3a] & 0x10))
							svga->bpp = 32;
						else
							svga->bpp = 8;
						break;

					case 1:
						if (ramdac->cr1 & 8)
							svga->bpp = 16;
						else
							svga->bpp = 15;
						break;

					case 2:
						svga->bpp = 8;
						break;

					case 3:
						svga->bpp = 4;
						break;
				}
				svga_recalctimings(svga);
				break;			
			}
			else { /*REG00*/
				svga_out(addr, val, svga);
				break;
			}
		}
		break;
		
	case 0x3C9:
		if (rs2) {
			if (rs3) { /*REG0D, Hardware Cursor X-position*/
				ramdac->hwc_x = (ramdac->hwc_x & 0x00ff) | ((val & 0x0f) << 8);
				svga->hwcursor.x = ramdac->hwc_x - svga->hwcursor.xsize;
				/* pclog("BT485 0D X=%d\n", ramdac->hwc_x); */
				break;
			} else { /*REG05, Cursor/Overscan Data*/
				svga->dac_status = 0;
				svga->fullchange = changeframecount;
				switch (svga->dac_pos) {
					case 0:
						svga->dac_r = val;
						svga->dac_pos++; 
						break;
					case 1:
						svga->dac_g = val;
						svga->dac_pos++; 
						break;
					case 2:
						ramdac->extpal[svga->dac_write].r = svga->dac_r;
						ramdac->extpal[svga->dac_write].g = svga->dac_g;
						ramdac->extpal[svga->dac_write].b = val; 
						if (svga->ramdac_type == RAMDAC_8BIT)
							ramdac->extpallook[svga->dac_write & 3] = makecol32(ramdac->extpal[svga->dac_write].r & 0x3f, ramdac->extpal[svga->dac_write].g & 0x3f, ramdac->extpal[svga->dac_write].b & 0x3f);
						else
							ramdac->extpallook[svga->dac_write & 3] = makecol32(video_6to8[ramdac->extpal[svga->dac_write].r & 0x3f], video_6to8[ramdac->extpal[svga->dac_write].g & 0x3f], video_6to8[ramdac->extpal[svga->dac_write].b & 0x3f]);
						
						if ((svga->crtc[0x33] & 0x40) && ((svga->dac_write & 3) == 0)) {
							o32 = svga->overscan_color;
							svga->overscan_color = ramdac->extpallook[0];
							if (o32 != svga->overscan_color)
								svga_recalctimings(svga);
						}
						svga->dac_write = (svga->dac_write + 1) & 15;
						svga->dac_pos = 0; 
					break;
				}
				break;				
			}
		}
		else {
			if (rs3) { /*REG09, Command Reg 2*/
				ramdac->cr2 = val;
				svga->hwcursor.ena = ramdac->cr2 & 0x03;
				svga_recalctimings(svga);
				break;
			} else { /*REG01*/
				svga_out(addr, val, svga);
				break;				
			}
		}
		break;
    }
    return;
}


uint8_t
bt485_ramdac_in(uint16_t addr, int rs2, int rs3, bt485_ramdac_t *ramdac, svga_t *svga)
{
    uint8_t temp = 0xff;
    uint8_t *cd;

    switch (addr) {
	case 0x3C6:
		if (rs2) {
			if (rs3)	/*REG0E, Hardware Cursor Y-position, write only*/
				return 0xff;
			else		/*REG06, Command Reg 0*/
				return ramdac->cr0;	
		} else {
			if (rs3) { /*REG0A*/
				switch (ramdac->set_reg0a) {
					case 0: /*Status, read-only*/
						return 0x0b; /*Bt485*/

					case 1: /*Command Reg 3*/
						if (ramdac->cr2 & 4) {
							if (ramdac->cr3 & 2)
								temp = 0xa9;
							else
								temp = 0xa8;
						} else
							temp = ramdac->cr3;
						temp &= 0xfc;
						if (svga->hwcursor.xsize == 64)
							temp |= (svga->dac_pos >> 8) & 0x03;
						return temp;
				}
				return 0xff;
			} else /*REG02*/
				return svga_in(addr, svga);
		}
		break;
		
	case 0x3C7:
		if (rs2) {
			if (rs3)	/*REG0F, Hardware Cursor Y-position, write only*/
				return 0xff;
			else		/*REG07, Cursor/Overscan Read Address*/
				return svga->dac_status;
		} else {
			if (rs3) { /*REG0B, Cursor Ram Data*/
				if (svga->hwcursor.xsize == 64)
					cd = (uint8_t *) ramdac->cursor64_data;
				else
					cd = (uint8_t *) ramdac->cursor32_data;

				temp = cd[svga->dac_pos];
				svga->dac_pos++;
				if (svga->hwcursor.xsize == 32)
					svga->dac_pos &= 0x00ff;
				else
					svga->dac_pos &= 0x03ff;
				return temp;
			} else /*REG03*/
				return svga_in(addr, svga);
		}
		break;

	case 0x3C8:
		if (rs2) {
			if (rs3) /*REG0C, Hardware Cursor X-position, write only*/
				return 0xff;
			else /*REG04, Cursor/Overscan Write Address*/
				return svga->dac_write;
		} else {
			if (rs3) /*REG08, Command Reg 1*/
				return ramdac->cr1;
			else /*REG00*/
				return svga_in(addr, svga);
		}
		break;

	case 0x3C9:
		if (rs2) {
			if (rs3) /*REG0D, Hardware Cursor X-position, write only*/
				return 0xff;
			else { /*REG05, Cursor/Overscan Data*/
				svga->dac_status = 0;
				switch (svga->dac_pos) {
					case 0:
						svga->dac_pos++;
						return ramdac->extpal[svga->dac_read].r & 0x3f;
					case 1: 
						svga->dac_pos++;
						return ramdac->extpal[svga->dac_read].g & 0x3f;
					case 2: 
						svga->dac_pos=0;
						svga->dac_read = (svga->dac_read + 1) & 15;
						return ramdac->extpal[(svga->dac_read - 1) & 15].b & 0x3f;
						
				}
				return 0xff;
			}
		} else {
			if (rs3) /*REG09, Command Reg 2*/
				return ramdac->cr2;
			else /*REG01*/
				return svga_in(addr, svga);
		}
		break;
    }

    return temp;
}
