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

    uint32_t		vram_mask;

    int			ext_reg_enable;
    int			clk_sel;
        
    uint8_t		read_bank_reg[2], write_bank_reg[2];
    uint32_t		read_bank[2], write_bank[2];
    uint8_t		misc, pad;
    uint16_t		id;
        
    uint8_t		bg_latch[8];
        
    uint8_t		ht_regs[256];
} ht216_t;


#define HT_MISC_PAGE_SEL (1 << 5)

/*Shifts CPU VRAM read address by 3 bits, for use with fat pixel colour expansion*/
#define HT_REG_C8_MOVSB (1 << 0)
#define HT_REG_C8_E256  (1 << 4)
#define HT_REG_C8_XLAM  (1 << 6)

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


#define BIOS_G2_GC205_PATH			L"roms/video/video7/BIOS.BIN"
#define BIOS_VIDEO7_VGA_1024I_PATH		L"roms/video/video7/Video Seven VGA 1024i - BIOS - v2.19 - 435-0062-05 - U17 - 27C256.BIN"

static video_timings_t	timing_v7vga_isa = {VIDEO_ISA, 3,  3,  6,   5,  5, 10};
static video_timings_t	timing_v7vga_vlb = {VIDEO_ISA, 5,  5,  9,  20, 20, 30};


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
		ht216->clk_sel = (ht216->clk_sel & ~3) | ((val & 0x0c) >> 2);
		ht216->misc = val;
                ht216->read_bank_reg[0]  = (ht216->read_bank_reg[0] & ~0x20) | ((val & HT_MISC_PAGE_SEL) ? 0x20 : 0);
                ht216->write_bank_reg[0] = (ht216->write_bank_reg[0] & ~0x20) | ((val & HT_MISC_PAGE_SEL) ? 0x20 : 0);
		ht216_remap(ht216);
		svga_recalctimings(&ht216->svga);
		break;

	case 0x3c5:
		if (svga->seqaddr == 4) {
			svga->chain4 = val & 8;
			ht216_remap(ht216);
		} else if (svga->seqaddr == 6) {
			if (val == 0xea)
				ht216->ext_reg_enable = 1;
			else if (val == 0xae)
				ht216->ext_reg_enable = 0;
		} else if (svga->seqaddr >= 0x80 && ht216->ext_reg_enable) {
			old = ht216->ht_regs[svga->seqaddr & 0xff];
			ht216->ht_regs[svga->seqaddr & 0xff] = val;
			switch (svga->seqaddr & 0xff) {
				case 0x83:
					svga->attraddr = val & 0x1f;
					svga->attrff = (val & 0x80) ? 1 : 0;
					break;

				case 0x94:
					svga->hwcursor.addr = ((val << 6) | (3 << 14) | ((ht216->ht_regs[0xff] & 0x60) << 11)) << 2;
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
					ht216->clk_sel = (val >> 2) & 0xf;
					svga->miscout = (svga->miscout & ~0xc) | ((ht216->clk_sel & 3) << 2);
					break;
				case 0xa5:
					svga->hwcursor.ena = val & 0x80;
					break;

				case 0xc8:
					if ((old ^ val) & 0x10) {
						svga->fullchange = changeframecount;
						svga_recalctimings(svga);
					}
					break;

				case 0xe8:
					ht216->read_bank_reg[0] = val;
					ht216->write_bank_reg[0] = val;
					break;
				case 0xe9:
					ht216->read_bank_reg[1] = val;
					ht216->write_bank_reg[1] = val;
					break;
				case 0xf6:
					svga->vram_display_mask = (val & 0x40) ? ht216->vram_mask : 0x3ffff;
					ht216->read_bank_reg[0]  = (ht216->read_bank_reg[0] & ~0xc0) | ((val & 0xc) << 4);
					ht216->write_bank_reg[0] = (ht216->write_bank_reg[0] & ~0xc0) | ((val & 0x3) << 6);
					break;
				case 0xf9:
					ht216->read_bank_reg[0]  = (ht216->read_bank_reg[0] & ~0x10) | ((val & 1) ? 0x10 : 0);
					ht216->write_bank_reg[0] = (ht216->write_bank_reg[0] & ~0x10) | ((val & 1) ? 0x10 : 0);
					break;
				case 0xff:
					svga->hwcursor.addr = ((ht216->ht_regs[0x94] << 6) | (3 << 14) | ((val & 0x60) << 11)) << 2;
					break;
			}
			switch (svga->seqaddr & 0xff) {
				case 0xa4: case 0xf6: case 0xfc:
					svga->fullchange = changeframecount;
					svga_recalctimings(&ht216->svga);
					break;
			}
			switch (svga->seqaddr & 0xff) {
				case 0xc8: case 0xc9: case 0xcf:
				case 0xe0: case 0xe8: case 0xe9:
				case 0xf6: case 0xf9:
					ht216_remap(ht216);
					break;
			}
			return;
		}
		break;

	case 0x3cf:
		if (svga->gdcaddr == 6) {
			if (val & 8)
				svga->banked_mask = 0x7fff;
			else
				svga->banked_mask = 0xffff;
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
				if (svga->crtcreg < 0xe ||  svga->crtcreg > 0x10) {
					svga->fullchange = changeframecount;
					svga_recalctimings(&ht216->svga);
				}
			}
			break;

		case 0x46e8:
			io_removehandler(0x03c0, 0x0020, ht216_in, NULL, NULL, ht216_out, NULL, NULL, ht216);
			mem_mapping_disable(&ht216->svga.mapping);
			mem_mapping_disable(&ht216->linear_mapping);
			if (val & 8) {
				io_sethandler(0x03c0, 0x0020, ht216_in, NULL, NULL, ht216_out, NULL, NULL, ht216);
				mem_mapping_enable(&ht216->svga.mapping);
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

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
	addr ^= 0x60;

    switch (addr) {
	case 0x3c2:
		break;

	case 0x3c5:
		if (svga->seqaddr == 6)
			return ht216->ext_reg_enable;
		if (svga->seqaddr >= 0x80) {
			if (ht216->ext_reg_enable) {
				switch (svga->seqaddr & 0xff) {
					case 0x83:
						if (svga->attrff)
							return svga->attraddr | 0x80;
						return svga->attraddr;

					case 0x8e: return ht216->id & 0xff;
					case 0x8f: return (ht216->id >> 8) & 0xff;

					case 0xa0:
						return svga->latch.b[0];
					case 0xa1:
						return svga->latch.b[1];
					case 0xa2:
						return svga->latch.b[2];
					case 0xa3:
						return svga->latch.b[3];
				}
				return ht216->ht_regs[svga->seqaddr & 0xff];
			} else
				return 0xff;
		}
		break;

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
	uint32_t linear_base = ((ht216->ht_regs[0xc9] & 0xf) << 20) | (ht216->ht_regs[0xcf] << 24);

	mem_mapping_set_addr(&ht216->linear_mapping, linear_base, 0x100000);

	/*Linear mapping enabled*/
    } else {
	uint8_t read_bank_reg[2] = {ht216->read_bank_reg[0], ht216->read_bank_reg[1]};
	uint8_t write_bank_reg[2] = {ht216->write_bank_reg[0], ht216->write_bank_reg[1]};

	if (!svga->chain4 || !(ht216->ht_regs[0xfc] & HT_REG_FC_ECOLRE)) {
		read_bank_reg[0] &= ~0x30;
		read_bank_reg[1] &= ~0x30;
		write_bank_reg[0] &= ~0x30;
		write_bank_reg[1] &= ~0x30;
	}

	ht216->read_bank[0] = read_bank_reg[0] << 12;
	ht216->write_bank[0] = write_bank_reg[0] << 12;
	if (ht216->ht_regs[0xe0] & HT_REG_E0_SBAE) {
		/*Split bank*/
		ht216->read_bank[1] = read_bank_reg[1] << 12;
		ht216->write_bank[1] = write_bank_reg[1] << 12;
	} else {
		ht216->read_bank[1] = ht216->read_bank[0] + (svga->chain4 ? 0x8000 : 0x20000);
		ht216->write_bank[1] = ht216->write_bank[0] + (svga->chain4 ? 0x8000 : 0x20000);
	}

	if (!svga->chain4) {
		ht216->read_bank[0] >>= 2;
		ht216->read_bank[1] >>= 2;
		ht216->write_bank[0] >>= 2;
		ht216->write_bank[1] >>= 2;
	}
    }
}


