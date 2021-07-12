/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		ET4000/W32 series emulation.
 *
 * Known bugs:	Accelerator doesn't work in planar modes
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>


#define BIOS_ROM_PATH_DIAMOND	"roms/video/et4000w32/et4000w32.bin"
#define BIOS_ROM_PATH_CARDEX	"roms/video/et4000w32/cardex.vbi"
#define BIOS_ROM_PATH_W32	"roms/video/et4000w32/ET4000W32VLB_bios_MX27C512.BIN"
#define BIOS_ROM_PATH_W32I_ISA	"roms/video/et4000w32/ET4KW32I.VBI"
#define BIOS_ROM_PATH_W32I_VLB	"roms/video/et4000w32/tseng.u41.bin"
#define BIOS_ROM_PATH_W32P	"roms/video/et4000w32/ET4K_W32.BIN"
#define BIOS_ROM_PATH_W32P_REVC	"roms/video/et4000w32/et4000w32pcardex.BIN"


#define ACL_WRST		1
#define ACL_RDST		2
#define ACL_XYST		4
#define ACL_SSO			8


enum
{
    ET4000W32,
    ET4000W32I,
    ET4000W32P_REVC,
    ET4000W32P,
    ET4000W32P_CARDEX,
    ET4000W32P_DIAMOND
};


typedef struct et4000w32p_t
{
    mem_mapping_t	linear_mapping;
    mem_mapping_t	mmu_mapping;

    rom_t		bios_rom;

    svga_t		svga;

    uint8_t		banking, banking2, adjust_cursor, rev;

    uint8_t		regs[256], pci_regs[256];

    int			index, vlb, pci, interleaved,
			bank, blitter_busy, type;

    uint32_t		linearbase, linearbase_old;
    uint32_t		vram_mask;

    /* Accelerator */
    struct {
	struct {
		uint8_t		vbus, pixel_depth, xy_dir, pattern_wrap,
				source_wrap, ctrl_routing, ctrl_reload, rop_fg,
				rop_bg;

		uint16_t	pattern_off, source_off, dest_off, mix_off,
				count_x,count_y, pos_x, pos_y,
				error, dmin, dmaj;

		uint32_t	pattern_addr, source_addr, dest_addr, mix_addr;
	} queued, internal;

	uint8_t		status;

	int		pattern_x, source_x, pattern_x_back, source_x_back,
			pattern_y, source_y, cpu_dat_pos, pix_pos,
			cpu_input_num;

	uint32_t	pattern_addr, source_addr, dest_addr, mix_addr,
			pattern_back, source_back, dest_back, mix_back,
			cpu_input;

	uint64_t	cpu_dat;
    } acl;

    struct {
	uint32_t	base[3];
	uint8_t		ctrl;
    } mmu;
} et4000w32p_t;


static int		et4000w32_vbus[4] = {1, 2, 4, 4};

static int		et4000w32_max_x[8] = {0, 0, 4, 8, 16, 32, 64, 0x70000000};
static int		et4000w32_wrap_x[8] = {0, 0, 3, 7, 15, 31, 63, 0xffffffff};
static int		et4000w32_wrap_y[8] = {1, 2, 4, 8, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};

static video_timings_t	timing_et4000w32_vlb = {VIDEO_BUS, 4,  4,  4,  10, 10, 10};
static video_timings_t	timing_et4000w32_pci = {VIDEO_PCI, 4,  4,  4,  10, 10, 10};
static video_timings_t	timing_et4000w32_isa = {VIDEO_ISA, 4,  4,  4,  10, 10, 10};


void		et4000w32p_recalcmapping(et4000w32p_t *et4000);

uint8_t		et4000w32p_mmu_read(uint32_t addr, void *p);
void		et4000w32p_mmu_write(uint32_t addr, uint8_t val, void *p);

void		et4000w32_blit_start(et4000w32p_t *et4000);
void		et4000w32p_blit_start(et4000w32p_t *et4000);
void		et4000w32p_blit(int count, uint32_t mix, uint32_t sdat, int cpu_input, et4000w32p_t *et4000);
uint8_t		et4000w32p_in(uint16_t addr, void *p);


#ifdef ENABLE_ET4000W32_LOG
int et4000w32_do_log = ENABLE_ET4000W32_LOG;


static void
et4000w32_log(const char *fmt, ...)
{
    va_list ap;

    if (et4000w32_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define et4000w32_log(fmt, ...)
#endif


void
et4000w32p_out(uint16_t addr, uint8_t val, void *p)
{
    et4000w32p_t *et4000 = (et4000w32p_t *)p;
    svga_t *svga = &et4000->svga;
    uint8_t old;
    uint32_t add2addr = 0;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1)) 
	addr ^= 0x60;

    switch (addr) {
	case 0x3c2:
		if (et4000->type == ET4000W32P_DIAMOND)
			icd2061_write(svga->clock_gen, (val >> 2) & 3);
		break;

	case 0x3c6: case 0x3c7: case 0x3c8: case 0x3c9:
		if (et4000->type <= ET4000W32P_REVC)
			sdac_ramdac_out(addr, 0, val, svga->ramdac, svga);
		else
			stg_ramdac_out(addr, val, svga->ramdac, svga);
		return;

	case 0x3cb:	/* Banking extension */
		if (!(svga->crtc[0x36] & 0x10) && !(svga->gdcreg[6] & 0x08)) {
			svga->write_bank = (svga->write_bank & 0xfffff) | ((val & 1) << 20);
			svga->read_bank  = (svga->read_bank  & 0xfffff) | ((val & 0x10) << 16);
		}
		et4000->banking2 = val;
		return;
	case 0x3cd:	/* Banking */
		if (!(svga->crtc[0x36] & 0x10) && !(svga->gdcreg[6] & 0x08)) {
			svga->write_bank = (svga->write_bank & 0x100000) | ((val & 0xf) * 65536);
			svga->read_bank  = (svga->read_bank  & 0x100000) | (((val >> 4) & 0xf) * 65536);
		}
		et4000->banking = val;
		return;
	case 0x3cf:
		switch (svga->gdcaddr & 15) {
			case 6:
				if (!(svga->crtc[0x36] & 0x10) && !(val & 0x08)) {
					svga->write_bank = ((et4000->banking2 & 1) << 20) | ((et4000->banking & 0xf) * 65536);
					svga->read_bank  = ((et4000->banking2 & 0x10) << 16) | (((et4000->banking >> 4) & 0xf) * 65536);
				} else
					svga->write_bank = svga->read_bank = 0;

				svga->gdcreg[svga->gdcaddr & 15] = val;
				et4000w32p_recalcmapping(et4000);
				return;
		}
		break;
	case 0x3d4:
		svga->crtcreg = val & 63;
		return;
	case 0x3d5:
		if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
			return;
		if ((svga->crtcreg == 0x35) && (svga->crtc[0x11] & 0x80))
			return;
		if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
			val = (svga->crtc[7] & ~0x10) | (val & 0x10);
		old = svga->crtc[svga->crtcreg];
		svga->crtc[svga->crtcreg] = val;
		if (svga->crtcreg == 0x36) {
			if (!(val & 0x10) && !(svga->gdcreg[6] & 0x08)) {
				svga->write_bank = ((et4000->banking2 & 1) << 20) | ((et4000->banking & 0xf) * 65536);
				svga->read_bank  = ((et4000->banking2 & 0x10) << 16) | (((et4000->banking >> 4) & 0xf) * 65536);
			} else
				svga->write_bank = svga->read_bank = 0;
		}
		if (old != val) {
                        if (svga->crtcreg < 0xe || svga->crtcreg > 0x10)
                        {
				if ((svga->crtcreg == 0xc) || (svga->crtcreg == 0xd)) {
                                	svga->fullchange = 3;
					svga->ma_latch = ((svga->crtc[0xc] << 8) | svga->crtc[0xd]) + ((svga->crtc[8] & 0x60) >> 5);
				} else {
					svga->fullchange = changeframecount;
	                                svga_recalctimings(svga);
				}
                        }
		}
		if (svga->crtcreg == 0x30) {
			if (et4000->pci)
				et4000->linearbase = (et4000->linearbase & 0xc0000000) | ((val & 0xfc) << 22);
			else
				et4000->linearbase = val << 22;
			et4000w32p_recalcmapping(et4000);
		}
		if (svga->crtcreg == 0x32 || svga->crtcreg == 0x36)
			et4000w32p_recalcmapping(et4000);
		break;

	case 0x210a: case 0x211a: case 0x212a: case 0x213a:
	case 0x214a: case 0x215a: case 0x216a: case 0x217a:
		et4000->index = val;
		return;
	case 0x210b: case 0x211b: case 0x212b: case 0x213b:
	case 0x214b: case 0x215b: case 0x216b: case 0x217b:
		et4000->regs[et4000->index] = val;
		svga->hwcursor.xsize = svga->hwcursor.ysize = ((et4000->regs[0xEF] & 4) || (et4000->type == ET4000W32)) ? 128 : 64;
		svga->hwcursor.x     = et4000->regs[0xE0] | ((et4000->regs[0xE1] & 7) << 8);
		svga->hwcursor.y     = et4000->regs[0xE4] | ((et4000->regs[0xE5] & 7) << 8);
		svga->hwcursor.ena   = !!(et4000->regs[0xF7] & 0x80);
		svga->hwcursor.xoff  = et4000->regs[0xE2];
		svga->hwcursor.yoff  = et4000->regs[0xE6];

		if (et4000->type == ET4000W32) {
			switch (svga->bpp) {
				case 8:
					svga->hwcursor.xoff += 32;
					break;			
			}
		}

		if (svga->hwcursor.xsize == 128) {
			svga->hwcursor.xoff &= 0x7f;
			svga->hwcursor.yoff &= 0x7f;
		} else {
			svga->hwcursor.xoff &= 0x3f;
			svga->hwcursor.yoff &= 0x3f;
		}
		svga->hwcursor.addr  = (et4000->regs[0xe8] | (et4000->regs[0xe9] << 8) | ((et4000->regs[0xea] & 7) << 16)) << 2;

		add2addr = svga->hwcursor.yoff * ((svga->hwcursor.xsize == 128) ? 32 : 16);
		svga->hwcursor.addr += add2addr;
		return;
    }

    svga_out(addr, val, svga);
}


