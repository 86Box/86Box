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
#define BIOS_ROM_PATH_W32P_VIDEOMAGIC_REVB_VLB	"roms/video/et4000w32/VideoMagic-BioS-HXIRTW32PWSRL.BIN"
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
	ET4000W32P_VIDEOMAGIC_REVB,
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
			bank, type;

    uint32_t		linearbase;
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

	uint8_t		suspend_terminate, osr;
	uint8_t		status;
	uint16_t	x_count, y_count;

	int		pattern_x, source_x, pattern_x_back, source_x_back,
			pattern_y, source_y, cpu_dat_pos, pix_pos,
			cpu_input_num, fifo_queue;
	int		pattern_x_diff, pattern_y_diff, pattern_x_diff2, pattern_y_diff2;
	int		patcnt, mmu_start;

	uint32_t	pattern_addr, source_addr, dest_addr, mix_addr,
			pattern_back, source_back, dest_back, mix_back,
			cpu_input;

	uint64_t	cpu_dat;
    } acl;

    struct {
	uint32_t	base[3];
	uint8_t		ctrl;
    } mmu;

	volatile int busy;
} et4000w32p_t;


static int		et4000w32_vbus[4] = {1, 2, 4, 4};

static int		et4000w32_max_x[8] = {0,0,4,8,0x10,0x20,0x40,0x70000000};
static int		et4000w32_wrap_x[8] = {0,0,3,7,0x0F,0x1F,0x3F,~0};
static int		et4000w32_wrap_y[8] = {1,2,4,8,~0,~0,~0,~0};

static video_timings_t	timing_et4000w32_vlb = {VIDEO_BUS, 4,  4,  4,  10, 10, 10};
static video_timings_t	timing_et4000w32_pci = {VIDEO_PCI, 4,  4,  4,  10, 10, 10};
static video_timings_t	timing_et4000w32_isa = {VIDEO_ISA, 4,  4,  4,  10, 10, 10};


void		et4000w32p_recalcmapping(et4000w32p_t *et4000);

static uint8_t		et4000w32p_mmu_read(uint32_t addr, void *p);
static void		et4000w32p_mmu_write(uint32_t addr, uint8_t val, void *p);

static void		et4000w32_blit_start(et4000w32p_t *et4000);
static void		et4000w32p_blit_start(et4000w32p_t *et4000);
static void	et4000w32_blit(int count, int cpu_input, uint32_t src_dat, uint32_t mix_dat, et4000w32p_t *et4000);
static void		et4000w32p_blit(int count, uint32_t mix, uint32_t sdat, int cpu_input, et4000w32p_t *et4000);
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
		svga->crtcreg = val & 0x3f;
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
			if (et4000->pci && (et4000->rev != 5))
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
			if (et4000->type > ET4000W32P_REVC) {
				if (svga->bpp == 24) {
					et4000->adjust_cursor = 2;
				}
			}
		} else {
			if (et4000->type > ET4000W32P_REVC) {
				if (svga->bpp == 24 && et4000->adjust_cursor) {
					et4000->adjust_cursor = 0;
				}
			}
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
		if (et4000->type == ET4000W32) {
			if (svga->crtcreg == 0x37)
				return 0x09;
		}
		return svga->crtc[svga->crtcreg];

	case 0x3da:
		svga->attrff = 0;

		/*Bit 1 of the Input Status Register is required by the OS/2 and NT ET4000W32/I drivers to be set otherwise
		  the guest will loop infinitely upon reaching the GUI*/
		if (svga->cgastat & 0x01)
			svga->cgastat &= ~0x32;
		else
			svga->cgastat ^= 0x32;
		return svga->cgastat;

	case 0x210a: case 0x211a: case 0x212a: case 0x213a:
	case 0x214a: case 0x215a: case 0x216a: case 0x217a:
		return et4000->index;
	case 0x210B: case 0x211B: case 0x212B: case 0x213B:
	case 0x214B: case 0x215B: case 0x216B: case 0x217B:
		if (et4000->index == 0xec) {
			return (et4000->regs[0xec] & 0xf) | (et4000->rev << 4);
		}
		if (et4000->index == 0xee) {
			if (svga->bpp == 8) {
				if ((svga->gdcreg[5] & 0x60) >= 0x40)
					return 3;
				else if ((svga->gdcreg[5] & 0x60) == 0x20)
					return 1;
				else
					return 2;
			} else if (svga->bpp == 15 || svga->bpp == 16)
				return 4;
			else
				break;
		}
		if (et4000->index == 0xef)  {
			if (et4000->pci)
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

	if (et4000->type != ET4000W32P_DIAMOND) {
		if ((svga->gdcreg[6] & 1) || (svga->attrregs[0x10] & 1)) {
			if (svga->gdcreg[5] & 0x40) {
				switch (svga->bpp) {
					case 8:
						svga->clock /= 2;
						break;
					case 15: case 16:
						svga->clock /= 3;
						break;
					case 24:
						svga->clock /= 4;
						break;
				}
			}
		}
	}

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
			} else if ((svga->gdcreg[5] & 0x40) == 0) {
				svga->hdisp += (svga->seqregs[1] & 1) ? 8 : 9;
				if (svga->hdisp == 648 || svga->hdisp == 808 || svga->hdisp == 1032)
					svga->hdisp -= 8;
			}
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
		if (et4000->type <= ET4000W32P_REVC) {
			if (et4000->type == ET4000W32P_REVC) {
				if (svga->hdisp != 1024)
					et4000->adjust_cursor = 1;
			} else
				et4000->adjust_cursor = 1;
		}
		break;
	case 24:
		svga->hdisp /= 3;
		if (et4000->type <= ET4000W32P_REVC)
			et4000->adjust_cursor = 2;
		if (et4000->type == ET4000W32P_DIAMOND && (svga->hdisp == 640/2 || svga->hdisp == 1232)) {
			svga->hdisp = 640;
		}
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
		if (svga->adv_flags & FLAG_NOSKEW) {
			svga->ma_latch--;
		}

		switch (svga->gdcreg[5] & 0x60) {
			case 0x00:
				if (et4000->rev == 5)
					svga->ma_latch++;

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
	mem_mapping_disable(&et4000->linear_mapping);
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
    }

    if (!et4000->interleaved && (svga->crtc[0x32] & 0x80))
	mem_mapping_disable(&svga->mapping);
}


