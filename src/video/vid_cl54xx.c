/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of select Cirrus Logic cards (currently only
 *		CL-GD 5428 and 5429 are fully supported).
 *
 * Version:	@(#)vid_cl_54xx.c	1.0.0	2018/02/18
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Barry Rodewald,
 *		TheCollector1995,
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2018 Barry Rodewald
 *		Copyright 2016-2018 TheCollector1995.
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <wchar.h>
#include "../86box.h"
#include "../cpu/cpu.h"
#include "../io.h"
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_svga_render.h"
#include "vid_cl54xx.h"

#define BIOS_GD5428_PATH		L"roms/video/cirruslogic/Diamond SpeedStar PRO VLB (Cirrus Logic 5428)_v3.04.bin"
#define BIOS_GD5429_PATH		L"roms/video/cirruslogic/5429.vbi"

#define CIRRUS_ID_CLGD5428		0x98
#define CIRRUS_ID_CLGD5429		0x9c

/* sequencer 0x07 */
#define CIRRUS_SR7_BPP_VGA		0x00
#define CIRRUS_SR7_BPP_SVGA		0x01
#define CIRRUS_SR7_BPP_MASK		0x0e
#define CIRRUS_SR7_BPP_8		0x00
#define CIRRUS_SR7_BPP_16_DOUBLEVCLK	0x02
#define CIRRUS_SR7_BPP_24		0x04
#define CIRRUS_SR7_BPP_16		0x06
#define CIRRUS_SR7_BPP_32		0x08
#define CIRRUS_SR7_ISAADDR_MASK		0xe0

/* sequencer 0x12 */
#define CIRRUS_CURSOR_SHOW		0x01
#define CIRRUS_CURSOR_HIDDENPEL		0x02
#define CIRRUS_CURSOR_LARGE		0x04	/* 64x64 if set, 32x32 if clear */

/* control 0x30 */
#define CIRRUS_BLTMODE_BACKWARDS	0x01
#define CIRRUS_BLTMODE_MEMSYSDEST	0x02
#define CIRRUS_BLTMODE_MEMSYSSRC	0x04
#define CIRRUS_BLTMODE_TRANSPARENTCOMP	0x08
#define CIRRUS_BLTMODE_PATTERNCOPY	0x40
#define CIRRUS_BLTMODE_COLOREXPAND	0x80
#define CIRRUS_BLTMODE_PIXELWIDTHMASK	0x30
#define CIRRUS_BLTMODE_PIXELWIDTH8	0x00
#define CIRRUS_BLTMODE_PIXELWIDTH16	0x10
#define CIRRUS_BLTMODE_PIXELWIDTH24	0x20
#define CIRRUS_BLTMODE_PIXELWIDTH32	0x30


typedef struct gd54xx_t
{
    mem_mapping_t	mmio_mapping;

    svga_t		svga;

    rom_t		bios_rom;

    uint32_t		vram_size;
    uint8_t		vram_code;
    uint32_t		vram_mask;

    uint8_t		vclk_n[4];
    uint8_t		vclk_d[4];        
    uint32_t		bank[2];

    struct {
	uint8_t			state;
	int			ctrl;
	PALETTE			pal;
    } ramdac;		
	
    struct {
	uint16_t		fg_col, bg_col;
	uint16_t		width, height;
	uint16_t		dst_pitch, src_pitch;               
	uint32_t		dst_addr, src_addr;
	uint8_t			mask, mode, rop;
	uint8_t			status;
	uint16_t		trans_col, trans_mask;

	uint32_t		dst_addr_backup, src_addr_backup;
	uint16_t		width_backup, height_internal;

	int			x_count, y_count;
	int			sys_tx;
	uint8_t			sys_cnt;
	uint32_t		sys_buf;
	uint16_t		pixel_cnt;
	uint16_t		scan_cnt;
    } blt;
} gd54xx_t;

static void 
gd543x_mmio_write(uint32_t addr, uint8_t val, void *p);

static void 
gd54xx_recalc_banking(gd54xx_t *gd54xx);

static void 
gd543x_recalc_mapping(gd54xx_t *gd54xx);

static void 
gd54xx_start_blit(uint32_t cpu_dat, int count, gd54xx_t *gd54xx, svga_t *svga);

static void
gd54xx_out(uint16_t addr, uint8_t val, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;
    uint8_t old;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1)) 
	addr ^= 0x60;

    switch (addr) {
	case 0x3c4:
		svga->seqaddr = val;
		break;
	case 0x3c5:
		if (svga->seqaddr > 5) {
			svga->seqregs[svga->seqaddr & 0x1f] = val;
			switch (svga->seqaddr & 0x1f) {
				case 6: /* cirrus unlock extensions */
					val &= 0x17;
					if (val == 0x12)
						svga->seqregs[6] = 0x12;
					else
						svga->seqregs[6] = 0x0f;
					break;
				case 0x0b: case 0x0c: case 0x0d: case 0x0e: /* VCLK stuff */
					gd54xx->vclk_n[svga->seqaddr-0x0b] = val;
					break;
				case 0x1b: case 0x1c: case 0x1d: case 0x1e: /* VCLK stuff */
					gd54xx->vclk_d[svga->seqaddr-0x1b] = val;
					break;
				case 0x10: case 0x30: case 0x50: case 0x70:
				case 0x90: case 0xb0: case 0xd0: case 0xf0:
					svga->hwcursor.x = (val << 3) | ((svga->seqaddr >> 5) & 7);
					break;
				case 0x11: case 0x31: case 0x51: case 0x71:
				case 0x91: case 0xb1: case 0xd1: case 0xf1:
					svga->hwcursor.y = (val << 3) | ((svga->seqaddr >> 5) & 7);
					break;
				case 0x12:
					svga->hwcursor.ena = val & 1;
					break;
				case 0x13:
					svga->hwcursor.addr = 0x1fc000 + ((val & 0x3f) * 256);
					break;
				case 0x17:
					svga->seqregs[0x17] = (svga->seqregs[0x17] & 0x38) | (val & 0xc7);
					gd543x_recalc_mapping(gd54xx);
					break;
			}
			return;
		}
		break;
	case 0x3C6:
		if (gd54xx->ramdac.state == 4) {
			gd54xx->ramdac.state = 0;
			gd54xx->ramdac.ctrl = val;

			if (svga->seqregs[0x07] & CIRRUS_SR7_BPP_SVGA)  {
				switch (svga->seqregs[0x07] & CIRRUS_SR7_BPP_MASK) {
					case CIRRUS_SR7_BPP_8:
						svga->bpp = 8;
						break;		
					case CIRRUS_SR7_BPP_16_DOUBLEVCLK:
					case CIRRUS_SR7_BPP_16:
						if (gd54xx->ramdac.ctrl & 0x01)
							svga->bpp = 16;
						else
							svga->bpp = 15;
						break;	
					case CIRRUS_SR7_BPP_24:
						svga->bpp = 24;
						break;
					case CIRRUS_SR7_BPP_32:
						svga->bpp = 32;
						break;
				}
			}
			svga_recalctimings(svga);
			return;
		}
		gd54xx->ramdac.state = 0;
		break;
	case 0x3C7: case 0x3C8:
		gd54xx->ramdac.state = 0;
		break;	
	case 0x3C9:
		if (svga->seqregs[0x12] & CIRRUS_CURSOR_HIDDENPEL) {
			gd54xx->ramdac.state = 0;
			svga->fullchange = changeframecount;
			switch (svga->dac_pos) {
				case 0: 
					gd54xx->ramdac.pal[svga->dac_write & 0xf].r = val & 63;
					svga->dac_pos++; 
					break;
				case 1: 
					gd54xx->ramdac.pal[svga->dac_write & 0xf].g = val & 63;
					svga->dac_pos++; 
					break;
				case 2: 
					gd54xx->ramdac.pal[svga->dac_write & 0xf].b = val & 63;
					svga->dac_pos = 0; 
					svga->dac_write = (svga->dac_write + 1) & 255; 
					break;
			}
			return;
		}
		gd54xx->ramdac.state = 0;
		break;	
	case 0x3cf:
		if (svga->gdcaddr == 0)
				gd543x_mmio_write(0x00, val, gd54xx);
		if (svga->gdcaddr == 1)
				gd543x_mmio_write(0x04, val, gd54xx);
	
		if (svga->gdcaddr == 5) {
			svga->gdcreg[5] = val;
			if (svga->gdcreg[0x0b] & 0x04)
				svga->writemode = val & 7;
			else
				svga->writemode = val & 3;
			svga->readmode = val & 8;
			svga->chain2_read = val & 0x10;
			return;
		}
		if (svga->gdcaddr > 8) {
			svga->gdcreg[svga->gdcaddr & 0x3f] = val;
			switch (svga->gdcaddr) {
				case 0x09: case 0x0a: case 0x0b:
					gd54xx_recalc_banking(gd54xx);
					if (svga->gdcreg[0xb] & 0x04)
						svga->writemode = svga->gdcreg[5] & 7;
					else
						svga->writemode = svga->gdcreg[5] & 3;
					break;
					
					case 0x10:
					gd543x_mmio_write(0x01, val, gd54xx);
					break;
					case 0x11:
					gd543x_mmio_write(0x05, val, gd54xx);
					break;
					
					case 0x20:
					gd543x_mmio_write(0x08, val, gd54xx);
					break;
					case 0x21:
					gd543x_mmio_write(0x09, val, gd54xx);
					break;
					case 0x22:
					gd543x_mmio_write(0x0a, val, gd54xx);
					break;
					case 0x23:
					gd543x_mmio_write(0x0b, val, gd54xx);
					break;
					case 0x24:
					gd543x_mmio_write(0x0c, val, gd54xx);
					break;
					case 0x25:
					gd543x_mmio_write(0x0d, val, gd54xx);
					break;
					case 0x26:
					gd543x_mmio_write(0x0e, val, gd54xx);
					break;
					case 0x27:
					gd543x_mmio_write(0x0f, val, gd54xx);
					break;
	
					case 0x28:
					gd543x_mmio_write(0x10, val, gd54xx);
					break;
					case 0x29:
					gd543x_mmio_write(0x11, val, gd54xx);
					break;
					case 0x2a:
					gd543x_mmio_write(0x12, val, gd54xx);
					break;

					case 0x2c:
					gd543x_mmio_write(0x14, val, gd54xx);
					break;
					case 0x2d:
					gd543x_mmio_write(0x15, val, gd54xx);
					break;
					case 0x2e:
					gd543x_mmio_write(0x16, val, gd54xx);
					break;

					case 0x2f:
					gd543x_mmio_write(0x17, val, gd54xx);
					break;
					case 0x30:
					gd543x_mmio_write(0x18, val, gd54xx);
					break;
	
					case 0x32:
					gd543x_mmio_write(0x1a, val, gd54xx);
					break;
	
					case 0x31:
					gd543x_mmio_write(0x40, val, gd54xx);
					break;
			}
			return;
		}
		break;
	case 0x3D4:
		svga->crtcreg = val & 0x3f;
		return;
	case 0x3D5:
		if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
			return;
		if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
			val = (svga->crtc[7] & ~0x10) | (val & 0x10);
		old = svga->crtc[svga->crtcreg];
		svga->crtc[svga->crtcreg] = val;
		
		if (old != val) {
			if (svga->crtcreg < 0xe || svga->crtcreg > 0x10) {
				svga->fullchange = changeframecount;
				svga_recalctimings(svga);
			}
		}
		break;
    }
    svga_out(addr, val, svga);
}


