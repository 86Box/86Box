/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Video 7 VGA 1024i emulation.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2019 Sarah Walker.
 *		Copyright 2019 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/io.h>
#include <86box/mca.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/pic.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>


typedef struct ht216_t
{
    svga_t		svga;

    mem_mapping_t	linear_mapping;

    rom_t		bios_rom;

    uint32_t		vram_mask, linear_base;
    uint8_t		adjust_cursor, monitor_type;

    int			ext_reg_enable;
	int			isabus;
	int			mca;

    uint8_t		read_bank_reg[2], write_bank_reg[2];
    uint16_t		id, misc;
    uint32_t		read_banks[2], write_banks[2];

    uint8_t		bg_latch[8];
    uint8_t 		fg_latch[4];
    uint8_t		bg_plane_sel, fg_plane_sel;

    uint8_t		ht_regs[256];
    uint8_t		extensions, reg_3cb;

    uint8_t		pos_regs[8];
} ht216_t;


#define HT_MISC_PAGE_SEL (1 << 5)

/*Shifts CPU VRAM read address by 3 bits, for use with fat pixel color expansion*/
#define HT_REG_C8_MOVSB (1 << 0)
#define HT_REG_C8_E256  (1 << 4)
#define HT_REG_C8_XLAM  (1 << 6)

#define HT_REG_CD_P8PCEXP  (1 << 0)
#define HT_REG_CD_FP8PCEXP (1 << 1)
#define HT_REG_CD_BMSKSL   (3 << 2)
#define HT_REG_CD_RMWMDE   (1 << 5)
/*Use GDC data rotate as offset when reading VRAM data into latches*/
#define HT_REG_CD_ASTODE   (1 << 6)
#define HT_REG_CD_EXALU    (1 << 7)

#define HT_REG_E0_SBAE  (1 << 7)

#define HT_REG_F9_XPSEL (1 << 0)

/*Enables A[14:15] of VRAM address in chain-4 modes*/
#define HT_REG_FC_ECOLRE (1 << 2)

#define HT_REG_FE_FBRC  (1 << 1)
#define HT_REG_FE_FBMC  (3 << 2)
#define HT_REG_FE_FBRSL (3 << 4)


void ht216_remap(ht216_t *ht216);

void ht216_out(uint16_t addr, uint8_t val, void *p);
uint8_t ht216_in(uint16_t addr, void *p);


#define BIOS_G2_GC205_PATH			"roms/video/video7/BIOS.BIN"
#define BIOS_VIDEO7_VGA_1024I_PATH		"roms/video/video7/Video Seven VGA 1024i - BIOS - v2.19 - 435-0062-05 - U17 - 27C256.BIN"
#define BIOS_RADIUS_SVGA_MULTIVIEW_PATH	"roms/video/video7/U18.BIN"
#define BIOS_HT216_32_PATH			"roms/video/video7/HT21632.BIN"

static video_timings_t	timing_v7vga_isa = {VIDEO_ISA, 3,  3,  6,   5,  5, 10};
static video_timings_t	timing_v7vga_mca = {VIDEO_MCA, 4,  5, 10,   5,  5, 10};
static video_timings_t	timing_v7vga_vlb = {VIDEO_BUS, 5,  5,  9,  20, 20, 30};


#ifdef ENABLE_HT216_LOG
int ht216_do_log = ENABLE_HT216_LOG;