void
ht216_recalctimings(svga_t *svga)
{
    ht216_t *ht216 = (ht216_t *)svga->p;

    switch (ht216->clk_sel) {
	case 5:  svga->clock = (cpuclock * (double)(1ull << 32)) / 65000000.0; break;
	case 6:  svga->clock = (cpuclock * (double)(1ull << 32)) / 40000000.0; break;
	case 10: svga->clock = (cpuclock * (double)(1ull << 32)) / 80000000.0; break;
    }
    svga->lowres = !(ht216->ht_regs[0xc8] & HT_REG_C8_E256);
    svga->ma_latch |= ((ht216->ht_regs[0xf6] & 0x30) << 12);
    svga->interlace = ht216->ht_regs[0xe0] & 1;

    if ((svga->bpp == 8) && !svga->lowres)
	svga->render = svga_render_8bpp_highres;
}


static void
ht216_hwcursor_draw(svga_t *svga, int displine)
{
    int x;
    uint32_t dat[2];
    int offset = svga->hwcursor_latch.x + svga->x_add;

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

    for (x = 0; x < 32; x++) {
	if (!(dat[0] & 0x80000000))
		((uint32_t *)buffer32->line[displine])[offset + x]  = 0;
	if (dat[1] & 0x80000000)
		((uint32_t *)buffer32->line[displine])[offset + x] ^= 0xffffff;

	dat[0] <<= 1;
	dat[1] <<= 1;
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
    uint8_t vala, valb, valc, vald, wm = svga->writemask;
    int writemask2 = svga->writemask;
    uint8_t fg_data[4] = {0, 0, 0, 0};

    if (!(svga->gdcreg[6] & 1))
	svga->fullchange = 2;
    if (svga->chain4 || svga->fb_only) {
	writemask2=1<<(addr&3);
	addr&=~3;
    } else if (svga->chain2_write) {
	writemask2 &= ~0xa;
	if (addr & 1)
		writemask2 <<= 1;
	addr &= ~1;
	addr <<= 2;
    } else
	addr<<=2;
    if (addr >= svga->vram_max)
	return;

    svga->changedvram[addr >> 12]=changeframecount;

    switch (ht216->ht_regs[0xfe] & HT_REG_FE_FBMC) {
	case 0x00:
		fg_data[0] = fg_data[1] = fg_data[2] = fg_data[3] = cpu_dat;
		break;
	case 0x04:
		if (ht216->ht_regs[0xfe] & HT_REG_FE_FBRC) {
			if (addr & 4) {
				fg_data[0] = (cpu_dat_unexpanded & (1 << (((addr + 4) & 7) ^ 7))) ? ht216->ht_regs[0xfa] : ht216->ht_regs[0xfb];
				fg_data[1] = (cpu_dat_unexpanded & (1 << (((addr + 5) & 7) ^ 7))) ? ht216->ht_regs[0xfa] : ht216->ht_regs[0xfb];
				fg_data[2] = (cpu_dat_unexpanded & (1 << (((addr + 6) & 7) ^ 7))) ? ht216->ht_regs[0xfa] : ht216->ht_regs[0xfb];
				fg_data[3] = (cpu_dat_unexpanded & (1 << (((addr + 7) & 7) ^ 7))) ? ht216->ht_regs[0xfa] : ht216->ht_regs[0xfb];
			} else {
				fg_data[0] = (cpu_dat_unexpanded & (1 << (((addr + 0) & 7) ^ 7))) ? ht216->ht_regs[0xfa] : ht216->ht_regs[0xfb];
				fg_data[1] = (cpu_dat_unexpanded & (1 << (((addr + 1) & 7) ^ 7))) ? ht216->ht_regs[0xfa] : ht216->ht_regs[0xfb];
				fg_data[2] = (cpu_dat_unexpanded & (1 << (((addr + 2) & 7) ^ 7))) ? ht216->ht_regs[0xfa] : ht216->ht_regs[0xfb];
				fg_data[3] = (cpu_dat_unexpanded & (1 << (((addr + 3) & 7) ^ 7))) ? ht216->ht_regs[0xfa] : ht216->ht_regs[0xfb];
			}
		} else {
			if (addr & 4) {
				fg_data[0] = (ht216->ht_regs[0xf5] & (1 << (((addr + 4) & 7) ^ 7))) ? ht216->ht_regs[0xfa] : ht216->ht_regs[0xfb];
				fg_data[1] = (ht216->ht_regs[0xf5] & (1 << (((addr + 5) & 7) ^ 7))) ? ht216->ht_regs[0xfa] : ht216->ht_regs[0xfb];
				fg_data[2] = (ht216->ht_regs[0xf5] & (1 << (((addr + 6) & 7) ^ 7))) ? ht216->ht_regs[0xfa] : ht216->ht_regs[0xfb];
				fg_data[3] = (ht216->ht_regs[0xf5] & (1 << (((addr + 7) & 7) ^ 7))) ? ht216->ht_regs[0xfa] : ht216->ht_regs[0xfb];
			} else {
				fg_data[0] = (ht216->ht_regs[0xf5] & (1 << (((addr + 0) & 7) ^ 7))) ? ht216->ht_regs[0xfa] : ht216->ht_regs[0xfb];
				fg_data[1] = (ht216->ht_regs[0xf5] & (1 << (((addr + 1) & 7) ^ 7))) ? ht216->ht_regs[0xfa] : ht216->ht_regs[0xfb];
				fg_data[2] = (ht216->ht_regs[0xf5] & (1 << (((addr + 2) & 7) ^ 7))) ? ht216->ht_regs[0xfa] : ht216->ht_regs[0xfb];
				fg_data[3] = (ht216->ht_regs[0xf5] & (1 << (((addr + 3) & 7) ^ 7))) ? ht216->ht_regs[0xfa] : ht216->ht_regs[0xfb];
			}
		}
		break;
	case 0x08:
		fg_data[0] = ht216->ht_regs[0xec];
		fg_data[1] = ht216->ht_regs[0xed];
		fg_data[2] = ht216->ht_regs[0xee];
		fg_data[3] = ht216->ht_regs[0xef];
		break;
	case 0x0c:
		fg_data[0] = ht216->ht_regs[0xec];
		fg_data[1] = ht216->ht_regs[0xed];
		fg_data[2] = ht216->ht_regs[0xee];
		fg_data[3] = ht216->ht_regs[0xef];
		break;
    }

    switch (svga->writemode) {
	case 1:
		if (writemask2 & 1) svga->vram[addr]       = svga->latch.b[0];
		if (writemask2 & 2) svga->vram[addr | 0x1] = svga->latch.b[1];
		if (writemask2 & 4) svga->vram[addr | 0x2] = svga->latch.b[2];
		if (writemask2 & 8) svga->vram[addr | 0x3] = svga->latch.b[3];
		break;
	case 0:
		if (svga->gdcreg[8] == 0xff && !(svga->gdcreg[3] & 0x18) &&
		    (!svga->gdcreg[1] || svga->set_reset_disabled)) {
			if (writemask2 & 1) svga->vram[addr]       = fg_data[0];
			if (writemask2 & 2) svga->vram[addr | 0x1] = fg_data[1];
			if (writemask2 & 4) svga->vram[addr | 0x2] = fg_data[2];
			if (writemask2 & 8) svga->vram[addr | 0x3] = fg_data[3];
		} else {
			if (svga->gdcreg[1] & 1) vala = (svga->gdcreg[0] & 1) ? 0xff : 0;
			else                     vala = fg_data[0];
			if (svga->gdcreg[1] & 2) valb = (svga->gdcreg[0] & 2) ? 0xff : 0;
			else                     valb = fg_data[1];
			if (svga->gdcreg[1] & 4) valc = (svga->gdcreg[0] & 4) ? 0xff : 0;
			else                     valc = fg_data[2];
			if (svga->gdcreg[1] & 8) vald = (svga->gdcreg[0] & 8) ? 0xff : 0;
			else                     vald = fg_data[3];

			switch (svga->gdcreg[3] & 0x18) {
				case 0: /*Set*/
					if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) | (svga->latch.b[0] & ~svga->gdcreg[8]);
					if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) | (svga->latch.b[1] & ~svga->gdcreg[8]);
					if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) | (svga->latch.b[2] & ~svga->gdcreg[8]);
					if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) | (svga->latch.b[3] & ~svga->gdcreg[8]);
					break;
				case 8: /*AND*/
					if (writemask2 & 1) svga->vram[addr]       = (vala | ~svga->gdcreg[8]) & svga->latch.b[0];
					if (writemask2 & 2) svga->vram[addr | 0x1] = (valb | ~svga->gdcreg[8]) & svga->latch.b[1];
					if (writemask2 & 4) svga->vram[addr | 0x2] = (valc | ~svga->gdcreg[8]) & svga->latch.b[2];
					if (writemask2 & 8) svga->vram[addr | 0x3] = (vald | ~svga->gdcreg[8]) & svga->latch.b[3];
					break;
				case 0x10: /*OR*/
					if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) | svga->latch.b[0];
					if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) | svga->latch.b[1];
					if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) | svga->latch.b[2];
					if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) | svga->latch.b[3];
					break;
				case 0x18: /*XOR*/
					if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) ^ svga->latch.b[0];
					if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) ^ svga->latch.b[1];
					if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) ^ svga->latch.b[2];
					if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) ^ svga->latch.b[3];
					break;
			}
		}
		break;
	case 2:
		if (!(svga->gdcreg[3] & 0x18) && (!svga->gdcreg[1] || svga->set_reset_disabled)) {
			if (writemask2 & 1) svga->vram[addr]       = (((cpu_dat & 1) ? 0xff : 0) & svga->gdcreg[8]) | (svga->latch.b[0] & ~svga->gdcreg[8]);
			if (writemask2 & 2) svga->vram[addr | 0x1] = (((cpu_dat & 2) ? 0xff : 0) & svga->gdcreg[8]) | (svga->latch.b[1] & ~svga->gdcreg[8]);
			if (writemask2 & 4) svga->vram[addr | 0x2] = (((cpu_dat & 4) ? 0xff : 0) & svga->gdcreg[8]) | (svga->latch.b[2] & ~svga->gdcreg[8]);
			if (writemask2 & 8) svga->vram[addr | 0x3] = (((cpu_dat & 8) ? 0xff : 0) & svga->gdcreg[8]) | (svga->latch.b[3] & ~svga->gdcreg[8]);
		} else {
			vala = ((cpu_dat & 1) ? 0xff : 0);
			valb = ((cpu_dat & 2) ? 0xff : 0);
			valc = ((cpu_dat & 4) ? 0xff : 0);
			vald = ((cpu_dat & 8) ? 0xff : 0);
			switch (svga->gdcreg[3] & 0x18) {
				case 0: /*Set*/
					if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) | (svga->latch.b[0] & ~svga->gdcreg[8]);
					if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) | (svga->latch.b[1] & ~svga->gdcreg[8]);
					if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) | (svga->latch.b[2] & ~svga->gdcreg[8]);
					if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) | (svga->latch.b[3] & ~svga->gdcreg[8]);
					break;
				case 8: /*AND*/
					if (writemask2 & 1) svga->vram[addr]       = (vala | ~svga->gdcreg[8]) & svga->latch.b[0];
					if (writemask2 & 2) svga->vram[addr | 0x1] = (valb | ~svga->gdcreg[8]) & svga->latch.b[1];
					if (writemask2 & 4) svga->vram[addr | 0x2] = (valc | ~svga->gdcreg[8]) & svga->latch.b[2];
					if (writemask2 & 8) svga->vram[addr | 0x3] = (vald | ~svga->gdcreg[8]) & svga->latch.b[3];
					break;
				case 0x10: /*OR*/
					if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) | svga->latch.b[0];
					if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) | svga->latch.b[1];
					if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) | svga->latch.b[2];
					if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) | svga->latch.b[3];
					break;
				case 0x18: /*XOR*/
					if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) ^ svga->latch.b[0];
					if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) ^ svga->latch.b[1];
					if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) ^ svga->latch.b[2];
					if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) ^ svga->latch.b[3];
					break;
			}
		}
		break;
	case 3:
		wm = svga->gdcreg[8];
		svga->gdcreg[8] &= cpu_dat;

		vala = (svga->gdcreg[0] & 1) ? 0xff : 0;
		valb = (svga->gdcreg[0] & 2) ? 0xff : 0;
		valc = (svga->gdcreg[0] & 4) ? 0xff : 0;
		vald = (svga->gdcreg[0] & 8) ? 0xff : 0;
		switch (svga->gdcreg[3] & 0x18) {
			case 0: /*Set*/
				if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) | (svga->latch.b[0] & ~svga->gdcreg[8]);
				if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) | (svga->latch.b[1] & ~svga->gdcreg[8]);
				if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) | (svga->latch.b[2] & ~svga->gdcreg[8]);
				if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) | (svga->latch.b[3] & ~svga->gdcreg[8]);
				break;
			case 8: /*AND*/
				if (writemask2 & 1) svga->vram[addr]       = (vala | ~svga->gdcreg[8]) & svga->latch.b[0];
				if (writemask2 & 2) svga->vram[addr | 0x1] = (valb | ~svga->gdcreg[8]) & svga->latch.b[1];
				if (writemask2 & 4) svga->vram[addr | 0x2] = (valc | ~svga->gdcreg[8]) & svga->latch.b[2];
				if (writemask2 & 8) svga->vram[addr | 0x3] = (vald | ~svga->gdcreg[8]) & svga->latch.b[3];
				break;
			case 0x10: /*OR*/
				if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) | svga->latch.b[0];
				if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) | svga->latch.b[1];
				if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) | svga->latch.b[2];
				if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) | svga->latch.b[3];
				break;
			case 0x18: /*XOR*/
				if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) ^ svga->latch.b[0];
				if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) ^ svga->latch.b[1];
				if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) ^ svga->latch.b[2];
				if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) ^ svga->latch.b[3];
				break;
		}
		svga->gdcreg[8] = wm;
		break;
    }
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

    if (ht216->ht_regs[0xcd] & HT_REG_CD_RMWMDE) /*RMW*/
	input_b = svga->vram[addr];
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
		input_a = ht216->ht_regs[0xec + (addr & 3)];
		break;
	case 0x0c:
		input_a = ht216->bg_latch[addr & 7];
		break;
    }

    fg = extalu(ht216->ht_regs[0xce] >> 4, input_a, input_b);
    bg = extalu(ht216->ht_regs[0xce] & 0xf,  input_a, input_b);
    output = (fg & rop_select) | (bg & ~rop_select);
    svga->vram[addr] = (svga->vram[addr] & ~bit_mask) | (output & bit_mask);
    svga->changedvram[addr >> 12] = changeframecount;
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
    uint8_t bit_mask = 0, rop_select = 0;

    sub_cycles(video_timing_write_b);

    egawrites++;

    addr &= 0xfffff;

    val = svga_rotate[svga->gdcreg[3] & 7][val];

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
		ht216_dm_extalu_write(ht216, addr,     (val & 0x80) ? 0xff : 0, (bit_mask & 0x80) ? 0xff : 0, val, (rop_select & 0x80) ? 0xff : 0);
		ht216_dm_extalu_write(ht216, addr + 1, (val & 0x40) ? 0xff : 0, (bit_mask & 0x40) ? 0xff : 0, val, (rop_select & 0x40) ? 0xff : 0);
		ht216_dm_extalu_write(ht216, addr + 2, (val & 0x20) ? 0xff : 0, (bit_mask & 0x20) ? 0xff : 0, val, (rop_select & 0x20) ? 0xff : 0);
		ht216_dm_extalu_write(ht216, addr + 3, (val & 0x10) ? 0xff : 0, (bit_mask & 0x10) ? 0xff : 0, val, (rop_select & 0x10) ? 0xff : 0);
		ht216_dm_extalu_write(ht216, addr + 4, (val & 0x08) ? 0xff : 0, (bit_mask & 0x08) ? 0xff : 0, val, (rop_select & 0x08) ? 0xff : 0);
		ht216_dm_extalu_write(ht216, addr + 5, (val & 0x04) ? 0xff : 0, (bit_mask & 0x04) ? 0xff : 0, val, (rop_select & 0x04) ? 0xff : 0);
		ht216_dm_extalu_write(ht216, addr + 6, (val & 0x02) ? 0xff : 0, (bit_mask & 0x02) ? 0xff : 0, val, (rop_select & 0x02) ? 0xff : 0);
		ht216_dm_extalu_write(ht216, addr + 7, (val & 0x01) ? 0xff : 0, (bit_mask & 0x01) ? 0xff : 0, val, (rop_select & 0x01) ? 0xff : 0);
	} else
		ht216_dm_extalu_write(ht216, addr, val, bit_mask, val, rop_select);
    } else {
	if (ht216->ht_regs[0xcd] & HT_REG_CD_FP8PCEXP) {	/*1->8 bit expansion*/
		addr = (addr << 3) & 0xfffff;
		ht216_dm_write(ht216, addr,     (val & 0x80) ? 0xff : 0, val);
		ht216_dm_write(ht216, addr + 1, (val & 0x40) ? 0xff : 0, val);
		ht216_dm_write(ht216, addr + 2, (val & 0x20) ? 0xff : 0, val);
		ht216_dm_write(ht216, addr + 3, (val & 0x10) ? 0xff : 0, val);
		ht216_dm_write(ht216, addr + 4, (val & 0x08) ? 0xff : 0, val);
		ht216_dm_write(ht216, addr + 5, (val & 0x04) ? 0xff : 0, val);
		ht216_dm_write(ht216, addr + 6, (val & 0x02) ? 0xff : 0, val);
		ht216_dm_write(ht216, addr + 7, (val & 0x01) ? 0xff : 0, val);
	} else
		ht216_dm_write(ht216, addr, val, val);
    }
}


