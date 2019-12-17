/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of nVidia's RIVA 128 graphics card.
 *		Special thanks to Marcin Kościelnicki, without whom this
 *		would not have been possible.
 *
 * Version:	@(#)vid_riva128.c	1.0.0	2019/09/13
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Melissa Goad
 *
 *		Copyright 2019 Miran Grca.
 *		Copyright 2019 Melissa Goad.
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
#include "vid_riva128.h"

#define BIOS_RIVA128_PATH		L"roms/video/nvidia/Diamond_V330_rev-e.vbi"

#define RIVA128_VENDOR_ID 0x12d2
#define RIVA128_DEVICE_ID 0x0018

typedef struct riva128_t
{
	mem_mapping_t	mmio_mapping;
	mem_mapping_t 	linear_mapping;
	mem_mapping_t 	ramin_mapping;

	svga_t		svga;

	rom_t		bios_rom;

	uint32_t		vram_size, vram_mask,
			mmio_base, lfb_base;

	uint8_t		read_bank, write_bank;

	uint8_t		pci_regs[256];
	uint8_t		int_line;

	int			card;

	struct
	{
		int sda, scl;
	} i2c;

	struct
	{
		uint8_t rma_access_reg[4];
		uint8_t rma_mode;
		uint32_t rma_dst_addr;
		uint32_t rma_data;
	} rma;

	struct 
	{
		uint32_t intr;
		uint32_t intr_en;
		uint32_t intr_line;
		uint32_t enable;
	} pmc;

	struct
	{
		uint32_t cache_error;
		uint32_t intr;
		uint32_t intr_en;

		uint32_t ramht;
		uint32_t ramht_addr;
		uint32_t ramht_size;

		uint32_t ramfc;
		uint32_t ramfc_addr;

		uint32_t ramro;
		uint32_t ramro_addr;
		uint32_t ramro_size;

		uint16_t chan_mode;
		uint16_t chan_dma;
		uint16_t chan_size; //0 = 1024, 1 = 512

		uint32_t runout_put, runout_get;

		int caches_reassign;

		struct
		{
			uint32_t dmaput;
			uint32_t dmaget;
		} channels[16];

		struct
		{
			int chanid;
			int push_enabled;
			int runout;
			uint32_t get, put;
			uint32_t pull_ctrl;
			uint32_t pull_state;
			uint32_t ctx[8];
		} caches[2];

		struct
		{
			int subchan;
			uint16_t method;
			uint32_t param;
		} cache0, cache1[64];
	} pfifo;

	struct
	{
		uint32_t intr, intr_en;

		uint64_t time;
		uint32_t alarm;

		uint16_t clock_mul, clock_div;
	} ptimer;

	struct
	{
		uint16_t width;
		int bpp;

		uint32_t config_0;
	} pfb;
	

	struct
	{
		uint32_t debug_0;

		uint32_t intr_0, intr_1;
		uint32_t intr_en_0, intr_en_1;

		uint32_t ctx_switch_a, ctx_control;
		uint32_t ctx_user;
		uint32_t ctx_cache[8];

		uint32_t pattern_mono_color_rgb[2];
		uint32_t pattern_mono_color_a[2];
		uint32_t chroma;

		uint16_t clipx_min, clipx_max, clipy_min, clipy_max;

		int fifo_access;
	} pgraph;
	

	struct
	{
		uint32_t nvpll, mpll, vpll;
	} pramdac;

	pc_timer_t nvtimer;
	pc_timer_t mtimer;

	uint64_t nvtime;
	uint64_t mtime;
} riva128_t;

static uint8_t riva128_in(uint16_t addr, void *p);
static void riva128_out(uint16_t addr, uint8_t val, void *p);
void riva128_do_gpu_work(void *p);

static uint8_t 
riva128_pci_read(int func, int addr, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	// svga_t *svga = &riva128->svga;

	switch (addr) {
	case 0x00: return 0xd2; /*nVidia*/
	case 0x01: return 0x12;

	case 0x02: return 0x18;
	case 0x03: return 0x00;
	
	case 0x04: return riva128->pci_regs[0x04] & 0x37; /*Respond to IO and memory accesses*/
	case 0x05: return riva128->pci_regs[0x05] & 0x01;

	case 0x06: return 0x20;
	case 0x07: return 0x02; /*Fast DEVSEL timing*/

	case 0x08: return 0x00; /*Revision ID*/
	case 0x09: return 0x00; /*Programming interface*/

	case 0x0a: return 0x00; /*Supports VGA interface*/
	case 0x0b: return 0x03;

	case 0x13: return riva128->mmio_base >> 24;

	case 0x17: return riva128->lfb_base >> 24;

	case 0x2c: case 0x2d: case 0x2e: case 0x2f:
		return riva128->pci_regs[addr];

	case 0x30: return (riva128->pci_regs[0x30] & 0x01); /*BIOS ROM address*/
	case 0x31: return 0x00;
	case 0x32: return riva128->pci_regs[0x32];
	case 0x33: return riva128->pci_regs[0x33];

	case 0x3c: return riva128->int_line;
	case 0x3d: return PCI_INTA;

	case 0x3e: return 0x03;
	case 0x3f: return 0x01;
	}

	return 0x00;
}


static void 
riva128_recalc_mapping(riva128_t *riva128)
{
	svga_t *svga = &riva128->svga;
		
	if (!(riva128->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM)) {
	//pclog("PCI mem off\n");
		mem_mapping_disable(&svga->mapping);
		mem_mapping_disable(&riva128->mmio_mapping);
		mem_mapping_disable(&riva128->ramin_mapping);
		mem_mapping_disable(&riva128->linear_mapping);
	return;
	}

	//pclog("PCI mem on\n");
	//pclog("riva128->mmio_base = %08X\n", riva128->mmio_base);
	if (riva128->mmio_base)
		mem_mapping_set_addr(&riva128->mmio_mapping, riva128->mmio_base, 0x1000000);
	else
		mem_mapping_disable(&riva128->mmio_mapping);

	//pclog("riva128->lfb_base = %08X\n", riva128->lfb_base);
	if (riva128->lfb_base) {
	mem_mapping_set_addr(&riva128->linear_mapping, riva128->lfb_base, 0x0400000);
	mem_mapping_set_addr(&riva128->ramin_mapping, riva128->lfb_base + 0x0c00000, 0x0400000);
	} else {
		mem_mapping_disable(&riva128->linear_mapping);
		mem_mapping_disable(&riva128->ramin_mapping);
	}

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
		break;
	}
}


static void 
riva128_pci_write(int func, int addr, uint8_t val, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	switch (addr) {
	case PCI_REG_COMMAND:
		riva128->pci_regs[PCI_REG_COMMAND] = val & 0x27;
		io_removehandler(0x03c0, 0x0020, riva128_in, NULL, NULL, riva128_out, NULL, NULL, riva128);
		if (val & PCI_COMMAND_IO)
			io_sethandler(0x03c0, 0x0020, riva128_in, NULL, NULL, riva128_out, NULL, NULL, riva128);
		riva128_recalc_mapping(riva128);
		break;

	case 0x05:
		riva128->pci_regs[0x05] = val & 0x01;
		break;

	case 0x07:
		riva128->pci_regs[0x07] = (riva128->pci_regs[0x07] & 0x08f) | (val & 0x70);
		break;

	case 0x13:
		riva128->mmio_base = val << 24;
		riva128_recalc_mapping(riva128);
		break;

	case 0x17: 
		riva128->lfb_base = val << 24;
		riva128_recalc_mapping(riva128);
		break;

	case 0x30: case 0x32: case 0x33:
		riva128->pci_regs[addr] = val;
		if (riva128->pci_regs[0x30] & 0x01) {
			uint32_t addr = (riva128->pci_regs[0x32] << 16) | (riva128->pci_regs[0x33] << 24);
			mem_mapping_set_addr(&riva128->bios_rom.mapping, addr, 0x8000);
		} else
			mem_mapping_disable(&riva128->bios_rom.mapping);
		break;

	case 0x3c:
		riva128->int_line = val;
		break;

	case 0x40: case 0x41: case 0x42: case 0x43:
		/* 0x40-0x43 are ways to write to 0x2c-0x2f */
		riva128->pci_regs[0x2c + (addr & 0x03)] = val;
		break;
	}
}

uint8_t
riva128_ramin_read(uint32_t addr, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	addr &= 0x3fffff;

	return svga_read_linear(addr ^ 0x3ffff0, &riva128->svga);
}


