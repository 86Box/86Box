/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of select Cirrus Logic cards (CL-GD 5428,
 *		CL-GD 5429, CL-GD 5430, CL-GD 5434 and CL-GD 5436 are supported).
 *
 * Version:	@(#)vid_cl_54xx.c	1.0.32	2020/01/11
 *
 * Authors:	TheCollector1995,
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2020 TheCollector1995.
 *		Copyright 2016-2020 Miran Grca.
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
#include "../pci.h"
#include "../rom.h"
#include "../device.h"
#include "../timer.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_svga_render.h"
#include "vid_cl54xx.h"

#if defined(DEV_BRANCH) && defined(USE_CL5422)
#define BIOS_GD5420_PATH		L"roms/video/cirruslogic/5420.vbi"
#define BIOS_GD5422_PATH		L"roms/video/cirruslogic/cl5422.bin"
#endif
#define BIOS_GD5426_PATH		L"roms/video/cirruslogic/Diamond SpeedStar PRO VLB v3.04.bin"
#define BIOS_GD5428_ISA_PATH		L"roms/video/cirruslogic/5428.bin"
#define BIOS_GD5428_PATH		L"roms/video/cirruslogic/vlbusjapan.BIN"
#define BIOS_GD5429_PATH		L"roms/video/cirruslogic/5429.vbi"
#define BIOS_GD5430_VLB_PATH		L"roms/video/cirruslogic/diamondvlbus.bin"
#define BIOS_GD5430_PCI_PATH		L"roms/video/cirruslogic/pci.bin"
#define BIOS_GD5434_PATH		L"roms/video/cirruslogic/gd5434.bin"
#define BIOS_GD5436_PATH		L"roms/video/cirruslogic/5436.vbi"
#define BIOS_GD5440_PATH		L"roms/video/cirruslogic/BIOS.BIN"
#define BIOS_GD5446_PATH		L"roms/video/cirruslogic/5446BV.VBI"
#define BIOS_GD5446_STB_PATH		L"roms/video/cirruslogic/stb nitro64v.BIN"
#define BIOS_GD5480_PATH		L"roms/video/cirruslogic/clgd5480.rom"

#define CIRRUS_ID_CLGD5402	  	0x89
#define CIRRUS_ID_CLGD5420	  	0x8a
#define CIRRUS_ID_CLGD5422  		0x8c
#define CIRRUS_ID_CLGD5424  		0x94
#define CIRRUS_ID_CLGD5426		0x90
#define CIRRUS_ID_CLGD5428		0x98
#define CIRRUS_ID_CLGD5429		0x9c
#define CIRRUS_ID_CLGD5430		0xa0
#define CIRRUS_ID_CLGD5434		0xa8
#define CIRRUS_ID_CLGD5436		0xac
#define CIRRUS_ID_CLGD5440		0xa0	/* Yes, the 5440 has the same ID as the 5430. */
#define CIRRUS_ID_CLGD5446		0xb8
#define CIRRUS_ID_CLGD5480		0xbc

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

// sequencer 0x17
#define CIRRUS_BUSTYPE_VLBFAST   0x10
#define CIRRUS_BUSTYPE_PCI       0x20
#define CIRRUS_BUSTYPE_VLBSLOW   0x30
#define CIRRUS_BUSTYPE_ISA       0x38
#define CIRRUS_MMIO_ENABLE       0x04
#define CIRRUS_MMIO_USE_PCIADDR  0x40	/* 0xb8000 if cleared. */
#define CIRRUS_MEMSIZEEXT_DOUBLE 0x80

// control 0x0b
#define CIRRUS_BANKING_DUAL             0x01
#define CIRRUS_BANKING_GRANULARITY_16K  0x20	/* set:16k, clear:4k */

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

// control 0x31
#define CIRRUS_BLT_BUSY                 0x01
#define CIRRUS_BLT_START                0x02
#define CIRRUS_BLT_RESET                0x04
#define CIRRUS_BLT_FIFOUSED             0x10
#define CIRRUS_BLT_PAUSED		0x20
#define CIRRUS_BLT_APERTURE2		0x40
#define CIRRUS_BLT_AUTOSTART            0x80

// control 0x33
#define CIRRUS_BLTMODEEXT_BACKGROUNDONLY   0x08
#define CIRRUS_BLTMODEEXT_SOLIDFILL        0x04
#define CIRRUS_BLTMODEEXT_COLOREXPINV      0x02
#define CIRRUS_BLTMODEEXT_DWORDGRANULARITY 0x01

#define CL_GD5429_SYSTEM_BUS_VESA 5
#define CL_GD5429_SYSTEM_BUS_ISA  7

#define CL_GD543X_SYSTEM_BUS_PCI  4
#define CL_GD543X_SYSTEM_BUS_VESA 6
#define CL_GD543X_SYSTEM_BUS_ISA  7

typedef struct gd54xx_t
{
    mem_mapping_t	mmio_mapping;
    mem_mapping_t 	linear_mapping;
    mem_mapping_t 	aperture2_mapping;

    svga_t		svga;

    int			has_bios, rev;
    rom_t		bios_rom;

    uint32_t		vram_size;
    uint32_t		vram_mask;

    uint8_t		vclk_n[4];
    uint8_t		vclk_d[4];        

    struct {
	uint8_t			state;
	int			ctrl;
    } ramdac;		
	
    struct {
	uint16_t		width, height;
	uint16_t		dst_pitch, src_pitch;               
	uint16_t		trans_col, trans_mask;
	uint16_t		height_internal;
	uint16_t		msd_buf_pos, msd_buf_cnt;

	uint8_t			status;
	uint8_t			mask, mode, rop, modeext;
	uint8_t			ms_is_dest, msd_buf[32];

	uint32_t		fg_col, bg_col;
	uint32_t		dst_addr_backup, src_addr_backup;
	uint32_t		dst_addr, src_addr;
	uint32_t		sys_src32, sys_cnt;

	/* Internal state */
	int			pixel_width, pattern_x;
	int			x_count, y_count;
	int			xx_count, dir;
	int			unlock_special;
    } blt;

    int			pci, vlb;
    int			countminusone;

    uint8_t		pci_regs[256];
    uint8_t		int_line, unlocked;

    uint8_t		fc;		/* Feature Connector */

    int			card;

    uint32_t		lfb_base;

    int			mmio_vram_overlap;

    uint32_t		extpallook[256];
    PALETTE		extpal;
} gd54xx_t;


static video_timings_t timing_gd54xx_isa	= {VIDEO_ISA, 3,  3,  6,   8,  8, 12};
static video_timings_t timing_gd54xx_vlb_pci	= {VIDEO_BUS, 4,  4,  8,  10, 10, 20};


static void 
gd543x_mmio_write(uint32_t addr, uint8_t val, void *p);
static void 
gd543x_mmio_writeb(uint32_t addr, uint8_t val, void *p);
static void 
gd543x_mmio_writew(uint32_t addr, uint16_t val, void *p);
static void 
gd543x_mmio_writel(uint32_t addr, uint32_t val, void *p);
static uint8_t
gd543x_mmio_read(uint32_t addr, void *p);
static uint16_t
gd543x_mmio_readw(uint32_t addr, void *p);
static uint32_t
gd543x_mmio_readl(uint32_t addr, void *p);

static void 
gd54xx_recalc_banking(gd54xx_t *gd54xx);

static void 
gd543x_recalc_mapping(gd54xx_t *gd54xx);

static void
gd54xx_reset_blit(gd54xx_t *gd54xx);
static void 
gd54xx_start_blit(uint32_t cpu_dat, uint32_t count, gd54xx_t *gd54xx, svga_t *svga);


/* Returns 1 if the card is a 5422+ */
static int
gd54xx_is_5422(svga_t *svga)
{
    if (svga->crtc[0x27] >= CIRRUS_ID_CLGD5422)
	return 1;
    else
	return 0;
}


/* Returns 1 if the card supports the 8-bpp/16-bpp transparency color or mask. */
static int
gd54xx_has_transp(svga_t *svga, int mask)
{
    if (((svga->crtc[0x27] == CIRRUS_ID_CLGD5446) || (svga->crtc[0x27] == CIRRUS_ID_CLGD5480)) &&
	!mask)
	return 1;	/* 5446 and 5480 have mask but not transparency. */
    if ((svga->crtc[0x27] == CIRRUS_ID_CLGD5426) || (svga->crtc[0x27] == CIRRUS_ID_CLGD5428))
	return 1;	/* 5426 and 5428 have both. */
    else
	return 0;	/* The rest have neither. */
}


/* Returns 1 if the card is a 5434, 5436/46, or 5480. */
static int
gd54xx_is_5434(svga_t *svga)
{
    if (svga->crtc[0x27] >= CIRRUS_ID_CLGD5434)
	return 1;
    else
	return 0;
}


static void
gd54xx_out(uint16_t addr, uint8_t val, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;
    uint8_t old;
    int c;
    uint8_t o, index;
    uint32_t o32;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1)) 
	addr ^= 0x60;

    switch (addr) {
	case 0x3c0:
	case 0x3c1:
		if (!svga->attrff) {
			svga->attraddr = val & 31;
			if ((val & 0x20) != svga->attr_palette_enable) {
				svga->fullchange = 3;
				svga->attr_palette_enable = val & 0x20;
				svga_recalctimings(svga);
			}
		} else {
			o = svga->attrregs[svga->attraddr & 31];
			svga->attrregs[svga->attraddr & 31] = val;
			if (svga->attraddr < 16) 
				svga->fullchange = changeframecount;
			if (svga->attraddr == 0x10 || svga->attraddr == 0x14 || svga->attraddr < 0x10) {
				for (c = 0; c < 16; c++) {
					if (svga->attrregs[0x10] & 0x80) svga->egapal[c] = (svga->attrregs[c] &  0xf) | ((svga->attrregs[0x14] & 0xf) << 4);
					else                             svga->egapal[c] = (svga->attrregs[c] & 0x3f) | ((svga->attrregs[0x14] & 0xc) << 4);
				}
			}
			/* Recalculate timings on change of attribute register 0x11 (overscan border color) too. */
			if (svga->attraddr == 0x10) {
				if (o != val)
					svga_recalctimings(svga);
			} else if (svga->attraddr == 0x11) {
				if (!(svga->seqregs[0x12] & 0x80)) {
					svga->overscan_color = svga->pallook[svga->attrregs[0x11]];
					if (o != val)  svga_recalctimings(svga);
				}
			} else if (svga->attraddr == 0x12) {
				if ((val & 0xf) != svga->plane_mask)
					svga->fullchange = changeframecount;
				svga->plane_mask = val & 0xf;
			}
		}
		svga->attrff ^= 1;
                return;
	case 0x3c4:
		svga->seqaddr = val;
		break;
	case 0x3c5:
		if ((svga->seqaddr == 2) && !gd54xx->unlocked) {
			o = svga->seqregs[svga->seqaddr & 0x1f];
			svga_out(addr, val, svga);
			svga->seqregs[svga->seqaddr & 0x1f] = (o & 0xf0) | (val & 0x0f);
			return;
		} else if ((svga->seqaddr > 6) && !gd54xx->unlocked)
			return;

		if (svga->seqaddr > 5) {
			o = svga->seqregs[svga->seqaddr & 0x1f];
			svga->seqregs[svga->seqaddr & 0x1f] = val;
			switch (svga->seqaddr) {
				case 6:
					val &= 0x17;
					if (val == 0x12)
						svga->seqregs[6] = 0x12;
					else
						svga->seqregs[6] = 0x0f;
					if (svga->crtc[0x27] < CIRRUS_ID_CLGD5429)
						gd54xx->unlocked = (svga->seqregs[6] == 0x12);
					break;
				case 0x0b: case 0x0c: case 0x0d: case 0x0e: /* VCLK stuff */
					gd54xx->vclk_n[svga->seqaddr-0x0b] = val;
					break;
				case 0x1b: case 0x1c: case 0x1d: case 0x1e: /* VCLK stuff */
					gd54xx->vclk_d[svga->seqaddr-0x1b] = val;
					break;
				case 0x10: case 0x30: case 0x50: case 0x70:
				case 0x90: case 0xb0: case 0xd0: case 0xf0:
					svga->hwcursor.x = (val << 3) | (svga->seqaddr >> 5);
					break;
				case 0x11: case 0x31: case 0x51: case 0x71:
				case 0x91: case 0xb1: case 0xd1: case 0xf1:
					svga->hwcursor.y = (val << 3) | (svga->seqaddr >> 5);
					break;
				case 0x12:
					svga->ext_overscan = !!(val & 0x80);
					if (svga->ext_overscan && (svga->crtc[0x27] >= CIRRUS_ID_CLGD5426))
						svga->overscan_color = gd54xx->extpallook[2];
					else
						svga->overscan_color = svga->pallook[svga->attrregs[0x11]];
					svga_recalctimings(svga);
					svga->hwcursor.ena = val & CIRRUS_CURSOR_SHOW;
					if (svga->crtc[0x27] >= CIRRUS_ID_CLGD5422)
						svga->hwcursor.xsize = svga->hwcursor.ysize =
								       ((val & CIRRUS_CURSOR_LARGE) && (svga->crtc[0x27] >= CIRRUS_ID_CLGD5422)) ? 64 : 32;
					else
						svga->hwcursor.xsize = 32;
					svga->hwcursor.yoff = (svga->hwcursor.ysize == 32) ? 32 : 0;

					if ((svga->seqregs[0x12] & CIRRUS_CURSOR_LARGE) && (svga->crtc[0x27] >= CIRRUS_ID_CLGD5422))
						svga->hwcursor.addr = ((gd54xx->vram_size - 0x4000) + ((svga->seqregs[0x13] & 0x3c) * 256));
					else
						svga->hwcursor.addr = ((gd54xx->vram_size - 0x4000) + ((svga->seqregs[0x13] & 0x3f) * 256));
					break;
				case 0x13:
					if ((svga->seqregs[0x12] & CIRRUS_CURSOR_LARGE) && (svga->crtc[0x27] >= CIRRUS_ID_CLGD5422))
						svga->hwcursor.addr = ((gd54xx->vram_size - 0x4000) + ((val & 0x3c) * 256));
					else
						svga->hwcursor.addr = ((gd54xx->vram_size - 0x4000) + ((val & 0x3f) * 256));
					break;
				case 0x07:
					if (gd54xx_is_5422(svga))
						gd543x_recalc_mapping(gd54xx);
					else
						svga->seqregs[svga->seqaddr] &= 0x0f;
					if (svga->crtc[0x27] >= CIRRUS_ID_CLGD5429)
						svga->set_reset_disabled = svga->seqregs[7] & 1;
				case 0x17:
					if (gd54xx_is_5422(svga))
						gd543x_recalc_mapping(gd54xx);
					else
						return;
					break;
			}
			return;
		}
		break;
	case 0x3c6:
		if (!gd54xx->unlocked)
			break;
		if (gd54xx->ramdac.state == 4) {
			gd54xx->ramdac.state = 0;
			gd54xx->ramdac.ctrl = val;
			svga_recalctimings(svga);
			return;
		}
		gd54xx->ramdac.state = 0;
		break;
	case 0x3c7: case 0x3c8:
		gd54xx->ramdac.state = 0;
		break;
	case 0x3c9:
		gd54xx->ramdac.state = 0;
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
				index = svga->dac_addr & 0xff;
				if (svga->seqregs[0x12] & 2) {
					index &= 0x0f;
					gd54xx->extpal[index].r = svga->dac_r;
					gd54xx->extpal[index].g = svga->dac_g;
					gd54xx->extpal[index].b = val; 
					gd54xx->extpallook[index] = makecol32(video_6to8[gd54xx->extpal[index].r & 0x3f], video_6to8[gd54xx->extpal[index].g & 0x3f], video_6to8[gd54xx->extpal[index].b & 0x3f]);
					if (svga->ext_overscan && (index == 2)) {
						o32 = svga->overscan_color;
						svga->overscan_color = gd54xx->extpallook[2];
						if (o32 != svga->overscan_color)
							svga_recalctimings(svga);
					}
				} else {
					svga->vgapal[index].r = svga->dac_r;
					svga->vgapal[index].g = svga->dac_g;
					svga->vgapal[index].b = val; 
					svga->pallook[index] = makecol32(video_6to8[svga->vgapal[index].r & 0x3f], video_6to8[svga->vgapal[index].g & 0x3f], video_6to8[svga->vgapal[index].b & 0x3f]);
				}
				svga->dac_addr = (svga->dac_addr + 1) & 255;
				svga->dac_pos = 0; 
                        break;
                }
                return;
	case 0x3ce:
		/* Per the CL-GD 5446 manual: bits 0-5 are the GDC register index, bits 6-7 are reserved. */
		svga->gdcaddr = val/* & 0x3f*/;
		return;
	case 0x3cf:
		if ((svga->gdcaddr > 0x1f) && ((svga->crtc[0x27] <= CIRRUS_ID_CLGD5422) ||
		    (svga->crtc[0x27] == CIRRUS_ID_CLGD5424)))
			return;

		o = svga->gdcreg[svga->gdcaddr];

		if ((svga->gdcaddr < 2) && !gd54xx->unlocked)
			svga->gdcreg[svga->gdcaddr] = (svga->gdcreg[svga->gdcaddr] & 0xf0) | (val & 0x0f);
		else if ((svga->gdcaddr <= 8) || gd54xx->unlocked)
			svga->gdcreg[svga->gdcaddr] = val;

		if (svga->gdcaddr <= 8) {
			switch (svga->gdcaddr) {
				case 0:	
					gd543x_mmio_write(0xb8000, val, gd54xx);
					break;
				case 1:
					gd543x_mmio_write(0xb8004, val, gd54xx);
					break;
				case 2:
					svga->colourcompare = val;
					break;
				case 4:
					svga->readplane = val & 3;
					break;
				case 5:
					if (svga->gdcreg[0xb] & 0x04)
						svga->writemode = val & 7;
					else
						svga->writemode = val & 3;
					svga->readmode = val & 8;
					svga->chain2_read = val & 0x10;
					break;
				case 6:
					if ((o ^ val) & 0x0c)
						gd543x_recalc_mapping(gd54xx);
					break;
				case 7:
					svga->colournocare = val;
					break;
			}

			svga->fast = (svga->gdcreg[8] == 0xff && !(svga->gdcreg[3] & 0x18) &&
				     !svga->gdcreg[1]) && svga->chain4;
			if (((svga->gdcaddr == 5) && ((val ^ o) & 0x70)) ||
			    ((svga->gdcaddr == 6) && ((val ^ o) & 1)))
				svga_recalctimings(svga);
		} else {
			switch (svga->gdcaddr) {
				case 0x09: case 0x0a: case 0x0b:
					if (svga->gdcreg[0xb] & 0x04)
						svga->writemode = svga->gdcreg[5] & 7;
					else
						svga->writemode = svga->gdcreg[5] & 3;
					svga->adv_flags = 0;
					if (svga->gdcreg[0xb] & 0x01)
						svga->adv_flags = FLAG_EXTRA_BANKS;
					if (svga->gdcreg[0xb] & 0x02)
						svga->adv_flags |= FLAG_ADDR_BY8;
					if (svga->gdcreg[0xb] & 0x08)
						svga->adv_flags |= FLAG_LATCH8;
					gd54xx_recalc_banking(gd54xx);
					break;

				case 0x10:
					gd543x_mmio_write(0xb8001, val, gd54xx);
					break;
				case 0x11:
					gd543x_mmio_write(0xb8005, val, gd54xx);
					break;
				case 0x12:
					gd543x_mmio_write(0xb8002, val, gd54xx);
					break;
				case 0x13:
					gd543x_mmio_write(0xb8006, val, gd54xx);
					break;
				case 0x14:
					gd543x_mmio_write(0xb8003, val, gd54xx);
					break;
				case 0x15:
					gd543x_mmio_write(0xb8007, val, gd54xx);
					break;

				case 0x20:
					gd543x_mmio_write(0xb8008, val, gd54xx);
					break;
				case 0x21:
					gd543x_mmio_write(0xb8009, val, gd54xx);
					break;
				case 0x22:
					gd543x_mmio_write(0xb800a, val, gd54xx);
					break;
				case 0x23:
					gd543x_mmio_write(0xb800b, val, gd54xx);
					break;
				case 0x24:
					gd543x_mmio_write(0xb800c, val, gd54xx);
					break;
				case 0x25:
					gd543x_mmio_write(0xb800d, val, gd54xx);
					break;
				case 0x26:
					gd543x_mmio_write(0xb800e, val, gd54xx);
					break;
				case 0x27:
					gd543x_mmio_write(0xb800f, val, gd54xx);
					break;

				case 0x28:
					gd543x_mmio_write(0xb8010, val, gd54xx);
					break;
				case 0x29:
					gd543x_mmio_write(0xb8011, val, gd54xx);
					break;
				case 0x2a:
					gd543x_mmio_write(0xb8012, val, gd54xx);
					break;

				case 0x2c:
					gd543x_mmio_write(0xb8014, val, gd54xx);
					break;
				case 0x2d:
					gd543x_mmio_write(0xb8015, val, gd54xx);
					break;
				case 0x2e:
					gd543x_mmio_write(0xb8016, val, gd54xx);
					break;

				case 0x2f:
					gd543x_mmio_write(0xb8017, val, gd54xx);
					break;
				case 0x30:
					gd543x_mmio_write(0xb8018, val, gd54xx);
					break;

				case 0x32:
					gd543x_mmio_write(0xb801a, val, gd54xx);
					break;

				case 0x33:
					gd543x_mmio_write(0xb801b, val, gd54xx);
					break;

				case 0x31:
					gd543x_mmio_write(0xb8040, val, gd54xx);
					break;

				case 0x34:
					gd543x_mmio_write(0xb801c, val, gd54xx);
					break;

				case 0x35:
					gd543x_mmio_write(0xb801d, val, gd54xx);
					break;

				case 0x38:
					gd543x_mmio_write(0xb8020, val, gd54xx);
					break;

				case 0x39:
					gd543x_mmio_write(0xb8021, val, gd54xx);
					break;
			}
		}
		return;
	case 0x3d4:
		svga->crtcreg = val & 0x3f;
		return;
	case 0x3d5:
		if (((svga->crtcreg == 0x19) || (svga->crtcreg == 0x1a) ||
		     (svga->crtcreg == 0x1b) || (svga->crtcreg == 0x1d) ||
		     (svga->crtcreg == 0x25) || (svga->crtcreg == 0x27)) &&
		    !gd54xx->unlocked)
			return;
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

    uint8_t index, ret = 0xff;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1)) 
	addr ^= 0x60;

    switch (addr) {
	case 0x3c4:
		if (svga->seqregs[6] == 0x12) {
			ret = svga->seqaddr;
			if ((ret & 0x1e) == 0x10) {
				if (ret & 1)
					ret = ((svga->hwcursor.y & 7) << 5) | 0x11;
				else
					ret = ((svga->hwcursor.x & 7) << 5) | 0x10;
			}
		} else
			ret = svga->seqaddr;
		break;

	case 0x3c5:
		if ((svga->seqaddr == 2) && !gd54xx->unlocked)
			ret = svga_in(addr, svga) & 0x0f;
		else if ((svga->seqaddr > 6) && !gd54xx->unlocked)
			ret = 0xff;
		else if (svga->seqaddr > 5) {
			ret = svga->seqregs[svga->seqaddr & 0x3f];
			switch (svga->seqaddr) {
				case 6:
					ret = svga->seqregs[6];
					break;
				case 0x0b: case 0x0c: case 0x0d: case 0x0e:
					ret = gd54xx->vclk_n[svga->seqaddr-0x0b];
					break;
				case 0x17:
					ret = svga->gdcreg[0x17] & ~(7 << 3);
					if (svga->crtc[0x27] <= CIRRUS_ID_CLGD5429) {
						if (gd54xx->vlb)
							ret |= (CL_GD5429_SYSTEM_BUS_VESA << 3);
						else
							ret |= (CL_GD5429_SYSTEM_BUS_ISA << 3);
					} else {
						if (gd54xx->pci)
							ret |= (CL_GD543X_SYSTEM_BUS_PCI << 3);
						else if (gd54xx->vlb)
							ret |= (CL_GD543X_SYSTEM_BUS_VESA << 3);
						else
							ret |= (CL_GD543X_SYSTEM_BUS_ISA << 3);
					}
					break;
				case 0x18:
					ret = svga->seqregs[0x18] & 0xfe;
					break;
				case 0x1b: case 0x1c: case 0x1d: case 0x1e:
					ret = gd54xx->vclk_d[svga->seqaddr - 0x1b];
					break;
			}
			break;
		} else
			ret = svga_in(addr, svga);
		break;
	case 0x3c6:
		if (!gd54xx->unlocked)
			ret = svga_in(addr, svga);
		else if (gd54xx->ramdac.state == 4) {
			/* CL-GD 5428 does not lock the register when it's read. */
			if (svga->crtc[0x27] != CIRRUS_ID_CLGD5428)
				gd54xx->ramdac.state = 0;
			ret = gd54xx->ramdac.ctrl;
		} else {
			gd54xx->ramdac.state++;
			if (gd54xx->ramdac.state == 4)
				ret = gd54xx->ramdac.ctrl;
			else
				ret = svga_in(addr, svga);
		}
		break;
	case 0x3c7: case 0x3c8:
		gd54xx->ramdac.state = 0;
		ret = svga_in(addr, svga);
		break;
	case 0x3c9:
		gd54xx->ramdac.state = 0;
		svga->dac_status = 3;
		index = (svga->dac_addr - 1) & 0xff;
		if (svga->seqregs[0x12] & 2)
			index &= 0x0f;
		switch (svga->dac_pos) {
			case 0:
				svga->dac_pos++;
				if (svga->seqregs[0x12] & 2)
					ret = gd54xx->extpal[index].r & 0x3f;
				else
					ret = svga->vgapal[index].r & 0x3f;
				break;
			case 1: 
				svga->dac_pos++;
				if (svga->seqregs[0x12] & 2)
					ret = gd54xx->extpal[index].g & 0x3f;
				else
					ret = svga->vgapal[index].g & 0x3f;
				break;
                        case 2: 
				svga->dac_pos=0;
				svga->dac_addr = (svga->dac_addr + 1) & 255;
				if (svga->seqregs[0x12] & 2)
                        		ret = gd54xx->extpal[index].b & 0x3f;
				else
                        		ret = svga->vgapal[index].b & 0x3f;
				break;
                }
                break;
	case 0x3ce:
		ret = svga->gdcaddr & 0x3f;
		break;
	case 0x3cf:
		if (svga->gdcaddr >= 0x10) {
			if ((svga->gdcaddr > 8) && !gd54xx->unlocked)
				ret = 0xff;
			else if ((svga->gdcaddr > 0x1f) && ((svga->crtc[0x27] <= CIRRUS_ID_CLGD5422) ||
				 (svga->crtc[0x27] == CIRRUS_ID_CLGD5424)))
				ret = 0xff;
			else switch (svga->gdcaddr) {
				case 0x10:
					ret = gd543x_mmio_read(0xb8001, gd54xx);
					break;
				case 0x11:
					ret = gd543x_mmio_read(0xb8005, gd54xx);
					break;
				case 0x12:
					ret = gd543x_mmio_read(0xb8002, gd54xx);
					break;
				case 0x13:
					ret = gd543x_mmio_read(0xb8006, gd54xx);
					break;
				case 0x14:
					ret = gd543x_mmio_read(0xb8003, gd54xx);
					break;
				case 0x15:
					ret = gd543x_mmio_read(0xb8007, gd54xx);
					break;

				case 0x20:
					ret = gd543x_mmio_read(0xb8008, gd54xx);
					break;
				case 0x21:
					ret = gd543x_mmio_read(0xb8009, gd54xx);
					break;
				case 0x22:
					ret = gd543x_mmio_read(0xb800a, gd54xx);
					break;
				case 0x23:
					ret = gd543x_mmio_read(0xb800b, gd54xx);
					break;
				case 0x24:
					ret = gd543x_mmio_read(0xb800c, gd54xx);
					break;
				case 0x25:
					ret = gd543x_mmio_read(0xb800d, gd54xx);
					break;
				case 0x26:
					ret = gd543x_mmio_read(0xb800e, gd54xx);
					break;
				case 0x27:
					ret = gd543x_mmio_read(0xb800f, gd54xx);
					break;

				case 0x28:
					ret = gd543x_mmio_read(0xb8010, gd54xx);
					break;
				case 0x29:
					ret = gd543x_mmio_read(0xb8011, gd54xx);
					break;
				case 0x2a:
					ret = gd543x_mmio_read(0xb8012, gd54xx);
					break;

				case 0x2c:
					ret = gd543x_mmio_read(0xb8014, gd54xx);
					break;
				case 0x2d:
					ret = gd543x_mmio_read(0xb8015, gd54xx);
					break;
				case 0x2e:
					ret = gd543x_mmio_read(0xb8016, gd54xx);
					break;

				case 0x2f:
					ret = gd543x_mmio_read(0xb8017, gd54xx);
					break;
				case 0x30:
					ret = gd543x_mmio_read(0xb8018, gd54xx);
					break;

				case 0x32:
					ret = gd543x_mmio_read(0xb801a, gd54xx);
					break;

				case 0x33:
					ret = gd543x_mmio_read(0xb801b, gd54xx);
					break;

				case 0x31:
					ret = gd543x_mmio_read(0xb8040, gd54xx);
					break;

				case 0x34:
					ret = gd543x_mmio_read(0xb801c, gd54xx);
					break;

				case 0x35:
					ret = gd543x_mmio_read(0xb801d, gd54xx);
					break;

				case 0x38:
					ret = gd543x_mmio_read(0xb8020, gd54xx);
					break;

				case 0x39:
					ret = gd543x_mmio_read(0xb8021, gd54xx);
					break;
			}
		} else {
			if ((svga->gdcaddr < 2) && !gd54xx->unlocked)
				ret = (svga->gdcreg[svga->gdcaddr] & 0x0f);
			else
				ret = svga->gdcreg[svga->gdcaddr];
		}
		break;
	case 0x3d4:
		ret = svga->crtcreg;
		break;
	case 0x3d5:
		ret = svga->crtc[svga->crtcreg];
		if (((svga->crtcreg == 0x19) || (svga->crtcreg == 0x1a) ||
		     (svga->crtcreg == 0x1b) || (svga->crtcreg == 0x1d) ||
		     (svga->crtcreg == 0x25) || (svga->crtcreg == 0x27)) &&
		    !gd54xx->unlocked)
			ret = 0xff;
		else switch (svga->crtcreg) {
			case 0x22: /*Graphis Data Latches Readback Register*/
				/*Should this be & 7 if 8 byte latch is enabled? */
				ret = svga->latch.b[svga->gdcreg[4] & 3];
				break;
			case 0x24: /*Attribute controller toggle readback (R)*/
				ret = svga->attrff << 7;
				break;
			case 0x26: /*Attribute controller index readback (R)*/
				ret = svga->attraddr & 0x3f;	
				break;
			case 0x27: /*ID*/
				ret = svga->crtc[0x27]; /*GD542x/GD543x*/
				break;
			case 0x28: /*Class ID*/
				if ((svga->crtc[0x27] == CIRRUS_ID_CLGD5430) || (svga->crtc[0x27] == CIRRUS_ID_CLGD5440))
					ret = 0xff; /*Standard CL-GD5430/40*/
				break;
		}
		break;
	default:
		ret = svga_in(addr, svga);
		break;
    }

    return ret;
}