static void
ht216_write(uint32_t addr, uint8_t val, void *p)
{
    ht216_t *ht216 = (ht216_t *)p;
    svga_t *svga = &ht216->svga;

    addr &= svga->banked_mask;
    addr = (addr & 0x7fff) + ht216->write_bank[(addr >> 15) & 1];

    if (!ht216->ht_regs[0xcd] && !ht216->ht_regs[0xfe])
	svga_write_linear(addr, val, &ht216->svga);
    else
	ht216_write_common(ht216, addr, val);
}


static void
ht216_writew(uint32_t addr, uint16_t val, void *p)
{
    ht216_t *ht216 = (ht216_t *)p;
    svga_t *svga = &ht216->svga;

    addr &= svga->banked_mask;
    addr = (addr & 0x7fff) + ht216->write_bank[(addr >> 15) & 1];
    if (!ht216->ht_regs[0xcd] && !ht216->ht_regs[0xfe])
	svga_writew_linear(addr, val, &ht216->svga);
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

    addr &= svga->banked_mask;
    addr = (addr & 0x7fff) + ht216->write_bank[(addr >> 15) & 1];
    if (!ht216->ht_regs[0xcd] && !ht216->ht_regs[0xfe])
	svga_writel_linear(addr, val, &ht216->svga);
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

    if (!svga->chain4) /*Bits 16 and 17 of linear address seem to be unused in planar modes*/
	addr = (addr & 0xffff) | ((addr & 0xc0000) >> 2);

    if (!ht216->ht_regs[0xcd] && !ht216->ht_regs[0xfe])
	svga_write_linear(addr, val, &ht216->svga);
    else
	ht216_write_common(ht216, addr, val);
}