uint16_t
riva128_ramin_read_w(uint32_t addr, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	addr &= 0x3fffff;

	return svga_readw_linear(addr ^ 0x3ffff0, &riva128->svga);
}


uint32_t
riva128_ramin_read_l(uint32_t addr, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	addr &= 0x3fffff;

	return svga_readl_linear(addr ^ 0x3ffff0, &riva128->svga);
}


void
riva128_ramin_write(uint32_t addr, uint8_t val, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	addr &= 0x3fffff;

	pclog("[RIVA 128] RAMIN write %08x %02x\n", addr, val);

	return svga_write_linear(addr ^ 0x3ffff0, val, &riva128->svga);
}


void
riva128_ramin_write_w(uint32_t addr, uint16_t val, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	addr &= 0x3fffff;

	pclog("[RIVA 128] RAMIN write %08x %04x\n", addr, val);

	return svga_writew_linear(addr ^ 0x3ffff0, val, &riva128->svga);
}


void
riva128_ramin_write_l(uint32_t addr, uint32_t val, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	addr &= 0x3fffff;

	pclog("[RIVA 128] RAMIN write %08x %08x\n", addr, val);

	return svga_writel_linear(addr ^ 0x3ffff0, val, &riva128->svga);
}

void
riva128_pfifo_reset(void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	
	riva128->pfifo.intr_en = 0;
	riva128->pfifo.ramro = 0x1e00;
	riva128->pfifo.ramro_addr = 0x1e00;
	riva128->pfifo.ramro_size = 512;
}

uint32_t
riva128_pmc_recompute_intr(void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	uint32_t intr = 0;
	if(riva128->pfifo.intr & riva128->pfifo.intr_en) intr |= (1 << 8);
	if((riva128->pgraph.intr_0 & (1 << 8)) && (riva128->pgraph.intr_en_0 & (1 << 8))) intr |= (1 << 24);
	else if(riva128->pgraph.intr_0 & riva128->pgraph.intr_en_0) intr |= (1 << 12);
	if(riva128->ptimer.intr & riva128->ptimer.intr_en) intr |= (1 << 20);
	if(riva128->pmc.intr & (1u << 31)) intr |= (1u << 31);
	
	if((intr & 0x7fffffff) && (riva128->pmc.intr_en & 1)) pci_set_irq(riva128->card, PCI_INTA);
	else if((intr & (1 << 31)) && (riva128->pmc.intr_en & 2)) pci_set_irq(riva128->card, PCI_INTA);
	else pci_clear_irq(riva128->card, PCI_INTA);
	return intr;
}

uint32_t
riva128_pmc_read(uint32_t addr, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	switch(addr)
	{
	case 0x000000:
		return 0x00030100; //ID register.
	case 0x000100:
		return riva128_pmc_recompute_intr(riva128);
	case 0x000140:
		return riva128->pmc.intr_en;
	case 0x000200:
		return riva128->pmc.enable;
	}
	return 0;
}

void
riva128_pmc_write(uint32_t addr, uint32_t val, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	switch(addr)
	{
	case 0x000100:
		riva128->pmc.intr = val & (1u << 31);
		riva128_pmc_recompute_intr(riva128);
		break;
	case 0x000140:
		riva128->pmc.intr_en = val & 3;
		riva128_pmc_recompute_intr(riva128);
		break;
	case 0x000200:
		riva128->pmc.enable = val;
		break;
	}
}

void
riva128_pfifo_interrupt(int num, void *p)
{
	//nv_riva_log("RIVA 128 PTIMER interrupt #%d fired!\n", num);
	riva128_t *riva128 = (riva128_t *)p;

	riva128->pfifo.intr |= (1 << num);

	riva128_pmc_recompute_intr(riva128);
}

//Apparently, PFIFO's CACHE1 uses some sort of Gray code... oh well
uint32_t riva128_pfifo_normal2gray(uint32_t val)
{
	return val ^ (val >> 1);
}

uint32_t riva128_pfifo_gray2normal(uint32_t val)
{
	uint32_t mask = val >> 1;
	while(mask)
	{
		val ^= mask;
		mask >>= 1;
	}
	return val;
}

uint32_t riva128_pfifo_free(void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	uint32_t put = riva128_pfifo_gray2normal(riva128->pfifo.caches[1].put >> 2);
	uint32_t get = riva128_pfifo_gray2normal(riva128->pfifo.caches[1].get >> 2);
	
	uint32_t free = ((31 + get - put) & 0x1f) << 2;
	return free;
}

uint32_t
riva128_pfifo_read(uint32_t addr, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	switch(addr)
	{
	case 0x002080:
		return riva128->pfifo.cache_error;
	case 0x002100:
		return riva128->pfifo.intr;
	case 0x002140:
		return riva128->pfifo.intr_en;
	case 0x002210:
		return riva128->pfifo.ramht;
	case 0x002214:
		return riva128->pfifo.ramfc;
	case 0x002218:
		return riva128->pfifo.ramro;
	case 0x002400:
	{
		uint32_t temp = 0;
		if(riva128->pfifo.runout_put == riva128->pfifo.runout_get) temp |= 0x010;
		if(((riva128->pfifo.runout_put + 8) & (riva128->pfifo.ramro_size - 1)) == riva128->pfifo.runout_get) temp |= 0x100;
		if(riva128->pfifo.runout_put != riva128->pfifo.runout_get) temp |= 0x001;
		return temp;
	}
	case 0x002410:
		return riva128->pfifo.runout_put;
	case 0x002420:
		return riva128->pfifo.runout_get;
	case 0x002500:
		return riva128->pfifo.caches_reassign;
	case 0x003010:
		return riva128->pfifo.caches[0].put;
	case 0x003014:
	{
		uint32_t temp = 0;
		if(riva128->pfifo.caches[0].put == riva128->pfifo.caches[0].get) temp |= 0x010;
		else temp |= 0x100;
		return temp;
	}
	case 0x003040:
		return riva128->pfifo.caches[0].pull_ctrl;
	case 0x003070:
		return riva128->pfifo.caches[0].get;
	case 0x003080:
		return riva128->pfifo.caches[0].ctx[0];
	case 0x003100:
		return riva128->pfifo.cache0.method |
			(riva128->pfifo.cache0.subchan << 13);
	case 0x003104:
		return riva128->pfifo.cache0.param;
	case 0x003200:
		return riva128->pfifo.caches[1].push_enabled;
	case 0x003204:
		return riva128->pfifo.caches[1].chanid;
	case 0x003210:
		return riva128->pfifo.caches[1].put;
	case 0x003214:
	{
		uint32_t temp = 0;
		if(riva128->pfifo.caches[1].put == riva128->pfifo.caches[1].get) temp |= 0x010;
		if(riva128_pfifo_free(riva128) == 0) temp |= 0x100;
		if(riva128->pfifo.runout_put != riva128->pfifo.runout_get) temp |= 0x001;
		return temp;
	}
	case 0x003240:
		return riva128->pfifo.caches[1].pull_ctrl;
	case 0x003250:
		return riva128->pfifo.caches[1].pull_state;
	case 0x003270:
		return riva128->pfifo.caches[1].get;
	case 0x003280:
		return riva128->pfifo.caches[1].ctx[0];
	case 0x003290:
		return riva128->pfifo.caches[1].ctx[1];
	case 0x0032a0:
		return riva128->pfifo.caches[1].ctx[2];
	case 0x0032b0:
		return riva128->pfifo.caches[1].ctx[3];
	case 0x0032c0:
		return riva128->pfifo.caches[1].ctx[4];
	case 0x0032d0:
		return riva128->pfifo.caches[1].ctx[5];
	case 0x0032e0:
		return riva128->pfifo.caches[1].ctx[6];
	case 0x0032f0:
		return riva128->pfifo.caches[1].ctx[7];
	}
	if((addr >= 0x003300) && (addr <= 0x003403))
	{
		if(addr & 4) return riva128->pfifo.cache1[(addr >> 3) & 0x1f].param;
		else
		{
			return riva128->pfifo.cache1[(addr >> 3) & 0x1f].method |
			(riva128->pfifo.cache1[(addr >> 3) & 0x1f].subchan << 13);
		}
	}
	return 0;
}