static void
gd54xx_recalc_banking(gd54xx_t *gd54xx)
{
    svga_t *svga = &gd54xx->svga;

    if (!gd54xx_is_5422(svga)) {
	svga->extra_banks[0] = (svga->gdcreg[0x09] & 0x7f) << 12;

	if (svga->gdcreg[0x0b] & CIRRUS_BANKING_DUAL)
		svga->extra_banks[1] = (svga->gdcreg[0x0a] & 0x7f) << 12;
	else
		svga->extra_banks[1] = svga->extra_banks[0] + 0x8000;
    } else {
	if ((svga->gdcreg[0x0b] & CIRRUS_BANKING_GRANULARITY_16K) &&
	    (svga->crtc[0x27] >= CIRRUS_ID_CLGD5426) && (svga->crtc[0x27] != CIRRUS_ID_CLGD5424))
		svga->extra_banks[0] = svga->gdcreg[0x09] << 14;
	else
		svga->extra_banks[0] = svga->gdcreg[0x09] << 12;

	if (svga->gdcreg[0x0b] & CIRRUS_BANKING_DUAL) {
		if ((svga->gdcreg[0x0b] & CIRRUS_BANKING_GRANULARITY_16K) &&
		    (svga->crtc[0x27] >= CIRRUS_ID_CLGD5426) && (svga->crtc[0x27] != CIRRUS_ID_CLGD5424))
			svga->extra_banks[1] = svga->gdcreg[0x0a] << 14;
		else
			svga->extra_banks[1] = svga->gdcreg[0x0a] << 12;
	} else
		svga->extra_banks[1] = svga->extra_banks[0] + 0x8000;
    }

    svga->write_bank = svga->read_bank = svga->extra_banks[0];
}