static uint8_t
gd54xx_in(uint16_t addr, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3d0) && !(svga->miscout & 1)) 
	addr ^= 0x60;

    switch (addr) {
	case 0x3c5:
		if (svga->seqaddr > 5) {
			switch (svga->seqaddr) {
				case 6:
					return ((svga->seqregs[6] & 0x17) == 0x12) ? 0x12 : 0x0f;			
				case 0x0b: case 0x0c: case 0x0d: case 0x0e:
					return gd54xx->vclk_n[svga->seqaddr-0x0b];
				case 0x0f:
					return svga->seqregs[0x0f];			
				case 0x15:
					return gd54xx->vram_code;			
				case 0x17:
					return svga->seqregs[0x17];
				case 0x1b: case 0x1c: case 0x1d: case 0x1e:
					return gd54xx->vclk_d[svga->seqaddr-0x1b];
				case 0x1f:
					return svga->seqregs[0x1f];
			}
			return svga->seqregs[svga->seqaddr & 0x3f];
		}
		break;
	case 0x3C6:
		if (gd54xx->ramdac.state == 4) {
			gd54xx->ramdac.state = 0;
			return gd54xx->ramdac.ctrl;
		}
		gd54xx->ramdac.state++;
		break;
	case 0x3C7: case 0x3C8:
		gd54xx->ramdac.state = 0;
		break;
	case 0x3C9:
		if (svga->seqregs[0x12] & CIRRUS_CURSOR_HIDDENPEL) {
			gd54xx->ramdac.state = 0;
			switch (svga->dac_pos) {
				case 0: 
					svga->dac_pos++; 
					return gd54xx->ramdac.pal[svga->dac_read & 0xf].r;
				case 1: 
					svga->dac_pos++; 
					return gd54xx->ramdac.pal[svga->dac_read & 0xf].g;
				case 2: 
					svga->dac_pos=0; 
					svga->dac_read = (svga->dac_read + 1) & 255; 
					return gd54xx->ramdac.pal[(svga->dac_read - 1) & 15].b;
			}
		}
		gd54xx->ramdac.state = 0;
		break;
	case 0x3cf:
		if (svga->gdcaddr > 8) {
			return svga->gdcreg[svga->gdcaddr & 0x3f];
		}
		break;
	case 0x3D4:
		return svga->crtcreg;
	case 0x3D5:
		switch (svga->crtcreg) {
			case 0x24: /*Attribute controller toggle readback (R)*/
				return svga->attrff << 7;
			case 0x26: /*Attribute controller index readback (R)*/
				return svga->attraddr & 0x3f;					
			case 0x27: /*ID*/
				return svga->crtc[0x27]; /*GD542x/GD543x*/
		}
		return svga->crtc[svga->crtcreg];
    }
    return svga_in(addr, svga);
}