void
riva128_pfifo_write(uint32_t addr, uint32_t val, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	switch(addr)
	{
	case 0x002100:
	{
		uint32_t tmp = riva128->pfifo.intr & ~val;
		riva128->pfifo.intr = tmp;
		riva128_pmc_recompute_intr(riva128);
		if(!riva128->pfifo.intr) riva128->pfifo.cache_error = 0;
		break;
	}
	case 0x002140:
		riva128->pfifo.intr_en = val & 0x11111;
		riva128_pmc_recompute_intr(riva128);
		break;
	case 0x002210:
		riva128->pfifo.ramht = val & 0x3f000;
		riva128->pfifo.ramht_addr = val & 0xf000;
		switch(val & 0x30000)
		{
			case 0x00000: riva128->pfifo.ramht_size = 4096; break;
			case 0x10000: riva128->pfifo.ramht_size = 8192; break;
			case 0x20000: riva128->pfifo.ramht_size = 16384; break;
			case 0x30000: riva128->pfifo.ramht_size = 32768; break;
		}
		break;
	case 0x002214:
		riva128->pfifo.ramfc = riva128->pfifo.ramfc_addr = val & 0xfe00;
		break;
	case 0x002218:
		riva128->pfifo.ramro = val & 0x1fe00;
		riva128->pfifo.ramro_addr = val & 0xfe00;
		if(val & 0x10000) riva128->pfifo.ramro_size = 8192;
		else riva128->pfifo.ramro_size = 512;
		break;
	case 0x002410:	
		riva128->pfifo.runout_put = val & 0x1ff8;
		break;
	case 0x002420:
		riva128->pfifo.runout_get = val & 0x1ff8;
		break;
	case 0x002500:
		riva128->pfifo.caches_reassign = val & 1;
		break;
	case 0x003004:
		riva128->pfifo.caches[0].chanid = val;
		break;
	case 0x003010:
		riva128->pfifo.caches[0].put = val;
		break;
	case 0x003040:
		riva128->pfifo.caches[0].pull_ctrl = val & 0x1;
		break;
	case 0x003070:
		riva128->pfifo.caches[0].get = val;
		break;
	case 0x003080:
		riva128->pfifo.caches[0].ctx[0] = val & 0xffffff;
		break;
	case 0x003100:
		riva128->pfifo.cache0.method = val & 0x1ffc;
		riva128->pfifo.cache0.subchan = val >> 13;
		break;
	case 0x003104:
		riva128->pfifo.cache0.param = val;
		break;
	case 0x003200:
		riva128->pfifo.caches[1].push_enabled = val & 1;
		break;
	case 0x003210:
		riva128->pfifo.caches[1].put = val & 0x7c;
		break;
	case 0x003240:
		riva128->pfifo.caches[1].pull_ctrl = val & 0x1;
		break;
	case 0x003250:
		riva128->pfifo.caches[1].pull_state = val & 0x10;
		break;
	case 0x003270:
		riva128->pfifo.caches[1].get = val & 0x7c;
		break;
	case 0x003280:
		riva128->pfifo.caches[1].ctx[0] = val;
		riva128->pfifo.caches[1].pull_state &= ~0x10;
		break;
	case 0x003290:
		riva128->pfifo.caches[1].ctx[1] = val;
		riva128->pfifo.caches[1].pull_state &= ~0x10;
		break;
	case 0x0032a0:
		riva128->pfifo.caches[1].ctx[2] = val;
		riva128->pfifo.caches[1].pull_state &= ~0x10;
		break;
	case 0x0032b0:
		riva128->pfifo.caches[1].ctx[3] = val;
		riva128->pfifo.caches[1].pull_state &= ~0x10;
		break;
	case 0x0032c0:
		riva128->pfifo.caches[1].ctx[4] = val;
		riva128->pfifo.caches[1].pull_state &= ~0x10;
		break;
	case 0x0032d0:
		riva128->pfifo.caches[1].ctx[5] = val;
		riva128->pfifo.caches[1].pull_state &= ~0x10;
		break;
	case 0x0032e0:
		riva128->pfifo.caches[1].ctx[6] = val;
		riva128->pfifo.caches[1].pull_state &= ~0x10;
		break;
	case 0x0032f0:
		riva128->pfifo.caches[1].ctx[7] = val;
		riva128->pfifo.caches[1].pull_state &= ~0x10;
		break;
	}
	if((addr >= 0x003300) && (addr <= 0x003403))
	{
		if(addr & 4) riva128->pfifo.cache1[(addr >> 3) & 0x1f].param = val;
		else
		{
			riva128->pfifo.cache1[(addr >> 3) & 0x1f].method = val & 0x1ffc;
			riva128->pfifo.cache1[(addr >> 3) & 0x1f].subchan = val >> 13;
		}
	}
}

void
riva128_ptimer_interrupt(int num, void *p)
{
	//nv_riva_log("RIVA 128 PTIMER interrupt #%d fired!\n", num);
	riva128_t *riva128 = (riva128_t *)p;

	riva128->ptimer.intr |= (1 << num);

	riva128_pmc_recompute_intr(riva128);
}

uint32_t
riva128_ptimer_read(uint32_t addr, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	switch(addr)
	{
	case 0x009100:
		return riva128->ptimer.intr;
	case 0x009140:
		return riva128->ptimer.intr_en;
	case 0x009200:
		return riva128->ptimer.clock_div;
	case 0x009210:
		return riva128->ptimer.clock_mul;
	case 0x009400:
		return riva128->ptimer.time & 0xffffffffULL;
	case 0x009410:
		return riva128->ptimer.time >> 32;
	case 0x009420:
		return riva128->ptimer.alarm;
	}
	return 0;
}

void
riva128_ptimer_write(uint32_t addr, uint32_t val, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	switch(addr)
	{
	case 0x009100:
		riva128->ptimer.intr &= ~val;
		riva128_pmc_recompute_intr(riva128);
		break;
	case 0x009140:
		riva128->ptimer.intr_en = val & 1;
		riva128_pmc_recompute_intr(riva128);
		break;
	case 0x009200:
		if(!(uint16_t)val) val = 1;
		riva128->ptimer.clock_div = (uint16_t)val;
		break;
	case 0x009210:
		riva128->ptimer.clock_mul = (uint16_t)val;
		break;
	case 0x009400:
		riva128->ptimer.time &= 0x0fffffff00000000ULL;
		riva128->ptimer.time |= val & 0xffffffe0;
		break;
	case 0x009410:
		riva128->ptimer.time &= 0xffffffe0;
		riva128->ptimer.time |= (uint64_t)(val & 0x0fffffff) << 32;
		break;
	case 0x009420:
		riva128->ptimer.alarm = val & 0xffffffe0;
		break;
	}
}

uint32_t
riva128_pfb_read(uint32_t addr, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	uint32_t result = 0;
	switch(addr)
	{
		case 0x100000:
		{
			switch(riva128->vram_size)
			{
				case 1 << 20: result = 0; break;
				case 2 << 20: result = 1; break;
				case 4 << 20: result = 2; break;
			}
			return result | 0x0c;
		}
		case 0x100200:
			return riva128->pfb.config_0;
	}
	return 0;
}

void
riva128_pfb_write(uint32_t addr, uint32_t val, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	switch(addr)
	{
		case 0x100200:
			riva128->pfb.config_0 = (val & 0x33f) | 0x1000;
			riva128->pfb.width = (val & 0x3f) << 5;
			switch((val >> 8) & 3)
			{
				case 1:
					riva128->pfb.bpp = 8;
					break;
				case 2:
					riva128->pfb.bpp = 16;
					break;
				case 3:
					riva128->pfb.bpp = 32;
					break;
			}
			break;
	}
}

void
riva128_pgraph_interrupt(int num, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	riva128->pgraph.intr_0 |= (1 << num);

	riva128_pmc_recompute_intr(riva128);
}

void
riva128_pgraph_invalid_interrupt(int num, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	riva128->pgraph.intr_1 |= (1 << num);
	/*if(riva128->pgraph.intr_en_0 & 1)*/ riva128->pgraph.intr_0 |= (1 << 0);

	riva128_pmc_recompute_intr(riva128);
}

uint32_t
riva128_pgraph_read(uint32_t addr, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	switch(addr)
	{
		case 0x400080:
			return riva128->pgraph.debug_0;
		case 0x400100:
			return riva128->pgraph.intr_0;
		case 0x400104:
			return riva128->pgraph.intr_1;
		case 0x400140:
			return riva128->pgraph.intr_en_0;
		case 0x400144:
			return riva128->pgraph.intr_en_1;
		case 0x400180:
			return riva128->pgraph.ctx_switch_a;
		case 0x400194:
			return riva128->pgraph.ctx_user;
		case 0x40062c:
			return riva128->pgraph.chroma;
		case 0x4006a4:
			return riva128->pgraph.fifo_access;
	}
	return 0;
}