static void
ht216_writew_linear(uint32_t addr, uint16_t val, void *p)
{
    ht216_t *ht216 = (ht216_t *)p;
    svga_t *svga = &ht216->svga;

    if (!svga->chain4)
	addr = (addr & 0xffff) | ((addr & 0xc0000) >> 2);

    if (!ht216->ht_regs[0xcd] && !ht216->ht_regs[0xfe])
	svga_writew_linear(addr, val, &ht216->svga);
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

    if (!svga->chain4)
	addr = (addr & 0xffff) | ((addr & 0xc0000) >> 2);

    if (!ht216->ht_regs[0xcd] && !ht216->ht_regs[0xfe])
	svga_writel_linear(addr, val, &ht216->svga);
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
    uint8_t temp, temp2, temp3, temp4, or;
    int readplane = svga->readplane;
    int offset;
    uint32_t latch_addr;

    if (ht216->ht_regs[0xc8] & HT_REG_C8_MOVSB)
	addr <<= 3;

    addr &= 0xfffff;

    sub_cycles(video_timing_read_b);

    egareads++;

    if (svga->chain4 || svga->fb_only) {
	addr &= svga->decode_mask;
	if (addr >= svga->vram_max)
		return 0xff;

	latch_addr = (addr & svga->vram_mask) & ~7;
	if (ht216->ht_regs[0xcd] & HT_REG_CD_ASTODE)
		latch_addr += (svga->gdcreg[3] & 7);
	ht216->bg_latch[0] = svga->vram[latch_addr];
	ht216->bg_latch[1] = svga->vram[latch_addr + 1];
	ht216->bg_latch[2] = svga->vram[latch_addr + 2];
	ht216->bg_latch[3] = svga->vram[latch_addr + 3];
	ht216->bg_latch[4] = svga->vram[latch_addr + 4];
	ht216->bg_latch[5] = svga->vram[latch_addr + 5];
	ht216->bg_latch[6] = svga->vram[latch_addr + 6];
	ht216->bg_latch[7] = svga->vram[latch_addr + 7];

	return svga->vram[addr & svga->vram_mask];
    } else if (svga->chain2_read) {
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

	ht216->bg_latch[0] = svga->vram[latch_addr | offset];
	ht216->bg_latch[1] = svga->vram[latch_addr | ((offset + 1) & 7)];
	ht216->bg_latch[2] = svga->vram[latch_addr | ((offset + 2) & 7)];
	ht216->bg_latch[3] = svga->vram[latch_addr | ((offset + 3) & 7)];
	ht216->bg_latch[4] = svga->vram[latch_addr | ((offset + 4) & 7)];
	ht216->bg_latch[5] = svga->vram[latch_addr | ((offset + 5) & 7)];
	ht216->bg_latch[6] = svga->vram[latch_addr | ((offset + 6) & 7)];
	ht216->bg_latch[7] = svga->vram[latch_addr | ((offset + 7) & 7)];
    } else {
	ht216->bg_latch[0] = svga->vram[latch_addr];
	ht216->bg_latch[1] = svga->vram[latch_addr | 1];
	ht216->bg_latch[2] = svga->vram[latch_addr | 2];
	ht216->bg_latch[3] = svga->vram[latch_addr | 3];
	ht216->bg_latch[4] = svga->vram[latch_addr | 4];
	ht216->bg_latch[5] = svga->vram[latch_addr | 5];
	ht216->bg_latch[6] = svga->vram[latch_addr | 6];
	ht216->bg_latch[7] = svga->vram[latch_addr | 7];
    }
    or = addr & 4;
    svga->latch.d[0] = ht216->bg_latch[0 | or] | (ht216->bg_latch[1 | or] << 8) |
		       (ht216->bg_latch[2 | or] << 16) | (ht216->bg_latch[3 | or] << 24);
    if (svga->readmode) {
	temp   = svga->latch.b[0];
	temp  ^= (svga->colourcompare & 1) ? 0xff : 0;
	temp  &= (svga->colournocare & 1)  ? 0xff : 0;
	temp2  = svga->latch.b[1];
	temp2 ^= (svga->colourcompare & 2) ? 0xff : 0;
	temp2 &= (svga->colournocare & 2)  ? 0xff : 0;
	temp3  = svga->latch.b[2];
	temp3 ^= (svga->colourcompare & 4) ? 0xff : 0;
	temp3 &= (svga->colournocare & 4)  ? 0xff : 0;
	temp4  = svga->latch.b[3];
	temp4 ^= (svga->colourcompare & 8) ? 0xff : 0;
	temp4 &= (svga->colournocare & 8)  ? 0xff : 0;
	return ~(temp | temp2 | temp3 | temp4);
    }

    return svga->vram[addr | readplane];
}