static void
et4000w32p_accel_write_fifo(et4000w32p_t *et4000, uint32_t addr, uint8_t val)
{
	et4000->acl.fifo_queue++;
    switch (addr & 0xff) {
	case 0x80:
		et4000->acl.queued.pattern_addr = (et4000->acl.queued.pattern_addr & 0x3fff00) | val;
		break;
	case 0x81:
		et4000->acl.queued.pattern_addr = (et4000->acl.queued.pattern_addr & 0x3f00ff) | (val << 8);
		break;
	case 0x82:
		et4000->acl.queued.pattern_addr = (et4000->acl.queued.pattern_addr & 0x00ffff) | ((val & 0x3f) << 16);
		break;
	case 0x84:
		et4000->acl.queued.source_addr = (et4000->acl.queued.source_addr & 0x3fff00) | val;
		break;
	case 0x85:
		et4000->acl.queued.source_addr = (et4000->acl.queued.source_addr & 0x3f00ff) | (val << 8);
		break;
	case 0x86:
		et4000->acl.queued.source_addr = (et4000->acl.queued.source_addr & 0x00ffff) | ((val & 0x3f) << 16);
		break;
	case 0x88:
		et4000->acl.queued.pattern_off = (et4000->acl.queued.pattern_off & 0x0f00) | val;
		break;
	case 0x89:
		et4000->acl.queued.pattern_off = (et4000->acl.queued.pattern_off & 0x00ff) | ((val & 0x0f) << 8);
		break;
	case 0x8a:
		et4000->acl.queued.source_off = (et4000->acl.queued.source_off & 0x0f00) | val;
		break;
	case 0x8b:
		et4000->acl.queued.source_off = (et4000->acl.queued.source_off & 0x00ff) | ((val & 0x0f) << 8);
		break;
	case 0x8c:
		et4000->acl.queued.dest_off = (et4000->acl.queued.dest_off & 0x0f00) | val;
		break;
	case 0x8d:
		et4000->acl.queued.dest_off = (et4000->acl.queued.dest_off & 0x00ff) | ((val & 0x0f) << 8);
		break;
	case 0x8e:
		if (et4000->type >= ET4000W32P_REVC)
			et4000->acl.queued.pixel_depth = val & 0x30;
		else
			et4000->acl.queued.vbus = val & 0x03;
		break;
	case 0x8f:
		if (et4000->type >= ET4000W32P_REVC)
			et4000->acl.queued.xy_dir = val & 0xb7;
		else
			et4000->acl.queued.xy_dir = val & 0x03;
		break;
	case 0x90:
		et4000->acl.queued.pattern_wrap = val & 0x77;
		break;
	case 0x92:
		et4000->acl.queued.source_wrap = val & 0x77;
		break;
	case 0x98:
		et4000->acl.queued.count_x = (et4000->acl.queued.count_x & 0x0f00) | val;
		break;
	case 0x99:
		et4000->acl.queued.count_x = (et4000->acl.queued.count_x & 0x00ff) | ((val & 0x0f) << 8);
		break;
	case 0x9a:
		et4000->acl.queued.count_y = (et4000->acl.queued.count_y & 0x0f00) | val;
		break;
	case 0x9b:
		et4000->acl.queued.count_y = (et4000->acl.queued.count_y & 0x00ff) | ((val & 0x0f) << 8);
		break;
	case 0x9c:
		if (et4000->type >= ET4000W32P_REVC)
			et4000->acl.queued.ctrl_routing = val & 0xdb;
		else
			et4000->acl.queued.ctrl_routing = val & 0xb7;
		break;
	case 0x9d:
		et4000->acl.queued.ctrl_reload = val & 0x03;
		break;
	case 0x9e:
		et4000->acl.queued.rop_bg = val;
		break;
	case 0x9f:
		et4000->acl.queued.rop_fg = val;
		break;
	case 0xa0:
		et4000->acl.queued.dest_addr = (et4000->acl.queued.dest_addr & 0x3fff00) | val;
		break;
	case 0xa1:
		et4000->acl.queued.dest_addr = (et4000->acl.queued.dest_addr & 0x3f00ff) | (val << 8);
		break;
	case 0xa2:
		et4000->acl.queued.dest_addr = (et4000->acl.queued.dest_addr & 0x00ffff) | ((val & 0x3f) << 16);
		break;
	case 0xa3:
		et4000->acl.internal = et4000->acl.queued;
		if (et4000->type >= ET4000W32P_REVC) {
			et4000w32p_blit_start(et4000);
			et4000w32_log("Destination Address write and start XY Block, xcnt = %i, ycnt = %i\n", et4000->acl.x_count + 1, et4000->acl.y_count + 1);
			if (!(et4000->acl.queued.ctrl_routing & 0x43)) {
				et4000w32p_blit(0xffffff, ~0, 0, 0, et4000);
			}
			if ((et4000->acl.queued.ctrl_routing & 0x40) && !(et4000->acl.internal.ctrl_routing & 3)) {
				et4000w32p_blit(4, ~0, 0, 0, et4000);
			}
		} else {
			et4000w32_blit_start(et4000);
			et4000->acl.cpu_input_num = 0;
			if (!(et4000->acl.queued.ctrl_routing & 0x37)) {
				et4000->acl.mmu_start = 0;
				et4000w32_blit(-1, 0, 0, 0xffffffff, et4000);
			}
		}
		break;
	case 0xa4:
		et4000->acl.queued.mix_addr = (et4000->acl.queued.mix_addr & 0xFFFFFF00) | val;
		break;
	case 0xa5:
		et4000->acl.queued.mix_addr = (et4000->acl.queued.mix_addr & 0xFFFF00FF) | (val << 8);
		break;
	case 0xa6:
		et4000->acl.queued.mix_addr = (et4000->acl.queued.mix_addr & 0xFF00FFFF) | (val << 16);
		break;
	case 0xa7:
		et4000->acl.queued.mix_addr = (et4000->acl.queued.mix_addr & 0x00FFFFFF) | (val << 24);
		break;
	case 0xa8:
		et4000->acl.queued.mix_off = (et4000->acl.queued.mix_off & 0xFF00) | val;
		break;
	case 0xa9:
		et4000->acl.queued.mix_off = (et4000->acl.queued.mix_off & 0x00FF) | (val << 8);
		break;
	case 0xaa:
		et4000->acl.queued.error = (et4000->acl.queued.error & 0xFF00) | val;
		break;
	case 0xab:
		et4000->acl.queued.error = (et4000->acl.queued.error & 0x00FF) | (val << 8);
		break;
	case 0xac:
		et4000->acl.queued.dmin = (et4000->acl.queued.dmin & 0xFF00) | val;
		break;
	case 0xad:
		et4000->acl.queued.dmin = (et4000->acl.queued.dmin & 0x00FF) | (val << 8);
		break;
	case 0xae:
		et4000->acl.queued.dmaj = (et4000->acl.queued.dmaj & 0xFF00) | val;
		break;
	case 0xaf:
		et4000->acl.queued.dmaj = (et4000->acl.queued.dmaj & 0x00FF) | (val << 8);
		break;
    }
}

static void
et4000w32p_accel_write_mmu(et4000w32p_t *et4000, uint32_t addr, uint8_t val, uint8_t bank)
{
    if (et4000->type >= ET4000W32P_REVC) {
	if (!(et4000->acl.status & ACL_XYST)) {
		et4000w32_log("XY MMU block not started\n");
		return;
	}
	if (et4000->acl.internal.ctrl_routing & 3) {
		et4000->acl.fifo_queue++;
		if ((et4000->acl.internal.ctrl_routing & 3) == 2) /*CPU data is Mix data*/
			et4000w32p_blit(8 - (et4000->acl.mix_addr & 7), val >> (et4000->acl.mix_addr & 7), 0, 1, et4000);
		else if ((et4000->acl.internal.ctrl_routing & 3) == 1) /*CPU data is Source data*/
			et4000w32p_blit(1, ~0, val, 2, et4000);
	}
    } else {
	if (!(et4000->acl.status & ACL_XYST)) {
		et4000->acl.fifo_queue++;
		et4000->acl.queued.dest_addr = ((addr & 0x1fff) + et4000->mmu.base[bank]);
		et4000->acl.internal = et4000->acl.queued;
		et4000w32_blit_start(et4000);
		et4000w32_log("ET4000W32 Accelerated MMU aperture start XY Block (Implicit): bank = %i, patx = %i, paty = %i, wrap y = %i\n", et4000->bank, et4000->acl.pattern_x, et4000->acl.pattern_y, et4000w32_wrap_y[(et4000->acl.internal.pattern_wrap >> 4) & 7]);
		et4000->acl.cpu_input_num = 0;
		if (!(et4000->acl.queued.ctrl_routing & 0x37)) {
			et4000->acl.mmu_start = 1;
			et4000w32_blit(-1, 0, 0, 0xffffffff, et4000);
		}
	}

	if (et4000->acl.internal.ctrl_routing & 7) {
		et4000->acl.fifo_queue++;
		et4000->acl.cpu_input = (et4000->acl.cpu_input & ~(0xff << (et4000->acl.cpu_input_num << 3))) |
					(val << (et4000->acl.cpu_input_num << 3));
		et4000->acl.cpu_input_num++;

		if (et4000->acl.cpu_input_num == et4000w32_vbus[et4000->acl.internal.vbus]) {
			if ((et4000->acl.internal.ctrl_routing & 7) == 2) /*CPU data is Mix data*/
				et4000w32_blit(et4000->acl.cpu_input_num << 3, 2, 0, et4000->acl.cpu_input, et4000);
			else if ((et4000->acl.internal.ctrl_routing & 7) == 1) /*CPU data is Source data*/
				et4000w32_blit(et4000->acl.cpu_input_num, 1, et4000->acl.cpu_input, 0xffffffff, et4000);

			et4000->acl.cpu_input_num = 0;
		}

		if ((et4000->acl.internal.ctrl_routing & 7) == 4) /*CPU data is X Count*/
			et4000w32_blit(val | (et4000->acl.queued.count_x << 8), 0, 0, 0xffffffff, et4000);
		if ((et4000->acl.internal.ctrl_routing & 7) == 5) /*CPU data is Y Count*/
			et4000w32_blit(val | (et4000->acl.queued.count_y << 8), 0, 0, 0xffffffff, et4000);
	}
    }
}