static void
ht216_log(const char *fmt, ...)
{
    va_list ap;

    if (ht216_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define ht216_log(fmt, ...)
#endif

/*Remap address for chain-4/doubleword style layout*/
static __inline uint32_t
dword_remap(svga_t *svga, uint32_t in_addr)
{
	if (svga->packed_chain4)
		return in_addr;
	return ((in_addr & 0xfffc) << 2) | ((in_addr & 0x30000) >> 14) | (in_addr & ~0x3ffff);
}

static void
ht216_recalc_bank_regs(ht216_t *ht216, int mode)
{
	svga_t *svga = &ht216->svga;

	if (mode) {
	ht216->read_bank_reg[0] = ht216->ht_regs[0xe8];
	ht216->write_bank_reg[0] = ht216->ht_regs[0xe8];
	ht216->read_bank_reg[1] = ht216->ht_regs[0xe9];
	ht216->write_bank_reg[1] = ht216->ht_regs[0xe9];
    } else {
	ht216->read_bank_reg[0]  = ((ht216->ht_regs[0xf6] & 0xc) << 4);
	ht216->read_bank_reg[1]  = ((ht216->ht_regs[0xf6] & 0xc) << 4);
	ht216->write_bank_reg[0] = ((ht216->ht_regs[0xf6] & 0x3) << 6);
	ht216->write_bank_reg[1] = ((ht216->ht_regs[0xf6] & 0x3) << 6);

	if (svga->packed_chain4 || (ht216->ht_regs[0xfc] & HT_REG_FC_ECOLRE)) {
		ht216->read_bank_reg[0]  |= (ht216->misc & 0x20);
		ht216->read_bank_reg[1]  |= (ht216->misc & 0x20);
		ht216->write_bank_reg[0] |= (ht216->misc & 0x20);
		ht216->write_bank_reg[1] |= (ht216->misc & 0x20);
	}

	if (svga->packed_chain4 || ((ht216->ht_regs[0xfc] & 0x06) == 0x04)) {
		ht216->read_bank_reg[0]  |= ((ht216->ht_regs[0xf9] & 1) << 4);
		ht216->read_bank_reg[1]  |= ((ht216->ht_regs[0xf9] & 1) << 4);
		ht216->write_bank_reg[0] |= ((ht216->ht_regs[0xf9] & 1) << 4);
		ht216->write_bank_reg[1] |= ((ht216->ht_regs[0xf9] & 1) << 4);
	}
    }
}


void
ht216_out(uint16_t addr, uint8_t val, void *p)
{
    ht216_t *ht216 = (ht216_t *)p;
    svga_t *svga = &ht216->svga;
    uint8_t old;

    ht216_log("ht216 %i out %04X %02X %04X:%04X\n", svga->miscout & 1, addr, val, CS, cpu_state.pc);

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
	addr ^= 0x60;

    switch (addr) {
	case 0x3c2:
		/*Bit 17 of the display memory address, only active on odd/even modes, has no effect on graphics modes.*/
		ht216->misc = val;
		svga->miscout = val;
		ht216_log("HT216 misc val = %02x, mode = 0, chain4 = %x\n", val, svga->chain4);
		ht216_recalc_bank_regs(ht216, 0);
		ht216_remap(ht216);
		svga_recalctimings(svga);
		break;

	case 0x3c4:
		svga->seqaddr = val;
		break;

	case 0x3c5:
		if (svga->seqaddr == 4) {
			svga->chain2_write = !(val & 4);
			svga->chain4 = val & 8;
			ht216_remap(ht216);
		} else if (svga->seqaddr == 6) {
			if (val == 0xea)
				ht216->ext_reg_enable = 1;
			else if (val == 0xae)
				ht216->ext_reg_enable = 0;
#ifdef ENABLE_HT216_LOG
		/* Functionality to output to the console a dump of all registers for debugging purposes. */
		} else if (svga->seqaddr == 0x7f) {
			ht216_log(" 8  |   0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n");
			ht216_log("----+-------------------------------------------------\n");
			ht216_log(" 8  |  %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
				  ht216->ht_regs[0x80], ht216->ht_regs[0x81], ht216->ht_regs[0x82], ht216->ht_regs[0x83],
				  ht216->ht_regs[0x84], ht216->ht_regs[0x85], ht216->ht_regs[0x86], ht216->ht_regs[0x87],
				  ht216->ht_regs[0x88], ht216->ht_regs[0x89], ht216->ht_regs[0x8a], ht216->ht_regs[0x8b],
				  ht216->ht_regs[0x8c], ht216->ht_regs[0x8d], ht216->ht_regs[0x8e], ht216->ht_regs[0x8f]);
			ht216_log(" 9  |  %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
				  ht216->ht_regs[0x90], ht216->ht_regs[0x91], ht216->ht_regs[0x92], ht216->ht_regs[0x93],
				  ht216->ht_regs[0x94], ht216->ht_regs[0x95], ht216->ht_regs[0x96], ht216->ht_regs[0x97],
				  ht216->ht_regs[0x98], ht216->ht_regs[0x99], ht216->ht_regs[0x9a], ht216->ht_regs[0x9b],
				  ht216->ht_regs[0x9c], ht216->ht_regs[0x9d], ht216->ht_regs[0x9e], ht216->ht_regs[0x9f]);
			ht216_log(" A  |  %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
				  ht216->ht_regs[0xa0], ht216->ht_regs[0xa1], ht216->ht_regs[0xa2], ht216->ht_regs[0xa3],
				  ht216->ht_regs[0xa4], ht216->ht_regs[0xa5], ht216->ht_regs[0xa6], ht216->ht_regs[0xa7],
				  ht216->ht_regs[0xa8], ht216->ht_regs[0xa9], ht216->ht_regs[0xaa], ht216->ht_regs[0xab],
				  ht216->ht_regs[0xac], ht216->ht_regs[0xad], ht216->ht_regs[0xae], ht216->ht_regs[0xaf]);
			ht216_log(" B  |  %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
				  ht216->ht_regs[0xb0], ht216->ht_regs[0xb1], ht216->ht_regs[0xb2], ht216->ht_regs[0xb3],
				  ht216->ht_regs[0xb4], ht216->ht_regs[0xb5], ht216->ht_regs[0xb6], ht216->ht_regs[0xb7],
				  ht216->ht_regs[0xb8], ht216->ht_regs[0xb9], ht216->ht_regs[0xba], ht216->ht_regs[0xbb],
				  ht216->ht_regs[0xbc], ht216->ht_regs[0xbd], ht216->ht_regs[0xbe], ht216->ht_regs[0xbf]);
			ht216_log(" C  |  %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
				  ht216->ht_regs[0xc0], ht216->ht_regs[0xc1], ht216->ht_regs[0xc2], ht216->ht_regs[0xc3],
				  ht216->ht_regs[0xc4], ht216->ht_regs[0xc5], ht216->ht_regs[0xc6], ht216->ht_regs[0xc7],
				  ht216->ht_regs[0xc8], ht216->ht_regs[0xc9], ht216->ht_regs[0xca], ht216->ht_regs[0xcb],
				  ht216->ht_regs[0xcc], ht216->ht_regs[0xcd], ht216->ht_regs[0xce], ht216->ht_regs[0xcf]);
			ht216_log(" D  |  %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
				  ht216->ht_regs[0xd0], ht216->ht_regs[0xd1], ht216->ht_regs[0xd2], ht216->ht_regs[0xd3],
				  ht216->ht_regs[0xd4], ht216->ht_regs[0xd5], ht216->ht_regs[0xd6], ht216->ht_regs[0xd7],
				  ht216->ht_regs[0xd8], ht216->ht_regs[0xd9], ht216->ht_regs[0xda], ht216->ht_regs[0xdb],
				  ht216->ht_regs[0xdc], ht216->ht_regs[0xdd], ht216->ht_regs[0xde], ht216->ht_regs[0xdf]);
			ht216_log(" E  |  %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
				  ht216->ht_regs[0xe0], ht216->ht_regs[0xe1], ht216->ht_regs[0xe2], ht216->ht_regs[0xe3],
				  ht216->ht_regs[0xe4], ht216->ht_regs[0xe5], ht216->ht_regs[0xe6], ht216->ht_regs[0xe7],
				  ht216->ht_regs[0xe8], ht216->ht_regs[0xe9], ht216->ht_regs[0xea], ht216->ht_regs[0xeb],
				  ht216->ht_regs[0xec], ht216->ht_regs[0xed], ht216->ht_regs[0xee], ht216->ht_regs[0xef]);
			ht216_log(" F  |  %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
				  ht216->ht_regs[0xf0], ht216->ht_regs[0xf1], ht216->ht_regs[0xf2], ht216->ht_regs[0xf3],
				  ht216->ht_regs[0xf4], ht216->ht_regs[0xf5], ht216->ht_regs[0xf6], ht216->ht_regs[0xf7],
				  ht216->ht_regs[0xf8], ht216->ht_regs[0xf9], ht216->ht_regs[0xfa], ht216->ht_regs[0xfb],
				  ht216->ht_regs[0xfc], ht216->ht_regs[0xfd], ht216->ht_regs[0xfe], ht216->ht_regs[0xff]);
			return;
#endif
		} else if (svga->seqaddr >= 0x80 && ht216->ext_reg_enable) {
			old = ht216->ht_regs[svga->seqaddr & 0xff];
			ht216->ht_regs[svga->seqaddr & 0xff] = val;

			switch (svga->seqaddr & 0xff) {
				case 0x83:
					svga->attraddr = val & 0x1f;
					svga->attrff = !!(val & 0x80);
					break;

				case 0x94:
				case 0xff:
					svga->hwcursor.addr = ((ht216->ht_regs[0x94] << 6) | 0xc000 | ((ht216->ht_regs[0xff] & 0x60) << 11)) << 2;
					svga->hwcursor.addr &= svga->vram_mask;
					if (svga->crtc[0x17] == 0xeb) /*Looks like that 1024x768 mono mode expects 512K of video memory*/
						svga->hwcursor.addr += 0x40000;
					break;
				case 0x9c: case 0x9d:
					svga->hwcursor.x = ht216->ht_regs[0x9d] | ((ht216->ht_regs[0x9c] & 7) << 8);
					break;
				case 0x9e: case 0x9f:
					svga->hwcursor.y = ht216->ht_regs[0x9f] | ((ht216->ht_regs[0x9e] & 3) << 8);
					break;

				case 0xa0:
					svga->latch.b[0] = val;
					break;
				case 0xa1:
					svga->latch.b[1] = val;
					break;
				case 0xa2:
					svga->latch.b[2] = val;
					break;
				case 0xa3:
					svga->latch.b[3] = val;
					break;

				case 0xa4:
				case 0xf8:
					svga->fullchange = changeframecount;
					svga_recalctimings(svga);
					break;

				case 0xa5:
					svga->hwcursor.ena = !!(val & 0x80);
					break;

				case 0xc0:
					break;

				case 0xc1:
					break;

				case 0xc8:
					if ((old ^ val) & HT_REG_C8_E256) {
						svga->fullchange = changeframecount;
						svga_recalctimings(svga);
					}
					ht216_remap(ht216);
					break;

				case 0xc9: case 0xcf:
					ht216_remap(ht216);
					break;

				case 0xe0:
					svga->adv_flags &= ~FLAG_RAMDAC_SHIFT;
					if (val & 0x04)
						svga->adv_flags |= FLAG_RAMDAC_SHIFT;
					/* FALLTHROUGH */
				/*Bank registers*/
				case 0xe8: case 0xe9:
					ht216_log("HT216 reg 0x%02x write = %02x, mode = 1, chain4 = %x\n", svga->seqaddr & 0xff, val, svga->chain4);
					ht216_recalc_bank_regs(ht216, 1);
					ht216_remap(ht216);
					break;

				case 0xec:
					ht216->fg_latch[0] = val;
					break;
				case 0xed:
					ht216->fg_latch[1] = val;
					break;
				case 0xee:
					ht216->fg_latch[2] = val;
					break;
				case 0xef:
					ht216->fg_latch[3] = val;
					break;

				case 0xf0:
					ht216->fg_latch[ht216->fg_plane_sel] = val;
					ht216->fg_plane_sel = (ht216->fg_plane_sel + 1) & 3;
					break;

				case 0xf1:
					ht216->bg_plane_sel = val & 3;
					ht216->fg_plane_sel = (val & 0x30) >> 4;
					break;

				case 0xf2:
					svga->latch.b[ht216->bg_plane_sel] = val;
					ht216->bg_plane_sel = (ht216->bg_plane_sel + 1) & 3;
					break;

				case 0xf6:
					/*Bits 18 and 19 of the display memory address*/
					ht216_log("HT216 reg 0xf6 write = %02x, mode = 0, chain4 = %x, vram mask = %08x, cr17 = %02x\n", val, svga->chain4, svga->vram_display_mask, svga->crtc[0x17]);
					ht216_recalc_bank_regs(ht216, 0);
					ht216_remap(ht216);
					svga->fullchange = changeframecount;
					svga_recalctimings(svga);
					break;

				case 0xf9:
					/*Bit 16 of the display memory address, only active when in chain4 mode and 256 color mode.*/
					ht216_log("HT216 reg 0xf9 write = %02x, mode = 0, chain4 = %x\n", val & HT_REG_F9_XPSEL, svga->chain4);
					ht216_recalc_bank_regs(ht216, 0);
					ht216_remap(ht216);
					break;

				case 0xfc:
					ht216_log("HT216 reg 0xfc write = %02x, mode = 0, chain4 = %x, bit 7 = %02x, packedchain = %02x\n", val, svga->chain4, val & 0x80, val & 0x20);
					svga->packed_chain4 = !!(val & 0x20);
					ht216_recalc_bank_regs(ht216, 0);
					ht216_remap(ht216);
					svga->fullchange = changeframecount;
					svga_recalctimings(svga);
					break;
			}
			return;
		}
		break;

	case 0x3c6: case 0x3c7: case 0x3c8: case 0x3c9:
		if (ht216->id == 0x7152)
			sc1148x_ramdac_out(addr, 0, val, svga->ramdac, svga);
		else
			svga_out(addr, val, svga);
		return;

	case 0x3cb:
		if (ht216->id == 0x7152) {
			ht216->reg_3cb = val;
			svga_set_ramdac_type(svga, (val & 0x20) ? RAMDAC_6BIT : RAMDAC_8BIT);
		}
		break;

	case 0x3cf:
		if (svga->gdcaddr == 5) {
			svga->chain2_read = val & 0x10;
			ht216_remap(ht216);
		} else if (svga->gdcaddr == 6) {
			if (val & 8)
				svga->banked_mask = 0x7fff;
			else
				svga->banked_mask = 0xffff;
		}

		if (svga->gdcaddr <= 8) {
			svga->fast = (svga->gdcreg[8] == 0xff && !(svga->gdcreg[3] & 0x18) &&
					 !svga->gdcreg[1]) && svga->chain4 && svga->packed_chain4;
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
				if ((svga->crtcreg == 0xc) || (svga->crtcreg == 0xd)) {
					svga->fullchange = 3;
					svga->ma_latch = ((svga->crtc[0xc] << 8) | svga->crtc[0xd]) + ((svga->crtc[8] & 0x60) >> 5);
				} else {
					svga->fullchange = changeframecount;
					svga_recalctimings(svga);
				}
			}
		}
		break;

	case 0x46e8:
		if ((ht216->id == 0x7152) && ht216->isabus)
			io_removehandler(0x0105, 0x0001, ht216_in, NULL, NULL, ht216_out, NULL, NULL, ht216);
		io_removehandler(0x03c0, 0x0020, ht216_in, NULL, NULL, ht216_out, NULL, NULL, ht216);
		mem_mapping_disable(&svga->mapping);
		mem_mapping_disable(&ht216->linear_mapping);
		if (val & 8) {
			if ((ht216->id == 0x7152) && ht216->isabus)
				io_sethandler(0x0105, 0x0001, ht216_in, NULL, NULL, ht216_out, NULL, NULL, ht216);
			io_sethandler(0x03c0, 0x0020, ht216_in, NULL, NULL, ht216_out, NULL, NULL, ht216);
			mem_mapping_enable(&svga->mapping);
			ht216_remap(ht216);
		}
		break;
    }

    svga_out(addr, val, svga);
}


uint8_t
ht216_in(uint16_t addr, void *p)
{
    ht216_t *ht216 = (ht216_t *)p;
    svga_t *svga = &ht216->svga;
    uint8_t ret = 0xff;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
		addr ^= 0x60;

    if ((ht216->id == 0x7152) && ht216->isabus) {
	if (addr == 0x105)
		return ht216->extensions;
    }

    switch (addr) {
	case 0x3c4:
		return svga->seqaddr;

	case 0x3c5:
		if (svga->seqaddr == 6)
			return ht216->ext_reg_enable;
		else if (svga->seqaddr >= 0x80) {
			if (ht216->ext_reg_enable) {
				ret = ht216->ht_regs[svga->seqaddr & 0xff];

				switch (svga->seqaddr & 0xff) {
					case 0x83:
						if (svga->attrff)
							ret = svga->attraddr | 0x80;
						else
							ret = svga->attraddr;
						break;

					case 0x8e:
						ret = ht216->id & 0xff;
						break;
					case 0x8f:
						ret = (ht216->id >> 8) & 0xff;
						break;

					case 0xa0:
						ret = svga->latch.b[0];
						break;
					case 0xa1:
						ret = svga->latch.b[1];
						break;
					case 0xa2:
						ret = svga->latch.b[2];
						break;
					case 0xa3:
						ret = svga->latch.b[3];
						break;

					case 0xf0:
						ret = ht216->fg_latch[ht216->fg_plane_sel];
						ht216->fg_plane_sel = 0;
						break;

					case 0xf2:
						ret = svga->latch.b[ht216->bg_plane_sel];
						ht216->bg_plane_sel = 0;
						break;
				}

				return ret;
			} else
				return 0xff;
		}
		break;

	case 0x3c6: case 0x3c7: case 0x3c8: case 0x3c9:
		if (ht216->id == 0x7152)
			return sc1148x_ramdac_in(addr, 0, svga->ramdac, svga);
		return svga_in(addr, svga);

	case 0x3cb:
		if (ht216->id == 0x7152)
			return ht216->reg_3cb;
		break;

	case 0x3cc:
		return svga->miscout;

	case 0x3D4:
		return svga->crtcreg;
	case 0x3D5:
		if (svga->crtcreg == 0x1f)
			return svga->crtc[0xc] ^ 0xea;
		return svga->crtc[svga->crtcreg];
    }

    return svga_in(addr, svga);
}

void
ht216_remap(ht216_t *ht216)
{
    svga_t *svga = &ht216->svga;

    mem_mapping_disable(&ht216->linear_mapping);
    if (ht216->ht_regs[0xc8] & HT_REG_C8_XLAM) {
	/*Linear mapping enabled*/
	ht216_log("Linear mapping enabled\n");
	ht216->linear_base = ((ht216->ht_regs[0xc9] & 0xf) << 20) | (ht216->ht_regs[0xcf] << 24);
	mem_mapping_disable(&svga->mapping);
	mem_mapping_set_addr(&ht216->linear_mapping, ht216->linear_base, 0x100000);
    }

    ht216->read_banks[0] = ht216->read_bank_reg[0] << 12;
    ht216->write_banks[0] = ht216->write_bank_reg[0] << 12;

    /* Split bank: two banks used */
    if (ht216->ht_regs[0xe0] & HT_REG_E0_SBAE) {
	ht216->read_banks[1] = ht216->read_bank_reg[1] << 12;
	ht216->write_banks[1] = ht216->write_bank_reg[1] << 12;
    }

    if (!svga->chain4) {
	ht216->read_banks[0] = ((ht216->read_banks[0] & 0xc0000) >> 2) | (ht216->read_banks[0] & 0xffff);
	ht216->read_banks[1] = ((ht216->read_banks[1] & 0xc0000) >> 2) | (ht216->read_banks[1] & 0xffff);
	ht216->write_banks[0] = ((ht216->write_banks[0] & 0xc0000) >> 2) | (ht216->write_banks[0] & 0xffff);
	ht216->write_banks[1] = ((ht216->write_banks[1] & 0xc0000) >> 2) | (ht216->write_banks[1] & 0xffff);
    }

    if (!(ht216->ht_regs[0xe0] & HT_REG_E0_SBAE)) {
	ht216->read_banks[1] = ht216->read_banks[0] + 0x8000;
	ht216->write_banks[1] = ht216->write_banks[0] + 0x8000;
    }

#ifdef ENABLE_HT216_LOG
    ht216_log("Registers: %02X, %02X, %02X, %02X, %02X\n", ht216->misc, ht216->ht_regs[0xe8], ht216->ht_regs[0xe9],
	      ht216->ht_regs[0xf6], ht216->ht_regs[0xf9]);
    ht216_log("Banks: %08X, %08X, %08X, %08X\n", ht216->read_banks[0], ht216->read_banks[1],
	      ht216->write_banks[0], ht216->write_banks[1]);
#endif
}


void
ht216_recalctimings(svga_t *svga)
{
    ht216_t *ht216 = (ht216_t *)svga->p;
    int high_res_256 = 0;

	switch ((((((svga->miscout >> 2) & 3) || ((ht216->ht_regs[0xa4] >> 2) & 3)) |
			((ht216->ht_regs[0xa4] >> 2) & 4)) || ((ht216->ht_regs[0xf8] >> 5) & 0x0f)) |
			((ht216->ht_regs[0xf8] << 1) & 8)) {
		case 0:
		case 1:
			break;
		case 4:
			svga->clock = (cpuclock * (double)(1ull << 32)) / 50350000.0;
			break;
		case 5:
			svga->clock = (cpuclock * (double)(1ull << 32)) / 65000000.0;
			break;
		case 7:
			svga->clock = (cpuclock * (double)(1ull << 32)) / 40000000.0;
			break;
		default:
			svga->clock = (cpuclock * (double)(1ull << 32)) / 36000000.0;
			break;
	}

    svga->ma_latch |= ((ht216->ht_regs[0xf6] & 0x30) << 12);

    svga->interlace = ht216->ht_regs[0xe0] & 1;

    if (svga->interlace)
	high_res_256 = (svga->htotal * 8) > (svga->vtotal * 4);
    else
	high_res_256 = (svga->htotal * 8) > (svga->vtotal * 2);

    ht216->adjust_cursor = 0;

    if (!svga->scrblank && svga->attr_palette_enable) {
	if (!(svga->gdcreg[6] & 1) && !(svga->attrregs[0x10] & 1)) { /*Text mode*/
		if (svga->seqregs[1] & 8) /*40 column*/ {
			svga->render = svga_render_text_40;
		} else {
			svga->render = svga_render_text_80;
		}
	} else {
		if (svga->crtc[0x17] == 0xeb) {
		svga->rowoffset <<= 1;
		svga->render = svga_render_2bpp_headland_highres;
		}

		if (svga->bpp == 8) {
		ht216_log("regC8 = %02x, gdcreg5 bit 6 = %02x, no lowres = %02x, regf8 bit 7 = %02x, regfc = %02x\n", ht216->ht_regs[0xc8] & HT_REG_C8_E256, svga->gdcreg[5] & 0x40, !svga->lowres, ht216->ht_regs[0xf6] & 0x80, ht216->ht_regs[0xfc] & HT_REG_FC_ECOLRE);
		if (((ht216->ht_regs[0xc8] & HT_REG_C8_E256) || (svga->gdcreg[5] & 0x40)) && (!svga->lowres || (ht216->ht_regs[0xf6] & 0x80))) {
			if (high_res_256) {
				svga->hdisp >>= 1;
				ht216->adjust_cursor = 1;
			}
			svga->render = svga_render_8bpp_highres;
		} else if (svga->lowres) {
			if (high_res_256) {
				svga->hdisp >>= 1;
				ht216->adjust_cursor = 1;
				svga->render = svga_render_8bpp_highres;
			} else {
				ht216_log("8bpp low, packed = %02x, chain4 = %02x\n", svga->packed_chain4, svga->chain4);
				svga->render = svga_render_8bpp_lowres;
			}
		} else if (ht216->ht_regs[0xfc] & HT_REG_FC_ECOLRE) {
			if (ht216->id == 0x7152) {
				svga->hdisp = svga->crtc[1] - ((svga->crtc[5] & 0x60) >> 5);
				if (!(svga->crtc[1] & 1))
					svga->hdisp--;
				svga->hdisp++;
				svga->hdisp *= (svga->seqregs[1] & 8) ? 16 : 8;
				svga->rowoffset <<= 1;
				if ((svga->crtc[0x17] & 0x60) == 0x20) /*Would result in a garbled screen with trailing cursor glitches*/
					svga->crtc[0x17] |= 0x40;
			}
			svga->render = svga_render_8bpp_highres;
		}
		} else if (svga->bpp == 15) {
			svga->rowoffset <<= 1;
			svga->hdisp >>= 1;
			if ((svga->crtc[0x17] & 0x60) == 0x20) /*Would result in a garbled screen with trailing cursor glitches*/
				svga->crtc[0x17] |= 0x40;
			svga->render = svga_render_15bpp_highres;
		}
	}
	}

	svga->ma_latch |= ((ht216->ht_regs[0xf6] & 0x30) << 14);

    if (svga->crtc[0x17] == 0xeb) /*Looks like 1024x768 mono mode expects 512K of video memory*/
	svga->vram_display_mask = 0x7ffff;
    else
	svga->vram_display_mask = (ht216->ht_regs[0xf6] & 0x40) ? ht216->vram_mask : 0x3ffff;
}


static void
ht216_hwcursor_draw(svga_t *svga, int displine)
{
    ht216_t *ht216 = (ht216_t *)svga->p;
    int x, shift = (ht216->adjust_cursor ? 2 : 1);
    uint32_t dat[2];
    int offset = svga->hwcursor_latch.x + svga->hwcursor_latch.xoff;
    int width = (ht216->adjust_cursor ? 16 : 32);

    if (ht216->adjust_cursor)
	offset >>= 1;

    if (svga->interlace && svga->hwcursor_oddeven)
	svga->hwcursor_latch.addr += 4;

    dat[0] = (svga->vram[svga->hwcursor_latch.addr]   << 24) |
	     (svga->vram[svga->hwcursor_latch.addr+1] << 16) |
	     (svga->vram[svga->hwcursor_latch.addr+2] <<  8) |
	      svga->vram[svga->hwcursor_latch.addr+3];
    dat[1] = (svga->vram[svga->hwcursor_latch.addr+128]   << 24) |
	     (svga->vram[svga->hwcursor_latch.addr+128+1] << 16) |
	     (svga->vram[svga->hwcursor_latch.addr+128+2] <<  8) |
	      svga->vram[svga->hwcursor_latch.addr+128+3];

    for (x = 0; x < width; x++) {
	if (!(dat[0] & 0x80000000))
		((uint32_t *)buffer32->line[displine])[svga->x_add + offset + x]  = 0;
	if (dat[1] & 0x80000000)
		((uint32_t *)buffer32->line[displine])[svga->x_add + offset + x] ^= 0xffffff;

	dat[0] <<= shift;
	dat[1] <<= shift;
    }

    svga->hwcursor_latch.addr += 4;
    if (svga->interlace && !svga->hwcursor_oddeven)
	svga->hwcursor_latch.addr += 4;
}


static __inline uint8_t
extalu(int op, uint8_t input_a, uint8_t input_b)
{
    uint8_t val;

    switch (op) {
	case 0x0: val = 0;                    break;
	case 0x1: val = ~(input_a | input_b); break;
	case 0x2: val = input_a & ~input_b;   break;
	case 0x3: val = ~input_b;             break;
	case 0x4: val = ~input_a & input_b;   break;
	case 0x5: val = ~input_a;             break;
	case 0x6: val = input_a ^ input_b;    break;
	case 0x7: val = ~(input_a & input_b); break;
	case 0x8: val = input_a & input_b;    break;
	case 0x9: val = ~(input_a ^ input_b); break;
	case 0xa: val = input_a;              break;
	case 0xb: val = input_a | ~input_b;   break;
	case 0xc: val = input_b;              break;
	case 0xd: val = ~input_a | input_b;   break;
	case 0xe: val = input_a | input_b;    break;
	case 0xf: default: val = 0xff;        break;
    }

    return val;
}

static void
ht216_dm_write(ht216_t *ht216, uint32_t addr, uint8_t cpu_dat, uint8_t cpu_dat_unexpanded)
{
    svga_t *svga = &ht216->svga;
    int writemask2 = svga->writemask, reset_wm = 0;
    latch_t vall;
    uint8_t i, wm = svga->writemask;
    uint8_t count = 4, fg_data[8] = {0, 0, 0, 0, 0, 0, 0, 0};

    if (ht216->ht_regs[0xcd] & HT_REG_CD_P8PCEXP)
	writemask2 = svga->seqregs[2];

    if (!(svga->gdcreg[6] & 1))
		svga->fullchange = 2;

	if (svga->chain4) {
	writemask2 = 1 << (addr & 3);
	addr = dword_remap(svga, addr) & ~3;
	} else if (svga->chain2_write && (svga->crtc[0x17] != 0xeb)) {
	writemask2 &= ~0xa;
	if (addr & 1)
		writemask2 <<= 1;
	addr &= ~1;
	addr <<= 2;
    } else
	addr <<= 2;

    if (addr >= svga->vram_max)
	return;

    svga->changedvram[addr >> 12] = changeframecount;

    if (ht216->ht_regs[0xcd] & HT_REG_CD_P8PCEXP)
	count = 8;

    switch (ht216->ht_regs[0xfe] & HT_REG_FE_FBMC) {
	case 0x00:
		for (i = 0; i < count; i++)
			fg_data[i] = cpu_dat;
		break;
	case 0x04:
		if (ht216->ht_regs[0xfe] & HT_REG_FE_FBRC) {
			for (i = 0; i < count; i++) {
				if (ht216->ht_regs[0xfa] & (1 << i))
					fg_data[i] = cpu_dat_unexpanded;
				else if (ht216->ht_regs[0xfb] & (1 << i))
					fg_data[i] = 0xff - cpu_dat_unexpanded;
			}
		} else {
			for (i = 0; i < count; i++) {
				if (ht216->ht_regs[0xfa] & (1 << i))
					fg_data[i] = ht216->ht_regs[0xf5];
				else if (ht216->ht_regs[0xfb] & (1 << i))
					fg_data[i] = 0xff - ht216->ht_regs[0xf5];
			}
		}
		break;
	case 0x08:
	case 0x0c:
		for (i = 0; i < count; i++)
			fg_data[i] = ht216->fg_latch[i];
		break;
    }

    switch (svga->writemode) {
	case 0:
		if ((svga->gdcreg[8] == 0xff) && !(svga->gdcreg[3] & 0x18) && (!svga->gdcreg[1] || svga->set_reset_disabled)) {
			for (i = 0; i < count; i++) {
				if (ht216->ht_regs[0xcd] & HT_REG_CD_P8PCEXP) {
					if (writemask2 & (0x80 >> i))
						svga->vram[addr | i] = fg_data[i];
				} else {
					if (writemask2 & (1 << i))
						svga->vram[addr | i] = fg_data[i];
				}
			}
			return;
		} else {
			for (i = 0; i < count; i++) {
				if (svga->gdcreg[1] & (1 << i))
					vall.b[i] = !!(svga->gdcreg[0] & (1 << i)) * 0xff;
				else
					vall.b[i] = fg_data[i];
			}
		}
		break;
	case 1:
		for (i = 0; i < count; i++) {
			if (ht216->ht_regs[0xcd] & HT_REG_CD_P8PCEXP) {
				if (writemask2 & (0x80 >> i))
					svga->vram[addr | i] = svga->latch.b[i];
			} else {
				if (writemask2 & (1 << i))
					svga->vram[addr | i] = svga->latch.b[i];
			}
		}
		return;
	case 2:
		for (i = 0; i < count; i++)
			vall.b[i] = !!(cpu_dat & (1 << i)) * 0xff;

		if (!(svga->gdcreg[3] & 0x18) && (!svga->gdcreg[1] || svga->set_reset_disabled)) {
			for (i = 0; i < count; i++) {
				if (ht216->ht_regs[0xcd] & HT_REG_CD_P8PCEXP) {
					if (writemask2 & (0x80 >> i))
						svga->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) | (svga->latch.b[i] & ~svga->gdcreg[8]);
				} else {
					if (writemask2 & (1 << i))
						svga->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) | (svga->latch.b[i] & ~svga->gdcreg[8]);
				}
			}
			return;
		}
		break;
	case 3:
                wm = svga->gdcreg[8];
                svga->gdcreg[8] &= cpu_dat;

		for (i = 0; i < count; i++)
			vall.b[i] = !!(svga->gdcreg[0] & (1 << i)) * 0xff;

                reset_wm = 1;
                break;
    }

    switch (svga->gdcreg[3] & 0x18) {
	case 0x00:	/* Set */
		for (i = 0; i < count; i++) {
			if (ht216->ht_regs[0xcd] & HT_REG_CD_P8PCEXP) {
				if (writemask2 & (0x80 >> i))
					svga->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) | (svga->latch.b[i] & ~svga->gdcreg[8]);
			} else {
				if (writemask2 & (1 << i))
					svga->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) | (svga->latch.b[i] & ~svga->gdcreg[8]);
			}
		}
		break;
	case 0x08:	/* AND */
		for (i = 0; i < count; i++) {
			if (ht216->ht_regs[0xcd] & HT_REG_CD_P8PCEXP) {
				if (writemask2 & (0x80 >> i))
					svga->vram[addr | i] = (vall.b[i] | ~svga->gdcreg[8]) & svga->latch.b[i];
			} else {
				if (writemask2 & (1 << i))
					svga->vram[addr | i] = (vall.b[i] | ~svga->gdcreg[8]) & svga->latch.b[i];
			}
		}
		break;
	case 0x10:	/* OR */
		for (i = 0; i < count; i++) {
			if (ht216->ht_regs[0xcd] & HT_REG_CD_P8PCEXP) {
				if (writemask2 & (0x80 >> i))
					svga->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) | svga->latch.b[i];
			} else {
				if (writemask2 & (1 << i))
					svga->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) | svga->latch.b[i];
			}
		}
		break;
	case 0x18:	/* XOR */
		for (i = 0; i < count; i++) {
			if (ht216->ht_regs[0xcd] & HT_REG_CD_P8PCEXP) {
				if (writemask2 & (0x80 >> i))
					svga->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) ^ svga->latch.b[i];
			} else {
				if (writemask2 & (1 << i))
					svga->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) ^ svga->latch.b[i];
			}
		}
		break;
    }

    if (reset_wm)
	svga->gdcreg[8] = wm;
}