static uint8_t
ht216_read(uint32_t addr, void *p)
{
    ht216_t *ht216 = (ht216_t *)p;
    svga_t *svga = &ht216->svga;

    addr &= svga->banked_mask;
    addr = (addr & 0x7fff) + ht216->read_bank[(addr >> 15) & 1];

    return ht216_read_common(ht216, addr);
}


static uint8_t
ht216_read_linear(uint32_t addr, void *p)
{
    ht216_t *ht216 = (ht216_t *)p;
    svga_t *svga = &ht216->svga;

    if (svga->chain4)
	return ht216_read_common(ht216, addr);
    else
	return ht216_read_common(ht216, (addr & 0xffff) | ((addr & 0xc0000) >> 2));
}


void
*ht216_init(const device_t *info, uint32_t mem_size, int has_rom)
{
    ht216_t *ht216 = malloc(sizeof(ht216_t));
    svga_t *svga;

    memset(ht216, 0, sizeof(ht216_t));
    svga = &ht216->svga;

    io_sethandler(0x03c0, 0x0020, ht216_in, NULL, NULL, ht216_out, NULL, NULL, ht216);
    io_sethandler(0x46e8, 0x0001, ht216_in, NULL, NULL, ht216_out, NULL, NULL, ht216);

    if (has_rom == 1)
	rom_init(&ht216->bios_rom, BIOS_VIDEO7_VGA_1024I_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
    else if (has_rom == 2)
	rom_init(&ht216->bios_rom, BIOS_G2_GC205_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    if (info->flags & DEVICE_VLB)
	video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_v7vga_vlb);
    else
	video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_v7vga_isa);

    svga_init(info, &ht216->svga, ht216, mem_size,
	      ht216_recalctimings,
	      ht216_in, ht216_out,
	      ht216_hwcursor_draw,
	      NULL);
    svga->hwcursor.ysize = 32;
    ht216->vram_mask = mem_size - 1;
    svga->decode_mask = mem_size - 1;

    if (info->flags & DEVICE_VLB) {
	mem_mapping_set_handler(&ht216->svga.mapping, ht216_read, NULL, NULL, ht216_write, ht216_writew, ht216_writel);
	mem_mapping_add(&ht216->linear_mapping, 0, 0, ht216_read_linear, NULL, NULL, ht216_write_linear, ht216_writew_linear, ht216_writel_linear, NULL, MEM_MAPPING_EXTERNAL, &ht216->svga);
    } else {
	mem_mapping_set_handler(&ht216->svga.mapping, ht216_read, NULL, NULL, ht216_write, ht216_writew, NULL);
	mem_mapping_add(&ht216->linear_mapping, 0, 0, ht216_read_linear, NULL, NULL, ht216_write_linear, ht216_writew_linear, NULL, NULL, MEM_MAPPING_EXTERNAL, &ht216->svga);
    }
    mem_mapping_set_p(&ht216->svga.mapping, ht216);

    svga->bpp = 8;
    svga->miscout = 1;

    ht216->ht_regs[0xb4] = 0x08; /*32-bit DRAM bus*/
    ht216->id = info->local;

    return ht216;
}