uint8_t
et4000w32p_in(uint16_t addr, void *p)
{
    et4000w32p_t *et4000 = (et4000w32p_t *)p;
    svga_t *svga = &et4000->svga;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1)) 
	addr ^= 0x60;

    switch (addr) {
	case 0x3c5:
		if ((svga->seqaddr & 0xf) == 7) 
			return svga->seqregs[svga->seqaddr & 0xf] | 4;
		break;

	case 0x3c6: case 0x3c7: case 0x3c8: case 0x3c9:
		if (et4000->type <= ET4000W32P_REVC)
			return sdac_ramdac_in(addr, 0, svga->ramdac, svga);
		else
			return stg_ramdac_in(addr, svga->ramdac, svga);
		break;

	case 0x3cb:
		return et4000->banking2;
	case 0x3cd:
		return et4000->banking;
	case 0x3d4:
		return svga->crtcreg;
	case 0x3d5:
		return svga->crtc[svga->crtcreg];

	case 0x210a: case 0x211a: case 0x212a: case 0x213a:
	case 0x214a: case 0x215a: case 0x216a: case 0x217a:
		return et4000->index;
		case 0x210B: case 0x211B: case 0x212B: case 0x213B:
		case 0x214B: case 0x215B: case 0x216B: case 0x217B:
		if (et4000->index == 0xec)
			return (et4000->regs[0xec] & 0xf) | (et4000->rev << 4);
		if (et4000->index == 0xee) {	/* Preliminary implementation */
			if (svga->bpp == 8)
				return 3;
			else if (svga->bpp == 16)
				return 4;
			else
				break;
		}
		if (et4000->index == 0xef)  {
			if (et4000->type >= ET4000W32P_REVC)
				return (et4000->regs[0xef] & 0x0f) | (et4000->rev << 4) | et4000->pci;
			else
				return (et4000->regs[0xef] & 0x8f) | (et4000->rev << 4) | et4000->vlb;
		}
		return et4000->regs[et4000->index];
    }

    return svga_in(addr, svga);
}


void
et4000w32p_recalctimings(svga_t *svga)
{
    et4000w32p_t *et4000 = (et4000w32p_t *)svga->p;

    svga->ma_latch |= (svga->crtc[0x33] & 0x7) << 16;
    if (svga->crtc[0x35] & 0x01)	svga->vblankstart += 0x400;
    if (svga->crtc[0x35] & 0x02)	svga->vtotal      += 0x400;
    if (svga->crtc[0x35] & 0x04)	svga->dispend     += 0x400;
    if (svga->crtc[0x35] & 0x08)	svga->vsyncstart  += 0x400;
    if (svga->crtc[0x35] & 0x10)	svga->split       += 0x400;
    if (svga->crtc[0x3F] & 0x80)	svga->rowoffset   += 0x100;
    if (svga->crtc[0x3F] & 0x01)	svga->htotal      += 256;
    if (svga->attrregs[0x16] & 0x20)	svga->hdisp <<= 1;

    svga->clock = (cpuclock * (double)(1ull << 32)) / svga->getclock((svga->miscout >> 2) & 3, svga->clock_gen);

    if (svga->adv_flags & FLAG_NOSKEW) {
		/* On the Cardex ET4000/W32p-based cards, adjust text mode clocks by 1. */
		if (!(svga->gdcreg[6] & 1) && !(svga->attrregs[0x10] & 1)) {	/* Text mode */
			svga->ma_latch--;

			if ((svga->seqregs[1] & 8)) /*40 column*/
				svga->hdisp += (svga->seqregs[1] & 1) ? 16 : 18;
			else
				svga->hdisp += (svga->seqregs[1] & 1) ? 8 : 9;
		} else {
			/* Also adjust the graphics mode clocks in some cases. */
			if ((svga->gdcreg[5] & 0x40) && (svga->bpp != 32)) {
				if ((svga->bpp == 15) || (svga->bpp == 16) || (svga->bpp == 24))
					svga->hdisp += (svga->seqregs[1] & 1) ? 16 : 18;
				else
					svga->hdisp += (svga->seqregs[1] & 1) ? 8 : 9;
			} else if ((svga->gdcreg[5] & 0x40) == 0)
				svga->hdisp += (svga->seqregs[1] & 1) ? 8 : 9;
		}
    }

	if (et4000->type == ET4000W32) {
		if ((svga->gdcreg[6] & 1) || (svga->attrregs[0x10] & 1)) {
			if (svga->gdcreg[5] & 0x40) {
				switch (svga->bpp) {
					case 8:
						if (svga->hdisp == 640 || svga->hdisp == 800 || svga->hdisp == 1024)
							break;
						svga->hdisp -= 24;
						break;
				}
			}
		}
	}

    et4000->adjust_cursor = 0;
	
	switch (svga->bpp) {
	case 15: case 16:
		svga->hdisp >>= 1;
		if (et4000->type <= ET4000W32P_REVC)
			et4000->adjust_cursor = 1;
		break;
	case 24:
		svga->hdisp /= 3;
		if (et4000->type <= ET4000W32P_REVC)
			et4000->adjust_cursor = 2;
		break;
	}

    svga->render = svga_render_blank;
    if (!svga->scrblank && svga->attr_palette_enable) {
	if (!(svga->gdcreg[6] & 1) && !(svga->attrregs[0x10] & 1)) {	/* Text mode */
		if (svga->seqregs[1] & 8)	/* 40 column */
			svga->render = svga_render_text_40;
		else
			svga->render = svga_render_text_80;
	} else {
		if (svga->adv_flags & FLAG_NOSKEW)
			svga->ma_latch--;

		switch (svga->gdcreg[5] & 0x60) {
			case 0x00: 
				if (svga->seqregs[1] & 8)	/* Low res (320) */
					svga->render = svga_render_4bpp_lowres;
				else
					svga->render = svga_render_4bpp_highres;
				break;
			case 0x20:				/* 4 colours */
				if (svga->seqregs[1] & 8) /*Low res (320)*/
					svga->render = svga_render_2bpp_lowres;
				else
					svga->render = svga_render_2bpp_highres;
				break;
			case 0x40: case 0x60:			/* 256+ colours */
				if (et4000->type <= ET4000W32P_REVC)
					svga->clock /= 2;

				switch (svga->bpp) {
					case 8:
						svga->map8 = svga->pallook;
						if (svga->lowres)
							svga->render = svga_render_8bpp_lowres;
						else
							svga->render = svga_render_8bpp_highres;
						break;
					case 15:
						if (svga->lowres || (svga->seqregs[1] & 8))
							svga->render = svga_render_15bpp_lowres;
						else
							svga->render = svga_render_15bpp_highres;
						break;
					case 16:
						if (svga->lowres || (svga->seqregs[1] & 8))
							svga->render = svga_render_16bpp_lowres;
						else
							svga->render = svga_render_16bpp_highres;
						break;
					case 17:
						if (svga->lowres || (svga->seqregs[1] & 8))
							svga->render = svga_render_15bpp_mix_lowres;
						else
							svga->render = svga_render_15bpp_mix_highres;
						break;
					case 24:
						if (svga->lowres || (svga->seqregs[1] & 8))
							svga->render = svga_render_24bpp_lowres;
						else
							svga->render = svga_render_24bpp_highres;
						break;
					case 32:
						if (svga->lowres || (svga->seqregs[1] & 8))
							svga->render = svga_render_32bpp_lowres;
						else
							svga->render = svga_render_32bpp_highres;
						break;
				}
				break;
		}
	}
    }
}


void
et4000w32p_recalcmapping(et4000w32p_t *et4000)
{
    svga_t *svga = &et4000->svga;
    int map;

    if (et4000->pci && !(et4000->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM)) {
	mem_mapping_disable(&svga->mapping);
	mem_mapping_disable(&et4000->linear_mapping);
	mem_mapping_disable(&et4000->mmu_mapping);
	return;
    }

    if (svga->crtc[0x36] & 0x10) {	/* Linear frame buffer */
	mem_mapping_set_addr(&et4000->linear_mapping, et4000->linearbase, 0x200000);
	mem_mapping_disable(&svga->mapping);
	mem_mapping_disable(&et4000->mmu_mapping);
    } else {
	map = (svga->gdcreg[6] & 0xc) >> 2;
	if (svga->crtc[0x36] & 0x20)	map |= 4;
	if (svga->crtc[0x36] & 0x08)	map |= 8;
	switch (map) {
		case 0x0: case 0x4: case 0x8: case 0xc:	/* 128k at A0000 */
			mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x20000);
			mem_mapping_disable(&et4000->mmu_mapping);
			svga->banked_mask = 0x1ffff;
			break;
		case 0x1:				/* 64k at A0000 */
			mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
			mem_mapping_disable(&et4000->mmu_mapping);
			svga->banked_mask = 0xffff;
			break;
		case 0x2:				/* 32k at B0000 */
			mem_mapping_set_addr(&svga->mapping, 0xb0000, 0x08000);
			mem_mapping_disable(&et4000->mmu_mapping);
			svga->banked_mask = 0x7fff;
			break;
		case 0x3:				/* 32k at B8000 */
			mem_mapping_set_addr(&svga->mapping, 0xb8000, 0x08000);
			mem_mapping_disable(&et4000->mmu_mapping);
			svga->banked_mask = 0x7fff;
			break;
		case 0x5: case 0x9: case 0xd:		/* 64k at A0000, MMU at B8000 */
			mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
			mem_mapping_set_addr(&et4000->mmu_mapping, 0xb8000, 0x08000);
			svga->banked_mask = 0xffff;
			break;
		case 0x6: case 0xa: case 0xe:		/* 32k at B0000, MMU at A8000 */
			mem_mapping_set_addr(&svga->mapping, 0xb0000, 0x08000);
			mem_mapping_set_addr(&et4000->mmu_mapping, 0xa8000, 0x08000);
			svga->banked_mask = 0x7fff;
			break;
		case 0x7: case 0xb: case 0xf:		/* 32k at B8000, MMU at A8000 */
			mem_mapping_set_addr(&svga->mapping, 0xb8000, 0x08000);
			mem_mapping_set_addr(&et4000->mmu_mapping, 0xa8000, 0x08000);
			svga->banked_mask = 0x7fff;
			break;
	}

	mem_mapping_disable(&et4000->linear_mapping);
    }

    et4000->linearbase_old = et4000->linearbase;

    if (!et4000->interleaved && (svga->crtc[0x32] & 0x80))
	mem_mapping_disable(&svga->mapping);
}