void
riva128_pgraph_write(uint32_t addr, uint32_t val, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	switch(addr)
	{
		case 0x400080:
			riva128->pgraph.debug_0 = val;
			break;
		case 0x400100:
			riva128->pgraph.intr_0 &= ~val;
			riva128_pmc_recompute_intr(riva128);
			break;
		case 0x400104:
			riva128->pgraph.intr_1 &= ~val;
			//if(!riva128->pgraph.intr_1) riva128->pgraph.intr_0 &= ~1;
			riva128_pmc_recompute_intr(riva128);
			break;
		case 0x400140:
			riva128->pgraph.intr_en_0 = val & 0x11111111;
			riva128_pmc_recompute_intr(riva128);
			break;
		case 0x400144:
			riva128->pgraph.intr_en_1 = val & 0x00011111;
			riva128_pmc_recompute_intr(riva128);
			break;
		case 0x400180:
			riva128->pgraph.ctx_switch_a = val & 0x3ff3f71f;
			break;
		case 0x400194:
			riva128->pgraph.ctx_user = val;
			break;
		case 0x40062c:
			riva128->pgraph.chroma = val;
			break;
		case 0x4006a4:
			riva128->pgraph.fifo_access = val & 1;
			break;
	}
}

uint32_t
riva128_pramdac_read(uint32_t addr, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	switch(addr)
	{
		case 0x680500:
			return riva128->pramdac.nvpll;
		case 0x680504:
			return riva128->pramdac.mpll;
		case 0x680508:
			return riva128->pramdac.vpll;
	}
	return 0;
}

void
riva128_pramdac_write(uint32_t addr, uint32_t val, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	switch(addr)
	{
		case 0x680500:
			riva128->pramdac.nvpll = val;
			break;
		case 0x680504:
			riva128->pramdac.mpll = val;
			break;
		case 0x680508:
			riva128->pramdac.vpll = val;
			break;
	}
	svga_recalctimings(&riva128->svga);
}

uint8_t
riva128_ramht_hash(uint32_t handle, uint8_t chanid)
{
	return (handle ^ (handle >> 8) ^ (handle >> 16) ^ (handle >> 24) ^ chanid) & 0xff;
}

int
riva128_ramht_lookup(uint32_t handle, int cache_num, uint8_t chanid, int subchanid, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	int bucket_entries = 2;

	switch(riva128->pfifo.ramht_size)
	{
		case 4096: bucket_entries = 2; break;
		case 8192: bucket_entries = 4; break;
		case 16384: bucket_entries = 8; break;
		case 32768: bucket_entries = 16; break;
	}

	uint32_t ramht_addr = riva128->pfifo.ramht_addr +
	((uint32_t)riva128_ramht_hash(handle, chanid) * bucket_entries * 8);

	int found = 0;

	for(int i = 0; i < bucket_entries; i++)
	{
		uint32_t handle_check = riva128_ramin_read_l(ramht_addr, riva128);
		uint8_t chanid_check = riva128_ramin_read(ramht_addr + 7, riva128);
		if(handle_check == handle/* && chanid_check == chanid*/)
		{
			found = 1;
			break;
		}
		ramht_addr += 8;
	}

	if(!found)
	{
		pclog("[RIVA 128] Cache error: Handle not found!\n");
		riva128->pfifo.caches[cache_num].pull_ctrl |= 0x010;
		riva128->pfifo.caches[cache_num].pull_ctrl &= ~1;
		riva128->pfifo.cache_error |= 0x11;
		riva128_pfifo_interrupt(0, riva128);
		return 1;
	}
	else
	{
		uint32_t ctx = riva128_ramin_read_l(ramht_addr + 4, riva128);
		if(cache_num) riva128->pfifo.caches[1].ctx[subchanid] = ctx & 0xffffff;
		else riva128->pfifo.caches[0].ctx[0] = ctx & 0xffffff;
		pclog("[RIVA 128] CTX %08x\n", ctx & 0xffffff);
		if(!(ctx & 0x800000))
		{
			pclog("[RIVA 128] Cache error: Software object!\n");
			riva128->pfifo.caches[cache_num].pull_ctrl |= 0x100;
			riva128->pfifo.caches[cache_num].pull_ctrl &= ~1;
			riva128->pfifo.cache_error |= 0x11;
			riva128_pfifo_interrupt(0, riva128);
			return 1;
		}
		else return 0;
	}
	
}

typedef struct riva128_pgraph_color
{
	uint16_t r, g, b;
	uint8_t a, i;
	uint16_t i16;
	enum
	{
		RIVA128_COLOR_MODE_RGB5,
		RIVA128_COLOR_MODE_RGB8,
		RIVA128_COLOR_MODE_RGB10,
		RIVA128_COLOR_MODE_Y8,
		RIVA128_COLOR_MODE_Y16,
	} mode;
} riva128_pgraph_color_t;

riva128_pgraph_color_t
riva128_pgraph_expand_color(uint32_t graphobj0, uint32_t color, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	riva128_pgraph_color_t color_ret;

	int format = graphobj0 & 0x7;
	int fa = (graphobj0 >> 3) & 1;

	switch(format)
	{
		case 0:
			//X16A1R5G5B5
			color_ret.a = ((color >> 15) & 1) * 0xff;
			color_ret.r = ((color >> 10) & 0x1f) * 0x20;
			color_ret.g = ((color >> 5) & 0x1f) * 0x20;
			color_ret.b = ((color >> 0) & 0x1f) * 0x20;
			color_ret.mode = RIVA128_COLOR_MODE_RGB5;
			break;
		case 1:
			//A8R8G8B8
			color_ret.a = color >> 24;
			color_ret.r = (((color >> 16) & 0xff) * 0x100) >> 6;
			color_ret.g = (((color >> 8) & 0xff) * 0x100) >> 6;
			color_ret.b = (((color >> 0) & 0xff) * 0x100) >> 6;
			color_ret.mode = RIVA128_COLOR_MODE_RGB8;
			break;
		case 2:
			//A2R10G10B10
			color_ret.a = (color >> 30) * 0x55;
			color_ret.r = (color >> 20) & 0x3ff;
			color_ret.g = (color >> 10) & 0x3ff;
			color_ret.b = (color >> 0) & 0x3ff;
			color_ret.mode = RIVA128_COLOR_MODE_RGB10;
			break;
		case 3:
			//X16A8Y8
			color_ret.a = (color >> 8) & 0xff;
			color_ret.r = color_ret.g = color_ret.b = ((color & 0xff) * 0x100) >> 6;
			color_ret.mode = RIVA128_COLOR_MODE_Y8;
			break;
		case 4:
			//A16Y16
			color_ret.a = (color >> 16) & 0xffff;
			color_ret.r = color_ret.g = color_ret.b = (color & 0xffff) >> 6;
			color_ret.mode = RIVA128_COLOR_MODE_Y16;
			break;
	}
	color_ret.i = color & 0xff;
	color_ret.i16 = color & 0xffff;
	if(!fa) color_ret.a = 0xff;

	return color_ret;
}

uint32_t
riva128_pgraph_to_a1r10g10b10(riva128_pgraph_color_t color)
{
	return !!color.a << 30 | color.r << 20 | color.g << 10 | color.b;
}

void
riva128_pgraph_write_pixel(uint16_t x, uint16_t y, uint32_t color, uint8_t a, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	svga_t *svga = &riva128->svga;

	int bytes_per_pixel = riva128->pfb.bpp >> 3;

	uint32_t addr = (x + (riva128->pfb.width) + y) * bytes_per_pixel;

	switch(bytes_per_pixel)
	{
		case 1:
			svga->vram[(addr & riva128->vram_mask)] = color & 0xff;
			break;
		case 2:
			svga->vram[(addr & riva128->vram_mask) + 0] = color & 0xff;
			svga->vram[(addr & riva128->vram_mask) + 1] = (color >> 8) & 0xff;
			break;
		case 4:
			svga->vram[(addr & riva128->vram_mask) + 0] = color & 0xff;
			svga->vram[(addr & riva128->vram_mask) + 1] = (color >> 8) & 0xff;
			svga->vram[(addr & riva128->vram_mask) + 2] = (color >> 16) & 0xff;
			svga->vram[(addr & riva128->vram_mask) + 3] = (color >> 24) & 0xff;
			break;
	}

	svga->changedvram[(addr & riva128->vram_mask) >> 12] = changeframecount;
}

