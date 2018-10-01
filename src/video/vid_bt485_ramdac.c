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
 * Version:	@(#)vid_bt485_ramdac.c	1.0.5	2018/01/10
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
    uint8_t rs = (addr & 0x03);
    rs |= (!!rs2 << 2);
    rs |= (!!rs3 << 3);

    switch (rs) {
	case 0x00:	/* Palette Write Index Register (RS value = 0000) */
	case 0x01:	/* Palette Data Register (RS value = 0001) */
	case 0x02:	/* Pixel Read Mask Register (RS value = 0010) */
	case 0x03:	/* Palette Read Index Register (RS value = 0011) */
	case 0x04:	/* Ext Palette Write Index Register (RS value = 0100) */
	case 0x07:	/* Ext Palette Read Index Register (RS value = 0111) */
		svga_out(addr, val, svga);
		break;
	case 0x05:	/* Ext Palette Data Register (RS value = 0101) */
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
				ramdac->extpal[svga->dac_write & 3].r = svga->dac_r;
				ramdac->extpal[svga->dac_write & 3].g = svga->dac_g;
				ramdac->extpal[svga->dac_write & 3].b = val;
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
	case 0x06:	/* Command Register 0 (RS value = 0110) */
		ramdac->cr0 = val;
		ramdac->set_reg0a = !!(val & 0x80);
		svga->ramdac_type = (val & 0x01) ? RAMDAC_8BIT : RAMDAC_6BIT;
		break;
	case 0x08:	/* Command Register 1 (RS value = 1000) */
		ramdac->cr1 = val;
		switch ((val >> 5) & 0x03) {
			case 0:
				if (val & 0x10)
					svga->bpp = 32;
				else
					svga->bpp = 8;
				break;

			case 1:
				if (val & 0x10) {
					if (val & 0x08)
						svga->bpp = 16;
					else
						svga->bpp = 15;
				} else
					svga->bpp = 8;
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
	case 0x09:	/* Command Register 2 (RS value = 1001) */
		ramdac->cr2 = val;
		svga->hwcursor.ena = !!(val & 0x03);
		svga_recalctimings(svga);
		break;
	case 0x0a:
		switch (ramdac->set_reg0a) {
			case 0:	/* Status Register (RS value = 1010) */
				break;

			case 1: /* Command Register 3 (RS value = 1010) */
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
	case 0x0b:	/* Cursor RAM Data Register (RS value = 1011) */
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
	case 0x0c:	/* Cursor X Low Register (RS value = 1100) */
		ramdac->hwc_x = (ramdac->hwc_x & 0x0f00) | val;
		svga->hwcursor.x = ramdac->hwc_x - svga->hwcursor.xsize;
		break;
	case 0x0d:	/* Cursor X High Register (RS value = 1101) */
		ramdac->hwc_x = (ramdac->hwc_x & 0x00ff) | ((val & 0x0f) << 8);
		svga->hwcursor.x = ramdac->hwc_x - svga->hwcursor.xsize;
		break;
	case 0x0e:	/* Cursor Y Low Register (RS value = 1110) */
		ramdac->hwc_y = (ramdac->hwc_y & 0x0f00) | val;
		svga->hwcursor.y = ramdac->hwc_y - svga->hwcursor.ysize;
		break;
	case 0x0f:	/* Cursor Y High Register (RS value = 1111) */
		ramdac->hwc_y = (ramdac->hwc_y & 0x00ff) | ((val & 0x0f) << 8);
		svga->hwcursor.y = ramdac->hwc_y - svga->hwcursor.ysize;
		break;
    }

    return;
}


uint8_t
bt485_ramdac_in(uint16_t addr, int rs2, int rs3, bt485_ramdac_t *ramdac, svga_t *svga)
{
    uint8_t temp = 0xff;
    uint8_t *cd;
    uint8_t rs = (addr & 0x03);
    rs |= (!!rs2 << 2);
    rs |= (!!rs3 << 3);

    switch (rs) {
	case 0x00:	/* Palette Write Index Register (RS value = 0000) */
	case 0x01:	/* Palette Data Register (RS value = 0001) */
	case 0x02:	/* Pixel Read Mask Register (RS value = 0010) */
	case 0x03:	/* Palette Read Index Register (RS value = 0011) */
	case 0x04:	/* Ext Palette Write Index Register (RS value = 0100) */
	case 0x07:	/* Ext Palette Read Index Register (RS value = 0111) */
		temp = svga_in(addr, svga);
		break;
	case 0x05:	/* Ext Palette Data Register (RS value = 0101) */
		svga->dac_status = 0;
		switch (svga->dac_pos) {
			case 0:
				svga->dac_pos++;
				temp = ramdac->extpal[svga->dac_read].r & 0x3f;
			case 1:
				svga->dac_pos++;
				temp = ramdac->extpal[svga->dac_read].g & 0x3f;
			case 2:
				svga->dac_pos=0;
				svga->dac_read = (svga->dac_read + 1) & 15;
				temp = ramdac->extpal[(svga->dac_read - 1) & 15].b & 0x3f;
		}
		break;
	case 0x06:	/* Command Register 0 (RS value = 0110) */
		temp = ramdac->cr0;
		break;
	case 0x08:	/* Command Register 1 (RS value = 1000) */
		temp = ramdac->cr1;
		break;
	case 0x09:	/* Command Register 2 (RS value = 1001) */
		temp = ramdac->cr2;
		break;
	case 0x0a:
		if (ramdac->set_reg0a)
			temp = ramdac->cr3;
		else
			temp = 0x60; /*Bt485*/
			/* Datasheet says bits 7,6 = 01, bits 5,4 = revision */
		break;
	case 0x0b:	/* Cursor RAM Data Register (RS value = 1011) */
		if (svga->hwcursor.xsize == 64)
			cd = (uint8_t *) ramdac->cursor64_data;
		else
			cd = (uint8_t *) ramdac->cursor32_data;

		temp = cd[svga->dac_pos];
		svga->dac_pos++;

		svga->dac_pos &= ((svga->hwcursor.xsize == 64) ? 0x03ff : 0x00ff);
		break;
	case 0x0c:	/* Cursor X Low Register (RS value = 1100) */
		temp = ramdac->hwc_x & 0xff;
		break;
	case 0x0d:	/* Cursor X High Register (RS value = 1101) */
		temp = (ramdac->hwc_x >> 8) & 0xff;
		break;
	case 0x0e:	/* Cursor Y Low Register (RS value = 1110) */
		temp = ramdac->hwc_y & 0xff;
		break;
	case 0x0f:	/* Cursor Y High Register (RS value = 1111) */
		temp = (ramdac->hwc_y >> 8) & 0xff;
		break;
    }

    return temp;
}
