/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the Brooktree BT484-485A true colour RAMDAC
 *		family.
 *
 * Version:	@(#)vid_bt48x_ramdac.c	1.0.13	2018/10/19
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		TheCollector1995,
 *
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2018 TheCollector1995.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../device.h"
#include "../mem.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_bt48x_ramdac.h"


enum {
	BT484 = 0,
	ATT20C504,
	BT485,
	ATT20C505,
	BT485A
};


static void
bt48x_set_bpp(bt48x_ramdac_t *ramdac, svga_t *svga)
{
    if (!(ramdac->cr2 & 0x20))
	svga->bpp = 8;
    else switch ((ramdac->cr1 >> 5) & 0x03) {
	case 0:
		svga->bpp = 32;
		break;
	case 1:
		if (ramdac->cr1 & 0x08)
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
}


void
bt48x_ramdac_out(uint16_t addr, int rs2, int rs3, uint8_t val, bt48x_ramdac_t *ramdac, svga_t *svga)
{
    uint32_t o32;
    uint8_t *cd;
    uint16_t index;
    uint8_t rs = (addr & 0x03);
    uint16_t da_mask = 0x03ff;
    rs |= (!!rs2 << 2);
    rs |= (!!rs3 << 3);
    if (ramdac->type < BT485)
	da_mask = 0x00ff;

    switch (rs) {
	case 0x00:	/* Palette Write Index Register (RS value = 0000) */
	case 0x03:
		svga->dac_pos = 0;
		svga->dac_status = addr & 0x03;
		svga->dac_addr = val;
		if (ramdac->type >= BT485)
			svga->dac_addr |= ((int) (ramdac->cr3 & 0x03) << 8);
		if (svga->dac_status)
			svga->dac_addr = (svga->dac_addr + 1) & da_mask;
		break;
	case 0x01:	/* Palette Data Register (RS value = 0001) */
	case 0x02:	/* Pixel Read Mask Register (RS value = 0010) */
		svga_out(addr, val, svga);
		break;
	case 0x04:	/* Ext Palette Write Index Register (RS value = 0100) */
	case 0x07:	/* Ext Palette Read Index Register (RS value = 0111) */
		svga->dac_pos = 0;
		svga->dac_status = rs & 0x03;
		ramdac->ext_addr = (val + (rs & 0x01)) & 255;
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
				index = ramdac->ext_addr & 3;
				ramdac->extpal[index].r = svga->dac_r;
				ramdac->extpal[index].g = svga->dac_g;
				ramdac->extpal[index].b = val;
				if (svga->ramdac_type == RAMDAC_8BIT)
					ramdac->extpallook[index] = makecol32(ramdac->extpal[index].r, ramdac->extpal[index].g, ramdac->extpal[index].b);
				else
					ramdac->extpallook[index] = makecol32(video_6to8[ramdac->extpal[index].r & 0x3f], video_6to8[ramdac->extpal[index].g & 0x3f], video_6to8[ramdac->extpal[index].b & 0x3f]);

				if (svga->ext_overscan && !index) {
					o32 = svga->overscan_color;
					svga->overscan_color = ramdac->extpallook[0];
					if (o32 != svga->overscan_color)
						svga_recalctimings(svga);
				}
				ramdac->ext_addr = (ramdac->ext_addr + 1) & 0xff;
				svga->dac_pos = 0;
				break;
		}
		break;
	case 0x06:	/* Command Register 0 (RS value = 0110) */
		ramdac->cr0 = val;
		svga->ramdac_type = (val & 0x02) ? RAMDAC_8BIT : RAMDAC_6BIT;
		break;
	case 0x08:	/* Command Register 1 (RS value = 1000) */
		ramdac->cr1 = val;
		bt48x_set_bpp(ramdac, svga);
		break;
	case 0x09:	/* Command Register 2 (RS value = 1001) */
		ramdac->cr2 = val;
		svga->hwcursor.ena = !!(val & 0x03);
		bt48x_set_bpp(ramdac, svga);
		break;
	case 0x0a:
		if ((ramdac->type >= BT485) && (ramdac->cr0 & 0x80)) {
			switch ((svga->dac_addr & 0xff)) {
				case 0x01:
					/* Command Register 3 (RS value = 1010) */
					ramdac->cr3 = val;
					svga->hwcursor.xsize = svga->hwcursor.ysize = (val & 4) ? 64 : 32;
					svga->hwcursor.yoff = (svga->hwcursor.ysize == 32) ? 32 : 0;
					svga->hwcursor.x = ramdac->hwc_x - svga->hwcursor.xsize;
					svga->hwcursor.y = ramdac->hwc_y - svga->hwcursor.ysize;
					svga->dac_addr = (svga->dac_addr & 0x00ff) | ((val & 0x03) << 8);
					svga_recalctimings(svga);
					break;
				case 0x02:
				case 0x20:
				case 0x21:
				case 0x22:
					if (ramdac->type != BT485A)
						break;
					else if (svga->dac_addr == 2) {
						ramdac->cr4 = val;
						break;
					}
					break;
			}
		}
		break;
	case 0x0b:	/* Cursor RAM Data Register (RS value = 1011) */
		index = svga->dac_addr & da_mask;
		if ((ramdac->type >= BT485) && (svga->hwcursor.xsize == 64))
			cd = (uint8_t *) ramdac->cursor64_data;
		else
			cd = (uint8_t *) ramdac->cursor32_data;

		cd[index] = val;

		svga->dac_addr = (svga->dac_addr + 1) & da_mask;
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
bt48x_ramdac_in(uint16_t addr, int rs2, int rs3, bt48x_ramdac_t *ramdac, svga_t *svga)
{
    uint8_t temp = 0xff;
    uint8_t *cd;
    uint16_t index;
    uint8_t rs = (addr & 0x03);
    uint16_t da_mask = 0x03ff;
    rs |= (!!rs2 << 2);
    rs |= (!!rs3 << 3);
    if (ramdac->type < BT485)
	da_mask = 0x00ff;

    switch (rs) {
	case 0x00:	/* Palette Write Index Register (RS value = 0000) */
	case 0x01:	/* Palette Data Register (RS value = 0001) */
	case 0x02:	/* Pixel Read Mask Register (RS value = 0010) */
		temp = svga_in(addr, svga);
		break;
	case 0x03:	/* Palette Read Index Register (RS value = 0011) */
		temp = svga->dac_addr & 0xff;
		break;
	case 0x04:	/* Ext Palette Write Index Register (RS value = 0100) */
		temp = ramdac->ext_addr;
		break;
	case 0x05:	/* Ext Palette Data Register (RS value = 0101) */
		index = (ramdac->ext_addr - 1) & 3;
		svga->dac_status = 3;
		switch (svga->dac_pos) {
			case 0:
				svga->dac_pos++;
				if (svga->ramdac_type == RAMDAC_8BIT)
					temp = ramdac->extpal[index].r;
				else
					temp = ramdac->extpal[index].r & 0x3f;
				break;
			case 1:
				svga->dac_pos++;
				if (svga->ramdac_type == RAMDAC_8BIT)
					temp = ramdac->extpal[index].g;
				else
					temp = ramdac->extpal[index].g & 0x3f;
				break;
			case 2:
				svga->dac_pos=0;
				ramdac->ext_addr = ramdac->ext_addr + 1;
				if (svga->ramdac_type == RAMDAC_8BIT)
					temp = ramdac->extpal[index].b;
				else
					temp = ramdac->extpal[index].b & 0x3f;
				break;
		}
		break;
	case 0x06:	/* Command Register 0 (RS value = 0110) */
		temp = ramdac->cr0;
		break;
	case 0x07:	/* Ext Palette Read Index Register (RS value = 0111) */
		temp = ramdac->ext_addr;
		break;
	case 0x08:	/* Command Register 1 (RS value = 1000) */
		temp = ramdac->cr1;
		break;
	case 0x09:	/* Command Register 2 (RS value = 1001) */
		temp = ramdac->cr2;
		break;
	case 0x0a:
		if ((ramdac->type >= BT485) && (ramdac->cr0 & 0x80)) {
			switch ((svga->dac_addr & 0xff)) {
				case 0x00:
					temp = ramdac->status | (svga->dac_status ? 0x04 : 0x00);
					break;
				case 0x01:
					temp = ramdac->cr3 & 0xfc;
					temp |= (svga->dac_addr & 0x300) >> 8;
					break;
				case 0x02:
				case 0x20:
				case 0x21:
				case 0x22:
					if (ramdac->type != BT485A)
						break;
					else if (svga->dac_addr == 2) {
						temp = ramdac->cr4;
						break;
					} else {
						/* TODO: Red, Green, and Blue Signature Analysis Registers */
						temp = 0xff;
						break;
					}
					break;
			}
		} else
			temp = ramdac->status | (svga->dac_status ? 0x04 : 0x00);
		break;
	case 0x0b:	/* Cursor RAM Data Register (RS value = 1011) */
		index = (svga->dac_addr - 1) & da_mask;
		if ((ramdac->type >= BT485) && (svga->hwcursor.xsize == 64))
			cd = (uint8_t *) ramdac->cursor64_data;
		else
			cd = (uint8_t *) ramdac->cursor32_data;

		temp = cd[index];

		svga->dac_addr = (svga->dac_addr + 1) & da_mask;
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


void
bt48x_hwcursor_draw(svga_t *svga, int displine)
{
    int x, xx, comb, b0, b1;
    uint16_t dat[2];
    int offset = svga->hwcursor_latch.x - svga->hwcursor_latch.xoff;
    int y_add, x_add;
    int pitch, bppl, mode, x_pos, y_pos;
    uint32_t clr1, clr2, clr3, *p;
    uint8_t *cd;
    bt48x_ramdac_t *ramdac = (bt48x_ramdac_t *) svga->ramdac;

    clr1 = ramdac->extpallook[1];
    clr2 = ramdac->extpallook[2];
    clr3 = ramdac->extpallook[3];

    y_add = (enable_overscan && !suppress_overscan) ? (overscan_y >> 1) : 0;
    x_add = (enable_overscan && !suppress_overscan) ? 8 : 0;

    /* The planes come in two parts, and each plane is 1bpp,
       so a 32x32 cursor has 4 bytes per line, and a 64x64
       cursor has 8 bytes per line. */
    pitch = (svga->hwcursor_latch.xsize >> 3);				/* Bytes per line. */
    /* A 32x32 cursor has 128 bytes per line, and a 64x64
       cursor has 512 bytes per line. */
    bppl = (pitch * svga->hwcursor_latch.ysize);			/* Bytes per plane. */
    mode = ramdac->cr2 & 0x03;

    if (svga->interlace && svga->hwcursor_oddeven)
	svga->hwcursor_latch.addr += pitch;

    if (svga->hwcursor_latch.xsize == 64)
	cd = (uint8_t *) ramdac->cursor64_data;
    else
	cd = (uint8_t *) ramdac->cursor32_data;

    for (x = 0; x < svga->hwcursor_latch.xsize; x += 16) {
	dat[0] = (cd[svga->hwcursor_latch.addr]        << 8) |
		  cd[svga->hwcursor_latch.addr + 1];
	dat[1] = (cd[svga->hwcursor_latch.addr + bppl] << 8) |
		  cd[svga->hwcursor_latch.addr + bppl + 1];

	for (xx = 0; xx < 16; xx++) {
		b0 = (dat[0] >> (15 - xx)) & 1;
		b1 = (dat[1] >> (15 - xx)) & 1;
		comb = (b0 | (b1 << 1));

		y_pos = displine + y_add;
		x_pos = offset + 32 + x_add;
		p = ((uint32_t *)buffer32->line[y_pos]);

		if (offset >= svga->hwcursor_latch.x) {
			switch (mode) {
				case 1:		/* Three Color */
					switch (comb) {
						case 1:
							p[x_pos] = clr1;
							break;
						case 2:
							p[x_pos] = clr2;
							break;
						case 3:
							p[x_pos] = clr3;
							break;
					}
					break;
				case 2:		/* PM/Windows */
					switch (comb) {
						case 0:
							p[x_pos] = clr1;
							break;
						case 1:
							p[x_pos] = clr2;
							break;
						case 3:
							p[x_pos] ^= 0xffffff;
							break;
					}
					break;
				case 3:		/* X-Windows */
					switch (comb) {
						case 2:
							p[x_pos] = clr1;
							break;
						case 3:
							p[x_pos] = clr2;
							break;
					}
					break;
			}
		}
		offset++;
	}
	svga->hwcursor_latch.addr += 2;
    }

    if (svga->interlace && !svga->hwcursor_oddeven)
	svga->hwcursor_latch.addr += pitch;
}


void *
bt48x_ramdac_init(const device_t *info)
{
    bt48x_ramdac_t *ramdac = (bt48x_ramdac_t *) malloc(sizeof(bt48x_ramdac_t));
    memset(ramdac, 0, sizeof(bt48x_ramdac_t));

    ramdac->type = info->local;

    /* Set the RAM DAC status byte to the correct ID bits.

	Both the BT484 and BT485 datasheets say this:
	SR7-SR6: These bits are identification values. SR7=0 and SR6=1.
	But all other sources seem to assume SR7=1 and SR6=0. */
    switch (ramdac->type) {
	case BT484:
		ramdac->status = 0x40;
		break;
	case ATT20C504:
		ramdac->status = 0x40;
		break;
	case BT485:
		ramdac->status = 0x60;
		break;
	case ATT20C505:
		ramdac->status = 0xd0;
		break;
	case BT485A:
		ramdac->status = 0x20;
		break;
    }

    return ramdac;
}


static void
bt48x_ramdac_close(void *priv)
{
    bt48x_ramdac_t *ramdac = (bt48x_ramdac_t *) priv;

    if (ramdac)
	free(ramdac);
}


const device_t bt484_ramdac_device =
{
        "Brooktree Bt484 RAMDAC",
        0, BT484,
        bt48x_ramdac_init, bt48x_ramdac_close,
	NULL, NULL, NULL, NULL
};

const device_t att20c504_ramdac_device =
{
        "AT&T 20c504 RAMDAC",
        0, ATT20C504,
        bt48x_ramdac_init, bt48x_ramdac_close,
	NULL, NULL, NULL, NULL
};

const device_t bt485_ramdac_device =
{
        "Brooktree Bt485 RAMDAC",
        0, BT485,
        bt48x_ramdac_init, bt48x_ramdac_close,
	NULL, NULL, NULL, NULL
};

const device_t att20c505_ramdac_device =
{
        "AT&T 20c505 RAMDAC",
        0, ATT20C505,
        bt48x_ramdac_init, bt48x_ramdac_close,
	NULL, NULL, NULL, NULL
};

const device_t bt485a_ramdac_device =
{
        "Brooktree Bt485A RAMDAC",
        0, BT485A,
        bt48x_ramdac_init, bt48x_ramdac_close,
	NULL, NULL, NULL, NULL
};