static void
ht216_dm_extalu_write(ht216_t *ht216, uint32_t addr, uint8_t cpu_dat, uint8_t bit_mask, uint8_t cpu_dat_unexpanded, uint8_t rop_select)
{
    /*Input B = CD.5
      Input A = FE[3:2]
            00 = Set/Reset output mode
                    output = CPU-side ALU input
            01 = Solid fg/bg mode (3C4:FA/FB)
                    Bit mask = 3CF.F5 or CPU byte
            10 = Dithered fg  (3CF:EC-EF)
            11 = RMW (dest data) (set if CD.5 = 1)
      F/B ROP select = FE[5:4]
            00 = CPU byte
            01 = Bit mask (3CF:8)
            1x = (3C4:F5)*/
    svga_t *svga = &ht216->svga;
    uint8_t input_a = 0, input_b = 0;
    uint8_t fg, bg;
    uint8_t output;
	uint32_t remapped_addr = dword_remap(svga, addr);

    if (ht216->ht_regs[0xcd] & HT_REG_CD_RMWMDE) /*RMW*/
	input_b = svga->vram[remapped_addr];
    else
	input_b = ht216->bg_latch[addr & 7];

    switch (ht216->ht_regs[0xfe] & HT_REG_FE_FBMC) {
	case 0x00:
		input_a = cpu_dat;
		break;
	case 0x04:
		if (ht216->ht_regs[0xfe] & HT_REG_FE_FBRC)
			input_a = (cpu_dat_unexpanded & (1 << ((addr & 7) ^ 7))) ? ht216->ht_regs[0xfa] : ht216->ht_regs[0xfb];
		else
			input_a = (ht216->ht_regs[0xf5] & (1 << ((addr & 7) ^ 7))) ? ht216->ht_regs[0xfa] : ht216->ht_regs[0xfb];
		break;
	case 0x08:
		input_a = ht216->fg_latch[addr & 3];
		break;
	case 0x0c:
		input_a = ht216->bg_latch[addr & 7];
		break;
    }

    fg = extalu(ht216->ht_regs[0xce] >> 4, input_a, input_b);
    bg = extalu(ht216->ht_regs[0xce] & 0xf,  input_a, input_b);
    output = (fg & rop_select) | (bg & ~rop_select);
    svga->vram[addr] = (svga->vram[remapped_addr] & ~bit_mask) | (output & bit_mask);
    svga->changedvram[remapped_addr >> 12] = changeframecount;
}