static void
gd54xx_recalc_banking(gd54xx_t *gd54xx)
{
    svga_t *svga = &gd54xx->svga;

    if (svga->gdcreg[0xb] & 0x20)
	gd54xx->bank[0] = svga->gdcreg[0x09] << 14;
    else
	gd54xx->bank[0] = svga->gdcreg[0x09] << 12;
                        
    if (svga->gdcreg[0xb] & 0x01) {
	if (svga->gdcreg[0xb] & 0x20)
		gd54xx->bank[1] = svga->gdcreg[0x0a] << 14;
	else
		gd54xx->bank[1] = svga->gdcreg[0x0a] << 12;
    } else
	gd54xx->bank[1] = gd54xx->bank[0] + 0x8000;
}


static void 
gd543x_recalc_mapping(gd54xx_t *gd54xx)
{
    svga_t *svga = &gd54xx->svga;
        
    switch (svga->gdcreg[6] & 0x0C) {
	case 0x0: /*128k at A0000*/
		mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
		mem_mapping_disable(&gd54xx->mmio_mapping);
		svga->banked_mask = 0xffff;
		break;
	case 0x4: /*64k at A0000*/
		mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
		if (svga->seqregs[0x17] & 0x04)
			mem_mapping_set_addr(&gd54xx->mmio_mapping, 0xb8000, 0x00100);
		svga->banked_mask = 0xffff;
		break;
	case 0x8: /*32k at B0000*/
		mem_mapping_set_addr(&svga->mapping, 0xb0000, 0x08000);
		mem_mapping_disable(&gd54xx->mmio_mapping);
		svga->banked_mask = 0x7fff;
		break;
	case 0xC: /*32k at B8000*/
		mem_mapping_set_addr(&svga->mapping, 0xb8000, 0x08000);
		mem_mapping_disable(&gd54xx->mmio_mapping);
		svga->banked_mask = 0x7fff;
		break;
        }
}


static void
gd54xx_recalctimings(svga_t *svga)
{
    gd54xx_t *gd54xx = (gd54xx_t *)svga->p;	
    uint8_t clocksel;

    svga->rowoffset = (svga->crtc[0x13]) | ((svga->crtc[0x1b] & 0x10) << 4);

    svga->ma_latch = (svga->crtc[0x0c] << 8)
		   +  svga->crtc[0x0d]
		   +  ((svga->crtc[0x1b] & 0x01) << 16)
		   +  ((svga->crtc[0x1b] & 0x0c) << 15)
		   +  ((svga->crtc[0x1d] & 0x80) << 12);

    svga->interlace = (svga->crtc[0x1a] & 0x01);

    if (svga->seqregs[0x07] & CIRRUS_SR7_BPP_SVGA) {
	switch (svga->bpp) {
		case 8:
			svga->render = svga_render_8bpp_highres;
			break;
		case 15:
			svga->render = svga_render_15bpp_highres;
			break;
		case 16:
			svga->render = svga_render_16bpp_highres;
			break;
		case 24:
			svga->render = svga_render_24bpp_highres;
			break;
		case 32:
			svga->render = svga_render_32bpp_highres;
			break;			
	}
    }

    clocksel = (svga->miscout >> 2) & 3;

    if (!gd54xx->vclk_n[clocksel] || !gd54xx->vclk_d[clocksel])
	svga->clock = cpuclock / ((svga->miscout & 0x0c) ? 28322000.0 : 25175000.0);
    else {
	int n = gd54xx->vclk_n[clocksel] & 0x7f;
	int d = (gd54xx->vclk_d[clocksel] & 0x3e) >> 1;
	int m = gd54xx->vclk_d[clocksel] & 0x01 ? 2 : 1;
	float freq = (14318184.0 * ((float)n / ((float)d * m)));
	svga->clock = cpuclock / freq;
    }
	
    svga->vram_display_mask = (svga->crtc[0x1b] & 2) ? gd54xx->vram_mask : 0x3ffff;
}


static void
gd54xx_hwcursor_draw(svga_t *svga, int displine)
{
    int x;
    uint8_t dat[2];
    int xx;
    int offset = svga->hwcursor_latch.x - svga->hwcursor_latch.xoff;
    int largecur = (svga->seqregs[0x12] & CIRRUS_CURSOR_LARGE);
    int cursize = (largecur) ? 64 : 32;
    int y_add = (enable_overscan && !suppress_overscan) ? 16 : 0;
    int x_add = (enable_overscan && !suppress_overscan) ? 8 : 0;

    if (svga->interlace && svga->hwcursor_oddeven)
		svga->hwcursor_latch.addr += 4;		
	
    for (x = 0; x < cursize; x += 8) {
	dat[0] = svga->vram[svga->hwcursor_latch.addr];
	dat[1] = svga->vram[svga->hwcursor_latch.addr + 0x80];
	for (xx = 0; xx < 8; xx++) {
		if (offset >= svga->hwcursor_latch.x) {
			if (dat[1] & 0x80)
				((uint32_t *)buffer32->line[displine + y_add])[offset + cursize + x_add] = 0;
			if (dat[0] & 0x80)
				((uint32_t *)buffer32->line[displine + y_add])[offset + cursize + x_add] ^= 0xffffff;
		}
           
		offset++;
		dat[0] <<= 1;
		dat[1] <<= 1;
	}
	svga->hwcursor_latch.addr++;
    }
	
    if (svga->interlace && !svga->hwcursor_oddeven)
	svga->hwcursor_latch.addr += 4;		
}