static void
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
			et4000w32p_accel_write_mmu(et4000, addr & 0x7fff, val, et4000->bank);
		} else {
			if (((addr & 0x1fff) + et4000->mmu.base[et4000->bank]) < svga->vram_max) {
				svga->vram[((addr & 0x1fff) + et4000->mmu.base[et4000->bank]) & et4000->vram_mask] = val;
				svga->changedvram[(((addr & 0x1fff) + et4000->mmu.base[et4000->bank]) & et4000->vram_mask) >> 12] = changeframecount;
			}
		}
		break;
	case 0x6000:
		if ((addr & 0xff) >= 0x80) {
			et4000w32p_accel_write_fifo(et4000, addr & 0x7fff, val);
		} else {
			switch (addr & 0xff) {
				case 0x00:
					et4000->mmu.base[0] = (et4000->mmu.base[0] & 0x3fff00) | val;
					break;
				case 0x01:
					et4000->mmu.base[0] = (et4000->mmu.base[0] & 0x3f00ff) | (val << 8);
					break;
				case 0x02:
					et4000->mmu.base[0] = (et4000->mmu.base[0] & 0x00ffff) | ((val & 0x3f) << 16);
					break;
				case 0x04:
					et4000->mmu.base[1] = (et4000->mmu.base[1] & 0x3fff00) | val;
					break;
				case 0x05:
					et4000->mmu.base[1] = (et4000->mmu.base[1] & 0x3f00ff) | (val << 8);
					break;
				case 0x06:
					et4000->mmu.base[1] = (et4000->mmu.base[1] & 0x00ffff) | ((val & 0x3f) << 16);
					break;
				case 0x08:
					et4000->mmu.base[2] = (et4000->mmu.base[2] & 0x3fff00) | val;
					break;
				case 0x09:
					et4000->mmu.base[2] = (et4000->mmu.base[2] & 0x3f00ff) | (val << 8);
					break;
				case 0x0a:
					et4000->mmu.base[2] = (et4000->mmu.base[2] & 0x00ffff) | ((val & 0x3f) << 16);
					break;
				case 0x13:
					et4000->mmu.ctrl = val;
					break;
				case 0x30:
					et4000->acl.suspend_terminate = val;
					break;
				case 0x31:
					et4000->acl.osr = val;
					break;
			}
		}
		break;
    }
}

static uint8_t
et4000w32p_mmu_read(uint32_t addr, void *p)
{
    et4000w32p_t *et4000 = (et4000w32p_t *)p;
    svga_t *svga = &et4000->svga;
    uint8_t temp;

    switch (addr & 0x6000) {
	case 0x0000:	/* MMU 0 */
	case 0x2000:	/* MMU 1 */
	case 0x4000:	/* MMU 2 */
		et4000->bank = (addr >> 13) & 3;
		if (et4000->mmu.ctrl & (1 << et4000->bank)) {
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

		if ((addr & 0x1fff) + et4000->mmu.base[et4000->bank] >= svga->vram_max)
			return 0xff;

		return svga->vram[(addr & 0x1fff) + et4000->mmu.base[et4000->bank]];

	case 0x6000:
		switch (addr & 0xff) {
			case 0x00:
				return et4000->mmu.base[0] & 0xff;
			case 0x01:
				return et4000->mmu.base[0] >> 8;
			case 0x02:
				return et4000->mmu.base[0] >> 16;
			case 0x03:
				return et4000->mmu.base[0] >> 24;
			case 0x04:
				return et4000->mmu.base[1] & 0xff;
			case 0x05:
				return et4000->mmu.base[1] >> 8;
			case 0x06:
				return et4000->mmu.base[1] >> 16;
			case 0x07:
				return et4000->mmu.base[1] >> 24;
			case 0x08:
				return et4000->mmu.base[2] & 0xff;
			case 0x09:
				return et4000->mmu.base[2] >> 8;
			case 0x0a:
				return et4000->mmu.base[2] >> 16;
			case 0x0b:
				return et4000->mmu.base[2] >> 24;
			case 0x13:
				return et4000->mmu.ctrl;

			case 0x36:
				if (et4000->acl.fifo_queue) {
					et4000->acl.status |= ACL_RDST;
					et4000->acl.fifo_queue = 0;
				} else
					et4000->acl.status &= ~ACL_RDST;
				return et4000->acl.status;

			case 0x80:
				return et4000->acl.internal.pattern_addr & 0xff;
			case 0x81:
				return et4000->acl.internal.pattern_addr >> 8;
			case 0x82:
				return et4000->acl.internal.pattern_addr >> 16;
			case 0x83:
				return et4000->acl.internal.pattern_addr >> 24;
			case 0x84:
				return et4000->acl.internal.source_addr & 0xff;
			case 0x85:
				return et4000->acl.internal.source_addr >> 8;
			case 0x86:
				return et4000->acl.internal.source_addr >> 16;
			case 0x87:
				return et4000->acl.internal.source_addr >> 24;
			case 0x88:
				return et4000->acl.internal.pattern_off & 0xff;
			case 0x89:
				return et4000->acl.internal.pattern_off >> 8;
			case 0x8a:
				return et4000->acl.internal.source_off & 0xff;
			case 0x8b:
				return et4000->acl.internal.source_off >> 8;
			case 0x8c:
				return et4000->acl.internal.dest_off & 0xff;
			case 0x8d:
				return et4000->acl.internal.dest_off >> 8;
			case 0x8e:
				if (et4000->type >= ET4000W32P_REVC)
					return et4000->acl.internal.pixel_depth;
				else
					return et4000->acl.internal.vbus;
				break;
			case 0x8f:	return et4000->acl.internal.xy_dir;
			case 0x90:	return et4000->acl.internal.pattern_wrap;
			case 0x92:	return et4000->acl.internal.source_wrap;
			case 0x98:	return et4000->acl.internal.count_x & 0xff;
			case 0x99:	return et4000->acl.internal.count_x >> 8;
			case 0x9a:	return et4000->acl.internal.count_y & 0xff;
			case 0x9b:	return et4000->acl.internal.count_y >> 8;
			case 0x9c:	return et4000->acl.internal.ctrl_routing;
			case 0x9d:	return et4000->acl.internal.ctrl_reload;
			case 0x9e:	return et4000->acl.internal.rop_bg;
			case 0x9f:	return et4000->acl.internal.rop_fg;
			case 0xa0:	return et4000->acl.internal.dest_addr & 0xff;
			case 0xa1:	return et4000->acl.internal.dest_addr >> 8;
			case 0xa2:	return et4000->acl.internal.dest_addr >> 16;
			case 0xa3:	return et4000->acl.internal.dest_addr >> 24;
	}

	return 0xff;
    }

    return 0xff;
}


void
et4000w32_blit_start(et4000w32p_t *et4000)
{
	et4000->acl.x_count = et4000->acl.internal.count_x;
	et4000->acl.y_count = et4000->acl.internal.count_y;

	et4000->acl.pattern_addr = et4000->acl.internal.pattern_addr;
	et4000->acl.source_addr	= et4000->acl.internal.source_addr;
    et4000->acl.dest_addr = et4000->acl.internal.dest_addr;
    et4000->acl.dest_back = et4000->acl.dest_addr;
    et4000->acl.pattern_x = et4000->acl.source_x = et4000->acl.pattern_y = et4000->acl.source_y = 0;

    et4000->acl.status |= ACL_XYST;
	et4000->acl.status &= ~ACL_SSO;

    if (!(et4000->acl.internal.ctrl_routing & 7) || (et4000->acl.internal.ctrl_routing & 4))
	et4000->acl.status |= ACL_SSO;

    if (et4000w32_wrap_x[et4000->acl.internal.pattern_wrap & 7]) {
	et4000->acl.pattern_x = et4000->acl.pattern_addr & et4000w32_wrap_x[et4000->acl.internal.pattern_wrap & 7];
	et4000->acl.pattern_addr &= ~et4000w32_wrap_x[et4000->acl.internal.pattern_wrap & 7];
    }
    et4000->acl.pattern_back = et4000->acl.pattern_addr;

    if (!(et4000->acl.internal.pattern_wrap & 0x40)) {
	if ((et4000w32_wrap_x[et4000->acl.internal.pattern_wrap & 7] + 1) == 0x00) { /*This is to avoid a division by zero crash*/
		et4000->acl.pattern_y = (et4000->acl.pattern_addr / (0x7f + 1)) & (et4000w32_wrap_y[(et4000->acl.internal.pattern_wrap >> 4) & 7] - 1);
	} else
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
	if ((et4000w32_wrap_x[et4000->acl.internal.source_wrap & 7] + 1) == 0x00) { /*This is to avoid a division by zero crash*/
		et4000->acl.source_y = (et4000->acl.source_addr / (0x7f + 1)) & (et4000w32_wrap_y[(et4000->acl.internal.source_wrap >> 4) & 7] - 1);
	} else
		et4000->acl.source_y = (et4000->acl.source_addr / (et4000w32_wrap_x[et4000->acl.internal.source_wrap & 7] + 1)) & (et4000w32_wrap_y[(et4000->acl.internal.source_wrap >> 4) & 7] - 1);
	et4000->acl.source_back &= ~(((et4000w32_wrap_x[et4000->acl.internal.source_wrap & 7] + 1) * et4000w32_wrap_y[(et4000->acl.internal.source_wrap >> 4) & 7]) - 1);
    }
    et4000->acl.source_x_back = et4000->acl.source_x;
}


static void
et4000w32p_blit_start(et4000w32p_t *et4000)
{
	et4000->acl.x_count = et4000->acl.internal.count_x;
	et4000->acl.y_count = et4000->acl.internal.count_y;

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

    et4000w32_max_x[2] = (et4000->acl.internal.pixel_depth == 0x20) ? 3 : 4;

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
	et4000->acl.source_addr		= et4000->acl.source_back + (et4000w32_wrap_x[et4000->acl.internal.source_wrap & 7] *(et4000w32_wrap_y[(et4000->acl.internal.source_wrap >> 4) & 7] - 1));
    }
}