static void
et4000w32p_accel_write_fifo(et4000w32p_t *et4000, uint32_t addr, uint8_t val)
{
    switch (addr & 0x7fff) {
	case 0x7f80:	et4000->acl.queued.pattern_addr = (et4000->acl.queued.pattern_addr & 0xFFFFFF00) | val;		break;
	case 0x7f81:	et4000->acl.queued.pattern_addr = (et4000->acl.queued.pattern_addr & 0xFFFF00FF) | (val << 8);	break;
	case 0x7f82:	et4000->acl.queued.pattern_addr = (et4000->acl.queued.pattern_addr & 0xFF00FFFF) | (val << 16);	break;
	case 0x7f83:	et4000->acl.queued.pattern_addr = (et4000->acl.queued.pattern_addr & 0x00FFFFFF) | (val << 24);	break;
	case 0x7f84:	et4000->acl.queued.source_addr  = (et4000->acl.queued.source_addr  & 0xFFFFFF00) | val;		break;
	case 0x7f85:	et4000->acl.queued.source_addr  = (et4000->acl.queued.source_addr  & 0xFFFF00FF) | (val << 8);	break;
	case 0x7f86:	et4000->acl.queued.source_addr  = (et4000->acl.queued.source_addr  & 0xFF00FFFF) | (val << 16);	break;
	case 0x7f87:	et4000->acl.queued.source_addr  = (et4000->acl.queued.source_addr  & 0x00FFFFFF) | (val << 24);	break;
	case 0x7f88:	et4000->acl.queued.pattern_off  = (et4000->acl.queued.pattern_off  & 0xFF00) | val;		break;
	case 0x7f89:	et4000->acl.queued.pattern_off  = (et4000->acl.queued.pattern_off  & 0x00FF) | (val << 8);	break;
	case 0x7f8a:	et4000->acl.queued.source_off   = (et4000->acl.queued.source_off   & 0xFF00) | val;		break;
	case 0x7f8b:	et4000->acl.queued.source_off   = (et4000->acl.queued.source_off   & 0x00FF) | (val << 8);	break;
	case 0x7f8c:	et4000->acl.queued.dest_off     = (et4000->acl.queued.dest_off     & 0xFF00) | val;		break;
	case 0x7f8d:	et4000->acl.queued.dest_off     = (et4000->acl.queued.dest_off     & 0x00FF) | (val << 8);	break;
	case 0x7f8e:
		if (et4000->type >= ET4000W32P_REVC) 
			et4000->acl.queued.pixel_depth = val;
		else
			et4000->acl.queued.vbus = val; 
		break;
	case 0x7f8f:	et4000->acl.queued.xy_dir = val;	break;
	case 0x7f90:	et4000->acl.queued.pattern_wrap = val;	break;
	case 0x7f92:	et4000->acl.queued.source_wrap  = val;	break;
	case 0x7f98:	et4000->acl.queued.count_x    = (et4000->acl.queued.count_x & 0xFF00) | val;			break;
	case 0x7f99:	et4000->acl.queued.count_x    = (et4000->acl.queued.count_x & 0x00FF) | (val << 8);		break;
	case 0x7f9a:	et4000->acl.queued.count_y    = (et4000->acl.queued.count_y & 0xFF00) | val;			break;
	case 0x7f9b:	et4000->acl.queued.count_y    = (et4000->acl.queued.count_y & 0x00FF) | (val << 8);		break;
	case 0x7f9c:	et4000->acl.queued.ctrl_routing = val;	break;
	case 0x7f9d:	et4000->acl.queued.ctrl_reload  = val;	break;
	case 0x7f9e:	et4000->acl.queued.rop_bg       = val;	break;
	case 0x7f9f:	et4000->acl.queued.rop_fg       = val;	break;
	case 0x7fa0:	et4000->acl.queued.dest_addr = (et4000->acl.queued.dest_addr & 0xFFFFFF00) | val;		break;
	case 0x7fa1:	et4000->acl.queued.dest_addr = (et4000->acl.queued.dest_addr & 0xFFFF00FF) | (val << 8);	break;
	case 0x7fa2:	et4000->acl.queued.dest_addr = (et4000->acl.queued.dest_addr & 0xFF00FFFF) | (val << 16);	break;
	case 0x7fa3:	et4000->acl.queued.dest_addr = (et4000->acl.queued.dest_addr & 0x00FFFFFF) | (val << 24);
		et4000->acl.internal = et4000->acl.queued;
		if (et4000->type >= ET4000W32P_REVC) {
			et4000w32p_blit_start(et4000);
			if (!(et4000->acl.queued.ctrl_routing & 0x43))
				et4000w32p_blit(0xffffff, ~0, 0, 0, et4000);
			if ((et4000->acl.queued.ctrl_routing & 0x40) && !(et4000->acl.internal.ctrl_routing & 3))
				et4000w32p_blit(4, ~0, 0, 0, et4000);
		} else {
			et4000w32_blit_start(et4000);
			et4000->acl.cpu_input_num = 0;
			if (!(et4000->acl.queued.ctrl_routing & 0x37))
				et4000w32p_blit(0xffffff, ~0, 0, 0, et4000);
		}
		break;
	case 0x7fa4:	et4000->acl.queued.mix_addr = (et4000->acl.queued.mix_addr & 0xFFFFFF00) | val;			break;
	case 0x7fa5:	et4000->acl.queued.mix_addr = (et4000->acl.queued.mix_addr & 0xFFFF00FF) | (val << 8);		break;
	case 0x7fa6:	et4000->acl.queued.mix_addr = (et4000->acl.queued.mix_addr & 0xFF00FFFF) | (val << 16);		break;
	case 0x7fa7:	et4000->acl.queued.mix_addr = (et4000->acl.queued.mix_addr & 0x00FFFFFF) | (val << 24);		break;
	case 0x7fa8:	et4000->acl.queued.mix_off = (et4000->acl.queued.mix_off & 0xFF00) | val;			break;
	case 0x7fa9:	et4000->acl.queued.mix_off = (et4000->acl.queued.mix_off & 0x00FF) | (val << 8);		break;
	case 0x7faa:	et4000->acl.queued.error   = (et4000->acl.queued.error   & 0xFF00) | val;			break;
	case 0x7fab:	et4000->acl.queued.error   = (et4000->acl.queued.error   & 0x00FF) | (val << 8);		break;
	case 0x7fac:	et4000->acl.queued.dmin    = (et4000->acl.queued.dmin    & 0xFF00) | val;			break;
	case 0x7fad:	et4000->acl.queued.dmin    = (et4000->acl.queued.dmin    & 0x00FF) | (val << 8);		break;
	case 0x7fae:	et4000->acl.queued.dmaj    = (et4000->acl.queued.dmaj    & 0xFF00) | val;			break;
	case 0x7faf:	et4000->acl.queued.dmaj    = (et4000->acl.queued.dmaj    & 0x00FF) | (val << 8);		break;
    }
}


static void
et4000w32p_accel_write_mmu(et4000w32p_t *et4000, uint32_t addr, uint8_t val)
{
    if (et4000->type >= ET4000W32P_REVC) {
	if (!(et4000->acl.status & ACL_XYST))
		return;
	if (et4000->acl.internal.ctrl_routing & 3) {
		if ((et4000->acl.internal.ctrl_routing & 3) == 2) {
			if (et4000->acl.mix_addr & 7)
				et4000w32p_blit(8 - (et4000->acl.mix_addr & 7), val >> (et4000->acl.mix_addr & 7), 0, 1, et4000);
			else
				et4000w32p_blit(8, val, 0, 1, et4000);
		}
		else if ((et4000->acl.internal.ctrl_routing & 3) == 1)
			et4000w32p_blit(1, ~0, val, 2, et4000);
	}
    } else {
	if (!(et4000->acl.status & ACL_XYST)) {
		et4000->acl.queued.dest_addr = (addr & 0x1FFF) + et4000->mmu.base[et4000->bank];
		et4000->acl.internal = et4000->acl.queued;
		et4000w32_blit_start(et4000);
		if (!(et4000->acl.internal.ctrl_routing & 0x37))
			et4000w32p_blit(0xFFFFFF, ~0, 0, 0, et4000);
		et4000->acl.cpu_input_num = 0;
	}

	if (et4000->acl.internal.ctrl_routing & 7) {
		et4000->acl.cpu_input = (et4000->acl.cpu_input &~ (0xFF << (et4000->acl.cpu_input_num << 3))) |
					(val << (et4000->acl.cpu_input_num << 3));
		et4000->acl.cpu_input_num++;

		if (et4000->acl.cpu_input_num == et4000w32_vbus[et4000->acl.internal.vbus & 3]) {
			if ((et4000->acl.internal.ctrl_routing & 7) == 2)
				et4000w32p_blit(et4000->acl.cpu_input_num << 3, et4000->acl.cpu_input, 0, 1, et4000);
			else if ((et4000->acl.internal.ctrl_routing & 7) == 1)
				et4000w32p_blit(et4000->acl.cpu_input_num, ~0, et4000->acl.cpu_input, 2, et4000);
			else if ((et4000->acl.internal.ctrl_routing & 7) == 4)
				et4000w32p_blit(et4000->acl.cpu_input_num, ~0, et4000->acl.internal.count_x, 2, et4000);
			else if ((et4000->acl.internal.ctrl_routing & 7) == 5)
				et4000w32p_blit(et4000->acl.cpu_input_num, ~0, et4000->acl.internal.count_y, 2, et4000);

			et4000->acl.cpu_input_num = 0;
		}
	}
    }
}


