/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of select Cirrus Logic cards (currently only GD5428 is fully supported).
 *
 * Version:	@(#)vid_cl_5428.c	1.0.0	2018/12/11
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
#include "vid_cl5428.h"

#define BIOS_GD5428_PATH		L"roms/video/cirruslogic/Diamond SpeedStar PRO VLB (Cirrus Logic 5428)_v3.04.bin"

#define CIRRUS_ID_CLGD5422		0x8c
#define CIRRUS_ID_CLGD5428		0x98
#define CIRRUS_ID_CLGD5429		0x9c
#define CIRRUS_ID_CLGD5430		0xa0
#define CIRRUS_ID_CLGD5434		0xa8
#define CIRRUS_ID_CLGD5436		0xac
#define CIRRUS_ID_CLGD5446		0xb8

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


typedef struct gd5428_t
{
    mem_mapping_t	mmio_mapping;

    svga_t		svga;

    rom_t		bios_rom;

    uint32_t		vram_size;
    uint8_t		vram_code;

    uint8_t		vclk_n[4];
    uint8_t		vclk_d[4];        
    uint32_t		bank[2];

    struct {
	uint8_t			state;
	int			ctrl;
	PALETTE			pal;
    } ramdac;		
	
    struct {
	uint16_t		width, height;
	uint16_t		dst_pitch, src_pitch;               
	uint32_t		dst_addr, src_addr;
	uint8_t			mask, mode, rop;
	uint8_t			status;
	uint16_t		trans_col, trans_mask;

	uint32_t		dst_addr_backup, src_addr_backup;
	uint16_t		width_backup, height_internal;

	int			sys_tx;
	uint8_t			sys_cnt;
	uint32_t		sys_buf;
	uint16_t		pixel_cnt;
	uint16_t		scan_cnt;
    } blt;
} gd5428_t;


static void 
gd5428_blit_dword(gd5428_t *gd5428, svga_t *svga);

static void 
gd5428_blit_byte(gd5428_t *gd5428, svga_t *svga);

static void 
gd5428_copy_pixel(gd5428_t *gd5428, svga_t *svga, uint8_t src, uint8_t dst);

static void 
gd5428_start_blit(gd5428_t *gd5428, svga_t *svga);

static void gd5428_recalc_banking(gd5428_t *gd5428);
static void gd5428_recalc_mapping(gd5428_t *gd5428);


static void
gd5428_out(uint16_t addr, uint8_t val, void *p)
{
    gd5428_t *gd5428 = (gd5428_t *)p;
    svga_t *svga = &gd5428->svga;
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
					gd5428->vclk_n[svga->seqaddr-0x0b] = val;
					break;
				case 0x1b: case 0x1c: case 0x1d: case 0x1e: /* VCLK stuff */
					gd5428->vclk_d[svga->seqaddr-0x1b] = val;
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
					gd5428_recalc_mapping(gd5428);
					break;
			}
			return;
		}
		break;
	case 0x3C6:
		if (gd5428->ramdac.state == 4) {
			gd5428->ramdac.state = 0;
			gd5428->ramdac.ctrl = val;

			if (svga->seqregs[0x07] & CIRRUS_SR7_BPP_SVGA)  {
				switch (svga->seqregs[0x07] & CIRRUS_SR7_BPP_MASK) {
					case CIRRUS_SR7_BPP_8:
						svga->bpp = 8;
						break;		
					case CIRRUS_SR7_BPP_16_DOUBLEVCLK:
					case CIRRUS_SR7_BPP_16:
						if (gd5428->ramdac.ctrl & 0x01)
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
		gd5428->ramdac.state = 0;
		break;
	case 0x3C7: case 0x3C8:
		gd5428->ramdac.state = 0;
		break;	
	case 0x3C9:
		if (svga->seqregs[0x12] & CIRRUS_CURSOR_HIDDENPEL) {
			gd5428->ramdac.state = 0;
			svga->fullchange = changeframecount;
			switch (svga->dac_pos) {
				case 0: 
					gd5428->ramdac.pal[svga->dac_write & 0xf].r = val & 63;
					svga->dac_pos++; 
					break;
				case 1: 
					gd5428->ramdac.pal[svga->dac_write & 0xf].g = val & 63;
					svga->dac_pos++; 
					break;
				case 2: 
					gd5428->ramdac.pal[svga->dac_write & 0xf].b = val & 63;
					svga->dac_pos = 0; 
					svga->dac_write = (svga->dac_write + 1) & 255; 
					break;
			}
			return;
		}
		gd5428->ramdac.state = 0;
		break;	
	case 0x3cf:
		if (svga->gdcaddr == 0) {
			svga->gdcreg[0] = val & 0xff;
			return;
		}
		if (svga->gdcaddr == 1) {
			svga->gdcreg[1] = val & 0xff;
			return;
		}
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
		if (svga->gdcaddr == 6) {
			if ((svga->gdcreg[6] & 0xc) != (val & 0xc)) {
				svga->gdcreg[6] = val;
				gd5428_recalc_mapping(gd5428);
			} else
				svga->gdcreg[6] = val;
			return;
		}
		if (svga->gdcaddr > 8) {
			svga->gdcreg[svga->gdcaddr & 0x3f] = val;
			switch (svga->gdcaddr) {
				case 0x09: case 0x0a: case 0x0b:
					gd5428_recalc_banking(gd5428);
					if (svga->gdcreg[0xb] & 0x04)
						svga->writemode = svga->gdcreg[5] & 7;
					else {
						svga->writemode = svga->gdcreg[5] & 3;
						svga->gdcreg[0] &= 0x0f;
						svga->gdcreg[1] &= 0x0f;
					}
					break;
				case 0x20:
					gd5428->blt.width = (gd5428->blt.width & 0xff00) | val;
					break;
				case 0x21:
					gd5428->blt.width = (gd5428->blt.width & 0x00ff) | (val << 8);
					break;
				case 0x22:
					gd5428->blt.height = (gd5428->blt.height & 0xff00) | val;
					break;
				case 0x23:
					gd5428->blt.height = (gd5428->blt.height & 0x00ff) | (val << 8);
					break;
				case 0x24:
					gd5428->blt.dst_pitch = (gd5428->blt.dst_pitch & 0xff00) | val;
					break;
				case 0x25:
					gd5428->blt.dst_pitch = (gd5428->blt.dst_pitch & 0x00ff) | (val << 8);
					break;
				case 0x26:
					gd5428->blt.src_pitch = (gd5428->blt.src_pitch & 0xff00) | val;
					break;
				case 0x27:
					gd5428->blt.src_pitch = (gd5428->blt.src_pitch & 0x00ff) | (val << 8);
					break;			
				case 0x28:
					gd5428->blt.dst_addr = (gd5428->blt.dst_addr & 0xffff00) | val;
					break;
				case 0x29:
					gd5428->blt.dst_addr = (gd5428->blt.dst_addr & 0xff00ff) | (val << 8);
					break;
				case 0x2a:
					gd5428->blt.dst_addr = (gd5428->blt.dst_addr & 0x00ffff) | (val << 16);
					break;
				case 0x2c:
					gd5428->blt.src_addr = (gd5428->blt.src_addr & 0xffff00) | val;
					break;
				case 0x2d:
					gd5428->blt.src_addr = (gd5428->blt.src_addr & 0xff00ff) | (val << 8);
					break;
				case 0x2e:
					gd5428->blt.src_addr = (gd5428->blt.src_addr & 0x00ffff) | (val << 16);
					break;
				case 0x30:
					gd5428->blt.mode = val;
					break;			
				case 0x31:
					gd5428->blt.status = val & ~0xf2;
					if (val & 0x02) {
						if (gd5428->blt.mode & CIRRUS_BLTMODE_MEMSYSSRC) {
							gd5428->blt.sys_tx = 1;
							gd5428->blt.sys_cnt = 0;
							gd5428->blt.sys_buf = 0;
							gd5428->blt.pixel_cnt = gd5428->blt.scan_cnt = 0;
							gd5428->blt.src_addr_backup = gd5428->blt.src_addr;
							gd5428->blt.dst_addr_backup = gd5428->blt.dst_addr;
							gd5428->blt.status |= 0x09;
						} else
							gd5428_start_blit(gd5428, &gd5428->svga);
					}
					break;			
				case 0x32:
					gd5428->blt.rop = val;
					break;
				case 0x34:
					gd5428->blt.trans_col = (gd5428->blt.trans_col & 0xff00) | val;
					break;
				case 0x35:
					gd5428->blt.trans_col = (gd5428->blt.trans_col & 0x00ff) | (val << 8);
					break;
				case 0x36:
					gd5428->blt.trans_mask = (gd5428->blt.trans_mask & 0xff00) | val;
					break;
				case 0x37:
					gd5428->blt.trans_mask = (gd5428->blt.trans_mask & 0x00ff) | (val << 8);
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
gd5428_in(uint16_t addr, void *p)
{
    gd5428_t *gd5428 = (gd5428_t *)p;
    svga_t *svga = &gd5428->svga;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3d0) && !(svga->miscout & 1)) 
	addr ^= 0x60;

    switch (addr) {
	case 0x3c5:
		if (svga->seqaddr > 5) {
			switch (svga->seqaddr) {
				case 6:
					return ((svga->seqregs[6] & 0x17) == 0x12) ? 0x12 : 0x0f;			
				case 0x0b: case 0x0c: case 0x0d: case 0x0e:
					return gd5428->vclk_n[svga->seqaddr-0x0b];
				case 0x0f:
					return svga->seqregs[0x0f];			
				case 0x15:
					return gd5428->vram_code;			
				case 0x17:
					return svga->seqregs[0x17];
				case 0x1b: case 0x1c: case 0x1d: case 0x1e:
					return gd5428->vclk_d[svga->seqaddr-0x1b];
				case 0x1f:
					return svga->seqregs[0x1f];
			}
			return svga->seqregs[svga->seqaddr & 0x3f];
		}
		break;
	case 0x3C6:
		if (gd5428->ramdac.state == 4) {
			gd5428->ramdac.state = 0;
			return gd5428->ramdac.ctrl;
		}
		gd5428->ramdac.state++;
		break;
	case 0x3C7: case 0x3C8:
		gd5428->ramdac.state = 0;
		break;
	case 0x3C9:
		if (svga->seqregs[0x12] & CIRRUS_CURSOR_HIDDENPEL) {
			gd5428->ramdac.state = 0;
			switch (svga->dac_pos) {
				case 0: 
					svga->dac_pos++; 
					return gd5428->ramdac.pal[svga->dac_read & 0xf].r;
				case 1: 
					svga->dac_pos++; 
					return gd5428->ramdac.pal[svga->dac_read & 0xf].g;
				case 2: 
					svga->dac_pos=0; 
					svga->dac_read = (svga->dac_read + 1) & 255; 
					return gd5428->ramdac.pal[(svga->dac_read - 1) & 15].b;
			}
		}
		gd5428->ramdac.state = 0;
		break;
	case 0x3cf:
		if (svga->gdcaddr > 8) {
			if (svga->gdcaddr == 0x31)
				return gd5428->blt.status;	
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
gd5428_recalc_banking(gd5428_t *gd5428)
{
    svga_t *svga = &gd5428->svga;

    if (svga->gdcreg[0xb] & 0x20)
	gd5428->bank[0] = (svga->gdcreg[0x09] & 0x7f) << 14;
    else
	gd5428->bank[0] = svga->gdcreg[0x09] << 12;
                        
    if (svga->gdcreg[0xb] & 0x01) {
	if (svga->gdcreg[0xb] & 0x20)
		gd5428->bank[1] = (svga->gdcreg[0x0a] & 0x7f) << 14;
	else
		gd5428->bank[1] = svga->gdcreg[0x0a] << 12;
    } else
	gd5428->bank[1] = gd5428->bank[0] + 0x8000;
}


static void
gd5428_recalc_mapping(gd5428_t *gd5428)
{
    svga_t *svga = &gd5428->svga;

    switch (svga->gdcreg[6] & 0x0C) {
	case 0x0: /*128k at A0000*/
		mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x20000);
		mem_mapping_disable(&gd5428->mmio_mapping);
		svga->banked_mask = 0xffff;
		break;
	case 0x4: /*64k at A0000*/
		mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
		if (svga->seqregs[0x17] & 0x04)
			mem_mapping_set_addr(&gd5428->mmio_mapping, 0xb8000, 0x00100);
		svga->banked_mask = 0xffff;
		break;
	case 0x8: /*32k at B0000*/
		mem_mapping_set_addr(&svga->mapping, 0xb0000, 0x08000);
		mem_mapping_disable(&gd5428->mmio_mapping);
		svga->banked_mask = 0x7fff;
		break;
	case 0xC: /*32k at B8000*/
		mem_mapping_set_addr(&svga->mapping, 0xb8000, 0x08000);	
		mem_mapping_disable(&gd5428->mmio_mapping);
		svga->banked_mask = 0x7fff;
		break;
    }
}
        

static void
gd5428_recalctimings(svga_t *svga)
{
    gd5428_t *gd5428 = (gd5428_t *)svga->p;	
    uint8_t clocksel;

    svga->rowoffset = (svga->crtc[0x13]) | ((svga->crtc[0x1b] & 0x10) << 4);

    svga->ma_latch = (svga->crtc[0x0c] << 8)
		   +  svga->crtc[0x0d]
		   +  ((svga->crtc[0x1b] & 0x01) << 16)
		   +  ((svga->crtc[0x1b] & 0x0c) << 15)
		   +  ((svga->crtc[0x1d] & 0x80) << 12);

    if (svga->crtc[0x1a] & 0x01) {
	svga->vtotal *= 2;
	svga->dispend *= 2;
	svga->vblankstart *= 2;
	svga->vsyncstart *= 2;
	svga->split *= 2;
    }

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

    if (!gd5428->vclk_n[clocksel] || !gd5428->vclk_d[clocksel])
	svga->clock = cpuclock / ((svga->miscout & 0x0c) ? 28322000.0 : 25175000.0);
    else {
	int n = gd5428->vclk_n[clocksel] & 0x7f;
	int d = (gd5428->vclk_d[clocksel] & 0x3e) >> 1;
	int m = gd5428->vclk_d[clocksel] & 0x01 ? 2 : 1;
	float freq = (14318184.0 * ((float)n / ((float)d * m)));
	svga->clock = cpuclock / freq;
    }
}


static void gd5428_hwcursor_draw(svga_t *svga, int displine)
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
gd5428_mem_writeb_mode4and5_8bpp(gd5428_t *gd5428, svga_t *svga,
					      uint8_t mode,
					      uint32_t offset,
					      uint32_t val)
{
    int x;
    uint8_t *dst;

    dst = svga->vram + (offset & svga->vram_mask);
    for (x = 0; x < 8; x++) {
	if (val & 0x80) 
	{
		*dst = svga->gdcreg[1];
	} 
	else 
	{
		if (mode == 5)
			*dst = svga->gdcreg[0];
	}
	val <<= 1;
	dst++;
    }
    svga->changedvram[(offset & svga->vram_mask) >> 12] = changeframecount;
}


static void
gd5428_mem_writeb_mode4and5_16bpp(gd5428_t *gd5428, svga_t *svga,
					      uint8_t mode,
					      uint32_t offset,
					      uint32_t val)
{
    int x;
    uint8_t *dst;

    dst = svga->vram + (offset & svga->vram_mask);
    for (x = 0; x < 8; x++) {
	if (val & 0x80) 
	{
		*dst = svga->gdcreg[1];
		*(dst + 1) = svga->gdcreg[0x11];
	} 
	else
	{
		if (mode == 5)
		{
			*dst = svga->gdcreg[0];
			*(dst + 1) = svga->gdcreg[0x10];
		}
	}
	val <<= 1;
	dst += 2;
    }
    svga->changedvram[(offset & svga->vram_mask) >> 12] = changeframecount;
}


static void
gd5428_write(uint32_t addr, uint8_t val, void *p)
{
    gd5428_t *gd5428 = (gd5428_t *)p;
    svga_t *svga = &gd5428->svga;

    if (gd5428->blt.sys_tx) {
	if (gd5428->blt.mode & CIRRUS_BLTMODE_COLOREXPAND) {
		gd5428->blt.sys_buf &= ~(0x000000ff);
		gd5428->blt.sys_buf |= val;
		gd5428_blit_byte(gd5428, svga);
		gd5428->blt.sys_cnt = 0;
	} else {
		gd5428->blt.sys_buf &= ~(0x000000ff << (gd5428->blt.sys_cnt * 8));
		gd5428->blt.sys_buf |= (val << (gd5428->blt.sys_cnt * 8));
		gd5428->blt.sys_cnt++;
		if(gd5428->blt.sys_cnt >= 4) {
			gd5428_blit_dword(gd5428, svga);
			gd5428->blt.sys_cnt = 0;
		}
	}
	gd5428_recalc_mapping(gd5428);
	return;
    }
	
    addr &= svga->banked_mask;

    addr = (addr & 0x7fff) + gd5428->bank[(addr >> 15) & 1];

    if (svga->writemode < 4 || svga->writemode > 5)
	svga_write_linear(addr, val, svga);
    else {
	cycles -= video_timing_write_b;
	cycles_lost += video_timing_write_b;

	egawrites++;

	svga->changedvram[addr >> 12]=changeframecount;

	if ((svga->gdcreg[0x0b] & 0x14) != 0x14)
		gd5428_mem_writeb_mode4and5_8bpp(gd5428, svga, svga->writemode, addr << 3, val);
	else
		gd5428_mem_writeb_mode4and5_16bpp(gd5428, svga, svga->writemode, addr << 4, val);	
    }
}


static uint8_t
gd5428_read(uint32_t addr, void *p)
{
    gd5428_t *gd5428 = (gd5428_t *)p;
    svga_t *svga = &gd5428->svga;
    addr &= svga->banked_mask;
    addr = (addr & 0x7fff) + gd5428->bank[(addr >> 15) & 1];
    return svga_read_linear(addr, svga);
}


/* non colour-expanded BitBLTs from system memory must be doubleword sized, extra bytes are ignored */
static void 
gd5428_blit_dword(gd5428_t *gd5428, svga_t *svga)
{
    /* TODO: add support for reverse direction */
    uint8_t x, pixel;

    for (x=0;x<32;x+=8) {
	pixel = ((gd5428->blt.sys_buf & (0x000000ff << x)) >> x);
	if(gd5428->blt.pixel_cnt <= gd5428->blt.width)
		gd5428_copy_pixel(gd5428, svga, pixel, svga->vram[gd5428->blt.dst_addr_backup & svga->vram_mask]);
	gd5428->blt.dst_addr_backup++;
	gd5428->blt.pixel_cnt++;
    }
    if (gd5428->blt.pixel_cnt > gd5428->blt.width) {
	gd5428->blt.pixel_cnt = 0;
	gd5428->blt.scan_cnt++;
	gd5428->blt.dst_addr_backup = gd5428->blt.dst_addr + (gd5428->blt.dst_pitch*gd5428->blt.scan_cnt);
    }
    if (gd5428->blt.scan_cnt > gd5428->blt.height) {
	gd5428->blt.sys_tx = 0;  /*  BitBLT complete */
	gd5428->blt.status &= ~0x0b;
	gd5428_recalc_mapping(gd5428);
    }
}

/* colour-expanded BitBLTs from system memory are on a byte boundary, unused bits are ignored */
static void 
gd5428_blit_byte(gd5428_t *gd5428, svga_t *svga)
{
    /* TODO: add support for reverse direction */
    uint8_t x, pixel;

    for (x=0;x<8;x++) {
	/* use GR0/1/10/11 background/foreground regs */
	if (gd5428->blt.dst_addr_backup & 1)	
		pixel = ((gd5428->blt.sys_buf & (0x00000001 << (7-x))) >> (7-x)) ? svga->gdcreg[0x11] : svga->gdcreg[0x10];
	else
		pixel = ((gd5428->blt.sys_buf & (0x00000001 << (7-x))) >> (7-x)) ? svga->gdcreg[1] : svga->gdcreg[0];
	if(gd5428->blt.pixel_cnt <= gd5428->blt.width - 1)
		gd5428_copy_pixel(gd5428, svga, pixel, svga->vram[gd5428->blt.dst_addr_backup & svga->vram_mask]);
	gd5428->blt.dst_addr_backup++;
	gd5428->blt.pixel_cnt++;
    }
    if (gd5428->blt.pixel_cnt > gd5428->blt.width) {
	gd5428->blt.pixel_cnt = 0;
	gd5428->blt.scan_cnt++;
	gd5428->blt.dst_addr_backup = gd5428->blt.dst_addr + (gd5428->blt.dst_pitch*gd5428->blt.scan_cnt);
    }
    if(gd5428->blt.scan_cnt > gd5428->blt.height) {
	gd5428->blt.sys_tx = 0;  //  BitBLT complete
	gd5428->blt.status &= ~0x0b;
	gd5428_recalc_mapping(gd5428);
    }
}									 


static void
gd5428_copy_pixel(gd5428_t *gd5428, svga_t *svga, uint8_t src, uint8_t dst)
{
    svga->changedvram[(gd5428->blt.dst_addr_backup & svga->vram_mask) >> 12] = changeframecount;

    switch (gd5428->blt.rop) {
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

    /* handle transparency compare */
    if(gd5428->blt.mode & CIRRUS_BLTMODE_TRANSPARENTCOMP) {  /* TODO: 16-bit compare */
	/* if ROP result matches the transparency colour, don't change the pixel */
	if((dst & (~gd5428->blt.trans_mask & 0xff)) == ((gd5428->blt.trans_col & 0xff) & (~gd5428->blt.trans_mask & 0xff)))
		return;
    }

    svga->vram[gd5428->blt.dst_addr_backup & svga->vram_mask] = dst;
}


static void 
gd5428_start_blit(gd5428_t *gd5428, svga_t *svga)
{
    uint32_t x, y;

    gd5428->blt.src_addr_backup = gd5428->blt.src_addr;
    gd5428->blt.dst_addr_backup = gd5428->blt.dst_addr;

    for (y=0;y<=gd5428->blt.height;y++) {
	for (x=0;x<=gd5428->blt.width;x++) {
		if (gd5428->blt.mode & CIRRUS_BLTMODE_COLOREXPAND) {
			if (gd5428->blt.mode & CIRRUS_BLTMODE_PIXELWIDTH16) {
				uint16_t pixel = (svga->vram[gd5428->blt.src_addr_backup & svga->vram_mask] >> (7-((x/2) % 8)) & 0x01) ? ((svga->gdcreg[0x11]<<8)|svga->gdcreg[1]) : ((svga->gdcreg[0x10]<<8)|svga->gdcreg[0]);
	
				if(gd5428->blt.dst_addr_backup & 1)
					gd5428_copy_pixel(gd5428, svga, pixel >> 8, svga->vram[gd5428->blt.dst_addr_backup & svga->vram_mask]);
				else
					gd5428_copy_pixel(gd5428, svga, pixel & 0xff, svga->vram[gd5428->blt.dst_addr_backup & svga->vram_mask]);
				if((x % 8) == 7 && !(gd5428->blt.mode & CIRRUS_BLTMODE_PATTERNCOPY)) {  /* don't increment if a pattern (it's only 8 bits) */
					if (gd5428->blt.mode & CIRRUS_BLTMODE_BACKWARDS)
						gd5428->blt.src_addr_backup--;
					else
						gd5428->blt.src_addr_backup++;
				}
			} else {
				uint8_t pixel = (svga->vram[gd5428->blt.src_addr_backup & svga->vram_mask] >> (7-(x % 8)) & 0x01) ? svga->gdcreg[1] : svga->gdcreg[0];

				gd5428_copy_pixel(gd5428, svga, pixel, svga->vram[gd5428->blt.dst_addr_backup & svga->vram_mask]);					

				if((x % 8) == 7 && !(gd5428->blt.mode & CIRRUS_BLTMODE_PATTERNCOPY)) {  /* don't increment if a pattern (it's only 8 bits) */
					if (gd5428->blt.mode & CIRRUS_BLTMODE_BACKWARDS)
						gd5428->blt.src_addr_backup--;
					else
						gd5428->blt.src_addr_backup++;
				}
			}
		} else {
			gd5428_copy_pixel(gd5428, svga, svga->vram[gd5428->blt.src_addr_backup & svga->vram_mask], svga->vram[gd5428->blt.dst_addr_backup & svga->vram_mask]);
	
			if (gd5428->blt.mode & CIRRUS_BLTMODE_BACKWARDS)
				gd5428->blt.src_addr_backup--;
			else
				gd5428->blt.src_addr_backup++;
		}

		if (gd5428->blt.mode & CIRRUS_BLTMODE_BACKWARDS)
			gd5428->blt.dst_addr_backup--;
		else
			gd5428->blt.dst_addr_backup++;

		if (gd5428->blt.mode & CIRRUS_BLTMODE_BACKWARDS) {
			if (gd5428->blt.mode & CIRRUS_BLTMODE_PATTERNCOPY && (x % 8) == 7) {  /* 8x8 pattern - reset pattern source location */
				if (gd5428->blt.mode & CIRRUS_BLTMODE_COLOREXPAND) /* colour expand */
					gd5428->blt.src_addr_backup = gd5428->blt.src_addr - (1 * (y % 8)); /* patterns are linear data */
				else if(svga->bpp == 15 || svga->bpp == 16) {
					if(gd5428->blt.mode & CIRRUS_BLTMODE_PATTERNCOPY && (x % 16) == 15)
						gd5428->blt.src_addr_backup = gd5428->blt.src_addr - (16 * (y % 8));
				} else
					gd5428->blt.src_addr_backup = gd5428->blt.src_addr - (8 * (y % 8));
			}
		} else {
			if (gd5428->blt.mode & CIRRUS_BLTMODE_PATTERNCOPY && (x % 8) == 7) {  /* 8x8 pattern - reset pattern source location */
				if (gd5428->blt.mode & CIRRUS_BLTMODE_COLOREXPAND) /* colour expand */
					gd5428->blt.src_addr_backup = gd5428->blt.src_addr + (1 * (y % 8)); // patterns are linear data
				else if (svga->bpp == 15 || svga->bpp == 16) {
					if(gd5428->blt.mode & CIRRUS_BLTMODE_PATTERNCOPY && (x % 16) == 15)
						gd5428->blt.src_addr_backup = gd5428->blt.src_addr + (16 * (y % 8));
				} else
					gd5428->blt.src_addr_backup = gd5428->blt.src_addr + (8 * (y % 8));
			}
		}
	}

	if (gd5428->blt.mode & CIRRUS_BLTMODE_BACKWARDS) {
		if (gd5428->blt.mode & CIRRUS_BLTMODE_PATTERNCOPY) {  /* 8x8 pattern */
			if (gd5428->blt.mode & CIRRUS_BLTMODE_COLOREXPAND) /* colour expand */
				gd5428->blt.src_addr_backup = gd5428->blt.src_addr - (1 * (y % 8)); /* patterns are linear data */
			else if (svga->bpp == 15 || svga->bpp == 16) {
				if (gd5428->blt.mode & CIRRUS_BLTMODE_PATTERNCOPY && (x % 16) == 15)
					gd5428->blt.src_addr_backup = gd5428->blt.src_addr - (16 * (y % 8));
			} else
				gd5428->blt.src_addr_backup = gd5428->blt.src_addr - (8 * (y % 8));
		} else
			gd5428->blt.src_addr_backup = gd5428->blt.src_addr - (gd5428->blt.src_pitch*(y+1));

		gd5428->blt.dst_addr_backup = gd5428->blt.dst_addr - (gd5428->blt.dst_pitch*(y+1));
	} else {
		if (gd5428->blt.mode & CIRRUS_BLTMODE_PATTERNCOPY) {  /* 8x8 pattern */
			if (gd5428->blt.mode & CIRRUS_BLTMODE_COLOREXPAND) /* colour expand */
				gd5428->blt.src_addr_backup = gd5428->blt.src_addr + (1 * (y % 8)); /* patterns are linear data */
			else if(svga->bpp == 15 || svga->bpp == 16) {
				if (gd5428->blt.mode & CIRRUS_BLTMODE_PATTERNCOPY && (x % 16) == 15)
					gd5428->blt.src_addr_backup = gd5428->blt.src_addr + (16 * (y % 8));
			} else
				gd5428->blt.src_addr_backup = gd5428->blt.src_addr + (8 * (y % 8));
		} else
			gd5428->blt.src_addr_backup = gd5428->blt.src_addr + (gd5428->blt.src_pitch*(y+1));
				
		gd5428->blt.dst_addr_backup = gd5428->blt.dst_addr + (gd5428->blt.dst_pitch*(y+1));
	}
    }

    gd5428->blt.status &= ~0x02;
    gd5428_recalc_mapping(gd5428);
}


static void
*gd5428_init(device_t *info)
{
    gd5428_t *gd5428 = malloc(sizeof(gd5428_t));
    svga_t *svga = &gd5428->svga;
    int id = info->local;
    memset(gd5428, 0, sizeof(gd5428_t));

    rom_init(&gd5428->bios_rom, BIOS_GD5428_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    gd5428->vram_size = (2 << 20); /*2mb*/

    svga_init(&gd5428->svga, gd5428, gd5428->vram_size,
	      gd5428_recalctimings, gd5428_in, gd5428_out,
	      gd5428_hwcursor_draw, NULL);

    mem_mapping_set_handler(&svga->mapping, gd5428_read, NULL, NULL, gd5428_write, NULL, NULL);
    mem_mapping_set_p(&svga->mapping, gd5428);

    io_sethandler(0x03c0, 0x0020, gd5428_in, NULL, NULL, gd5428_out, NULL, NULL, gd5428);

    gd5428->vram_code = 3;
    svga->seqregs[0x0f] = 0x18; /*2MB of memory*/
    svga->seqregs[0x17] = 0x38; /*ISA*/
    svga->seqregs[0x1f] = 0x22;

    svga->crtc[0x27] = id;

    svga->hwcursor.yoff = 32;
    svga->hwcursor.xoff = 0;

    gd5428->vclk_n[0] = 0x4a;
    gd5428->vclk_d[0] = 0x2b;
    gd5428->vclk_n[1] = 0x5b;
    gd5428->vclk_d[1] = 0x2f;			

    gd5428->bank[1] = 0x8000;

    return gd5428;
}


static int
gd5428_available(void)
{
    return rom_present(BIOS_GD5428_PATH);
}


void
gd5428_close(void *p)
{
    gd5428_t *gd5428 = (gd5428_t *)p;

    svga_close(&gd5428->svga);
    
    free(gd5428);
}


void
gd5428_speed_changed(void *p)
{
    gd5428_t *gd5428 = (gd5428_t *)p;
    
    svga_recalctimings(&gd5428->svga);
}


void gd5428_force_redraw(void *p)
{
    gd5428_t *gd5428 = (gd5428_t *)p;

    gd5428->svga.fullchange = changeframecount;
}


void gd5428_add_status_info(char *s, int max_len, void *p)
{
    gd5428_t *gd5428 = (gd5428_t *)p;
    
    svga_add_status_info(s, max_len, &gd5428->svga);
}


device_t gd5428_device =
{
    "Cirrus Logic GD5428",
    DEVICE_VLB,
    CIRRUS_ID_CLGD5428,
    gd5428_init, 
    gd5428_close, 
    NULL,
    gd5428_available,
    gd5428_speed_changed,
    gd5428_force_redraw,
    gd5428_add_status_info,
    NULL
};