static void 
gd543x_recalc_mapping(gd54xx_t *gd54xx)
{
    svga_t *svga = &gd54xx->svga;
    uint32_t base, size;
        
    if (!(gd54xx->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM)) {
	mem_mapping_disable(&svga->mapping);
	mem_mapping_disable(&gd54xx->linear_mapping);
	mem_mapping_disable(&gd54xx->mmio_mapping);
	return;
    }
	
    gd54xx->mmio_vram_overlap = 0;

    if (!gd54xx_is_5422(svga) || !(svga->seqregs[7] & 0xf0) || !(svga->seqregs[0x07] & 0x01)) {
	mem_mapping_disable(&gd54xx->linear_mapping);
	mem_mapping_disable(&gd54xx->aperture2_mapping);
	switch (svga->gdcreg[6] & 0x0c) {
		case 0x0: /*128k at A0000*/
			mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x20000);
			svga->banked_mask = 0xffff;
			break;
		case 0x4: /*64k at A0000*/
			mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
			svga->banked_mask = 0xffff;
			break;
		case 0x8: /*32k at B0000*/
			mem_mapping_set_addr(&svga->mapping, 0xb0000, 0x08000);
			svga->banked_mask = 0x7fff;
			break;
		case 0xC: /*32k at B8000*/
			mem_mapping_set_addr(&svga->mapping, 0xb8000, 0x08000);
			svga->banked_mask = 0x7fff;
			gd54xx->mmio_vram_overlap = 1;
			break;
	}

	if ((svga->seqregs[0x17] & CIRRUS_MMIO_ENABLE) && (svga->seqregs[0x07] & 0x01) &&
	    (svga->crtc[0x27] >= CIRRUS_ID_CLGD5426) && (svga->crtc[0x27] != CIRRUS_ID_CLGD5424)) {
		if (gd54xx->mmio_vram_overlap) {
			mem_mapping_disable(&svga->mapping);
			mem_mapping_set_addr(&gd54xx->mmio_mapping, 0xb8000, 0x08000);
		} else
			mem_mapping_set_addr(&gd54xx->mmio_mapping, 0xb8000, 0x00100);
	} else
		mem_mapping_disable(&gd54xx->mmio_mapping);
    } else {
	if ((svga->crtc[0x27] <= CIRRUS_ID_CLGD5429) || (!gd54xx->pci && !gd54xx->vlb)) {
		if (svga->gdcreg[0x0b] & CIRRUS_BANKING_GRANULARITY_16K) {
			base = (svga->seqregs[7] & 0xf0) << 16;
			size = 1 * 1024 * 1024;
		} else {
			base = (svga->seqregs[7] & 0xe0) << 16;
			size = 2 * 1024 * 1024;
		}
	} else if (gd54xx->pci) {
		base = gd54xx->lfb_base;
		if (svga->crtc[0x27] >= CIRRUS_ID_CLGD5436)
			size = 16 * 1024 * 1024;
		else
			size = 4 * 1024 * 1024;
	} else { /*VLB*/
		base = 128*1024*1024;
		if (svga->crtc[0x27] >= CIRRUS_ID_CLGD5436)
			size = 16 * 1024 * 1024;
		else
			size = 4 * 1024 * 1024;
	}

	mem_mapping_disable(&svga->mapping);
	mem_mapping_set_addr(&gd54xx->linear_mapping, base, size);
	if ((svga->seqregs[0x17] & CIRRUS_MMIO_ENABLE) && (svga->crtc[0x27] >= CIRRUS_ID_CLGD5426) &&
	    (svga->crtc[0x27] != CIRRUS_ID_CLGD5424)) {
		if (svga->seqregs[0x17] & CIRRUS_MMIO_USE_PCIADDR)
			mem_mapping_disable(&gd54xx->mmio_mapping); /* MMIO is handled in the linear read/write functions */
		else
			mem_mapping_set_addr(&gd54xx->mmio_mapping, 0xb8000, 0x00100);
	} else
		mem_mapping_disable(&gd54xx->mmio_mapping);

	if ((svga->crtc[0x27] >= CIRRUS_ID_CLGD5436) && (gd54xx->blt.status & CIRRUS_BLT_APERTURE2) &&
	    ((gd54xx->blt.mode & (CIRRUS_BLTMODE_COLOREXPAND | CIRRUS_BLTMODE_MEMSYSSRC)) ==
	    (CIRRUS_BLTMODE_COLOREXPAND | CIRRUS_BLTMODE_MEMSYSSRC)))
		mem_mapping_set_addr(&gd54xx->aperture2_mapping, 0xbc000, 0x04000);
	else
		mem_mapping_disable(&gd54xx->aperture2_mapping);
    }
}


static void
gd54xx_recalctimings(svga_t *svga)
{
    gd54xx_t *gd54xx = (gd54xx_t *)svga->p;	
    uint8_t clocksel, rdmask;

    svga->rowoffset = (svga->crtc[0x13]) | ((svga->crtc[0x1b] & 0x10) << 4);

    svga->interlace = (svga->crtc[0x1a] & 0x01);

    svga->map8 = svga->pallook;
    if (svga->seqregs[7] & CIRRUS_SR7_BPP_SVGA)
	svga->render = svga_render_8bpp_highres;
    else if (svga->gdcreg[5] & 0x40)
	svga->render = svga_render_8bpp_lowres;

    svga->ma_latch |= ((svga->crtc[0x1b] & 0x01) << 16) | ((svga->crtc[0x1b] & 0xc) << 15);

    svga->bpp = 8;

    if (gd54xx->ramdac.ctrl & 0x80)  {
	if (gd54xx->ramdac.ctrl & 0x40) {
		if ((svga->crtc[0x27] >= CIRRUS_ID_CLGD5428) || (svga->crtc[0x27] == CIRRUS_ID_CLGD5426))
			rdmask = 0xf;
		else
			rdmask = 0x7;

		switch (gd54xx->ramdac.ctrl & rdmask) {
			case 0:
				svga->bpp = 15;
				if (gd54xx->ramdac.ctrl & 0x10)
					svga->render = svga_render_15bpp_mix_highres;
				else
					svga->render = svga_render_15bpp_highres;
				break;

			case 1:
				svga->bpp = 16;
				svga->render = svga_render_16bpp_highres;
				break;

			case 5:
				if (gd54xx_is_5434(svga) && (svga->seqregs[7] & CIRRUS_SR7_BPP_32)) {
					svga->bpp = 32;
					svga->render = svga_render_32bpp_highres;
					if (svga->crtc[0x27] < CIRRUS_ID_CLGD5436)
						svga->rowoffset *= 2;
				} else {
					svga->bpp = 24;
					svga->render = svga_render_24bpp_highres;
				}
				break;

			case 8:
				svga->bpp = 8;
				svga->map8 = video_8togs;
				svga->render = svga_render_8bpp_highres;
				break;

			case 9:
				svga->bpp = 8;
				svga->map8 = video_8to32;
				svga->render = svga_render_8bpp_highres;
				break;
		}
	} else {
		svga->bpp = 15;
		if (gd54xx->ramdac.ctrl & 0x10)
			svga->render = svga_render_15bpp_mix_highres;
		else
			svga->render = svga_render_15bpp_highres;
	}
    }

    clocksel = (svga->miscout >> 2) & 3;

    if (!gd54xx->vclk_n[clocksel] || !gd54xx->vclk_d[clocksel])
	svga->clock = (cpuclock * (float)(1ull << 32)) / ((svga->miscout & 0xc) ? 28322000.0 : 25175000.0);
    else {
	int n = gd54xx->vclk_n[clocksel] & 0x7f;
	int d = (gd54xx->vclk_d[clocksel] & 0x3e) >> 1;
	int m = gd54xx->vclk_d[clocksel] & 0x01 ? 2 : 1;
	float freq = (14318184.0 * ((float)n / ((float)d * m)));
	if (gd54xx_is_5422(svga)) {
		switch (svga->seqregs[7] & (gd54xx_is_5434(svga) ? 0xe : 6)) {
			case 2:
				freq /= 2.0;
				break;
			case 4:
				if (!gd54xx_is_5434(svga))
					freq /= 3.0; 
				break;
		}
	}
	svga->clock = (cpuclock * (double)(1ull << 32)) / freq;
    }

    svga->vram_display_mask = (svga->crtc[0x1b] & 2) ? gd54xx->vram_mask : 0x3ffff;
}


static
void gd54xx_hwcursor_draw(svga_t *svga, int displine)
{
    gd54xx_t *gd54xx = (gd54xx_t *)svga->p;	
    int x, xx, comb, b0, b1;
    uint8_t dat[2];
    int offset = svga->hwcursor_latch.x - svga->hwcursor_latch.xoff;
    int pitch = (svga->hwcursor.xsize == 64) ? 16 : 4;
    uint32_t bgcol = gd54xx->extpallook[0x00];
    uint32_t fgcol = gd54xx->extpallook[0x0f];

    if (svga->interlace && svga->hwcursor_oddeven)
	svga->hwcursor_latch.addr += pitch;

    for (x = 0; x < svga->hwcursor.xsize; x += 8) {
	dat[0] = svga->vram[svga->hwcursor_latch.addr];
	if (svga->hwcursor.xsize == 64)
		dat[1] = svga->vram[svga->hwcursor_latch.addr + 0x08];
	else
		dat[1] = svga->vram[svga->hwcursor_latch.addr + 0x80];
	for (xx = 0; xx < 8; xx++) {
		b0 = (dat[0] >> (7 - xx)) & 1;
		b1 = (dat[1] >> (7 - xx)) & 1;
		comb = (b1 | (b0 << 1));
		if (offset >= svga->hwcursor_latch.x) {
			switch(comb) {
				case 0:
					/* The original screen pixel is shown (invisible cursor) */
					break;
				case 1:
					/* The pixel is shown in the cursor background color */
					((uint32_t *)buffer32->line[displine])[offset + svga->x_add] = bgcol;
					break;
				case 2:
					/* The pixel is shown as the inverse of the original screen pixel
					   (XOR cursor) */
					((uint32_t *)buffer32->line[displine])[offset + svga->x_add] ^= 0xffffff;
					break;
				case 3:
					/* The pixel is shown in the cursor foreground color */
					((uint32_t *)buffer32->line[displine])[offset + svga->x_add] = fgcol;
					break;
			}
		}
		   
		offset++;
	}
	svga->hwcursor_latch.addr++;
    }

    if (svga->hwcursor.xsize == 64)
	svga->hwcursor_latch.addr += 8;

    if (svga->interlace && !svga->hwcursor_oddeven)
	svga->hwcursor_latch.addr += pitch;
}


static void
gd54xx_rop(gd54xx_t *gd54xx, uint8_t *res, uint8_t *dst, const uint8_t *src) {
    switch (gd54xx->blt.rop) {
	case 0x00: *res =   0x00;          break;
	case 0x05: *res =   *src &  *dst;  break;
	case 0x06: *res =   *dst;          break;
	case 0x09: *res =   *src & ~*dst;  break;
	case 0x0b: *res =  ~*dst;          break;
	case 0x0d: *res =   *src;          break;
	case 0x0e: *res =   0xff;          break;
	case 0x50: *res =  ~*src &  *dst;  break;
	case 0x59: *res =   *src ^  *dst;  break;
	case 0x6d: *res =   *src |  *dst;  break;
	case 0x90: *res = ~(*src |  *dst); break;
	case 0x95: *res = ~(*src ^  *dst); break;
	case 0xad: *res =   *src | ~*dst;  break;
	case 0xd0: *res =  ~*src;          break;
	case 0xd6: *res =  ~*src |  *dst;  break;
	case 0xda: *res = ~(*src &  *dst); break;
    }
}


static uint8_t
gd54xx_mem_sys_dest_read(gd54xx_t *gd54xx)
{
    uint8_t ret = 0xff;

    if (gd54xx->blt.msd_buf_cnt != 0) {
	ret = gd54xx->blt.msd_buf[gd54xx->blt.msd_buf_pos++];
	gd54xx->blt.msd_buf_cnt--;

	if (gd54xx->blt.msd_buf_cnt == 0) {
		if (gd54xx->countminusone == 1) {
			gd54xx->blt.msd_buf_pos = 0;
			if ((gd54xx->blt.mode & CIRRUS_BLTMODE_COLOREXPAND) &&
			    !(gd54xx->blt.modeext & CIRRUS_BLTMODEEXT_DWORDGRANULARITY))
				gd54xx_start_blit(0xff, 8, gd54xx, &gd54xx->svga);
			else
				gd54xx_start_blit(0xffffffff, 32, gd54xx, &gd54xx->svga);
		} else
			gd54xx_reset_blit(gd54xx);	/* End of blit, do no more. */
	}
    }

    return ret;
}


static void
gd54xx_mem_sys_src_write(gd54xx_t *gd54xx, uint8_t val)
{
    int i;

    gd54xx->blt.sys_src32 &= ~(0xff << (gd54xx->blt.sys_cnt << 3));
    gd54xx->blt.sys_src32 |= (val << (gd54xx->blt.sys_cnt << 3));
    gd54xx->blt.sys_cnt = (gd54xx->blt.sys_cnt + 1) & 3;

    if (gd54xx->blt.sys_cnt == 0) {
	if ((gd54xx->blt.mode & CIRRUS_BLTMODE_COLOREXPAND) &&
	    !(gd54xx->blt.modeext & CIRRUS_BLTMODEEXT_DWORDGRANULARITY)) {
		for (i = 0; i < 32; i += 8)
			gd54xx_start_blit((gd54xx->blt.sys_src32 >> i) & 0xff, 8, gd54xx, &gd54xx->svga);
	} else
		gd54xx_start_blit(gd54xx->blt.sys_src32, 32, gd54xx, &gd54xx->svga);
    }
}