void
et4000w32p_mmu_write(uint32_t addr, uint8_t val, void *p)
{
    et4000w32p_t *et4000 = (et4000w32p_t *)p;
    svga_t *svga = &et4000->svga;

    switch (addr & 0x6000) {
	case 0x0000:	/* MMU 0 */
	case 0x2000:	/* MMU 1 */
	case 0x4000:	/* MMU 2 */
		et4000->bank = (addr >> 13) & 3;
		if (et4000->mmu.ctrl & (1 << et4000->bank)) {
			et4000w32p_accel_write_mmu(et4000, addr & 0x7fff, val);
		} else {
			if (((addr & 0x1fff) + et4000->mmu.base[et4000->bank]) < svga->vram_max) {
				svga->vram[(addr & 0x1fff) + et4000->mmu.base[et4000->bank]] = val;
				svga->changedvram[((addr & 0x1fff) + et4000->mmu.base[et4000->bank]) >> 12] = changeframecount;
			}
		}
		break;
	case 0x6000:
		if ((addr & 0x7fff) >= 0x7f80) {
			et4000w32p_accel_write_fifo(et4000, addr & 0x7fff, val);
		} else switch (addr & 0x7fff) {
			case 0x7f00:	et4000->mmu.base[0] = (et4000->mmu.base[0] & 0xFFFFFF00) | val;		break;
			case 0x7f01:	et4000->mmu.base[0] = (et4000->mmu.base[0] & 0xFFFF00FF) | (val << 8);	break;
                        case 0x7f02:	et4000->mmu.base[0] = (et4000->mmu.base[0] & 0xFF00FFFF) | (val << 16);	break;
			case 0x7f03:	et4000->mmu.base[0] = (et4000->mmu.base[0] & 0x00FFFFFF) | (val << 24);	break;
			case 0x7f04:	et4000->mmu.base[1] = (et4000->mmu.base[1] & 0xFFFFFF00) | val;		break;
			case 0x7f05:	et4000->mmu.base[1] = (et4000->mmu.base[1] & 0xFFFF00FF) | (val << 8);	break;
			case 0x7f06:	et4000->mmu.base[1] = (et4000->mmu.base[1] & 0xFF00FFFF) | (val << 16);	break;
			case 0x7f07:	et4000->mmu.base[1] = (et4000->mmu.base[1] & 0x00FFFFFF) | (val << 24);	break;
			case 0x7f08:	et4000->mmu.base[2] = (et4000->mmu.base[2] & 0xFFFFFF00) | val;		break;
			case 0x7f09:	et4000->mmu.base[2] = (et4000->mmu.base[2] & 0xFFFF00FF) | (val << 8);	break;
			case 0x7f0a:	et4000->mmu.base[2] = (et4000->mmu.base[2] & 0xFF00FFFF) | (val << 16);	break;
			case 0x7f0b:	et4000->mmu.base[2] = (et4000->mmu.base[2] & 0x00FFFFFF) | (val << 24);	break;
			case 0x7f13:	et4000->mmu.ctrl = val;	break;
		}
		break;
    }
}


uint8_t
et4000w32p_mmu_read(uint32_t addr, void *p)
{
    et4000w32p_t *et4000 = (et4000w32p_t *)p;
    svga_t *svga = &et4000->svga;
    int bank;
    uint8_t temp;

    switch (addr & 0x6000) {
	case 0x0000:	/* MMU 0 */
	case 0x2000:	/* MMU 1 */
	case 0x4000:	/* MMU 2 */
		bank = (addr >> 13) & 3;
		if (et4000->mmu.ctrl & (1 << bank)) {
			temp = 0xff;
			if (et4000->acl.cpu_dat_pos) {
				et4000->acl.cpu_dat_pos--;
				temp = et4000->acl.cpu_dat & 0xff;
				et4000->acl.cpu_dat >>= 8;
			}
			if ((et4000->acl.queued.ctrl_routing & 0x40) && !et4000->acl.cpu_dat_pos && !(et4000->acl.internal.ctrl_routing & 3))
				et4000w32p_blit(4, ~0, 0, 0, et4000);

			/* ???? */
			return temp;
		}

		if ((addr&0x1fff) + et4000->mmu.base[bank] >= svga->vram_max)
			return 0xff;

		return svga->vram[(addr&0x1fff) + et4000->mmu.base[bank]];

	case 0x6000:
		switch (addr & 0x7fff) {
			case 0x7f00:	return et4000->mmu.base[0];
			case 0x7f01:	return et4000->mmu.base[0] >> 8;
			case 0x7f02:	return et4000->mmu.base[0] >> 16;
			case 0x7f03:	return et4000->mmu.base[0] >> 24;
			case 0x7f04:	return et4000->mmu.base[1];
			case 0x7f05:	return et4000->mmu.base[1] >> 8;
			case 0x7f06:	return et4000->mmu.base[1] >> 16;
			case 0x7f07:	return et4000->mmu.base[1] >> 24;
			case 0x7f08:	return et4000->mmu.base[2];
			case 0x7f09:	return et4000->mmu.base[2] >> 8;
			case 0x7f0a:	return et4000->mmu.base[2] >> 16;
			case 0x7f0b:	return et4000->mmu.base[2] >> 24;
			case 0x7f13:	return et4000->mmu.ctrl;

			case 0x7f36:
				if (et4000->type >= ET4000W32P_REVC) {
					temp = et4000->acl.status;
					temp &= ~(ACL_RDST | ACL_WRST);
					if (temp == ACL_XYST && (et4000->acl.internal.ctrl_routing == 1 || et4000->acl.internal.ctrl_routing == 2))
						temp |= ACL_RDST;
				} else {
					et4000->acl.status &= ~(ACL_XYST | ACL_SSO);
					temp = et4000->acl.status;
				}
				return temp;

			case 0x7f80:	return et4000->acl.internal.pattern_addr;
			case 0x7f81:	return et4000->acl.internal.pattern_addr >> 8;
			case 0x7f82:	return et4000->acl.internal.pattern_addr >> 16;
			case 0x7f83:	return et4000->acl.internal.pattern_addr >> 24;
			case 0x7f84:	return et4000->acl.internal.source_addr;
			case 0x7f85:	return et4000->acl.internal.source_addr >> 8;
			case 0x7f86:	return et4000->acl.internal.source_addr >> 16;
			case 0x7f87:	return et4000->acl.internal.source_addr >> 24;
			case 0x7f88:	return et4000->acl.internal.pattern_off;
			case 0x7f89:	return et4000->acl.internal.pattern_off >> 8;
			case 0x7f8a:	return et4000->acl.internal.source_off;
			case 0x7f8b:	return et4000->acl.internal.source_off >> 8;
			case 0x7f8c:	return et4000->acl.internal.dest_off;
			case 0x7f8d:	return et4000->acl.internal.dest_off >> 8;
			case 0x7f8e: 
				if (et4000->type >= ET4000W32P_REVC) 
					return et4000->acl.internal.pixel_depth;
				else
					return et4000->acl.internal.vbus;
				break;
			case 0x7f8f:	return et4000->acl.internal.xy_dir;
			case 0x7f90:	return et4000->acl.internal.pattern_wrap;
			case 0x7f92:	return et4000->acl.internal.source_wrap;
			case 0x7f98:	return et4000->acl.internal.count_x;
			case 0x7f99:	return et4000->acl.internal.count_x >> 8;
			case 0x7f9a:	return et4000->acl.internal.count_y;
			case 0x7f9b:	return et4000->acl.internal.count_y >> 8;
			case 0x7f9c:	return et4000->acl.internal.ctrl_routing;
			case 0x7f9d:	return et4000->acl.internal.ctrl_reload;
			case 0x7f9e:	return et4000->acl.internal.rop_bg;
			case 0x7f9f:	return et4000->acl.internal.rop_fg;
			case 0x7fa0:	return et4000->acl.internal.dest_addr;
			case 0x7fa1:	return et4000->acl.internal.dest_addr >> 8;
			case 0x7fa2:	return et4000->acl.internal.dest_addr >> 16;
			case 0x7fa3:	return et4000->acl.internal.dest_addr >> 24;
	}

	return 0xff;
    }

    return 0xff;
}