static void
ht216_dm_masked_write(ht216_t *ht216, uint32_t addr, uint8_t val, uint8_t bit_mask)
{
    svga_t *svga = &ht216->svga;
    int writemask2 = svga->writemask;
    uint8_t count = 4, i;
    uint8_t full_mask = 0x0f;

    if (ht216->ht_regs[0xcd] & HT_REG_CD_P8PCEXP)
	writemask2 = svga->seqregs[2];

    if (!(svga->gdcreg[6] & 1))
		svga->fullchange = 2;

	if (svga->chain4) {
	writemask2 = 1 << (addr & 3);
	addr = dword_remap(svga, addr) & ~3;
	} else if (svga->chain2_write) {
	writemask2 &= ~0xa;
	if (addr & 1)
		writemask2 <<= 1;
	addr &= ~1;
	addr <<= 2;
    } else
	addr <<= 2;

    if (addr >= svga->vram_max)
	return;

    addr &= svga->decode_mask;

    if (addr >= svga->vram_max)
	return;

    addr &= svga->vram_mask;

    svga->changedvram[addr >> 12] = changeframecount;

    if (ht216->ht_regs[0xcd] & HT_REG_CD_P8PCEXP) {
	count = 8;
	full_mask = 0xff;
    }

    if (bit_mask == 0xff) {
	for (i = 0; i < count; i++) {
		if (writemask2 & (1 << i))
			svga->vram[addr | i] = val;
	}
    } else {
	if (writemask2 == full_mask) {
		for (i = 0; i < count; i++)
			svga->vram[addr | i] = (svga->latch.b[i] & bit_mask) | (svga->vram[addr | i] & ~bit_mask);
	} else {
		for (i = 0; i < count; i++) {
			if (writemask2 & (1 << i))
				svga->vram[addr | i] = (val & bit_mask) | (svga->vram[addr | i] & ~bit_mask);
		}
	}
    }
}