static void
gd5428_copy_pixel(gd54xx_t *gd54xx, svga_t *svga, uint8_t src, uint8_t dst)
{
    uint8_t res = src;
    svga->changedvram[(gd54xx->blt.dst_addr_backup & svga->vram_mask) >> 12] = changeframecount;

    switch (gd54xx->blt.rop) {
	case 0x00: res = 0;             break;
	case 0x05: res =   src &  dst;  break;
	case 0x06: res =   dst;         break;
	case 0x09: res =   src & ~dst;  break;
	case 0x0b: res = ~ dst;         break;
	case 0x0d: res =   src;         break;
	case 0x0e: res = 0xff;          break;
	case 0x50: res = ~ src &  dst;  break;
	case 0x59: res =   src ^  dst;  break;
	case 0x6d: res =   src |  dst;  break;
	case 0x90: res = ~(src |  dst); break;
	case 0x95: res = ~(src ^  dst); break;
	case 0xad: res =   src | ~dst;  break;
	case 0xd0: res =  ~src;         break;
	case 0xd6: res =  ~src |  dst;  break;
	case 0xda: res = ~(src &  dst); break;
    }

    /* handle transparency compare */
    if(gd54xx->blt.mode & CIRRUS_BLTMODE_TRANSPARENTCOMP) {  /* TODO: 16-bit compare */
	/* if ROP result matches the transparency colour, don't change the pixel */
	if((res & (~gd54xx->blt.trans_mask & 0xff)) == ((gd54xx->blt.trans_col & 0xff) & (~gd54xx->blt.trans_mask & 0xff)))
		return;
    }

    svga->vram[gd54xx->blt.dst_addr_backup & svga->vram_mask] = res;	
}


/* non colour-expanded BitBLTs from system memory must be doubleword sized, extra bytes are ignored */
static void 
gd54xx_blit_dword(gd54xx_t *gd54xx, svga_t *svga)
{
    /* TODO: add support for reverse direction */
    uint8_t x, pixel;

    for (x=0;x<32;x+=8) {
	pixel = ((gd54xx->blt.sys_buf & (0xff << x)) >> x);
	if(gd54xx->blt.pixel_cnt <= gd54xx->blt.width)
		gd5428_copy_pixel(gd54xx, svga, pixel, svga->vram[gd54xx->blt.dst_addr_backup & svga->vram_mask]);
	gd54xx->blt.dst_addr_backup++;
	gd54xx->blt.pixel_cnt++;
    }
    if (gd54xx->blt.pixel_cnt > gd54xx->blt.width) {
	gd54xx->blt.pixel_cnt = 0;
	gd54xx->blt.scan_cnt++;
	gd54xx->blt.dst_addr_backup = gd54xx->blt.dst_addr + (gd54xx->blt.dst_pitch*gd54xx->blt.scan_cnt);
    }
    if (gd54xx->blt.scan_cnt > gd54xx->blt.height) {
	gd54xx->blt.sys_tx = 0;  /*  BitBLT complete */
	gd543x_recalc_mapping(gd54xx);
    }
}


static void
gd54xx_blt_write_w(uint32_t addr, uint16_t val, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;

    gd54xx_start_blit(val, 16, gd54xx, &gd54xx->svga);
}


static void
gd54xx_blt_write_l(uint32_t addr, uint32_t val, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;

    gd54xx_start_blit(val, 32, gd54xx, &gd54xx->svga);
}


static void	gd54xx_write_linear(uint32_t addr, uint8_t val, gd54xx_t *gd54xx);


static void
gd54xx_write(uint32_t addr, uint8_t val, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;	
	
    if (gd54xx->blt.sys_tx) {
	if (gd54xx->blt.mode == CIRRUS_BLTMODE_MEMSYSSRC) {
		gd54xx->blt.sys_buf &= ~(0xff << (gd54xx->blt.sys_cnt * 8));
		gd54xx->blt.sys_buf |= (val << (gd54xx->blt.sys_cnt * 8));
		gd54xx->blt.sys_cnt++;
		if(gd54xx->blt.sys_cnt >= 4) {
			gd54xx_blit_dword(gd54xx, svga);
			gd54xx->blt.sys_cnt = 0;
		}
	}
	return;
    }

    addr &= svga->banked_mask;
    addr = (addr & 0x7fff) + gd54xx->bank[(addr >> 15) & 1];
    gd54xx_write_linear(addr, val, gd54xx);
}


static void 
gd54xx_writew(uint32_t addr, uint16_t val, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;
    addr &= svga->banked_mask;
    addr = (addr & 0x7fff) + gd54xx->bank[(addr >> 15) & 1];

    if (svga->writemode < 4)
	svga_writew_linear(addr, val, svga);
    else {
	gd54xx_write_linear(addr, val, gd54xx);
	gd54xx_write_linear(addr+1, val >> 8, gd54xx);
    }
}


static void
gd54xx_writel(uint32_t addr, uint32_t val, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;

    addr &= svga->banked_mask;
    addr = (addr & 0x7fff) + gd54xx->bank[(addr >> 15) & 1];

    if (svga->writemode < 4)
	svga_writel_linear(addr, val, svga);
    else {
	gd54xx_write_linear(addr, val, gd54xx);
	gd54xx_write_linear(addr+1, val >> 8, gd54xx);
	gd54xx_write_linear(addr+2, val >> 16, gd54xx);
	gd54xx_write_linear(addr+3, val >> 24, gd54xx);
    }
}