#define ROPMIX(R, D, P, S, out) \
	{										\
		switch (R) {								\
			case 0x00: out = 0; break;					\
			case 0x01: out = ~(D | (P | S)); break;				\
			case 0x02: out = D & ~(P | S); break;				\
			case 0x03: out = ~(P | S); break;				\
			case 0x04: out = S & ~(D | P); break;				\
			case 0x05: out = ~(D | P); break;				\
			case 0x06: out = ~(P | ~(D ^ S)); break;			\
			case 0x07: out = ~(P | (D & S)); break;				\
			case 0x08: out = S & (D & ~P); break;				\
			case 0x09: out = ~(P | (D ^ S)); break;				\
			case 0x0a: out = D & ~P; break;					\
			case 0x0b: out = ~(P | (S & ~D)); break;			\
			case 0x0c: out = S & ~P; break;					\
			case 0x0d: out = ~(P | (D & ~S)); break;			\
			case 0x0e: out = ~(P | ~(D | S)); break;			\
			case 0x0f: out = ~P; break;					\
			case 0x10: out = P & ~(D | S); break;				\
			case 0x11: out = ~(D | S); break;				\
			case 0x12: out = ~(S | ~(D ^ P)); break;			\
			case 0x13: out = ~(S | (D & P)); break;				\
			case 0x14: out = ~(D | ~(P ^ S)); break;			\
			case 0x15: out = ~(D | (P & S)); break;				\
			case 0x16: out = P ^ (S ^ (D & ~(P & S))); break;		\
			case 0x17: out = ~(S ^ ((S ^ P) & (D ^ S))); break;		\
			case 0x18: out = (S ^ P) & (P ^ D); break;			\
			case 0x19: out = ~(S ^ (D & ~(P & S))); break;			\
			case 0x1a: out = P ^ (D | (S & P)); break;			\
			case 0x1b: out = ~(S ^ (D & (P ^ S))); break;			\
			case 0x1c: out = P ^ (S | (D & P)); break;			\
			case 0x1d: out = ~(D ^ (S & (P ^ D))); break;			\
			case 0x1e: out = P ^ (D | S); break;				\
			case 0x1f: out = ~(P & (D | S)); break;				\
			case 0x20: out = D & (P & ~S); break;				\
			case 0x21: out = ~(S | (D ^ P)); break;				\
			case 0x22: out = D & ~S; break;					\
			case 0x23: out = ~(S | (P & ~D)); break;			\
			case 0x24: out = (S ^ P) & (D ^ S); break;			\
			case 0x25: out = ~(P ^ (D & ~(S & P))); break;			\
			case 0x26: out = S ^ (D | (P & S)); break;			\
			case 0x27: out = S ^ (D | ~(P ^ S)); break;			\
			case 0x28: out = D & (P ^ S); break;				\
			case 0x29: out = ~(P ^ (S ^ (D | (P & S)))); break;		\
			case 0x2a: out = D & ~(P & S); break;				\
			case 0x2b: out = ~(S ^ ((S ^ P) & (P ^ D))); break;		\
			case 0x2c: out = S ^ (P & (D | S)); break;			\
			case 0x2d: out = P ^ (S | ~D); break;				\
			case 0x2e: out = P ^ (S | (D ^ P)); break;			\
			case 0x2f: out = ~(P & (S | ~D)); break;			\
			case 0x30: out = P & ~S; break;					\
			case 0x31: out = ~(S | (D & ~P)); break;			\
			case 0x32: out = S ^ (D | (P | S)); break;			\
			case 0x33: out = ~S; break;					\
			case 0x34: out = S ^ (P | (D & S)); break;			\
			case 0x35: out = S ^ (P | ~(D ^ S)); break;			\
			case 0x36: out = S ^ (D | P); break;				\
			case 0x37: out = ~(S & (D | P)); break;				\
			case 0x38: out = P ^ (S & (D | P)); break;			\
			case 0x39: out = S ^ (P | ~D); break;				\
			case 0x3a: out = S ^ (P | (D ^ S)); break;			\
			case 0x3b: out = ~(S & (P | ~D)); break;			\
			case 0x3c: out = P ^ S; break;					\
			case 0x3d: out = S ^ (P | ~(D | S)); break;			\
			case 0x3e: out = S ^ (P | (D & ~S)); break;			\
			case 0x3f: out = ~(P & S); break;				\
			case 0x40: out = P & (S & ~D); break;				\
			case 0x41: out = ~(D | (P ^ S)); break;				\
			case 0x42: out = (S ^ D) & (P ^ D); break;			\
			case 0x43: out = ~(S ^ (P & ~(D & S))); break;			\
			case 0x44: out = S & ~D; break;					\
			case 0x45: out = ~(D | (P & ~S)); break;			\
			case 0x46: out = D ^ (S | (P & D)); break;			\
			case 0x47: out = ~(P ^ (S & (D ^ P))); break;			\
			case 0x48: out = S & (D ^ P); break;				\
			case 0x49: out = ~(P ^ (D ^ (S | (P & D)))); break;		\
			case 0x4a: out = D ^ (P & (S | D)); break;			\
			case 0x4b: out = P ^ (D | ~S); break;				\
			case 0x4c: out = S & ~(D & P); break;				\
			case 0x4d: out = ~(S ^ ((S ^ P) | (D ^ S))); break;		\
			case 0x4e: out = P ^ (D | (S ^ P)); break;			\
			case 0x4f: out = ~(P & (D | ~S)); break;			\
			case 0x50: out = P & ~D; break;					\
			case 0x51: out = ~(D | (S & ~P)); break;			\
			case 0x52: out = D ^ (P | (S & D)); break;			\
			case 0x53: out = ~(S ^ (P & (D ^ S))); break;			\
			case 0x54: out = ~(D | ~(P | S)); break;			\
			case 0x55: out = ~D; break;					\
			case 0x56: out = D ^ (P | S); break;				\
			case 0x57: out = ~(D & (P | S)); break;				\
			case 0x58: out = P ^ (D & (S | P)); break;			\
			case 0x59: out = D ^ (P | ~S); break;				\
			case 0x5a: out = D ^ P; break;					\
			case 0x5b: out = D ^ (P | ~(S | D)); break;			\
			case 0x5c: out = D ^ (P | (S ^ D)); break;			\
			case 0x5d: out = ~(D & (P | ~S)); break;			\
			case 0x5e: out = D ^ (P | (S & ~D)); break;			\
			case 0x5f: out = ~(D & P); break;				\
			case 0x60: out = P & (D ^ S); break;				\
			case 0x61: out = ~(D ^ (S ^ (P | (D & S)))); break;		\
			case 0x62: out = D ^ (S & (P | D)); break;			\
			case 0x63: out = S ^ (D | ~P); break;				\
			case 0x64: out = S ^ (D & (P | S)); break;			\
			case 0x65: out = D ^ (S | ~P); break;				\
			case 0x66: out = D ^ S; break;					\
			case 0x67: out = S ^ (D | ~(P | S)); break;			\
			case 0x68: out = ~(D ^ (S ^ (P | ~(D | S)))); break;		\
			case 0x69: out = ~(P ^ (D ^ S)); break;				\
			case 0x6a: out = D ^ (P & S); break;				\
			case 0x6b: out = ~(P ^ (S ^ (D & (P | S)))); break;		\
			case 0x6c: out = S ^ (D & P); break;				\
			case 0x6d: out = ~(P ^ (D ^ (S & (P | D)))); break;		\
			case 0x6e: out = S ^ (D & (P | ~S)); break;			\
			case 0x6f: out = ~(P & ~(D ^ S)); break;			\
			case 0x70: out = P & ~(D & S); break;				\
			case 0x71: out = ~(S ^ ((S ^ D) & (P ^ D))); break;		\
			case 0x72: out = S ^ (D | (P ^ S)); break;			\
			case 0x73: out = ~(S & (D | ~P)); break;			\
			case 0x74: out = D ^ (S | (P ^ D)); break;			\
			case 0x75: out = ~(D & (S | ~P)); break;			\
			case 0x76: out = S ^ (D | (P & ~S)); break;			\
			case 0x77: out = ~(D & S); break;				\
			case 0x78: out = P ^ (D & S); break;				\
			case 0x79: out = ~(D ^ (S ^ (P & (D | S)))); break;		\
			case 0x7a: out = D ^ (P & (S | ~D)); break;			\
			case 0x7b: out = ~(S & ~(D ^ P)); break;			\
			case 0x7c: out = S ^ (P & (D | ~S)); break;			\
			case 0x7d: out = ~(D & ~(P ^ S)); break;			\
			case 0x7e: out = (S ^ P) | (D ^ S); break;			\
			case 0x7f: out = ~(D & (P & S)); break;				\
			case 0x80: out = D & (P & S); break;				\
			case 0x81: out = ~((S ^ P) | (D ^ S)); break;			\
			case 0x82: out = D & ~(P ^ S); break;				\
			case 0x83: out = ~(S ^ (P & (D | ~S))); break;			\
			case 0x84: out = S & ~(D ^ P); break;				\
			case 0x85: out = ~(P ^ (D & (S | ~P))); break;			\
			case 0x86: out = D ^ (S ^ (P & (D | S))); break;		\
			case 0x87: out = ~(P ^ (D & S)); break;				\
			case 0x88: out = D & S; break;					\
			case 0x89: out = ~(S ^ (D | (P & ~S))); break;			\
			case 0x8a: out = D & (S | ~P); break;				\
			case 0x8b: out = ~(D ^ (S | (P ^ D))); break;			\
			case 0x8c: out = S & (D | ~P); break;				\
			case 0x8d: out = ~(S ^ (D | (P ^ S))); break;			\
			case 0x8e: out = S ^ ((S ^ D) & (P ^ D)); break;		\
			case 0x8f: out = ~(P & ~(D & S)); break;			\
			case 0x90: out = P & ~(D ^ S); break;				\
			case 0x91: out = ~(S ^ (D & (P | ~S))); break;			\
			case 0x92: out = D ^ (P ^ (S & (D | P))); break;		\
			case 0x93: out = ~(S ^ (P & D)); break;				\
			case 0x94: out = P ^ (S ^ (D & (P | S))); break;		\
			case 0x95: out = ~(D ^ (P & S)); break;				\
			case 0x96: out = D ^ (P ^ S); break;				\
			case 0x97: out = P ^ (S ^ (D | ~(P | S))); break;		\
			case 0x98: out = ~(S ^ (D | ~(P | S))); break;			\
			case 0x99: out = ~(D ^ S); break;				\
			case 0x9a: out = D ^ (P & ~S); break;				\
			case 0x9b: out = ~(S ^ (D & (P | S))); break;			\
			case 0x9c: out = S ^ (P & ~D); break;				\
			case 0x9d: out = ~(D ^ (S & (P | D))); break;			\
			case 0x9e: out = D ^ (S ^ (P | (D & S))); break;		\
			case 0x9f: out = ~(P & (D ^ S)); break;				\
			case 0xa0: out = D & P; break;					\
			case 0xa1: out = ~(P ^ (D | (S & ~P))); break;			\
			case 0xa2: out = D & (P | ~S); break;				\
			case 0xa3: out = ~(D ^ (P | (S ^ D))); break;			\
			case 0xa4: out = ~(P ^ (D | ~(S | P))); break;			\
			case 0xa5: out = ~(P ^ D); break;				\
			case 0xa6: out = D ^ (S & ~P); break;				\
			case 0xa7: out = ~(P ^ (D & (S | P))); break;			\
			case 0xa8: out = D & (P | S); break;				\
			case 0xa9: out = ~(D ^ (P | S)); break;				\
			case 0xaa: out = D; break;					\
			case 0xab: out = D | ~(P | S); break;				\
			case 0xac: out = S ^ (P & (D ^ S)); break;			\
			case 0xad: out = ~(D ^ (P | (S & D))); break;			\
			case 0xae: out = D | (S & ~P); break;				\
			case 0xaf: out = D | ~P; break;					\
			case 0xb0: out = P & (D | ~S); break;				\
			case 0xb1: out = ~(P ^ (D | (S ^ P))); break;			\
			case 0xb2: out = S ^ ((S ^ P) | (D ^ S)); break;		\
			case 0xb3: out = ~(S & ~(D & P)); break;			\
			case 0xb4: out = P ^ (S & ~D); break;				\
			case 0xb5: out = ~(D ^ (P & (S | D))); break;			\
			case 0xb6: out = D ^ (P ^ (S | (D & P))); break;		\
			case 0xb7: out = ~(S & (D ^ P)); break;				\
			case 0xb8: out = P ^ (S & (D ^ P)); break;			\
			case 0xb9: out = ~(D ^ (S | (P & D))); break;			\
			case 0xba: out = D | (P & ~S); break;				\
			case 0xbb: out = D | ~S; break;					\
			case 0xbc: out = S ^ (P & ~(D & S)); break;			\
			case 0xbd: out = ~((S ^ D) & (P ^ D)); break;			\
			case 0xbe: out = D | (P ^ S); break;				\
			case 0xbf: out = D | ~(P & S); break;				\
			case 0xc0: out = P & S; break;					\
			case 0xc1: out = ~(S ^ (P | (D & ~S))); break;			\
			case 0xc2: out = ~(S ^ (P | ~(D | S))); break;			\
			case 0xc3: out = ~(P ^ S); break;				\
			case 0xc4: out = S & (P | ~D); break;				\
			case 0xc5: out = ~(S ^ (P | (D ^ S))); break;			\
			case 0xc6: out = S ^ (D & ~P); break;				\
			case 0xc7: out = ~(P ^ (S & (D | P))); break;			\
			case 0xc8: out = S & (D | P); break;				\
			case 0xc9: out = ~(S ^ (P | D)); break;				\
			case 0xca: out = D ^ (P & (S ^ D)); break;			\
			case 0xcb: out = ~(S ^ (P | (D & S))); break;			\
			case 0xcc: out = S; break;					\
			case 0xcd: out = S | ~(D | P); break;				\
			case 0xce: out = S | (D & ~P); break;				\
			case 0xcf: out = S | ~P; break;					\
			case 0xd0: out = P & (S | ~D); break;				\
			case 0xd1: out = ~(P ^ (S | (D ^ P))); break;			\
			case 0xd2: out = P ^ (D & ~S); break;				\
			case 0xd3: out = ~(S ^ (P & (D | S))); break;			\
			case 0xd4: out = S ^ ((S ^ P) & (P ^ D)); break;		\
			case 0xd5: out = ~(D & ~(P & S)); break;			\
			case 0xd6: out = P ^ (S ^ (D | (P & S))); break;		\
			case 0xd7: out = ~(D & (P ^ S)); break;				\
			case 0xd8: out = P ^ (D & (S ^ P)); break;			\
			case 0xd9: out = ~(S ^ (D | (P & S))); break;			\
			case 0xda: out = D ^ (P & ~(S & D)); break;			\
			case 0xdb: out = ~((S ^ P) & (D ^ S)); break;			\
			case 0xdc: out = S | (P & ~D); break;				\
			case 0xdd: out = S | ~D; break;					\
			case 0xde: out = S | (D ^ P); break;				\
			case 0xdf: out = S | ~(D & P); break;				\
			case 0xe0: out = P & (D | S); break;				\
			case 0xe1: out = ~(P ^ (D | S)); break;				\
			case 0xe2: out = D ^ (S & (P ^ D)); break;			\
			case 0xe3: out = ~(P ^ (S | (D & P))); break;			\
			case 0xe4: out = S ^ (D & (P ^ S)); break;			\
			case 0xe5: out = ~(P ^ (D | (S & P))); break;			\
			case 0xe6: out = S ^ (D & ~(P & S)); break;			\
			case 0xe7: out = ~((S ^ P) & (P ^ D)); break;			\
			case 0xe8: out = S ^ ((S ^ P) & (D ^ S)); break;		\
			case 0xe9: out = ~(D ^ (S ^ (P & ~(D & S)))); break;		\
			case 0xea: out = D | (P & S); break;				\
			case 0xeb: out = D | ~(P ^ S); break;				\
			case 0xec: out = S | (D & P); break;				\
			case 0xed: out = S | ~(D ^ P); break;				\
			case 0xee: out = D | S; break;					\
			case 0xef: out = S | (D | ~P); break;				\
			case 0xf0: out = P; break;					\
			case 0xf1: out = P | ~(D | S); break;				\
			case 0xf2: out = P | (D & ~S); break;				\
			case 0xf3: out = P | ~S; break;					\
			case 0xf4: out = P | (S & ~D); break;				\
			case 0xf5: out = P | ~D; break;					\
			case 0xf6: out = P | (D ^ S); break;				\
			case 0xf7: out = P | ~(D & S); break;				\
			case 0xf8: out = P | (D & S); break;				\
			case 0xf9: out = P | ~(D ^ S); break;				\
			case 0xfa: out = D | P; break; 					\
			case 0xfb: out = D | (P | ~S); break; 				\
			case 0xfc: out = P | S; break; 					\
			case 0xfd: out = P | (S | ~D); break; 				\
			case 0xfe: out = D | (P | S); break; 				\
			case 0xff: out = ~0; break; 					\
		}									\
	}

