/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the Cirrus Logic RAMDAC.
 *
 * Version:	@(#)vid_cl_ramdac.c	1.0.1	2017/10/16
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016,2017 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../ibm.h"
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_cl_ramdac.h"
#include "vid_cl_gd.h"
#include "vid_cl_gd_blit.h"


void cl_ramdac_out(uint16_t addr, uint8_t val, cl_ramdac_t *ramdac, void *clgd, svga_t *svga)
{
	clgd_t *real_clgd = (clgd_t *) clgd;
        //pclog("OUT RAMDAC %04X %02X\n",addr,val);
	switch (addr)
	{
		case 0x3C6:
		if (ramdac->state == 4)
		{
			ramdac->state = 0;
			ramdac->ctrl = val;
			svga_recalctimings(svga);
			return;
		}
		ramdac->state = 0;
		break;
					
		case 0x3C7: case 0x3C8:
		ramdac->state = 0;
		break;
		
		case 0x3C9:
		if (svga->seqregs[0x12] & CIRRUS_CURSOR_HIDDENPEL)
		{
			ramdac->state = 0;
			svga->fullchange = changeframecount;
                	switch (svga->dac_pos)
                	{
                        	case 0: 
                        	real_clgd->hiddenpal[svga->dac_write & 0xf].r = val & 63;
                        	svga->dac_pos++; 
                        	break;
                        	case 1: 
                        	real_clgd->hiddenpal[svga->dac_write & 0xf].g = val & 63;
                        	svga->dac_pos++; 
                        	break;
                        	case 2: 
                        	real_clgd->hiddenpal[svga->dac_write & 0xf].b = val & 63;
                        	svga->dac_pos = 0; 
                        	svga->dac_write = (svga->dac_write + 1) & 255; 
                        	break;
                	}
					return;
		}
		ramdac->state = 0;
		break;
	}
        svga_out(addr, val, svga);
}

uint8_t cl_ramdac_in(uint16_t addr, cl_ramdac_t *ramdac, void *clgd, svga_t *svga)
{
	clgd_t *real_clgd = (clgd_t *) clgd;
        //pclog("IN RAMDAC %04X\n",addr);
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
                case 0x3C7: case 0x3C8:
                ramdac->state = 0;
                break;
                case 0x3C9:
		if (svga->seqregs[0x12] & CIRRUS_CURSOR_HIDDENPEL)
		{
                	ramdac->state = 0;
                	switch (svga->dac_pos)
                	{
                        	case 0: 
                        	svga->dac_pos++; 
                        	return real_clgd->hiddenpal[svga->dac_read & 0xf].r;
                        	case 1: 
                        	svga->dac_pos++; 
                        	return real_clgd->hiddenpal[svga->dac_read & 0xf].g;
                        	case 2: 
                        	svga->dac_pos=0; 
                        	svga->dac_read = (svga->dac_read + 1) & 255; 
                        	return real_clgd->hiddenpal[(svga->dac_read - 1) & 15].b;
                	}
		}
                ramdac->state = 0;
                break;
        }
        return svga_in(addr, svga);
}