void
riva128_pgraph_execute_command(uint16_t method, uint32_t param, uint32_t ctx,
uint32_t graphobj0, uint32_t graphobj1, uint32_t graphobj2, uint32_t graphobj3, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	uint8_t objclass = (ctx >> 16) & 0x1f;

	pclog("[RIVA 128] PGRAPH executing objclass %02x method %04x param %08x\n", objclass, method, param);

	switch(objclass)
	{
		case 0x03:
		{
			switch(method)
			{
				case 0x304:
				{
					riva128->pgraph.chroma = riva128_pgraph_to_a1r10g10b10(riva128_pgraph_expand_color(graphobj0, param, riva128));
					break;
				}
				default:
				{
					riva128_pgraph_invalid_interrupt(0, riva128);
					break;
				}
			}
			break;
		}
		case 0x06:
		{
			switch(method)
			{
				case 0x310:
				{
					riva128_pgraph_color_t color = riva128_pgraph_expand_color(graphobj0, param, riva128);
					riva128->pgraph.pattern_mono_color_rgb[0] = (color.r << 20) | (color.g << 10) | color.b;
					riva128->pgraph.pattern_mono_color_a[0] = color.a;
					break;
				}
				case 0x314:
				{
					riva128_pgraph_color_t color = riva128_pgraph_expand_color(graphobj0, param, riva128);
					riva128->pgraph.pattern_mono_color_rgb[1] = (color.r << 20) | (color.g << 10) | color.b;
					riva128->pgraph.pattern_mono_color_a[1] = color.a;
					break;
				}
			}
			break;
		}
	}
}

void
riva128_pgraph_command_submit(uint16_t method, uint8_t chanid, int subchanid,
uint32_t param, uint32_t ctx, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	if(!riva128->pgraph.fifo_access) return;
	
	uint8_t current_chanid = (riva128->pgraph.ctx_user >> 24) & 0x7f;
	if(chanid != current_chanid)
	{
		//PGRAPH context switch.
		riva128_pgraph_interrupt(4, riva128);
	}

	riva128->pgraph.ctx_user = ctx;

	uint16_t instance_addr = ctx & 0xffff;

	uint32_t graphobj[4];
	graphobj[0] = riva128_ramin_read_l((instance_addr << 4), riva128);
	graphobj[1] = riva128_ramin_read_l((instance_addr << 4) + 4, riva128);
	graphobj[2] = riva128_ramin_read_l((instance_addr << 4) + 8, riva128);
	graphobj[3] = riva128_ramin_read_l((instance_addr << 4) + 12, riva128);

	riva128_pgraph_execute_command(method, param, ctx, graphobj[0],
	graphobj[1], graphobj[2], graphobj[3], riva128);
}

void
riva128_do_cache0_puller(void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	if((riva128->pfifo.caches[0].pull_ctrl & 1) &&
	(riva128->pfifo.caches[0].put != riva128->pfifo.caches[0].get))
	{
		uint16_t method = riva128->pfifo.cache0.method;
		uint32_t param = riva128->pfifo.cache0.param;
		uint8_t chanid = riva128->pfifo.caches[0].chanid;
		int subchanid = riva128->pfifo.cache0.subchan;
		pclog("[RIVA 128] CACHE0 puller method %04x param %08x channel %02x subchannel %x\n",
		method, param, chanid, subchanid);
		if(method == 0)
		{
			int error = riva128_ramht_lookup(param, 0, chanid, 0, riva128);
			if(error) return;

			riva128->pfifo.caches[0].get ^= 4;
			
			uint32_t ctx = riva128->pfifo.caches[0].ctx[0];
			//TODO: forward to PGRAPH.
			pclog("[RIVA 128] CTX = %08x\n", ctx);
			riva128_pgraph_command_submit(method, chanid, subchanid, param, ctx, riva128);
		}
		else
		{
			uint32_t ctx = riva128->pfifo.caches[0].ctx[0];
			if(!(ctx & 0x800000))
			{
				pclog("[RIVA 128] Cache error: Software method!\n");
				riva128->pfifo.caches[0].pull_ctrl |= 0x100;
				riva128->pfifo.caches[0].pull_ctrl &= ~1;
				riva128->pfifo.cache_error |= 0x11;
				riva128_pfifo_interrupt(0, riva128);
				return;
			}
			else
			{
				riva128->pfifo.caches[0].get ^= 4;
				//TODO: forward to PGRAPH.
				pclog("[RIVA 128] CTX = %08x\n", ctx);
				riva128_pgraph_command_submit(method, chanid, subchanid, param, ctx, riva128);
			}
		}
	}
}

void
riva128_do_cache1_puller(void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	if((riva128->pfifo.caches[1].pull_ctrl & 1) &&
	(riva128->pfifo.caches[1].put != riva128->pfifo.caches[1].get))
	{
		uint16_t method = riva128->pfifo.cache1[riva128->pfifo.caches[1].get >> 2].method;
		uint32_t param = riva128->pfifo.cache1[riva128->pfifo.caches[1].get >> 2].param;
		uint8_t chanid = riva128->pfifo.caches[1].chanid;
		int subchanid = riva128->pfifo.cache1[riva128->pfifo.caches[1].get >> 2].subchan;
		pclog("[RIVA 128] CACHE1 puller method %04x param %08x channel %02x subchannel %x\n",
		method, param, chanid, subchanid);
		if(method == 0)
		{
			int error = riva128_ramht_lookup(param, 1, chanid, subchanid, riva128);
			if(error) return;
			uint32_t next_get = riva128_pfifo_gray2normal(riva128->pfifo.caches[1].get >> 2);
			next_get++;
			next_get &= 31;
			riva128->pfifo.caches[1].get = riva128_pfifo_normal2gray(next_get) << 2;

			uint32_t ctx = riva128->pfifo.caches[1].ctx[subchanid];
			
			//TODO: forward to PGRAPH.
			pclog("[RIVA 128] CTX = %08x\n", ctx);
			riva128_pgraph_command_submit(method, chanid, subchanid, param, ctx, riva128);
		}
		else
		{
			uint32_t ctx = riva128->pfifo.caches[1].ctx[subchanid];
			if(!(ctx & 0x800000))
			{
				pclog("[RIVA 128] Cache error: Software method!\n");
				riva128->pfifo.caches[1].pull_ctrl |= 0x100;
				riva128->pfifo.caches[1].pull_ctrl &= ~1;
				riva128->pfifo.cache_error |= 0x11;
				riva128_pfifo_interrupt(0, riva128);
				return;
			}
			else
			{
				uint32_t next_get = riva128_pfifo_gray2normal(riva128->pfifo.caches[1].get >> 2);
				next_get++;
				next_get &= 31;
				riva128->pfifo.caches[1].get = riva128_pfifo_normal2gray(next_get) << 2;
				//TODO: forward to PGRAPH.
				pclog("[RIVA 128] CTX = %08x\n", ctx);
				riva128_pgraph_command_submit(method, chanid, subchanid, param, ctx, riva128);
			}
		}
	}
}

void
riva128_do_gpu_work(void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	riva128_do_cache0_puller(riva128);
	riva128_do_cache1_puller(riva128);
}

uint32_t
riva128_user_read(uint32_t addr, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	int chanid = (addr >> 16) & 0xf;
	int subchanid = (addr >> 13) & 0x7;
	int offset = addr & 0x1ffc;

	if(offset == 0x0010) return riva128_pfifo_free(riva128);
	return 0;
}

