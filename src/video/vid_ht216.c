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
    uint16_t		id, misc;
    uint32_t		read_banks[2], write_banks[2];
        
    uint8_t		bg_latch[8];
        
    uint8_t		ht_regs[256];
} ht216_t;


#define HT_MISC_PAGE_SEL (1 << 5)

/*Shifts CPU VRAM read address by 3 bits, for use with fat pixel color expansion*/
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
		/*Bit 17 of the display memory address, only active on odd/even modes, has no effect on graphics modes.*/
                ht216->misc = val;
		ht216_log("HT216 misc val = %02x\n", val);
		ht216_remap(ht216);
		svga->fullchange = changeframecount;
		svga_recalctimings(svga);
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
					svga->fullchange = changeframecount;
					svga_recalctimings(svga);
					break;
				case 0xa5:
					svga->hwcursor.ena = !!(val & 0x80);
					break;

				case 0xc8:
					if ((old ^ val) & HT_REG_C8_E256) {
						svga->fullchange = changeframecount;
						svga_recalctimings(svga);
					}
					break;

				/*Bank registers*/
				case 0xe8:
				case 0xe9:
					ht216_remap(ht216);
					break;

				case 0xf6:
					svga->vram_display_mask = (val & 0x40) ? ht216->vram_mask : 0x3ffff;
					/*Bits 18 and 19 of the display memory address*/
					ht216_log("HT216 reg 0xf6 write = %02x, vram mask = %08x\n", val & 0x40, svga->vram_display_mask);
					ht216_remap(ht216);	
					svga->fullchange = changeframecount;
					svga_recalctimings(svga);					
					break;
				
				case 0xf9:
					/*Bit 16 of the display memory address, only active when in chain4 mode and 256 color mode.*/
					ht216_log("HT216 reg 0xf9 write = %02x\n", val & HT_REG_F9_XPSEL);				
					ht216_remap(ht216);
					break;
					
				case 0xfc:
					svga->fullchange = changeframecount;
					svga_recalctimings(svga);					
					break;
					
				case 0xff:
					svga->hwcursor.addr = ((ht216->ht_regs[0x94] << 6) | (3 << 14) | ((val & 0x60) << 11)) << 2;
					break;
			}
			switch (svga->seqaddr & 0xff) {
				case 0xc8: case 0xc9: case 0xcf: case 0xe0:
					if ((svga->seqaddr & 0xff) == 0xc8) {
						svga->adv_flags = 0;
						if (val & HT_REG_C8_MOVSB) {
							svga->adv_flags = FLAG_ADDR_BY8;
						}
					}
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
			mem_mapping_disable(&svga->mapping);
			mem_mapping_disable(&ht216->linear_mapping);
			if (val & 8) {
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
    uint8_t bank = ht216->ht_regs[0xf6] & 0x0f;

    mem_mapping_disable(&ht216->linear_mapping);
    if (ht216->ht_regs[0xc8] & HT_REG_C8_XLAM) {
	/*Linear mapping enabled*/
	uint32_t linear_base = ((ht216->ht_regs[0xc9] & 0xf) << 20) | (ht216->ht_regs[0xcf] << 24);
	mem_mapping_disable(&svga->mapping);
	mem_mapping_set_addr(&ht216->linear_mapping, linear_base, 0x100000);
    } else {
	if (ht216->ht_regs[0xe0] & HT_REG_E0_SBAE) {
		/*Split bank: two banks used*/
		/*Windows 3.1 always touches the misc register when split banks are enabled.*/
		if (!(ht216->misc & HT_MISC_PAGE_SEL)) {
			ht216->read_banks[0] = ht216->ht_regs[0xe8] << 12;
			ht216->write_banks[0] = ht216->ht_regs[0xe8] << 12;
			ht216->read_banks[1] = ht216->ht_regs[0xe9] << 12;
			ht216->write_banks[1] = ht216->ht_regs[0xe9] << 12;
			
			if (!svga->chain4) {
				if (ht216->ht_regs[0xe8] == 0x40) {
					ht216->read_banks[0] = 0x10000;
					ht216->write_banks[0] = 0x10000;
				}
				if (ht216->ht_regs[0xe9] == 0x40) {
					ht216->read_banks[1] = 0x10000;
					ht216->write_banks[1] = 0x10000;
				}
	
				if (ht216->ht_regs[0xe8] == 0x48) {
					ht216->read_banks[0] = 0x18000;
					ht216->write_banks[0] = 0x18000;
				}
				if (ht216->ht_regs[0xe9] == 0x48) {
					ht216->read_banks[1] = 0x18000;
					ht216->write_banks[1] = 0x18000;
				}
				
				if (ht216->ht_regs[0xe8] == 0x80) {
					ht216->read_banks[0] = 0x20000;
					ht216->write_banks[0] = 0x20000;
				}
				if (ht216->ht_regs[0xe9] == 0x80) {
					ht216->read_banks[1] = 0x20000;
					ht216->write_banks[1] = 0x20000;
				}
				
				if (ht216->ht_regs[0xe8] == 0x88) {
					ht216->read_banks[0] = 0x28000;
					ht216->write_banks[0] = 0x28000;
				}
				if (ht216->ht_regs[0xe9] == 0x88) {
					ht216->read_banks[1] = 0x28000;
					ht216->write_banks[1] = 0x28000;
				}
				
				if (ht216->ht_regs[0xe9] == 0xc0) {
					ht216->read_banks[1] = 0x30000;
					ht216->write_banks[1] = 0x30000;
				}
			}
		}
	} else {
		/*One bank used*/
		/*Bit 17 of the video memory address*/
		if (ht216->misc & HT_MISC_PAGE_SEL) {
			ht216->read_bank_reg[0] |= 0x20;
			ht216->write_bank_reg[0] |= 0x20;
		} else {
			ht216->read_bank_reg[0] &= ~0x20;
			ht216->write_bank_reg[0] &= ~0x20;
		}

		if (!(ht216->ht_regs[0xfc] & HT_REG_FC_ECOLRE)) {
			if (bank & 2) {
				bank &= ~2;
				bank |= 1;
			}
			if (bank & 8) {
				bank &= ~8;
				bank |= 4;
			}
		}

		/*Bit 18 and 19 of the video memory address*/		
		if (bank & 1)
			ht216->write_bank_reg[0] |= 0x40;
		else
			ht216->write_bank_reg[0] &= ~0x40;
		
		if (bank & 2)
			ht216->write_bank_reg[0] |= 0x80;
		else
			ht216->write_bank_reg[0] &= ~0x80;
		
		if (bank & 4)
			ht216->read_bank_reg[0] |= 0x40;
		else
			ht216->read_bank_reg[0] &= ~0x40;		
		
		if (bank & 8)
			ht216->read_bank_reg[0] |= 0x80;
		else
			ht216->read_bank_reg[0] &= ~0x80;		

		if (svga->chain4) {
			/*Bit 16 of the video memory address*/
			if (ht216->ht_regs[0xf9] & HT_REG_F9_XPSEL) {
				ht216->read_bank_reg[0] |= 0x10;
				ht216->write_bank_reg[0] |= 0x10;
			} else {
				ht216->read_bank_reg[0] &= ~0x10;
				ht216->write_bank_reg[0] &= ~0x10;
			}
		}
		
		if (!svga->chain4) {			
			/*In linear modes, bits 4 and 5 are ignored*/
			ht216->read_bank_reg[0] &= ~0x30;
			ht216->write_bank_reg[0] &= ~0x30;
		}
		
		ht216->read_banks[0] = ht216->read_bank_reg[0] << 12;
		ht216->write_banks[0] = ht216->write_bank_reg[0] << 12;	
		ht216->read_banks[1] = ht216->read_banks[0] + (svga->chain4 ? 0x8000 : 0x20000);
		ht216->write_banks[1] = ht216->write_banks[0] + (svga->chain4 ? 0x8000 : 0x20000);

		if (!svga->chain4) {
			ht216->read_banks[0] >>= 2;
			ht216->read_banks[1] >>= 2;
			ht216->write_banks[0] >>= 2;
			ht216->write_banks[1] >>= 2;
		} 
	}
	
	ht216_log("ReadBank0 = %06x, ReadBank1 = %06x, Misc Page Sel = %02x, F6 reg = %02x, F9 Sel = %02x, FC = %02x, E0 split = %02x, E8 = %02x, E9 = %02x, chain4 = %02x, banked mask = %04x\n", ht216->read_banks[0], ht216->read_banks[1], ht216->misc & HT_MISC_PAGE_SEL, bank, ht216->ht_regs[0xf9] & HT_REG_F9_XPSEL, ht216->ht_regs[0xfc] & (HT_REG_FC_ECOLRE | 2), ht216->ht_regs[0xe0] & HT_REG_E0_SBAE, ht216->ht_regs[0xe8], ht216->ht_regs[0xe9], svga->chain4, svga->banked_mask);
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

    if ((svga->bpp == 8) && !svga->lowres) {
	svga->render = svga_render_8bpp_highres;
    }
}


static void
ht216_hwcursor_draw(svga_t *svga, int displine)
{
    int x;
    uint32_t dat[2];
    int offset = svga->hwcursor_latch.x + svga->hwcursor_latch.xoff;

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
		((uint32_t *)buffer32->line[displine])[svga->x_add + offset + x]  = 0;
	if (dat[1] & 0x80000000)
		((uint32_t *)buffer32->line[displine])[svga->x_add + offset + x] ^= 0xffffff;

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
    int writemask2 = svga->writemask, reset_wm = 0;
    latch_t vall;
    uint8_t wm = svga->writemask;
    uint8_t count, i;
    uint8_t fg_data[4] = {0, 0, 0, 0};

    if (svga->adv_flags & FLAG_ADDR_BY8)
	writemask2 = svga->seqregs[2];

    if (!(svga->gdcreg[6] & 1))
	svga->fullchange = 2;

    if ((svga->adv_flags & FLAG_ADDR_BY8) && (svga->writemode < 4))
	addr <<= 3;
    else if ((svga->chain4 || svga->fb_only) && (svga->writemode < 4)) {
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

    if (addr >= svga->vram_max)
	return;

    addr &= svga->decode_mask;

    if (addr >= svga->vram_max)
	return;

    addr &= svga->vram_mask;

    svga->changedvram[addr >> 12] = changeframecount;

    count = 4;

    switch (ht216->ht_regs[0xfe] & HT_REG_FE_FBMC) {
	case 0x00:
		for (i = 0; i < count; i++)
			fg_data[i] = cpu_dat;
		break;
	case 0x04:
		if (ht216->ht_regs[0xfe] & HT_REG_FE_FBRC) {
			if (addr & 4) {
				for (i = 0; i < count; i++) {
					fg_data[i] = (cpu_dat_unexpanded & (1 << (((addr + i + 4) & 7) ^ 7))) ? ht216->ht_regs[0xfa] : ht216->ht_regs[0xfb];
				}
			} else {
				for (i = 0; i < count; i++) {
					fg_data[i] = (cpu_dat_unexpanded & (1 << (((addr + i) & 7) ^ 7))) ? ht216->ht_regs[0xfa] : ht216->ht_regs[0xfb];
				}
			}
		} else {
			if (addr & 4) {
				for (i = 0; i < count; i++)
					fg_data[i] = (ht216->ht_regs[0xf5] & (1 << (((addr + i + 4) & 7) ^ 7))) ? ht216->ht_regs[0xfa] : ht216->ht_regs[0xfb];
			} else {
				for (i = 0; i < count; i++)
					fg_data[i] = (ht216->ht_regs[0xf5] & (1 << (((addr + i) & 7) ^ 7))) ? ht216->ht_regs[0xfa] : ht216->ht_regs[0xfb];
			}
		}
		break;
	case 0x08:
	case 0x0c:
		for (i = 0; i < count; i++)
			fg_data[i] = ht216->ht_regs[0xec + i];
		break;
    }

    switch (svga->writemode) {
	case 0:
		if ((svga->gdcreg[8] == 0xff) && !(svga->gdcreg[3] & 0x18) && (!svga->gdcreg[1] || svga->set_reset_disabled)) {
			for (i = 0; i < count; i++) {
				if (svga->adv_flags & FLAG_ADDR_BY8) {
					if (writemask2 & (0x80 >> i))
						svga->vram[addr | i] = fg_data[i];
				} else {
					if (writemask2 & (1 << i)) {
						svga->vram[addr | i] = fg_data[i];
					}
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
			if (svga->adv_flags & FLAG_ADDR_BY8) {
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
				if (svga->adv_flags & FLAG_ADDR_BY8) {
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
			if (svga->adv_flags & FLAG_ADDR_BY8) {
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
			if (svga->adv_flags & FLAG_ADDR_BY8) {
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
			if (svga->adv_flags & FLAG_ADDR_BY8) {
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
			if (svga->adv_flags & FLAG_ADDR_BY8) {
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
ht216_dm_masked_write(ht216_t *ht216, uint32_t addr, uint8_t val, uint8_t bit_mask)
{    
    svga_t *svga = &ht216->svga;
    int writemask2 = svga->writemask;
    uint8_t count, i;

    if (svga->adv_flags & FLAG_ADDR_BY8)
	writemask2 = svga->seqregs[2];

    if (!(svga->gdcreg[6] & 1))
	svga->fullchange = 2;

    if ((svga->adv_flags & FLAG_ADDR_BY8) && (svga->writemode < 4))
	addr <<= 3;
    else if ((svga->chain4 || svga->fb_only) && (svga->writemode < 4)) {
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

    if (addr >= svga->vram_max)
	return;

    addr &= svga->decode_mask;

    if (addr >= svga->vram_max)
	return;

    addr &= svga->vram_mask;

    svga->changedvram[addr >> 12] = changeframecount;

    count = 4;

    if (bit_mask == 0xff) {
	for (i = 0; i < count; i++) {
		if (writemask2 & (1 << i)) {
			svga->vram[addr | i] = val;
		}
	}
    } else {
	if (writemask2 == 0x0f) {
		for (i = 0; i < count; i++) {
			svga->vram[addr | i] = (svga->latch.b[i] & bit_mask) | (svga->vram[addr | i] & ~bit_mask);
		}
	} else {
		for (i = 0; i < count; i++) {
			if (writemask2 & (1 << i)) {
				svga->vram[addr | i] = (val & bit_mask) | (svga->vram[addr | i] & ~bit_mask);
			}
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

    egawrites++;

    addr &= 0xfffff;

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

    addr &= svga->banked_mask;
    addr = (addr & 0x7fff) + ht216->write_banks[(addr >> 15) & 1];

    if (!ht216->ht_regs[0xcd] && !ht216->ht_regs[0xfe] && !ht216->ht_regs[0xf3])
	svga_write_linear(addr, val, svga);
    else
	ht216_write_common(ht216, addr, val);
}


static void
ht216_writew(uint32_t addr, uint16_t val, void *p)
{
    ht216_t *ht216 = (ht216_t *)p;
    svga_t *svga = &ht216->svga;
	
    addr &= svga->banked_mask;
    addr = (addr & 0x7fff) + ht216->write_banks[(addr >> 15) & 1];

    if (!ht216->ht_regs[0xcd] && !ht216->ht_regs[0xfe] && !ht216->ht_regs[0xf3])
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

    addr &= svga->banked_mask;
    addr = (addr & 0x7fff) + ht216->write_banks[(addr >> 15) & 1];	

    if (!ht216->ht_regs[0xcd] && !ht216->ht_regs[0xfe] && !ht216->ht_regs[0xf3])
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

    if (!svga->chain4) /*Bits 16 and 17 of linear address seem to be unused in planar modes*/
	addr = (addr & 0xffff) | ((addr & 0xc0000) >> 2);

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

    if (!svga->chain4)
	addr = (addr & 0xffff) | ((addr & 0xc0000) >> 2);

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

    if (!svga->chain4)
	addr = (addr & 0xffff) | ((addr & 0xc0000) >> 2);

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
    int readplane = svga->readplane;
    uint8_t or, count, i;
    uint8_t plane, pixel;
    uint8_t temp, ret;
    int offset;

    if (svga->adv_flags & FLAG_ADDR_BY8) {
	readplane = svga->gdcreg[4] & 7;
    }

    cycles -= video_timing_read_b;

    egareads++;
    
    addr &= 0xfffff;

    count = 2;

    latch_addr = (addr << count) & svga->decode_mask;
    count = (1 << count);

    if (svga->adv_flags & FLAG_ADDR_BY8)
	addr <<= 3;
    else if (svga->chain4 || svga->fb_only) {
	addr &= svga->decode_mask;
	if (addr >= svga->vram_max)
		return 0xff;
	latch_addr = (addr & svga->vram_mask) & ~7;
	if (ht216->ht_regs[0xcd] & HT_REG_CD_ASTODE)
		latch_addr += (svga->gdcreg[3] & 7);
	for (i = 0; i < 8; i++)
		ht216->bg_latch[i] = svga->vram[latch_addr | i];
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
	for (i = 0; i < 8; i++)
		ht216->bg_latch[i] = svga->vram[latch_addr | ((offset + i) & 7)];
    } else {
	for (i = 0; i < 8; i++)
		ht216->bg_latch[i] = svga->vram[latch_addr | i];
    }
    
    or = addr & 4;
    svga->latch.d[0] = ht216->bg_latch[0 | or] | (ht216->bg_latch[1 | or] << 8) |
		       (ht216->bg_latch[2 | or] << 16) | (ht216->bg_latch[3 | or] << 24);
    if (svga->readmode) {
	temp = 0xff;
	
	for (pixel = 0; pixel < 8; pixel++) {
		for (plane = 0; plane < 4; plane++) {
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
 
    addr &= svga->banked_mask;
    addr = (addr & 0x7fff) + ht216->read_banks[(addr >> 15) & 1];
    
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

    svga_init(info, svga, ht216, mem_size,
	      ht216_recalctimings,
	      ht216_in, ht216_out,
	      ht216_hwcursor_draw,
	      NULL);
    svga->hwcursor.ysize = 32;
    ht216->vram_mask = mem_size - 1;
    svga->decode_mask = mem_size - 1;

    if (info->flags & DEVICE_VLB) {
	mem_mapping_set_handler(&svga->mapping, ht216_read, NULL, NULL, ht216_write, ht216_writew, ht216_writel);
	mem_mapping_add(&ht216->linear_mapping, 0, 0, ht216_read_linear, NULL, NULL, ht216_write_linear, ht216_writew_linear, ht216_writel_linear, NULL, MEM_MAPPING_EXTERNAL, svga);
    } else {
	mem_mapping_set_handler(&svga->mapping, ht216_read, NULL, NULL, ht216_write, ht216_writew, NULL);
	mem_mapping_add(&ht216->linear_mapping, 0, 0, ht216_read_linear, NULL, NULL, ht216_write_linear, ht216_writew_linear, NULL, NULL, MEM_MAPPING_EXTERNAL, svga);
    }
    mem_mapping_set_p(&svga->mapping, ht216);
    mem_mapping_disable(&ht216->linear_mapping);

    svga->bpp = 8;
    svga->miscout = 1;

    ht216->id = info->local;
    
    if (ht216->id == 0x7861)
	ht216->ht_regs[0xb4] = 0x08; /*32-bit DRAM bus*/

    svga->adv_flags = 0;

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

static const device_config_t ht216_config[] =
{
        {
                "memory", "Memory size", CONFIG_SELECTION, "", 1024, "", { 0 },
                {
                        {
                                "512 kB", 512
                        },
                        {
                                "1 MB", 1024
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
    ht216_force_redraw,
    ht216_config
};