static void
ht216_write_common(ht216_t *ht216, uint32_t addr, uint8_t val)
{
    /*Input B = CD.5
      Input A = FE[3:2]
	    00 = Set/Reset output mode
		 output = CPU-side ALU input
	    01 = Solid fg/bg mode (3C4:FA/FB)
		 Bit mask = 3CF.F5 or CPU byte
	    10 = Dithered fg  (3CF:EC-EF)
	    11 = RMW (dest data) (set if CD.5 = 1)
      F/B ROP select = FE[5:4]
	    00 = CPU byte
	    01 = Bit mask (3CF:8)
	    1x = (3C4:F5)
    */
    svga_t *svga = &ht216->svga;
    int i;
    uint8_t bit_mask = 0, rop_select = 0;

    cycles -= video_timing_write_b;

    addr &= svga->vram_mask;

    val = ((val >> (svga->gdcreg[3] & 7)) | (val << (8 - (svga->gdcreg[3] & 7))));

    if (ht216->ht_regs[0xcd] & HT_REG_CD_EXALU) {
	/*Extended ALU*/
	switch (ht216->ht_regs[0xfe] & HT_REG_FE_FBRSL) {
		case 0x00:
			rop_select = val;
			break;
		case 0x10:
			rop_select = svga->gdcreg[8];
			break;
		case 0x20: case 0x30:
			rop_select = ht216->ht_regs[0xf5];
			break;
	}
	switch (ht216->ht_regs[0xcd] & HT_REG_CD_BMSKSL) {
		case 0x00:
			bit_mask = svga->gdcreg[8];
			break;
		case 0x04:
			bit_mask = val;
			break;
		case 0x08: case 0x0c:
			bit_mask = ht216->ht_regs[0xf5];
			break;
	}

	if (ht216->ht_regs[0xcd] & HT_REG_CD_FP8PCEXP) {	/*1->8 bit expansion*/
		addr = (addr << 3) & 0xfffff;
		for (i = 0; i < 8; i++)
			ht216_dm_extalu_write(ht216, addr + i, (val & (0x80 >> i)) ? 0xff : 0, (bit_mask & (0x80 >> i)) ? 0xff : 0, val, (rop_select & (0x80 >> i)) ? 0xff : 0);
	} else {
		ht216_dm_extalu_write(ht216, addr, val, bit_mask, val, rop_select);
	}
    } else if (ht216->ht_regs[0xf3]) {
	if (ht216->ht_regs[0xf3] & 2) {
		ht216_dm_masked_write(ht216, addr, val, val);
	} else
		ht216_dm_masked_write(ht216, addr, val, ht216->ht_regs[0xf4]);
    } else {
	if (ht216->ht_regs[0xcd] & HT_REG_CD_FP8PCEXP) {	/*1->8 bit expansion*/
		addr = (addr << 3) & 0xfffff;
		for (i = 0; i < 8; i++)
			ht216_dm_write(ht216, addr + i, (val & (0x80 >> i)) ? 0xff : 0, val);
	} else {
		ht216_dm_write(ht216, addr, val, val);
	}
    }
}