static void
gd54xx_write_linear(uint32_t addr, uint8_t val, gd54xx_t *gd54xx)
{
    svga_t *svga = &gd54xx->svga;
    uint8_t vala, valb, valc, vald, wm = svga->writemask;
    int writemask2 = svga->writemask;
    int i;
    uint8_t j;

    cycles -= video_timing_write_b;
    cycles_lost += video_timing_write_b;

    egawrites++;

    if (!(svga->gdcreg[6] & 1)) 
	svga->fullchange = 2;
    if ((svga->chain4 || svga->fb_only) && (svga->writemode < 4)) {
	writemask2 = 1 << (addr & 3);
	addr &= ~3;
    } else if (svga->chain2_write) {
	writemask2 &= ~0xa;
	if (addr & 1)
		writemask2 <<= 1;
	addr &= ~1;
	addr <<= 2;
    } else
	addr <<= 2;
    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
	return;
    addr &= svga->vram_mask;
    svga->changedvram[addr >> 12]=changeframecount;

    switch (svga->writemode) {
	case 4:
		if (svga->gdcreg[0xb] & 0x10) {
			addr <<= 2;
			svga->changedvram[addr >> 12] = changeframecount;

			for (i = 0; i < 8; i++) {
				if (val & svga->seqregs[2] & (0x80 >> i)) {
					svga->vram[addr + (i << 1)] = svga->gdcreg[1];
					svga->vram[addr + (i << 1) + 1] = svga->gdcreg[0x11];
				}
			}
		} else {
                        addr <<= 1;
                        svga->changedvram[addr >> 12] = changeframecount;

			for (i = 0; i < 8; i++) {
                        	if (val & svga->seqregs[2] & (0x80 >> i))
                                	svga->vram[addr + i] = svga->gdcreg[1];
			}
                }
                break;
                        
                case 5:
                if (svga->gdcreg[0xb] & 0x10)
                {
                        addr <<= 2;
                        svga->changedvram[addr >> 12] = changeframecount;

			for (i = 0; i < 8; i++) {
				j = (0x80 >> i);
				if (svga->seqregs[2] & j) {
					svga->vram[addr + (i << 1)] = (val & j) ? svga->gdcreg[1] : svga->gdcreg[0];
					svga->vram[addr + (i << 1) + 1] = (val & j) ? svga->gdcreg[0x11] : svga->gdcreg[0x10];
				}
			}
                }
                else
                {
                        addr <<= 1;
                        svga->changedvram[addr >> 12] = changeframecount;

			for (i = 0; i < 8; i++) {
				j = (0x80 >> i);
				if (svga->seqregs[2] & j)
					svga->vram[addr + i] = (val & j) ? svga->gdcreg[1] : svga->gdcreg[0];
			}
                }
                break;
                
	case 1:
		if (writemask2 & 1) svga->vram[addr]       = svga->la;
		if (writemask2 & 2) svga->vram[addr | 0x1] = svga->lb;
		if (writemask2 & 4) svga->vram[addr | 0x2] = svga->lc;
		if (writemask2 & 8) svga->vram[addr | 0x3] = svga->ld;
		break;
	case 0:
		if (svga->gdcreg[3] & 7) 
			val = svga_rotate[svga->gdcreg[3] & 7][val];
		if (svga->gdcreg[8] == 0xff && !(svga->gdcreg[3] & 0x18) && (!svga->gdcreg[1] || (svga->seqregs[7] & 1))) {
			if (writemask2 & 1) svga->vram[addr]       = val;
			if (writemask2 & 2) svga->vram[addr | 0x1] = val;
			if (writemask2 & 4) svga->vram[addr | 0x2] = val;
			if (writemask2 & 8) svga->vram[addr | 0x3] = val;
		} else {
			if (svga->gdcreg[1] & 1) vala = (svga->gdcreg[0] & 1) ? 0xff : 0;
			else                     vala = val;
			if (svga->gdcreg[1] & 2) valb = (svga->gdcreg[0] & 2) ? 0xff : 0;
			else                     valb = val;
			if (svga->gdcreg[1] & 4) valc = (svga->gdcreg[0] & 4) ? 0xff : 0;
			else                     valc = val;
			if (svga->gdcreg[1] & 8) vald = (svga->gdcreg[0] & 8) ? 0xff : 0;
			else                     vald = val;

			switch (svga->gdcreg[3] & 0x18) {
				case 0: /*Set*/
					if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) | (svga->la & ~svga->gdcreg[8]);
					if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) | (svga->lb & ~svga->gdcreg[8]);
					if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) | (svga->lc & ~svga->gdcreg[8]);
					if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) | (svga->ld & ~svga->gdcreg[8]);
					break;
				case 8: /*AND*/
					if (writemask2 & 1) svga->vram[addr]       = (vala | ~svga->gdcreg[8]) & svga->la;
					if (writemask2 & 2) svga->vram[addr | 0x1] = (valb | ~svga->gdcreg[8]) & svga->lb;
					if (writemask2 & 4) svga->vram[addr | 0x2] = (valc | ~svga->gdcreg[8]) & svga->lc;
					if (writemask2 & 8) svga->vram[addr | 0x3] = (vald | ~svga->gdcreg[8]) & svga->ld;
					break;
				case 0x10: /*OR*/
					if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) | svga->la;
					if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) | svga->lb;
					if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) | svga->lc;
					if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) | svga->ld;
					break;
				case 0x18: /*XOR*/
					if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) ^ svga->la;
					if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) ^ svga->lb;
					if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) ^ svga->lc;
					if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) ^ svga->ld;
					break;
			}
		}
		break;
	case 2:
		if (!(svga->gdcreg[3] & 0x18) && !svga->gdcreg[1]) {
			if (writemask2 & 1) svga->vram[addr]       = (((val & 1) ? 0xff : 0) & svga->gdcreg[8]) | (svga->la & ~svga->gdcreg[8]);
			if (writemask2 & 2) svga->vram[addr | 0x1] = (((val & 2) ? 0xff : 0) & svga->gdcreg[8]) | (svga->lb & ~svga->gdcreg[8]);
			if (writemask2 & 4) svga->vram[addr | 0x2] = (((val & 4) ? 0xff : 0) & svga->gdcreg[8]) | (svga->lc & ~svga->gdcreg[8]);
			if (writemask2 & 8) svga->vram[addr | 0x3] = (((val & 8) ? 0xff : 0) & svga->gdcreg[8]) | (svga->ld & ~svga->gdcreg[8]);
		} else {
			vala = ((val & 1) ? 0xff : 0);
			valb = ((val & 2) ? 0xff : 0);
			valc = ((val & 4) ? 0xff : 0);
			vald = ((val & 8) ? 0xff : 0);
			switch (svga->gdcreg[3] & 0x18) {
				case 0: /*Set*/
					if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) | (svga->la & ~svga->gdcreg[8]);
					if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) | (svga->lb & ~svga->gdcreg[8]);
					if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) | (svga->lc & ~svga->gdcreg[8]);
					if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) | (svga->ld & ~svga->gdcreg[8]);
					break;
				case 8: /*AND*/
					if (writemask2 & 1) svga->vram[addr]       = (vala | ~svga->gdcreg[8]) & svga->la;
					if (writemask2 & 2) svga->vram[addr | 0x1] = (valb | ~svga->gdcreg[8]) & svga->lb;
					if (writemask2 & 4) svga->vram[addr | 0x2] = (valc | ~svga->gdcreg[8]) & svga->lc;
					if (writemask2 & 8) svga->vram[addr | 0x3] = (vald | ~svga->gdcreg[8]) & svga->ld;
					break;
				case 0x10: /*OR*/
					if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) | svga->la;
					if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) | svga->lb;
					if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) | svga->lc;
					if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) | svga->ld;
					break;
				case 0x18: /*XOR*/
					if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) ^ svga->la;
					if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) ^ svga->lb;
					if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) ^ svga->lc;
					if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) ^ svga->ld;
					break;
			}
		}
		break;
	case 3:
		if (svga->gdcreg[3] & 7) 
			val = svga_rotate[svga->gdcreg[3] & 7][val];
		wm = svga->gdcreg[8];
		svga->gdcreg[8] &= val;

		vala = (svga->gdcreg[0] & 1) ? 0xff : 0;
		valb = (svga->gdcreg[0] & 2) ? 0xff : 0;
		valc = (svga->gdcreg[0] & 4) ? 0xff : 0;
		vald = (svga->gdcreg[0] & 8) ? 0xff : 0;
		switch (svga->gdcreg[3] & 0x18) {
			case 0: /*Set*/
				if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) | (svga->la & ~svga->gdcreg[8]);
				if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) | (svga->lb & ~svga->gdcreg[8]);
				if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) | (svga->lc & ~svga->gdcreg[8]);
				if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) | (svga->ld & ~svga->gdcreg[8]);
				break;
			case 8: /*AND*/
				if (writemask2 & 1) svga->vram[addr]       = (vala | ~svga->gdcreg[8]) & svga->la;
				if (writemask2 & 2) svga->vram[addr | 0x1] = (valb | ~svga->gdcreg[8]) & svga->lb;
				if (writemask2 & 4) svga->vram[addr | 0x2] = (valc | ~svga->gdcreg[8]) & svga->lc;
				if (writemask2 & 8) svga->vram[addr | 0x3] = (vald | ~svga->gdcreg[8]) & svga->ld;
				break;
			case 0x10: /*OR*/
				if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) | svga->la;
				if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) | svga->lb;
				if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) | svga->lc;
				if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) | svga->ld;
				break;
			case 0x18: /*XOR*/
				if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) ^ svga->la;
				if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) ^ svga->lb;
				if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) ^ svga->lc;
				if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) ^ svga->ld;
				break;
		}
		svga->gdcreg[8] = wm;
		break;
    }
}