static void
gd54xx_write(uint32_t addr, uint8_t val, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;	

    if (gd54xx->countminusone && !gd54xx->blt.ms_is_dest &&
	!(gd54xx->blt.status & CIRRUS_BLT_PAUSED)) {
	gd54xx_mem_sys_src_write(gd54xx, val);
	return;
    }

    if ((svga->seqregs[0x07] & 0x01) == 0) {
	svga_write(addr, val, svga);
	return;
    }

    addr = (addr & 0x7fff) + svga->extra_banks[(addr >> 15) & 1];

    svga_write_linear(addr, val, svga);
}


static void 
gd54xx_writew(uint32_t addr, uint16_t val, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;

    if (gd54xx->countminusone && !gd54xx->blt.ms_is_dest &&
	!(gd54xx->blt.status & CIRRUS_BLT_PAUSED)) {
	gd54xx_write(addr, val, gd54xx);
	gd54xx_write(addr + 1, val >> 8, gd54xx);
	return;
    }

    if ((svga->seqregs[0x07] & 0x01) == 0) {
	svga_writew(addr, val, svga);
	return;
    }

    addr = (addr & 0x7fff) + svga->extra_banks[(addr >> 15) & 1];

    if (svga->writemode < 4)	
    	svga_writew_linear(addr, val, svga);
    else {
	svga_write_linear(addr, val, svga);
	svga_write_linear(addr + 1, val >> 8, svga);
    }
}


static void
gd54xx_writel(uint32_t addr, uint32_t val, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;

    if (gd54xx->countminusone && !gd54xx->blt.ms_is_dest &&
	!(gd54xx->blt.status & CIRRUS_BLT_PAUSED)) {
	gd54xx_write(addr, val, gd54xx);
	gd54xx_write(addr + 1, val >> 8, gd54xx);
	gd54xx_write(addr + 2, val >> 16, gd54xx);
	gd54xx_write(addr + 3, val >> 24, gd54xx);
	return;
    }

    if ((svga->seqregs[0x07] & 0x01) == 0) {
	svga_writel(addr, val, svga);
	return;
    }

    addr = (addr & 0x7fff) + svga->extra_banks[(addr >> 15) & 1];

    if (svga->writemode < 4)
	svga_writel_linear(addr, val, svga);
    else {
	svga_write_linear(addr, val, svga);
	svga_write_linear(addr+1, val >> 8, svga);
	svga_write_linear(addr+2, val >> 16, svga);
	svga_write_linear(addr+3, val >> 24, svga);
    }
}


/* This adds write modes 4 and 5 to SVGA. */
static void
gd54xx_write_modes45(svga_t *svga, uint8_t val, uint32_t addr)
{
    uint32_t i, j;

    switch (svga->writemode) {
	case 4:
		if (svga->gdcreg[0xb] & 0x10) {
			addr <<= 2;

			for (i = 0; i < 8; i++) {
				if (val & svga->seqregs[2] & (0x80 >> i)) {
					svga->vram[addr + (i << 1)] = svga->gdcreg[1];
					svga->vram[addr + (i << 1) + 1] = svga->gdcreg[0x11];
				}
			}
		} else {
			addr <<= 1;

			for (i = 0; i < 8; i++) {
				if (val & svga->seqregs[2] & (0x80 >> i))
					svga->vram[addr + i] = svga->gdcreg[1];
			}
		}
		break;

	case 5:
		if (svga->gdcreg[0xb] & 0x10) {
			addr <<= 2;

			for (i = 0; i < 8; i++) {
				j = (0x80 >> i);
				if (svga->seqregs[2] & j) {
					svga->vram[addr + (i << 1)] = (val & j) ?
								      svga->gdcreg[1] : svga->gdcreg[0];
					svga->vram[addr + (i << 1) + 1] = (val & j) ?
									  svga->gdcreg[0x11] : svga->gdcreg[0x10];
				}
			}
		} else {
			addr <<= 1;

			for (i = 0; i < 8; i++) {
				j = (0x80 >> i);
				if (svga->seqregs[2] & j)
					svga->vram[addr + i] = (val & j) ? svga->gdcreg[1] : svga->gdcreg[0];
			}
		}
		break;
    }

    svga->changedvram[addr >> 12] = changeframecount;
}


static uint8_t
gd54xx_get_aperture(uint32_t addr)
{
    uint32_t ap = addr >> 22;
    return (uint8_t) (ap & 0x03);
}


static int
gd54xx_aperture2_enabled(gd54xx_t *gd54xx)
{
    svga_t *svga = &gd54xx->svga;

    if (svga->crtc[0x27] < CIRRUS_ID_CLGD5436)
	return 0;

    if (!(gd54xx->blt.mode & CIRRUS_BLTMODE_COLOREXPAND))
	return 0;

    if (!(gd54xx->blt.status & CIRRUS_BLT_APERTURE2))
	return 0;

    return 1;
}


static uint8_t
gd54xx_readb_linear(uint32_t addr, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;

    uint8_t ap = gd54xx_get_aperture(addr);
    addr &= 0x003fffff;	/* 4 MB mask */

    if ((svga->seqregs[0x07] & 0x01) == 0)
	return svga_read_linear(addr, svga);

    if ((addr >= (svga->vram_max - 256)) && (addr < svga->vram_max)) {
	if ((svga->seqregs[0x17] & CIRRUS_MMIO_ENABLE) && (svga->seqregs[0x17] & CIRRUS_MMIO_USE_PCIADDR))
		return gd543x_mmio_read(addr & 0x000000ff, gd54xx);
    }

    /* Do mem sys dest reads here if the blitter is neither paused, nor is there a second aperture. */
    if (gd54xx->countminusone && gd54xx->blt.ms_is_dest &&
	!gd54xx_aperture2_enabled(gd54xx) && !(gd54xx->blt.status & CIRRUS_BLT_PAUSED))
	return gd54xx_mem_sys_dest_read(gd54xx);

    switch (ap) {
	case 0:
	default:		
		break;
	case 1:
		/* 0 -> 1, 1 -> 0, 2 -> 3, 3 -> 2 */
		addr ^= 0x00000001;
		break;
	case 2:
		/* 0 -> 3, 1 -> 2, 2 -> 1, 3 -> 0 */
		addr ^= 0x00000003;
		break;
	case 3:
		return 0xff;
    }

    return svga_read_linear(addr, svga);
}


static uint16_t
gd54xx_readw_linear(uint32_t addr, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;

    uint8_t ap = gd54xx_get_aperture(addr);
    uint16_t temp;

    addr &= 0x003fffff;	/* 4 MB mask */

    if ((svga->seqregs[0x07] & 0x01) == 0)
	return svga_readw_linear(addr, svga);

    if ((addr >= (svga->vram_max - 256)) && (addr < svga->vram_max)) {
	if ((svga->seqregs[0x17] & CIRRUS_MMIO_ENABLE) && (svga->seqregs[0x17] & CIRRUS_MMIO_USE_PCIADDR)) {
		temp = gd543x_mmio_readw(addr & 0x000000ff, gd54xx);
		return temp;
	}
    }

    /* Do mem sys dest reads here if the blitter is neither paused, nor is there a second aperture. */
    if (gd54xx->countminusone && gd54xx->blt.ms_is_dest &&
	!gd54xx_aperture2_enabled(gd54xx) && !(gd54xx->blt.status & CIRRUS_BLT_PAUSED)) {
	temp = gd54xx_readb_linear(addr, p);
	temp |= gd54xx_readb_linear(addr + 1, p) << 8;
	return temp;
    }

    switch (ap) {
	case 0:
	default:		
		return svga_readw_linear(addr, svga);
	case 2:
		/* 0 -> 3, 1 -> 2, 2 -> 1, 3 -> 0 */
		addr ^= 0x00000002;
	case 1:
		temp = svga_readb_linear(addr + 1, svga);
		temp |= (svga_readb_linear(addr, svga) << 8);

		if (svga->fast)
		        sub_cycles(video_timing_read_w);

		return temp;
	case 3:
		return 0xffff;
    }
}


static uint32_t
gd54xx_readl_linear(uint32_t addr, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;

    uint8_t ap = gd54xx_get_aperture(addr);
    uint32_t temp;

    addr &= 0x003fffff;	/* 4 MB mask */

    if ((svga->seqregs[0x07] & 0x01) == 0)
	return svga_readl_linear(addr, svga);

    if ((addr >= (svga->vram_max - 256)) && (addr < svga->vram_max)) {
	if ((svga->seqregs[0x17] & CIRRUS_MMIO_ENABLE) && (svga->seqregs[0x17] & CIRRUS_MMIO_USE_PCIADDR)) {
		temp = gd543x_mmio_readl(addr & 0x000000ff, gd54xx);
		return temp;
	}
    }

    /* Do mem sys dest reads here if the blitter is neither paused, nor is there a second aperture. */
    if (gd54xx->countminusone && gd54xx->blt.ms_is_dest &&
	!gd54xx_aperture2_enabled(gd54xx) && !(gd54xx->blt.status & CIRRUS_BLT_PAUSED)) {
	temp = gd54xx_readb_linear(addr, p);
	temp |= gd54xx_readb_linear(addr + 1, p) << 8;
	temp |= gd54xx_readb_linear(addr + 2, p) << 16;
	temp |= gd54xx_readb_linear(addr + 3, p) << 24;
	return temp;
    }

    switch (ap) {
	case 0:
	default:		
		return svga_readl_linear(addr, svga);
	case 1:
		temp = svga_readb_linear(addr + 1, svga);
		temp |= (svga_readb_linear(addr, svga) << 8);
		temp |= (svga_readb_linear(addr + 3, svga) << 16);
		temp |= (svga_readb_linear(addr + 2, svga) << 24);

		if (svga->fast)
		        sub_cycles(video_timing_read_l);

		return temp;
	case 2:
		temp = svga_readb_linear(addr + 3, svga);
		temp |= (svga_readb_linear(addr + 2, svga) << 8);
		temp |= (svga_readb_linear(addr + 1, svga) << 16);
		temp |= (svga_readb_linear(addr, svga) << 24);

		if (svga->fast)
		        sub_cycles(video_timing_read_l);

		return temp;
	case 3:
		return 0xffffffff;
    }
}


static uint8_t
gd5436_aperture2_readb(uint32_t addr, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;

    if (gd54xx->countminusone && gd54xx->blt.ms_is_dest &&
	gd54xx_aperture2_enabled(gd54xx) && !(gd54xx->blt.status & CIRRUS_BLT_PAUSED))
	return gd54xx_mem_sys_dest_read(gd54xx);

    return 0xff;
}


static uint16_t
gd5436_aperture2_readw(uint32_t addr, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    uint16_t ret = 0xffff;

    if (gd54xx->countminusone && gd54xx->blt.ms_is_dest &&
	gd54xx_aperture2_enabled(gd54xx) && !(gd54xx->blt.status & CIRRUS_BLT_PAUSED)) {
	ret = gd5436_aperture2_readb(addr, p);
	ret |= gd5436_aperture2_readb(addr + 1, p) << 8;
	return ret;
    }

    return ret;
}


static uint32_t
gd5436_aperture2_readl(uint32_t addr, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    uint32_t ret = 0xffffffff;

    if (gd54xx->countminusone && gd54xx->blt.ms_is_dest &&
	gd54xx_aperture2_enabled(gd54xx) && !(gd54xx->blt.status & CIRRUS_BLT_PAUSED)) {
	ret = gd5436_aperture2_readb(addr, p);
	ret |= gd5436_aperture2_readb(addr + 1, p) << 8;
	ret |= gd5436_aperture2_readb(addr + 2, p) << 16;
	ret |= gd5436_aperture2_readb(addr + 3, p) << 24;
	return ret;
    }

    return ret;
}


static void
gd5436_aperture2_writeb(uint32_t addr, uint8_t val, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;

    if (gd54xx->countminusone && !gd54xx->blt.ms_is_dest
	&& gd54xx_aperture2_enabled(gd54xx) && !(gd54xx->blt.status & CIRRUS_BLT_PAUSED))
	gd54xx_mem_sys_src_write(gd54xx, val);
}


static void
gd5436_aperture2_writew(uint32_t addr, uint16_t val, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;

    if (gd54xx->countminusone && !gd54xx->blt.ms_is_dest
	&& gd54xx_aperture2_enabled(gd54xx) && !(gd54xx->blt.status & CIRRUS_BLT_PAUSED)) {
	gd5436_aperture2_writeb(addr, val, gd54xx);
	gd5436_aperture2_writeb(addr + 1, val >> 8, gd54xx);
    }
}


static void
gd5436_aperture2_writel(uint32_t addr, uint32_t val, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;

    if (gd54xx->countminusone && !gd54xx->blt.ms_is_dest
	&& gd54xx_aperture2_enabled(gd54xx) && !(gd54xx->blt.status & CIRRUS_BLT_PAUSED)) {
	gd5436_aperture2_writeb(addr, val, gd54xx);
	gd5436_aperture2_writeb(addr + 1, val >> 8, gd54xx);
	gd5436_aperture2_writeb(addr + 2, val >> 16, gd54xx);
	gd5436_aperture2_writeb(addr + 3, val >> 24, gd54xx);
    }
}


static void
gd54xx_writeb_linear(uint32_t addr, uint8_t val, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;

    uint8_t ap = gd54xx_get_aperture(addr);

    if ((svga->seqregs[0x07] & 0x01) == 0) {
	svga_write_linear(addr, val, svga);
	return;
    }

    addr &= 0x003fffff;	/* 4 MB mask */

    if ((addr >= (svga->vram_max - 256)) && (addr < svga->vram_max)) {
	if ((svga->seqregs[0x17] & CIRRUS_MMIO_ENABLE) && (svga->seqregs[0x17] & CIRRUS_MMIO_USE_PCIADDR)) {
		gd543x_mmio_write(addr & 0x000000ff, val, gd54xx);
		return;
	}
    }

    /* Do mem sys src writes here if the blitter is neither paused, nor is there a second aperture. */
    if (gd54xx->countminusone && !gd54xx->blt.ms_is_dest &&
	!gd54xx_aperture2_enabled(gd54xx) && !(gd54xx->blt.status & CIRRUS_BLT_PAUSED)) {
	gd54xx_mem_sys_src_write(gd54xx, val);
	return;
    }

    switch (ap) {
	case 0:
	default:
		break;
	case 1:
		/* 0 -> 1, 1 -> 0, 2 -> 3, 3 -> 2 */
		addr ^= 0x00000001;
		break;
	case 2:
		/* 0 -> 3, 1 -> 2, 2 -> 1, 3 -> 0 */
		addr ^= 0x00000003;
		break;
	case 3:
		return;
    }

    svga_write_linear(addr, val, svga);
}