static void
ht216_write(uint32_t addr, uint8_t val, void *p)
{
    ht216_t *ht216 = (ht216_t *)p;
    svga_t *svga = &ht216->svga;
    uint32_t prev_addr = addr;

    addr &= svga->banked_mask;
    addr = (addr & 0x7fff) + ht216->write_banks[(addr >> 15) & 1];

    if (svga->crtc[0x17] == 0xeb && !(svga->gdcreg[6] & 0xc) && prev_addr >= 0xb0000)
	addr += 0x10000;
    else if (svga->chain4 && ((ht216->ht_regs[0xfc] & 0x06) == 0x06))
	addr = (addr & 0xfffeffff) | (prev_addr & 0x10000);

    if (!ht216->ht_regs[0xcd] && !ht216->ht_regs[0xfe] && !ht216->ht_regs[0xf3] && svga->crtc[0x17] != 0xeb) {
	svga_write_linear(addr, val, svga);
    } else
	ht216_write_common(ht216, addr, val);
}


static void
ht216_writew(uint32_t addr, uint16_t val, void *p)
{
    ht216_t *ht216 = (ht216_t *)p;
    svga_t *svga = &ht216->svga;
    uint32_t prev_addr = addr;

    addr &= svga->banked_mask;
    addr = (addr & 0x7fff) + ht216->write_banks[(addr >> 15) & 1];

    if (svga->crtc[0x17] == 0xeb && !(svga->gdcreg[6] & 0xc) && prev_addr >= 0xb0000)
	addr += 0x10000;
    else if (svga->chain4 && ((ht216->ht_regs[0xfc] & 0x06) == 0x06))
	addr = (addr & 0xfffeffff) | (prev_addr & 0x10000);

    if (!ht216->ht_regs[0xcd] && !ht216->ht_regs[0xfe] && !ht216->ht_regs[0xf3] && svga->crtc[0x17] != 0xeb)
	svga_writew_linear(addr, val, svga);
    else {
	ht216_write_common(ht216, addr, val);
	ht216_write_common(ht216, addr+1, val >> 8);
    }
}


static void
ht216_writel(uint32_t addr, uint32_t val, void *p)
{
    ht216_t *ht216 = (ht216_t *)p;
    svga_t *svga = &ht216->svga;
    uint32_t prev_addr = addr;

    addr &= svga->banked_mask;
    addr = (addr & 0x7fff) + ht216->write_banks[(addr >> 15) & 1];

    if (svga->crtc[0x17] == 0xeb && !(svga->gdcreg[6] & 0xc) && prev_addr >= 0xb0000)
	addr += 0x10000;
    else if (svga->chain4 && ((ht216->ht_regs[0xfc] & 0x06) == 0x06))
	addr = (addr & 0xfffeffff) | (prev_addr & 0x10000);

    if (!ht216->ht_regs[0xcd] && !ht216->ht_regs[0xfe] && !ht216->ht_regs[0xf3] && svga->crtc[0x17] != 0xeb)
	svga_writel_linear(addr, val, svga);
    else {
	ht216_write_common(ht216, addr, val);
	ht216_write_common(ht216, addr+1, val >> 8);
	ht216_write_common(ht216, addr+2, val >> 16);
	ht216_write_common(ht216, addr+3, val >> 24);
    }
}


static void
ht216_write_linear(uint32_t addr, uint8_t val, void *p)
{
    ht216_t *ht216 = (ht216_t *)p;
    svga_t *svga = &ht216->svga;

    addr -= ht216->linear_base;
    if (!svga->chain4)		/*Bits 16 and 17 of linear address are unused in planar modes*/
	addr = (addr & 0xffff) | ((addr & 0xc0000) >> 2);
    addr += ht216->write_banks[0];

    if (!ht216->ht_regs[0xcd] && !ht216->ht_regs[0xfe])
	svga_write_linear(addr, val, svga);
    else
	ht216_write_common(ht216, addr, val);
}


static void
ht216_writew_linear(uint32_t addr, uint16_t val, void *p)
{
    ht216_t *ht216 = (ht216_t *)p;
    svga_t *svga = &ht216->svga;

    addr -= ht216->linear_base;
    if (!svga->chain4)		/*Bits 16 and 17 of linear address are unused in planar modes*/
	addr = (addr & 0xffff) | ((addr & 0xc0000) >> 2);
    addr += ht216->write_banks[0];

    if (!ht216->ht_regs[0xcd] && !ht216->ht_regs[0xfe])
	svga_writew_linear(addr, val, svga);
    else {
	ht216_write_common(ht216, addr, val);
	ht216_write_common(ht216, addr+1, val >> 8);
    }
}