static uint8_t
gd54xx_read(uint32_t addr, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;
    addr &= svga->banked_mask;
    addr = (addr & 0x7fff) + gd54xx->bank[(addr >> 15) & 1];
    return svga_read_linear(addr, svga);
}


static uint16_t
gd54xx_readw(uint32_t addr, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;

    addr &= svga->banked_mask;
    addr = (addr & 0x7fff) + gd54xx->bank[(addr >> 15) & 1];
    return svga_readw_linear(addr, svga);
}


static uint32_t
gd54xx_readl(uint32_t addr, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;

    addr &= svga->banked_mask;
    addr = (addr & 0x7fff) + gd54xx->bank[(addr >> 15) & 1];
    return svga_readl_linear(addr, svga);
}


static void
gd543x_mmio_write(uint32_t addr, uint8_t val, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;

    switch (addr & 0xff) {
	case 0x00:
		gd54xx->blt.bg_col = (gd54xx->blt.bg_col & 0xff00) | val;
		break;
	case 0x01:
		gd54xx->blt.bg_col = (gd54xx->blt.bg_col & 0x00ff) | (val << 8);
		break;

	case 0x04:
		gd54xx->blt.fg_col = (gd54xx->blt.fg_col & 0xff00) | val;
		break;
	case 0x05:
		gd54xx->blt.fg_col = (gd54xx->blt.fg_col & 0x00ff) | (val << 8);
		break;

	case 0x08:
		gd54xx->blt.width = (gd54xx->blt.width & 0xff00) | val;
		break;
	case 0x09:
		gd54xx->blt.width = (gd54xx->blt.width & 0x00ff) | (val << 8);
		break;
	case 0x0a:
		gd54xx->blt.height = (gd54xx->blt.height & 0xff00) | val;
		break;
	case 0x0b:
		gd54xx->blt.height = (gd54xx->blt.height & 0x00ff) | (val << 8);
		break;
	case 0x0c:
		gd54xx->blt.dst_pitch = (gd54xx->blt.dst_pitch & 0xff00) | val;
		break;
	case 0x0d:
		gd54xx->blt.dst_pitch = (gd54xx->blt.dst_pitch & 0x00ff) | (val << 8);
		break;
	case 0x0e:
		gd54xx->blt.src_pitch = (gd54xx->blt.src_pitch & 0xff00) | val;
		break;
	case 0x0f:
		gd54xx->blt.src_pitch = (gd54xx->blt.src_pitch & 0x00ff) | (val << 8);
		break;

	case 0x10:
		gd54xx->blt.dst_addr = (gd54xx->blt.dst_addr & 0xffff00) | val;
		break;
	case 0x11:
		gd54xx->blt.dst_addr = (gd54xx->blt.dst_addr & 0xff00ff) | (val << 8);
		break;
	case 0x12:
		gd54xx->blt.dst_addr = (gd54xx->blt.dst_addr & 0x00ffff) | (val << 16);
		break;

	case 0x14:
		gd54xx->blt.src_addr = (gd54xx->blt.src_addr & 0xffff00) | val;
		break;
	case 0x15:
		gd54xx->blt.src_addr = (gd54xx->blt.src_addr & 0xff00ff) | (val << 8);
		break;
	case 0x16:
		gd54xx->blt.src_addr = (gd54xx->blt.src_addr & 0x00ffff) | (val << 16);
		break;

	case 0x17:
		gd54xx->blt.mask = val;
		break;
	case 0x18:
		gd54xx->blt.mode = val;
		break;

	case 0x1a:
		gd54xx->blt.rop = val;
		break;

	case 0x40:
		if (val & 0x02) {
			if (gd54xx->blt.mode == CIRRUS_BLTMODE_MEMSYSSRC) {
				gd54xx->blt.sys_tx = 1;
				gd54xx->blt.sys_cnt = 0;
				gd54xx->blt.sys_buf = 0;
				gd54xx->blt.pixel_cnt = gd54xx->blt.scan_cnt = 0;
				gd54xx->blt.src_addr_backup = gd54xx->blt.src_addr;
				gd54xx->blt.dst_addr_backup = gd54xx->blt.dst_addr;						
			} else
				gd54xx_start_blit(0, -1, gd54xx, &gd54xx->svga);
		}
		break;
    }
}


static uint8_t
gd543x_mmio_read(uint32_t addr, void *p)
{
    switch (addr & 0xff) {
	case 0x40: /*BLT status*/
		return 0;
    }
    return 0xff; /*All other registers read-only*/
}