static void 
gd54xx_writew_linear(uint32_t addr, uint16_t val, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;

    uint8_t ap = gd54xx_get_aperture(addr);

    if ((svga->seqregs[0x07] & 0x01) == 0) {
	svga_writew_linear(addr, val, svga);
	return;
    }

    addr &= 0x003fffff;	/* 4 MB mask */

    if ((addr >= (svga->vram_max - 256)) && (addr < svga->vram_max)) {
	if ((svga->seqregs[0x17] & CIRRUS_MMIO_ENABLE) && (svga->seqregs[0x17] & CIRRUS_MMIO_USE_PCIADDR)) {
		gd543x_mmio_writew(addr & 0x000000ff, val, gd54xx);
		return;
	}
    }

    /* Do mem sys src writes here if the blitter is neither paused, nor is there a second aperture. */
    if (gd54xx->countminusone && !gd54xx->blt.ms_is_dest &&
	!gd54xx_aperture2_enabled(gd54xx) && !(gd54xx->blt.status & CIRRUS_BLT_PAUSED)) {
	gd54xx_writeb_linear(addr, val, gd54xx);
	gd54xx_writeb_linear(addr + 1, val >> 8, gd54xx);
	return;
    }

    if (svga->writemode < 4) {
	switch(ap) {
		case 0:
		default:
			svga_writew_linear(addr, val, svga);
			return;
		case 2:
			addr ^= 0x00000002;
		case 1:
			svga_writeb_linear(addr + 1, val & 0xff, svga);
			svga_writeb_linear(addr, val >> 8, svga);

			if (svga->fast)
		        	sub_cycles(video_timing_write_w);
		case 3:
			return;
	}
    } else {
	switch(ap) {
		case 0:
		default:
			svga_write_linear(addr, val & 0xff, svga);
			svga_write_linear(addr + 1, val >> 8, svga);
			return;
		case 2:
			addr ^= 0x00000002;
		case 1:
			svga_write_linear(addr + 1, val & 0xff, svga);
			svga_write_linear(addr, val >> 8, svga);
		case 3:
			return;
	}
    }
}


static void 
gd54xx_writel_linear(uint32_t addr, uint32_t val, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;

    uint8_t ap = gd54xx_get_aperture(addr);

    if ((svga->seqregs[0x07] & 0x01) == 0) {
	svga_writel_linear(addr, val, svga);
	return;
    }

    addr &= 0x003fffff;	/* 4 MB mask */

    if ((addr >= (svga->vram_max - 256)) && (addr < svga->vram_max)) {
	if ((svga->seqregs[0x17] & CIRRUS_MMIO_ENABLE) && (svga->seqregs[0x17] & CIRRUS_MMIO_USE_PCIADDR)) {
		gd543x_mmio_writel(addr & 0x000000ff, val, gd54xx);
		return;
	}
    }

    /* Do mem sys src writes here if the blitter is neither paused, nor is there a second aperture. */
    if (gd54xx->countminusone && !gd54xx->blt.ms_is_dest &&
	!gd54xx_aperture2_enabled(gd54xx) && !(gd54xx->blt.status & CIRRUS_BLT_PAUSED)) {
	gd54xx_writeb_linear(addr, val, gd54xx);
	gd54xx_writeb_linear(addr + 1, val >> 8, gd54xx);
	gd54xx_writeb_linear(addr + 2, val >> 16, gd54xx);
	gd54xx_writeb_linear(addr + 3, val >> 24, gd54xx);
	return;
    }

    if (svga->writemode < 4) {
	switch(ap) {
		case 0:
		default:
			svga_writel_linear(addr, val, svga);
			return;
		case 1:
			svga_writeb_linear(addr + 1, val & 0xff, svga);
			svga_writeb_linear(addr, val >> 8, svga);
			svga_writeb_linear(addr + 3, val >> 16, svga);
			svga_writeb_linear(addr + 2, val >> 24, svga);
			return;
		case 2:
			svga_writeb_linear(addr + 3, val & 0xff, svga);
			svga_writeb_linear(addr + 2, val >> 8, svga);
			svga_writeb_linear(addr + 1, val >> 16, svga);
			svga_writeb_linear(addr, val >> 24, svga);
		case 3:
			return;
	}

	if (svga->fast)
        	sub_cycles(video_timing_write_l);
    } else {
	switch(ap) {
		case 0:
		default:
			svga_write_linear(addr, val & 0xff, svga);
			svga_write_linear(addr+1, val >> 8, svga);
			svga_write_linear(addr+2, val >> 16, svga);
			svga_write_linear(addr+3, val >> 24, svga);
			return;
		case 1:
			svga_write_linear(addr + 1, val & 0xff, svga);
			svga_write_linear(addr, val >> 8, svga);
			svga_write_linear(addr + 3, val >> 16, svga);
			svga_write_linear(addr + 2, val >> 24, svga);
			return;
		case 2:
			svga_write_linear(addr + 3, val & 0xff, svga);
			svga_write_linear(addr + 2, val >> 8, svga);
			svga_write_linear(addr + 1, val >> 16, svga);
			svga_write_linear(addr, val >> 24, svga);
		case 3:
			return;
	}
    }
}


static uint8_t
gd54xx_read(uint32_t addr, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;

    if ((svga->seqregs[0x07] & 0x01) == 0)
	return svga_read(addr, svga);

    if (gd54xx->countminusone && gd54xx->blt.ms_is_dest &&
	!(gd54xx->blt.status & CIRRUS_BLT_PAUSED))
	return gd54xx_mem_sys_dest_read(gd54xx);

    addr = (addr & 0x7fff) + svga->extra_banks[(addr >> 15) & 1];
    return svga_read_linear(addr, svga);
}


static uint16_t
gd54xx_readw(uint32_t addr, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;
    uint16_t ret;

    if ((svga->seqregs[0x07] & 0x01) == 0)
	return svga_readw(addr, svga);

    if (gd54xx->countminusone && gd54xx->blt.ms_is_dest &&
	!(gd54xx->blt.status & CIRRUS_BLT_PAUSED)) {
	ret = gd54xx_read(addr, p);
	ret |= gd54xx_read(addr + 1, p) << 8;
	return ret;
    }

    addr = (addr & 0x7fff) + svga->extra_banks[(addr >> 15) & 1];
    return svga_readw_linear(addr, svga);
}


static uint32_t
gd54xx_readl(uint32_t addr, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;
    uint32_t ret;

    if ((svga->seqregs[0x07] & 0x01) == 0)
	return svga_readl(addr, svga);

    if (gd54xx->countminusone && gd54xx->blt.ms_is_dest &&
	!(gd54xx->blt.status & CIRRUS_BLT_PAUSED)) {
	ret = gd54xx_read(addr, p);
	ret |= gd54xx_read(addr + 1, p) << 8;
	ret |= gd54xx_read(addr + 2, p) << 16;
	ret |= gd54xx_read(addr + 3, p) << 24;
	return ret;
    }

    addr = (addr & 0x7fff) + svga->extra_banks[(addr >> 15) & 1];
    return svga_readl_linear(addr, svga);
}


static int
gd543x_do_mmio(svga_t *svga, uint32_t addr)
{
    if (svga->seqregs[0x17] & CIRRUS_MMIO_USE_PCIADDR)
	return 1;
    else
	return ((addr & ~0xff) == 0xb8000);
}


static void
gd543x_mmio_write(uint32_t addr, uint8_t val, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;
    uint8_t old;

    if (gd543x_do_mmio(svga, addr)) {
	switch (addr & 0xff) {
		case 0x00:
			if (gd54xx_is_5434(svga))
				gd54xx->blt.bg_col = (gd54xx->blt.bg_col & 0xffffff00) | val;
			else
				gd54xx->blt.bg_col = (gd54xx->blt.bg_col & 0xff00) | val;
			break;
		case 0x01:
			if (gd54xx_is_5434(svga))
				gd54xx->blt.bg_col = (gd54xx->blt.bg_col & 0xffff00ff) | (val << 8);
			else
				gd54xx->blt.bg_col = (gd54xx->blt.bg_col & 0x00ff) | (val << 8);
			break;
		case 0x02:
			if (gd54xx_is_5434(svga))
				gd54xx->blt.bg_col = (gd54xx->blt.bg_col & 0xff00ffff) | (val << 16);
			break;
		case 0x03:
			if (gd54xx_is_5434(svga))
				gd54xx->blt.bg_col = (gd54xx->blt.bg_col & 0x00ffffff) | (val << 24);
			break;

		case 0x04:
			if (gd54xx_is_5434(svga))
				gd54xx->blt.fg_col = (gd54xx->blt.fg_col & 0xffffff00) | val;
			else
				gd54xx->blt.fg_col = (gd54xx->blt.fg_col & 0xff00) | val;
			break;
		case 0x05:
			if (gd54xx_is_5434(svga))
				gd54xx->blt.fg_col = (gd54xx->blt.fg_col & 0xffff00ff) | (val << 8);
			else
				gd54xx->blt.fg_col = (gd54xx->blt.fg_col & 0x00ff) | (val << 8);
			break;
		case 0x06:
			if (gd54xx_is_5434(svga))
				gd54xx->blt.fg_col = (gd54xx->blt.fg_col & 0xff00ffff) | (val << 16);
			break;
		case 0x07:
			if (gd54xx_is_5434(svga))
				gd54xx->blt.fg_col = (gd54xx->blt.fg_col & 0x00ffffff) | (val << 24);
			break;

		case 0x08:
			gd54xx->blt.width = (gd54xx->blt.width & 0xff00) | val;
			break;
		case 0x09:
			gd54xx->blt.width = (gd54xx->blt.width & 0x00ff) | (val << 8);
			if (gd54xx_is_5434(svga))
				gd54xx->blt.width &= 0x1fff;
			else
				gd54xx->blt.width &= 0x07ff;
			break;
		case 0x0a:
			gd54xx->blt.height = (gd54xx->blt.height & 0xff00) | val;
			break;
		case 0x0b:
			gd54xx->blt.height = (gd54xx->blt.height & 0x00ff) | (val << 8);
			if (svga->crtc[0x27] >= CIRRUS_ID_CLGD5436)
				gd54xx->blt.height &= 0x07ff;
			else
				gd54xx->blt.height &= 0x03ff;
			break;
		case 0x0c:
			gd54xx->blt.dst_pitch = (gd54xx->blt.dst_pitch & 0xff00) | val;
			break;
		case 0x0d:
			gd54xx->blt.dst_pitch = (gd54xx->blt.dst_pitch & 0x00ff) | (val << 8);
			gd54xx->blt.dst_pitch &= 0x1fff;
			break;
		case 0x0e:
			gd54xx->blt.src_pitch = (gd54xx->blt.src_pitch & 0xff00) | val;
			break;
		case 0x0f:
			gd54xx->blt.src_pitch = (gd54xx->blt.src_pitch & 0x00ff) | (val << 8);
			gd54xx->blt.src_pitch &= 0x1fff;
			break;

		case 0x10:
			gd54xx->blt.dst_addr = (gd54xx->blt.dst_addr & 0xffff00) | val;
			break;
		case 0x11:
			gd54xx->blt.dst_addr = (gd54xx->blt.dst_addr & 0xff00ff) | (val << 8);
			break;
		case 0x12:
			gd54xx->blt.dst_addr = (gd54xx->blt.dst_addr & 0x00ffff) | (val << 16);
			if (gd54xx_is_5434(svga))
				gd54xx->blt.dst_addr &= 0x3fffff;
			else
				gd54xx->blt.dst_addr &= 0x1fffff;

			if ((svga->crtc[0x27] >= CIRRUS_ID_CLGD5436) && (gd54xx->blt.status & CIRRUS_BLT_AUTOSTART) &&
			    !(gd54xx->blt.status & CIRRUS_BLT_BUSY)) {
				gd54xx->blt.status |= CIRRUS_BLT_BUSY;
				gd54xx_start_blit(0, 0xffffffff, gd54xx, svga);
			}
			break;

		case 0x14:
			gd54xx->blt.src_addr = (gd54xx->blt.src_addr & 0xffff00) | val;
			break;
		case 0x15:
			gd54xx->blt.src_addr = (gd54xx->blt.src_addr & 0xff00ff) | (val << 8);
			break;
		case 0x16:
			gd54xx->blt.src_addr = (gd54xx->blt.src_addr & 0x00ffff) | (val << 16);
			if (gd54xx_is_5434(svga))
				gd54xx->blt.src_addr &= 0x3fffff;
			else
				gd54xx->blt.src_addr &= 0x1fffff;
			break;

		case 0x17:
			gd54xx->blt.mask = val;
			break;
		case 0x18:
			gd54xx->blt.mode = val;
			gd543x_recalc_mapping(gd54xx);
			break;

		case 0x1a:
			gd54xx->blt.rop = val;
			break;

		case 0x1b:
			if (svga->crtc[0x27] >= CIRRUS_ID_CLGD5436)	
				gd54xx->blt.modeext = val;
			break;

		case 0x1c:
			gd54xx->blt.trans_col = (gd54xx->blt.trans_col & 0xff00) | val;
			break;
		case 0x1d:	
			gd54xx->blt.trans_col = (gd54xx->blt.trans_col & 0x00ff) | (val << 8);
			break;

		case 0x20:
			gd54xx->blt.trans_mask = (gd54xx->blt.trans_mask & 0xff00) | val;
			break;
		case 0x21:
			gd54xx->blt.trans_mask = (gd54xx->blt.trans_mask & 0x00ff) | (val << 8);
			break;

		case 0x40:
			old = gd54xx->blt.status;
			gd54xx->blt.status = val;
			gd543x_recalc_mapping(gd54xx);
			if (!(old & CIRRUS_BLT_RESET) && (gd54xx->blt.status & CIRRUS_BLT_RESET))
				gd54xx_reset_blit(gd54xx);
			else if (!(old & CIRRUS_BLT_START) && (gd54xx->blt.status & CIRRUS_BLT_START)) {
				gd54xx->blt.status |= CIRRUS_BLT_BUSY;
				gd54xx_start_blit(0, 0xffffffff, gd54xx, svga);
			}
			break;
	}
    } else if (gd54xx->mmio_vram_overlap)
	gd54xx_write(addr, val, gd54xx);
}


static void
gd543x_mmio_writeb(uint32_t addr, uint8_t val, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;

    if (!gd543x_do_mmio(svga, addr) && !gd54xx->blt.ms_is_dest &&
	gd54xx->countminusone && !(gd54xx->blt.status & CIRRUS_BLT_PAUSED)) {
	gd54xx_mem_sys_src_write(gd54xx, val);
	return;
    }

    gd543x_mmio_write(addr, val, p);
}


static void
gd543x_mmio_writew(uint32_t addr, uint16_t val, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;

    if (gd543x_do_mmio(svga, addr)) {
	gd543x_mmio_write(addr, val & 0xff, gd54xx);
	gd543x_mmio_write(addr + 1, val >> 8, gd54xx);
    } else if (gd54xx->mmio_vram_overlap) {
	if (gd54xx->countminusone && !gd54xx->blt.ms_is_dest &&
	    !(gd54xx->blt.status & CIRRUS_BLT_PAUSED)) {
		gd543x_mmio_write(addr, val & 0xff, gd54xx);
		gd543x_mmio_write(addr + 1, val >> 8, gd54xx);
	} else {
		gd54xx_write(addr, val, gd54xx);
		gd54xx_write(addr + 1, val >> 8, gd54xx);
	}
    }
}


static void
gd543x_mmio_writel(uint32_t addr, uint32_t val, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;

    if (gd543x_do_mmio(svga, addr)) {
	gd543x_mmio_write(addr, val & 0xff, gd54xx);
	gd543x_mmio_write(addr+1, val >> 8, gd54xx);
	gd543x_mmio_write(addr+2, val >> 16, gd54xx);
	gd543x_mmio_write(addr+3, val >> 24, gd54xx);		
    } else if (gd54xx->mmio_vram_overlap) {
	if (gd54xx->countminusone && !gd54xx->blt.ms_is_dest &&
	    !(gd54xx->blt.status & CIRRUS_BLT_PAUSED)) {
		gd543x_mmio_write(addr, val & 0xff, gd54xx);
		gd543x_mmio_write(addr+1, val >> 8, gd54xx);
		gd543x_mmio_write(addr+2, val >> 16, gd54xx);
		gd543x_mmio_write(addr+3, val >> 24, gd54xx);		
	} else {
		gd54xx_write(addr, val, gd54xx);
		gd54xx_write(addr+1, val >> 8, gd54xx);
		gd54xx_write(addr+2, val >> 16, gd54xx);
		gd54xx_write(addr+3, val >> 24, gd54xx);
	}
    }
}