void
riva128_user_write(uint32_t addr, uint32_t val, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	int chanid = (addr >> 16) & 0x7f;
	int subchanid = (addr >> 13) & 0x7;
	int offset = addr & 0x1ffc;
	uint32_t err = (addr & 0x7ffffc) | 0x800000; //Set bit 23 because this is a write.
	int ranout = 0;

	if(offset == 0x0010) ranout = 1; //REASON is 0 so don't modify err.
	
	else if(!riva128->pfifo.caches[1].push_enabled)
	{
		ranout = 1;
		err |= 1 << 28;
	}

	else if(riva128->pfifo.runout_get != riva128->pfifo.runout_put)
	{
		ranout = 1;
		err |= 2 << 28;
	}

	else if(riva128_pfifo_free(riva128) == 0)
	{
		ranout = 1;
		err |= 3 << 28;
	}

	else if((offset < 0x100) && (offset != 0))
	{
		ranout = 1;
		err |= 5 << 28;
	}

	else if(chanid != riva128->pfifo.caches[1].chanid)
	{
		if(!riva128->pfifo.caches_reassign ||
		(riva128->pfifo.caches[1].put != riva128->pfifo.caches[1].get))
		{
			ranout = 1;
			err |= 2 << 28;
		}
		//TODO: channel switch.
		pclog("[RIVA 128] PFIFO CHANNEL SWITCH\n");
	}

	if(ranout)
	{
		pclog("[RIVA 128] Command rejected to RAMRO! error %08x value %08x\n", err, val);
		riva128_ramin_write_l(riva128->pfifo.ramro_addr + riva128->pfifo.runout_put, err, riva128);
		riva128_ramin_write_l(riva128->pfifo.ramro_addr + riva128->pfifo.runout_put + 4, val, riva128);
		riva128->pfifo.runout_put += 8;
		riva128->pfifo.runout_put &= (riva128->pfifo.ramro_size == 8192) ? 0x1ff8 : 0x1f8;
		riva128_pfifo_interrupt(4, riva128);
		if(riva128->pfifo.runout_put == riva128->pfifo.runout_get)
		{
			riva128_pfifo_interrupt(8, riva128);
		}
		return;
	}

	//Command passed pusher tests, send it off to CACHE1.
	riva128->pfifo.cache1[riva128->pfifo.caches[1].put >> 2].subchan = subchanid;
	riva128->pfifo.cache1[riva128->pfifo.caches[1].put >> 2].method = offset;
	riva128->pfifo.cache1[riva128->pfifo.caches[1].put >> 2].param = val;

	uint32_t put_normal = riva128_pfifo_gray2normal(riva128->pfifo.caches[1].put >> 2);
	put_normal++;
	put_normal &= 0x1f;
	riva128->pfifo.caches[1].put = riva128_pfifo_normal2gray(put_normal) << 2;
}

void
riva128_ptimer_tick(void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	//pclog("[RIVA 128] PTIMER tick! mul %04x div %04x\n", riva128->ptimer.clock_mul, riva128->ptimer.clock_div);

	double time = ((double)riva128->ptimer.clock_mul * 100.0f) / (double)riva128->ptimer.clock_div;
	uint32_t tmp;
	int alarm_check;

	//if(cs == 0x0008 && !riva128->pgraph.beta) nv_riva_log("RIVA 128 PTIMER time elapsed %f alarm %08x, time_low %08x\n", time, riva128->ptimer.alarm, riva128->ptimer.time & 0xffffffff);

	tmp = riva128->ptimer.time;
	riva128->ptimer.time += (uint64_t)time;

	alarm_check = (riva128->ptimer.alarm - tmp) <= ((uint32_t)riva128->ptimer.time - tmp) ? 1 : 0;

	//pclog("[RIVA 128] Timer %08x %016llx %08x %d\n", riva128->ptimer.alarm, riva128->ptimer.time, tmp, alarm_check);

	if(alarm_check)
	{
		//pclog("[RIVA 128] PTIMER ALARM interrupt fired!\n");
		riva128_ptimer_interrupt(0, riva128);
	}
}

void
riva128_nvclk_poll(void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	riva128_do_gpu_work(riva128);
	timer_advance_u64(&riva128->nvtimer, riva128->nvtime);
}

void
riva128_mclk_poll(void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	if(riva128->pmc.enable & (1 << 16)) riva128_ptimer_tick(riva128);

	timer_advance_u64(&riva128->mtimer, riva128->mtime);
}

uint32_t
riva128_mmio_read_l(uint32_t addr, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	addr &= 0xffffff;

	uint32_t ret = 0;

	switch(addr) {
	case 0x6013b4: case 0x6013b5:
	case 0x6013d4: case 0x6013d5:
	case 0x6013da:
	case 0x0c03c2: case 0x0c03c3: case 0x0c03c4: case 0x0c03c5: case 0x0c03cc:
	case 0x6813c6: case 0x6813c7: case 0x6813c8: case 0x6813c9: case 0x6813ca: case 0x6813cb:
		ret = (riva128_in((addr+0) & 0x3ff,p) << 0) | (riva128_in((addr+1) & 0x3ff,p) << 8) | (riva128_in((addr+2) & 0x3ff,p) << 16) | (riva128_in((addr+3) & 0x3ff,p) << 24);
		break;
	}

	addr &= 0xfffffc;

	if ((addr >= 0x000000) && (addr <= 0x000fff)) ret = riva128_pmc_read(addr, riva128);
	if ((addr >= 0x002000) && (addr <= 0x003fff)) ret = riva128_pfifo_read(addr, riva128);
	if ((addr >= 0x009000) && (addr <= 0x009fff)) ret = riva128_ptimer_read(addr, riva128);
	if ((addr >= 0x100000) && (addr <= 0x100fff)) ret = riva128_pfb_read(addr, riva128);
	if ((addr >= 0x400000) && (addr <= 0x400fff)) ret = riva128_pgraph_read(addr, riva128);
	if ((addr >= 0x680000) && (addr <= 0x680fff)) ret = riva128_pramdac_read(addr, riva128);
	if ((addr >= 0x110000) && (addr <= 0x11ffff)) ret = ((uint32_t *) riva128->bios_rom.rom)[(addr & riva128->bios_rom.mask) >> 2];
	if ((addr >= 0x800000)) ret = riva128_user_read(addr, riva128);

	if ((addr >= 0x1800) && (addr <= 0x18ff))
		ret = (riva128_pci_read(0,(addr+0) & 0xff,p) << 0) | (riva128_pci_read(0,(addr+1) & 0xff,p) << 8) | (riva128_pci_read(0,(addr+2) & 0xff,p) << 16) | (riva128_pci_read(0,(addr+3) & 0xff,p) << 24);

	pclog("[RIVA 128] MMIO read %08x returns value %08x\n", addr, ret);

	riva128_do_gpu_work(riva128);

	return ret;
}


uint8_t
riva128_mmio_read(uint32_t addr, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	addr &= 0xffffff;

	if ((addr >= 0x110000) && (addr <= 0x11ffff)) return riva128->bios_rom.rom[addr & riva128->bios_rom.mask];

	if ((addr >= 0x1800) && (addr <= 0x18ff))
	return riva128_pci_read(0,addr & 0xff,p);

	switch(addr) {
	case 0x6013b4: case 0x6013b5:
	case 0x6013d4: case 0x6013d5:
	case 0x6013da:
	case 0x0c03c2: case 0x0c03c3: case 0x0c03c4: case 0x0c03c5: case 0x0c03cc:
	case 0x6813c6: case 0x6813c7: case 0x6813c8: case 0x6813c9: case 0x6813ca: case 0x6813cb:
		return riva128_in(addr & 0x3ff,p);
		break;
	}

	return (riva128_mmio_read_l(addr & 0xffffff, riva128) >> ((addr & 3) << 3)) & 0xff;
}


uint16_t
riva128_mmio_read_w(uint32_t addr, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	addr &= 0xffffff;

	if ((addr >= 0x110000) && (addr <= 0x11ffff)) return ((uint16_t *) riva128->bios_rom.rom)[(addr & riva128->bios_rom.mask) >> 1];

	if ((addr >= 0x1800) && (addr <= 0x18ff))
	return (riva128_pci_read(0,(addr+0) & 0xff,p) << 0) | (riva128_pci_read(0,(addr+1) & 0xff,p) << 8);

	switch(addr) {
	case 0x6013b4: case 0x6013b5:
	case 0x6013d4: case 0x6013d5:
	case 0x6013da:
	case 0x0c03c2: case 0x0c03c3: case 0x0c03c4: case 0x0c03c5: case 0x0c03cc:
	case 0x6813c6: case 0x6813c7: case 0x6813c8: case 0x6813c9: case 0x6813ca: case 0x6813cb:
		return (riva128_in((addr+0) & 0x3ff,p) << 0) | (riva128_in((addr+1) & 0x3ff,p) << 8);
		break;
	}

   return (riva128_mmio_read_l(addr & 0xffffff, riva128) >> ((addr & 3) << 3)) & 0xffff;
}