static void
et4000w32_blit(int count, int cpu_input, uint32_t src_dat, uint32_t mix_dat, et4000w32p_t *et4000)
{
    svga_t *svga = &et4000->svga;
    uint8_t pattern, source, dest;
    uint8_t rop;
	uint8_t out;
	int mixmap;

	while (count-- && et4000->acl.y_count >= 0) {
		pattern	= svga->vram[(et4000->acl.pattern_addr + et4000->acl.pattern_x) & et4000->vram_mask];

		if (cpu_input == 1) {
			source = src_dat & 0xff;
			src_dat >>= 8;
		} else /*The source data is from the display memory if the Control Routing register is not set to 1*/
			source = svga->vram[(et4000->acl.source_addr + et4000->acl.source_x) & et4000->vram_mask];

		dest = svga->vram[et4000->acl.dest_addr & et4000->vram_mask];
		mixmap = mix_dat & 1;

		/*Now determine the Raster Operation*/
		rop = mixmap ? et4000->acl.internal.rop_fg : et4000->acl.internal.rop_bg;
		mix_dat >>= 1;
		mix_dat |= 0x80000000;

		ROPMIX(rop, dest, pattern, source, out);

		/*Write the data*/
		svga->vram[et4000->acl.dest_addr & et4000->vram_mask] = out;
		svga->changedvram[(et4000->acl.dest_addr & et4000->vram_mask) >> 12] = changeframecount;

		if (et4000->acl.internal.xy_dir & 1) {
			et4000->acl.dest_addr--;
			et4000->acl.pattern_x--;
			et4000->acl.source_x--;
			if (et4000->acl.pattern_x < 0)
				et4000->acl.pattern_x += (et4000w32_wrap_x[et4000->acl.internal.pattern_wrap & 7] + 1);
			if (et4000->acl.source_x < 0)
				et4000->acl.source_x += (et4000w32_wrap_x[et4000->acl.internal.source_wrap & 7] + 1);
		} else {
			et4000->acl.dest_addr++;
			et4000->acl.pattern_x++;
			et4000->acl.source_x++;
			if (et4000->acl.pattern_x >= (et4000w32_wrap_x[et4000->acl.internal.pattern_wrap & 7] + 1))
				et4000->acl.pattern_x -= (et4000w32_wrap_x[et4000->acl.internal.pattern_wrap & 7] + 1);
			if (et4000->acl.source_x >= (et4000w32_wrap_x[et4000->acl.internal.source_wrap & 7] + 1))
				et4000->acl.source_x -= (et4000w32_wrap_x[et4000->acl.internal.source_wrap & 7] + 1);
		}

		et4000->acl.x_count--;
		if (et4000->acl.x_count == 0xffff) {
			et4000->acl.x_count = et4000->acl.internal.count_x;

			if (et4000->acl.internal.xy_dir & 2) {
				et4000->acl.pattern_addr -= (et4000->acl.internal.pattern_off + 1);
				et4000->acl.source_addr	-= (et4000->acl.internal.source_off + 1);
				et4000->acl.dest_addr -= (et4000->acl.internal.dest_off + 1);
				et4000->acl.pattern_y--;
				if ((et4000->acl.pattern_y < 0) && !(et4000->acl.internal.pattern_wrap & 0x40)) {
					et4000->acl.pattern_y = et4000w32_wrap_y[(et4000->acl.internal.pattern_wrap >> 4) & 7] - 1;
					et4000->acl.pattern_addr = et4000->acl.pattern_back + (et4000w32_wrap_x[et4000->acl.internal.pattern_wrap & 7] * (et4000w32_wrap_y[(et4000->acl.internal.pattern_wrap >> 4) & 7] - 1));
				}
				et4000->acl.source_y--;
				if ((et4000->acl.source_y < 0) && !(et4000->acl.internal.source_wrap & 0x40)) {
					et4000->acl.source_y = et4000w32_wrap_y[(et4000->acl.internal.source_wrap >> 4) & 7] - 1;
					et4000->acl.source_addr	= et4000->acl.source_back + (et4000w32_wrap_x[et4000->acl.internal.source_wrap & 7] * (et4000w32_wrap_y[(et4000->acl.internal.source_wrap >> 4) & 7] - 1));
				}
				et4000->acl.dest_back = et4000->acl.dest_addr = et4000->acl.dest_back - (et4000->acl.internal.dest_off + 1);
			} else {
				et4000->acl.pattern_addr += (et4000->acl.internal.pattern_off + 1);
				et4000->acl.source_addr	+= (et4000->acl.internal.source_off + 1);
				et4000->acl.dest_addr += (et4000->acl.internal.dest_off + 1);
				et4000->acl.pattern_y++;
				if (et4000->acl.pattern_y == et4000w32_wrap_y[(et4000->acl.internal.pattern_wrap >> 4) & 7]) {
					et4000->acl.pattern_y = 0;
					et4000->acl.pattern_addr = et4000->acl.pattern_back;
				}
				et4000->acl.source_y++;
				if (et4000->acl.source_y == et4000w32_wrap_y[(et4000->acl.internal.source_wrap >> 4) & 7]) {
					et4000->acl.source_y = 0;
					et4000->acl.source_addr = et4000->acl.source_back;
				}
				et4000->acl.dest_back = et4000->acl.dest_addr = et4000->acl.dest_back + (et4000->acl.internal.dest_off + 1);
			}

			et4000->acl.pattern_x = et4000->acl.pattern_x_back;
			et4000->acl.source_x = et4000->acl.source_x_back;

			et4000->acl.y_count--;
			if (et4000->acl.y_count == 0xffff) {
				et4000->acl.status &= ~ACL_XYST;
				if (!(et4000->acl.internal.ctrl_routing & 7) || (et4000->acl.internal.ctrl_routing & 4)) {
					et4000w32_log("W32i: end blit, xcount = %i\n", et4000->acl.x_count);
					et4000->acl.status &= ~ACL_SSO;
				}
				et4000->acl.cpu_input_num = 0;
				return;
			}

			if (cpu_input)
				return;
		}
	}
}