void
et4000w32_blit_start(et4000w32p_t *et4000)
{
    et4000->acl.pattern_addr	= et4000->acl.internal.pattern_addr;
    et4000->acl.source_addr	= et4000->acl.internal.source_addr;
    et4000->acl.dest_addr	= et4000->acl.internal.dest_addr;
    et4000->acl.dest_back	= et4000->acl.dest_addr;
    et4000->acl.internal.pos_x	= et4000->acl.internal.pos_y = 0;
    et4000->acl.pattern_x	= et4000->acl.source_x = et4000->acl.pattern_y = et4000->acl.source_y = 0;

    et4000->acl.status |= ACL_XYST;

    if (!(et4000->acl.internal.ctrl_routing & 7))
	et4000->acl.status |= ACL_SSO;

    if (et4000w32_wrap_x[et4000->acl.internal.pattern_wrap & 7]) {
	et4000->acl.pattern_x = et4000->acl.pattern_addr & et4000w32_wrap_x[et4000->acl.internal.pattern_wrap & 7];
	et4000->acl.pattern_addr &= ~et4000w32_wrap_x[et4000->acl.internal.pattern_wrap & 7];
    }
    et4000->acl.pattern_back = et4000->acl.pattern_addr;
    if (!(et4000->acl.internal.pattern_wrap & 0x40)) {
	et4000->acl.pattern_y = (et4000->acl.pattern_addr / (et4000w32_wrap_x[et4000->acl.internal.pattern_wrap & 7] + 1)) & (et4000w32_wrap_y[(et4000->acl.internal.pattern_wrap >> 4) & 7] - 1);
	et4000->acl.pattern_back &= ~(((et4000w32_wrap_x[et4000->acl.internal.pattern_wrap & 7] + 1) * et4000w32_wrap_y[(et4000->acl.internal.pattern_wrap >> 4) & 7]) - 1);
    }
    et4000->acl.pattern_x_back = et4000->acl.pattern_x;

    if (et4000w32_wrap_x[et4000->acl.internal.source_wrap & 7]) {
	et4000->acl.source_x = et4000->acl.source_addr & et4000w32_wrap_x[et4000->acl.internal.source_wrap & 7];
	et4000->acl.source_addr &= ~et4000w32_wrap_x[et4000->acl.internal.source_wrap & 7];
    }

    et4000->acl.source_back = et4000->acl.source_addr;

    if (!(et4000->acl.internal.source_wrap & 0x40)) {
	et4000->acl.source_y = (et4000->acl.source_addr / (et4000w32_wrap_x[et4000->acl.internal.source_wrap & 7] + 1)) & (et4000w32_wrap_y[(et4000->acl.internal.source_wrap >> 4) & 7] - 1);
	et4000->acl.source_back &= ~(((et4000w32_wrap_x[et4000->acl.internal.source_wrap & 7] + 1) * et4000w32_wrap_y[(et4000->acl.internal.source_wrap >> 4) & 7]) - 1);
    }
    et4000->acl.source_x_back = et4000->acl.source_x;
}


void
et4000w32p_blit_start(et4000w32p_t *et4000)
{
    if (!(et4000->acl.queued.xy_dir & 0x20))
	et4000->acl.internal.error	= et4000->acl.internal.dmaj / 2;
    et4000->acl.pattern_addr	= et4000->acl.internal.pattern_addr;
    et4000->acl.source_addr	= et4000->acl.internal.source_addr;
    et4000->acl.mix_addr	= et4000->acl.internal.mix_addr;
    et4000->acl.mix_back	= et4000->acl.mix_addr;
    et4000->acl.dest_addr	= et4000->acl.internal.dest_addr;
    et4000->acl.dest_back	= et4000->acl.dest_addr;
    et4000->acl.internal.pos_x	= et4000->acl.internal.pos_y = 0;
    et4000->acl.pattern_x	= et4000->acl.source_x = et4000->acl.pattern_y = et4000->acl.source_y = 0;
    et4000->acl.status |= ACL_XYST;
	et4000w32_log("ACL status XYST set\n");
    if ((!(et4000->acl.internal.ctrl_routing & 7) || (et4000->acl.internal.ctrl_routing & 4)) && !(et4000->acl.internal.ctrl_routing & 0x40)) 
	et4000->acl.status |= ACL_SSO;

    if (et4000w32_wrap_x[et4000->acl.internal.pattern_wrap & 7]) {
	et4000->acl.pattern_x = et4000->acl.pattern_addr & et4000w32_wrap_x[et4000->acl.internal.pattern_wrap & 7];
	et4000->acl.pattern_addr &= ~et4000w32_wrap_x[et4000->acl.internal.pattern_wrap & 7];
    }
    et4000->acl.pattern_back = et4000->acl.pattern_addr;
    if (!(et4000->acl.internal.pattern_wrap & 0x40)) {
	et4000->acl.pattern_y = (et4000->acl.pattern_addr / (et4000w32_wrap_x[et4000->acl.internal.pattern_wrap & 7] + 1)) & (et4000w32_wrap_y[(et4000->acl.internal.pattern_wrap >> 4) & 7] - 1);
	et4000->acl.pattern_back &= ~(((et4000w32_wrap_x[et4000->acl.internal.pattern_wrap & 7] + 1) * et4000w32_wrap_y[(et4000->acl.internal.pattern_wrap >> 4) & 7]) - 1);
    }
    et4000->acl.pattern_x_back = et4000->acl.pattern_x;

    if (et4000w32_wrap_x[et4000->acl.internal.source_wrap & 7]) {
	et4000->acl.source_x = et4000->acl.source_addr & et4000w32_wrap_x[et4000->acl.internal.source_wrap & 7];
	et4000->acl.source_addr &= ~et4000w32_wrap_x[et4000->acl.internal.source_wrap & 7];
    }
    et4000->acl.source_back = et4000->acl.source_addr;
    if (!(et4000->acl.internal.source_wrap & 0x40)) {
	et4000->acl.source_y = (et4000->acl.source_addr / (et4000w32_wrap_x[et4000->acl.internal.source_wrap & 7] + 1)) & (et4000w32_wrap_y[(et4000->acl.internal.source_wrap >> 4) & 7] - 1);
	et4000->acl.source_back &= ~(((et4000w32_wrap_x[et4000->acl.internal.source_wrap & 7] + 1) * et4000w32_wrap_y[(et4000->acl.internal.source_wrap >> 4) & 7]) - 1);
    }
    et4000->acl.source_x_back = et4000->acl.source_x;

    et4000w32_max_x[2] = ((et4000->acl.internal.pixel_depth & 0x30) == 0x20) ? 3 : 4;

    et4000->acl.internal.count_x += (et4000->acl.internal.pixel_depth >> 4) & 3;
    et4000->acl.cpu_dat_pos = 0;
    et4000->acl.cpu_dat = 0;

    et4000->acl.pix_pos = 0;
}


void
et4000w32_incx(int c, et4000w32p_t *et4000)
{
    et4000->acl.dest_addr	+= c;
    et4000->acl.pattern_x	+= c;
    et4000->acl.source_x	+= c;
    et4000->acl.mix_addr	+= c;
    if (et4000->acl.pattern_x >= et4000w32_max_x[et4000->acl.internal.pattern_wrap & 7])
	et4000->acl.pattern_x		-= et4000w32_max_x[et4000->acl.internal.pattern_wrap & 7];
    if (et4000->acl.source_x  >= et4000w32_max_x[et4000->acl.internal.source_wrap  & 7])
	et4000->acl.source_x		-= et4000w32_max_x[et4000->acl.internal.source_wrap  & 7];
}


void
et4000w32_decx(int c, et4000w32p_t *et4000)
{
    et4000->acl.dest_addr	-= c;
    et4000->acl.pattern_x	-= c;
    et4000->acl.source_x	-= c;
    et4000->acl.mix_addr	-= c;
    if (et4000->acl.pattern_x < 0)
	et4000->acl.pattern_x		+= et4000w32_max_x[et4000->acl.internal.pattern_wrap & 7];
    if (et4000->acl.source_x  < 0)
	et4000->acl.source_x		+= et4000w32_max_x[et4000->acl.internal.source_wrap  & 7];
}


void
et4000w32_incy(et4000w32p_t *et4000)
{
    et4000->acl.pattern_addr	+= et4000->acl.internal.pattern_off + 1;
    et4000->acl.source_addr	+= et4000->acl.internal.source_off  + 1;
    et4000->acl.mix_addr	+= et4000->acl.internal.mix_off     + 1;
    et4000->acl.dest_addr	+= et4000->acl.internal.dest_off    + 1;
    et4000->acl.pattern_y++;
    if (et4000->acl.pattern_y == et4000w32_wrap_y[(et4000->acl.internal.pattern_wrap >> 4) & 7]) {
	et4000->acl.pattern_y		= 0;
	et4000->acl.pattern_addr	= et4000->acl.pattern_back;
    }
    et4000->acl.source_y++;
    if (et4000->acl.source_y == et4000w32_wrap_y[(et4000->acl.internal.source_wrap >> 4) & 7]) {
	et4000->acl.source_y		= 0;
	et4000->acl.source_addr		= et4000->acl.source_back;
    }
}


void
et4000w32_decy(et4000w32p_t *et4000)
{
    et4000->acl.pattern_addr	-= et4000->acl.internal.pattern_off + 1;
    et4000->acl.source_addr	-= et4000->acl.internal.source_off  + 1;
    et4000->acl.mix_addr	-= et4000->acl.internal.mix_off     + 1;
    et4000->acl.dest_addr	-= et4000->acl.internal.dest_off    + 1;
    et4000->acl.pattern_y--;
    if (et4000->acl.pattern_y < 0 && !(et4000->acl.internal.pattern_wrap & 0x40)) {
	et4000->acl.pattern_y		= et4000w32_wrap_y[(et4000->acl.internal.pattern_wrap >> 4) & 7] - 1;
	et4000->acl.pattern_addr	= et4000->acl.pattern_back + (et4000w32_wrap_x[et4000->acl.internal.pattern_wrap & 7] * (et4000w32_wrap_y[(et4000->acl.internal.pattern_wrap >> 4) & 7] - 1));
    }
    et4000->acl.source_y--;
    if (et4000->acl.source_y < 0 && !(et4000->acl.internal.source_wrap & 0x40)) {
	et4000->acl.source_y		= et4000w32_wrap_y[(et4000->acl.internal.source_wrap >> 4) & 7] - 1;
	et4000->acl.source_addr		= et4000->acl.source_back + (et4000w32_wrap_x[et4000->acl.internal.source_wrap & 7] *(et4000w32_wrap_y[(et4000->acl.internal.source_wrap >> 4) & 7] - 1));;
    }
}