void
riva128_mmio_write_l(uint32_t addr, uint32_t val, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	addr &= 0xffffff;

	pclog("[RIVA 128] MMIO write %08x %08x\n", addr, val);

	if ((addr >= 0x1800) && (addr <= 0x18ff)) {
	riva128_pci_write(0, addr & 0xff, val & 0xff, p);
	riva128_pci_write(0, (addr+1) & 0xff, (val>>8) & 0xff, p);
	riva128_pci_write(0, (addr+2) & 0xff, (val>>16) & 0xff, p);
	riva128_pci_write(0, (addr+3) & 0xff, (val>>24) & 0xff, p);
	return;
	}

	if((addr >= 0x000000) && (addr <= 0x000fff)) riva128_pmc_write(addr, val, riva128);
	if((addr >= 0x002000) && (addr <= 0x003fff)) riva128_pfifo_write(addr, val, riva128);
	if((addr >= 0x009000) && (addr <= 0x009fff)) riva128_ptimer_write(addr, val, riva128);
	if((addr >= 0x100000) && (addr <= 0x100fff)) riva128_pfb_write(addr, val, riva128);
	if((addr >= 0x400000) && (addr <= 0x400fff)) riva128_pgraph_write(addr, val, riva128);
	if((addr >= 0x680000) && (addr <= 0x680fff)) riva128_pramdac_write(addr, val, riva128);
	if(addr >= 0x800000) riva128_user_write(addr, val, riva128);

	riva128_do_gpu_work(riva128);

	switch(addr) {
	case 0x6013b4: case 0x6013b5:
	case 0x6013d4: case 0x6013d5:
	case 0x6013da:
	case 0x0c03c2: case 0x0c03c3: case 0x0c03c4: case 0x0c03c5: case 0x0c03cc:
	case 0x6813c6: case 0x6813c7: case 0x6813c8: case 0x6813c9: case 0x6813ca: case 0x6813cb:
		riva128_out(addr & 0xfff, val & 0xff, p);
		riva128_out((addr+1) & 0xfff, (val>>8) & 0xff, p);
		riva128_out((addr+2) & 0xfff, (val>>16) & 0xff, p);
		riva128_out((addr+3) & 0xfff, (val>>24) & 0xff, p);
		break;
	}
}


void
riva128_mmio_write(uint32_t addr, uint8_t val, void *p)
{
	uint32_t tmp;

	addr &= 0xffffff;

	switch(addr) {
	case 0x6013b4: case 0x6013b5:
	case 0x6013d4: case 0x6013d5:
	case 0x6013da:
	case 0x0c03c2: case 0x0c03c3: case 0x0c03c4: case 0x0c03c5: case 0x0c03cc:
	case 0x6813c6: case 0x6813c7: case 0x6813c8: case 0x6813c9: case 0x6813ca: case 0x6813cb:
		riva128_out(addr & 0xfff, val & 0xff, p);
		return;
	}

	tmp = riva128_mmio_read_l(addr,p);
	tmp &= ~(0xff << ((addr & 3) << 3));
	tmp |= val << ((addr & 3) << 3);
	riva128_mmio_write_l(addr, tmp, p);
	if ((addr >= 0x1800) && (addr <= 0x18ff)) riva128_pci_write(0, addr & 0xff, val, p);
}


void
riva128_mmio_write_w(uint32_t addr, uint16_t val, void *p)
{
	uint32_t tmp;

	if ((addr >= 0x1800) && (addr <= 0x18ff)) {
	riva128_pci_write(0, addr & 0xff, val & 0xff, p);
	riva128_pci_write(0, (addr+1) & 0xff, (val>>8) & 0xff, p);
	return;
	}

	addr &= 0xffffff;
	tmp = riva128_mmio_read_l(addr,p);
	tmp &= ~(0xffff << ((addr & 3) << 3));
	tmp |= val << ((addr & 3) << 3);
	riva128_mmio_write_l(addr, tmp, p);
}

uint8_t
riva128_rma_in(uint16_t addr, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	svga_t *svga = &riva128->svga;
	uint8_t ret = 0;

	addr &= 0xff;

	// nv_riva_log("RIVA 128 RMA read %04X %04X:%08X\n", addr, CS, cpu_state.pc);

	switch(addr) {
	case 0x00:
		ret = 0x65;
		break;
	case 0x01:
		ret = 0xd0;
		break;
	case 0x02:
		ret = 0x16;
		break;
	case 0x03:
		ret = 0x2b;
		break;
	case 0x08:
	case 0x09:
	case 0x0a:
	case 0x0b:
		if (riva128->rma.rma_dst_addr < 0x1000000)
			ret = riva128_mmio_read((riva128->rma.rma_dst_addr + (addr & 3)) & 0xffffff, riva128);
		else
			ret = svga_read_linear((riva128->rma.rma_dst_addr - 0x1000000) & 0xffffff, svga);
		break;
	}

	return ret;
}


void
riva128_rma_out(uint16_t addr, uint8_t val, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	svga_t* svga = &riva128->svga;

	addr &= 0xff;

	// nv_riva_log("RIVA 128 RMA write %04X %02X %04X:%08X\n", addr, val, CS, cpu_state.pc);

	switch(addr) {
	case 0x04:
		riva128->rma.rma_dst_addr &= ~0xff;
		riva128->rma.rma_dst_addr |= val;
		break;
	case 0x05:
		riva128->rma.rma_dst_addr &= ~0xff00;
		riva128->rma.rma_dst_addr |= (val << 8);
		break;
	case 0x06:
		riva128->rma.rma_dst_addr &= ~0xff0000;
		riva128->rma.rma_dst_addr |= (val << 16);
		break;
	case 0x07:
		riva128->rma.rma_dst_addr &= ~0xff000000;
		riva128->rma.rma_dst_addr |= (val << 24);
		break;
	case 0x08:
	case 0x0c:
	case 0x10:
	case 0x14:
		riva128->rma.rma_data &= ~0xff;
		riva128->rma.rma_data |= val;
		break;
	case 0x09:
	case 0x0d:
	case 0x11:
	case 0x15:
		riva128->rma.rma_data &= ~0xff00;
		riva128->rma.rma_data |= (val << 8);
		break;
	case 0x0a:
	case 0x0e:
	case 0x12:
	case 0x16:
		riva128->rma.rma_data &= ~0xff0000;
		riva128->rma.rma_data |= (val << 16);
		break;
	case 0x0b:
	case 0x0f:
	case 0x13:
	case 0x17:
		riva128->rma.rma_data &= ~0xff000000;
		riva128->rma.rma_data |= (val << 24);
		if (riva128->rma.rma_dst_addr < 0x1000000)
			riva128_mmio_write_l(riva128->rma.rma_dst_addr & 0xffffff, riva128->rma.rma_data, riva128);
		else
			svga_writel_linear((riva128->rma.rma_dst_addr - 0x1000000) & 0xffffff, riva128->rma.rma_data, svga);
		break;
	}

	if (addr & 0x10)
	riva128->rma.rma_dst_addr+=4;
}


static void
riva128_out(uint16_t addr, uint8_t val, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	svga_t *svga = &riva128->svga;
	uint8_t old;

	if ((addr >= 0x3d0) && (addr <= 0x3d3)) {
	riva128->rma.rma_access_reg[addr & 3] = val;
	if(!(riva128->rma.rma_mode & 1))
		return;
	riva128_rma_out(((riva128->rma.rma_mode & 0xe) << 1) + (addr & 3), riva128->rma.rma_access_reg[addr & 3], riva128);
	}

	if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
	addr ^= 0x60;

	switch (addr) {
	case 0x3D4:
		svga->crtcreg = val;
		return;
	case 0x3D5:
		if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
			return;
		if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
			val = (svga->crtc[7] & ~0x10) | (val & 0x10);
		old = svga->crtc[svga->crtcreg];
		svga->crtc[svga->crtcreg] = val;
		if(svga->seqregs[0x06] == 0x57)
		{
			switch(svga->crtcreg) {
				case 0x1e:
					riva128->read_bank = val;
					if (svga->chain4) svga->read_bank = riva128->read_bank << 15;
					else              svga->read_bank = riva128->read_bank << 13;
					break;
				case 0x1d:
					riva128->write_bank = val;
					if (svga->chain4) svga->write_bank = riva128->write_bank << 15;
					else              svga->write_bank = riva128->write_bank << 13;
					break;
				case 0x19: case 0x1a: case 0x25: case 0x28:
				case 0x2d:
					svga_recalctimings(svga);
					break;
				case 0x38:
					riva128->rma.rma_mode = val & 0xf;
					break;
				case 0x3f:
					riva128->i2c.sda = (val >> 4) & 1;
					riva128->i2c.scl = (val >> 5) & 1;
					break;
			}
		}
		//if (svga->crtcreg > 0x18)
			// pclog("RIVA 128 Extended CRTC write %02X %02x\n", svga->crtcreg, val);
		if (old != val) {
			if ((svga->crtcreg < 0xe) || (svga->crtcreg > 0x10)) {
				svga->fullchange = changeframecount;
				svga_recalctimings(svga);
			}
		}
		break;
	}

	svga_out(addr, val, svga);
}