static uint8_t
gd543x_mmio_read(uint32_t addr, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;
    uint8_t ret = 0xff;

    if (gd543x_do_mmio(svga, addr)) {
	switch (addr & 0xff) {
		case 0x00:
			ret = gd54xx->blt.bg_col & 0xff;
			break;
		case 0x01:
			ret = (gd54xx->blt.bg_col >> 8) & 0xff;
			break;
		case 0x02:
			if (gd54xx_is_5434(svga))
				ret = (gd54xx->blt.bg_col >> 16) & 0xff;
			break;
		case 0x03:
			if (gd54xx_is_5434(svga))
				ret = (gd54xx->blt.bg_col >> 24) & 0xff;
			break;

		case 0x04:
			ret = gd54xx->blt.fg_col & 0xff;
			break;
		case 0x05:
			ret = (gd54xx->blt.fg_col >> 8) & 0xff;
			break;
		case 0x06:
			if (gd54xx_is_5434(svga))
				ret = (gd54xx->blt.fg_col >> 16) & 0xff;
			break;
		case 0x07:
			if (gd54xx_is_5434(svga))
				ret = (gd54xx->blt.fg_col >> 24) & 0xff;
			break;

		case 0x08:
			ret = gd54xx->blt.width & 0xff;
			break;
		case 0x09:
			if (gd54xx_is_5434(svga))
				ret = (gd54xx->blt.width >> 8) & 0x1f;
			else
				ret = (gd54xx->blt.width >> 8) & 0x07;
			break;
		case 0x0a:
			ret = gd54xx->blt.height & 0xff;
			break;
		case 0x0b:
			if (svga->crtc[0x27] >= CIRRUS_ID_CLGD5436)
				ret = (gd54xx->blt.height >> 8) & 0x07;
			else
				ret = (gd54xx->blt.height >> 8) & 0x03;
			break;
		case 0x0c:
			ret = gd54xx->blt.dst_pitch & 0xff;
			break;
		case 0x0d:
			ret = (gd54xx->blt.dst_pitch >> 8) & 0x1f;
			break;
		case 0x0e:
			ret = gd54xx->blt.src_pitch & 0xff;
			break;
		case 0x0f:
			ret = (gd54xx->blt.src_pitch >> 8) & 0x1f;
			break;

		case 0x10:
			ret = gd54xx->blt.dst_addr & 0xff;
			break;
		case 0x11:
			ret = (gd54xx->blt.dst_addr >> 8) & 0xff;
			break;
		case 0x12:
			if (gd54xx_is_5434(svga))
				ret = (gd54xx->blt.dst_addr >> 16) & 0x3f;
			else
				ret = (gd54xx->blt.dst_addr >> 16) & 0x1f;
			break;

		case 0x14:
			ret = gd54xx->blt.src_addr & 0xff;
			break;
		case 0x15:
			ret = (gd54xx->blt.src_addr >> 8) & 0xff;
			break;
		case 0x16:
			if (gd54xx_is_5434(svga))
				ret = (gd54xx->blt.src_addr >> 16) & 0x3f;
			else
				ret = (gd54xx->blt.src_addr >> 16) & 0x1f;
			break;

		case 0x17:
			ret = gd54xx->blt.mask;
			break;
		case 0x18:
			ret = gd54xx->blt.mode;
			break;

		case 0x1a:
			ret = gd54xx->blt.rop;
			break;

		case 0x1b:
			if (svga->crtc[0x27] >= CIRRUS_ID_CLGD5436)	
				ret = gd54xx->blt.modeext;
			break;

		case 0x1c:
			ret = gd54xx->blt.trans_col & 0xff;
			break;
		case 0x1d:	
			ret = (gd54xx->blt.trans_col >> 8) & 0xff;
			break;

		case 0x20:
			ret = gd54xx->blt.trans_mask & 0xff;
			break;
		case 0x21:	
			ret = (gd54xx->blt.trans_mask >> 8) & 0xff;
			break;

		case 0x40:
			ret = gd54xx->blt.status;
			break;
	}
    } else if (gd54xx->mmio_vram_overlap)
	ret = gd54xx_read(addr, gd54xx);
    else if (gd54xx->countminusone && gd54xx->blt.ms_is_dest &&
	     !(gd54xx->blt.status & CIRRUS_BLT_PAUSED)) {
	ret = gd54xx_mem_sys_dest_read(gd54xx);
    }

    return ret;
}


static uint16_t
gd543x_mmio_readw(uint32_t addr, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;
    uint16_t ret = 0xffff;

    if (gd543x_do_mmio(svga, addr))
	ret = gd543x_mmio_read(addr, gd54xx) | (gd543x_mmio_read(addr+1, gd54xx) << 8);
    else if (gd54xx->mmio_vram_overlap)
	ret = gd54xx_read(addr, gd54xx) | (gd54xx_read(addr+1, gd54xx) << 8);
    else if (gd54xx->countminusone && gd54xx->blt.ms_is_dest &&
	     !(gd54xx->blt.status & CIRRUS_BLT_PAUSED)) {
	ret = gd543x_mmio_read(addr, p);
	ret |= gd543x_mmio_read(addr + 1, p) << 8;
	return ret;
    }

    return ret;
}


static uint32_t
gd543x_mmio_readl(uint32_t addr, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;
    uint32_t ret = 0xffffffff;

    if (gd543x_do_mmio(svga, addr))
	ret = gd543x_mmio_read(addr, gd54xx) | (gd543x_mmio_read(addr+1, gd54xx) << 8) | (gd543x_mmio_read(addr+2, gd54xx) << 16) | (gd543x_mmio_read(addr+3, gd54xx) << 24);
    else if (gd54xx->mmio_vram_overlap)
	ret = gd54xx_read(addr, gd54xx) | (gd54xx_read(addr+1, gd54xx) << 8) | (gd54xx_read(addr+2, gd54xx) << 16) | (gd54xx_read(addr+3, gd54xx) << 24);
    else if (gd54xx->countminusone && gd54xx->blt.ms_is_dest &&
	     !(gd54xx->blt.status & CIRRUS_BLT_PAUSED)) {
	ret = gd543x_mmio_read(addr, p);
	ret |= gd543x_mmio_read(addr + 1, p) << 8;
	ret |= gd543x_mmio_read(addr + 2, p) << 16;
	ret |= gd543x_mmio_read(addr + 3, p) << 24;
	return ret;
    }

    return ret;
}


static uint8_t
gd54xx_color_expand(gd54xx_t *gd54xx, int mask, int shift)
{
    uint8_t ret;

    if (gd54xx->blt.mode & CIRRUS_BLTMODE_TRANSPARENTCOMP)
	ret = gd54xx->blt.fg_col >> (shift << 3);
    else
	ret = mask ? (gd54xx->blt.fg_col >> (shift << 3)) : (gd54xx->blt.bg_col >> (shift << 3));

    return ret;
}


static int
gd54xx_get_pixel_width(gd54xx_t *gd54xx)
{
    int ret = 1;

    switch (gd54xx->blt.mode & CIRRUS_BLTMODE_PIXELWIDTHMASK) {
	case CIRRUS_BLTMODE_PIXELWIDTH8:
		ret = 1;
		break;
	case CIRRUS_BLTMODE_PIXELWIDTH16:
		ret = 2;
		break;
	case CIRRUS_BLTMODE_PIXELWIDTH24:
		ret = 3;
		break;
	case CIRRUS_BLTMODE_PIXELWIDTH32:
		ret = 4;
		break;
    }

    return ret;
}


static void
gd54xx_blit(gd54xx_t *gd54xx, uint8_t mask, uint8_t *dst, uint8_t target, int skip)
{
    int is_transp, is_bgonly;

    /* skip indicates whether or not it is a pixel to be skipped (used for left skip);
       mask indicates transparency or not (only when transparent comparison is enabled):
	color expand: direct pattern bit; 1 = write, 0 = do not write
		      (the other way around in inverse mode);
	normal 8-bpp or 16-bpp: does not match transparent color = write,
				matches transparent color = do not write */

    /* Make sure to always ignore transparency and skip in case of mem sys dest. */
    is_transp = (gd54xx->blt.mode & CIRRUS_BLTMODE_MEMSYSDEST) ? 0 : (gd54xx->blt.mode & CIRRUS_BLTMODE_TRANSPARENTCOMP);
    is_bgonly = (gd54xx->blt.mode & CIRRUS_BLTMODE_MEMSYSDEST) ? 0 : (gd54xx->blt.modeext & CIRRUS_BLTMODEEXT_BACKGROUNDONLY);
    skip = (gd54xx->blt.mode & CIRRUS_BLTMODE_MEMSYSDEST) ? 0 : skip;

    if (is_transp) {
	if ((gd54xx->blt.mode & CIRRUS_BLTMODE_COLOREXPAND) &&
	    (gd54xx->blt.modeext & CIRRUS_BLTMODEEXT_COLOREXPINV))
		mask = !mask;

	/* If mask is 1 and it is not a pixel to be skipped, write it. */
	if (mask && !skip)
		*dst = target;
    } else if ((gd54xx->blt.mode & CIRRUS_BLTMODE_COLOREXPAND) && is_bgonly) {
	/* If mask is 1 or it is not a pixel to be skipped, write it.
	   (Skip only background pixels.) */
	if (mask || !skip)
		*dst = target;
    } else {
	/* If if it is not a pixel to be skipped, write it. */
	if (!skip)
		*dst = target;
    }
}


static int
gd54xx_transparent_comp(gd54xx_t *gd54xx, uint32_t xx, uint8_t src)
{
    svga_t *svga = &gd54xx->svga;
    int ret = 1;

    if ((gd54xx->blt.pixel_width <= 2) && gd54xx_has_transp(svga, 0)) {
	ret = src ^ ((uint8_t *) &(gd54xx->blt.trans_col))[xx];
	if (gd54xx_has_transp(svga, 1))
		ret &= ~(((uint8_t *) &(gd54xx->blt.trans_mask))[xx]);
	ret = !ret;
    }

    return ret;
}


static void
gd54xx_pattern_copy(gd54xx_t *gd54xx)
{
    uint8_t target, src, *dst;
    int x, y, pattern_y, pattern_pitch;
    uint32_t bitmask = 0, xx, pixel;
    uint32_t srca, srca2, dsta;
    svga_t *svga = &gd54xx->svga;

    pattern_pitch = gd54xx->blt.pixel_width << 3;

    if (gd54xx->blt.pixel_width == 3)
	pattern_pitch = 32;

    if (gd54xx->blt.mode & CIRRUS_BLTMODE_COLOREXPAND)
	pattern_pitch = 1;

    dsta = gd54xx->blt.dst_addr & svga->vram_mask;
    /* The vertical offset is in the three low-order bits of the Source Address register. */
    pattern_y = gd54xx->blt.src_addr & 0x07;

    /* Mode		Pattern bytes	Pattern line bytes
       ---------------------------------------------------
       Color Expansion	8		1
       8-bpp		64		8
       16-bpp		128		16
       24-bpp		256		32
       32-bpp		256		32
     */

    /* The boundary has to be equal to the size of the pattern. */
    srca = (gd54xx->blt.src_addr & ~0x07) & svga->vram_mask;

    for (y = 0; y <= gd54xx->blt.height; y++) {
	/* Go to the correct pattern line. */
	srca2 = srca + (pattern_y * pattern_pitch);
	pixel = 0;
	for (x = 0; x <= gd54xx->blt.width; x += gd54xx->blt.pixel_width) {
		if (gd54xx->blt.mode & CIRRUS_BLTMODE_COLOREXPAND) {
			if (gd54xx->blt.modeext & CIRRUS_BLTMODEEXT_SOLIDFILL)
				bitmask = 1;
			else
				bitmask = svga->vram[srca2 & svga->vram_mask] & (0x80 >> pixel);
		}
		for (xx = 0; xx < gd54xx->blt.pixel_width; xx++) {
			if (gd54xx->blt.mode & CIRRUS_BLTMODE_COLOREXPAND)
				src = gd54xx_color_expand(gd54xx, bitmask, xx);
			else {
				src = svga->vram[(srca2 + (x % (gd54xx->blt.pixel_width << 3)) + xx) & svga->vram_mask];
				bitmask = gd54xx_transparent_comp(gd54xx, xx, src);
			}
			dst = &(svga->vram[(dsta + x + xx) & svga->vram_mask]);
			target = *dst;
			gd54xx_rop(gd54xx, &target, &target, &src);
			if (gd54xx->blt.pixel_width == 3)
				gd54xx_blit(gd54xx, bitmask, dst, target, ((x + xx) < gd54xx->blt.pattern_x));
			else
				gd54xx_blit(gd54xx, bitmask, dst, target, (x < gd54xx->blt.pattern_x));
		}
		pixel = (pixel + 1) & 7;
		svga->changedvram[((dsta + x) & svga->vram_mask) >> 12] = changeframecount;
	}
	pattern_y = (pattern_y + 1) & 7;
	dsta += gd54xx->blt.dst_pitch;
    }
}


static void
gd54xx_reset_blit(gd54xx_t *gd54xx)
{
    gd54xx->countminusone = 0;
    gd54xx->blt.status &= ~(CIRRUS_BLT_START|CIRRUS_BLT_BUSY|CIRRUS_BLT_FIFOUSED);
}


/* Each blit is either 1 byte -> 1 byte (non-color expand blit)
   or 1 byte -> 8/16/24/32 bytes (color expand blit). */
static void
gd54xx_mem_sys_src(gd54xx_t *gd54xx, uint32_t cpu_dat, uint32_t count)
{
    uint8_t *dst, exp, target;
    int mask_shift;
    uint32_t byte_pos, bitmask = 0;
    svga_t *svga = &gd54xx->svga;

    gd54xx->blt.ms_is_dest = 0;

    if (gd54xx->blt.mode & (CIRRUS_BLTMODE_MEMSYSDEST | CIRRUS_BLTMODE_PATTERNCOPY))
	gd54xx_reset_blit(gd54xx);
    else if (count == 0xffffffff) {
	gd54xx->blt.dst_addr_backup = gd54xx->blt.dst_addr;
	gd54xx->blt.src_addr_backup = gd54xx->blt.src_addr;
	gd54xx->blt.x_count = gd54xx->blt.xx_count = 0;
	gd54xx->blt.y_count = 0;
	gd54xx->countminusone = 1;
	gd54xx->blt.sys_src32 = 0x00000000;
	gd54xx->blt.sys_cnt = 0;
	return;
    } else if (gd54xx->countminusone) {
	if (!(gd54xx->blt.mode & CIRRUS_BLTMODE_COLOREXPAND) || (gd54xx->blt.modeext & CIRRUS_BLTMODEEXT_DWORDGRANULARITY)) {
		if (!gd54xx->blt.xx_count && !gd54xx->blt.x_count)
			byte_pos = (((gd54xx->blt.mask >> 5) & 3) << 3);
		else
			byte_pos = 0;
		mask_shift = 31 - byte_pos;
		if (!(gd54xx->blt.mode & CIRRUS_BLTMODE_COLOREXPAND))
			cpu_dat >>= byte_pos;
	} else
		mask_shift = 7;

	while (mask_shift > -1) {
		if (gd54xx->blt.mode & CIRRUS_BLTMODE_COLOREXPAND) {
			bitmask = (cpu_dat >> mask_shift) & 0x01;
			exp = gd54xx_color_expand(gd54xx, bitmask, gd54xx->blt.xx_count);
		} else {
			exp = cpu_dat & 0xff;
			bitmask = gd54xx_transparent_comp(gd54xx, gd54xx->blt.xx_count, exp);
		}

		dst = &(svga->vram[gd54xx->blt.dst_addr_backup & svga->vram_mask]);
		target = *dst;
		gd54xx_rop(gd54xx, &target, &target, &exp);
		if ((gd54xx->blt.pixel_width == 3) && (gd54xx->blt.mode & CIRRUS_BLTMODE_COLOREXPAND))
			gd54xx_blit(gd54xx, bitmask, dst, target, ((gd54xx->blt.x_count + gd54xx->blt.xx_count) < gd54xx->blt.pattern_x));
		else
			gd54xx_blit(gd54xx, bitmask, dst, target, (gd54xx->blt.x_count < gd54xx->blt.pattern_x));

		gd54xx->blt.dst_addr_backup += gd54xx->blt.dir;

		if (gd54xx->blt.mode & CIRRUS_BLTMODE_COLOREXPAND)
			gd54xx->blt.xx_count = (gd54xx->blt.xx_count + 1) % gd54xx->blt.pixel_width;

		svga->changedvram[(gd54xx->blt.dst_addr_backup & svga->vram_mask) >> 12] = changeframecount;

		if (!gd54xx->blt.xx_count) {
			/* 1 mask bit = 1 blitted pixel */
			if (gd54xx->blt.mode & CIRRUS_BLTMODE_COLOREXPAND)
				mask_shift--;
			else {
				cpu_dat >>= 8;
				mask_shift -= 8;
			}

			if (gd54xx->blt.mode & CIRRUS_BLTMODE_COLOREXPAND)
				gd54xx->blt.x_count = (gd54xx->blt.x_count + gd54xx->blt.pixel_width) % (gd54xx->blt.width + 1);
			else
				gd54xx->blt.x_count = (gd54xx->blt.x_count + 1) % (gd54xx->blt.width + 1);

			if (!gd54xx->blt.x_count) {
				gd54xx->blt.y_count = (gd54xx->blt.y_count + 1) % (gd54xx->blt.height + 1);
				if (gd54xx->blt.y_count)
					gd54xx->blt.dst_addr_backup = gd54xx->blt.dst_addr + (gd54xx->blt.dst_pitch * gd54xx->blt.y_count * gd54xx->blt.dir);
				else {
				    /* If we're here, the blit is over, reset. */
				    gd54xx_reset_blit(gd54xx);
				}
				/* Stop blitting and request new data if end of line reached. */
				return;
			}
		}
	}
    }
}