static void 
gd54xx_start_blit(uint32_t cpu_dat, int count, gd54xx_t *gd54xx, svga_t *svga)
{
    int blt_mask = gd54xx->blt.mask & 7;

    if (gd54xx->blt.mode & CIRRUS_BLTMODE_PIXELWIDTH16)
	blt_mask *= 2;

    if (count == -1) {
	gd54xx->blt.dst_addr_backup = gd54xx->blt.dst_addr;
	gd54xx->blt.src_addr_backup = gd54xx->blt.src_addr;
	gd54xx->blt.width_backup    = gd54xx->blt.width;
	gd54xx->blt.height_internal = gd54xx->blt.height;
	gd54xx->blt.x_count         = 0;
	if ((gd54xx->blt.mode & (CIRRUS_BLTMODE_PATTERNCOPY|CIRRUS_BLTMODE_COLOREXPAND)) == (CIRRUS_BLTMODE_PATTERNCOPY|CIRRUS_BLTMODE_COLOREXPAND))
		gd54xx->blt.y_count = gd54xx->blt.src_addr & 7;
	else
		gd54xx->blt.y_count = 0;

	if ((gd54xx->blt.mode & (CIRRUS_BLTMODE_MEMSYSSRC|CIRRUS_BLTMODE_COLOREXPAND)) == (CIRRUS_BLTMODE_MEMSYSSRC|CIRRUS_BLTMODE_COLOREXPAND)) {
		mem_mapping_set_handler(&svga->mapping, NULL, NULL, NULL, NULL, gd54xx_blt_write_w, gd54xx_blt_write_l);
		mem_mapping_set_p(&svga->mapping, gd54xx);
		return;
	} else if (gd54xx->blt.mode != CIRRUS_BLTMODE_MEMSYSSRC) {
		mem_mapping_set_handler(&svga->mapping, gd54xx_read, NULL, NULL, gd54xx_write, NULL, NULL);
		mem_mapping_set_p(&gd54xx->svga.mapping, gd54xx);
		gd543x_recalc_mapping(gd54xx);
	}                
    } else if (gd54xx->blt.height_internal == 0xffff)
	return;

    while (count) {
	uint8_t src = 0, dst;
	int mask = 0;

	if (gd54xx->blt.mode & CIRRUS_BLTMODE_MEMSYSSRC) {
		if (gd54xx->blt.mode & CIRRUS_BLTMODE_COLOREXPAND) {
			src = (cpu_dat & 0x80) ? gd54xx->blt.fg_col : gd54xx->blt.bg_col;
			mask = cpu_dat & 0x80;
			cpu_dat <<= 1;
			count--;
		}
	} else {
		switch (gd54xx->blt.mode & (CIRRUS_BLTMODE_PATTERNCOPY|CIRRUS_BLTMODE_COLOREXPAND)) {
			case 0x00:
				src = svga->vram[gd54xx->blt.src_addr & svga->vram_mask];
				gd54xx->blt.src_addr += ((gd54xx->blt.mode & CIRRUS_BLTMODE_BACKWARDS) ? -1 : 1);
				mask = 1;
				break;
			case CIRRUS_BLTMODE_PATTERNCOPY:
				if (gd54xx->blt.mode & CIRRUS_BLTMODE_PIXELWIDTH16)
					src = svga->vram[(gd54xx->blt.src_addr & (svga->vram_mask & ~3)) + (gd54xx->blt.y_count << 4) + (gd54xx->blt.x_count & 15)];
				else
					src = svga->vram[(gd54xx->blt.src_addr & (svga->vram_mask & ~7)) + (gd54xx->blt.y_count << 3) + (gd54xx->blt.x_count & 7)];
				mask = 1;
				break;
			case CIRRUS_BLTMODE_COLOREXPAND:
				if (gd54xx->blt.mode & CIRRUS_BLTMODE_PIXELWIDTH16) {
					mask = svga->vram[gd54xx->blt.src_addr & svga->vram_mask] & (0x80 >> (gd54xx->blt.x_count >> 1));
					if (gd54xx->blt.dst_addr & 1)
						src = mask ? (gd54xx->blt.fg_col >> 8) : (gd54xx->blt.bg_col >> 8);
					else
						src = mask ? gd54xx->blt.fg_col : gd54xx->blt.bg_col;
				} else {
					mask = svga->vram[gd54xx->blt.src_addr & svga->vram_mask] & (0x80 >> gd54xx->blt.x_count);
					src = mask ? gd54xx->blt.fg_col : gd54xx->blt.bg_col;
				}
				break;
			case CIRRUS_BLTMODE_PATTERNCOPY|CIRRUS_BLTMODE_COLOREXPAND:
				if (gd54xx->blt.mode & CIRRUS_BLTMODE_PIXELWIDTH16) {
					mask = svga->vram[(gd54xx->blt.src_addr & svga->vram_mask & ~7) | gd54xx->blt.y_count] & (0x80 >> (gd54xx->blt.x_count >> 1));
					if (gd54xx->blt.dst_addr & 1)
						src = mask ? (gd54xx->blt.fg_col >> 8) : (gd54xx->blt.bg_col >> 8);
					else
						src = mask ? gd54xx->blt.fg_col : gd54xx->blt.bg_col;
				} else {
					mask = svga->vram[(gd54xx->blt.src_addr & svga->vram_mask & ~7) | gd54xx->blt.y_count] & (0x80 >> gd54xx->blt.x_count);
					src = mask ? gd54xx->blt.fg_col : gd54xx->blt.bg_col;
				}
				break;
		}
		count--;
	}
	dst = svga->vram[gd54xx->blt.dst_addr & svga->vram_mask];
	svga->changedvram[(gd54xx->blt.dst_addr & svga->vram_mask) >> 12] = changeframecount;

	switch (gd54xx->blt.rop) {
		case 0x00: dst = 0;             break;
		case 0x05: dst =   src &  dst;  break;
		case 0x06: dst =   dst;         break;
		case 0x09: dst =   src & ~dst;  break;
		case 0x0b: dst = ~ dst;         break;
		case 0x0d: dst =   src;         break;
		case 0x0e: dst = 0xff;          break;
		case 0x50: dst = ~ src &  dst;  break;
		case 0x59: dst =   src ^  dst;  break;
		case 0x6d: dst =   src |  dst;  break;
		case 0x90: dst = ~(src |  dst); break;
		case 0x95: dst = ~(src ^  dst); break;
		case 0xad: dst =   src | ~dst;  break;
		case 0xd0: dst =  ~src;         break;
		case 0xd6: dst =  ~src |  dst;  break;
		case 0xda: dst = ~(src &  dst); break;                       
	}

	if ((gd54xx->blt.width_backup - gd54xx->blt.width) >= blt_mask &&
	    !((gd54xx->blt.mode & CIRRUS_BLTMODE_TRANSPARENTCOMP) && !mask))
		svga->vram[gd54xx->blt.dst_addr & svga->vram_mask] = dst;

	gd54xx->blt.dst_addr += ((gd54xx->blt.mode & CIRRUS_BLTMODE_BACKWARDS) ? -1 : 1);

	gd54xx->blt.x_count++;
	if (gd54xx->blt.x_count == ((gd54xx->blt.mode & CIRRUS_BLTMODE_PIXELWIDTH16) ? 16 : 8)) {
		gd54xx->blt.x_count = 0;
		if ((gd54xx->blt.mode & (CIRRUS_BLTMODE_PATTERNCOPY|CIRRUS_BLTMODE_COLOREXPAND)) == CIRRUS_BLTMODE_COLOREXPAND)
			gd54xx->blt.src_addr++;
	}

	gd54xx->blt.width--;

	if (gd54xx->blt.width == 0xffff) {
		gd54xx->blt.width = gd54xx->blt.width_backup;

		gd54xx->blt.dst_addr = gd54xx->blt.dst_addr_backup = gd54xx->blt.dst_addr_backup + ((gd54xx->blt.mode & CIRRUS_BLTMODE_BACKWARDS) ? -gd54xx->blt.dst_pitch : gd54xx->blt.dst_pitch);

		switch (gd54xx->blt.mode & (CIRRUS_BLTMODE_PATTERNCOPY|CIRRUS_BLTMODE_COLOREXPAND)) {
			case 0x00:
				gd54xx->blt.src_addr = gd54xx->blt.src_addr_backup = gd54xx->blt.src_addr_backup + ((gd54xx->blt.mode & CIRRUS_BLTMODE_BACKWARDS) ? -gd54xx->blt.src_pitch : gd54xx->blt.src_pitch);
				break;
			case CIRRUS_BLTMODE_COLOREXPAND:
				if (gd54xx->blt.x_count != 0)
					gd54xx->blt.src_addr++;
				break;
		}

		gd54xx->blt.x_count = 0;
		if (gd54xx->blt.mode & CIRRUS_BLTMODE_BACKWARDS)
			gd54xx->blt.y_count = (gd54xx->blt.y_count - 1) & 7;
		else
			gd54xx->blt.y_count = (gd54xx->blt.y_count + 1) & 7;

		gd54xx->blt.height_internal--;
		if (gd54xx->blt.height_internal == 0xffff) {
			if (gd54xx->blt.mode & CIRRUS_BLTMODE_MEMSYSSRC) {
				mem_mapping_set_handler(&svga->mapping, gd54xx_read, NULL, NULL, gd54xx_write, NULL, NULL);
				mem_mapping_set_p(&svga->mapping, gd54xx);
				gd543x_recalc_mapping(gd54xx);
			}
			return;
		}

		if (gd54xx->blt.mode & CIRRUS_BLTMODE_MEMSYSSRC)
			return;
	}
    }
}