static uint8_t
riva128_in(uint16_t addr, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	svga_t *svga = &riva128->svga;
	uint8_t temp;

	if ((addr >= 0x3d0) && (addr <= 0x3d3)) {
	if (!(riva128->rma.rma_mode & 1))
		return 0x00;
	return riva128_rma_in(((riva128->rma.rma_mode & 0xe) << 1) + (addr & 3), riva128);
	}

	if (((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && !(svga->miscout&1)) addr ^= 0x60;

	switch (addr) {
	case 0x3D4:
		temp = svga->crtcreg;
		break;
	case 0x3D5:
		switch(svga->crtcreg) {
			case 0x28:
				temp = svga->crtc[0x28] & 0x3f;
				break;
			case 0x34:
				temp = svga->displine & 0xff;
				break;
			case 0x35:
				temp = (svga->displine >> 8) & 7;
				break;
			case 0x3e:
					/* DDC status register */
				temp = (riva128->i2c.sda << 3) | (riva128->i2c.scl << 2);
				break;
			default:
				temp = svga->crtc[svga->crtcreg];
				break;
		}
		break;
	default:
		temp = svga_in(addr, svga);
		break;
	}

	return temp;
}

static void
riva128_vblank_start(svga_t *svga)
{
	riva128_t *riva128 = (riva128_t *)svga->p;

	riva128->pgraph.intr_0 |= (1 << 8);

	riva128_pmc_recompute_intr(riva128);
}

static void
riva128_recalctimings(svga_t *svga)
{
	riva128_t *riva128 = (riva128_t *)svga->p;

	svga->ma_latch += (svga->crtc[0x19] & 0x1f) << 16;
	svga->rowoffset += (svga->crtc[0x19] & 0xe0) << 3;
	if (svga->crtc[0x25] & 0x01) svga->vtotal      += 0x400;
	if (svga->crtc[0x25] & 0x02) svga->dispend     += 0x400;
	if (svga->crtc[0x25] & 0x04) svga->vblankstart += 0x400;
	if (svga->crtc[0x25] & 0x08) svga->vsyncstart  += 0x400;
	if (svga->crtc[0x25] & 0x10) svga->htotal      += 0x100;
	if (svga->crtc[0x2d] & 0x01) svga->hdisp       += 0x100;	
	/* The effects of the large screen bit seem to just be doubling the row offset.
	   However, these large modes still don't work. Possibly core SVGA bug? It does report 640x2 res after all. */

	switch(svga->crtc[0x28] & 3) {
	case 1:
		svga->bpp = 8;
		svga->lowres = 0;
		svga->render = svga_render_8bpp_highres;
		break;
	case 2:
		svga->bpp = 16;
		svga->lowres = 0;
		svga->render = svga_render_16bpp_highres;
		break;
	case 3:
		svga->bpp = 32;
		svga->lowres = 0;
		svga->render = svga_render_32bpp_highres;
		break;
	}

	uint64_t freq = 13500000;
	int m_m = riva128->pramdac.mpll & 0xff;
	int m_n = (riva128->pramdac.mpll >> 8) & 0xff;
	int m_p = (riva128->pramdac.mpll >> 16) & 7;

	if(m_n == 0) m_n = 1;
	if(m_m == 0) m_m = 1;

	freq = (freq * m_n) / (1 << m_p) / m_m;
	riva128->mtime = (TIMER_USEC * 100000000) / freq;
	timer_enable(&riva128->mtimer);

	freq = 13500000;
	int nv_m = riva128->pramdac.nvpll & 0xff;
	int nv_n = (riva128->pramdac.nvpll >> 8) & 0xff;
	int nv_p = (riva128->pramdac.nvpll >> 16) & 7;

	if(nv_n == 0) nv_n = 1;
	if(nv_m == 0) nv_m = 1;

	freq = (freq * nv_n) / (1 << nv_p) / nv_m;
	riva128->nvtime = (TIMER_USEC * 100000000) / freq;
	timer_enable(&riva128->nvtimer);
}


static void
*riva128_init(const device_t *info)
{
	riva128_t *riva128 = malloc(sizeof(riva128_t));
	svga_t *svga;
	wchar_t *romfn = BIOS_RIVA128_PATH;
	memset(riva128, 0, sizeof(riva128_t));
	svga = &riva128->svga;

	riva128->vram_size = device_get_config_int("memory") << 20;
	riva128->vram_mask = riva128->vram_size - 1;

	rom_init(&riva128->bios_rom, romfn, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

	svga_init(&riva128->svga, riva128, riva128->vram_size,
		  riva128_recalctimings, riva128_in, riva128_out,
		  NULL, NULL);

	svga->decode_mask = riva128->vram_mask;

	mem_mapping_add(&riva128->mmio_mapping, 0, 0, riva128_mmio_read, riva128_mmio_read_w, riva128_mmio_read_l, riva128_mmio_write, riva128_mmio_write_w, riva128_mmio_write_l,  NULL, MEM_MAPPING_EXTERNAL, riva128);
	mem_mapping_disable(&riva128->mmio_mapping);
	mem_mapping_add(&riva128->linear_mapping, 0, 0, svga_read_linear, svga_readw_linear, svga_readl_linear, svga_write_linear, svga_writew_linear, svga_writel_linear,  NULL, MEM_MAPPING_EXTERNAL, &riva128->svga);
	mem_mapping_disable(&riva128->linear_mapping);
	mem_mapping_add(&riva128->ramin_mapping, 0, 0, riva128_ramin_read, riva128_ramin_read_w, riva128_ramin_read_l, riva128_ramin_write, riva128_ramin_write_w, riva128_ramin_write_l,  NULL, MEM_MAPPING_EXTERNAL, riva128);
	mem_mapping_disable(&riva128->ramin_mapping);

	mem_mapping_set_handler(&svga->mapping, svga_read, svga_readw, svga_readl, svga_write, svga_writew, svga_writel);

	svga->vblank_start = riva128_vblank_start;

	io_sethandler(0x03c0, 0x0020, riva128_in, NULL, NULL, riva128_out, NULL, NULL, riva128);

	riva128->card = pci_add_card(PCI_ADD_VIDEO, riva128_pci_read, riva128_pci_write, riva128);

	riva128->pci_regs[0x04] = 0x08;
	riva128->pci_regs[0x07] = 0x02;

	riva128->pci_regs[0x2c] = 0xd2;
	riva128->pci_regs[0x2d] = 0x12;
	riva128->pci_regs[0x2e] = 0x00;
	riva128->pci_regs[0x2f] = 0x03;

	riva128->pci_regs[0x30] = 0x00;
	riva128->pci_regs[0x32] = 0x0c;
	riva128->pci_regs[0x33] = 0x00;

	riva128->pfifo.ramro = 0x1e00;
	riva128->pfifo.ramro_addr = 0x1e00;
	riva128->pfifo.ramro_size = 512;
	riva128->pfifo.runout_get = riva128->pfifo.runout_put = 0;
	riva128->pfifo.caches[1].put = riva128->pfifo.caches[1].get = 0;

	timer_add(&riva128->nvtimer, riva128_nvclk_poll, riva128, 0);
	timer_add(&riva128->mtimer, riva128_mclk_poll, riva128, 0);

	return riva128;
}


static int
riva128_available(void)
{
	return rom_present(BIOS_RIVA128_PATH);
}


void
riva128_close(void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	
	svga_close(&riva128->svga);
	
	free(riva128);
}


void
riva128_speed_changed(void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	
	svga_recalctimings(&riva128->svga);
}


void
riva128_force_redraw(void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	riva128->svga.fullchange = changeframecount;
}


static const device_config_t riva128_config[] =
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

const device_t riva128_pci_device =
{
	"nVidia RIVA 128 (PCI)",
	DEVICE_PCI,
	RIVA128_DEVICE_ID,
	riva128_init,
	riva128_close, 
	NULL,
	riva128_available,
	riva128_speed_changed,
	riva128_force_redraw,
	riva128_config
};