static void *
g2_gc205_init(const device_t *info)
{
    ht216_t *ht216 = ht216_init(info, 1 << 19, 2);

    return ht216;
}


static void *
v7_vga_1024i_init(const device_t *info)
{
    ht216_t *ht216 = ht216_init(info, device_get_config_int("memory") << 10, 1);

    return ht216;
}


static void *
ht216_pb410a_init(const device_t *info)
{
    ht216_t *ht216 = ht216_init(info, 1 << 20, 0);

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


static const device_config_t v7_vga_1024i_config[] =
{
        {
                "memory", "Memory size", CONFIG_SELECTION, "", 512, "", { 0 },
                {
                        {
                                "256 kB", 256
                        },
                        {
                                "512 kB", 512
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

const device_t g2_gc205_device =
{
    "G2 GC205",
    DEVICE_ISA,
    0x7070,
    g2_gc205_init,
    ht216_close,
    NULL,
    { g2_gc205_available },
    ht216_speed_changed,
    ht216_force_redraw
};

const device_t v7_vga_1024i_device =
{
    "Video 7 VGA 1024i",
    DEVICE_ISA,
    0x7140,
    v7_vga_1024i_init,
    ht216_close,
    NULL,
    { v7_vga_1024i_available },
    ht216_speed_changed,
    ht216_force_redraw,
    v7_vga_1024i_config
};

const device_t ht216_32_pb410a_device =
{
    "Headland HT216-32 (Packard Bell PB410A)",
    DEVICE_VLB,
    0x7861,	/*HT216-32*/
    ht216_pb410a_init,
    ht216_close,
    NULL,
    { NULL },
    ht216_speed_changed,
    ht216_force_redraw
};