static void
*gd54xx_init(device_t *info)
{
    gd54xx_t *gd54xx = malloc(sizeof(gd54xx_t));
    svga_t *svga = &gd54xx->svga;
    int id = info->local;
	wchar_t *romfn = NULL;
    memset(gd54xx, 0, sizeof(gd54xx_t));

    switch (id) {
	case CIRRUS_ID_CLGD5428:
		romfn = BIOS_GD5428_PATH;
		break;

	case CIRRUS_ID_CLGD5429:
		romfn = BIOS_GD5429_PATH;
		break;
    }

    gd54xx->vram_size = device_get_config_int("memory");
    gd54xx->vram_mask = (gd54xx->vram_size << 20) - 1;

    rom_init(&gd54xx->bios_rom, romfn, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    svga_init(&gd54xx->svga, gd54xx, gd54xx->vram_size << 20,
	      gd54xx_recalctimings, gd54xx_in, gd54xx_out,
	      gd54xx_hwcursor_draw, NULL);

    mem_mapping_set_handler(&svga->mapping, gd54xx_read, gd54xx_readw, gd54xx_readl, gd54xx_write, gd54xx_writew, gd54xx_writel);
    mem_mapping_set_p(&svga->mapping, gd54xx);

    mem_mapping_add(&gd54xx->mmio_mapping, 0, 0, gd543x_mmio_read, NULL, NULL, gd543x_mmio_write, NULL, NULL,  NULL, 0, gd54xx);

    io_sethandler(0x03c0, 0x0020, gd54xx_in, NULL, NULL, gd54xx_out, NULL, NULL, gd54xx);

    if (gd54xx->vram_size == (2 << 20)) {
	gd54xx->vram_code = 3;
	svga->seqregs[0x0f] = 0x18; /*2MB of memory*/
	svga->seqregs[0x17] = 0x38; /*ISA, win3.1 drivers require so*/
	svga->seqregs[0x1f] = 0x22;
    } else {
	gd54xx->vram_code = 2;
	svga->seqregs[0x0f] = 0x10; /*1MB of memory*/
	svga->seqregs[0x17] = 0x38; /*ISA, win3.1 drivers require so*/
	svga->seqregs[0x1f] = 0x22;	
    }

    svga->crtc[0x27] = id;

    svga->hwcursor.yoff = 32;
    svga->hwcursor.xoff = 0;

    gd54xx->vclk_n[0] = 0x4a;
    gd54xx->vclk_d[0] = 0x2b;
    gd54xx->vclk_n[1] = 0x5b;
    gd54xx->vclk_d[1] = 0x2f;

    gd54xx->bank[1] = 0x8000;

    return gd54xx;
}


static int
gd5428_available(void)
{
    return rom_present(BIOS_GD5428_PATH);
}


static int
gd5429_available(void)
{
    return rom_present(BIOS_GD5429_PATH);
}


void
gd54xx_close(void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;

    svga_close(&gd54xx->svga);
    
    free(gd54xx);
}


void
gd54xx_speed_changed(void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    
    svga_recalctimings(&gd54xx->svga);
}


void
gd54xx_force_redraw(void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;

    gd54xx->svga.fullchange = changeframecount;
}


void
gd54xx_add_status_info(char *s, int max_len, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    
    svga_add_status_info(s, max_len, &gd54xx->svga);
}


static device_config_t gd542x_config[] =
{
        {
                .name = "memory",
                .description = "Memory size",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "1 MB",
                                .value = 1
                        },
                        {
                                .description = "2 MB",
                                .value = 2
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 2
        },
        {
                .type = -1
        }
};


device_t gd5428_device =
{
    "Cirrus Logic GD5428",
    DEVICE_VLB,
    CIRRUS_ID_CLGD5428,
    gd54xx_init, 
    gd54xx_close, 
    NULL,
    gd5428_available,
    gd54xx_speed_changed,
    gd54xx_force_redraw,
    gd54xx_add_status_info,
    gd542x_config
};


device_t gd5429_device =
{
    "Cirrus Logic GD5429",
    DEVICE_VLB,
    CIRRUS_ID_CLGD5429,
    gd54xx_init, 
    gd54xx_close, 
    NULL,
    gd5429_available,
    gd54xx_speed_changed,
    gd54xx_force_redraw,
    gd54xx_add_status_info,
    gd542x_config
};