void
et4000w32p_blit(int count, uint32_t mix, uint32_t sdat, int cpu_input, et4000w32p_t *et4000)
{
    svga_t *svga = &et4000->svga;
    int c, d;
    uint8_t pattern, source, dest, out;
    uint8_t rop;
    int mixdat;

    if (!(et4000->acl.status & ACL_XYST) && (et4000->type >= ET4000W32P_REVC))
	return;

    if (et4000->acl.internal.xy_dir & 0x80){	/* Line draw */
	while (count--) {
		et4000w32_log("%i,%i : ", et4000->acl.internal.pos_x, et4000->acl.internal.pos_y);
		pattern = svga->vram[(et4000->acl.pattern_addr + et4000->acl.pattern_x) & et4000->vram_mask];
		source  = svga->vram[(et4000->acl.source_addr  + et4000->acl.source_x)  & et4000->vram_mask];
		et4000w32_log("%06X %06X ", (et4000->acl.pattern_addr + et4000->acl.pattern_x) & et4000->vram_mask, (et4000->acl.source_addr + et4000->acl.source_x) & et4000->vram_mask);
		if (cpu_input == 2) {
			source = sdat & 0xff;
			sdat >>= 8;
		}
		dest = svga->vram[et4000->acl.dest_addr & et4000->vram_mask];
		out = 0;
		et4000w32_log("%06X   ", et4000->acl.dest_addr);
		if ((et4000->acl.internal.ctrl_routing & 0xa) == 8) {
			mixdat = svga->vram[(et4000->acl.mix_addr >> 3) & et4000->vram_mask] & (1 << (et4000->acl.mix_addr & 7));
			et4000w32_log("%06X %02X  ", et4000->acl.mix_addr, svga->vram[(et4000->acl.mix_addr >> 3) & et4000->vram_mask]);
		} else {
			mixdat = mix & 1;
			mix >>= 1; 
			mix |= 0x80000000;
		}
		et4000->acl.mix_addr++;
		rop = mixdat ? et4000->acl.internal.rop_fg : et4000->acl.internal.rop_bg;
		for (c = 0; c < 8; c++) {
			d = (dest & (1 << c)) ? 1 : 0;
			if (source & (1 << c))		d |= 2;
			if (pattern & (1 << c))		d |= 4;
			if (rop & (1 << d))		out |= (1 << c);
		}
		et4000w32_log("%06X = %02X\n", et4000->acl.dest_addr & et4000->vram_mask, out);
		if (!(et4000->acl.internal.ctrl_routing & 0x40)) {
			svga->vram[et4000->acl.dest_addr & et4000->vram_mask] = out;
			svga->changedvram[(et4000->acl.dest_addr & et4000->vram_mask) >> 12] = changeframecount;
		} else {
			et4000->acl.cpu_dat |= ((uint64_t)out << (et4000->acl.cpu_dat_pos * 8));
			et4000->acl.cpu_dat_pos++;
		}

		et4000->acl.pix_pos++;
		et4000->acl.internal.pos_x++;
		if (et4000->acl.pix_pos <= ((et4000->acl.internal.pixel_depth >> 4) & 3)) {
			if (et4000->acl.internal.xy_dir & 1)	et4000w32_decx(1, et4000);
			else					et4000w32_incx(1, et4000);
		} else {
			if (et4000->acl.internal.xy_dir & 1) 
				et4000w32_incx((et4000->acl.internal.pixel_depth >> 4) & 3, et4000);
			else                       
				et4000w32_decx((et4000->acl.internal.pixel_depth >> 4) & 3, et4000);
			et4000->acl.pix_pos = 0;

			/*Next pixel*/
			switch (et4000->acl.internal.xy_dir & 7) {
				case 0: case 1:	/* Y+ */
					et4000w32_incy(et4000);
					et4000->acl.internal.pos_y++;
					et4000->acl.internal.pos_x -= ((et4000->acl.internal.pixel_depth >> 4) & 3) + 1;
					break;
				case 2: case 3:	/* Y- */
					et4000w32_decy(et4000);
					et4000->acl.internal.pos_y++;
					et4000->acl.internal.pos_x -= ((et4000->acl.internal.pixel_depth >> 4) & 3) + 1;
					break;
				case 4: case 6:	/* X+ */
					et4000w32_incx(((et4000->acl.internal.pixel_depth >> 4) & 3) + 1, et4000);
					break;
				case 5: case 7:	/* X- */
					et4000w32_decx(((et4000->acl.internal.pixel_depth >> 4) & 3) + 1, et4000);
					break;
			}
			et4000->acl.internal.error += et4000->acl.internal.dmin;
			if (et4000->acl.internal.error > et4000->acl.internal.dmaj) {
				et4000->acl.internal.error -= et4000->acl.internal.dmaj;
				switch (et4000->acl.internal.xy_dir & 7) {
					case 0: case 2:	/* X+ */
						et4000w32_incx(((et4000->acl.internal.pixel_depth >> 4) & 3) + 1, et4000);
						et4000->acl.internal.pos_x++;
						break;
					case 1: case 3:	/* X- */
						et4000w32_decx(((et4000->acl.internal.pixel_depth >> 4) & 3) + 1, et4000);
						et4000->acl.internal.pos_x++;
						break;
					case 4: case 5:	/* Y+ */
						et4000w32_incy(et4000);
						et4000->acl.internal.pos_y++;
						break;
					case 6: case 7:	/* Y- */
						et4000w32_decy(et4000);
						et4000->acl.internal.pos_y++;
						break;
				}
			}
			if ((et4000->acl.internal.pos_x > et4000->acl.internal.count_x) ||
			    (et4000->acl.internal.pos_y > et4000->acl.internal.count_y)) {
				et4000w32_log("ACL status linedraw 0\n");
				et4000->acl.status &= ~(ACL_XYST | ACL_SSO);
				return;
			}
		}
	}
    } else {
	while (count--) {
		et4000w32_log("%i,%i : ", et4000->acl.internal.pos_x, et4000->acl.internal.pos_y);

		pattern	= svga->vram[(et4000->acl.pattern_addr + et4000->acl.pattern_x) & et4000->vram_mask];
		source	= svga->vram[(et4000->acl.source_addr  + et4000->acl.source_x)  & et4000->vram_mask];
		et4000w32_log("%i %06X %06X %02X %02X  ", et4000->acl.pattern_y, (et4000->acl.pattern_addr + et4000->acl.pattern_x) & et4000->vram_mask, (et4000->acl.source_addr + et4000->acl.source_x) & et4000->vram_mask, pattern, source);

		if (cpu_input == 2) {
			source = sdat & 0xff;
			sdat >>= 8;
		}
		dest = svga->vram[et4000->acl.dest_addr & et4000->vram_mask];
		out = 0;
		et4000w32_log("%06X %02X  %i %08X %08X  ", dest, et4000->acl.dest_addr, mix & 1, mix, et4000->acl.mix_addr);
		if ((et4000->acl.internal.ctrl_routing & 0xa) == 8) {
			mixdat = svga->vram[(et4000->acl.mix_addr >> 3) & et4000->vram_mask] & (1 << (et4000->acl.mix_addr & 7));
			et4000w32_log("%06X %02X  ", et4000->acl.mix_addr, svga->vram[(et4000->acl.mix_addr >> 3) & et4000->vram_mask]);
		} else {
			mixdat = mix & 1;
			mix >>= 1; 
			mix |= 0x80000000;
		}

		rop = mixdat ? et4000->acl.internal.rop_fg : et4000->acl.internal.rop_bg;
		for (c = 0; c < 8; c++) {
			d = (dest & (1 << c)) ? 1 : 0;
			if (source & (1 << c))		d |= 2;
			if (pattern & (1 << c))		d |= 4;
			if (rop & (1 << d))		out |= (1 << c);
		}
		et4000w32_log("%06X = %02X\n", et4000->acl.dest_addr & et4000->vram_mask, out);
		if (!(et4000->acl.internal.ctrl_routing & 0x40)) {
			svga->vram[et4000->acl.dest_addr & et4000->vram_mask] = out;
			svga->changedvram[(et4000->acl.dest_addr & et4000->vram_mask) >> 12] = changeframecount;
		} else {
			et4000->acl.cpu_dat |= ((uint64_t)out << (et4000->acl.cpu_dat_pos * 8));
			et4000->acl.cpu_dat_pos++;
		}

		if (et4000->acl.internal.xy_dir & 1)	et4000w32_decx(1, et4000);
		else					et4000w32_incx(1, et4000);

		et4000->acl.internal.pos_x++;
		if (et4000->acl.internal.pos_x > et4000->acl.internal.count_x) {
			if (et4000->acl.internal.xy_dir & 2) {
				et4000w32_decy(et4000);
				et4000->acl.mix_back	= et4000->acl.mix_addr	= et4000->acl.mix_back	- (et4000->acl.internal.mix_off		+ 1);
				et4000->acl.dest_back	= et4000->acl.dest_addr	= et4000->acl.dest_back	- (et4000->acl.internal.dest_off	+ 1);
			} else {
				et4000w32_incy(et4000);
				et4000->acl.mix_back	= et4000->acl.mix_addr	= et4000->acl.mix_back	+ et4000->acl.internal.mix_off		+ 1;
				et4000->acl.dest_back	= et4000->acl.dest_addr	= et4000->acl.dest_back	+ et4000->acl.internal.dest_off		+ 1;
			}

			et4000->acl.pattern_x = et4000->acl.pattern_x_back;
			et4000->acl.source_x  = et4000->acl.source_x_back;

			et4000->acl.internal.pos_y++;
			et4000->acl.internal.pos_x = 0;
			if (et4000->acl.internal.pos_y > et4000->acl.internal.count_y) {
				et4000->acl.status &= ~(ACL_XYST | ACL_SSO);
				return;
			}

			if (cpu_input)
				return;

			if (et4000->acl.internal.ctrl_routing & 0x40) {
				if (et4000->acl.cpu_dat_pos & 3) 
					et4000->acl.cpu_dat_pos += 4 - (et4000->acl.cpu_dat_pos & 3);
				return;
			}
		}
	}
    }
}