static void
ht216_writel_linear(uint32_t addr, uint32_t val, void *p)
{
    ht216_t *ht216 = (ht216_t *)p;
    svga_t *svga = &ht216->svga;

    addr -= ht216->linear_base;
    if (!svga->chain4)		/*Bits 16 and 17 of linear address are unused in planar modes*/
	addr = (addr & 0xffff) | ((addr & 0xc0000) >> 2);
    addr += ht216->write_banks[0];

    if (!ht216->ht_regs[0xcd] && !ht216->ht_regs[0xfe])
	svga_writel_linear(addr, val, svga);
    else {
	ht216_write_common(ht216, addr, val);
	ht216_write_common(ht216, addr+1, val >> 8);
	ht216_write_common(ht216, addr+2, val >> 16);
	ht216_write_common(ht216, addr+3, val >> 24);
    }
}


static uint8_t
ht216_read_common(ht216_t *ht216, uint32_t addr)
{
    svga_t *svga = &ht216->svga;
    uint32_t latch_addr = 0;
    int offset, readplane = svga->readplane;
    uint8_t or, i;
    uint8_t count = 2;
    uint8_t plane, pixel;
    uint8_t temp, ret;

    if (ht216->ht_regs[0xc8] & HT_REG_C8_MOVSB)
	addr <<= 3;

    addr &= svga->vram_mask;

    cycles -= video_timing_read_b;

    count = (1 << count);

    if (svga->chain4 && svga->packed_chain4) {
	addr &= svga->decode_mask;
	if (addr >= svga->vram_max)
		return 0xff;
	latch_addr = (addr & svga->vram_mask) & ~7;
	if (ht216->ht_regs[0xcd] & HT_REG_CD_ASTODE)
		latch_addr += (svga->gdcreg[3] & 7);
	for (i = 0; i < 8; i++)
		ht216->bg_latch[i] = svga->vram[dword_remap(svga, latch_addr + i)];
	return svga->vram[dword_remap(svga, addr) & svga->vram_mask];
    } else if (svga->chain4) {
	readplane = addr & 3;
	addr = ((addr & 0xfffc) << 2) | ((addr & 0x30000) >> 14) | (addr & ~0x3ffff);
	} else if (svga->chain2_read && (svga->crtc[0x17] != 0xeb)) {
	readplane = (readplane & 2) | (addr & 1);
	addr &= ~1;
	addr <<= 2;
    } else
	addr <<= 2;

    addr &= svga->decode_mask;

    if (addr >= svga->vram_max)
	return 0xff;

    addr &= svga->vram_mask;

    latch_addr = addr & ~7;
    if (ht216->ht_regs[0xcd] & HT_REG_CD_ASTODE) {
	offset = addr & 7;
	for (i = 0; i < 8; i++)
		ht216->bg_latch[i] = svga->vram[latch_addr | ((offset + i) & 7)];
    } else {
	for (i = 0; i < 8; i++)
		ht216->bg_latch[i] = svga->vram[latch_addr | i];
    }

    or = addr & 4;
    for (i = 0; i < 4; i++)
	svga->latch.b[i] = ht216->bg_latch[i | or];

    if (svga->readmode) {
	temp = 0xff;

	for (pixel = 0; pixel < 8; pixel++) {
		for (plane = 0; plane < (1 << count); plane++) {
			if (svga->colournocare & (1 << plane)) {
				/* If we care about a plane, and the pixel has a mismatch on it, clear its bit. */
				if (((svga->latch.b[plane] >> pixel) & 1) != ((svga->colourcompare >> plane) & 1))
					temp &= ~(1 << pixel);
			}
		}
	}

	ret = temp;
    } else
	ret = svga->vram[addr | readplane];

    return ret;
}


static uint8_t
ht216_read(uint32_t addr, void *p)
{
    ht216_t *ht216 = (ht216_t *)p;
    svga_t *svga = &ht216->svga;
    uint32_t prev_addr = addr;

    addr &= svga->banked_mask;
    addr = (addr & 0x7fff) + ht216->read_banks[(addr >> 15) & 1];

    if (svga->crtc[0x17] == 0xeb && !(svga->gdcreg[6] & 0xc) && prev_addr >= 0xb0000)
	addr += 0x10000;
    else if (svga->chain4 && ((ht216->ht_regs[0xfc] & 0x06) == 0x06))
	addr = (addr & 0xfffeffff) | (prev_addr & 0x10000);

    return ht216_read_common(ht216, addr);
}


static uint8_t
ht216_read_linear(uint32_t addr, void *p)
{
    ht216_t *ht216 = (ht216_t *)p;
    svga_t *svga = &ht216->svga;

    addr -= ht216->linear_base;
    if (!svga->chain4)		/*Bits 16 and 17 of linear address are unused in planar modes*/
	addr = (addr & 0xffff) | ((addr & 0xc0000) >> 2);
    addr += ht216->read_banks[0];

    return ht216_read_common(ht216, addr);
}

static uint8_t
radius_mca_read(int port, void *priv)
{
    ht216_t *ht216 = (ht216_t *)priv;
	ht216_log("Port %03x MCA read = %02x\n", port, ht216->pos_regs[port & 7]);
    return (ht216->pos_regs[port & 7]);
}

static void
radius_mca_write(int port, uint8_t val, void *priv)
{
    ht216_t *ht216 = (ht216_t *)priv;

    /* MCA does not write registers below 0x0100. */
    if (port < 0x0102) return;

	ht216_log("Port %03x MCA write = %02x, setup mode = %02x\n", port, val, ht216->ht_regs[0xfc] & 0x80);

    /* Save the MCA register value. */
    ht216->pos_regs[port & 7] = val;
}

static uint8_t
radius_mca_feedb(void *priv)
{
	return 1;
}