static void
et4000w32p_blit(int count, uint32_t mix, uint32_t sdat, int cpu_input, et4000w32p_t *et4000)
{
    svga_t *svga = &et4000->svga;
    uint8_t pattern, source, dest, out;
    uint8_t rop;
    int mixdat;

    if (!(et4000->acl.status & ACL_XYST)) {
		et4000w32_log("XY Block not started\n");
		return;
	}

    if (et4000->acl.internal.xy_dir & 0x80) {	/* Line draw */
	et4000w32_log("Line draw\n");
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

		ROPMIX(rop, dest, pattern, source, out);

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
		et4000w32_log("BitBLT: count = %i\n", count);
		while (count-- && et4000->acl.y_count >= 0) {
			pattern	= svga->vram[(et4000->acl.pattern_addr + et4000->acl.pattern_x) & et4000->vram_mask];

			if (cpu_input == 2) {
				source = sdat & 0xff;
				sdat >>= 8;
			} else
				source = svga->vram[(et4000->acl.source_addr  + et4000->acl.source_x) & et4000->vram_mask];

			dest = svga->vram[et4000->acl.dest_addr & et4000->vram_mask];
			out = 0;

			if ((et4000->acl.internal.ctrl_routing & 0xa) == 8) {
				mixdat = svga->vram[(et4000->acl.mix_addr >> 3) & et4000->vram_mask] & (1 << (et4000->acl.mix_addr & 7));
			} else {
				mixdat = mix & 1;
				mix >>= 1;
				mix |= 0x80000000;
			}

			rop = mixdat ? et4000->acl.internal.rop_fg : et4000->acl.internal.rop_bg;

			ROPMIX(rop, dest, pattern, source, out);

			if (!(et4000->acl.internal.ctrl_routing & 0x40)) {
				svga->vram[et4000->acl.dest_addr & et4000->vram_mask] = out;
				svga->changedvram[(et4000->acl.dest_addr & et4000->vram_mask) >> 12] = changeframecount;
			} else {
				et4000->acl.cpu_dat |= ((uint64_t)out << (et4000->acl.cpu_dat_pos * 8));
				et4000->acl.cpu_dat_pos++;
			}

			if (et4000->acl.internal.xy_dir & 1)
				et4000w32_decx(1, et4000);
			else
				et4000w32_incx(1, et4000);

			et4000->acl.x_count--;
			if (et4000->acl.x_count == 0xffff) {
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

				et4000->acl.y_count--;
				et4000->acl.x_count = et4000->acl.internal.count_x;
				if (et4000->acl.y_count == 0xffff) {
					et4000w32_log("BitBLT end\n");
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
			uint32_t biosaddr = (et4000->pci_regs[0x33] << 24);
			if (!biosaddr)
				biosaddr = 0xc0000;
			et4000w32_log("ET4000 bios_rom enabled at %08x\n", biosaddr);
			mem_mapping_set_addr(&et4000->bios_rom.mapping, biosaddr, 0x8000);
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

	case ET4000W32P_VIDEOMAGIC_REVB:
		/* ET4000/W32p rev B */
		et4000->rev = 5;

		rom_init(&et4000->bios_rom, BIOS_ROM_PATH_W32P_VIDEOMAGIC_REVB_VLB, 0xc0000, 0x8000, 0x7fff, 0,
			 MEM_MAPPING_EXTERNAL);

		et4000->svga.ramdac = device_add(&stg_ramdac_device);
		et4000->svga.clock_gen = et4000->svga.ramdac;
		et4000->svga.getclock = stg_getclock;
		et4000->svga.adv_flags |= FLAG_NOSKEW;
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
et4000w32p_videomagic_revb_vlb_available(void)
{
    return rom_present(BIOS_ROM_PATH_W32P_VIDEOMAGIC_REVB_VLB);
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

static const device_config_t et4000w32p_config[] = {
// clang-format off
    {
        .name = "memory",
        .description = "Memory size",
        .type = CONFIG_SELECTION,
        .default_int = 2,
        .selection = {
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
        }
    },
    {
        .type = CONFIG_END
    }
// clang-format on
};

const device_t et4000w32_device = {
    .name = "Tseng Labs ET4000/w32 ISA",
    .internal_name = "et4000w32",
    .flags = DEVICE_ISA | DEVICE_AT, ET4000W32,
    .init = et4000w32p_init,
    .close = et4000w32p_close,
    .reset = NULL,
    { .available = et4000w32_available },
    .speed_changed = et4000w32p_speed_changed,
    .force_redraw = et4000w32p_force_redraw,
    .config = NULL
};

const device_t et4000w32_onboard_device = {
    .name = "Tseng Labs ET4000/w32 (ISA) (On-Board)",
    .internal_name = "et4000w32_onboard",
    .flags = DEVICE_ISA | DEVICE_AT, ET4000W32,
    .init = et4000w32p_init,
    .close = et4000w32p_close,
    .reset = NULL,
    { .available = et4000w32_available },
    .speed_changed = et4000w32p_speed_changed,
    .force_redraw = et4000w32p_force_redraw,
    .config = NULL
};

const device_t et4000w32i_isa_device = {
    .name = "Tseng Labs ET4000/w32i Rev. B ISA",
    .internal_name = "et4000w32i",
    .flags = DEVICE_ISA | DEVICE_AT, ET4000W32I,
    .init = et4000w32p_init,
    .close = et4000w32p_close,
    .reset = NULL,
    { .available = et4000w32i_isa_available },
    .speed_changed = et4000w32p_speed_changed,
    .force_redraw = et4000w32p_force_redraw,
    .config = NULL
};

const device_t et4000w32i_vlb_device = {
    .name = "Tseng Labs ET4000/w32i Rev. B VLB",
    .internal_name = "et4000w32i_vlb",
    .flags = DEVICE_VLB, ET4000W32I,
    .init = et4000w32p_init,
    .close = et4000w32p_close,
    .reset = NULL,
    { .available = et4000w32i_vlb_available },
    .speed_changed = et4000w32p_speed_changed,
    .force_redraw = et4000w32p_force_redraw,
    .config = et4000w32p_config
};

const device_t et4000w32p_videomagic_revb_vlb_device = {
    .name = "Tseng Labs ET4000/w32p Rev. B VLB (VideoMagic)",
    .internal_name = "et4000w32p_videomagic_revb_vlb",
    .flags = DEVICE_VLB, ET4000W32P_VIDEOMAGIC_REVB,
    .init = et4000w32p_init,
    .close = et4000w32p_close,
    .reset = NULL,
    { .available = et4000w32p_videomagic_revb_vlb_available },
    .speed_changed = et4000w32p_speed_changed,
    .force_redraw = et4000w32p_force_redraw,
    .config = et4000w32p_config
};

const device_t et4000w32p_videomagic_revb_pci_device = {
    .name = "Tseng Labs ET4000/w32p Rev. B PCI (VideoMagic)",
    .internal_name = "et4000w32p_videomagic_revb_pci",
    .flags = DEVICE_PCI, ET4000W32P_VIDEOMAGIC_REVB,
    .init = et4000w32p_init,
    .close = et4000w32p_close,
    .reset = NULL,
    { .available = et4000w32p_videomagic_revb_vlb_available },
    .speed_changed = et4000w32p_speed_changed,
    .force_redraw = et4000w32p_force_redraw,
    .config = et4000w32p_config
};

const device_t et4000w32p_revc_vlb_device = {
    .name = "Tseng Labs ET4000/w32p Rev. C VLB (Cardex)",
    .internal_name = "et4000w32p_revc_vlb",
    .flags = DEVICE_VLB, ET4000W32P_REVC,
    .init = et4000w32p_init,
    .close = et4000w32p_close,
    .reset = NULL,
    { .available = et4000w32p_revc_available },
    .speed_changed = et4000w32p_speed_changed,
    .force_redraw = et4000w32p_force_redraw,
    .config = et4000w32p_config
};

const device_t et4000w32p_revc_pci_device = {
    .name = "Tseng Labs ET4000/w32p Rev. C PCI (Cardex)",
    .internal_name = "et4000w32p_revc_pci",
    .flags = DEVICE_PCI, ET4000W32P_REVC,
    .init = et4000w32p_init,
    .close = et4000w32p_close,
    .reset = NULL,
    { .available = et4000w32p_revc_available },
    .speed_changed = et4000w32p_speed_changed,
    .force_redraw = et4000w32p_force_redraw,
    .config = et4000w32p_config
};

const device_t et4000w32p_noncardex_vlb_device = {
    .name = "Tseng Labs ET4000/w32p Rev. D VLB",
    .internal_name = "et4000w32p_nc_vlb",
    .flags = DEVICE_VLB, ET4000W32P,
    .init = et4000w32p_init,
    .close = et4000w32p_close,
    .reset = NULL,
    { .available = et4000w32p_noncardex_available },
    .speed_changed = et4000w32p_speed_changed,
    .force_redraw = et4000w32p_force_redraw,
    .config = et4000w32p_config
};

const device_t et4000w32p_noncardex_pci_device = {
    .name = "Tseng Labs ET4000/w32p Rev. D PCI",
    .internal_name = "et4000w32p_nc_pci",
    .flags = DEVICE_PCI, ET4000W32P,
    .init = et4000w32p_init,
    .close = et4000w32p_close,
    .reset = NULL,
    { .available = et4000w32p_noncardex_available },
    .speed_changed = et4000w32p_speed_changed,
    .force_redraw = et4000w32p_force_redraw,
    .config = et4000w32p_config
};

const device_t et4000w32p_cardex_vlb_device = {
    .name = "Tseng Labs ET4000/w32p Rev. D VLB (Cardex)",
    .internal_name = "et4000w32p_vlb",
    .flags = DEVICE_VLB, ET4000W32P_CARDEX,
    .init = et4000w32p_init,
    .close = et4000w32p_close,
    .reset = NULL,
    { .available = et4000w32p_cardex_available },
    .speed_changed = et4000w32p_speed_changed,
    .force_redraw = et4000w32p_force_redraw,
    .config = et4000w32p_config
};

const device_t et4000w32p_cardex_pci_device = {
    .name = "Tseng Labs ET4000/w32p Rev. D PCI (Cardex)",
    .internal_name = "et4000w32p_pci",
    .flags = DEVICE_PCI, ET4000W32P_CARDEX,
    .init = et4000w32p_init,
    .close = et4000w32p_close,
    .reset = NULL,
    { .available = et4000w32p_cardex_available },
    .speed_changed = et4000w32p_speed_changed,
    .force_redraw = et4000w32p_force_redraw,
    .config = et4000w32p_config
};

const device_t et4000w32p_vlb_device = {
    .name = "Tseng Labs ET4000/w32p Rev. D VLB (Diamond Stealth32)",
    .internal_name = "stealth32_vlb",
    .flags = DEVICE_VLB, ET4000W32P_DIAMOND,
    .init = et4000w32p_init,
    .close = et4000w32p_close,
    .reset = NULL,
    { .available = et4000w32p_available },
    .speed_changed = et4000w32p_speed_changed,
    .force_redraw = et4000w32p_force_redraw,
    .config = et4000w32p_config
};

const device_t et4000w32p_pci_device = {
    .name = "Tseng Labs ET4000/w32p Rev. D PCI (Diamond Stealth32)",
    .internal_name = "stealth32_pci",
    .flags = DEVICE_PCI, ET4000W32P_DIAMOND,
    .init = et4000w32p_init,
    .close = et4000w32p_close,
    .reset = NULL,
    { .available = et4000w32p_available },
    .speed_changed = et4000w32p_speed_changed,
    .force_redraw = et4000w32p_force_redraw,
    .config = et4000w32p_config
};