void
et4000w32p_hwcursor_draw(svga_t *svga, int displine)
{
    et4000w32p_t *et4000 = (et4000w32p_t *)svga->p;
    int x, offset, xx, xx2;
    int shift = (et4000->adjust_cursor + 1);
    int width = (svga->hwcursor_latch.xsize - svga->hwcursor_latch.xoff);
    int pitch = (svga->hwcursor_latch.xsize == 128) ? 32 : 16;
	int x_acc = 4;
	int minus_width = 0;
    uint8_t dat;
    offset = svga->hwcursor_latch.xoff;
	
	if (et4000->type == ET4000W32) {
		switch (svga->bpp) {
			case 8:
				minus_width = 0;
				x_acc = 2;
				break;
			case 15: case 16:
				minus_width = 64;
				x_acc = 2;
				break;
		}
	}

    for (x = 0; x < (width - minus_width); x += x_acc) {
	dat = svga->vram[svga->hwcursor_latch.addr + (offset >> 2)];

	xx = svga->hwcursor_latch.x + svga->x_add + x;

	if (!(xx % shift)) {
		xx2 = xx / shift;
		if (!(dat & 2))			buffer32->line[displine][xx2]  = (dat & 1) ? 0xFFFFFF : 0;
		else if ((dat & 3) == 3)	buffer32->line[displine][xx2] ^= 0xFFFFFF;
	}
	dat >>= 2;
	xx++;
	if (!(xx % shift)) {
		xx2 = xx / shift;
		if (!(dat & 2))			buffer32->line[displine][xx2]  = (dat & 1) ? 0xFFFFFF : 0;
		else if ((dat & 3) == 3)	buffer32->line[displine][xx2] ^= 0xFFFFFF;
	}
	dat >>= 2;
	xx++;
	if (!(xx % shift)) {
		xx2 = xx / shift;
		if (!(dat & 2))			buffer32->line[displine][xx2]  = (dat & 1) ? 0xFFFFFF : 0;
		else if ((dat & 3) == 3)	buffer32->line[displine][xx2] ^= 0xFFFFFF;
	}
	dat >>= 2;
	xx++;
	if (!(xx % shift)) {
		xx2 = xx / shift;
		if (!(dat & 2))			buffer32->line[displine][xx2]  = (dat & 1) ? 0xFFFFFF : 0;
		else if ((dat & 3) == 3)	buffer32->line[displine][xx2] ^= 0xFFFFFF;
	}
	dat >>= 2;

	offset += 4;
    }

	svga->hwcursor_latch.addr += pitch;
}