static void
gd54xx_normal_blit(uint32_t count, gd54xx_t *gd54xx, svga_t *svga)
{
    uint8_t src = 0, dst;
    uint16_t width = gd54xx->blt.width;
    int x_max = 0, shift = 0, mask = 0;
    uint32_t src_addr = gd54xx->blt.src_addr;
    uint32_t dst_addr = gd54xx->blt.dst_addr;

    x_max = gd54xx->blt.pixel_width << 3;

    gd54xx->blt.dst_addr_backup = gd54xx->blt.dst_addr;
    gd54xx->blt.src_addr_backup = gd54xx->blt.src_addr;
    gd54xx->blt.height_internal = gd54xx->blt.height;
    gd54xx->blt.x_count = 0;
    gd54xx->blt.y_count = 0;

    while (count) {
	src = 0;
	mask = 0;

	if (gd54xx->blt.mode & CIRRUS_BLTMODE_COLOREXPAND) {
		mask = svga->vram[src_addr & svga->vram_mask] & (0x80 >> (gd54xx->blt.x_count / gd54xx->blt.pixel_width));
		shift = (gd54xx->blt.x_count % gd54xx->blt.pixel_width);
		src = gd54xx_color_expand(gd54xx, mask, shift);
	} else {
		src = svga->vram[src_addr & svga->vram_mask];
		src_addr += gd54xx->blt.dir;
		mask = 1;
	}
	count--;

	dst = svga->vram[dst_addr & svga->vram_mask];
	svga->changedvram[(dst_addr & svga->vram_mask) >> 12] = changeframecount;

	gd54xx_rop(gd54xx, (uint8_t *) &dst, (uint8_t *) &dst, (const uint8_t *) &src);

	if ((gd54xx->blt.mode & CIRRUS_BLTMODE_COLOREXPAND) && (gd54xx->blt.modeext & CIRRUS_BLTMODEEXT_COLOREXPINV))
		mask = !mask;

	/* This handles 8bpp and 16bpp non-color-expanding transparent comparisons. */
	if ((gd54xx->blt.mode & CIRRUS_BLTMODE_TRANSPARENTCOMP) && !(gd54xx->blt.mode & CIRRUS_BLTMODE_COLOREXPAND) &&
	    ((gd54xx->blt.mode & CIRRUS_BLTMODE_PIXELWIDTHMASK) <= CIRRUS_BLTMODE_PIXELWIDTH16) &&
	    (src != ((gd54xx->blt.trans_mask >> (shift << 3)) & 0xff)))
		mask = 0;

	if (((gd54xx->blt.width - width) >= gd54xx->blt.pattern_x) &&
		!((gd54xx->blt.mode & CIRRUS_BLTMODE_TRANSPARENTCOMP) && !mask)) {
		svga->vram[dst_addr & svga->vram_mask] = dst;
       }

	dst_addr += gd54xx->blt.dir;
	gd54xx->blt.x_count++;

	if (gd54xx->blt.x_count == x_max) {
		gd54xx->blt.x_count = 0;
		if (gd54xx->blt.mode & CIRRUS_BLTMODE_COLOREXPAND)
			src_addr++;
	}

	width--;
	if (width == 0xffff) {
		width = gd54xx->blt.width;
		dst_addr = gd54xx->blt.dst_addr_backup = gd54xx->blt.dst_addr_backup + (gd54xx->blt.dst_pitch * gd54xx->blt.dir);
		gd54xx->blt.y_count = (gd54xx->blt.y_count + gd54xx->blt.dir) & 7;

		if (gd54xx->blt.mode & CIRRUS_BLTMODE_COLOREXPAND) {
			if (gd54xx->blt.x_count != 0)
				src_addr++;
		} else
			src_addr = gd54xx->blt.src_addr_backup = gd54xx->blt.src_addr_backup + (gd54xx->blt.src_pitch * gd54xx->blt.dir);

		dst_addr &= svga->vram_mask;
		gd54xx->blt.dst_addr_backup &= svga->vram_mask;
		src_addr &= svga->vram_mask;
		gd54xx->blt.src_addr_backup &= svga->vram_mask;

		gd54xx->blt.x_count = 0;

		gd54xx->blt.height_internal--;
		if (gd54xx->blt.height_internal == 0xffff) {
			gd54xx_reset_blit(gd54xx);
			return;
		}
	}
    }

    /* Count exhausted, stuff still left to blit. */
    gd54xx_reset_blit(gd54xx);
}


static void
gd54xx_mem_sys_dest(uint32_t count, gd54xx_t *gd54xx, svga_t *svga)
{
    gd54xx->blt.ms_is_dest = 1;

    if (gd54xx->blt.mode & CIRRUS_BLTMODE_PATTERNCOPY) {
	fatal("mem sys dest pattern copy not allowed (see 1994 manual)\n");
	gd54xx_reset_blit(gd54xx);
    } else if (gd54xx->blt.mode & CIRRUS_BLTMODE_COLOREXPAND) {
	fatal("mem sys dest color expand not allowed (see 1994 manual)\n");
	gd54xx_reset_blit(gd54xx);
    } else {
	if (count == 0xffffffff) {
		gd54xx->blt.dst_addr_backup = gd54xx->blt.dst_addr;
		gd54xx->blt.msd_buf_cnt = 0;
		gd54xx->blt.src_addr_backup = gd54xx->blt.src_addr;
		gd54xx->blt.x_count = gd54xx->blt.xx_count = 0;
		gd54xx->blt.y_count = 0;
		gd54xx->countminusone = 1;
		count = 32;
	}

	gd54xx->blt.msd_buf_pos = 0;

	while (gd54xx->blt.msd_buf_pos < 32) {
		gd54xx->blt.msd_buf[gd54xx->blt.msd_buf_pos & 0x1f] = svga->vram[gd54xx->blt.src_addr_backup & svga->vram_mask];
		gd54xx->blt.src_addr_backup += gd54xx->blt.dir;
		gd54xx->blt.msd_buf_pos++;

		gd54xx->blt.x_count = (gd54xx->blt.x_count + 1) % (gd54xx->blt.width + 1);

		if (!gd54xx->blt.x_count) {
			gd54xx->blt.y_count = (gd54xx->blt.y_count + 1) % (gd54xx->blt.height + 1);

			if (gd54xx->blt.y_count)
				gd54xx->blt.src_addr_backup = gd54xx->blt.src_addr + (gd54xx->blt.src_pitch * gd54xx->blt.y_count * gd54xx->blt.dir);
			else
				gd54xx->countminusone = 2;	/* Signal end of blit. */
			/* End of line reached, stop and notify regardless of how much we already transferred. */
			goto request_more_data;
		}
	}

	/* End of while. */
request_more_data:
	/* If the byte count we have blitted are not divisible by 4, round them up. */
	if (gd54xx->blt.msd_buf_pos & 3)
		gd54xx->blt.msd_buf_cnt = (gd54xx->blt.msd_buf_pos & ~3) + 4;
	else
		gd54xx->blt.msd_buf_cnt = gd54xx->blt.msd_buf_pos;
	gd54xx->blt.msd_buf_pos = 0;
	return;
    }
}


static void 
gd54xx_start_blit(uint32_t cpu_dat, uint32_t count, gd54xx_t *gd54xx, svga_t *svga)
{
    if ((gd54xx->blt.mode & CIRRUS_BLTMODE_BACKWARDS) &&
	!(gd54xx->blt.mode & (CIRRUS_BLTMODE_PATTERNCOPY|CIRRUS_BLTMODE_COLOREXPAND)) &&
	!(gd54xx->blt.mode & CIRRUS_BLTMODE_TRANSPARENTCOMP))
	gd54xx->blt.dir = -1;
    else
	gd54xx->blt.dir = 1;

    gd54xx->blt.pixel_width = gd54xx_get_pixel_width(gd54xx);

    if (gd54xx->blt.mode & (CIRRUS_BLTMODE_PATTERNCOPY|CIRRUS_BLTMODE_COLOREXPAND)) {
	if (gd54xx->blt.pixel_width == 3)
		gd54xx->blt.pattern_x = gd54xx->blt.mask & 0x1f;				/* (Mask & 0x1f) bytes. */
	else
		gd54xx->blt.pattern_x = (gd54xx->blt.mask & 0x07) * gd54xx->blt.pixel_width;	/* (Mask & 0x07) pixels. */
    } else
	gd54xx->blt.pattern_x = 0;								/* No skip in normal blit mode. */

    if (gd54xx->blt.mode & CIRRUS_BLTMODE_MEMSYSSRC)
	gd54xx_mem_sys_src(gd54xx, cpu_dat, count);
    else if (gd54xx->blt.mode & CIRRUS_BLTMODE_MEMSYSDEST)
	gd54xx_mem_sys_dest(count, gd54xx, svga);
    else if (gd54xx->blt.mode & CIRRUS_BLTMODE_PATTERNCOPY) {
	gd54xx_pattern_copy(gd54xx);
	gd54xx_reset_blit(gd54xx);
    } else
	gd54xx_normal_blit(count, gd54xx, svga);
}


static uint8_t 
cl_pci_read(int func, int addr, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;
    svga_t *svga = &gd54xx->svga;

    if ((addr >= 0x30) && (addr <= 0x33) && (!gd54xx->has_bios))
	return 0;

    switch (addr) {
	case 0x00: return 0x13; /*Cirrus Logic*/
	case 0x01: return 0x10;

	case 0x02:
		return svga->crtc[0x27];
	case 0x03: return 0x00;
	
	case PCI_REG_COMMAND:
		return gd54xx->pci_regs[PCI_REG_COMMAND]; /*Respond to IO and memory accesses*/

	case 0x07: return 0x02; /*Fast DEVSEL timing*/
        
	case 0x08: return gd54xx->rev; /*Revision ID*/
	case 0x09: return 0x00; /*Programming interface*/
        
	case 0x0a: return 0x00; /*Supports VGA interface*/
	case 0x0b: return 0x03;

	case 0x10: return 0x08; /*Linear frame buffer address*/
	case 0x11: return 0x00;
	case 0x12: return 0x00;
	case 0x13: return gd54xx->lfb_base >> 24;

	case 0x30: return (gd54xx->pci_regs[0x30] & 0x01); /*BIOS ROM address*/
	case 0x31: return 0x00;
	case 0x32: return gd54xx->pci_regs[0x32];
	case 0x33: return gd54xx->pci_regs[0x33];

	case 0x3c: return gd54xx->int_line;
	case 0x3d: return PCI_INTA;
    }
    return 0;
}


static void 
cl_pci_write(int func, int addr, uint8_t val, void *p)
{
    gd54xx_t *gd54xx = (gd54xx_t *)p;

    if ((addr >= 0x30) && (addr <= 0x33) && (!gd54xx->has_bios))
	return;

    switch (addr) {
	case PCI_REG_COMMAND:
		gd54xx->pci_regs[PCI_REG_COMMAND] = val & 0x23;
		io_removehandler(0x03c0, 0x0020, gd54xx_in, NULL, NULL, gd54xx_out, NULL, NULL, gd54xx);
		if (val & PCI_COMMAND_IO)
			io_sethandler(0x03c0, 0x0020, gd54xx_in, NULL, NULL, gd54xx_out, NULL, NULL, gd54xx);
		gd543x_recalc_mapping(gd54xx);
		break;

	case 0x13: 
		gd54xx->lfb_base = val << 24;
		gd543x_recalc_mapping(gd54xx);
		break;                

	case 0x30: case 0x32: case 0x33:
		gd54xx->pci_regs[addr] = val;
		if (gd54xx->pci_regs[0x30] & 0x01) {
			uint32_t addr = (gd54xx->pci_regs[0x32] << 16) | (gd54xx->pci_regs[0x33] << 24);
			mem_mapping_set_addr(&gd54xx->bios_rom.mapping, addr, 0x8000);
		} else
			mem_mapping_disable(&gd54xx->bios_rom.mapping);
		return;

	case 0x3c:
		gd54xx->int_line = val;
		return;
    }
}