void
*ht216_init(const device_t *info, uint32_t mem_size, int has_rom)
{
    ht216_t *ht216 = malloc(sizeof(ht216_t));
    svga_t *svga;

    memset(ht216, 0, sizeof(ht216_t));
    svga = &ht216->svga;

    if (info->flags & DEVICE_VLB)
	video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_v7vga_vlb);
    else if (info->flags & DEVICE_MCA)
	video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_v7vga_mca);
    else
	video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_v7vga_isa);

    svga_init(info, svga, ht216, mem_size,
	      ht216_recalctimings,
	      ht216_in, ht216_out,
	      ht216_hwcursor_draw,
	      NULL);

    switch (has_rom) {
	case 1:
		rom_init(&ht216->bios_rom, BIOS_G2_GC205_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
		break;
	case 2:
		rom_init(&ht216->bios_rom, BIOS_VIDEO7_VGA_1024I_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
		break;
	case 3:
		ht216->monitor_type = device_get_config_int("monitor_type");
		rom_init(&ht216->bios_rom, BIOS_HT216_32_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
		/* Patch the BIOS for monitor type. */
		if (ht216->monitor_type & 0x10) {
			/* Color */
			ht216->bios_rom.rom[0x0526] = 0x0c;
			ht216->bios_rom.rom[0x0528] = 0xeb;
			ht216->bios_rom.rom[0x7fff] += 0x26;
		} else {
			/* Mono */
			ht216->bios_rom.rom[0x0526] = 0x24;
			ht216->bios_rom.rom[0x0527] = 0xef;
			ht216->bios_rom.rom[0x0528] = ht216->bios_rom.rom[0x0529] = 0x90;
			ht216->bios_rom.rom[0x7fff] += 0xfe;
		}
		/* Patch bios for interlaced/non-interlaced. */
		if (ht216->monitor_type & 0x08) {
			/* Non-Interlaced */
			ht216->bios_rom.rom[0x170b] = 0x0c;
			ht216->bios_rom.rom[0x170d] = ht216->bios_rom.rom[0x170e] = 0x90;
			ht216->bios_rom.rom[0x7fff] += 0xf4;
		} else {
			/* Interlaced */
			ht216->bios_rom.rom[0x170b] = 0x24;
			ht216->bios_rom.rom[0x170c] = 0xf7;
			ht216->bios_rom.rom[0x170d] = 0xeb;
			ht216->bios_rom.rom[0x7fff] += 0x1e;
		}
		break;
	case 4:
		if ((info->local == 0x7152) && (info->flags & DEVICE_ISA))
			ht216->extensions = device_get_config_int("extensions");
		else if ((info->local == 0x7152) && (info->flags & DEVICE_MCA)) {
			ht216->pos_regs[0] = 0xb7;
			ht216->pos_regs[1] = 0x80;
			mca_add(radius_mca_read, radius_mca_write, radius_mca_feedb, NULL, ht216);
		}
		rom_init(&ht216->bios_rom, BIOS_RADIUS_SVGA_MULTIVIEW_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
		break;
    }

    svga->hwcursor.ysize = 32;
    ht216->vram_mask = mem_size - 1;
    svga->decode_mask = mem_size - 1;

    if (has_rom == 4)
	svga->ramdac = device_add(&sc11484_nors2_ramdac_device);

    if ((info->flags & DEVICE_VLB) || (info->flags & DEVICE_MCA)) {
	mem_mapping_set_handler(&svga->mapping, ht216_read, NULL, NULL, ht216_write, ht216_writew, ht216_writel);
	mem_mapping_add(&ht216->linear_mapping, 0, 0, ht216_read_linear, NULL, NULL, ht216_write_linear, ht216_writew_linear, ht216_writel_linear, NULL, MEM_MAPPING_EXTERNAL, svga);
    } else {
	mem_mapping_set_handler(&svga->mapping, ht216_read, NULL, NULL, ht216_write, ht216_writew, NULL);
	mem_mapping_add(&ht216->linear_mapping, 0, 0, ht216_read_linear, NULL, NULL, ht216_write_linear, ht216_writew_linear, NULL, NULL, MEM_MAPPING_EXTERNAL, svga);
    }
    mem_mapping_set_p(&svga->mapping, ht216);
    mem_mapping_disable(&ht216->linear_mapping);

    ht216->id = info->local;
    ht216->isabus = (info->flags & DEVICE_ISA);
    ht216->mca = (info->flags & DEVICE_MCA);

    io_sethandler(0x03c0, 0x0020, ht216_in, NULL, NULL, ht216_out, NULL, NULL, ht216);
    io_sethandler(0x46e8, 0x0001, ht216_in, NULL, NULL, ht216_out, NULL, NULL, ht216);

    svga->bpp = 8;
    svga->miscout = 1;

    if (ht216->id == 0x7861)
	ht216->ht_regs[0xb4] = 0x08; /*32-bit DRAM bus*/

    if (ht216->id == 0x7152)
	ht216->reg_3cb = 0x20;

    /* Initialize the cursor pointer towards the end of its segment, needed for ht256sf.drv to work correctly
       when Windows 3.1 is started after boot. */
    ht216->ht_regs[0x94] = 0xff;

    svga->adv_flags = 0;

    return ht216;
}


static void *
g2_gc205_init(const device_t *info)
{
    ht216_t *ht216 = ht216_init(info, 1 << 19, 1);

    return ht216;
}


static void *
v7_vga_1024i_init(const device_t *info)
{
    ht216_t *ht216 = ht216_init(info, device_get_config_int("memory") << 10, 2);

    return ht216;
}


static void *
ht216_pb410a_init(const device_t *info)
{
    ht216_t *ht216 = ht216_init(info, 1 << 20, 0);

    return ht216;
}


static void *
ht216_standalone_init(const device_t *info)
{
    ht216_t *ht216 = ht216_init(info, 1 << 20, 3);

    return ht216;
}

static void *
radius_svga_multiview_init(const device_t *info)
{
    ht216_t *ht216 = ht216_init(info, 1 << 20, 4);

    return ht216;
}


static int
g2_gc205_available(void)
{
    return rom_present(BIOS_G2_GC205_PATH);
}


static int
v7_vga_1024i_available(void)
{
    return rom_present(BIOS_VIDEO7_VGA_1024I_PATH);
}


static int
ht216_standalone_available(void)
{
    return rom_present(BIOS_HT216_32_PATH);
}

static int
radius_svga_multiview_available(void)
{
    return rom_present(BIOS_RADIUS_SVGA_MULTIVIEW_PATH);
}


void
ht216_close(void *p)
{
    ht216_t *ht216 = (ht216_t *)p;

    svga_close(&ht216->svga);

    free(ht216);
}


void
ht216_speed_changed(void *p)
{
    ht216_t *ht216 = (ht216_t *)p;

    svga_recalctimings(&ht216->svga);
}


void
ht216_force_redraw(void *p)
{
    ht216_t *ht216 = (ht216_t *)p;

    ht216->svga.fullchange = changeframecount;
}

static const device_config_t v7_vga_1024i_config[] = {
    {
        .name = "memory",
        .description = "Memory size",
        .type = CONFIG_SELECTION,
        .default_int = 512,
        .selection = {
            {
                .description = "256 kB",
                .value = 256
            },
            {
                .description = "512 kB",
                .value = 512
            },
            {
                .description = ""
            }
        }
    },
    {
        .type = CONFIG_END
    }
};

// clang-format off
static const device_config_t ht216_32_standalone_config[] = {
    {
        .name = "monitor_type",
        .description = "Monitor type",
        .type = CONFIG_SELECTION,
        .default_int = 0x18,
        .selection = {
            {
                .description = "Mono Interlaced",
                .value = 0x00
            },
            {
                .description = "Mono Non-Interlaced",
                .value = 0x08
            },
            {
                .description = "Color Interlaced",
                .value = 0x10
            },
            {
                .description = "Color Non-Interlaced",
                .value = 0x18
            },
            {
                .description = ""
            }
        }
    },
    {
        .type = CONFIG_END
    }
};

static const device_config_t radius_svga_multiview_config[] = {
    {
        .name = "extensions",
        .description = "Extensions",
        .type = CONFIG_SELECTION,
        .default_int = 0x00,
        .selection = {
            {
                .description = "Extensions Enabled",
                .value = 0x00
            },
            {
                .description = "Extensions Disabled",
                .value = 0x02
            },
            {
                .description = ""
            }
        }
    },
    {
        .type = CONFIG_END
    }
};
// clang-format on

const device_t g2_gc205_device = {
    .name = "G2 GC205",
    .internal_name = "g2_gc205",
    .flags = DEVICE_ISA,
    .local = 0x7070,
    .init = g2_gc205_init,
    .close = ht216_close,
    .reset = NULL,
    { .available = g2_gc205_available },
    .speed_changed = ht216_speed_changed,
    .force_redraw = ht216_force_redraw,
    .config = NULL
};

const device_t v7_vga_1024i_device = {
    .name = "Video 7 VGA 1024i (HT208)",
    .internal_name = "v7_vga_1024i",
    .flags = DEVICE_ISA,
    .local = 0x7140,
    .init = v7_vga_1024i_init,
    .close = ht216_close,
    .reset = NULL,
    { .available = v7_vga_1024i_available },
    .speed_changed = ht216_speed_changed,
    .force_redraw = ht216_force_redraw,
    .config = v7_vga_1024i_config
};

const device_t ht216_32_pb410a_device = {
    .name = "Headland HT216-32 (Packard Bell PB410A)",
    .internal_name = "ht216_32_pb410a",
    .flags = DEVICE_VLB,
    .local = 0x7861,	/*HT216-32*/
    .init = ht216_pb410a_init,
    .close = ht216_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = ht216_speed_changed,
    .force_redraw = ht216_force_redraw,
    .config = NULL
};

const device_t ht216_32_standalone_device = {
    .name = "Headland HT216-32",
    .internal_name = "ht216_32",
    .flags = DEVICE_VLB,
    .local = 0x7861,	/*HT216-32*/
    .init = ht216_standalone_init,
    .close = ht216_close,
    .reset = NULL,
    { .available = ht216_standalone_available },
    .speed_changed = ht216_speed_changed,
    .force_redraw = ht216_force_redraw,
    .config = ht216_32_standalone_config
};

const device_t radius_svga_multiview_isa_device = {
    .name = "Radius SVGA Multiview ISA (HT209)",
    .internal_name = "radius_isa",
    .flags = DEVICE_ISA | DEVICE_AT,
    .local = 0x7152,	/*HT209*/
    .init = radius_svga_multiview_init,
    .close = ht216_close,
    .reset = NULL,
    { .available = radius_svga_multiview_available },
    .speed_changed = ht216_speed_changed,
    .force_redraw = ht216_force_redraw,
    .config = radius_svga_multiview_config
};

const device_t radius_svga_multiview_mca_device = {
    .name = "Radius SVGA Multiview MCA (HT209)",
    .internal_name = "radius_mc",
    .flags = DEVICE_MCA,
    .local = 0x7152,	/*HT209*/
    .init = radius_svga_multiview_init,
    .close = ht216_close,
    .reset = NULL,
    { .available = radius_svga_multiview_available },
    .speed_changed = ht216_speed_changed,
    .force_redraw = ht216_force_redraw,
    .config = NULL
};