static void
et4000w32p_io_remove(et4000w32p_t *et4000)
{
    io_removehandler(0x03c0, 0x0020, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);

    io_removehandler(0x210a, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
    io_removehandler(0x211a, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
    io_removehandler(0x212a, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
    io_removehandler(0x213a, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
    io_removehandler(0x214a, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
    io_removehandler(0x215a, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
    io_removehandler(0x216a, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
    io_removehandler(0x217a, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
}


static void
et4000w32p_io_set(et4000w32p_t *et4000)
{
    et4000w32p_io_remove(et4000);

    io_sethandler(0x03c0, 0x0020, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);

    io_sethandler(0x210a, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
    io_sethandler(0x211a, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
    io_sethandler(0x212a, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
    io_sethandler(0x213a, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
    io_sethandler(0x214a, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
    io_sethandler(0x215a, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
    io_sethandler(0x216a, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
    io_sethandler(0x217a, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
}


uint8_t
et4000w32p_pci_read(int func, int addr, void *p)
{
    et4000w32p_t *et4000 = (et4000w32p_t *)p;

    addr &= 0xff;

    switch (addr) {
	case 0x00: return 0x0c;	/* Tseng Labs */
	case 0x01: return 0x10;

	case 0x02: return (et4000->rev);
	case 0x03: return 0x32;

	case PCI_REG_COMMAND:
		return et4000->pci_regs[PCI_REG_COMMAND] | 0x80;	/* Respond to IO and memory accesses */

	case 0x07:	return 1 << 1;	/* Medium DEVSEL timing */

	case 0x08: return (et4000->rev);	/* Revision ID */
	case 0x09: return 0;	/* Programming interface */

	case 0x0a: return 0x00;	/* Supports VGA interface */
	case 0x0b: return 0x03;	/* This has to be done in order to make this card work with the two 486 PCI machines. */

	case 0x10: return 0x00;	/* Linear frame buffer address */
	case 0x11: return 0x00;
	case 0x12: return 0x00;
	case 0x13: return (et4000->linearbase >> 24);

	case 0x30: return et4000->pci_regs[0x30] & 0x01; /* BIOS ROM address */
	case 0x31: return 0x00;
	case 0x32: return 0x00;
	case 0x33: return et4000->pci_regs[0x33] & 0xf0;
    }

    return 0;
}


void
et4000w32p_pci_write(int func, int addr, uint8_t val, void *p)
{
    et4000w32p_t *et4000 = (et4000w32p_t *)p;
    svga_t *svga = &et4000->svga;

    addr &= 0xff;

    switch (addr) {
	case PCI_REG_COMMAND:
		et4000->pci_regs[PCI_REG_COMMAND] = (val & 0x23) | 0x80;
		if (val & PCI_COMMAND_IO)
			et4000w32p_io_set(et4000);
		else
			et4000w32p_io_remove(et4000);
		et4000w32p_recalcmapping(et4000);
		break;

	case 0x13: 
		et4000->linearbase &= 0x00c00000; 
		et4000->linearbase |= (et4000->pci_regs[0x13] << 24);
		svga->crtc[0x30] &= 3;
		svga->crtc[0x30] |= ((et4000->linearbase & 0x3f000000) >> 22);
		et4000w32p_recalcmapping(et4000); 
		break;

	case 0x30: case 0x31: case 0x32: case 0x33:
		et4000->pci_regs[addr] = val;
		et4000->pci_regs[0x30] = 1;
		et4000->pci_regs[0x31] = 0;
		et4000->pci_regs[0x32] = 0;
		et4000->pci_regs[0x33] &= 0xf0;
		if (et4000->pci_regs[0x30] & 0x01) {
			uint32_t addr = (et4000->pci_regs[0x33] << 24);
			if (!addr)
				addr = 0xc0000;
			et4000w32_log("ET4000 bios_rom enabled at %08x\n", addr);
			mem_mapping_set_addr(&et4000->bios_rom.mapping, addr, 0x8000);
		} else {
			et4000w32_log("ET4000 bios_rom disabled\n");
			mem_mapping_disable(&et4000->bios_rom.mapping);
		}
		return;
    }
}


void *
et4000w32p_init(const device_t *info)
{
    int vram_size;
    et4000w32p_t *et4000 = malloc(sizeof(et4000w32p_t));
    memset(et4000, 0, sizeof(et4000w32p_t));

    et4000->pci = (info->flags & DEVICE_PCI) ? 0x80 : 0x00;
    et4000->vlb = (info->flags & DEVICE_VLB) ? 0x40 : 0x00;
	
	/*The ET4000/W32i ISA BIOS seems to not support 2MB of VRAM*/
	if ((info->local == ET4000W32) || ((info->local == ET4000W32I) && !(et4000->vlb)))
		vram_size = 1;
	else
		vram_size = device_get_config_int("memory");

	/*The interleaved VRAM was introduced by the ET4000/W32i*/
    et4000->interleaved = ((vram_size == 2) && (info->local != ET4000W32)) ? 1 : 0;

    if (info->flags & DEVICE_PCI)
	video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_et4000w32_pci);
    else if (info->flags & DEVICE_VLB)
	video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_et4000w32_vlb);
    else
	video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_et4000w32_isa);

    svga_init(info, &et4000->svga, et4000, vram_size << 20,
	      et4000w32p_recalctimings,
	      et4000w32p_in, et4000w32p_out,
	      et4000w32p_hwcursor_draw,
	      NULL);

    et4000->vram_mask = (vram_size << 20) - 1;
	et4000->svga.decode_mask = (vram_size << 20) - 1;

    et4000->type = info->local;

    switch(et4000->type) {
	case ET4000W32:
		/* ET4000/W32 */
		et4000->rev = 0;
		
		rom_init(&et4000->bios_rom, BIOS_ROM_PATH_W32, 0xc0000, 0x8000, 0x7fff, 0,
			 MEM_MAPPING_EXTERNAL);

		et4000->svga.ramdac = device_add(&tseng_ics5301_ramdac_device);
		et4000->svga.clock_gen = et4000->svga.ramdac;
		et4000->svga.getclock = sdac_getclock;
		break;	

	case ET4000W32I:
		/* ET4000/W32i rev B */
		et4000->rev = 3;

		if (et4000->vlb) {
			rom_init(&et4000->bios_rom, BIOS_ROM_PATH_W32I_VLB, 0xc0000, 0x8000, 0x7fff, 0,
				 MEM_MAPPING_EXTERNAL);				
		} else {
			rom_init(&et4000->bios_rom, BIOS_ROM_PATH_W32I_ISA, 0xc0000, 0x8000, 0x7fff, 0,
				 MEM_MAPPING_EXTERNAL);
		}

		et4000->svga.ramdac = device_add(&tseng_ics5301_ramdac_device);
		et4000->svga.clock_gen = et4000->svga.ramdac;
		et4000->svga.getclock = sdac_getclock;
		break;

	case ET4000W32P_REVC:
		/* ET4000/W32p rev C */
		et4000->rev = 7;

		rom_init(&et4000->bios_rom, BIOS_ROM_PATH_W32P_REVC, 0xc0000, 0x8000, 0x7fff, 0,
			 MEM_MAPPING_EXTERNAL);

		et4000->svga.ramdac = device_add(&tseng_ics5341_ramdac_device);
		et4000->svga.clock_gen = et4000->svga.ramdac;
		et4000->svga.getclock = sdac_getclock;
		break;

	case ET4000W32P:
		/* ET4000/W32p rev D */
		et4000->rev = 6;

		rom_init(&et4000->bios_rom, BIOS_ROM_PATH_W32P, 0xc0000, 0x8000, 0x7fff, 0,
			 MEM_MAPPING_EXTERNAL);

		et4000->svga.ramdac = device_add(&stg_ramdac_device);
		et4000->svga.clock_gen = et4000->svga.ramdac;
		et4000->svga.getclock = stg_getclock;
		et4000->svga.adv_flags |= FLAG_NOSKEW;
		break;

	case ET4000W32P_CARDEX:
		/* ET4000/W32p rev D */
		et4000->rev = 6;

		rom_init(&et4000->bios_rom, BIOS_ROM_PATH_CARDEX, 0xc0000, 0x8000, 0x7fff, 0,
			 MEM_MAPPING_EXTERNAL);

		et4000->svga.ramdac = device_add(&stg_ramdac_device);
		et4000->svga.clock_gen = et4000->svga.ramdac;
		et4000->svga.getclock = stg_getclock;
		et4000->svga.adv_flags |= FLAG_NOSKEW;
		break;

	case ET4000W32P_DIAMOND:
		/* ET4000/W32p rev D */
		et4000->rev = 6;

		rom_init(&et4000->bios_rom, BIOS_ROM_PATH_DIAMOND, 0xc0000, 0x8000, 0x7fff, 0,
			 MEM_MAPPING_EXTERNAL);

		et4000->svga.ramdac = device_add(&stg_ramdac_device);
		et4000->svga.clock_gen = device_add(&icd2061_device);
		et4000->svga.getclock = icd2061_getclock;
		break;
    }
    if (info->flags & DEVICE_PCI)
	mem_mapping_disable(&et4000->bios_rom.mapping);

    mem_mapping_add(&et4000->linear_mapping, 0, 0, svga_read_linear, svga_readw_linear, svga_readl_linear, svga_write_linear, svga_writew_linear, svga_writel_linear, NULL, MEM_MAPPING_EXTERNAL, &et4000->svga);
    mem_mapping_add(&et4000->mmu_mapping,    0, 0, et4000w32p_mmu_read, NULL, NULL, et4000w32p_mmu_write, NULL, NULL, NULL, MEM_MAPPING_EXTERNAL, et4000);

    et4000w32p_io_set(et4000);

    if (info->flags & DEVICE_PCI)
	pci_add_card(PCI_ADD_VIDEO, et4000w32p_pci_read, et4000w32p_pci_write, et4000);

    /* Hardwired bits: 00000000 1xx0x0xx */
    /* R/W bits:                 xx xxxx */
    /* PCem bits:                    111 */
    et4000->pci_regs[0x04] = 0x83;

    et4000->pci_regs[0x10] = 0x00;
    et4000->pci_regs[0x11] = 0x00;
    et4000->pci_regs[0x12] = 0xff;
    et4000->pci_regs[0x13] = 0xff;

    et4000->pci_regs[0x30] = 0x00;
    et4000->pci_regs[0x31] = 0x00;
    et4000->pci_regs[0x32] = 0x00;
    et4000->pci_regs[0x33] = 0xf0;

	et4000->svga.packed_chain4 = 1;

    return et4000;
}


int
et4000w32_available(void)
{
    return rom_present(BIOS_ROM_PATH_W32);
}


int
et4000w32i_isa_available(void)
{
    return rom_present(BIOS_ROM_PATH_W32I_ISA);
}


int
et4000w32i_vlb_available(void)
{
    return rom_present(BIOS_ROM_PATH_W32I_VLB);
}


int
et4000w32p_revc_available(void)
{
    return rom_present(BIOS_ROM_PATH_W32P_REVC);
}


int
et4000w32p_noncardex_available(void)
{
    return rom_present(BIOS_ROM_PATH_W32P);
}


int
et4000w32p_available(void)
{
    return rom_present(BIOS_ROM_PATH_DIAMOND);
}


int
et4000w32p_cardex_available(void)
{
    return rom_present(BIOS_ROM_PATH_CARDEX);
}


void
et4000w32p_close(void *p)
{
    et4000w32p_t *et4000 = (et4000w32p_t *)p;

    svga_close(&et4000->svga);

    free(et4000);
}


void
et4000w32p_speed_changed(void *p)
{
    et4000w32p_t *et4000 = (et4000w32p_t *)p;

    svga_recalctimings(&et4000->svga);
}


void
et4000w32p_force_redraw(void *p)
{
    et4000w32p_t *et4000 = (et4000w32p_t *)p;

    et4000->svga.fullchange = changeframecount;
}


static const device_config_t et4000w32p_config[] =
{
        {
                "memory", "Memory size", CONFIG_SELECTION, "", 2, "", { 0 },
                {
                        {
                                "1 MB", 1
                        },
                        {
                                "2 MB", 2
                        },
                        {
                                ""
                        }
                }
        },
        {
                "", "", -1
        }
};


const device_t et4000w32_device =
{
        "Tseng Labs ET4000/w32",
        DEVICE_ISA | DEVICE_AT, ET4000W32,
        et4000w32p_init, et4000w32p_close, NULL,
        { et4000w32_available },
        et4000w32p_speed_changed,
        et4000w32p_force_redraw,
        NULL
};

const device_t et4000w32_onboard_device =
{
        "Tseng Labs ET4000/w32 (On-board)",
        DEVICE_ISA | DEVICE_AT, ET4000W32,
        et4000w32p_init, et4000w32p_close, NULL,
        { et4000w32_available },
        et4000w32p_speed_changed,
        et4000w32p_force_redraw,
        NULL
};

const device_t et4000w32i_isa_device =
{
        "Tseng Labs ET4000/w32i ISA",
        DEVICE_ISA | DEVICE_AT, ET4000W32I,
        et4000w32p_init, et4000w32p_close, NULL,
        { et4000w32i_isa_available },
        et4000w32p_speed_changed,
        et4000w32p_force_redraw,
        NULL
};

const device_t et4000w32i_vlb_device =
{
        "Tseng Labs ET4000/w32i VLB",
        DEVICE_VLB, ET4000W32I,
        et4000w32p_init, et4000w32p_close, NULL,
        { et4000w32i_vlb_available },
        et4000w32p_speed_changed,
        et4000w32p_force_redraw,
        et4000w32p_config
};

const device_t et4000w32p_revc_vlb_device =
{
        "Tseng Labs ET4000/w32p Rev. C VLB (Cardex)",
        DEVICE_VLB, ET4000W32P_REVC,
        et4000w32p_init, et4000w32p_close, NULL,
        { et4000w32p_revc_available },
        et4000w32p_speed_changed,
        et4000w32p_force_redraw,
        et4000w32p_config
};

const device_t et4000w32p_revc_pci_device =
{
        "Tseng Labs ET4000/w32p Rev. C PCI (Cardex)",
        DEVICE_PCI, ET4000W32P_REVC,
        et4000w32p_init, et4000w32p_close, NULL,
        { et4000w32p_revc_available },
        et4000w32p_speed_changed,
        et4000w32p_force_redraw,
        et4000w32p_config
};

const device_t et4000w32p_noncardex_vlb_device =
{
        "Tseng Labs ET4000/w32p VLB",
        DEVICE_VLB, ET4000W32P,
        et4000w32p_init, et4000w32p_close, NULL,
        { et4000w32p_noncardex_available },
        et4000w32p_speed_changed,
        et4000w32p_force_redraw,
        et4000w32p_config
};

const device_t et4000w32p_noncardex_pci_device =
{
        "Tseng Labs ET4000/w32p PCI",
        DEVICE_PCI, ET4000W32P,
        et4000w32p_init, et4000w32p_close, NULL,
        { et4000w32p_noncardex_available },
        et4000w32p_speed_changed,
        et4000w32p_force_redraw,
        et4000w32p_config
};

const device_t et4000w32p_cardex_vlb_device =
{
        "Tseng Labs ET4000/w32p VLB (Cardex)",
        DEVICE_VLB, ET4000W32P_CARDEX,
        et4000w32p_init, et4000w32p_close, NULL,
        { et4000w32p_cardex_available },
        et4000w32p_speed_changed,
        et4000w32p_force_redraw,
        et4000w32p_config
};

const device_t et4000w32p_cardex_pci_device =
{
        "Tseng Labs ET4000/w32p PCI (Cardex)",
        DEVICE_PCI, ET4000W32P_CARDEX,
        et4000w32p_init, et4000w32p_close, NULL,
        { et4000w32p_cardex_available },
        et4000w32p_speed_changed,
        et4000w32p_force_redraw,
        et4000w32p_config
};

const device_t et4000w32p_vlb_device =
{
        "Tseng Labs ET4000/w32p VLB (Diamond)",
        DEVICE_VLB, ET4000W32P_DIAMOND,
        et4000w32p_init, et4000w32p_close, NULL,
        { et4000w32p_available },
        et4000w32p_speed_changed,
        et4000w32p_force_redraw,
        et4000w32p_config
};

const device_t et4000w32p_pci_device =
{
        "Tseng Labs ET4000/w32p PCI (Diamond)",
        DEVICE_PCI, ET4000W32P_DIAMOND,
        et4000w32p_init, et4000w32p_close, NULL,
        { et4000w32p_available },
        et4000w32p_speed_changed,
        et4000w32p_force_redraw,
        et4000w32p_config
};