static void
*gd54xx_init(const device_t *info)
{
    gd54xx_t *gd54xx = malloc(sizeof(gd54xx_t));
    svga_t *svga = &gd54xx->svga;
    int id = info->local & 0xff;
    int vram;
    wchar_t *romfn = NULL;
    memset(gd54xx, 0, sizeof(gd54xx_t));

    gd54xx->pci = !!(info->flags & DEVICE_PCI);	
    gd54xx->vlb = !!(info->flags & DEVICE_VLB);	

    gd54xx->rev = 0;
    gd54xx->has_bios = 1;

    switch (id) {
#if defined(DEV_BRANCH) && defined(USE_CL5422)
	case CIRRUS_ID_CLGD5402:
	case CIRRUS_ID_CLGD5420:
		romfn = BIOS_GD5420_PATH;
		break;
	case CIRRUS_ID_CLGD5422:
	case CIRRUS_ID_CLGD5424:
		romfn = BIOS_GD5422_PATH;
		break;		
#endif
		
	case CIRRUS_ID_CLGD5426:
		romfn = BIOS_GD5426_PATH;
		break;
		
	case CIRRUS_ID_CLGD5428:
		if (gd54xx->vlb)
			romfn = BIOS_GD5428_PATH;
		else
			romfn = BIOS_GD5428_ISA_PATH;
		break;

	case CIRRUS_ID_CLGD5429:
		romfn = BIOS_GD5429_PATH;
		break;

	case CIRRUS_ID_CLGD5434:
		romfn = BIOS_GD5434_PATH;
		break;
		
	case CIRRUS_ID_CLGD5436:
		romfn = BIOS_GD5436_PATH;
		break;

	case CIRRUS_ID_CLGD5430:
		if (info->local & 0x400) {
			/* CL-GD 5440 */
			gd54xx->rev = 0x47;
			if (info->local & 0x200) {
				romfn = NULL;
				gd54xx->has_bios = 0;
			} else
				romfn = BIOS_GD5440_PATH;
		} else {
			/* CL-GD 5430 */
			if (gd54xx->pci)
				romfn = BIOS_GD5430_PCI_PATH;
			else
				romfn = BIOS_GD5430_VLB_PATH;
		}
		break;

	case CIRRUS_ID_CLGD5446:
		if (info->local & 0x100)
			romfn = BIOS_GD5446_STB_PATH;
		else
			romfn = BIOS_GD5446_PATH;
		break;

	case CIRRUS_ID_CLGD5480:
		romfn = BIOS_GD5480_PATH;
		break;
    }

    if (id >= CIRRUS_ID_CLGD5420)
	vram = device_get_config_int("memory");
    else
	vram = 0;

    if (vram)
	gd54xx->vram_size = vram << 20;	
    else
	gd54xx->vram_size = 1 << 19;

    gd54xx->vram_mask = gd54xx->vram_size - 1;

    if (romfn)
	rom_init(&gd54xx->bios_rom, romfn, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    if (info->flags & DEVICE_ISA)
	video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_gd54xx_isa);
    else
	video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_gd54xx_vlb_pci);

    svga_init(&gd54xx->svga, gd54xx, gd54xx->vram_size,
	      gd54xx_recalctimings, gd54xx_in, gd54xx_out,
	      gd54xx_hwcursor_draw, NULL);
    svga->ven_write = gd54xx_write_modes45;
    if (vram <= 1)
	svga->decode_mask = gd54xx->vram_mask;

    mem_mapping_set_handler(&svga->mapping, gd54xx_read, gd54xx_readw, gd54xx_readl, gd54xx_write, gd54xx_writew, gd54xx_writel);
    mem_mapping_set_p(&svga->mapping, gd54xx);

    mem_mapping_add(&gd54xx->mmio_mapping, 0, 0,
		    gd543x_mmio_read, gd543x_mmio_readw, gd543x_mmio_readl,
		    gd543x_mmio_writeb, gd543x_mmio_writew, gd543x_mmio_writel,
		    NULL, MEM_MAPPING_EXTERNAL, gd54xx);
    mem_mapping_disable(&gd54xx->mmio_mapping);
    mem_mapping_add(&gd54xx->linear_mapping, 0, 0,
		    gd54xx_readb_linear, gd54xx_readw_linear, gd54xx_readl_linear,
		    gd54xx_writeb_linear, gd54xx_writew_linear, gd54xx_writel_linear,
		    NULL, MEM_MAPPING_EXTERNAL, gd54xx);
    mem_mapping_disable(&gd54xx->linear_mapping);
    mem_mapping_add(&gd54xx->aperture2_mapping, 0, 0,
		    gd5436_aperture2_readb, gd5436_aperture2_readw, gd5436_aperture2_readl,
		    gd5436_aperture2_writeb, gd5436_aperture2_writew, gd5436_aperture2_writel,
		    NULL, MEM_MAPPING_EXTERNAL, gd54xx);
    mem_mapping_disable(&gd54xx->aperture2_mapping);

    io_sethandler(0x03c0, 0x0020, gd54xx_in, NULL, NULL, gd54xx_out, NULL, NULL, gd54xx);

    svga->hwcursor.yoff = 32;
    svga->hwcursor.xoff = 0;

#if defined(DEV_BRANCH) && defined(USE_CL5422)
    if (id >= CIRRUS_ID_CLGD5420) {
	gd54xx->vclk_n[0] = 0x4a;
	gd54xx->vclk_d[0] = 0x2b;
	gd54xx->vclk_n[1] = 0x5b;
	gd54xx->vclk_d[1] = 0x2f;
	gd54xx->vclk_n[2] = 0x45;
	gd54xx->vclk_d[2] = 0x30;
	gd54xx->vclk_n[3] = 0x7e;
	gd54xx->vclk_d[3] = 0x33;
    } else {
	gd54xx->vclk_n[0] = 0x66;
	gd54xx->vclk_d[0] = 0x3b;
	gd54xx->vclk_n[1] = 0x5b;
	gd54xx->vclk_d[1] = 0x2f;
	gd54xx->vclk_n[2] = 0x45;
	gd54xx->vclk_d[2] = 0x2c;
	gd54xx->vclk_n[3] = 0x7e;
	gd54xx->vclk_d[3] = 0x33;
    }
#else
    gd54xx->vclk_n[0] = 0x4a;
    gd54xx->vclk_d[0] = 0x2b;
    gd54xx->vclk_n[1] = 0x5b;
    gd54xx->vclk_d[1] = 0x2f;
    gd54xx->vclk_n[2] = 0x45;
    gd54xx->vclk_d[2] = 0x30;
    gd54xx->vclk_n[3] = 0x7e;
    gd54xx->vclk_d[3] = 0x33;
#endif

    svga->extra_banks[1] = 0x8000;

    if (gd54xx->pci && id >= CIRRUS_ID_CLGD5430)
	pci_add_card(PCI_ADD_VIDEO, cl_pci_read, cl_pci_write, gd54xx);

    gd54xx->pci_regs[PCI_REG_COMMAND] = 7;

    gd54xx->pci_regs[0x30] = 0x00;
    gd54xx->pci_regs[0x32] = 0x0c;
    gd54xx->pci_regs[0x33] = 0x00;
	
    svga->crtc[0x27] = id;

    svga->seqregs[6] = 0x0f;
    if (svga->crtc[0x27] >= CIRRUS_ID_CLGD5429)
	gd54xx->unlocked = 1;

    return gd54xx;
}

#if defined(DEV_BRANCH) && defined(USE_CL5422)
static int
gd5420_available(void)
{
    return rom_present(BIOS_GD5420_PATH);
}

static int
gd5422_available(void)
{
    return rom_present(BIOS_GD5422_PATH);
}
#endif

static int
gd5426_available(void)
{
    return rom_present(BIOS_GD5426_PATH);
}

static int
gd5428_available(void)
{
    return rom_present(BIOS_GD5428_PATH);
}

static int
gd5428_isa_available(void)
{
    return rom_present(BIOS_GD5428_ISA_PATH);
}

static int
gd5429_available(void)
{
    return rom_present(BIOS_GD5429_PATH);
}

static int
gd5430_vlb_available(void)
{
    return rom_present(BIOS_GD5430_VLB_PATH);
}

static int
gd5430_pci_available(void)
{
    return rom_present(BIOS_GD5430_PCI_PATH);
}

static int
gd5434_available(void)
{
    return rom_present(BIOS_GD5434_PATH);
}

static int
gd5436_available(void)
{
    return rom_present(BIOS_GD5436_PATH);
}

static int
gd5440_available(void)
{
    return rom_present(BIOS_GD5440_PATH);
}

static int
gd5446_available(void)
{
    return rom_present(BIOS_GD5446_PATH);
}

static int
gd5446_stb_available(void)
{
    return rom_present(BIOS_GD5446_STB_PATH);
}

static int
gd5480_available(void)
{
    return rom_present(BIOS_GD5480_PATH);
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

#if defined(DEV_BRANCH) && defined(USE_CL5422)
static const device_config_t gd5422_config[] =
{
        {
                "memory","Memory size",CONFIG_SELECTION,"",1,
                {
                        {
                                "512 KB",0
                        },
                        {
                                "1 MB",1
                        },
                        {
                                ""
                        }
                },
        },
        {
                "","",-1
        }
};
#endif

static const device_config_t gd5428_config[] =
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

static const device_config_t gd5440_onboard_config[] =
{
        {
                .name = "memory",
                .description = "Video memory size",
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

static const device_config_t gd5434_config[] =
{
        {
                .name = "memory",
                .description = "Memory size",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "2 MB",
                                .value = 2
                        },
                        {
                                .description = "4 MB",
                                .value = 4
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 4
        },
        {
                .type = -1
        }
};

#if defined(DEV_BRANCH) && defined(USE_CL5422)
const device_t gd5402_isa_device =
{
    "Cirrus Logic GD-5402 (ACUMOS AVGA2)",
    DEVICE_AT | DEVICE_ISA,
    CIRRUS_ID_CLGD5402,
    gd54xx_init, gd54xx_close,
    NULL,
    gd5420_available, /* Common BIOS between 5402 and 5420 */
    gd54xx_speed_changed,
    gd54xx_force_redraw,
    NULL,
};

const device_t gd5420_isa_device =
{
    "Cirrus Logic GD-5420",
    DEVICE_AT | DEVICE_ISA,
    CIRRUS_ID_CLGD5420,
    gd54xx_init, gd54xx_close,
    NULL,
    gd5420_available, /* Common BIOS between 5402 and 5420 */
    gd54xx_speed_changed,
    gd54xx_force_redraw,
    gd5422_config,
};

const device_t gd5422_isa_device = {
    "Cirrus Logic GD-5422",
    DEVICE_AT | DEVICE_ISA,
    CIRRUS_ID_CLGD5422,
    gd54xx_init, gd54xx_close,
    NULL,
    gd5422_available, /* Common BIOS between 5422 and 5424 */
    gd54xx_speed_changed,
    gd54xx_force_redraw,
    gd5422_config,
};

const device_t gd5424_vlb_device = {
    "Cirrus Logic GD-5424",
    DEVICE_VLB,
    CIRRUS_ID_CLGD5424,
    gd54xx_init, gd54xx_close,
    NULL,
    gd5422_available, /* Common BIOS between 5422 and 5424 */
    gd54xx_speed_changed,
    gd54xx_force_redraw,
    gd5422_config,
};
#endif

const device_t gd5426_vlb_device =
{
    "Cirrus Logic CL-GD 5426 (VLB)",
    DEVICE_VLB,
    CIRRUS_ID_CLGD5426,
    gd54xx_init, 
    gd54xx_close, 
    NULL,
    gd5426_available,
    gd54xx_speed_changed,
    gd54xx_force_redraw,
    gd5428_config
};

const device_t gd5428_isa_device =
{
    "Cirrus Logic CL-GD 5428 (ISA)",
    DEVICE_AT | DEVICE_ISA,
    CIRRUS_ID_CLGD5428,
    gd54xx_init, 
    gd54xx_close, 
    NULL,
    gd5428_isa_available,
    gd54xx_speed_changed,
    gd54xx_force_redraw,
    gd5428_config
};

const device_t gd5428_vlb_device =
{
    "Cirrus Logic CL-GD 5428 (VLB)",
    DEVICE_VLB,
    CIRRUS_ID_CLGD5428,
    gd54xx_init, 
    gd54xx_close, 
    NULL,
    gd5428_available,
    gd54xx_speed_changed,
    gd54xx_force_redraw,
    gd5428_config
};

const device_t gd5429_isa_device =
{
    "Cirrus Logic CL-GD 5429 (ISA)",
    DEVICE_AT | DEVICE_ISA,
    CIRRUS_ID_CLGD5429,
    gd54xx_init, 
    gd54xx_close, 
    NULL,
    gd5429_available,
    gd54xx_speed_changed,
    gd54xx_force_redraw,
    gd5428_config
};

const device_t gd5429_vlb_device =
{
    "Cirrus Logic CL-GD 5429 (VLB)",
    DEVICE_VLB,
    CIRRUS_ID_CLGD5429,
    gd54xx_init, 
    gd54xx_close, 
    NULL,
    gd5429_available,
    gd54xx_speed_changed,
    gd54xx_force_redraw,
    gd5428_config
};

const device_t gd5430_vlb_device =
{
    "Cirrus Logic CL-GD 5430 (VLB)",
    DEVICE_VLB,
    CIRRUS_ID_CLGD5430,
    gd54xx_init, 
    gd54xx_close, 
    NULL,
    gd5430_vlb_available,
    gd54xx_speed_changed,
    gd54xx_force_redraw,
    gd5428_config
};

const device_t gd5430_pci_device =
{
    "Cirrus Logic CL-GD 5430 (PCI)",
    DEVICE_PCI,
    CIRRUS_ID_CLGD5430,
    gd54xx_init, 
    gd54xx_close, 
    NULL,
    gd5430_pci_available,
    gd54xx_speed_changed,
    gd54xx_force_redraw,
    gd5428_config
};

const device_t gd5434_isa_device =
{
    "Cirrus Logic CL-GD 5434 (ISA)",
    DEVICE_AT | DEVICE_ISA,
    CIRRUS_ID_CLGD5434,
    gd54xx_init, 
    gd54xx_close,
    NULL,
    gd5434_available,
    gd54xx_speed_changed,
    gd54xx_force_redraw,
    gd5434_config
};

const device_t gd5434_vlb_device =
{
    "Cirrus Logic CL-GD 5434 (VLB)",
    DEVICE_VLB,
    CIRRUS_ID_CLGD5434,
    gd54xx_init, 
    gd54xx_close, 
    NULL,
    gd5434_available,
    gd54xx_speed_changed,
    gd54xx_force_redraw,
    gd5434_config
};

const device_t gd5434_pci_device =
{
    "Cirrus Logic CL-GD 5434 (PCI)",
    DEVICE_PCI,
    CIRRUS_ID_CLGD5434,
    gd54xx_init, 
    gd54xx_close, 
    NULL,
    gd5434_available,
    gd54xx_speed_changed,
    gd54xx_force_redraw,
    gd5434_config
};

const device_t gd5436_pci_device =
{
    "Cirrus Logic CL-GD 5436 (PCI)",
    DEVICE_PCI,
    CIRRUS_ID_CLGD5436,
    gd54xx_init, 
    gd54xx_close, 
    NULL,
    gd5436_available,
    gd54xx_speed_changed,
    gd54xx_force_redraw,
    gd5434_config
};

const device_t gd5440_onboard_pci_device =
{
    "Cirrus Logic CL-GD 5440 (On-Board PCI)",
    DEVICE_PCI,
    CIRRUS_ID_CLGD5440 | 0x600,
    gd54xx_init,
    gd54xx_close, 
    NULL,
    NULL,
    gd54xx_speed_changed,
    gd54xx_force_redraw,
    gd5440_onboard_config
};

const device_t gd5440_pci_device =
{
    "Cirrus Logic CL-GD 5440 (PCI)",
    DEVICE_PCI,
    CIRRUS_ID_CLGD5440 | 0x400,
    gd54xx_init,
    gd54xx_close, 
    NULL,
    gd5440_available,
    gd54xx_speed_changed,
    gd54xx_force_redraw,
    gd5428_config
};

const device_t gd5446_pci_device =
{
    "Cirrus Logic CL-GD 5446 (PCI)",
    DEVICE_PCI,
    CIRRUS_ID_CLGD5446,
    gd54xx_init,
    gd54xx_close, 
    NULL,
    gd5446_available,
    gd54xx_speed_changed,
    gd54xx_force_redraw,
    gd5434_config
};

const device_t gd5446_stb_pci_device =
{
    "STB Nitro 64V (PCI)",
    DEVICE_PCI,
    CIRRUS_ID_CLGD5446 | 0x100,
    gd54xx_init,
    gd54xx_close, 
    NULL,
    gd5446_stb_available,
    gd54xx_speed_changed,
    gd54xx_force_redraw,
    gd5434_config
};

const device_t gd5480_pci_device =
{
    "Cirrus Logic CL-GD 5480 (PCI)",
    DEVICE_PCI,
    CIRRUS_ID_CLGD5480,
    gd54xx_init, 
    gd54xx_close, 
    NULL,
    gd5480_available,
    gd54xx_speed_changed,
    gd54xx_force_redraw,
    gd5434_config
};
